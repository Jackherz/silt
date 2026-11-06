#include <silt/Resonator.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace silt
{

namespace
{
constexpr double pi = 3.14159265358979323846;

// Bell partials (after Risset's bell), over the lowest; higher up, the same a little stretched.
constexpr std::array<double, 9> bellPartials { 1.0, 1.643, 2.125, 3.054, 3.571, 4.893, 5.357, 6.714, 7.268 };

double noteToHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

// The strength of a mode's response against its width. At 0.5 long and short
// rings would match on noise; at 0 a hit would ring the same whatever the width.
// Between the two, a long ring is a little louder and a hit a little softer.
constexpr double widthExponent = 0.25;
constexpr double bankLevel = 0.22;   // sits a typical grain signal near its own level
} // namespace

double Resonator::levelAt (double ratio, double bright) noexcept
{
    return std::pow (ratio, -1.5 * (1.0 - std::clamp (bright, 0.0, 1.0)));
}

double Resonator::ringAt (double ratio, double bright, double damp) noexcept
{
    return std::pow (ratio, -0.7 * (1.0 - std::clamp (bright, 0.0, 1.0)) - 1.5 * damp);
}

int Resonator::bandRatios (const ResonatorParams& p, std::array<double, maxBands>& ratios) noexcept
{
    const double top = std::pow (2.0, std::clamp ((double) p.rangeOctaves, 0.0, 8.0)) * (1.0 + 1.0e-9);
    std::array<double, 1300> candidates;   // no clearing: this runs on the audio thread
    int count = 0;
    const auto add = [&] (double ratio, double limit)
    {
        if (ratio <= limit && count < (int) candidates.size())
            candidates[(size_t) count++] = ratio;
    };
    const int chordSize = std::clamp (p.chordSize, 0, (int) p.chord.size());

    // One note's bands, `offset` semitones over the root.
    const auto bandsFor = [&] (double offset)
    {
        const double shift = std::pow (2.0, offset / 12.0), limit = top * shift;
        switch (p.notes)
        {
            case ResonatorNotes::scale:
            {
                if (chordSize > 0)
                {
                    // A chord rings its own notes, in octaves.
                    for (double octave = 1.0; octave * shift <= limit; octave *= 2.0)
                        add (octave * shift, limit);
                    break;
                }
                // The root snaps into the scale; with no scale, the root and its fifths.
                const bool noScale = p.scale == Scale::off;
                const int root = noScale ? (int) std::lround (p.rootNote)
                                         : (int) std::lround (snapToScale (std::round (p.rootNote), p.scale, p.key));
                const int key = noScale ? root : p.key;
                const auto mask = scaleMask (noScale ? Scale::fifths : p.scale);
                for (int s = 0; s <= 96; ++s)
                {
                    const int degree = ((root + s - key) % 12 + 12) % 12;
                    if ((mask >> degree & 1u) != 0)
                        add (std::pow (2.0, (root + s - (double) p.rootNote) / 12.0), limit);
                }
                break;
            }
            case ResonatorNotes::harmonics:
                for (int n = 1; n <= maxBands; ++n)
                    add ((double) n * shift, limit);
                break;
            case ResonatorNotes::bell:
                for (double octave = 1.0; octave * shift <= limit; octave *= 2.0)
                    for (const double r : bellPartials)
                        add (r * octave * (1.0 + 0.002 * std::log2 (octave)) * shift, limit);
                break;
            case ResonatorNotes::sample:
            {
                const int n = std::clamp (p.sampleCount, 0, (int) p.sampleHz.size());
                if (n == 0)
                {
                    add (shift, limit);   // nothing found: the root alone
                    break;
                }
                const double rootHz = 440.0 * std::pow (2.0, ((double) p.rootNote - 69.0) / 12.0);
                const double lowest = p.sampleHz[0] / rootHz;
                for (int i = 0; i < n; ++i)
                    add (p.sampleHz[(size_t) i] / rootHz * shift, lowest * limit);
                break;
            }
        }
    };

    bandsFor (0.0);
    for (int c = 0; c < chordSize; ++c)
        bandsFor ((double) p.chord[(size_t) c]);
    std::sort (candidates.begin(), candidates.begin() + count);

    // The same band twice (a chord's shared notes and octaves) rings once.
    int distinct = 0;
    for (int i = 0; i < count; ++i)
        if (distinct == 0 || candidates[(size_t) i] > candidates[(size_t) distinct - 1] * 1.003)
            candidates[(size_t) distinct++] = candidates[(size_t) i];
    count = distinct;

    // More than fit: an even pick from lowest to highest.
    const int n = std::min ({ count, maxBands, std::max (1, p.bandCount) });
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
    bands.fill (Band {});
    re.fill (0.0f);
    im.fill (0.0f);
    turnCos.fill (0.0f);
    turnSin.fill (0.0f);
    gainLeft.fill (0.0f);
    gainRight.fill (0.0f);
    nextLeft.fill (0.0f);
    nextRight.fill (0.0f);
    numBands = 0;
    inMean = inPeak = inPower = inSlow = wetPower = choke = 0.0;
    ringGain = 1.0;
    settled = false;
    chokeMatters = true;
    chokeUsed = -1.0;
    bankSilent = false;
}

void Resonator::clearBand (int band) noexcept
{
    const auto i = (size_t) band;
    bands[i] = Band {};
    re[i] = im[i] = turnCos[i] = turnSin[i] = 0.0f;
    gainLeft[i] = gainRight[i] = nextLeft[i] = nextRight[i] = 0.0f;
}

void Resonator::updateCoefficients (const ResonatorParams& target, bool snap) noexcept
{
    // Nothing moving and nothing asked for: the audio loop keeps what it has.
    auto asked = target;
    asked.mix = lastTarget.mix;
    const bool same = std::memcmp (&asked, &lastTarget, sizeof (ResonatorParams)) == 0;
    if (! snap && settled && same && ! (chokeMatters && std::abs (choke - chokeUsed) > 1.0e-3))
    {
        nextLeft = gainLeft;
        nextRight = gainRight;
        return;
    }
    lastTarget = target;
    chokeUsed = choke;

    // The knobs glide (30 ms) so sweeping them moves the bank smoothly.
    const auto blockCoefficient = [this, snap] (double seconds)
    {
        return snap ? 1.0 : 1.0 - std::exp (-(double) controlInterval / (seconds * sampleRate));
    };
    const double knobGlide = blockCoefficient (0.03);
    bool moving = false;
    const auto glideTo = [knobGlide, &moving] (float& value, float to)
    {
        value += (float) knobGlide * (to - value);
        if (std::abs (to - value) > 1.0e-4f)
            moving = true;
        else
            value = to;
    };
    const float decayLog = std::log (std::max (0.01f, smoothed.decaySeconds));
    const float decayTo = std::log (std::max (0.01f, target.decaySeconds));
    smoothed.decaySeconds = std::exp (decayLog + (float) knobGlide * (decayTo - decayLog));
    if (std::abs (decayTo - decayLog) > 1.0e-4f)
        moving = true;
    else
        smoothed.decaySeconds = target.decaySeconds;
    glideTo (smoothed.bright, target.bright);
    glideTo (smoothed.width, target.width);
    glideTo (smoothed.rangeOctaves, target.rangeOctaves);
    glideTo (smoothed.resonance, target.resonance);
    glideTo (smoothed.drift, target.drift);
    glideTo (smoothed.spread, target.spread);
    glideTo (smoothed.oddEven, target.oddEven);
    glideTo (smoothed.drive, target.drive);
    glideTo (smoothed.damp, target.damp);
    smoothed.driftRate = target.driftRate;
    smoothed.bandCount = target.bandCount;
    smoothed.rootNote = target.rootNote;   // the band frequencies glide instead
    smoothed.notes = target.notes;
    smoothed.sampleHz = target.sampleHz;
    smoothed.sampleCount = target.sampleCount;
    smoothed.chord = target.chord;
    smoothed.chordSize = target.chordSize;
    smoothed.scale = target.scale;
    smoothed.key = target.key;
    const auto& p = smoothed;

    std::array<double, maxBands> ratios;
    const int count = bandRatios (p, ratios);
    const double rootHz = noteToHz (std::clamp ((double) p.rootNote, 0.0, 127.0));
    const double nyquistLimit = 0.45 * sampleRate;
    const double frequencyGlide = blockCoefficient (0.02);
    const double gainGlide = blockCoefficient (0.02);
    const double bright = std::clamp ((double) p.bright, 0.0, 1.0);
    const double damp = (double) p.damp;
    const double width = std::clamp ((double) p.width, 0.0, 1.0);
    const double resonanceQ = 8.0 * std::pow (15.0, std::clamp ((double) p.resonance, 0.0, 1.0));
    const double drift = std::clamp ((double) p.drift, 0.0, 1.0);
    const double driftStep = (double) controlInterval * std::clamp ((double) p.driftRate, 0.01, 20.0) / sampleRate;
    const double oddEven = std::clamp ((double) p.oddEven, -1.0, 1.0);
    if (drift > 0.0)
        moving = true;

    int n = 0;
    while (n < count && rootHz * ratios[(size_t) n] < nyquistLimit)
        ++n;

    // Shared across the bank, so the level doesn't change with how many modes there are.
    double levelSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double level = levelAt (ratios[(size_t) i], bright);
        levelSum += level * level;
    }
    const double perBank = bankLevel / std::sqrt (std::max (levelSum, 1.0e-12));

    bool widthUsesChoke = false;
    int sounding = n;
    for (int i = 0; i < std::max (n, numBands); ++i)
    {
        auto& b = bands[(size_t) i];
        const auto m = (size_t) i;
        double targetLeft = 0.0, targetRight = 0.0;   // bands past the range fade out

        if (i < n)
        {
            const double ratio = ratios[m];

            // DRIFT: each band glides to a new random level every 1/RATE s.
            double wander = 1.0, driftCents = 0.0;
            if (drift > 0.0)
            {
                b.driftPhase += driftStep;
                if (b.driftPhase >= 1.0)
                {
                    b.driftPhase -= std::floor (b.driftPhase);
                    b.driftFrom = b.driftTo;
                    b.driftTo = nextRandom();
                }
                const double eased = b.driftFrom + (b.driftTo - b.driftFrom) * (0.5 - 0.5 * std::cos (pi * b.driftPhase));
                wander = 1.0 - drift * (1.0 - eased);    // 1 at no drift; down to 0 at full
                driftCents = 12.0 * drift * (eased - 0.5);   // DRIFT: a little tuning too
            }
            const double cents = 35.0 * (double) p.spread * std::sin ((double) i * 2.4 + 0.7) + driftCents;   // SPREAD: fixed per band
            const double to = rootHz * ratio * (cents == 0.0 ? 1.0 : std::pow (2.0, cents / 1200.0));
            if (b.frequency <= 0.0)
                gainLeft[m] = gainRight[m] = 0.0f;   // a new band fades in
            b.frequency = (snap || b.frequency <= 0.0) ? to : b.frequency + frequencyGlide * (to - b.frequency);
            if (std::abs (b.frequency - to) > 1.0e-4 * to)
                moving = true;

            // Ring time to bandwidth: falling 60 dB in t60 is ln(1000) / (pi * B) wide.
            // While sound comes in the bands stay at least resonanceQ narrow, so
            // they sound pitched even at short decays; once it stops they're
            // choked to the decay time (the S-4's split of RESONANCE and DECAY).
            const double t60 = std::max (0.01, (double) p.decaySeconds) * ringAt (ratio, bright, damp);
            const double ringWidth = std::log (1000.0) / (pi * t60);
            const double heldWidth = std::min (ringWidth, b.frequency / resonanceQ);
            if (heldWidth < ringWidth)
                widthUsesChoke = true;
            const double bandwidth = std::min (heldWidth + (ringWidth - heldWidth) * choke, 2.0 * b.frequency);
            const double radius = std::exp (-pi * bandwidth / sampleRate);
            const double turn = 2.0 * pi * b.frequency / sampleRate;
            turnCos[m] = (float) (radius * std::cos (turn));
            turnSin[m] = (float) (radius * std::sin (turn));

            // Levels by the held width, so the choke doesn't jump the level as sound stops.
            const double strength = std::pow (2.0 * pi * heldWidth / sampleRate, widthExponent);
            const double against = i % 2 == 0 ? std::max (0.0, oddEven) : std::max (0.0, -oddEven);   // ODD/EVEN turns the other set down
            const double level = perBank * strength * levelAt (ratio, bright) * wander * (1.0 - against);
            const double pan = (i % 2 == 0 ? -1.0 : 1.0) * width * (n > 1 ? 1.0 : 0.0);
            const double angle = (pan + 1.0) * pi / 4.0;
            targetLeft = level * std::cos (angle) * std::sqrt (2.0);
            targetRight = level * std::sin (angle) * std::sqrt (2.0);
        }
        else if (std::abs (gainLeft[m]) + std::abs (gainRight[m]) < 1.0e-6f)
        {
            clearBand (i);   // faded out
            continue;
        }
        else
        {
            sounding = i + 1;
            moving = true;
        }

        // This block ramps sample by sample from the current gains to these.
        nextLeft[m] = (float) (snap ? targetLeft : gainLeft[m] + gainGlide * (targetLeft - gainLeft[m]));
        nextRight[m] = (float) (snap ? targetRight : gainRight[m] + gainGlide * (targetRight - gainRight[m]));
        if (snap)
        {
            gainLeft[m] = nextLeft[m];
            gainRight[m] = nextRight[m];
        }
        if (std::abs ((double) nextLeft[m] - targetLeft) + std::abs ((double) nextRight[m] - targetRight) > 2.0e-6)
            moving = true;
    }
    numBands = sounding;
    chokeMatters = widthUsesChoke;
    settled = ! moving;
}

double Resonator::nextRandom() noexcept
{
    randomState = randomState * 1664525u + 1013904223u;
    return (double) (randomState >> 8) / 16777216.0;
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

    // Level match: the rings against the input, held as long as a ring lasts: no
    // more than a sine at its peak, and no more than 3 dB over its average. A tone
    // on a mode otherwise builds far above what went in, and spiky material would
    // come out much louder; a single hit rings well under either, so it's left alone.
    const auto coefficient = [this] (double seconds) { return 1.0 - std::exp (-1.0 / (seconds * sampleRate)); };
    const double peakRelease = coefficient (std::max (0.05, (double) smoothed.decaySeconds / 6.9));
    const double meanSmoothing = coefficient (0.02);
    const double slowRelease = coefficient (0.3);
    const double inAttack = coefficient (0.005);
    // A tone on a long ring builds fast: the meter and the gain are quick to catch it.
    const double wetAttack = coefficient (0.001);
    const double gainDown = coefficient (0.002), gainUp = coefficient (0.3);
    const double chokeGlide = 1.0 - std::exp (-(double) controlInterval / (0.01 * sampleRate));
    // DRIVE: up to 20 dB into a soft clip, so loud parts flatten and add
    // harmonics that ring more bands; the level match keeps the result in line.
    const double drive = std::clamp ((double) smoothed.drive, 0.0, 1.0);
    const double driveGain = 1.0 + 9.0 * drive, driveMakeup = 1.0 + 2.0 * drive;

    for (int start = 0; start < numSamples; start += controlInterval)
    {
        const int len = std::min (controlInterval, numSamples - start);

        // Choke: 0 while sound comes in, 1 once it has fallen 12 dB below its
        // recent level (fully at 12 dB, from 3 dB).
        const double fall = inSlow > 1.0e-10 ? -10.0 * std::log10 (std::max (inMean, 1.0e-20) / inSlow) : 60.0;
        choke += chokeGlide * (std::clamp ((fall - 3.0) / 9.0, 0.0, 1.0) - choke);

        updateCoefficients (p, false);
        const float mixStep = (mixTarget - mixNow) / (float) len;

        // Nothing coming in and nothing ringing: nothing to work out.
        if (bankSilent && mixStep == 0.0f)
        {
            bool silentIn = true;
            for (int i = start; i < start + len && silentIn; ++i)
                silentIn = left[i] == 0.0f && right[i] == 0.0f;
            if (silentIn)
            {
                inMean = inPeak = inPower = inSlow = wetPower = 0.0;
                ringGain = 1.0;
                for (int m = 0; m < numBands; ++m)
                {
                    gainLeft[(size_t) m] = nextLeft[(size_t) m];
                    gainRight[(size_t) m] = nextRight[(size_t) m];
                }
                continue;
            }
        }
        const float rampScale = 1.0f / (float) len;

        // Four modes at a time; a part-filled last group runs its spare modes at zero gain.
        const int modes = (numBands + 3) & ~3;
        float* const reals = re.data();
        float* const imags = im.data();
        const float* const turnC = turnCos.data();
        const float* const turnS = turnSin.data();
        float* const gl = gainLeft.data();
        float* const gr = gainRight.data();
        std::array<float, maxBands> stepLeft, stepRight;
        for (int m = 0; m < modes; ++m)
        {
            stepLeft[(size_t) m] = (nextLeft[(size_t) m] - gl[m]) * rampScale;
            stepRight[(size_t) m] = (nextRight[(size_t) m] - gr[m]) * rampScale;
        }

        for (int i = start; i < start + len; ++i)
        {
            const float inLeft = left[i], inRight = right[i];
            const double dry = 0.5 * ((double) inLeft + (double) inRight);
            const float x = (float) (drive > 0.0 ? std::tanh (dry * driveGain) / driveGain * driveMakeup : dry);
            float sumLeft[4] = {}, sumRight[4] = {};
            for (int m = 0; m < modes; m += 4)
                for (int k = 0; k < 4; ++k)
                {
                    const int j = m + k;
                    const float turnedRe = turnC[j] * reals[j] - turnS[j] * imags[j] + x;
                    const float turnedIm = turnS[j] * reals[j] + turnC[j] * imags[j];
                    reals[j] = turnedRe;
                    imags[j] = turnedIm;
                    gl[j] += stepLeft[(size_t) j];
                    gr[j] += stepRight[(size_t) j];
                    sumLeft[k] += turnedIm * gl[j];
                    sumRight[k] += turnedIm * gr[j];
                }
            const double wetLeft = (double) sumLeft[0] + sumLeft[1] + sumLeft[2] + sumLeft[3];
            const double wetRight = (double) sumRight[0] + sumRight[1] + sumRight[2] + sumRight[3];

            inMean += meanSmoothing * (dry * dry - inMean);
            inSlow += (inMean > inSlow ? inAttack : slowRelease) * (inMean - inSlow);
            inPeak = std::max (dry * dry, inPeak + peakRelease * (dry * dry - inPeak));
            inPower += (inMean > inPower ? inAttack : peakRelease) * (inMean - inPower);
            const double wetNow = 0.5 * (wetLeft * wetLeft + wetRight * wetRight);
            wetPower += (wetNow > wetPower ? wetAttack : meanSmoothing) * (wetNow - wetPower);
            const double allowed = std::min (0.5 * inPeak, 2.0 * inPower);
            const double target = wetPower > allowed ? std::sqrt (allowed / wetPower) : 1.0;
            ringGain += (target < ringGain ? gainDown : gainUp) * (target - ringGain);

            mixNow += mixStep;
            left[i] = inLeft * (1.0f - mixNow) + softClip ((float) (wetLeft * ringGain)) * mixNow;
            right[i] = inRight * (1.0f - mixNow) + softClip ((float) (wetRight * ringGain)) * mixNow;
        }
        mixNow = mixTarget;

        // Landed exactly, and silent rings let go before they reach denormals.
        bool ringing = false;
        for (int m = 0; m < modes; ++m)
        {
            gl[m] = nextLeft[(size_t) m];
            gr[m] = nextRight[(size_t) m];
            if (std::abs (reals[m]) + std::abs (imags[m]) < 1.0e-12f)
                reals[m] = imags[m] = 0.0f;
            else
                ringing = true;
        }
        bankSilent = ! ringing;
    }

    if (mixTarget <= 0.0f)
    {
        active = false;   // faded out: back to passing the input through
        reset();
    }
}

} // namespace silt
