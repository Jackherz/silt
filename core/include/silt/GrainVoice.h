#pragma once

#include <silt/Dsp.h>
#include <silt/SampleBuffer.h>

#include <array>

namespace silt
{

// What a playhead does when it reaches either end of the sample.
enum class EndMode { loop, hold, stop };

// The grain controls of the Grain page (spec §3), in engine units.
// Latched into each grain at its onset, so changes never alter a grain in flight.
struct GrainParams
{
    double pitchSemitones = 0.0;   // −24…+24, added to the key's transposition
    double sizeMs = 100.0;         // grain length, 10…2000 ms
    double rateHz = 20.0;          // grains started per second, 0.1…100
    double spray = 0.0;            // 0…1: start scatter up to ±1 s, plus stereo scatter
    double start = 0.0;            // 0…1: where a note starts its playhead
    double scan = 1.0;             // playhead speed, −4…+4 (1 = real time, 0 = frozen)
    double reverseChance = 0.0;    // 0…1: chance a grain plays backwards
    int rootNote = 60;             // the key that plays at the sample's own pitch
    EndMode endMode = EndMode::loop;
    Interpolation interpolation = Interpolation::cubic;
};

// One MIDI-played voice: a playhead moving through the sample, a scheduler
// starting grains around it, a fixed grain pool and an amplitude envelope.
class GrainVoice
{
public:
    static constexpr int maxGrains = 64;

    void prepare (double sampleRate) noexcept;
    void setSeed (std::uint64_t seed) noexcept { random.setSeed (seed); }

    void noteOn (int note, float velocity, const SampleBuffer& buffer, const GrainParams&) noexcept;
    void noteOff() noexcept;
    void reset() noexcept;

    bool isActive() const noexcept { return envStage != EnvStage::idle || activeGrains > 0; }
    int getCurrentNote() const noexcept { return currentNote; }
    int getActiveGrainCount() const noexcept { return activeGrains; }

    // How many grains still read from this buffer (the engine retires an old
    // buffer only once none do).
    int countGrainsUsing (const SampleBuffer*) const noexcept;

    // Adds this voice's output to left/right. New grains read from `buffer`;
    // grains already playing keep reading the buffer they started on.
    void render (float* left, float* right, int numSamples,
                 const GrainParams&, const SampleBuffer* buffer) noexcept;

private:
    struct Grain
    {
        const SampleBuffer* source = nullptr;
        double readPos = 0.0;       // buffer frames, fractional
        double increment = 0.0;     // frames per output sample, negative when reversed
        double phase = 0.0;         // 0…1 through the envelope
        double phaseInc = 0.0;
        int remaining = 0;          // output samples left; 0 = free
        float gainL = 0.0f, gainR = 0.0f;
        Interpolation interpolation = Interpolation::cubic;
    };

    enum class EnvStage { idle, attack, sustain, release };

    void spawnGrain (const GrainParams&, const SampleBuffer&) noexcept;
    bool advancePlayhead (const GrainParams&, const SampleBuffer&) noexcept;
    float nextEnvelope() noexcept;
    float targetGain (const GrainParams&) const noexcept;

    double sampleRate = 48000.0;
    HannWindow window;
    Random random;
    std::array<Grain, maxGrains> grains {};
    int activeGrains = 0;

    int currentNote = -1;
    double noteRatio = 1.0;
    float velocity = 1.0f;
    double playhead = 0.0;          // buffer frames
    double samplesToNextGrain = 0.0;
    bool spawning = false;

    EnvStage envStage = EnvStage::idle;
    float envLevel = 0.0f;
    float attackStep = 0.0f, releaseStep = 0.0f;

    float outputGain = 1.0f;        // overlap normalisation, smoothed
    float gainSmoothing = 0.0f;
};

} // namespace silt
