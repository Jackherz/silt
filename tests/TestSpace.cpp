#include <catch2/catch_test_macros.hpp>

#include <silt/Space.h>

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

Stereo noise (int n, float level, std::uint64_t seed = 3)
{
    Stereo s (n);
    Random r (seed);
    for (int i = 0; i < n; ++i)
    {
        s.left[(size_t) i] = level * (2.0f * r.next01() - 1.0f);
        s.right[(size_t) i] = level * (2.0f * r.next01() - 1.0f);
    }
    return s;
}

Stereo sine (int n, double hz, float level)
{
    Stereo s (n);
    for (int i = 0; i < n; ++i)
        s.left[(size_t) i] = s.right[(size_t) i] = level * (float) std::sin (2.0 * 3.14159265358979 * hz * i / rate);
    return s;
}

void run (Space& space, Stereo& s, const SpaceParams& p, int from = 0, int to = -1, int block = 512)
{
    if (to < 0)
        to = (int) s.left.size();
    for (int i = from; i < to; i += block)
        space.process (s.left.data() + i, s.right.data() + i, std::min (block, to - i), p);
}

Stereo processed (Stereo s, const SpaceParams& p)
{
    Space space;
    space.prepare (rate);
    run (space, s, p);
    return s;
}

double rms (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (sum / std::max (1, to - from));
}

double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

SpaceParams reverbOnly (float decay, float size = 0.5f)
{
    SpaceParams p;
    p.reverbLevel = 1.0f;
    p.decaySeconds = decay;
    p.size = size;
    p.tone = 1.0f;   // bright: the high end decays nearly as slowly, so broadband RT60 is close to DECAY
    return p;
}

// RT60 from the slope of the tail's level between two windows.
double measuredRt60 (const SpaceParams& p)
{
    Stereo s ((int) rate * 4);
    s.left[10] = s.right[10] = 1.0f;
    const auto out = processed (s, p);
    const int a = (int) (0.3 * rate), b = (int) (1.3 * rate), w = (int) (0.1 * rate);
    const double drop = db (rms (out.left, a, a + w)) - db (rms (out.left, b, b + w));
    return 60.0 / drop;   // dB per second → seconds per 60 dB
}
} // namespace

TEST_CASE ("Space: with both sends at zero the input passes through untouched")
{
    auto s = noise (30000, 0.5f);
    const auto out = processed (s, SpaceParams {});
    CHECK (out.left == s.left);
    CHECK (out.right == s.right);
}

TEST_CASE ("Delay: echoes land on time, each quieter than the last")
{
    Stereo s ((int) rate);
    s.left[0] = s.right[0] = 1.0f;
    SpaceParams p;
    p.delayMs = 100.0f;
    p.feedback = 0.5f;
    p.delayLevel = 1.0f;
    p.pingPong = false;
    const auto out = processed (s, p);

    const auto peakNear = [&] (int at)
    {
        int best = at - 50;
        for (int i = at - 50; i < at + 50; ++i)
            if (std::abs (out.left[(size_t) i]) > std::abs (out.left[(size_t) best]))
                best = i;
        return best;
    };
    const int first = peakNear (4800), second = peakNear (9600);
    CHECK (std::abs (first - 4800) <= 2);
    CHECK (std::abs (second - 9600) <= 4);
    CHECK (std::abs (out.left[(size_t) second]) < std::abs (out.left[(size_t) first]) * 0.6f);
}

TEST_CASE ("Delay: ping-pong alternates sides")
{
    Stereo s ((int) rate);
    s.left[0] = s.right[0] = 1.0f;
    SpaceParams p;
    p.delayMs = 100.0f;
    p.feedback = 0.6f;
    p.delayLevel = 1.0f;
    const auto out = processed (s, p);
    const double firstLeft = rms (out.left, 4700, 4900), firstRight = rms (out.right, 4700, 4900);
    const double secondLeft = rms (out.left, 9500, 9700), secondRight = rms (out.right, 9500, 9700);
    CHECK (firstLeft > 10.0 * firstRight);
    CHECK (secondRight > 10.0 * secondLeft);
}

TEST_CASE ("Delay: full feedback stays bounded")
{
    SpaceParams p;
    p.delayMs = 50.0f;
    p.feedback = 1.0f;
    p.delayLevel = 1.0f;
    const auto out = processed (noise ((int) rate * 10, 0.8f), p);
    for (const float x : out.left)
        REQUIRE (std::abs (x) < 4.0f);
}

TEST_CASE ("Delay: a time jump glides instead of clicking")
{
    Space space;
    space.prepare (rate);
    auto s = sine ((int) rate * 2, 220.0, 0.3f);
    SpaceParams p;
    p.delayMs = 300.0f;
    p.feedback = 0.4f;
    p.delayLevel = 1.0f;
    run (space, s, p, 0, (int) rate);
    p.delayMs = 120.0f;   // a jump, as a synced value change would make
    run (space, s, p, (int) rate, (int) rate * 2);

    float biggestStep = 0.0f;
    for (int i = (int) rate - 2000; i < (int) rate * 2; ++i)
        biggestStep = std::max (biggestStep, std::abs (s.left[(size_t) i] - s.left[(size_t) i - 1]));
    INFO ("largest sample-to-sample step " << biggestStep);
    CHECK (biggestStep < 0.2f);   // 220 Hz at this level moves at most ~0.03 per sample per source
}

TEST_CASE ("Reverb: decays at the DECAY time, at any SIZE")
{
    for (const float size : { 0.1f, 0.5f, 0.9f })
        for (const float decay : { 1.0f, 3.0f })
        {
            const double rt = measuredRt60 (reverbOnly (decay, size));
            INFO ("size " << size << ", decay " << decay << " s: measured " << rt << " s");
            CHECK (std::abs (rt / decay - 1.0) < 0.3);
        }
}

TEST_CASE ("Reverb: the tail is dense, noise-like and wide")
{
    Stereo s ((int) rate * 2);
    s.left[10] = s.right[10] = 1.0f;
    const auto out = processed (s, reverbOnly (3.0f));

    // Normalized echo density (Abel and Huang) over 20 ms windows: 1 is Gaussian-dense.
    double worst = 1.0e9;
    for (int start = (int) (0.15 * rate); start + 960 < (int) (1.0 * rate); start += 960)
    {
        const double sd = rms (out.left, start, start + 960);
        int outside = 0;
        for (int i = start; i < start + 960; ++i)
            outside += std::abs (out.left[(size_t) i]) > sd;
        worst = std::min (worst, outside / 960.0 / 0.3173);
    }
    INFO ("lowest echo density after 150 ms: " << worst);
    CHECK (worst > 0.8);

    double cross = 0.0, l = 0.0, r = 0.0;
    for (int i = (int) (0.2 * rate); i < (int) rate; ++i)
    {
        cross += (double) out.left[(size_t) i] * out.right[(size_t) i];
        l += (double) out.left[(size_t) i] * out.left[(size_t) i];
        r += (double) out.right[(size_t) i] * out.right[(size_t) i];
    }
    const double correlation = cross / std::sqrt (l * r);
    INFO ("left/right correlation " << correlation);
    CHECK (std::abs (correlation) < 0.4);
}

TEST_CASE ("Reverb: TONE darkens the tail")
{
    const auto highShare = [] (float tone)
    {
        auto p = reverbOnly (3.0f);
        p.tone = tone;
        auto in = noise ((int) rate, 0.3f);
        for (int i = (int) rate / 2; i < (int) rate; ++i)
            in.left[(size_t) i] = in.right[(size_t) i] = 0.0f;
        const auto later = processed (in, p);
        // Second difference as a rough high-frequency meter, over the tail after the input.
        double high = 0.0, all = 0.0;
        for (int i = (int) (0.6 * rate); i < (int) rate; ++i)
        {
            const double d = (double) later.left[(size_t) i] - 2.0 * later.left[(size_t) i - 1] + later.left[(size_t) i - 2];
            high += d * d;
            all += (double) later.left[(size_t) i] * later.left[(size_t) i];
        }
        return high / all;
    };
    CHECK (highShare (0.0f) < 0.5 * highShare (1.0f));
}

TEST_CASE ("Reverb: FREEZE holds the tail and shuts the input out")
{
    Space space;
    space.prepare (rate);
    auto s = noise ((int) rate * 8, 0.3f);
    auto p = reverbOnly (2.0f);
    run (space, s, p, 0, (int) rate);
    p.freeze = true;
    for (int i = (int) rate * 2; i < (int) s.left.size(); ++i)   // louder input while frozen: must not get in
        s.left[(size_t) i] *= 3.0f, s.right[(size_t) i] *= 3.0f;
    // Measure the reverb alone: subtract the dry input afterwards.
    const auto dry = s;
    run (space, s, p, (int) rate, (int) s.left.size());
    std::vector<float> wet (s.left.size());
    for (size_t i = 0; i < wet.size(); ++i)
        wet[i] = s.left[i] - dry.left[i];
    const double held = db (rms (wet, (int) (1.5 * rate), (int) (2.0 * rate)));
    const double later = db (rms (wet, (int) (7.0 * rate), (int) (7.5 * rate)));
    INFO ("frozen level " << held << " dB, 5.5 s later " << later << " dB");
    CHECK (std::abs (later - held) < 3.0);
}

TEST_CASE ("Reverb: SHIMMER adds the octave above")
{
    const auto octave = [] (float shimmer)
    {
        auto p = reverbOnly (4.0f);
        p.shimmer = shimmer;
        auto s = sine ((int) rate * 3, 300.0, 0.3f);
        for (int i = (int) rate; i < (int) s.left.size(); ++i)
            s.left[(size_t) i] = s.right[(size_t) i] = 0.0f;
        const auto out = processed (s, p);
        // Goertzel at 600 Hz over the tail.
        const double w = 2.0 * 3.14159265358979 * 600.0 / rate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = (int) (1.5 * rate); i < (int) (2.5 * rate); ++i)
        {
            const double s0 = out.left[(size_t) i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return db (std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)));
    };
    const double without = octave (0.0f), with = octave (1.0f);
    INFO ("600 Hz in the tail: " << without << " dB without, " << with << " dB with shimmer");
    CHECK (with > without + 10.0);
}

TEST_CASE ("Space: stays finite and bounded with everything up")
{
    SpaceParams p;
    p.delayMs = 80.0f;
    p.feedback = 1.0f;
    p.delayLevel = 1.0f;
    p.size = 1.0f;
    p.decaySeconds = 30.0f;
    p.tone = 1.0f;
    p.shimmer = 1.0f;
    p.reverbLevel = 1.0f;
    const auto out = processed (noise ((int) rate * 20, 1.0f), p);
    float peak = 0.0f;
    for (size_t i = 0; i < out.left.size(); ++i)
    {
        REQUIRE (std::isfinite (out.left[i]));
        peak = std::max ({ peak, std::abs (out.left[i]), std::abs (out.right[i]) });
    }
    INFO ("peak " << peak);
    CHECK (peak < 6.0f);
}

TEST_CASE ("Space: sweeping SIZE glides without clicks")
{
    Space space;
    space.prepare (rate);
    auto s = sine ((int) rate * 4, 220.0, 0.3f);
    auto p = reverbOnly (2.0f);
    for (int i = 0; i < (int) s.left.size(); i += 256)
    {
        p.size = (i / (int) rate) % 2 == 0 ? 0.0f : 1.0f;   // slammed end to end each second
        space.process (s.left.data() + i, s.right.data() + i, std::min (256, (int) s.left.size() - i), p);
    }
    float biggestStep = 0.0f;
    for (int i = 1; i < (int) s.left.size(); ++i)
        biggestStep = std::max (biggestStep, std::abs (s.left[(size_t) i] - s.left[(size_t) i - 1]));
    INFO ("largest sample-to-sample step " << biggestStep);
    CHECK (biggestStep < 0.2f);
}

TEST_CASE ("Space: sweeping decay and tone doesn't zipper")
{
    const auto render = [] (int which, bool sweep)
    {
        Space space;
        space.prepare (rate);
        auto s = sine ((int) rate * 3, 110.0, 0.25f);
        auto p = reverbOnly (2.0f);
        p.delayLevel = 0.5f;
        for (int i = 0; i < (int) s.left.size(); i += 256)
        {
            const float t = sweep ? 0.5f + 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 1.0 * i / rate) : 0.5f;
            switch (which)
            {
                case 0: p.size = t; break;
                case 1: p.decaySeconds = 0.5f + 5.0f * t; break;
                default: p.tone = t; break;
            }
            space.process (s.left.data() + i, s.right.data() + i, std::min (256, (int) s.left.size() - i), p);
        }
        double rough = 0.0, all = 0.0;
        for (int i = (int) rate; i < (int) s.left.size(); ++i)
        {
            const double d = (double) s.left[(size_t) i] - 2.0 * s.left[(size_t) i - 1] + s.left[(size_t) i - 2];
            rough += d * d;
            all += (double) s.left[(size_t) i] * s.left[(size_t) i];
        }
        return rough / all;
    };
    const char* names[] { "size", "decay", "tone" };
    for (int which = 1; which < 3; ++which)
    {
        const double still = render (which, false), swept = render (which, true);
        INFO (names[which] << ": swept " << swept << ", still " << still);
        CHECK (swept < still * 4.0);
    }
}

TEST_CASE ("Space: closing the sends goes back to bypass, without an old tail later")
{
    Space space;
    space.prepare (rate);
    auto s = noise ((int) rate, 0.5f);
    auto p = reverbOnly (5.0f);
    run (space, s, p);
    p.reverbLevel = 0.0f;
    Stereo quiet ((int) rate);
    run (space, quiet, p);            // fades out, then clears a line per block
    p.reverbLevel = 1.0f;
    Stereo silent ((int) rate / 4);
    run (space, silent, p);
    for (const float x : silent.left)
        REQUIRE (x == 0.0f);
}
