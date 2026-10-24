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

// The sampler engine. M1: one voice, last-note priority.
//
// Threads: setBuffer() and collectGarbage() run on the message thread,
// everything else on the audio thread. A new buffer reaches the audio thread
// through a lock-free slot; grains already playing finish on the old buffer,
// which is then handed back to be freed (plan §3: no allocation, locks or
// frees on the audio thread).
class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate);
    void setSeed (std::uint64_t seed) noexcept { voice.setSeed (seed); }

    // Message thread.
    void setBuffer (std::unique_ptr<SampleBuffer>);
    void collectGarbage();

    // Audio thread. Writes (not adds) left/right. Events must be sorted by offset.
    void process (float* left, float* right, int numSamples,
                  const NoteEvent* events, int numEvents, const GrainParams&) noexcept;

    const SampleBuffer* getCurrentBuffer() const noexcept { return current; }
    int getActiveGrainCount() const noexcept { return voice.getActiveGrainCount(); }

private:
    void swapInPendingBuffer() noexcept;
    void handleEvent (const NoteEvent&, const GrainParams&) noexcept;

    GrainVoice voice;

    SampleBuffer* current = nullptr;     // owned; new grains read this
    SampleBuffer* previous = nullptr;    // owned; still read by older grains
    std::atomic<SampleBuffer*> pending { nullptr };   // message → audio
    std::atomic<SampleBuffer*> retired { nullptr };   // audio → message
};

} // namespace silt
