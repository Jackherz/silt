#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// Small building blocks for the grain engine (plan 02 §1–3).
namespace silt
{

// Grain envelope: a Hann window as a lookup table, read with linear
// interpolation. One guard point past the end keeps the read branch-free.
class HannWindow
{
public:
    static constexpr int size = 2048;

    HannWindow() noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        for (int i = 0; i <= size; ++i)
            table[(size_t) i] = (float) (0.5 - 0.5 * std::cos (twoPi * i / size));
    }

    // phase01 in [0, 1].
    float operator() (double phase01) const noexcept
    {
        const double pos = phase01 * size;
        const int i = (int) pos;
        if (i >= size)
            return table[size];
        const float frac = (float) (pos - i);
        return table[(size_t) i] + frac * (table[(size_t) i + 1] - table[(size_t) i]);
    }

private:
    std::array<float, size + 1> table {};
};

enum class Interpolation { linear, cubic };

// Reads channel data at a fractional frame position, wrapping around the
// buffer ends. Cubic is 4-point Hermite (Catmull-Rom).
inline float readFrame (const float* data, int numFrames, double pos, Interpolation mode) noexcept
{
    const auto wrap = [numFrames] (int i) noexcept
    {
        i %= numFrames;
        return i < 0 ? i + numFrames : i;
    };

    const double floorPos = std::floor (pos);
    const int i = wrap ((int) floorPos);
    const float t = (float) (pos - floorPos);
    const float x0 = data[i];
    const float x1 = data[wrap (i + 1)];

    if (mode == Interpolation::linear)
        return x0 + t * (x1 - x0);

    const float xm1 = data[wrap (i - 1)];
    const float x2 = data[wrap (i + 2)];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

// Seedable, allocation-free random numbers (xorshift64*), so every render
// with the same seed is identical (plan §3: deterministic tests).
class Random
{
public:
    explicit Random (std::uint64_t seed = 0x51c7) noexcept { setSeed (seed); }

    void setSeed (std::uint64_t seed) noexcept { state = seed != 0 ? seed : 0x9e3779b97f4a7c15ull; }

    // Uniform in [0, 1).
    float next01() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return (float) ((state * 0x2545f4914f6cdd1dull) >> 40) * (1.0f / 16777216.0f);
    }

    // Uniform in [-1, 1).
    float nextBipolar() noexcept { return next01() * 2.0f - 1.0f; }

private:
    std::uint64_t state = 0;
};

// The span of buffer frames a grain reads, [lo, hi), before wrapping.
// A reversed grain starts at the far end of the forward grain's span and
// reads back to its start, so both directions cover the same audio
// (plan gap 2: the far end is start + length × pitch ratio, not start + length).
struct ReadSpan { double lo, hi; };

inline ReadSpan grainReadSpan (double start, int lengthFrames, double ratio) noexcept
{
    return { start, start + lengthFrames * ratio };
}

inline double semitonesToRatio (double semitones) noexcept { return std::exp2 (semitones / 12.0); }

} // namespace silt
