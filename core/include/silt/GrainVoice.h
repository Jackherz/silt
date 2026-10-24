#pragma once

#include <silt/Dsp.h>
#include <silt/SampleBuffer.h>

#include <array>

namespace silt
{

// What a playhead does when it reaches either end of its region.
enum class EndMode { loop, hold, stop };

// The Grain and Voice page controls (spec §3, plan §5), in engine units.
// Grain controls are latched into each grain at its onset, so changes never
// alter a grain in flight.
struct GrainParams
{
    // Grain
    double pitchSemitones = 0.0;   // −24…+24, added to the key's transposition
    double sizeMs = 100.0;         // grain length, 10…2000 ms
    double rateHz = 20.0;          // grains started per second, 0.1…100
    double spray = 0.0;            // 0…1: start scatter up to ±1 s, plus stereo scatter
    double reverseChance = 0.0;    // 0…1: chance a grain plays backwards
    double jitter = 0.0;           // 0…1: random spread of grain start times (0 = clockwork).
                                   // Evenly spaced grains at fast rates sum to a pitched
                                   // comb tone; jitter breaks it up. Not used on the beat grid.
    double randomAmp = 0.0;        // 0…1: per-grain random level drop
    double fade = 1.0;             // grain envelope (GrainEnvelope)
    double slant = 0.5;
    double ease = 0.5;
    double declickMs = 1.0;        // shortest fade allowed

    // Grains mode (after Tonverk's Grainer "amount"): when > 0, this many grains
    // sound at once on average and the start interval follows the grain length,
    // so changing Size keeps the texture equally thick. rateHz is then ignored.
    int grainCount = 0;

    // Beat grid. When set, grains start on multiples of gridDivision (in
    // quarter notes) of the host position instead of every 1/rateHz seconds;
    // a note's first grain still starts on the note. rateHz should match the
    // division at the current tempo (it sets the overlap gain).
    bool gridSync = false;
    double gridPpq = 0.0;          // host position at this render call's first sample
    double ppqPerSample = 0.0;
    double gridDivision = 0.25;    // quarter notes between grains

    // Playhead and region
    double start = 0.0;            // 0…1: region start, where a note starts its playhead
    double length = 1.0;           // 0…1: region length as a share of the sample
    double scan = 1.0;             // playhead speed, −4…+4 (1 = real time, 0 = frozen)
    EndMode endMode = EndMode::loop;

    // Voice
    int rootNote = 60;             // the key that plays at the sample's own pitch
    double attackMs = 5.0;
    double decayMs = 200.0;
    double sustain = 1.0;          // 0…1
    double releaseMs = 300.0;

    Interpolation interpolation = Interpolation::cubic;
};

// One sounding grain as the UI draws it. Positions are shares of the buffer.
struct GrainView
{
    float start = 0.0f, end = 0.0f;   // the audio the grain covers
    float position = 0.0f;            // where it's reading now
    float level = 0.0f;               // envelope × voice level, 0…1
    std::uint8_t voice = 0, slot = 0;
    bool reversed = false;
};

// One MIDI-played voice: a playhead moving through a region of the sample,
// a scheduler starting grains around it, a fixed grain pool and an ADSR.
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
    bool isReleasing() const noexcept { return envStage == EnvStage::release || envStage == EnvStage::idle; }
    int getCurrentNote() const noexcept { return currentNote; }
    int getActiveGrainCount() const noexcept { return activeGrains; }
    std::uint64_t getGrainsStarted() const noexcept { return grainsStarted; }
    double getLastGrainStart() const noexcept { return lastGrainStart; }   // buffer frames

    // Playhead as a share of the buffer, for the UI.
    float getPlayheadPosition() const noexcept { return playheadPosition; }

    // Copies this voice's sounding grains into out (at most max); returns how many.
    int collectViews (GrainView* out, int max, std::uint8_t voiceIndex) const noexcept;

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
        GrainEnvelope envelope;
        Interpolation interpolation = Interpolation::cubic;

        // Loop reads (LOOP mode, grains that reach the loop point): past
        // loopEnd the read continues loopLength earlier, and over the last
        // fadeLength frames before loopEnd it crossfades into that audio.
        bool loops = false;
        int backwardWraps = 0;
        float spanStart = 0.0f, spanEnd = 0.0f, lastEnvelope = 0.0f;   // for the UI      // reversed grains: loop points still to cross
        double loopBegin = 0.0, loopEnd = 0.0, loopLength = 0.0, fadeStart = 0.0, fadeLength = 1.0;
    };

    enum class EnvStage { idle, attack, decay, sustain, release };

    struct Region { double begin, end; };

    void spawnGrain (const GrainParams&, const SampleBuffer&) noexcept;
    bool advancePlayhead (const GrainParams&, const SampleBuffer&) noexcept;
    float nextEnvelope (const GrainParams&) noexcept;
    struct GainEstimate { float estimate, coherent, incoherent; };
    GainEstimate gainEstimate (const GrainParams&, const SampleBuffer*) const noexcept;
    double pitchRatio (const GrainParams&, const SampleBuffer&) const noexcept;
    int grainLength (const GrainParams&, const Region&, double ratio) const noexcept;
    float readGrain (const Grain&, int channel) const noexcept;
    double grainStartFor (const Region&, double spanFrames) const noexcept;
    double scatterWithin (const Region&, double spanFrames, double scatter) const noexcept;
    static Region regionOf (const GrainParams&, const SampleBuffer&) noexcept;

    double sampleRate = 48000.0;
    HannWindow window;
    Random random;
    std::array<Grain, maxGrains> grains {};
    int activeGrains = 0;

    int currentNote = -1;
    double noteRatio = 1.0;
    float velocity = 1.0f;
    double playhead = 0.0;          // buffer frames
    float playheadPosition = 0.0f;
    double samplesToNextGrain = 0.0;
    bool spawning = false;
    std::uint64_t grainsStarted = 0;
    double lastGrainStart = 0.0;

    // Beat-grid scheduling: the next grid line, and where the previous render
    // call ended, to notice transport jumps.
    bool firstGrainPending = false;
    bool gridLocked = false;
    double nextGridPpq = 0.0, expectedPpq = 0.0, lockedDivision = 0.0, lastSpawnPpq = -1.0e9;

    EnvStage envStage = EnvStage::idle;
    float envLevel = 0.0f;
    float releaseStep = 0.0f;       // set at note-off, from the level reached

    float outputGain = 1.0f;        // overlap normalisation, smoothed
    float gainSmoothing = 0.0f;

    // Auto-gain: smoothed power of the grains' source audio (weighted by their
    // envelopes), the envelope weight, and the power of their sum.
    float autoGain = 1.0f;
    float sourcePower = 0.0f, envelopeWeight = 0.0f, sumPower = 0.0f, autoGainSmoothing = 0.0f;
};

} // namespace silt
