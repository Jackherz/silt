#include <silt/GrainVoice.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double attackSeconds = 0.005;
constexpr double releaseSeconds = 0.2;
constexpr double gainSmoothingSeconds = 0.03;
constexpr double maxSpraySeconds = 1.0;
constexpr int minGrainSamples = 64;     // shorter grains are click trains (02 §8)

double wrapFrames (double pos, int numFrames) noexcept
{
    if (pos >= 0.0 && pos < numFrames)
        return pos;
    pos = std::fmod (pos, (double) numFrames);
    return pos < 0.0 ? pos + numFrames : pos;
}
} // namespace

void GrainVoice::prepare (double rate) noexcept
{
    sampleRate = rate;
    attackStep = (float) (1.0 / (attackSeconds * sampleRate));
    releaseStep = (float) (1.0 / (releaseSeconds * sampleRate));
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

void GrainVoice::noteOn (int note, float vel, const SampleBuffer& buffer, const GrainParams& p) noexcept
{
    currentNote = note;
    velocity = vel;
    noteRatio = semitonesToRatio (note - p.rootNote);
    playhead = std::clamp (p.start, 0.0, 1.0) * (buffer.getNumFrames() - 1);
    samplesToNextGrain = 0.0;      // first grain on the note itself
    spawning = true;
    envStage = EnvStage::attack;   // from the current level: retriggers don't click
    outputGain = targetGain (p);
}

void GrainVoice::noteOff() noexcept
{
    if (envStage != EnvStage::idle)
        envStage = EnvStage::release;
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
// Hann windows sum to overlap/2. With no spray the grains are near-identical
// and add in phase; with spray they add like noise (rms ∝ √(0.375·overlap)).
float GrainVoice::targetGain (const GrainParams& p) const noexcept
{
    const double overlap = p.sizeMs * 0.001 * p.rateHz;
    const double coherent = 1.0 / std::max (1.0, overlap * 0.5);
    const double incoherent = 1.0 / std::max (1.0, std::sqrt (0.375 * overlap));
    const double spread = std::clamp (p.spray * 4.0, 0.0, 1.0);
    return (float) (coherent + spread * (incoherent - coherent));
}

float GrainVoice::nextEnvelope() noexcept
{
    switch (envStage)
    {
        case EnvStage::attack:
            envLevel += attackStep;
            if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = EnvStage::sustain; }
            break;
        case EnvStage::release:
            envLevel -= releaseStep;
            if (envLevel <= 0.0f)
            {
                envLevel = 0.0f;
                envStage = EnvStage::idle;
                spawning = false;
            }
            break;
        case EnvStage::sustain:
        case EnvStage::idle:
            break;
    }
    return envLevel;
}

// Moves the playhead one output sample. Returns false once a `stop` playhead
// runs off the sample.
bool GrainVoice::advancePlayhead (const GrainParams& p, const SampleBuffer& buffer) noexcept
{
    const int frames = buffer.getNumFrames();
    playhead += p.scan * (buffer.getSampleRate() / sampleRate);

    if (playhead >= 0.0 && playhead < frames)
        return true;

    switch (p.endMode)
    {
        case EndMode::loop: playhead = wrapFrames (playhead, frames); return true;
        case EndMode::hold: playhead = std::clamp (playhead, 0.0, (double) (frames - 1)); return true;
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

    const double rateFactor = buffer.getSampleRate() / sampleRate;
    const double ratio = noteRatio * semitonesToRatio (p.pitchSemitones) * rateFactor;
    const int length = std::max (minGrainSamples, (int) (p.sizeMs * 0.001 * sampleRate));

    const double scatter = random.nextBipolar() * p.spray * maxSpraySeconds * buffer.getSampleRate();
    const double start = playhead + scatter;
    const bool reversed = random.next01() < p.reverseChance;
    const auto span = grainReadSpan (start, length, ratio);

    // Stereo scatter as balance, so a centred grain keeps both channels at full level.
    const float pan = random.nextBipolar() * (float) std::clamp (p.spray, 0.0, 1.0);

    Grain& g = *free;
    g.source = &buffer;
    g.readPos = wrapFrames (reversed ? span.hi : span.lo, buffer.getNumFrames());
    g.increment = reversed ? -ratio : ratio;
    g.phase = 0.0;
    g.phaseInc = 1.0 / length;
    g.remaining = length;
    g.gainL = std::min (1.0f, 1.0f - pan);
    g.gainR = std::min (1.0f, 1.0f + pan);
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
                const float env = window (g.phase);
                accL += env * g.gainL * readFrame (g.source->getReadPointer (0), frames, g.readPos, g.interpolation);
                accR += env * g.gainR * readFrame (g.source->getReadPointer (1), frames, g.readPos, g.interpolation);

                g.readPos = wrapFrames (g.readPos + g.increment, frames);
                g.phase += g.phaseInc;
                if (--g.remaining == 0)
                    --activeGrains;
            }
        }

        outputGain += gainSmoothing * (gainTarget - outputGain);
        const float amp = nextEnvelope() * velocity * outputGain;
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
}

} // namespace silt
