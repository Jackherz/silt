#pragma once

#include <silt/Dsp.h>

#include <array>
#include <vector>

namespace silt
{

struct SpaceParams
{
    float delayMs = 375.0f;       // delay time, 2 samples … 4 s
    float feedback = 0.35f;       // 0…1
    float delayLevel = 0.0f;      // 0…1, a send: 0 adds nothing
    bool pingPong = true;         // false: straight stereo
    float size = 0.5f;            // 0…1: small room … cathedral (line lengths, pre-delay)
    float decaySeconds = 3.0f;    // RT60 at low and mid frequencies, 0.3…30 s
    float tone = 0.5f;            // 0 dark … 1 bright: reverb bandwidth and HF decay, delay loop filters
    float shimmer = 0.0f;         // 0…1: octave-up feedback into the reverb
    float reverbLevel = 0.0f;     // 0…1, a send
    bool freeze = false;          // hold the reverb tail and the delay loop
};

// SPACE: a tape-style stereo delay feeding a reverb. Both are sends added to
// the input, so with DELAY and REVERB at 0 the input passes through untouched.
// Design notes and sources: research doc 10-space-reverb-delay.md.
//
// Reverb: 8 channels; a 4-step diffuser (delays, Hadamard mix, polarity flips)
// into an 8-line feedback delay network (Householder matrix), lines spread over
// an octave and slowly modulated, each with an absorption filter matched to its
// length so DECAY is the same at any SIZE. SHIMMER pitch-shifts the tank's output
// up an octave and feeds it back in.
//
// Delay: glides when the time changes (tape-style). Each repeat passes TONE's
// filters and a soft saturation, so repeats darken and high feedback stays bounded.
//
// Audio thread only, after prepare(), which allocates. Controls glide.
class Space
{
public:
    static constexpr double maxDelaySeconds = 4.0;
    static constexpr int channels = 8;

    void prepare (double sampleRate);
    void reset() noexcept;

    // In place; left and right may be the same pointer (mono).
    void process (float* left, float* right, int numSamples, const SpaceParams&) noexcept;

private:
    struct Line
    {
        std::vector<float> buffer;
        int mask = 0, write = 0;

        void allocate (int maxDelaySamples);
        void clear() noexcept;
        void push (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }
        float at (int delay) const noexcept { return buffer[(size_t) ((write - delay) & mask)]; }
        // The value pushed `delay` samples ago (≥ 2), Hermite-interpolated.
        float read (double delay) const noexcept;
        float readLinear (double delay) const noexcept;   // cheaper, for lines whose length barely moves
    };

    struct Smoothed
    {
        float delaySamples = 0.0f, feedback = 0.0f, delayLevel = 0.0f, pingPong = 1.0f;
        float size = 0.5f, decaySeconds = 3.0f, tone = 0.5f, shimmer = 0.0f, reverbLevel = 0.0f, freeze = 0.0f;
    };

    void updateReverb() noexcept;   // per block: lengths, gains, filters from the smoothed controls
    void clearSome (int amount) noexcept;

    double sampleRate = 44100.0;
    Smoothed now;
    bool primed = false, active = false;
    int clearPosition = -1;   // ≥ 0: lines still to clear after going idle, a slice per block

    // Delay
    std::array<Line, 2> delayLines;
    std::array<float, 2> loopLow {}, loopHigh {};

    // Reverb
    std::array<Line, 2> preDelay;
    std::array<float, 2> inputLow {}, inputHigh {};
    std::array<std::array<Line, channels>, 4> diffusers;
    std::array<std::array<float, channels>, 4> diffuserShare {};   // where in its step's range each line sits
    std::array<std::array<bool, channels>, 4> diffuserFlip {};
    std::array<Line, channels> tank;
    std::array<float, channels> tankLength {}, tankGain {}, tankSmooth {}, tankState {};
    std::array<double, channels> modPhase {}, modRate {};
    std::array<float, channels> modNow {}, modNext {};   // line modulation (samples), ramped per block
    float diffuserRange = 0.0f, preDelaySamples = 0.0f, outputTrim = 1.0f;
    float inputLowCoefficient = 0.0f, loopLowCoefficient = 0.0f, loopHighCoefficient = 0.0f;

    // Shimmer
    Line shiftLine;
    double shiftPhase = 0.0;
    float shimmerHigh = 0.0f, shimmerLow = 0.0f, tankEnergy = 0.0f;
    float shimmerInject = 0.0f;   // fed into the tank on the next sample
};

} // namespace silt
