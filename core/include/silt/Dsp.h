#pragma once

#include <algorithm>
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

// Grain envelope after GRM Play's Fade / Slant / Ease (research 04, GRM Atelier).
//   fade  share of the grain spent fading in and out, 0…1 (1 = all of it)
//   slant attack's share of that fade, 0…1 (0.5 = symmetric)
//   ease  curve: 0 linear, 0.5 raised cosine, 1 abrupt
// Defaults (1, 0.5, 0.5) give exactly the Hann window.
class GrainEnvelope
{
public:
    // lengthSamples: the grain; minFadeSamples: the Declick floor on each fade.
    void set (double fade, double slant, double ease, int lengthSamples, double minFadeSamples) noexcept
    {
        const double len = std::max (1, lengthSamples);
        double attack = std::max (minFadeSamples, fade * slant * len);
        double release = std::max (minFadeSamples, fade * (1.0 - slant) * len);
        if (attack + release > len)
        {
            const double scale = len / (attack + release);
            attack *= scale;
            release *= scale;
        }
        attackEnd = attack / len;
        releaseStart = 1.0 - release / len;
        easeAmount = (float) std::clamp (ease, 0.0, 1.0);
    }

    float operator() (const HannWindow& hann, double phase01) const noexcept
    {
        if (phase01 < attackEnd)
            return curve (hann, phase01 / attackEnd);
        if (phase01 > releaseStart)
            return curve (hann, (1.0 - phase01) / (1.0 - releaseStart));
        return 1.0f;
    }

    // Where the rise ends and the fall begins, 0…1 of the grain.
    double getAttackEnd() const noexcept { return attackEnd; }
    double getReleaseStart() const noexcept { return releaseStart; }

private:
    // Rises 0 → 1 over t in [0, 1].
    float curve (const HannWindow& hann, double t) const noexcept
    {
        const float lin = (float) t;
        const float smooth = hann (t * 0.5);   // first half of Hann = raised cosine
        if (easeAmount <= 0.5f)
            return lin + easeAmount * 2.0f * (smooth - lin);
        const float u = 1.0f - lin;
        const float abrupt = 1.0f - u * u * u * u;
        return smooth + (easeAmount - 0.5f) * 2.0f * (abrupt - smooth);
    }

    double attackEnd = 0.5, releaseStart = 0.5;
    float easeAmount = 0.5f;
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
    const float t = (float) (pos - floorPos);
    int i = (int) floorPos;

    // Almost every read is well inside the buffer: no wrapping needed (the
    // modulo it takes is most of a read's cost, for every grain and sample).
    const bool inside = i >= 1 && i + 2 < numFrames;
    if (! inside)
        i = wrap (i);
    const float x0 = data[i];
    const float x1 = data[inside ? i + 1 : wrap (i + 1)];

    if (mode == Interpolation::linear)
        return x0 + t * (x1 - x0);

    const float xm1 = data[inside ? i - 1 : wrap (i - 1)];
    const float x2 = data[inside ? i + 2 : wrap (i + 2)];
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

// Scales for pitch snapping, as sets of pitch classes above the key.
// Linear to the knee, then a smooth bend to full scale: effects that can run
// hot (rings, resonance) saturate instead of clipping.
inline float softClip (float x) noexcept
{
    constexpr float knee = 0.7f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    return std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}

enum class Scale { off, chromatic, major, minor, dorian, phrygian, lydian, mixolydian,
                   majorPentatonic, minorPentatonic, wholeTone, octaves, fifths };

constexpr int scaleCount = 13;

// Bit n set = n semitones above the key is in the scale.
constexpr std::uint16_t scaleMask (Scale s) noexcept
{
    switch (s)
    {
        case Scale::off:
        case Scale::chromatic:       return 0xfff;
        case Scale::major:           return 0b101010110101;   // 0 2 4 5 7 9 11
        case Scale::minor:           return 0b010110101101;   // 0 2 3 5 7 8 10
        case Scale::dorian:          return 0b011010101101;   // 0 2 3 5 7 9 10
        case Scale::phrygian:        return 0b010110101011;   // 0 1 3 5 7 8 10
        case Scale::lydian:          return 0b101011010101;   // 0 2 4 6 7 9 11
        case Scale::mixolydian:      return 0b011010110101;   // 0 2 4 5 7 9 10
        case Scale::majorPentatonic: return 0b001010010101;   // 0 2 4 7 9
        case Scale::minorPentatonic: return 0b010010101001;   // 0 3 5 7 10
        case Scale::wholeTone:       return 0b010101010101;   // 0 2 4 6 8 10
        case Scale::octaves:         return 0b000000000001;   // 0
        case Scale::fifths:          return 0b000010000001;   // 0 7
    }
    return 0xfff;
}

// Snaps a (fractional) MIDI pitch to the nearest note of the scale in the
// given key (0 = C … 11 = B); ties go down. Scale::off leaves it unchanged.
inline double snapToScale (double pitch, Scale scale, int key) noexcept
{
    if (scale == Scale::off)
        return pitch;
    const auto mask = scaleMask (scale);
    const int nearest = (int) std::lround (pitch);
    double best = nearest;
    double bestDistance = 1.0e9;
    for (int offset = -12; offset <= 12; ++offset)
    {
        const int candidate = nearest + offset;
        const int degree = ((candidate - key) % 12 + 12) % 12;
        if ((mask >> degree & 1u) == 0)
            continue;
        const double distance = std::abs (candidate - pitch);
        if (distance < bestDistance - 1.0e-9)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

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
