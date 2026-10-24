#include <silt/GrainVoice.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double gainSmoothingSeconds = 0.03;
constexpr double maxSpraySeconds = 1.0;
constexpr int minGrainSamples = 64;     // shorter grains are click trains (02 §8)
constexpr double edgeGuardFrames = 2.0; // keeps cubic reads' neighbours inside the buffer

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

GrainVoice::Region GrainVoice::regionOf (const GrainParams& p, const SampleBuffer& buffer) noexcept
{
    const double frames = buffer.getNumFrames();
    const double begin = std::clamp (p.start, 0.0, 1.0) * (frames - 1.0);
    const double end = std::min (frames, begin + std::max (1.0, std::clamp (p.length, 0.0, 1.0) * frames));
    return { begin, end };
}

void GrainVoice::noteOn (int note, float vel, const SampleBuffer& buffer, const GrainParams& p) noexcept
{
    const auto region = regionOf (p, buffer);
    currentNote = note;
    velocity = vel;
    noteRatio = semitonesToRatio (note - p.rootNote);
    playhead = p.scan < 0.0 ? region.end - 1.0 : region.begin;   // reverse scans start at the end
    samplesToNextGrain = 0.0;      // first grain on the note itself
    spawning = true;
    envStage = EnvStage::attack;   // from the current level: retriggers don't click
    outputGain = targetGain (p);
}

void GrainVoice::noteOff() noexcept
{
    if (envStage == EnvStage::idle || envStage == EnvStage::release)
        return;
    envStage = EnvStage::release;
    releaseStep = 0.0f;            // set from the level on the next sample
}

int GrainVoice::countGrainsUsing (const SampleBuffer* buffer) const noexcept
{
    int count = 0;
    for (const auto& g : grains)
        if (g.remaining > 0 && g.source == buffer)
            ++count;
    return count;
}

// Overlap normalisation (plan gap 10). With `overlap` grains sounding at once,
// envelopes sum to overlap × their mean. With no spray the grains are
// near-identical and add in phase; with spray they add like noise
// (rms ∝ √(overlap × mean square)). Hann: mean 0.5, mean square 0.375;
// a flat envelope (fade 0): both 1.
float GrainVoice::targetGain (const GrainParams& p) const noexcept
{
    const double fade = std::clamp (p.fade, 0.0, 1.0);
    const double mean = 1.0 - 0.5 * fade;
    const double meanSquare = 1.0 - 0.625 * fade;
    const double overlap = p.sizeMs * 0.001 * p.rateHz;
    const double coherent = 1.0 / std::max (1.0, overlap * mean);
    const double incoherent = 1.0 / std::max (1.0, std::sqrt (overlap * meanSquare));
    const double spread = std::clamp (p.spray * 4.0, 0.0, 1.0);
    return (float) (coherent + spread * (incoherent - coherent));
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

void GrainVoice::spawnGrain (const GrainParams& p, const SampleBuffer& buffer) noexcept
{
    // A full pool drops the grain rather than cutting one off mid-envelope.
    auto free = std::find_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.remaining <= 0; });
    if (free == grains.end())
        return;

    const int frames = buffer.getNumFrames();
    const double rateFactor = buffer.getSampleRate() / sampleRate;
    const double ratio = noteRatio * semitonesToRatio (p.pitchSemitones) * rateFactor;
    const int length = std::max (minGrainSamples, (int) (p.sizeMs * 0.001 * sampleRate));

    const double scatter = random.nextBipolar() * p.spray * maxSpraySeconds * buffer.getSampleRate();
    double start = playhead + scatter;
    const bool reversed = random.next01() < p.reverseChance;
    auto span = grainReadSpan (start, length, ratio);

    // Keep the whole read inside the buffer, so no grain crosses the seam
    // between its last and first frame (that would click unless the sample
    // happens to loop cleanly). Only a grain longer than the buffer wraps.
    const double spanFrames = span.hi - span.lo;
    if (spanFrames < frames - 2.0 * edgeGuardFrames)
    {
        start = std::clamp (start, edgeGuardFrames, frames - edgeGuardFrames - spanFrames);
        span = grainReadSpan (start, length, ratio);
    }

    // Stereo scatter as balance, so a centred grain keeps both channels at full level.
    const float pan = random.nextBipolar() * (float) std::clamp (p.spray, 0.0, 1.0);
    const float amp = 1.0f - random.next01() * (float) std::clamp (p.randomAmp, 0.0, 1.0);

    Grain& g = *free;
    g.source = &buffer;
    g.readPos = wrapFrames (reversed ? span.hi : span.lo, frames);
    g.increment = reversed ? -ratio : ratio;
    g.phase = 0.0;
    g.phaseInc = 1.0 / length;
    g.remaining = length;
    g.gainL = amp * std::min (1.0f, 1.0f - pan);
    g.gainR = amp * std::min (1.0f, 1.0f + pan);
    g.envelope.set (p.fade, p.slant, p.ease, length, p.declickMs * 0.001 * sampleRate);
    g.interpolation = p.interpolation;
    ++activeGrains;
}

void GrainVoice::render (float* left, float* right, int numSamples,
                         const GrainParams& p, const SampleBuffer* buffer) noexcept
{
    if (! isActive())
        return;

    const float gainTarget = targetGain (p);
    const double interval = sampleRate / std::clamp (p.rateHz, 0.1, 1000.0);

    for (int i = 0; i < numSamples; ++i)
    {
        if (spawning && buffer != nullptr)
        {
            if (samplesToNextGrain <= 0.0)
            {
                spawnGrain (p, *buffer);
                samplesToNextGrain += interval;
            }
            samplesToNextGrain -= 1.0;

            if (! advancePlayhead (p, *buffer))
            {
                spawning = false;
                noteOff();
            }
        }

        float accL = 0.0f, accR = 0.0f;

        if (activeGrains > 0)
        {
            for (auto& g : grains)
            {
                if (g.remaining <= 0)
                    continue;

                const int frames = g.source->getNumFrames();
                const float env = g.envelope (window, g.phase);
                accL += env * g.gainL * readFrame (g.source->getReadPointer (0), frames, g.readPos, g.interpolation);
                accR += env * g.gainR * readFrame (g.source->getReadPointer (1), frames, g.readPos, g.interpolation);

                g.readPos = wrapFrames (g.readPos + g.increment, frames);
                g.phase += g.phaseInc;
                if (--g.remaining == 0)
                    --activeGrains;
            }
        }

        outputGain += gainSmoothing * (gainTarget - outputGain);
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

    if (buffer != nullptr)
        playheadPosition = (float) (playhead / buffer->getNumFrames());
}

} // namespace silt
