#include <silt/Filter.h>
#include <silt/Dsp.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double butterworthQ = 0.7071067811865476;
constexpr double maxQ = 30.0;
// A 4-pole Butterworth from two 2-pole stages: Q 0.541 then 1.307.
constexpr double firstStageQ = 0.5411961001461970, secondStageQ = 1.3065629648763766;

double qFor (double lowest, double resonance) noexcept { return lowest * std::pow (maxQ / lowest, resonance); }
} // namespace

void Filter::prepare (double rate) noexcept
{
    sampleRate = rate;
    reset();
    active = false;
    primed = false;
    wet = 0.0;
}

void Filter::reset() noexcept
{
    for (auto& c : channels)
        c = Channel {};
    envelope = 0.0;
}

void Filter::process (float* left, float* right, int numSamples, const FilterParams& p) noexcept
{
    const double wetTarget = p.slope == FilterSlope::off ? 0.0 : std::clamp ((double) p.mix, 0.0, 1.0);
    if (! active)
    {
        if (wetTarget <= 0.0)
            return;   // exact bypass
        active = true;
        wet = 0.0;
        reset();
    }

    const double nyquistLimit = 0.42 * sampleRate;
    const auto blockCoefficient = [this] (double seconds)
    {
        return 1.0 - std::exp (-(double) controlInterval / (seconds * sampleRate));
    };
    const double glide = blockCoefficient (0.02), fade = blockCoefficient (0.01);
    const double envAttack = 1.0 - std::exp (-1.0 / (0.003 * sampleRate));
    const double envRelease = 1.0 - std::exp (-1.0 / (0.15 * sampleRate));

    const double cutoffTarget = std::clamp ((double) p.cutoffHz, 16.0, nyquistLimit);
    const double slopeTarget = p.slope == FilterSlope::db24 ? 1.0 : p.slope == FilterSlope::db12 ? 0.0 : slopeBlend;
    if (! primed)
    {
        primed = true;
        cutoffNow = cutoffTarget;
        resonanceNow = p.resonance;
        morphNow = p.morph;
        driveNow = p.drive;
        slopeBlend = slopeTarget;
        gNow = std::tan (pi * cutoffNow / sampleRate);
    }

    for (int start = 0; start < numSamples; start += controlInterval)
    {
        const int len = std::min (controlInterval, numSamples - start);

        cutoffNow *= std::pow (cutoffTarget / cutoffNow, glide);
        resonanceNow += glide * (std::clamp ((double) p.resonance, 0.0, 1.0) - resonanceNow);
        morphNow += glide * (std::clamp ((double) p.morph, 0.0, 2.0) - morphNow);
        driveNow += glide * (std::clamp ((double) p.drive, 0.0, 1.0) - driveNow);
        const double slopeNext = slopeBlend + fade * (slopeTarget - slopeBlend);
        const double wetNext = wet + fade * (wetTarget - wet);

        // The cutoff this block ends at: knob, envelope and key tracking.
        const double envelopeLevel = std::clamp ((20.0 * std::log10 (std::max (envelope, 1.0e-6)) + 48.0) / 48.0, 0.0, 1.0);
        const double octaves = 4.0 * std::clamp ((double) p.envAmount, -1.0, 1.0) * envelopeLevel + p.keyOctaves;
        const double cutoff = std::clamp (cutoffNow * std::pow (2.0, octaves), 16.0, nyquistLimit);
        const double gNext = std::tan (pi * cutoff / sampleRate);

        // Resonance, and a passband drop as it rises (as on many analogue
        // filters) so the peak doesn't run far over the input's level.
        const double q12 = qFor (butterworthQ, resonanceNow), q24 = qFor (secondStageQ, resonanceNow);
        const double k12 = 1.0 / q12, kFirst = 1.0 / firstStageQ, kSecond = 1.0 / q24;
        const double trim12 = 1.0 / std::sqrt (q12 / butterworthQ), trim24 = 1.0 / std::sqrt (q24 / secondStageQ);

        // Morph: low → band → high. The band pass is scaled to meet the others at the peak.
        const double m = morphNow;
        const double lowMix = std::max (0.0, 1.0 - m), highMix = std::max (0.0, m - 1.0);
        const double bandMix = 1.0 - lowMix - highMix;

        const double driveGain = std::pow (10.0, 24.0 * driveNow / 20.0);
        const double driveBlend = std::min (1.0, driveNow * 10.0);   // no saturation at all at zero

        for (int i = start; i < start + len; ++i)
        {
            const double along = (double) (i - start + 1) / len;
            const double g = gNow + (gNext - gNow) * along;
            const double w = wet + (wetNext - wet) * along;
            const double blend = slopeBlend + (slopeNext - slopeBlend) * along;

            const float inLeft = left[i], inRight = right[i];
            const double peak = std::max (std::abs ((double) inLeft), std::abs ((double) inRight));
            envelope += (peak > envelope ? envAttack : envRelease) * (peak - envelope);

            float* outs[2] { left + i, right + i };
            const float ins[2] { inLeft, inRight };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = channels[(size_t) ch];
                double x = ins[ch];
                x += driveBlend * (std::tanh (driveGain * x) / std::sqrt (driveGain) - x);

                double low, band, high;
                c.twelve.run (x * trim12, g, k12, low, band, high);
                const double y12 = low * lowMix + band * bandMix + high * highMix;

                c.first.run (x * trim24, g, kFirst, low, band, high);
                const double stage1 = low * lowMix + band * bandMix * kFirst + high * highMix;
                c.second.run (stage1, g, kSecond, low, band, high);
                const double y24 = low * lowMix + band * bandMix + high * highMix;

                const double y = y12 + (y24 - y12) * blend;
                *outs[ch] = (float) (ins[ch] * (1.0 - w) + softClip ((float) y) * w);
            }
        }
        gNow = gNext;
        wet = wetNext;
        slopeBlend = slopeNext;
    }

    if (wetTarget <= 0.0 && wet < 1.0e-4)
    {
        active = false;   // faded out: back to passing the input through
        wet = 0.0;
        reset();
    }
}

} // namespace silt
