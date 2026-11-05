#include <silt/Filter.h>
#include <silt/Dsp.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double pi = 3.14159265358979323846;

constexpr double level = 1.5;           // how hard a full-scale signal reaches the stages at no DRIVE
constexpr double maxDriveDb = 24.0;
constexpr double evenAtRest = 0.03, evenAtFullDrive = 0.2;   // a little second harmonic, as a real input stage has
constexpr double channelSpread = 0.004;   // the channels' cutoffs, about 7 cents either side

// Feedback at no resonance holds the corner near 3 dB down at the knob; at full
// resonance it stops just under self-oscillation.
constexpr double closedHz = 20.0, openHz = 40.0;   // where the low pass closes
constexpr double restFeedback12 = 0.3, restFeedback24 = 1.0, maxFeedback = 3.8;

// tanh (x) / x, cheaply: it's the gain a stage gives at level x.
inline double saturationGain (double x) noexcept
{
    const double x2 = x * x;
    return ((x2 + 105.0) * x2 + 945.0) / ((15.0 * x2 + 420.0) * x2 + 945.0);
}

double restFeedback (double slope) noexcept { return restFeedback12 + slope * (restFeedback24 - restFeedback12); }
} // namespace

double Filter::feedbackFor (double resonance, double slope) noexcept
{
    const double rest = restFeedback (slope);
    return rest + (maxFeedback - rest) * std::pow (resonance, 1.5);
}

// The low pass makes up what the feedback takes from the bass, leaving a little of that
// drop as resonance rises. At the bottom of its travel it closes altogether: with the knob
// all the way down there's nothing left, not even the rumble under the cutoff.
std::array<double, 5> Filter::tapMixFor (double morph, double slope, double resonance, double cutoffHz) noexcept
{
    constexpr std::array<double, 5> low24 { 0, 0, 0, 0, 1 }, low12 { 0, 0, 1, 0, 0 };
    constexpr std::array<double, 5> band24 { 0, 0, 4, -8, 4 }, band12 { 0, 2, -2, 0, 0 };
    constexpr std::array<double, 5> high24 { 1, -4, 6, -4, 1 }, high12 { 1, -2, 1, 0, 0 };
    const double lowMix = std::max (0.0, 1.0 - morph), highMix = std::max (0.0, morph - 1.0);
    const double bandMix = 1.0 - lowMix - highMix;
    const double rest = restFeedback (slope);
    const double opening = std::clamp ((cutoffHz - closedHz) / (openHz - closedHz), 0.0, 1.0);
    const double bass = opening * opening * (3.0 - 2.0 * opening) * (1.0 + rest + 0.5 * (feedbackFor (resonance, slope) - rest));
    std::array<double, 5> mix {};
    for (size_t t = 0; t < mix.size(); ++t)
        mix[t] = lowMix * bass * (low12[t] + slope * (low24[t] - low12[t]))
               + bandMix * (band12[t] + slope * (band24[t] - band12[t]))
               + highMix * (high12[t] + slope * (high24[t] - high12[t]));
    return mix;
}

void Filter::prepare (double rate) noexcept
{
    sampleRate = rate;
    dcCoefficient = std::exp (-2.0 * pi * 5.0 / rate);
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

// The saturation's gains come from the last tick's states, which turns the rest into a
// linear solve: stable, and cheap enough for every sample.
void Filter::tick (Channel& c, double in, double f, double feedback, std::array<double, 5>& taps) noexcept
{
    auto& s = c.poles;
    const double t0 = saturationGain (in - feedback * s[3]);
    const double t1 = saturationGain (s[0]), t2 = saturationGain (s[1]), t3 = saturationGain (s[2]), t4 = saturationGain (s[3]);
    const double g0 = 1.0 / (1.0 + f * t1), g1 = 1.0 / (1.0 + f * t2), g2 = 1.0 / (1.0 + f * t3), g3 = 1.0 / (1.0 + f * t4);
    const double f3 = f * t3 * g3, f2 = f * t2 * g2 * f3, f1 = f * t1 * g1 * f2, f0 = f * t0 * g0 * f1;

    const double y3 = (g3 * s[3] + f3 * g2 * s[2] + f2 * g1 * s[1] + f1 * g0 * s[0] + f0 * in) / (1.0 + feedback * f0);
    const double x = t0 * (in - feedback * y3);
    const double y0 = t1 * g0 * (s[0] + f * x);
    const double y1 = t2 * g1 * (s[1] + f * y0);
    const double y2 = t3 * g2 * (s[2] + f * y1);

    s[0] += 2.0 * f * (x - y0);
    s[1] += 2.0 * f * (y0 - y1);
    s[2] += 2.0 * f * (y1 - y2);
    s[3] += 2.0 * f * (y2 - t4 * y3);
    taps = { x, y0, y1, y2, y3 };
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

    const double nyquistLimit = 0.49 * sampleRate;
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
        gNow = std::tan (pi * cutoffNow / (2.0 * sampleRate));
        feedbackNow = feedbackFor (resonanceNow, slopeBlend);
        mixNow = tapMixFor (morphNow, slopeBlend, resonanceNow, cutoffNow);
        driveGainNow = std::pow (10.0, maxDriveDb * driveNow / 20.0);
        evenNow = evenAtRest + driveNow * (evenAtFullDrive - evenAtRest);
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
        const double gNext = std::tan (pi * cutoff / (2.0 * sampleRate));

        const double feedbackNext = feedbackFor (resonanceNow, slopeNext);
        const auto mixNext = tapMixFor (morphNow, slopeNext, resonanceNow, cutoff);
        const double driveGainNext = std::pow (10.0, maxDriveDb * driveNow / 20.0);
        const double evenNext = evenAtRest + driveNow * (evenAtFullDrive - evenAtRest);

        for (int i = start; i < start + len; ++i)
        {
            const double along = (double) (i - start + 1) / len;
            const double g = gNow + (gNext - gNow) * along;
            const double w = wet + (wetNext - wet) * along;
            const double feedback = feedbackNow + (feedbackNext - feedbackNow) * along;
            const double driveGain = driveGainNow + (driveGainNext - driveGainNow) * along;
            const double even = evenNow + (evenNext - evenNow) * along;
            std::array<double, 5> mix;
            for (size_t t = 0; t < mix.size(); ++t)
                mix[t] = mixNow[t] + (mixNext[t] - mixNow[t]) * along;

            const float inLeft = left[i], inRight = right[i];
            const double peak = std::max (std::abs ((double) inLeft), std::abs ((double) inRight));
            envelope += (peak > envelope ? envAttack : envRelease) * (peak - envelope);

            float* outs[2] { left + i, right + i };
            const float ins[2] { inLeft, inRight };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const double f = g * (ch == 0 ? 1.0 - channelSpread : 1.0 + channelSpread);

                const double driven = ins[ch] * level * driveGain;
                const auto shaped = [even] (double x) { return x + even * x * x / (1.0 + x * x); };

                const auto mixed = [&mix] (const std::array<double, 5>& taps)
                {
                    double sum = 0.0;
                    for (size_t t = 0; t < taps.size(); ++t)
                        sum += mix[t] * taps[t];
                    return sum;
                };
                std::array<double, 5> taps;
                tick (c, shaped (c.upA.run (driven, kit::HalfBandPath::a)), f, feedback, taps);
                const double older = mixed (taps);
                tick (c, shaped (c.upB.run (driven, kit::HalfBandPath::b)), f, feedback, taps);
                const double newer = mixed (taps);
                const double y = 0.5 * (c.downA.run (newer, kit::HalfBandPath::a) + c.downB.run (older, kit::HalfBandPath::b)) / (level * std::sqrt (driveGain));

                // The even harmonic's offset, and the only thing that would stay on with no input.
                c.dcOut = y - c.dcIn + dcCoefficient * c.dcOut;
                c.dcIn = y;
                *outs[ch] = (float) (ins[ch] * (1.0 - w) + softClip ((float) c.dcOut) * w);
            }
        }
        feedbackNow = feedbackNext;
        mixNow = mixNext;
        driveGainNow = driveGainNext;
        evenNow = evenNext;
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
