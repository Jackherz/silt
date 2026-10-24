#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <silt/Resonator.h>

#include <cmath>
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
