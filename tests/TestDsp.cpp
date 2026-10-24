#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <silt/Dsp.h>

#include <vector>

using namespace silt;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Hann window: zero at the edges, one in the middle")
{
    HannWindow w;
    CHECK_THAT (w (0.0), WithinAbs (0.0, 1e-6));
    CHECK_THAT (w (0.5), WithinAbs (1.0, 1e-6));
    CHECK_THAT (w (1.0), WithinAbs (0.0, 1e-6));
}

TEST_CASE ("Hann windows at 50% overlap sum to one")
{
    HannWindow w;
    for (double t = 0.0; t <= 0.5; t += 0.0137)
        CHECK_THAT (w (t) + w (t + 0.5), WithinAbs (1.0, 1e-4));
}

TEST_CASE ("Default grain envelope is the Hann window")
{
    HannWindow hann;
    GrainEnvelope env;
    env.set (1.0, 0.5, 0.5, 4800, 0.0);
    for (double t = 0.0; t <= 1.0; t += 0.01)
        CHECK_THAT (env (hann, t), WithinAbs (hann (t), 1e-3));
}

TEST_CASE ("Grain envelope: fade, slant and the declick floor")
{
    HannWindow hann;
    GrainEnvelope env;

    // No fade: flat, except the 48-sample declick ramps at each end.
    env.set (0.0, 0.5, 0.5, 4800, 48.0);
    CHECK_THAT (env (hann, 0.5), WithinAbs (1.0, 1e-6));
    CHECK (env (hann, 0.0) == 0.0f);
    CHECK_THAT (env (hann, 0.01), WithinAbs (1.0, 1e-6));   // 48 samples is 1 % of 4800

    // Slant 0: instant attack (only the declick floor), all fade at the end.
    env.set (1.0, 0.0, 0.5, 4800, 48.0);
    CHECK_THAT (env (hann, 0.02), WithinAbs (1.0, 0.01));
    CHECK (env (hann, 0.5) < 1.0f);

    // Linear ease: a straight ramp.
    env.set (1.0, 0.5, 0.0, 4800, 0.0);
    CHECK_THAT (env (hann, 0.25), WithinAbs (0.5, 1e-3));
}

TEST_CASE ("Interpolation is exact on whole frames")
{
    const std::vector<float> data { 0.1f, -0.4f, 0.9f, 0.3f, -0.7f };
    for (int i = 0; i < (int) data.size(); ++i)
    {
        CHECK_THAT (readFrame (data.data(), 5, i, Interpolation::linear), WithinAbs (data[(size_t) i], 1e-6));
        CHECK_THAT (readFrame (data.data(), 5, i, Interpolation::cubic), WithinAbs (data[(size_t) i], 1e-6));
    }
}

TEST_CASE ("Cubic interpolation reproduces a straight line")
{
    std::vector<float> ramp (16);
    for (int i = 0; i < 16; ++i)
        ramp[(size_t) i] = 0.1f * i;

    for (double pos = 1.0; pos < 13.0; pos += 0.37)
        CHECK_THAT (readFrame (ramp.data(), 16, pos, Interpolation::cubic), WithinAbs (0.1 * pos, 1e-5));
}

TEST_CASE ("Reads wrap around the buffer ends")
{
    const std::vector<float> data { 1.0f, 0.0f, 0.0f, 3.0f };
    // Halfway between the last frame (3) and the first (1).
    CHECK_THAT (readFrame (data.data(), 4, 3.5, Interpolation::linear), WithinAbs (2.0, 1e-6));
    CHECK_THAT (readFrame (data.data(), 4, -0.5, Interpolation::linear), WithinAbs (2.0, 1e-6));
}

TEST_CASE ("A reversed grain covers the same span as a forward one (gap 2)")
{
    // 480 output samples at 2× pitch read 960 buffer frames.
    const auto span = grainReadSpan (1000.0, 480, 2.0);
    CHECK (span.lo == 1000.0);
    CHECK (span.hi == 1960.0);
}

TEST_CASE ("Random is deterministic per seed and stays in range")
{
    Random a (42), b (42), c (43);
    bool differs = false;
    for (int i = 0; i < 1000; ++i)
    {
        const float x = a.next01();
        CHECK (x == b.next01());
        CHECK (x >= 0.0f);
        CHECK (x < 1.0f);
        differs |= x != c.next01();
    }
    CHECK (differs);
}

TEST_CASE ("Scale snapping lands on the nearest scale note, ties going down")
{
    CHECK (snapToScale (60.0, Scale::major, 0) == 60.0);    // C in C major
    CHECK (snapToScale (61.0, Scale::major, 0) == 60.0);    // C# → C (tie, down)
    CHECK (snapToScale (61.4, Scale::major, 0) == 62.0);    // nearer D
    CHECK (snapToScale (66.0, Scale::major, 0) == 65.0);    // F# → F (tie, down)
    CHECK (snapToScale (61.0, Scale::major, 2) == 61.0);    // C# is in D major
    CHECK (snapToScale (63.0, Scale::minorPentatonic, 0) == 63.0);
    CHECK (snapToScale (64.0, Scale::minorPentatonic, 0) == 63.0);
    CHECK (snapToScale (66.0, Scale::octaves, 0) == 60.0);  // tie between 60 and 72, down
    CHECK (snapToScale (67.0, Scale::octaves, 0) == 72.0);
    CHECK (snapToScale (60.37, Scale::chromatic, 0) == 60.0);
    CHECK (snapToScale (60.37, Scale::off, 0) == 60.37);
}

TEST_CASE ("Every scale contains its key")
{
    for (int s = 0; s < scaleCount; ++s)
        CHECK ((scaleMask ((Scale) s) & 1u) == 1u);
}
