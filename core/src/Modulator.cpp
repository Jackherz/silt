#include <silt/Modulator.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double twoPi = 6.283185307179586;

float shapeAt (double phase, int shape) noexcept
{
    switch (shape)
    {
        case 0:  return (float) std::sin (twoPi * phase);
        case 1:  return (float) (phase < 0.25 ? 4.0 * phase : phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0);
        case 2:  return (float) (2.0 * phase - 1.0);
        default: return phase < 0.5 ? -1.0f : 1.0f;   // low first, so blending from the saw keeps full depth
    }
}
} // namespace

bool Modulator::isBipolar (const ModulatorParams& p) noexcept
{
    switch (p.type)
    {
        case ModType::wave:
        case ModType::random:   return true;
        case ModType::midi:     return p.source == MidiSource::note || p.source == MidiSource::pitchBend;
        default:                return false;
    }
}

void Modulator::reset (std::uint64_t seed) noexcept
{
    phase = 0.0;
    stepProgress = 1.0;
    stepLength = 1.0;
    lastStep = -1;
    from = to = 0.0f;
    stage = Stage::idle;
    level = 0.0f;
    followed = 0.0f;
    random.setSeed (seed);
}

double Modulator::cycleRate (const ModulatorParams& p, const ModContext& c) const noexcept
{
    return p.sync ? c.bpm / 60.0 / std::max (1.0e-3, p.syncQuarters) : std::max (0.0f, p.rateHz);
}

float Modulator::advance (const ModulatorParams& p, const ModContext& c) noexcept
{
    switch (p.type)
    {
        case ModType::wave:     return wave (p, c);
        case ModType::random:   return randomStep (p, c);
        case ModType::envelope: return envelope (p, c);
        case ModType::follow:   return follow (p, c);
        case ModType::macro:    return std::clamp (p.macro, 0.0f, 1.0f);
        case ModType::midi:
            switch (p.source)
            {
                case MidiSource::velocity:   return c.velocity;
                case MidiSource::note:       return std::clamp ((float) (c.note - 60) / 48.0f, -1.0f, 1.0f);
                case MidiSource::modWheel:   return c.modWheel;
                case MidiSource::aftertouch: return c.aftertouch;
                case MidiSource::pitchBend:  return c.pitchBend;
            }
    }
    return 0.0f;
}

// Synced while the host plays: the phase comes from the host's position, so the
// wave lines up with the bar. Otherwise it runs free (or restarts per note).
float Modulator::wave (const ModulatorParams& p, const ModContext& c) noexcept
{
    const double step = cycleRate (p, c) * c.numSamples / c.sampleRate;
    if (p.sync && c.hostPlaying)
        phase = c.ppqEnd / std::max (1.0e-3, p.syncQuarters) + p.phase;
    else if (p.retrigger && c.noteStarted)
        phase = p.phase + step;
    else
        phase += step;
    phase -= std::floor (phase);

    // SYMMETRY warps the phase before the wave is read: the first half of the
    // cycle takes `symmetry` of it, the second half the rest.
    double read = phase;
    if (p.symmetry != 0.5f)
    {
        const double s = std::clamp ((double) p.symmetry, 0.001, 0.999);
        read = phase < s ? 0.5 * phase / s : 0.5 + 0.5 * (phase - s) / (1.0 - s);
    }

    const float shape = std::clamp (p.shape, 0.0f, 3.0f);
    const int lower = (int) shape;
    const float blend = shape - (float) lower;
    const float a = shapeAt (read, lower);
    const float y = blend > 0.0f ? a + (shapeAt (read, lower + 1) - a) * blend : a;

    // CURVE bends the output, keeping its sign and its peaks: |y|^(4^curve).
    if (p.curve == 0.0f)
        return y;
    const float exponent = std::pow (4.0f, std::clamp (p.curve, -1.0f, 1.0f));
    return std::copysign (std::pow (std::abs (y), exponent), y);
}

// A new random value each step; SMOOTH glides to it (cosine), JITTER spreads
// the step lengths. Synced while the host plays, steps fall on the grid.
float Modulator::randomStep (const ModulatorParams& p, const ModContext& c) noexcept
{
    const auto newTarget = [this]
    {
        from = to;
        to = 2.0f * random.next01() - 1.0f;
    };

    if (p.sync && c.hostPlaying)
    {
        const double position = c.ppqEnd / std::max (1.0e-3, p.syncQuarters);
        const auto index = (long long) std::floor (position);
        if (index != lastStep)
        {
            lastStep = index;
            newTarget();
        }
        stepProgress = position - (double) index;
    }
    else
    {
        lastStep = -1;
        stepProgress += cycleRate (p, c) * c.numSamples / c.sampleRate / stepLength;
        while (stepProgress >= 1.0)
        {
            stepProgress -= 1.0;
            newTarget();
            stepLength = 1.0 + std::clamp ((double) p.jitter, 0.0, 1.0) * (1.5 * random.next01() - 0.75);
        }
    }

    const float eased = from + (to - from) * (float) (0.5 - 0.5 * std::cos (3.14159265358979 * std::min (1.0, stepProgress)));
    const float smooth = std::clamp (p.smooth, 0.0f, 1.0f);
    return to + (eased - to) * smooth;
}

// Linear segments: attack from wherever it is to 1, decay to SUSTAIN, release
// to 0 once no keys are held. REPEAT loops attack and decay while held.
float Modulator::envelope (const ModulatorParams& p, const ModContext& c) noexcept
{
    const float ms = (float) (c.numSamples * 1000.0 / c.sampleRate);
    const float sustain = std::clamp (p.sustain, 0.0f, 1.0f);

    if (c.noteStarted)
        stage = Stage::attack;
    else if (c.heldNotes == 0 && stage != Stage::idle && stage != Stage::release)
        stage = Stage::release;

    switch (stage)
    {
        case Stage::attack:
            level += ms / std::max (0.1f, p.attackMs);
            if (level >= 1.0f)
            {
                level = 1.0f;
                stage = Stage::decay;
            }
            break;
        case Stage::decay:
            level -= (1.0f - sustain) * ms / std::max (0.1f, p.decayMs);
            if (level <= sustain)
            {
                level = sustain;
                stage = p.repeat ? Stage::attack : Stage::sustain;
            }
            break;
        case Stage::sustain:
            level = sustain;
            break;
        case Stage::release:
            level -= ms / std::max (0.1f, p.releaseMs);
            if (level <= 0.0f)
            {
                level = 0.0f;
                stage = Stage::idle;
            }
            break;
        case Stage::idle:
            break;
    }
    return level;
}

// The input's peak level, smoothed, read on a 60 dB scale (plus GAIN).
float Modulator::follow (const ModulatorParams& p, const ModContext& c) noexcept
{
    const float ms = std::max (0.1f, c.inputPeak > followed ? p.followAttackMs : p.followReleaseMs);
    const float k = 1.0f - std::exp (-(float) c.numSamples / (ms * 0.001f * (float) c.sampleRate));
    followed += k * (c.inputPeak - followed);
    const float db = 20.0f * std::log10 (std::max (followed, 1.0e-6f)) + p.gainDb;
    return std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

} // namespace silt
