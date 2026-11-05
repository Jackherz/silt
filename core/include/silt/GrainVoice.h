#pragma once

#include <silt/Dsp.h>
#include <silt/SampleBuffer.h>
#include <silt/Snap.h>

#include <array>

namespace silt
{

// What a playhead does when it reaches either end of its region.
enum class EndMode { loop, hold, stop };

// Live mode with no keys held: nothing plays (off), a voice at the root note
// plays constantly (always), or the input plays that voice's envelope (follow):
// getting louder opens it, going quiet releases it, a sudden jump retriggers.
enum class LiveTrigger { off, always, follow };

// The Grain and Voice page controls (spec §3, plan §5), in engine units.
// Grain controls are latched into each grain at its onset, so changes never
// alter a grain in flight.
struct GrainParams
{
    // Grain
    double pitchSemitones = 0.0;   // −24…+24, added to the key's transposition
    double sizeMs = 100.0;         // grain length, 10…2000 ms
    double rateHz = 20.0;          // grains started per second, 0.1…100
    double spray = 0.0;            // 0…1: start scatter, up to ±1 s
    double reverseChance = 0.0;    // 0…1: chance a grain plays backwards
    double jitter = 0.0;           // 0…1: random spread of grain start times (0 = clockwork).
                                   // Evenly spaced grains at fast rates sum to a pitched
                                   // comb tone; jitter breaks it up. Not used on the beat grid.
    double randomAmp = 0.0;        // 0…1: per-grain random level drop
    double randomPan = 0.0;        // 0…1: per-grain random stereo position (1 = anywhere L to R)
    double fade = 1.0;             // grain envelope (GrainEnvelope)
    double slant = 0.5;
    double ease = 0.5;
    double declickMs = 1.0;        // shortest fade allowed

    // Pitch page. Each grain's pitch is the key's transposition + pitchSemitones
    // + a random offset (±randomPitch) + the pattern's current step × patternDepth,
    // then snapped to the scale in key (0 = C … 11 = B).
    double randomPitch = 0.0;       // semitones either way
    Scale scale = Scale::off;
    int key = 0;
    double patternDepth = 0.0;      // semitones at a pattern value of ±1
    int patternSteps = 8;
    const float* pattern = nullptr; // patternSteps values in −1…1 (set by the engine)
    std::uint32_t patternSeed = 1;  // picks the pattern; the engine makes it
    // Heads: playheads per note, spread across the region (or further back in
    // time in Live mode) by headSpread; grains take turns between them.
    int heads = 1;
    double headSpread = 0.5;        // 0…1 of the region (Live: of 1 s)

    // Grains mode (after Tonverk's Grainer "amount"): when > 0, this many grains
    // sound at once on average and the start interval follows the grain length,
    // so changing Size keeps the texture equally thick. rateHz is then ignored.
    int grainCount = 0;
    // The most grains this voice may have sounding at once (the engine shares
    // its grain budget out across the voices playing; a voice at its share
    // skips new grains, as a full pool does).
    int grainLimit = 64;

    // Live mode: grains read the engine's live ring buffer instead of the
    // sample, liveDelayMs behind the newest audio. The engine sets liveNow (the
    // ring frame being written at this render call's first sample) and
    // liveMargin (how far ahead of "now" a block can write).
    bool live = false;
    double liveDelayMs = 250.0;
    double feedback = 0.0;         // 0…1.2: grain output written back into the ring. In FEED the
                                   // grains' losses are made up, so 1 holds, less fades and more
                                   // grows into the feedback's soft clip
    bool selfFeed = false;         // FEED: the ring takes only Silt's own output, not the input;
                                   // seeded from the sample when it starts
    bool freeze = false;           // stop writing: grains play the frozen ring
    LiveTrigger liveTrigger = LiveTrigger::always;   // what plays grains while no keys are held
    double liveNow = 0.0, liveMargin = 0.0;

    // Beat grid. When set, grains start on multiples of gridDivision (in
    // quarter notes) of the host position instead of every 1/rateHz seconds;
    // a note's first grain still starts on the note. rateHz should match the
    // division at the current tempo (it sets the overlap gain).
    bool gridSync = false;
    double gridPpq = 0.0;          // host position at this render call's first sample
    double ppqPerSample = 0.0;
    double gridDivision = 0.25;    // quarter notes between grains

    // Playhead and region
    // Slice: keys from C1 (firstSliceNote) up each play their own slice of
    // the region, at the sample's own pitch: the region cut in equal parts,
    // or at the sample's transients. Keys below or past the slices are silent.
    bool slice = false;
    bool sliceTransients = false;
    int sliceCount = 16;           // equal slices, 1…128
    const float* sliceTable = nullptr;   // MANUAL: where each slice starts, shares of the sample, ascending
    int sliceTableSize = 0;
    double start = 0.0;            // 0…1: region start, where a note starts its playhead
    double length = 1.0;           // 0…1: region length as a share of the sample
    double scan = 1.0;             // playhead speed, −4…+4 (1 = real time, 0 = frozen)
    EndMode endMode = EndMode::loop;
    Snap snap = Snap::off;         // where the region's edges snap to

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
    float pan = 0.0f;                 // −1 left … +1 right
    std::uint8_t voice = 0, slot = 0;
    bool reversed = false;
};

// One MIDI-played voice: a playhead moving through a region of the sample,
// a scheduler starting grains around it, a fixed grain pool and an ADSR.
class GrainVoice
{
public:
    static constexpr int maxGrains = 256;   // more than the longest SIZE at the fastest RATE asks for

    void prepare (double sampleRate) noexcept;
    void setSeed (std::uint64_t seed) noexcept { random.setSeed (seed); }

    void noteOn (int note, float velocity, const SampleBuffer& buffer, const GrainParams&) noexcept;

    static constexpr int firstSliceNote = 36;   // C1: the first slice
    // How many slices the keys reach, in Slice mode.
    static int sliceCountFor (const GrainParams&, const SampleBuffer&) noexcept;
    void noteOff() noexcept;
    double getSampleRate() const noexcept { return sampleRate; }
    void reset() noexcept;

    bool isActive() const noexcept { return envStage != EnvStage::idle || activeGrains > 0; }
    bool isReleasing() const noexcept { return envStage == EnvStage::release || envStage == EnvStage::idle; }
    int getCurrentNote() const noexcept { return currentNote; }
    int getActiveGrainCount() const noexcept { return activeGrains; }
    std::uint64_t getGrainsStarted() const noexcept { return grainsStarted; }
    double getLastGrainStart() const noexcept { return lastGrainStart; }   // buffer frames
    double getLastGrainSemitones() const noexcept { return lastGrainSemitones; }   // above the root note

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
        float spanStart = 0.0f, spanEnd = 0.0f, lastEnvelope = 0.0f, pan = 0.0f;   // for the UI      // reversed grains: loop points still to cross
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
    double nextGrainSemitones (const GrainParams&) noexcept;
    int grainLength (const GrainParams&, const Region&, double ratio) const noexcept;
    float readGrain (const Grain&, int channel) const noexcept;
    double grainStartFor (const Region&, double spanFrames) const noexcept;
    double liveStart (const GrainParams&, const SampleBuffer& ring, int length, double ratio, double scatter) const noexcept;
    double scatterWithin (const Region&, double spanFrames, double scatter) const noexcept;
    static Region computeRegion (const GrainParams&, const SampleBuffer&) noexcept;
    Region regionFor (const GrainParams&, const SampleBuffer&) const noexcept;   // this voice's: its slice, in Slice mode
    static int transientEdges (const SampleBuffer&, const Region& whole, std::array<double, 129>& edges) noexcept;
    // The region, computed once per render call (snapping searches the analysis).
    Region regionOf (const GrainParams& p, const SampleBuffer& buffer) const noexcept
    {
        return &buffer == regionBuffer ? blockRegion : regionFor (p, buffer);
    }
    int slice = -1;   // Slice mode: which slice this voice's note plays (−1: not slicing)
    Region blockRegion { 0.0, 0.0 };
    const SampleBuffer* regionBuffer = nullptr;

    double sampleRate = 48000.0;
    HannWindow window;
    Random random;
    std::array<Grain, maxGrains> grains {};
    int activeGrains = 0;
    int grainsInUse = 0;   // one past the last playing grain: new ones take the lowest free place

    int currentNote = -1;
    double noteRatio = 1.0;
    float velocity = 1.0f, heardVelocity = 1.0f, velocitySmoothing = 0.0f;
    double playhead = 0.0;          // buffer frames
    float playheadPosition = 0.0f;
    double samplesToNextGrain = 0.0;
    bool spawning = false;
    std::uint64_t grainsStarted = 0;
    double lastGrainStart = 0.0, lastGrainSemitones = 0.0;
    int patternStep = 0, headCounter = 0;

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
