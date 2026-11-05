#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <silt/Color.h>
#include <silt/Filter.h>
#include <kit/TapeInput.h>
#include <silt/Pitches.h>
#include <silt/Resonator.h>

#include <cmath>
#include <functional>
#include <vector>

using namespace silt;

namespace
{
constexpr double rate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
    explicit Stereo (int n) : left ((size_t) n), right ((size_t) n) {}
};

Stereo noise (int n, float level, std::uint64_t seed = 7)
{
    Stereo s (n);
    Random r (seed);
    for (int i = 0; i < n; ++i)
        s.left[(size_t) i] = s.right[(size_t) i] = level * (2.0f * r.next01() - 1.0f);
    return s;
}

void run (Resonator& res, Stereo& s, const ResonatorParams& p, int block = 512)
{
    for (int i = 0; i < (int) s.left.size(); i += block)
    {
        const int n = std::min (block, (int) s.left.size() - i);
        res.process (s.left.data() + i, s.right.data() + i, n, p);
    }
}

double rms (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (sum / std::max (1, to - from));
}

double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

// One band at the root: harmonics with no range above it.
ResonatorParams singleBand (float note, float decay)
{
    ResonatorParams p;
    p.notes = ResonatorNotes::harmonics;
    p.rangeOctaves = 0.0f;
    p.rootNote = note;
    p.decaySeconds = decay;
    p.mix = 1.0f;
    return p;
}
} // namespace

TEST_CASE ("Resonator: at zero mix the input passes through untouched")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise (10000, 0.5f);
    const auto original = s.left;
    ResonatorParams p;   // mix 0
    run (res, s, p);
    CHECK (s.left == original);
    CHECK (s.right == original);
}

TEST_CASE ("Resonator: bands follow the scale, harmonics and range")
{
    std::array<double, Resonator::maxBands> r {};

    ResonatorParams p;
    p.notes = ResonatorNotes::harmonics;
    p.rangeOctaves = 2.0f;
    REQUIRE (Resonator::bandRatios (p, r) == 4);
    for (int i = 0; i < 4; ++i)
        CHECK_THAT (r[(size_t) i], Catch::Matchers::WithinAbs (i + 1.0, 1e-9));

    // C major from C3 over one octave: C D E F G A B C.
    p.notes = ResonatorNotes::scale;
    p.scale = Scale::major;
    p.key = 0;
    p.rootNote = 48.0f;
    p.rangeOctaves = 1.0f;
    REQUIRE (Resonator::bandRatios (p, r) == 8);
    const int steps[] { 0, 2, 4, 5, 7, 9, 11, 12 };
    for (int i = 0; i < 8; ++i)
        CHECK_THAT (r[(size_t) i], Catch::Matchers::WithinAbs (std::pow (2.0, steps[i] / 12.0), 1e-9));

    // An off-scale root snaps in: C# in C major rings from a scale note.
    p.rootNote = 49.0f;
    Resonator::bandRatios (p, r);
    const double first = 12.0 * std::log2 (r[0]) + 49.0;
    CHECK ((std::lround (first) == 48 || std::lround (first) == 50));

    // No scale: the root and its fifths.
    p.scale = Scale::off;
    p.rootNote = 45.0f;
    p.rangeOctaves = 2.0f;
    REQUIRE (Resonator::bandRatios (p, r) == 5);   // A, E, A, E, A

    // Never more than twelve bands, spread evenly up the range.
    p.scale = Scale::chromatic;
    p.rangeOctaves = 4.0f;
    REQUIRE (Resonator::bandRatios (p, r) == Resonator::maxBands);
    CHECK_THAT (r[0], Catch::Matchers::WithinAbs (1.0, 1e-9));
    CHECK_THAT (r[11], Catch::Matchers::WithinAbs (16.0, 1e-9));
}

TEST_CASE ("Resonator: a band rings at its note")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise ((int) rate * 2, 0.3f);
    run (res, s, singleBand (69.0f, 2.0f));   // A4

    // Zero crossings over the second second: two per cycle.
    int crossings = 0;
    for (int i = (int) rate + 1; i < (int) rate * 2; ++i)
        crossings += (s.left[(size_t) i - 1] < 0.0f) != (s.left[(size_t) i] < 0.0f);
    CHECK (std::abs (crossings / 2.0 - 440.0) < 440.0 * 0.03);
}

TEST_CASE ("Resonator: the ring falls 60 dB in the decay time")
{
    for (const float decay : { 0.3f, 1.5f })
    {
        Resonator res;
        res.prepare (rate);
        Stereo s ((int) (rate * (decay + 0.5)));
        s.left[100] = s.right[100] = 1.0f;   // an impulse
        run (res, s, singleBand (57.0f, decay));

        const int window = 480;
        const int early = 2000, late = early + (int) (decay * rate);
        const double drop = db (rms (s.left, early, early + window)) - db (rms (s.left, late, late + window));
        INFO ("decay " << decay << " s: " << drop << " dB");
        CHECK (std::abs (drop - 60.0) < 4.0);
    }
}

TEST_CASE ("Resonator: short and long rings sit at a similar level")
{
    std::vector<double> levels;
    for (const float decay : { 0.1f, 1.0f, 8.0f })
    {
        Resonator res;
        res.prepare (rate);
        auto s = noise ((int) rate * 4, 0.2f);
        ResonatorParams p;
        p.scale = Scale::minor;
        p.mix = 1.0f;
        p.decaySeconds = decay;
        run (res, s, p);
        levels.push_back (db (rms (s.left, (int) rate * 2, (int) rate * 4)) - db (0.2 / std::sqrt (3.0)));
        INFO ("decay " << decay << " s: " << levels.back() << " dB against the input");
        CHECK (levels.back() > -18.0);
        CHECK (levels.back() < 3.0);
    }
    CHECK (std::abs (levels.front() - levels.back()) < 12.0);
}

TEST_CASE ("Resonator: stays finite and bounded at extreme settings")
{
    for (const auto notes : { ResonatorNotes::scale, ResonatorNotes::harmonics, ResonatorNotes::bell })
        for (const float root : { 0.0f, 60.0f, 127.0f })
        {
            Resonator res;
            res.prepare (rate);
            auto s = noise ((int) rate, 1.0f);
            ResonatorParams p;
            p.notes = notes;
            p.rootNote = root;
            p.rangeOctaves = 4.0f;
            p.decaySeconds = 30.0f;
            p.bright = 1.0f;
            p.width = 1.0f;
            p.mix = 1.0f;
            run (res, s, p, 64);
            for (size_t i = 0; i < s.left.size(); ++i)
            {
                REQUIRE (std::isfinite (s.left[i]));
                REQUIRE (std::abs (s.left[i]) <= 1.0f);
                REQUIRE (std::abs (s.right[i]) <= 1.0f);
            }
        }
}

TEST_CASE ("Resonator: sweeping the tune glides without blowing up")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise ((int) rate * 2, 0.5f);
    auto p = singleBand (40.0f, 5.0f);
    p.notes = ResonatorNotes::harmonics;
    p.rangeOctaves = 3.0f;
    for (int i = 0; i < (int) s.left.size(); i += 64)
    {
        p.rootNote = 40.0f + 50.0f * (float) i / (float) s.left.size();
        res.process (s.left.data() + i, s.right.data() + i, 64, p);
    }
    for (const float x : s.left)
        REQUIRE (std::isfinite (x));
}

TEST_CASE ("Resonator: fading the mix out returns to an exact pass-through")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise (4096, 0.5f);
    auto p = singleBand (60.0f, 1.0f);
    run (res, s, p);
    p.mix = 0.0f;
    run (res, s, p);   // fades out over the first block, then stops

    auto after = noise (2048, 0.5f, 99);
    const auto original = after.left;
    run (res, after, p);
    CHECK (after.left == original);
}

TEST_CASE ("Resonator: width moves alternate bands apart")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise ((int) rate, 0.3f);
    ResonatorParams p;
    p.scale = Scale::major;
    p.mix = 1.0f;
    p.width = 1.0f;
    run (res, s, p);
    double difference = 0.0;
    for (size_t i = (size_t) rate / 2; i < s.left.size(); ++i)
        difference += std::abs (s.left[i] - s.right[i]);
    CHECK (difference > 1.0);

    Resonator mono;
    mono.prepare (rate);
    auto m = noise ((int) rate, 0.3f);
    p.width = 0.0f;
    run (mono, m, p);
    CHECK (m.left == m.right);
}

TEST_CASE ("Resonator: a tone on a band rings no louder than it went in")
{
    for (const float decay : { 0.3f, 2.0f, 12.0f })
    {
        Resonator res;
        res.prepare (rate);
        Stereo s ((int) rate * 3);
        for (size_t i = 0; i < s.left.size(); ++i)   // A3, a band of A minor
            s.left[i] = s.right[i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / rate);
        ResonatorParams p;
        p.scale = Scale::minor;
        p.key = 9;
        p.rootNote = 57.0f;
        p.decaySeconds = decay;
        p.mix = 1.0f;
        run (res, s, p);

        const double bothChannels = std::sqrt (0.5 * (std::pow (rms (s.left, (int) rate * 2, (int) rate * 3), 2.0)
                                                     + std::pow (rms (s.right, (int) rate * 2, (int) rate * 3), 2.0)));
        const double gain = db (bothChannels) - db (0.25 / std::sqrt (2.0));
        float peak = 0.0f;
        for (size_t i = 0; i < s.left.size(); ++i)
            peak = std::max ({ peak, std::abs (s.left[i]), std::abs (s.right[i]) });
        INFO ("decay " << decay << " s: " << gain << " dB, peak " << peak);
        CHECK (gain < 1.5);
        CHECK (peak < 0.7f);   // below the soft clip's knee: no saturation
    }
}

namespace
{
// How alike the signal is to itself one period later: near 1 for a clear
// pitch, low for something close to the noise that went in.
double periodicity (const std::vector<float>& x, int from, int to, int lag)
{
    double cross = 0.0, a = 0.0, b = 0.0;
    for (int i = from; i < to; ++i)
    {
        cross += (double) x[(size_t) i] * x[(size_t) (i + lag)];
        a += (double) x[(size_t) i] * x[(size_t) i];
        b += (double) x[(size_t) (i + lag)] * x[(size_t) (i + lag)];
    }
    return cross / std::sqrt (a * b + 1.0e-30);
}

// Energy in the second difference: clicks and zipper steps light it up; a
// smooth low tone hardly does.
double roughness (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from + 2; i < to; ++i)
    {
        const double d = (double) x[(size_t) i] - 2.0 * x[(size_t) i - 1] + x[(size_t) i - 2];
        sum += d * d;
    }
    return sum / (to - from);
}
} // namespace

TEST_CASE ("Resonator: a short decay still sounds pitched while sound comes in")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise ((int) rate, 0.3f);
    run (res, s, singleBand (69.0f, 0.05f));   // A4, 50 ms
    const double alike = periodicity (s.left, (int) rate / 2, (int) rate - 200, (int) std::lround (rate / 440.0));
    INFO ("periodicity " << alike);
    CHECK (alike > 0.8);
}

TEST_CASE ("Resonator: a short decay stops soon after the sound does")
{
    Resonator res;
    res.prepare (rate);
    auto s = noise ((int) rate, 0.3f);
    const int stop = (int) rate / 2;
    for (int i = stop; i < (int) rate; ++i)
        s.left[(size_t) i] = s.right[(size_t) i] = 0.0f;
    auto p = singleBand (45.0f, 0.05f);   // A2: at Q 30 it would ring ~1 s unchoked
    run (res, s, p);
    const double during = db (rms (s.left, stop - 4800, stop));
    const double after = db (rms (s.left, stop + 9600, stop + 14400));   // 200–300 ms later
    INFO ("during " << during << " dB, 200 ms after " << after << " dB");
    CHECK (during - after > 40.0);
}

TEST_CASE ("Resonator: sweeping range, decay, bright and width doesn't zipper")
{
    const auto render = [] (int which, bool sweep)
    {
        Resonator res;
        res.prepare (rate);
        Stereo s ((int) rate * 2);
        for (size_t i = 0; i < s.left.size(); ++i)   // a steady low tone on a band
            s.left[i] = s.right[i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 110.0 * (double) i / rate);
        ResonatorParams p;
        p.scale = Scale::minor;
        p.key = 9;
        p.rootNote = 45.0f;
        p.decaySeconds = 2.0f;
        p.mix = 1.0f;
        for (int i = 0; i < (int) s.left.size(); i += 256)   // a host block, knob moved each block
        {
            const float t = sweep ? 0.5f + 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 2.0 * i / rate) : 0.5f;
            switch (which)
            {
                case 0: p.rangeOctaves = 1.0f + 3.0f * t; break;
                case 1: p.decaySeconds = 0.05f * std::pow (200.0f, t); break;
                case 2: p.bright = t; break;
                default: p.width = t; break;
            }
            res.process (s.left.data() + i, s.right.data() + i, std::min (256, (int) s.left.size() - i), p);
        }
        // Roughness against level, so a louder sweep isn't counted as zipper.
        const double level = rms (s.left, (int) rate / 2, (int) rate * 2);
        return roughness (s.left, (int) rate / 2, (int) rate * 2) / (level * level + 1.0e-20);
    };

    const char* names[] { "range", "decay", "bright", "width" };
    for (int which = 0; which < 4; ++which)
    {
        const double still = render (which, false), swept = render (which, true);
        INFO (names[which] << ": swept " << swept << ", still " << still);
        CHECK (swept < still * 4.0);
    }
}

// --- Filter -----------------------------------------------------------------------

namespace
{
Stereo sine (int n, double hz, float level)
{
    Stereo s (n);
    for (int i = 0; i < n; ++i)
        s.left[(size_t) i] = s.right[(size_t) i] = level * (float) std::sin (2.0 * 3.14159265358979 * hz * i / rate);
    return s;
}

void run (Filter& f, Stereo& s, const FilterParams& p, int block = 512)
{
    for (int i = 0; i < (int) s.left.size(); i += block)
        f.process (s.left.data() + i, s.right.data() + i, std::min (block, (int) s.left.size() - i), p);
}

// Gain in dB of a sine through the filter, measured after it settles.
double gainAt (double hz, const FilterParams& p, float level = 0.1f)
{
    Filter f;
    f.prepare (rate);
    auto s = sine ((int) rate / 2, hz, level);
    run (f, s, p);
    return db (rms (s.left, (int) rate / 4, (int) rate / 2)) - db (level / std::sqrt (2.0));
}

// The level of one frequency in a settled stretch, in dB.
double toneLevel (const std::vector<float>& x, double hz, int from, int to)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (i - from) / (to - from));
        const double phase = 2.0 * 3.14159265358979 * hz * i / rate;
        re += hann * x[(size_t) i] * std::cos (phase);
        im += hann * x[(size_t) i] * std::sin (phase);
    }
    return db (4.0 * std::sqrt (re * re + im * im) / (to - from));
}

FilterParams lowPass (FilterSlope slope)
{
    FilterParams p;
    p.slope = slope;
    p.cutoffHz = 1000.0f;
    return p;
}
} // namespace

TEST_CASE ("Filter: off passes the input through untouched")
{
    Filter f;
    f.prepare (rate);
    auto s = noise (10000, 0.5f);
    const auto original = s.left;
    run (f, s, FilterParams {});
    CHECK (s.left == original);
}

TEST_CASE ("Filter: low pass at 12 and 24 dB per octave")
{
    for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
    {
        const auto p = lowPass (slope);
        const double pass = gainAt (100.0, p), corner = gainAt (1000.0, p), stop = gainAt (8000.0, p);
        INFO ((slope == FilterSlope::db12 ? "12" : "24") << " dB: 100 Hz " << pass << ", 1 kHz " << corner << ", 8 kHz " << stop);
        CHECK (std::abs (pass) < 0.5);
        CHECK (std::abs (corner + 3.0) < 1.0);
        CHECK (stop < (slope == FilterSlope::db12 ? -30.0 : -60.0));
    }
}

TEST_CASE ("Filter: morph moves from low pass through band pass to high pass")
{
    auto p = lowPass (FilterSlope::db12);
    p.morph = 1.0f;   // band pass
    CHECK (gainAt (1000.0, p) - gainAt (100.0, p) > 12.0);
    CHECK (gainAt (1000.0, p) - gainAt (10000.0, p) > 12.0);
    p.morph = 2.0f;   // high pass
    CHECK (gainAt (100.0, p) < -30.0);
    CHECK (std::abs (gainAt (10000.0, p)) < 0.5);
}

TEST_CASE ("Filter: resonance raises a peak at the cutoff")
{
    for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
    {
        auto p = lowPass (slope);
        constexpr float quiet = 0.003f;
        const double flat = gainAt (1000.0, p, quiet) - gainAt (100.0, p, quiet);
        p.resonance = 1.0f;
        const double peaked = gainAt (1000.0, p, quiet) - gainAt (100.0, p, quiet);
        INFO ("peak over the passband: " << flat << " dB flat, " << peaked << " dB resonant");
        CHECK (peaked > flat + 20.0);
    }
}

TEST_CASE ("Filter: a loud signal presses the resonance down instead of spiking through it")
{
    for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
    {
        auto p = lowPass (slope);
        p.resonance = 1.0f;
        const double quiet = gainAt (1000.0, p, 0.003f), loud = gainAt (1000.0, p, 0.5f);
        INFO ("gain at the peak: " << quiet << " dB quiet, " << loud << " dB loud");
        CHECK (loud < quiet - 10.0);
        CHECK (loud < 8.0);
    }
}

TEST_CASE ("Filter: DRIVE warms with low harmonics and doesn't fold back")
{
    auto p = lowPass (FilterSlope::db24);
    p.cutoffHz = 20000.0f;
    p.drive = 1.0f;
    const auto through = [&p] (double hz)
    {
        Filter f;
        f.prepare (rate);
        auto s = sine ((int) rate, hz, 0.5f);
        run (f, s, p);
        return s;
    };
    const int from = (int) rate / 2, to = (int) rate;

    const auto low = through (220.0);
    const double fundamental = toneLevel (low.left, 220.0, from, to);
    CHECK (toneLevel (low.left, 440.0, from, to) > fundamental - 50.0);   // the even harmonic is there
    CHECK (toneLevel (low.left, 660.0, from, to) > fundamental - 40.0);

    // A loud 7 kHz at full DRIVE is nearly square. 35 kHz would land on 13 kHz at the plain
    // sample rate; what's left there is the very high harmonics, the floor of running at 2x.
    const auto high = through (7000.0);
    const double foldedOver = toneLevel (high.left, rate - 35000.0, from, to) - toneLevel (high.left, 7000.0, from, to);
    INFO ("folded harmonic " << foldedOver << " dB under the tone");
    CHECK (foldedOver < -50.0);
}

TEST_CASE ("Filter: a low pass all the way down is closed, and it opens smoothly")
{
    for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
    {
        auto p = lowPass (slope);
        p.cutoffHz = 20.0f;
        CHECK (gainAt (15.0, p) < -80.0);    // under the cutoff too
        CHECK (gainAt (100.0, p) < -80.0);
        p.cutoffHz = 60.0f;
        CHECK (gainAt (30.0, p) > -3.0);     // open again a little way up
        p.cutoffHz = 20.0f;
        p.morph = 2.0f;                      // a high pass down there stays open
        CHECK (std::abs (gainAt (2000.0, p)) < 0.5);
    }
}

TEST_CASE ("INPUT past 0 dB meets the tape: a soft ceiling, not a peak")
{
    // A full-scale sine pushed by this many dB, after the tape.
    const auto driven = [] (double hz, float db)
    {
        kit::TapeInput tape;
        tape.prepare (rate);
        auto s = sine ((int) rate, hz, 1.0f);
        const float gain = std::pow (10.0f, db / 20.0f);
        for (int i = 0; i < (int) s.left.size(); i += 64)
        {
            const int n = std::min (64, (int) s.left.size() - i);
            for (int k = 0; k < n; ++k)
            {
                s.left[(size_t) (i + k)] *= gain;
                s.right[(size_t) (i + k)] *= gain;
            }
            tape.process (s.left.data() + i, s.right.data() + i, n, db, db);
        }
        return s;
    };

    // At or below 0 dB, bit for bit.
    {
        auto flat = sine (4800, 220.0, 0.5f);
        const auto original = flat.left;
        kit::TapeInput tape;
        tape.prepare (rate);
        tape.process (flat.left.data(), flat.right.data(), 4800, 0.0f, 0.0f);
        CHECK (flat.left == original);
    }

    for (const float db : { 3.0f, 12.0f, 24.0f })
    {
        const auto s = driven (220.0, db);
        float most = 0.0f;
        for (int i = (int) rate / 2; i < (int) rate; ++i)
            most = std::max (most, std::abs (s.left[(size_t) i]));
        INFO (db << " dB: peak " << most);
        CHECK (most < 1.05f);
        CHECK (most > 0.6f);
    }

    const int from = (int) rate / 2, to = (int) rate;
    const auto low = driven (220.0, 18.0f);
    const double one = toneLevel (low.left, 220.0, from, to);
    CHECK (toneLevel (low.left, 660.0, from, to) > one - 30.0);   // odd harmonics: it's rounding over
    CHECK (toneLevel (low.left, 440.0, from, to) > one - 50.0);   // and a little even: it leans

    // The more it's pushed, the less top survives, relative to the same tone pushed gently.
    const auto topAt = [&] (float db) { return toneLevel (driven (8000.0, db).left, 8000.0, from, to) - toneLevel (driven (220.0, db).left, 220.0, from, to); };
    CHECK (topAt (24.0f) < topAt (2.0f) - 4.0);

    // A hard-driven 7 kHz doesn't fold its harmonics back into the band.
    const auto high = driven (7000.0, 24.0f);
    CHECK (toneLevel (high.left, 13000.0, from, to) - toneLevel (high.left, 7000.0, from, to) < -40.0);
}

TEST_CASE ("Filter: the channels sit a little apart")
{
    auto p = lowPass (FilterSlope::db24);
    Filter f;
    f.prepare (rate);
    auto s = sine ((int) rate / 2, 1000.0, 0.1f);
    run (f, s, p);
    const int from = (int) rate / 4, to = (int) rate / 2;
    double difference = 0.0;
    for (int i = from; i < to; ++i)
        difference += std::pow ((double) s.left[(size_t) i] - s.right[(size_t) i], 2.0);
    const double apart = db (std::sqrt (difference / (to - from))) - db (rms (s.left, from, to));
    INFO ("difference " << apart << " dB");
    CHECK (apart > -60.0);
    CHECK (apart < -20.0);
}

TEST_CASE ("Filter: ENV opens the cutoff with the input's level")
{
    auto p = lowPass (FilterSlope::db24);
    p.cutoffHz = 300.0f;
    const double closed = gainAt (3000.0, p);
    p.envAmount = 1.0f;
    const double opened = gainAt (3000.0, p);
    INFO ("3 kHz: " << closed << " dB, with ENV " << opened << " dB");
    CHECK (opened > closed + 20.0);
}

TEST_CASE ("Filter: stays finite and bounded at extreme settings")
{
    for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
        for (const float morph : { 0.0f, 1.0f, 2.0f })
        {
            Filter f;
            f.prepare (rate);
            auto s = noise ((int) rate, 1.0f);
            FilterParams p;
            p.slope = slope;
            p.morph = morph;
            p.resonance = 1.0f;
            p.drive = 1.0f;
            p.envAmount = 1.0f;
            p.keyOctaves = 4.0f;
            for (int i = 0; i < (int) s.left.size(); i += 64)
            {
                p.cutoffHz = (i / 64) % 2 == 0 ? 20.0f : 20000.0f;   // the worst jumps there are
                f.process (s.left.data() + i, s.right.data() + i, 64, p);
            }
            for (size_t i = 0; i < s.left.size(); ++i)
            {
                REQUIRE (std::isfinite (s.left[i]));
                REQUIRE (std::abs (s.left[i]) <= 1.0f);
            }
        }
}

TEST_CASE ("Filter: sweeping the cutoff and switching slope don't zipper or click")
{
    const auto render = [] (int which, bool move)
    {
        Filter f;
        f.prepare (rate);
        auto s = sine ((int) rate * 2, 110.0, 0.25f);
        auto p = lowPass (FilterSlope::db12);
        p.resonance = 0.3f;
        for (int i = 0; i < (int) s.left.size(); i += 256)
        {
            const float t = move ? 0.5f + 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 2.0 * i / rate) : 0.5f;
            if (which == 0)
                p.cutoffHz = 400.0f * std::pow (20.0f, t);
            else
                p.slope = move && (i / 4096) % 2 == 1 ? FilterSlope::db24 : FilterSlope::db12;
            f.process (s.left.data() + i, s.right.data() + i, std::min (256, (int) s.left.size() - i), p);
        }
        const double level = rms (s.left, (int) rate / 2, (int) rate * 2);
        return roughness (s.left, (int) rate / 2, (int) rate * 2) / (level * level + 1.0e-20);
    };
    for (int which = 0; which < 2; ++which)
    {
        const double still = render (which, false), moved = render (which, true);
        INFO ((which == 0 ? "cutoff" : "slope") << ": moved " << moved << ", still " << still);
        CHECK (moved < still * 4.0);
    }
}

// --- Color ------------------------------------------------------------------------

namespace
{
void run (Color& c, Stereo& s, const ColorParams& p, int block = 512)
{
    for (int i = 0; i < (int) s.left.size(); i += block)
        c.process (s.left.data() + i, s.right.data() + i, std::min (block, (int) s.left.size() - i), p);
}

Stereo colored (Stereo s, const ColorParams& p)
{
    Color c;
    c.prepare (rate);
    run (c, s, p);
    return s;
}

// Level of one frequency (Goertzel), over the second half.
double toneLevel (const std::vector<float>& x, double hz)
{
    const int from = (int) x.size() / 2, n = (int) x.size() - from;
    const double w = 2.0 * 3.14159265358979 * hz / rate, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < (int) x.size(); ++i)
    {
        const double s0 = x[(size_t) i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / n;
}
} // namespace

TEST_CASE ("Color: at its defaults the input passes through untouched")
{
    auto s = noise (20000, 0.5f);
    const auto out = colored (s, ColorParams {});
    CHECK (out.left == s.left);
    CHECK (out.right == s.right);
}

TEST_CASE ("Color: drive adds harmonics, even ones included")
{
    ColorParams p;
    p.drive = 0.7f;
    const auto out = colored (sine ((int) rate, 200.0, 0.5f), p);
    const double fundamental = toneLevel (out.left, 200.0);
    const double second = db (toneLevel (out.left, 400.0) / fundamental), third = db (toneLevel (out.left, 600.0) / fundamental);
    INFO ("2nd " << second << " dB, 3rd " << third << " dB under the fundamental");
    CHECK (second > -40.0);
    CHECK (third > -30.0);
}

TEST_CASE ("Color: tone tilts lows against highs")
{
    for (const float tone : { -1.0f, 1.0f })
    {
        ColorParams p;
        p.tone = tone;
        const double low = db (toneLevel (colored (sine ((int) rate, 40.0, 0.25f), p).left, 40.0) / 0.25);
        const double high = db (toneLevel (colored (sine ((int) rate, 12000.0, 0.25f), p).left, 12000.0) / 0.25);
        INFO ("tone " << tone << ": 40 Hz " << low << " dB, 12 kHz " << high << " dB");
        CHECK (std::abs (low + 6.0 * tone) < 1.0);
        CHECK (std::abs (high - 6.0 * tone) < 1.5);
    }
}

TEST_CASE ("Color: bits round to fewer steps; rate holds samples")
{
    ColorParams p;
    p.bits = 4.0f;
    const auto crushed = colored (noise (48000, 0.5f), p);
    for (size_t i = 24000; i < crushed.left.size(); ++i)
        REQUIRE (std::abs (crushed.left[i] * 8.0f - std::round (crushed.left[i] * 8.0f)) < 1.0e-4f);

    p = {};
    p.rateHz = 1000.0f;
    const auto held = colored (noise (48000, 0.5f), p);
    int changes = 0;
    for (size_t i = 24001; i < held.left.size(); ++i)
        changes += held.left[i] != held.left[i - 1];
    CHECK (std::abs (changes - 500) < 10);   // 1000 new values a second
}

TEST_CASE ("Color: comp narrows the gap between loud and quiet")
{
    ColorParams p;
    p.comp = 1.0f;
    const double loud = db (rms (colored (sine ((int) rate, 300.0, 0.9f), p).left, (int) rate / 2, (int) rate));
    const double quiet = db (rms (colored (sine ((int) rate, 300.0, 0.03f), p).left, (int) rate / 2, (int) rate));
    INFO ("29.5 dB apart in, " << loud - quiet << " dB apart out");
    CHECK (loud - quiet < 12.0);
}

TEST_CASE ("Color: noise follows the signal, so silence stays silent")
{
    ColorParams p;
    p.noise = 1.0f;
    const auto silent = colored (Stereo (48000), p);
    for (const float x : silent.left)
        REQUIRE (x == 0.0f);

    const auto out = colored (sine ((int) rate, 200.0, 0.5f), p);
    const double hiss = db (toneLevel (out.left, 5000.0) + toneLevel (out.left, 9000.0));
    CHECK (hiss > -90.0);
}

TEST_CASE ("Color: wow bends the pitch, and adds no delay at zero")
{
    ColorParams p;
    p.wow = 1.0f;
    const auto out = colored (sine ((int) rate * 3, 1000.0, 0.5f), p);
    // Cycle lengths from zero crossings: steady in, wobbling out.
    std::vector<double> periods;
    int last = -1;
    for (int i = 1; i < (int) out.left.size(); ++i)
        if (out.left[(size_t) i - 1] < 0.0f && out.left[(size_t) i] >= 0.0f)
        {
            if (last >= 0)
                periods.push_back (i - last);
            last = i;
        }
    const auto [lo, hi] = std::minmax_element (periods.begin() + 10, periods.end());
    CHECK (*hi - *lo >= 1.0);
}

TEST_CASE ("Color: stays finite and bounded with everything up")
{
    ColorParams p { 1.0f, 1.0f, 2.0f, 500.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    const auto out = colored (noise ((int) rate, 1.0f), p);
    for (size_t i = 0; i < out.left.size(); ++i)
    {
        REQUIRE (std::isfinite (out.left[i]));
        REQUIRE (std::abs (out.left[i]) <= 2.0f);
    }
}

TEST_CASE ("Color: sweeping drive, tone, comp and wow doesn't zipper")
{
    const auto render = [] (int which, bool sweep)
    {
        Color c;
        c.prepare (rate);
        auto s = sine ((int) rate * 2, 110.0, 0.25f);
        ColorParams p;
        for (int i = 0; i < (int) s.left.size(); i += 256)
        {
            const float t = sweep ? 0.5f + 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 2.0 * i / rate) : 0.5f;
            switch (which)
            {
                case 0: p.drive = t; break;
                case 1: p.tone = 2.0f * t - 1.0f; break;
                case 2: p.comp = t; break;
                default: p.wow = t; break;
            }
            c.process (s.left.data() + i, s.right.data() + i, std::min (256, (int) s.left.size() - i), p);
        }
        const double level = rms (s.left, (int) rate / 2, (int) rate * 2);
        return roughness (s.left, (int) rate / 2, (int) rate * 2) / (level * level + 1.0e-20);
    };
    const char* names[] { "drive", "tone", "comp", "wow" };
    for (int which = 0; which < 4; ++which)
    {
        const double still = render (which, false), swept = render (which, true);
        INFO (names[which] << ": swept " << swept << ", still " << still);
        CHECK (swept < still * 4.0);
    }
}

TEST_CASE ("Resonator page 2: BANDS, ODD/EVEN, DRIFT and DRIVE")
{
    std::array<double, Resonator::maxBands> r {};
    ResonatorParams p;
    p.notes = ResonatorNotes::harmonics;
    p.rangeOctaves = 4.0f;
    REQUIRE (Resonator::bandRatios (p, r) == Resonator::maxBands);
    p.bandCount = 3;
    REQUIRE (Resonator::bandRatios (p, r) == 3);
    CHECK (r[0] == 1.0);   // still from the lowest…
    CHECK (r[2] == 16.0);  // …to the highest

    // Every variant stays finite and no louder than the input allows.
    const auto peakWith = [] (const std::function<void (ResonatorParams&)>& set)
    {
        Resonator res;
        res.prepare (rate);
        ResonatorParams q;
        q.notes = ResonatorNotes::harmonics;
        q.rootNote = 45.0f;
        q.mix = 1.0f;
        set (q);
        auto s = noise ((int) rate * 2, 0.5f);
        run (res, s, q);
        float peak = 0.0f;
        for (const float v : s.left)
        {
            REQUIRE (std::isfinite (v));
            peak = std::max (peak, std::abs (v));
        }
        return peak;
    };
    for (const auto& set : std::vector<std::function<void (ResonatorParams&)>> {
             [] (ResonatorParams& q) { q.resonance = 0.0f; },
             [] (ResonatorParams& q) { q.resonance = 1.0f; },
             [] (ResonatorParams& q) { q.drift = 1.0f; q.driftRate = 8.0f; },
             [] (ResonatorParams& q) { q.spread = 1.0f; },
             [] (ResonatorParams& q) { q.oddEven = -1.0f; },
             [] (ResonatorParams& q) { q.drive = 1.0f; },
             [] (ResonatorParams& q) { q.damp = 1.0f; } })
        CHECK (peakWith (set) < 1.5f);
}

TEST_CASE ("Resonator: ODD/EVEN at its ends silences the other set of bands")
{
    // A tone on the 2nd harmonic rings with EVEN bands kept, and barely with only ODD.
    const auto ringOn2nd = [] (float oddEven)
    {
        Resonator res;
        res.prepare (rate);
        ResonatorParams p;
        p.notes = ResonatorNotes::harmonics;
        p.rootNote = 45.0f;   // 110 Hz: the 2nd band at 220 Hz
        p.rangeOctaves = 2.0f;
        p.decaySeconds = 2.0f;
        p.mix = 1.0f;
        p.oddEven = oddEven;
        Stereo s ((int) rate);
        for (size_t i = 0; i < s.left.size(); ++i)
            s.left[i] = s.right[i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) i / rate);
        run (res, s, p);
        double energy = 0.0;
        for (size_t i = s.left.size() / 2; i < s.left.size(); ++i)
            energy += (double) s.left[i] * s.left[i];
        return energy;
    };
    CHECK (ringOn2nd (1.0f) > ringOn2nd (-1.0f) * 4.0);
}

TEST_CASE ("Resonator: DRIFT makes each band's level wander over time")
{
    // With DRIFT, a steady input's ring level moves around; without, it holds.
    const auto spread = [] (float drift)
    {
        Resonator res;
        res.prepare (rate);
        ResonatorParams p;
        p.notes = ResonatorNotes::harmonics;
        p.rootNote = 45.0f;
        p.mix = 1.0f;
        p.drift = drift;
        p.driftRate = 4.0f;
        Stereo s ((int) rate * 3);
        for (size_t i = 0; i < s.left.size(); ++i)
            s.left[i] = s.right[i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 110.0 * (double) i / rate);
        run (res, s, p);
        float lo = 1.0e9f, hi = 0.0f;
        for (size_t start = s.left.size() / 3; start + 2400 <= s.left.size(); start += 2400)
        {
            double sum = 0.0;
            for (size_t i = start; i < start + 2400; ++i)
                sum += (double) s.left[i] * s.left[i];
            const float level = (float) std::sqrt (sum / 2400.0);
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        return hi / std::max (lo, 1.0e-9f);
    };
    CHECK (spread (0.0f) < 1.1f);
    CHECK (spread (1.0f) > 1.5f);
}

TEST_CASE ("Resonator: NOTES SAMPLE rings the sample's pitches; FOLLOW with a chord rings each note")
{
    // The pitch finder hears a three-note chord.
    std::vector<float> chord ((size_t) rate * 2);
    for (size_t i = 0; i < chord.size(); ++i)
        for (const double hz : { 220.0, 277.18, 329.63 })
            chord[i] += 0.2f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / rate);
    const auto found = findPitches (chord, rate, 12);
    REQUIRE (found.size() == 3);
    CHECK (std::abs (found[0] - 220.0) < 1.5);
    CHECK (std::abs (found[1] - 277.18) < 1.5);
    CHECK (std::abs (found[2] - 329.63) < 1.5);

    // SAMPLE: bands at those pitches, over the root.
    ResonatorParams p;
    p.notes = ResonatorNotes::sample;
    p.rootNote = 57.0f;   // A3, 220 Hz
    p.sampleCount = 3;
    for (size_t i = 0; i < 3; ++i)
        p.sampleHz[i] = (float) found[i];
    std::array<double, Resonator::maxBands> r {};
    REQUIRE (Resonator::bandRatios (p, r) == 3);
    CHECK (std::abs (r[1] * 220.0 - 277.18) < 2.0);

    // A chord under FOLLOW (SCALE notes): the chord's notes, in octaves.
    ResonatorParams c;
    c.rootNote = 48.0f;
    c.rangeOctaves = 1.0f;
    c.chord = { 4.0f, 7.0f };
    c.chordSize = 2;
    const int n = Resonator::bandRatios (c, r);
    REQUIRE (n == 6);   // C, E, G and each an octave up: every note gets RANGE above itself
    CHECK (std::abs (r[1] - std::pow (2.0, 4.0 / 12.0)) < 1.0e-6);
    CHECK (std::abs (r[2] - std::pow (2.0, 7.0 / 12.0)) < 1.0e-6);
}
