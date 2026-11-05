#pragma once

#include <kit/HalfBand.h>

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

// A stereo multimode filter modelled on a transistor ladder: four one-pole stages, each
// saturating, in a resonance loop, solved without a delay in the loop (after Mystran's
// cheap non-linear zero-delay filters). Low, band and high pass at both slopes are mixes
// of the same four poles (after the Oberheim Xpander), so MORPH and slope move without a
// step, and loud signals press the resonance down instead of spiking through it. It runs
// at twice the sample rate, so the saturation doesn't fold back. The two channels sit a
// few cents apart, as two analogue filters do.
//
// Audio thread only, after prepare(). The cutoff glides (20 ms) and everything is
// interpolated sample by sample, so sweeps don't zipper.
class Filter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // In place; left and right may be the same pointer (mono).
    void process (float* left, float* right, int numSamples, const FilterParams&) noexcept;

    // What a setting comes to (slope: 0 is 12 dB, 1 is 24 dB), for the screen's curve too:
    // the loop's feedback, and how much of each tap (stage input, poles 1 to 4) is heard.
    static double feedbackFor (double resonance, double slope) noexcept;
    static std::array<double, 5> tapMixFor (double morph, double slope, double resonance, double cutoffHz) noexcept;

private:
    struct Channel
    {
        std::array<double, 4> poles {};
        kit::HalfBandPath upA, upB, downA, downB;
        double dcIn = 0.0, dcOut = 0.0;
    };

    // One tick at the doubled rate; taps are the input to the stages and each stage's output.
    static void tick (Channel&, double in, double f, double feedback, std::array<double, 5>& taps) noexcept;

    static constexpr int controlInterval = 16;
    double sampleRate = 44100.0;
    std::array<Channel, 2> channels {};
    double cutoffNow = 1000.0, resonanceNow = 0.0, morphNow = 0.0, driveNow = 0.0;
    double gNow = 0.0, feedbackNow = 0.0;
    std::array<double, 5> mixNow {};   // how much of each tap makes the output
    double evenNow = 0.0, driveGainNow = 1.0;
    double dcCoefficient = 0.999;
    double slopeBlend = 0.0;      // 0: 12 dB, 1: 24 dB
    double wet = 0.0;             // how much filter is in the output (mix, and off/on)
    double envelope = 0.0;        // the input's level, 0…1 over −48…0 dBFS
    bool active = false;
    bool primed = false;          // smoothed values start at their targets
};

} // namespace silt
