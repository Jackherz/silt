#include <silt/GrainVoice.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double gainSmoothingSeconds = 0.03;
constexpr double autoGainSeconds = 0.05;
constexpr double maxSpraySeconds = 1.0;
constexpr int minGrainSamples = 64;     // shorter grains are click trains (02 §8)
constexpr double edgeGuardFrames = 2.0; // keeps cubic reads' neighbours inside the buffer
constexpr double maxLoopFadeSeconds = 0.01;

double wrapFrames (double pos, int numFrames) noexcept
{
    if (pos >= 0.0 && pos < numFrames)
        return pos;
    pos = std::fmod (pos, (double) numFrames);
    return pos < 0.0 ? pos + numFrames : pos;
}

float perSample (double ms, double sampleRate) noexcept
{
    return (float) (1.0 / std::max (1.0, ms * 0.001 * sampleRate));
}
} // namespace

void GrainVoice::prepare (double rate) noexcept
{
    sampleRate = rate;
    gainSmoothing = (float) (1.0 - std::exp (-1.0 / (gainSmoothingSeconds * sampleRate)));
    autoGainSmoothing = (float) (1.0 - std::exp (-1.0 / (autoGainSeconds * sampleRate)));
    reset();
}

void GrainVoice::reset() noexcept
{
    for (auto& g : grains)
        g = {};
    activeGrains = 0;
    envStage = EnvStage::idle;
    envLevel = 0.0f;
    spawning = false;
    currentNote = -1;
}

GrainVoice::Region GrainVoice::computeRegion (const GrainParams& p, const SampleBuffer& buffer) noexcept
{
    const double frames = buffer.getNumFrames();
    if (p.live)   // the live ring has no region: grains place themselves by delay
        return { 0.0, frames };
    const double begin = std::clamp (p.start, 0.0, 1.0) * (frames - 1.0);
    const double end = std::min (frames, begin + std::max (1.0, std::clamp (p.length, 0.0, 1.0) * frames));
    const auto snapped = snapRegion (begin, end, buffer.getNumFrames(), p.snap,
                                     buffer.getZeroCrossings(), buffer.getTransients());
    return { snapped.begin, snapped.end };
}

// Slice mode: the whole region cut into this voice's slice. Equal slices,
// or from the region's start to its first transient, transient to transient,
// the last to the region's end. Keys wrap round the slices.
GrainVoice::Region GrainVoice::regionFor (const GrainParams& p, const SampleBuffer& buffer) const noexcept
{
    const auto whole = computeRegion (p, buffer);
    if (slice < 0 || p.live)
        return whole;

    if (p.sliceTable != nullptr && p.sliceTableSize > 0)
    {
        // MANUAL: from each mark to the next; the last to the region's end
        // (or the sample's, if the region ends before it).
        const double frames = buffer.getNumFrames();
        const int count = p.sliceTableSize;
        const int i = ((slice % count) + count) % count;
        const double begin = std::clamp ((double) p.sliceTable[i], 0.0, 1.0) * (frames - 1.0);
        double end = i + 1 < count ? std::clamp ((double) p.sliceTable[i + 1], 0.0, 1.0) * frames
                                   : (whole.end > begin + 1.0 ? whole.end : frames);
        return { begin, std::max (begin + 1.0, end) };
    }

    if (p.sliceTransients)
    {
        constexpr double minimum = 256.0;   // a hit this close to the last starts no new slice
        std::array<double, 129> edges {};
        int count = 0;
        edges[(size_t) count++] = whole.begin;
        for (const int t : buffer.getTransients())
            if ((double) t > edges[(size_t) count - 1] + minimum && (double) t < whole.end - minimum && count < (int) edges.size() - 1)
                edges[(size_t) count++] = (double) t;
        const int i = ((slice % count) + count) % count;
        return { edges[(size_t) i], i + 1 < count ? edges[(size_t) i + 1] : whole.end };
    }

    const int count = std::clamp (p.sliceCount, 1, 128);
    const int i = ((slice % count) + count) % count;
    const double each = (whole.end - whole.begin) / count;
    return { whole.begin + each * i, whole.begin + each * (i + 1) };
}

void GrainVoice::noteOn (int note, float vel, const SampleBuffer& buffer, const GrainParams& p) noexcept
{
    slice = p.slice && ! p.live ? note - p.rootNote : -1;
    const auto region = regionFor (p, buffer);
    currentNote = note;
    velocity = vel;
    noteRatio = slice >= 0 ? 1.0 : semitonesToRatio (note - p.rootNote);   // slices play at the sample's pitch
    playhead = p.scan < 0.0 ? region.end - 1.0 : region.begin;   // reverse scans start at the end
    samplesToNextGrain = 0.0;      // first grain on the note itself
    patternStep = 0;
    headCounter = 0;
    firstGrainPending = true;
    spawning = true;
    envStage = EnvStage::attack;   // from the current level: retriggers don't click
    autoGain = outputGain = gainEstimate (p, &buffer).estimate;
    sourcePower = envelopeWeight = sumPower = 0.0f;
}

double GrainVoice::pitchRatio (const GrainParams& p, const SampleBuffer& buffer) const noexcept
{
    return noteRatio * semitonesToRatio (p.pitchSemitones) * (buffer.getSampleRate() / sampleRate);
}

// This grain's pitch above the root note, per the Pitch page: transposition,
// random offset and pattern step, then snapped to the scale (plan gap 5: the
// parts are added first and snapped once, so intervals come out right).
double GrainVoice::nextGrainSemitones (const GrainParams& p) noexcept
{
    double semitones = (slice >= 0 ? 0 : currentNote - p.rootNote) + p.pitchSemitones;
    if (p.randomPitch > 0.0)
        semitones += random.nextBipolar() * p.randomPitch;
    if (p.pattern != nullptr && p.patternSteps > 0)
    {
        semitones += p.pattern[patternStep % p.patternSteps] * p.patternDepth;
        patternStep = (patternStep + 1) % p.patternSteps;
    }
    return snapToScale (p.rootNote + semitones, p.scale, p.key) - p.rootNote;
}

// Grains only ever read the region. In LOOP mode they read through the loop
// point, so any length works; otherwise a grain longer than the region is
// shortened to fit it.
int GrainVoice::grainLength (const GrainParams& p, const Region& region, double ratio) const noexcept
{
    const int wanted = std::max (minGrainSamples, (int) (p.sizeMs * 0.001 * sampleRate));
    if (p.live)   // a third of the ring, so a grain always fits between old and new audio
        return std::max (minGrainSamples, std::min (wanted, (int) ((region.end - region.begin) / (3.0 * ratio))));
    if (p.endMode == EndMode::loop)
        return wanted;
    const double usable = std::max (1.0, region.end - region.begin - 2.0 * edgeGuardFrames);
    return std::max (minGrainSamples, std::min (wanted, (int) (usable / ratio)));
}

void GrainVoice::noteOff() noexcept
{
    if (envStage == EnvStage::idle || envStage == EnvStage::release)
        return;
    envStage = EnvStage::release;
    releaseStep = 0.0f;            // set from the level on the next sample
}

int GrainVoice::collectViews (GrainView* out, int max, std::uint8_t voiceIndex) const noexcept
{
    int n = 0;
    const float voiceLevel = envLevel * velocity;
    for (size_t i = 0; i < grains.size() && n < max; ++i)
    {
        const auto& g = grains[i];
        if (g.remaining <= 0 || g.source == nullptr)
            continue;
        auto& v = out[n++];
        v.start = g.spanStart;
        v.end = g.spanEnd;
        v.position = (float) (g.readPos / g.source->getNumFrames());
        v.level = g.lastEnvelope * voiceLevel;
        v.pan = g.pan;
        v.voice = voiceIndex;
        v.slot = (std::uint8_t) i;
        v.reversed = g.increment < 0.0;
    }
    return n;
}

int GrainVoice::countGrainsUsing (const SampleBuffer* buffer) const noexcept
{
    int count = 0;
    for (const auto& g : grains)
        if (g.remaining > 0 && g.source == buffer)
            ++count;
    return count;
}

// Overlap normalisation (plan gap 10): the starting estimate and the limits for
// the auto-gain in render(). With `overlap` grains sounding at once,
// envelopes sum to overlap × their mean. Grains reading the same audio at the
// same moment add in phase; grains reading it at different delays add like
// noise (rms ∝ √(overlap × mean square)). Hann: mean 0.5, mean square 0.375;
// a flat envelope (fade 0): both 1.
//
// How far apart the overlapping grains read: a grain started t samples ago has
// drifted (ratio − scan) × t frames from one started now, so across one grain
// length the spread is |ratio − scan| × length. Spray adds its scatter. A few
// milliseconds of spread is enough to decorrelate real audio. In HOLD / STOP a
// short region leaves spray no room to scatter.
GrainVoice::GainEstimate GrainVoice::gainEstimate (const GrainParams& p, const SampleBuffer* buffer) const noexcept
{
    double sizeMs = p.sizeMs, room = 1.0, driftMs = 0.0;
    if (buffer != nullptr)
    {
        const auto region = regionOf (p, *buffer);
        const double rateFactor = buffer->getSampleRate() / sampleRate;
        const double ratio = pitchRatio (p, *buffer);
        const int length = grainLength (p, region, ratio);
        const double span = length * ratio;
        sizeMs = length * 1000.0 / sampleRate;
        const double scan = p.live ? 1.0 : p.scan;   // live grains follow the write head
        driftMs = std::abs (ratio - scan * rateFactor) * length * 1000.0 / buffer->getSampleRate();
        room = p.live || p.endMode == EndMode::loop ? 1.0
                                          : std::clamp ((region.end - region.begin - span) / std::max (1.0, span), 0.0, 1.0);
    }

    constexpr double decorrelatedMs = 3.0;
    const double fade = std::clamp (p.fade, 0.0, 1.0);
    const double mean = 1.0 - 0.5 * fade;
    const double meanSquare = 1.0 - 0.625 * fade;
    const double overlap = p.grainCount > 0 ? (double) p.grainCount : sizeMs * 0.001 * p.rateHz;
    const double coherent = 1.0 / std::max (1.0, overlap * mean);
    const double incoherent = 1.0 / std::max (1.0, std::sqrt (overlap * meanSquare));
    const double spread = std::clamp (p.spray * 4.0 * room + driftMs / decorrelatedMs, 0.0, 1.0);
    return { (float) (coherent + spread * (incoherent - coherent)), (float) coherent, (float) incoherent };
}

float GrainVoice::nextEnvelope (const GrainParams& p) noexcept
{
    const float sustain = (float) std::clamp (p.sustain, 0.0, 1.0);

    switch (envStage)
    {
        case EnvStage::attack:
            envLevel += perSample (p.attackMs, sampleRate);
            if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = EnvStage::decay; }
            break;
        case EnvStage::decay:
            envLevel -= (1.0f - sustain) * perSample (p.decayMs, sampleRate);
            if (envLevel <= sustain) { envLevel = sustain; envStage = EnvStage::sustain; }
            break;
        case EnvStage::sustain:
            envLevel += 0.001f * (sustain - envLevel);   // follows sustain changes without zipper noise
            break;
        case EnvStage::release:
            if (releaseStep == 0.0f)
                releaseStep = std::max (1.0e-7f, envLevel * perSample (p.releaseMs, sampleRate));
            envLevel -= releaseStep;
            if (envLevel <= 0.0f)
            {
                envLevel = 0.0f;
                envStage = EnvStage::idle;
                spawning = false;
            }
            break;
        case EnvStage::idle:
            break;
    }
    return envLevel;
}

// Where a grain starts for the current playhead. A grain must fit inside the
// region, so its start can only range over the region minus one grain; the
// playhead's place in the region is scaled onto that range. Clamping instead
// would park every grain at the same spot while the playhead crosses the
// region's last grain-length, and the sound would freeze there.
double GrainVoice::grainStartFor (const Region& region, double spanFrames) const noexcept
{
    const double regionFrames = region.end - region.begin;
    const double range = std::max (0.0, regionFrames - 2.0 * edgeGuardFrames - spanFrames);
    const double along = std::clamp ((playhead - region.begin) / std::max (1.0, regionFrames), 0.0, 1.0);
    return region.begin + edgeGuardFrames + along * range;
}

// Spray offset from grainStartFor()'s position, wrapped so the grain stays in
// the same start range.
double GrainVoice::scatterWithin (const Region& region, double spanFrames, double scatter) const noexcept
{
    const double range = std::max (0.0, region.end - region.begin - 2.0 * edgeGuardFrames - spanFrames);
    if (range < 1.0)
        return 0.0;
    const double base = grainStartFor (region, spanFrames) - (region.begin + edgeGuardFrames);
    return std::fmod (std::fmod (base + scatter, range) + range, range) - base;
}

// Moves the playhead one output sample. Returns false once a `stop` playhead
// runs out of its region.
bool GrainVoice::advancePlayhead (const GrainParams& p, const SampleBuffer& buffer) noexcept
{
    const auto region = regionOf (p, buffer);
    playhead += p.scan * (buffer.getSampleRate() / sampleRate);

    if (playhead >= region.begin && playhead < region.end)
        return true;

    switch (p.endMode)
    {
        case EndMode::loop:
        {
            const double span = region.end - region.begin;
            playhead = region.begin + wrapFrames (playhead - region.begin, (int) std::max (1.0, span));
            return true;
        }
        case EndMode::hold: playhead = std::clamp (playhead, region.begin, region.end - 1.0); return true;
        case EndMode::stop: break;
    }
    return false;
}

// Live mode: where a grain starts in the ring. Aimed liveDelayMs (plus spray)
// behind the write head, then kept where the whole read is safe (plan gap 1):
// forward reads at a ratio above 1 gain on the write head, reversed reads
// start at the far end of their span, and the write head overwrites the
// oldest audio as the grain plays, so neither end of the read may cross it.
double GrainVoice::liveStart (const GrainParams& p, const SampleBuffer& ring, int length, double ratio, double scatter) const noexcept
{
    constexpr double guard = 64.0 + edgeGuardFrames;
    const double span = length * ratio;
    const double newest = p.liveNow - guard - std::max (span, (ratio - 1.0) * length);
    const double oldest = p.liveNow - ring.getNumFrames() + length + p.liveMargin + guard;
    const double aimed = p.liveNow - p.liveDelayMs * 0.001 * ring.getSampleRate() + scatter;
    return std::clamp (aimed, oldest, std::max (oldest, newest));
}

void GrainVoice::spawnGrain (const GrainParams& p, const SampleBuffer& buffer) noexcept
{
    // Grains mode keeps a ceiling of twice the count: while Size shrinks, the
    // longer grains already playing would otherwise block new ones and leave
    // gaps. A full pool drops the grain rather than cutting one off mid-envelope.
    if (p.grainCount > 0 && activeGrains >= 2 * p.grainCount)
        return;
    auto free = std::find_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.remaining <= 0; });
    if (free == grains.end())
        return;

    const int frames = buffer.getNumFrames();
    const double semitones = nextGrainSemitones (p);
    const double ratio = semitonesToRatio (semitones) * (buffer.getSampleRate() / sampleRate);
    lastGrainSemitones = semitones;

    // Grains only ever read the region: what's shaded on screen is all you hear.
    // A grain longer than the region is shortened to fit it, and spray scatter
    // wraps around inside it. Staying clear of the region's edges also keeps
    // every read off the seam between the buffer's last and first frame.
    const auto region = regionOf (p, buffer);
    const int length = grainLength (p, region, ratio);

    double scatter = random.nextBipolar() * p.spray * maxSpraySeconds * buffer.getSampleRate();
    const bool looping = p.endMode == EndMode::loop;
    const double regionFrames = std::max (1.0, region.end - region.begin);

    // Heads take turns: each grain goes to the next. At full spread they sit
    // evenly around the region (head k at k/heads of the way through), since
    // the region wraps; in Live mode they sit further back in time, up to 1 s.
    if (p.heads > 1)
    {
        const int head = headCounter++ % p.heads;
        const double spread = std::clamp (p.headSpread, 0.0, 1.0);
        scatter += p.live ? -(double) head / (double) (p.heads - 1) * spread * buffer.getSampleRate()
                          : (double) head / (double) p.heads * spread * regionFrames;
    }

    // LOOP: grains start at the playhead (spray wraps around the loop), so Scan 1
    // plays the region at its own speed. HOLD / STOP: grains must fit before the
    // region end (grainStartFor).
    double start = looping ? region.begin + wrapFrames (playhead + scatter - region.begin, (int) regionFrames)
                           : grainStartFor (region, length * ratio) + scatterWithin (region, length * ratio, scatter);
    if (p.live)
        start = liveStart (p, buffer, length, ratio, scatter);
    const bool reversed = random.next01() < p.reverseChance;
    const auto span = grainReadSpan (start, length, ratio);

    // Random pan as balance, so a centred grain keeps both channels at full level.
    const float pan = random.nextBipolar() * (float) std::clamp (p.randomPan, 0.0, 1.0);
    const float amp = 1.0f - random.next01() * (float) std::clamp (p.randomAmp, 0.0, 1.0);

    Grain& g = *free;
    g.source = &buffer;
    g.loops = false;
    g.backwardWraps = 0;
    g.readPos = wrapFrames (reversed ? span.hi : span.lo, frames);

    if (looping && ! p.live)
    {
        const double fade = std::min (maxLoopFadeSeconds * buffer.getSampleRate(), 0.25 * regionFrames);
        g.loopBegin = region.begin;
        g.loopEnd = region.end;
        g.fadeLength = std::max (1.0, fade);
        g.loopLength = std::max (1.0, regionFrames - fade);
        g.fadeStart = region.end - fade;
        g.loops = span.hi > g.fadeStart;

        if (g.loops && reversed)
        {
            // Start where the forward read would have ended, after its wraps.
            double hi = span.hi;
            while (hi >= g.loopEnd)
            {
                hi -= g.loopLength;
                ++g.backwardWraps;
            }
            g.readPos = hi;
        }
    }
    g.increment = reversed ? -ratio : ratio;
    g.phase = 0.0;
    g.phaseInc = 1.0 / length;
    g.remaining = length;
    g.pan = pan;
    g.gainL = amp * std::min (1.0f, 1.0f - pan);
    g.gainR = amp * std::min (1.0f, 1.0f + pan);
    g.envelope.set (p.fade, p.slant, p.ease, length, p.declickMs * 0.001 * sampleRate);
    g.interpolation = p.interpolation;
    ++activeGrains;
    ++grainsStarted;
    lastGrainStart = span.lo;

    // What the UI draws: the span inside the region (loops show up to the loop end).
    const double shown = p.live ? span.hi : std::min (span.hi, looping ? region.end : (double) frames);
    g.spanStart = (float) (wrapFrames (span.lo, frames) / frames);
    g.spanEnd = g.spanStart + (float) ((shown - span.lo) / frames);
    g.lastEnvelope = 0.0f;
}

// One channel of a grain's current frame. In the loop's fade zone the audio
// crossfades into the frames loopLength earlier, which is where the read
// continues after the loop point, so the seam never clicks.
float GrainVoice::readGrain (const Grain& g, int channel) const noexcept
{
    const float* data = g.source->getReadPointer (channel);
    const int frames = g.source->getNumFrames();
    const float here = readFrame (data, frames, g.readPos, g.interpolation);

    if (! g.loops || g.readPos < g.fadeStart)
        return here;

    const float a = (float) std::clamp ((g.readPos - g.fadeStart) / g.fadeLength, 0.0, 1.0);
    const float there = readFrame (data, frames, g.readPos - g.loopLength, g.interpolation);
    return here + a * (there - here);
}

void GrainVoice::render (float* left, float* right, int numSamples,
                         const GrainParams& p, const SampleBuffer* buffer) noexcept
{
    if (! isActive())
        return;

    regionBuffer = buffer;
    if (buffer != nullptr)
        blockRegion = regionFor (p, *buffer);

    // Auto-gain (after Mutable Instruments Beads): whether overlapping grains add
    // in phase or like noise depends on the audio as much as the settings (a
    // tone stays in phase across delays that scramble a voice), so measure it.
    // The sum is steered to the power of the audio the grains read, within
    // limits around the in-phase and noise-like estimates.
    const auto estimate = gainEstimate (p, buffer);
    const float lowest = 0.7f * estimate.coherent, highest = 1.4f * estimate.incoherent;
    double interval = sampleRate / std::clamp (p.rateHz, 0.1, 1000.0);
    if (p.grainCount > 0 && buffer != nullptr)
    {
        const auto region = regionOf (p, *buffer);
        interval = std::max (1.0, grainLength (p, region, pitchRatio (p, *buffer)) / (double) p.grainCount);
    }
    const bool grid = p.gridSync && p.ppqPerSample > 0.0 && p.gridDivision > 0.0;

    // Lock to the grid: on first use, a new division, or when the host position
    // isn't where the last call ended (a locate or loop jump). Carrying the next
    // grid line across calls means no line is missed at a block boundary.
    if (grid)
    {
        const bool jumped = std::abs (p.gridPpq - expectedPpq) > 2.0 * p.ppqPerSample + 1.0e-9;
        if (! gridLocked || jumped || p.gridDivision != lockedDivision)
        {
            nextGridPpq = std::ceil (p.gridPpq / p.gridDivision - 1.0e-9) * p.gridDivision;
            lockedDivision = p.gridDivision;
            gridLocked = true;
        }
        expectedPpq = p.gridPpq + numSamples * p.ppqPerSample;
    }
    else
    {
        gridLocked = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (spawning && buffer != nullptr)
        {
            if (grid)
            {
                const double ppq = p.gridPpq + i * p.ppqPerSample;
                if (firstGrainPending)
                {
                    spawnGrain (p, *buffer);
                    lastSpawnPpq = ppq;
                }
                else if (ppq >= nextGridPpq - 1.0e-9)
                {
                    // Skip a grid line that falls right after the note's own grain.
                    if (ppq - lastSpawnPpq > 0.25 * p.gridDivision)
                        spawnGrain (p, *buffer);
                    lastSpawnPpq = ppq;
                    while (nextGridPpq <= ppq + 1.0e-9)
                        nextGridPpq += p.gridDivision;
                }
                samplesToNextGrain = 0.0;   // free-running resumes at once if the grid goes away
            }
            else if (samplesToNextGrain <= 0.0)
            {
                spawnGrain (p, *buffer);
                // Jitter spreads the next start over 0…2 intervals at full amount,
                // keeping the average rate.
                const double jitter = std::clamp (p.jitter, 0.0, 1.0);
                samplesToNextGrain += interval * (1.0 + jitter * random.nextBipolar());
            }
            firstGrainPending = false;
            samplesToNextGrain -= 1.0;

            if (! p.live && ! advancePlayhead (p, *buffer))
            {
                spawning = false;
                noteOff();
            }
        }

        float accL = 0.0f, accR = 0.0f, grainPower = 0.0f, grainWeight = 0.0f;

        if (activeGrains > 0)
        {
            for (auto& g : grains)
            {
                if (g.remaining <= 0)
                    continue;

                const float env = g.envelope (window, g.phase);
                g.lastEnvelope = env;
                const float sL = g.gainL * readGrain (g, 0);
                const float sR = g.gainR * readGrain (g, 1);
                accL += env * sL;
                accR += env * sR;
                grainPower += env * 0.5f * (sL * sL + sR * sR);
                grainWeight += env;

                g.readPos += g.increment;
                if (! g.loops)
                    g.readPos = wrapFrames (g.readPos, g.source->getNumFrames());
                else if (g.increment > 0.0 && g.readPos >= g.loopEnd)
                    g.readPos -= g.loopLength;
                else if (g.increment < 0.0 && g.backwardWraps > 0 && g.readPos < g.loopBegin + g.fadeLength)
                {
                    g.readPos += g.loopLength;
                    --g.backwardWraps;
                }
                g.phase += g.phaseInc;
                if (--g.remaining == 0)
                    --activeGrains;
            }
        }

        sourcePower += autoGainSmoothing * (grainPower - sourcePower);
        envelopeWeight += autoGainSmoothing * (grainWeight - envelopeWeight);
        sumPower += autoGainSmoothing * (0.5f * (accL * accL + accR * accR) - sumPower);
        if (envelopeWeight > 0.05f && sourcePower > 1.0e-9f * envelopeWeight)   // hold through silence
            autoGain = std::clamp (std::sqrt (sourcePower / envelopeWeight / std::max (sumPower, 1.0e-12f)), lowest, highest);

        outputGain += gainSmoothing * (autoGain - outputGain);
        const float amp = nextEnvelope (p) * velocity * outputGain;
        left[i] += accL * amp;
        right[i] += accR * amp;

        // Released and silent: the remaining grains can't be heard any more.
        if (envStage == EnvStage::idle && activeGrains > 0)
        {
            for (auto& g : grains)
                g.remaining = 0;
            activeGrains = 0;
        }
    }

    // The UI shows where grains start: the playhead in LOOP mode, otherwise the
    // playhead scaled onto the range a whole grain fits in (grainStartFor).
    if (buffer != nullptr)
    {
        const auto region = regionOf (p, *buffer);
        const double ratio = pitchRatio (p, *buffer);
        const double at = p.live ? wrapFrames (p.liveNow + numSamples - p.liveDelayMs * 0.001 * buffer->getSampleRate(), buffer->getNumFrames())
                        : p.endMode == EndMode::loop ? playhead
                                                     : grainStartFor (region, grainLength (p, region, ratio) * ratio);
        playheadPosition = (float) (at / buffer->getNumFrames());
    }
}

} // namespace silt
