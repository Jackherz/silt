#pragma once

#include <array>

namespace silt
{

enum class FilterSlope { off, db12, db24 };

struct FilterParams
{
    FilterSlope slope = FilterSlope::off;   // off passes the input through untouched
    float cutoffHz = 1000.0f;
    float resonance = 0.0f;      // 0…1: flat to a strong ringing peak (Q 0.7…30)
    float morph = 0.0f;          // 0 low pass, 1 band pass, 2 high pass, blended between
    float drive = 0.0f;          // 0…1: up to +24 dB into a soft saturation
    float envAmount = 0.0f;      // −1…1: the input's level moves the cutoff up to ±4 octaves
    float keyOctaves = 0.0f;     // added to the cutoff, from key tracking (octaves)
    float mix = 1.0f;
};

// A stereo multimode filter: TPT state-variable stages (Zavalishin), one for
// 12 dB/oct and two in series for 24. Both slopes run while the filter is on,
// so switching slope crossfades instead of clicking.
//
// Audio thread only, after prepare(). The cutoff glides (20 ms) and its
// coefficients are interpolated sample by sample, so sweeps don't zipper.
class Filter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // In place; left and right may be the same pointer (mono).
    void process (float* left, float* right, int numSamples, const FilterParams&) noexcept;

private:
    struct Stage
    {
        double s1 = 0.0, s2 = 0.0;
        // Low, band and high pass outputs for input x, with g = tan(pi fc / fs) and damping k.
        void run (double x, double g, double k, double& low, double& band, double& high) noexcept
        {
            const double a1 = 1.0 / (1.0 + g * (g + k));
            high = (x - (k + g) * s1 - s2) * a1;
            band = g * high + s1;
            low = g * band + s2;
            s1 = g * high + band;
            s2 = g * band + low;
        }
    };

    struct Channel
    {
        Stage twelve, first, second;   // 12 dB; 24 dB = first then second
    };

    static constexpr int controlInterval = 16;
    double sampleRate = 44100.0;
    std::array<Channel, 2> channels {};
    double cutoffNow = 1000.0, resonanceNow = 0.0, morphNow = 0.0, driveNow = 0.0;
    double gNow = 0.0;
    double slopeBlend = 0.0;      // 0: 12 dB, 1: 24 dB
    double wet = 0.0;             // how much filter is in the output (mix, and off/on)
    double envelope = 0.0;        // the input's level, 0…1 over −48…0 dBFS
    bool active = false;
    bool primed = false;          // smoothed values start at their targets
};

} // namespace silt
