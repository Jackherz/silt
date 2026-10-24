#include <silt/Resonator.h>

#include <algorithm>
#include <cmath>

namespace silt
{

namespace
{
constexpr double pi = 3.14159265358979323846;

// Bell partials (after Risset's bell), over the lowest, and the same an octave up.
constexpr std::array<double, 9> bellPartials { 1.0, 1.643, 2.125, 3.054, 3.571, 4.893, 5.357, 6.714, 7.268 };

double noteToHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
} // namespace

int Resonator::bandRatios (const ResonatorParams& p, std::array<double, maxBands>& ratios) noexcept
{
    const double top = std::pow (2.0, std::clamp ((double) p.rangeOctaves, 0.0, 4.0)) * (1.0 + 1.0e-9);
    std::array<double, 64> candidates {};
    int count = 0;
    const auto add = [&] (double ratio)
    {
        if (ratio <= top && count < (int) candidates.size())
            candidates[(size_t) count++] = ratio;
    };

    switch (p.notes)
    {
        case ResonatorNotes::scale:
        {
            // The root snaps into the scale; with no scale, the root and its fifths.
            const bool noScale = p.scale == Scale::off;
            const int root = noScale ? (int) std::lround (p.rootNote)
                                     : (int) std::lround (snapToScale (std::round (p.rootNote), p.scale, p.key));
            const int key = noScale ? root : p.key;
            const auto mask = scaleMask (noScale ? Scale::fifths : p.scale);
            for (int s = 0; s <= 48; ++s)
            {
                const int degree = ((root + s - key) % 12 + 12) % 12;
                if ((mask >> degree & 1u) != 0)
                    add (std::pow (2.0, (root + s - (double) p.rootNote) / 12.0));
            }
            break;
        }
        case ResonatorNotes::harmonics:
            for (int n = 1; n <= 16; ++n)
                add ((double) n);
            break;
        case ResonatorNotes::bell:
        {
            for (const double r : bellPartials)
                add (r);
            for (const double r : bellPartials)
                add (2.0 * r);
            std::sort (candidates.begin(), candidates.begin() + count);
            break;
        }
    }

    // More than fit: an even pick from lowest to highest.
    const int n = std::min (count, maxBands);
    for (int i = 0; i < n; ++i)
    {
        const int from = n < 2 ? 0 : (int) std::lround ((double) i * (count - 1) / (n - 1));
        ratios[(size_t) i] = candidates[(size_t) from];
    }
    return n;
}

void Resonator::prepare (double rate) noexcept
{
    sampleRate = rate;
    reset();
    active = false;
    mixNow = 0.0f;
}

void Resonator::reset() noexcept
{
    for (auto& b : bands)
        b = Band {};
    numBands = 0;
    inMean = inPower = inSlow = wetPower = choke = 0.0;
    ringGain = 1.0;
}

void Resonator::updateCoefficients (const ResonatorParams& target, bool snap) noexcept
{
    // The knobs glide (30 ms) so sweeping them moves the bank smoothly.
    const auto blockCoefficient = [this, snap] (double seconds)
    {
        return snap ? 1.0 : 1.0 - std::exp (-(double) controlInterval / (seconds * sampleRate));
    };
    const double knobGlide = blockCoefficient (0.03);
    const auto glideTo = [knobGlide] (float& value, float to) { value += (float) knobGlide * (to - value); };
    const float decayLog = std::log (std::max (0.01f, smoothed.decaySeconds));
    smoothed.decaySeconds = std::exp (decayLog + (float) knobGlide * (std::log (std::max (0.01f, target.decaySeconds)) - decayLog));
    glideTo (smoothed.bright, target.bright);
    glideTo (smoothed.width, target.width);
    glideTo (smoothed.rangeOctaves, target.rangeOctaves);
    smoothed.rootNote = target.rootNote;   // the band frequencies glide instead
    smoothed.notes = target.notes;
    smoothed.scale = target.scale;
    smoothed.key = target.key;
    const auto& p = smoothed;

    std::array<double, maxBands> ratios {};
    const int count = bandRatios (p, ratios);
    const double rootHz = noteToHz (std::clamp ((double) p.rootNote, 0.0, 127.0));
    const double nyquistLimit = 0.45 * sampleRate;
    const double frequencyGlide = blockCoefficient (0.02);
    const double gainGlide = blockCoefficient (0.02);
    const double tilt = 1.0 - std::clamp ((double) p.bright, 0.0, 1.0);
    const double width = std::clamp ((double) p.width, 0.0, 1.0);

    int n = 0;
    while (n < count && rootHz * ratios[(size_t) n] < nyquistLimit)
        ++n;

    int sounding = n;
    for (int i = 0; i < std::max (n, numBands); ++i)
    {
        auto& b = bands[(size_t) i];
        double targetLeft = 0.0, targetRight = 0.0;   // bands past the range fade out

        if (i < n)
        {
            const double ratio = ratios[(size_t) i];
            const double to = rootHz * ratio;
            if (b.frequency <= 0.0)
                b.gainLeft = b.gainRight = 0.0;   // a new band fades in
            b.frequency = (snap || b.frequency <= 0.0) ? to : b.frequency * std::pow (to / b.frequency, frequencyGlide);

            // Ring time to bandwidth: a two-pole ring falls 60 dB in ln(1000) / (pi * B).
            // While sound comes in the bands stay at least resonanceQ narrow, so
            // they sound pitched even at short decays; once it stops they're
            // choked to the decay time (the S-4's split of RESONANCE and DECAY).
            const double t60 = std::max (0.01, (double) p.decaySeconds) * std::pow (ratio, -0.7 * tilt);
            const double ringWidth = std::log (1000.0) / (pi * t60);
            const double heldWidth = std::min (ringWidth, b.frequency / resonanceQ);
            b.k = std::min (2.0, (heldWidth + (ringWidth - heldWidth) * choke) / b.frequency);
            const double g = std::tan (pi * b.frequency / sampleRate);
            b.a1 = 1.0 / (1.0 + g * (g + b.k));
            b.a2 = g * b.a1;
            b.a3 = g * b.a2;

            // Each band peaks at unity; narrow bands get some of their lost energy
            // back, shared across the bank, so long rings stay as loud as short ones.
            const double makeup = std::pow (sampleRate / (pi * heldWidth * n), 0.375);
            const double level = makeup * std::pow (ratio, -0.75 * tilt);
            const double pan = (i % 2 == 0 ? -1.0 : 1.0) * width * (n > 1 ? 1.0 : 0.0);
            const double angle = (pan + 1.0) * pi / 4.0;
            // k scales the band-pass output to unity peak: part of the gain, so
            // it ramps per sample with the rest.
            targetLeft = b.k * level * std::cos (angle) * std::sqrt (2.0);
            targetRight = b.k * level * std::sin (angle) * std::sqrt (2.0);
        }
        else if (b.gainLeft + b.gainRight < 1.0e-4 * b.k)
        {
            b = Band {};   // faded out
            continue;
        }
        else
            sounding = i + 1;

        // This block ramps sample by sample from the current gains to these.
        b.nextLeft = snap ? targetLeft : b.gainLeft + gainGlide * (targetLeft - b.gainLeft);
        b.nextRight = snap ? targetRight : b.gainRight + gainGlide * (targetRight - b.gainRight);
        if (snap)
            b.gainLeft = b.nextLeft, b.gainRight = b.nextRight;
    }
    numBands = sounding;
}

void Resonator::process (float* left, float* right, int numSamples, const ResonatorParams& p) noexcept
{
    const float mixTarget = std::clamp (p.mix, 0.0f, 1.0f);
    if (! active)
    {
        if (mixTarget <= 0.0f)
            return;   // exact bypass
        active = true;
        mixNow = 0.0f;
        reset();
        updateCoefficients (p, true);
    }

    // Level match: the input's level, held as long as a ring lasts, against the
    // rings' level; the rings are turned down when they'd be louder than the
    // input (a tone right on a band otherwise rings ~15 dB hotter).
    const auto coefficient = [this] (double seconds) { return 1.0 - std::exp (-1.0 / (seconds * sampleRate)); };
    const double inAttack = coefficient (0.005);
    const double inRelease = coefficient (std::max (0.05, (double) smoothed.decaySeconds / 6.9));
    const double meanSmoothing = coefficient (0.02);
    const double slowRelease = coefficient (0.3);
    const double gainDown = coefficient (0.005), gainUp = coefficient (0.3);
    const double chokeGlide = 1.0 - std::exp (-(double) controlInterval / (0.01 * sampleRate));
    constexpr double headroom = 1.0;   // power: the input's own level

    for (int start = 0; start < numSamples; start += controlInterval)
    {
        const int len = std::min (controlInterval, numSamples - start);

        // Choke: 0 while sound comes in, 1 once it has fallen 12 dB below its
        // recent level (fully at 12 dB, from 3 dB).
        const double fall = inSlow > 1.0e-10 ? -10.0 * std::log10 (std::max (inMean, 1.0e-20) / inSlow) : 60.0;
        choke += chokeGlide * (std::clamp ((fall - 3.0) / 9.0, 0.0, 1.0) - choke);

        updateCoefficients (p, false);
        const float mixStep = (mixTarget - mixNow) / (float) len;
        const double rampScale = 1.0 / len;

        for (int i = start; i < start + len; ++i)
        {
            const float inLeft = left[i], inRight = right[i];
            const double x = 0.5 * ((double) inLeft + (double) inRight);
            const double along = (double) (i - start + 1) * rampScale;
            double wetLeft = 0.0, wetRight = 0.0;
            for (int bi = 0; bi < numBands; ++bi)
            {
                auto& b = bands[(size_t) bi];
                const double v3 = x - b.s2;
                const double v1 = b.a1 * b.s1 + b.a2 * v3;
                const double v2 = b.s2 + b.a2 * b.s1 + b.a3 * v3;
                b.s1 = 2.0 * v1 - b.s1;
                b.s2 = 2.0 * v2 - b.s2;
                wetLeft += v1 * (b.gainLeft + (b.nextLeft - b.gainLeft) * along);
                wetRight += v1 * (b.gainRight + (b.nextRight - b.gainRight) * along);
            }
            inMean += meanSmoothing * (x * x - inMean);   // averaged first, so the hold follows level, not peaks
            inPower += (inMean > inPower ? inAttack : inRelease) * (inMean - inPower);
            inSlow += (inMean > inSlow ? inAttack : slowRelease) * (inMean - inSlow);
            wetPower += meanSmoothing * (0.5 * (wetLeft * wetLeft + wetRight * wetRight) - wetPower);
            const double target = wetPower > headroom * inPower ? std::sqrt (headroom * inPower / wetPower) : 1.0;
            ringGain += (target < ringGain ? gainDown : gainUp) * (target - ringGain);

            mixNow += mixStep;
            left[i] = inLeft * (1.0f - mixNow) + softClip ((float) (wetLeft * ringGain)) * mixNow;
            right[i] = inRight * (1.0f - mixNow) + softClip ((float) (wetRight * ringGain)) * mixNow;
        }
        mixNow = mixTarget;

        for (int bi = 0; bi < numBands; ++bi)
        {
            auto& b = bands[(size_t) bi];
            b.gainLeft = b.nextLeft;
            b.gainRight = b.nextRight;
            if (std::abs (b.s1) < 1.0e-20 && std::abs (b.s2) < 1.0e-20)   // let silent rings reach true zero
                b.s1 = b.s2 = 0.0;
        }
    }

    if (mixTarget <= 0.0f)
    {
        active = false;   // faded out: back to passing the input through
        reset();
    }
}

} // namespace silt
