#pragma once

#include <silt/GrainVoice.h>

#include <atomic>
#include <memory>

namespace silt
{

struct NoteEvent
{
    int sampleOffset = 0;   // within the block
    int note = 60;
    float velocity = 1.0f;  // 0…1
    bool isNoteOn = true;
};

// The sampler engine: 16 voices sharing one read-only sample (plan D11).
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

    Engine();
    ~Engine();

    void prepare (double sampleRate);
    void setSeed (std::uint64_t seed) noexcept;

    // Message thread.
    void setBuffer (std::unique_ptr<SampleBuffer>);
    void collectGarbage();

    // Audio thread. Writes (not adds) left/right. Events must be sorted by offset.
    void process (float* left, float* right, int numSamples,
                  const NoteEvent* events, int numEvents, const GrainParams&) noexcept;
    void allNotesOff() noexcept;

    const SampleBuffer* getCurrentBuffer() const noexcept { return current; }
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
    void handleEvent (const NoteEvent&, const GrainParams&) noexcept;
    GrainVoice& voiceForNoteOn (int note) noexcept;

    std::array<GrainVoice, numVoices> voices;
    std::array<std::uint64_t, numVoices> startedAt {};   // note-on order, for stealing
    std::uint64_t noteCounter = 0;

    SampleBuffer* current = nullptr;     // owned; new grains read this
    SampleBuffer* previous = nullptr;    // owned; still read by older grains
    std::atomic<SampleBuffer*> pending { nullptr };   // message → audio
    std::atomic<SampleBuffer*> retired { nullptr };   // audio → message
};

} // namespace silt
