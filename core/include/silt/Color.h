#pragma once

#include <silt/Dsp.h>

#include <array>
#include <vector>

namespace silt
{

struct ColorParams
{
    float drive = 0.0f;       // 0…1: up to +30 dB into a slightly uneven tape-style curve
    float tone = 0.0f;        // −1 dark … +1 bright: a ±6 dB tilt around 800 Hz
    float bits = 16.0f;       // 2…16; 16 is off
    float rateHz = 40000.0f;  // sample-and-hold rate; Color::rateOff and up (or the sample rate) is off
    float comp = 0.0f;        // 0…1: lower threshold, higher ratio, matching makeup
    float noise = 0.0f;       // 0…1: hiss that follows the signal's level
    float wow = 0.0f;         // 0…1: slow wow plus faster flutter, up to ±3 ms
    float mix = 1.0f;
};

// The COLOR chain: wow → drive → tone → bits → rate → comp → noise. Each
// stage at its neutral value is skipped, so the defaults pass the input
// through untouched; each stage also reaches its neutral value smoothly, so
// nothing clicks as it comes in. The knobs glide (20 ms).
//
// Audio thread only, after prepare(), which allocates the wow delay line.
class Color
{
public:
    static constexpr float rateOff = 40000.0f;

    void prepare (double sampleRate);
    void reset() noexcept;

    // In place; left and right may be the same pointer (mono).
    void process (float* left, float* right, int numSamples, const ColorParams&) noexcept;

private:
    double sampleRate = 44100.0;
    ColorParams now;                       // the knobs, gliding
    bool primed = false;

    std::array<std::vector<float>, 2> wowLine;
    int wowWrite = 0;
    double wowPhase = 0.0, flutterPhase = 0.0;
    float flutterDrift = 0.0f, flutterTarget = 0.0f;

    std::array<float, 2> toneLow {}, holdValue {}, noiseState {};
    double holdPhase = 1.0;
    float detector = 0.0f;                 // compressor level (dB), stereo-linked
    float compGain = 0.0f;                 // dB, ≤ 0
    float level = 0.0f;                    // signal envelope for the noise
    Random random;
};

} // namespace silt
