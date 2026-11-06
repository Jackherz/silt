#include <silt/Resonator.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

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

double Resonator::rippleAt (double ratio, double top, const ResonatorParams& p) noexcept
{
    const double depth = std::clamp ((double) p.rippleDepth, 0.0, 1.0);
    if (depth <= 0.0)
        return 1.0;
    // Evenly up the octaves, so a peak spends as long on the low modes as the high ones.
    const double along = top > 1.0 ? std::log2 (std::max (ratio, 1.0)) / std::log2 (top) : 0.0;
    const double wave = 0.5 + 0.5 * std::cos (2.0 * pi * ((double) p.ripple * along - (double) p.sweep));
    return std::pow (wave, depth * 8.0);
}

double Resonator::ringAt (double ratio, double bright, double damp) noexcept
{
    return std::pow (ratio, -0.7 * (1.0 - std::clamp (bright, 0.0, 1.0)) - 1.5 * damp);
}

int Resonator::bankModes (const ResonatorParams& p, std::array<BankMode, maxBands>& modes) noexcept
{
    const double top = std::pow (2.0, std::clamp ((double) p.rangeOctaves, 0.0, 8.0)) * (1.0 + 1.0e-9);
    constexpr int maxNotes = (int) std::tuple_size_v<decltype (p.chord)> + 1;
    const int chordSize = std::clamp (p.chordSize, 0, maxNotes - 1);
    const int noteCount = chordSize + 1;

    // Each note's candidates sit in their own stretch of this; no clearing: this runs on the audio thread.
    std::array<double, maxNotes * maxBands> candidates;
    std::array<int, maxNotes> firstOf, countOf;
    std::array<double, maxNotes> shiftOf;

    // One note's bands, `offset` semitones over the root.
    const auto bandsFor = [&] (int note, double offset)
    {
        const double shift = std::pow (2.0, offset / 12.0), limit = top * shift;
        double* const out = candidates.data() + note * maxBands;
        int count = 0;
        const auto add = [&] (double ratio, double within)
        {
            if (ratio <= within && count < maxBands)
                out[count++] = ratio;
        };
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
        std::sort (out, out + count);

        // The same band twice (a series' octaves meeting) rings once.
        int distinct = 0;
        for (int i = 0; i < count; ++i)
            if (distinct == 0 || out[i] > out[distinct - 1] * 1.003)
                out[distinct++] = out[i];

        firstOf[(size_t) note] = note * maxBands;
        countOf[(size_t) note] = distinct;
        shiftOf[(size_t) note] = shift;
    };

    bandsFor (0, 0.0);
    for (int c = 0; c < chordSize; ++c)
        bandsFor (c + 1, (double) p.chord[(size_t) c]);

    // BANDS shared out a mode at a time, the lower notes first. A note with few candidates
    // leaves its share to the others.
    std::array<int, maxNotes> shareOf {};
    int budget = std::min (maxBands, std::max (1, p.bandCount));
    for (bool given = true; given && budget > 0;)
    {
        given = false;
        for (int note = 0; note < noteCount && budget > 0; ++note)
            if (shareOf[(size_t) note] < countOf[(size_t) note])
            {
                ++shareOf[(size_t) note];
                --budget;
                given = true;
            }
    }

    // Page 3: each note's series reshaped from its own fundamental, so a chord stays a chord.
    const double stretch = std::clamp ((double) p.stretch, -1.0, 1.0);
    const double snap = std::clamp ((double) p.snap, 0.0, 1.0);
    const double offset = std::clamp ((double) p.offset, -12.0, 12.0);
    const double power = std::pow (4.0, std::clamp ((double) p.stretchCurve, -1.0, 1.0));
    const double offsetRatio = std::pow (2.0, offset / 12.0);

    // SAMPLE: how strong the sample's mode at this ratio to its note is.
    const int sampleCount = std::clamp (p.sampleCount, 0, (int) p.sampleHz.size());
    const double rootHzHere = 440.0 * std::pow (2.0, ((double) p.rootNote - 69.0) / 12.0);
    const auto weightOf = [&] (double ratio)
    {
        if (p.notes != ResonatorNotes::sample || sampleCount == 0)
            return 1.0;
        const auto end = p.sampleHz.begin() + sampleCount;
        auto nearest = std::lower_bound (p.sampleHz.begin(), end, (float) (ratio * rootHzHere));
        if (nearest == end || (nearest != p.sampleHz.begin() && ratio * rootHzHere - (double) *(nearest - 1) < (double) *nearest - ratio * rootHzHere))
            --nearest;
        const float level = p.sampleLevel[(size_t) (nearest - p.sampleHz.begin())];
        return level > 0.0f ? (double) level : 1.0;
    };

    int total = 0;
    for (int note = 0; note < noteCount; ++note)
    {
        const int n = shareOf[(size_t) note], count = countOf[(size_t) note];
        const double shift = shiftOf[(size_t) note];
        const double* const from = candidates.data() + firstOf[(size_t) note];
        BankMode* const out = modes.data() + total;

        // More than fit: a pick from lowest to highest. A lone note's is even; in a chord the
        // share is smaller, so it leans low, where a note's pitch is heard, and still reaches the top.
        int previous = -1;
        for (int i = 0; i < n; ++i)
        {
            const double along = n < 2 ? 0.0 : (double) i / (n - 1);
            const int picked = (int) std::lround ((double) (count - 1) * (noteCount > 1 ? along * along : along));
            previous = noteCount > 1 ? std::max (picked, previous + 1) : picked;
            double own = from[previous] / shift;
            if (stretch != 0.0 && n > 1)
                own *= std::pow (2.0, 3.0 * stretch * std::pow (along, power));
            if (snap > 0.0)
                own += (std::max (1.0, std::round (own)) - own) * snap;
            if (i % 2 == 1)
                own *= offsetRatio;
            out[i].own = own;
            out[i].weight = weightOf (from[previous] / shift);
            out[i].ratio = own * shift;
            out[i].note = note;
        }
        // Squeezed hard, they can cross.
        std::sort (out, out + n, [] (const BankMode& a, const BankMode& b) { return a.ratio < b.ratio; });
        for (int i = 0; i < n; ++i)
            out[i].partial = i;
        total += n;
    }

    if (noteCount == 1)
        return total;
    std::sort (modes.begin(), modes.begin() + total, [] (const BankMode& a, const BankMode& b) { return a.ratio < b.ratio; });

    // Two notes on the same band (one's fifth harmonic, say, on another's third) ring once,
    // as whichever it's lowest in the series of.
    int distinct = 0;
    for (int i = 0; i < total; ++i)
    {
        const auto& mode = modes[(size_t) i];
        if (distinct > 0 && mode.note != modes[(size_t) distinct - 1].note && mode.ratio <= modes[(size_t) distinct - 1].ratio * 1.003)
        {
            if (mode.partial < modes[(size_t) distinct - 1].partial)
                modes[(size_t) distinct - 1] = mode;
            continue;
        }
        modes[(size_t) distinct++] = mode;
    }
    return distinct;
}

int Resonator::bandRatios (const ResonatorParams& p, std::array<double, maxBands>& ratios) noexcept
{
    std::array<BankMode, maxBands> modes;
    const int n = bankModes (p, modes);
    for (int i = 0; i < n; ++i)
        ratios[(size_t) i] = modes[(size_t) i].ratio;
    return n;
}

// A chord changing: modes the new chord still has stay in their slots, ringing on as they
// were. The rest move to the nearest free slots, low to low, so they have the least way to go.
void Resonator::keepSharedNotes (const std::array<BankMode, maxBands>& modes, int count, double rootHz) noexcept
{
    std::array<bool, maxBands> slotKept {}, modeKept {};
    for (int k = 0; k < count; ++k)
    {
        const double hz = rootHz * modes[(size_t) k].ratio;
        for (int m = 0; m < maxBands; ++m)
            if (! slotKept[(size_t) m] && bands[(size_t) m].nominal > 0.0 && std::abs (bands[(size_t) m].nominal / hz - 1.0) < 0.003)
            {
                slotOf[(size_t) k] = (short) m;
                slotKept[(size_t) m] = modeKept[(size_t) k] = true;
                break;
            }
    }

    std::array<short, maxBands> freeSlots;
    int numFree = 0;
    for (int m = 0; m < maxBands; ++m)
        if (! slotKept[(size_t) m])
            freeSlots[(size_t) numFree++] = (short) m;
    std::sort (freeSlots.begin(), freeSlots.begin() + numFree, [this] (short a, short b)
    {
        const double x = bands[(size_t) a].nominal, y = bands[(size_t) b].nominal;
        if ((x > 0.0) != (y > 0.0))
            return x > 0.0;   // ringing slots first: free ones are for what's left over
        return x != y ? x < y : a < b;
    });
    int next = 0;
    for (int k = 0; k < maxBands; ++k)
        if (! modeKept[(size_t) k])
            slotOf[(size_t) k] = freeSlots[(size_t) next++];
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
    strikeShare.fill (0.0f);
    modeRatio.fill (0.0f);
    previousIn = previousWet = 0.0;
    numBands = 0;
    for (int m = 0; m < maxBands; ++m)
        slotOf[(size_t) m] = (short) m;
    slottedRoot = 0.0f;
    slottedChord.fill (0.0f);
    slottedChordSize = 0;
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
    strikeShare[i] = modeRatio[i] = 0.0f;
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
    glideTo (smoothed.stretch, target.stretch);
    glideTo (smoothed.stretchCurve, target.stretchCurve);
    glideTo (smoothed.snap, target.snap);
    glideTo (smoothed.offset, target.offset);
    glideTo (smoothed.rippleDepth, target.rippleDepth);
    smoothed.ripple = target.ripple;
    glideTo (smoothed.sweep, target.sweep);
    glideTo (smoothed.hardness, target.hardness);
    glideTo (smoothed.particles, target.particles);
    glideTo (smoothed.top, target.top);
    glideTo (smoothed.saturate, target.saturate);
    glideTo (smoothed.strike, target.strike);
    smoothed.particleDensity = target.particleDensity;
    smoothed.driftRate = target.driftRate;
    smoothed.bandCount = target.bandCount;
    smoothed.rootNote = target.rootNote;   // the band frequencies glide instead
    smoothed.notes = target.notes;
    smoothed.sampleHz = target.sampleHz;
    smoothed.sampleCount = target.sampleCount;
    smoothed.sampleLevel = target.sampleLevel;
    smoothed.chord = target.chord;
    smoothed.chordSize = target.chordSize;
    smoothed.scale = target.scale;
    smoothed.key = target.key;
    const auto& p = smoothed;

    std::array<BankMode, maxBands> modes;
    const int count = bankModes (p, modes);
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
    while (n < count && rootHz * modes[(size_t) n].ratio < nyquistLimit)
        ++n;

    if (p.rootNote != slottedRoot || p.chordSize != slottedChordSize || p.chord != slottedChord)
    {
        if (! snap && (p.chordSize > 0 || slottedChordSize > 0))
            keepSharedNotes (modes, n, rootHz);
        slottedRoot = p.rootNote;
        slottedChord = p.chord;
        slottedChordSize = p.chordSize;
    }

    // Shared across the bank, so the level doesn't change with how many modes there are.
    const double top = n > 0 ? modes[(size_t) n - 1].ratio : 1.0;
    double levelSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto& mode = modes[(size_t) i];
        const double level = mode.weight * levelAt (mode.own, bright) * rippleAt (mode.ratio, top, p);
        levelSum += level * level;
    }
    const double perBank = bankLevel / std::sqrt (std::max (levelSum, 1.0e-12));

    bool widthUsesChoke = false;
    int sounding = 0;
    for (int k = 0; k < maxBands; ++k)
    {
        const int i = slotOf[(size_t) k];
        auto& b = bands[(size_t) i];
        const auto m = (size_t) i;
        if (k >= n && b.frequency <= 0.0)
            continue;
        double targetLeft = 0.0, targetRight = 0.0;   // bands past the range fade out

        if (k < n)
        {
            const auto& mode = modes[(size_t) k];
            const double ratio = mode.ratio, own = mode.own;
            b.nominal = rootHz * ratio;
            sounding = std::max (sounding, i + 1);

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
            const double t60 = std::max (0.01, (double) p.decaySeconds) * ringAt (own, bright, damp);
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
            const double against = mode.partial % 2 == 0 ? std::max (0.0, oddEven) : std::max (0.0, -oddEven);   // ODD/EVEN turns the other set down
            const double level = perBank * strength * mode.weight * levelAt (own, bright) * rippleAt (ratio, top, p) * wander * (1.0 - against);
            // A hit: the same ring at any width, so the strength cancels, softer up high as HARDNESS falls.
            strikeShare[m] = (float) (std::pow (own, -2.0 * (1.0 - std::clamp ((double) p.hardness, 0.0, 1.0))) / strength);
            modeRatio[m] = (float) own;
            const double pan = ((mode.partial + mode.note) % 2 == 0 ? -1.0 : 1.0) * width * (n > 1 ? 1.0 : 0.0);
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
            sounding = std::max (sounding, i + 1);
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

// A grain hitting the bank: every mode kicked at once, by how much that point on the object
// moves in that mode (the sine a string makes), so where it lands decides which ones ring.
void Resonator::strikeBank (const GrainStrike& hit, float amount) noexcept
{
    const float position = std::clamp (hit.position, 0.0f, 1.0f);
    constexpr float fullStrike = 0.6f;   // STRIKE at 100%: a lone hit at about the soft clip's knee
    const float kick = fullStrike * amount * std::clamp (hit.level, 0.0f, 1.0f);
    if (kick <= 0.0f)
        return;
    for (int m = 0; m < numBands; ++m)
        re[(size_t) m] += kick * strikeShare[(size_t) m] * std::sin ((float) pi * modeRatio[(size_t) m] * position);
    bankSilent = false;
    // It sets its own ceiling, or the level match would see a loud ring from a quiet input.
    inPeak = std::max (inPeak, (double) kick * kick);
    inPower = std::max (inPower, (double) kick * kick * 0.25);
}

double Resonator::nextRandom() noexcept
{
    randomState = randomState * 1664525u + 1013904223u;
    return (double) (randomState >> 8) / 16777216.0;
}

void Resonator::process (float* left, float* right, int numSamples, const ResonatorParams& p,
                         const GrainStrike* strikes, int numStrikes) noexcept
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
    const float strikeAmount = strikes != nullptr ? std::clamp (smoothed.strike, 0.0f, 1.0f) : 0.0f;
    const double topLift = 6.0 * std::clamp ((double) smoothed.top, 0.0, 1.0);
    const double particles = std::clamp ((double) smoothed.particles, 0.0, 1.0);
    const double particleChance = std::clamp ((double) smoothed.particleDensity, 1.0, sampleRate) / sampleRate;
    const double particleSize = particles * std::sqrt (1.0 / particleChance);   // the same energy at any density
    // SATURATE: each mode has its own soft ceiling, which can only take energy away, so
    // this can't run away however long the rings. What the saturation of the whole adds goes
    // back in: nothing at low level, a bounded push at high, ringing the modes it lands on.
    const double saturate = std::clamp ((double) smoothed.saturate, 0.0, 1.0);
    const double saturateGain = 1.0 + 9.0 * saturate, saturateSend = 0.5 * saturate;
    const float saturateCeiling = (float) (0.02 * saturate * saturate);
    int nextStrike = strikeAmount > 0.0f ? 0 : numStrikes;

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
        if (bankSilent && mixStep == 0.0f && nextStrike >= numStrikes)
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
            double excite = drive > 0.0 ? std::tanh (dry * driveGain) / driveGain * driveMakeup : dry;
            if (topLift > 0.0)
            {
                const double difference = excite - previousIn;
                previousIn = excite;
                excite += topLift * difference;
            }
            if (particles > 0.0 && nextRandom() < particleChance)
                excite += (nextRandom() < 0.5 ? -particleSize : particleSize) * std::sqrt (inMean);
            if (saturate > 0.0)
            {
                const double w = previousWet * ringGain * saturateGain;
                excite += saturateSend * (std::tanh (w) - w / (1.0 + w * w)) / saturateGain;
            }
            const float x = (float) excite;
            while (nextStrike < numStrikes && strikes[nextStrike].offset <= i)
                strikeBank (strikes[nextStrike++], strikeAmount);
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
            if (saturate > 0.0)
            {
                previousWet = 0.5 * (wetLeft + wetRight);
                for (int j = 0; j < modes; ++j)
                {
                    const float power = (reals[j] * reals[j] + imags[j] * imags[j]) * (gl[j] * gl[j] + gr[j] * gr[j]);
                    const float keep = 1.0f / (1.0f + saturateCeiling * power);
                    reals[j] *= keep;
                    imags[j] *= keep;
                }
            }

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
