#pragma once

#include <silt/GrainVoice.h>

#include <atomic>
#include <memory>
#include <vector>

namespace silt
{

struct NoteEvent
{
    int sampleOffset = 0;   // within the block
    int note = 60;
    float velocity = 1.0f;  // 0…1
    bool isNoteOn = true;
};

// The sampler engine: 16 voices sharing one read-only sample (plan D11), or in
// Live mode the live ring buffer: the last ringSeconds of input, written as it
// arrives (plus feedback), which grains read a delay behind.
//
// Threads: setBuffer() and collectGarbage() run on the message thread,
// everything else on the audio thread. A new buffer reaches the audio thread
// through a lock-free slot; grains already playing finish on the old buffer,
// which is then handed back to be freed (plan §3: no allocation, locks or
// frees on the audio thread).
class Engine
{
public:
    static constexpr int numVoices = 16;
    static constexpr double ringSeconds = 8.0;

    Engine();
    ~Engine();

    // Allocates the live ring; maxBlockSize bounds how far ahead a block writes.
    void prepare (double sampleRate, int maxBlockSize = 4096);
    void setSeed (std::uint64_t seed) noexcept;

    // Message thread.
    void setBuffer (std::unique_ptr<SampleBuffer>);
    void collectGarbage();

    // Audio thread. Writes (not adds) left/right. Events must be sorted by offset.
    // inLeft/inRight (may be null) feed the live ring in Live mode.
    void process (float* left, float* right, int numSamples,
                  const float* inLeft, const float* inRight,
                  const NoteEvent* events, int numEvents, const GrainParams&) noexcept;
    void process (float* left, float* right, int numSamples,
                  const NoteEvent* events, int numEvents, const GrainParams& p) noexcept
    {
        process (left, right, numSamples, nullptr, nullptr, events, numEvents, p);
    }
    void allNotesOff() noexcept;

    const SampleBuffer* getCurrentBuffer() const noexcept { return current; }
    const SampleBuffer* getLiveBuffer() const noexcept { return live.get(); }
    int getLiveWritePosition() const noexcept { return liveWrite; }
    int getActiveGrainCount() const noexcept;
    const GrainVoice& getVoice (int index) const noexcept { return voices[(size_t) index]; }

    // Every sounding grain, for the UI. Returns how many were written.
    int collectGrainViews (GrainView* out, int max) const noexcept
    {
        int n = 0;
        for (size_t v = 0; v < voices.size() && n < max; ++v)
            n += voices[v].collectViews (out + n, max - n, (std::uint8_t) v);
        return n;
    }

private:
    void swapInPendingBuffer() noexcept;
    void handleEvent (const NoteEvent&, const GrainParams&, const SampleBuffer*) noexcept;
    void writeLive (const float* inLeft, const float* inRight, int numSamples, const GrainParams&) noexcept;
    void updateDrone (const GrainParams&, const SampleBuffer*, const float* inLeft, const float* inRight, int numSamples) noexcept;
    void startDrone (const GrainParams&, const SampleBuffer&, float velocity) noexcept;
    GrainVoice& voiceForNoteOn (int note) noexcept;

    std::array<GrainVoice, numVoices> voices;
    std::array<std::uint64_t, numVoices> startedAt {};   // note-on order, for stealing
    std::uint64_t noteCounter = 0;

    SampleBuffer* current = nullptr;     // owned; new grains read this
    SampleBuffer* previous = nullptr;    // owned; still read by older grains
    std::atomic<SampleBuffer*> pending { nullptr };   // message → audio
    std::atomic<SampleBuffer*> retired { nullptr };   // audio → message

    // Live mode.
    std::unique_ptr<SampleBuffer> live;
    int liveWrite = 0, maxBlock = 4096;
    std::vector<float> lastOutLeft, lastOutRight;    // previous block's output, for feedback
    int lastOutCount = 0;
    std::array<float, 2> dcIn {}, dcOut {}, lowpass {};
    int heldNotes = 0;
    int droneVoice = -1;                             // the voice playing with no keys held, if any

    // The pitch pattern, regenerated when its seed changes.
    std::array<float, 16> pattern {};
    std::uint32_t patternSeed = 0;
    bool patternMade = false;

    // Input follower (LiveTrigger::follow): fast and slow level envelopes, the
    // gate they open, and the time since the last trigger.
    float fastLevel = 0.0f, slowLevel = 0.0f;
    bool gateOpen = false;
    int samplesSinceTrigger = 0;
    std::array<float, 4> followerCoefficients {};    // fast attack/release, slow attack/release
};

} // namespace silt
