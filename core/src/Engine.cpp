#include <silt/Engine.h>

#include <algorithm>

namespace silt
{

Engine::Engine() = default;

Engine::~Engine()
{
    delete current;
    delete previous;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void Engine::prepare (double sampleRate)
{
    voice.prepare (sampleRate);
}

void Engine::setBuffer (std::unique_ptr<SampleBuffer> buffer)
{
    // A buffer the audio thread hasn't picked up yet is simply replaced.
    delete pending.exchange (buffer.release());
}

void Engine::collectGarbage()
{
    delete retired.exchange (nullptr);
}

void Engine::swapInPendingBuffer() noexcept
{
    if (previous != nullptr && voice.countGrainsUsing (previous) == 0
        && retired.load (std::memory_order_acquire) == nullptr)
    {
        retired.store (previous, std::memory_order_release);
        previous = nullptr;
    }

    // Only one old buffer at a time: a newer one waits until it's retired.
    if (previous != nullptr)
        return;

    if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
    {
        previous = current;
        current = next;
    }
}

void Engine::handleEvent (const NoteEvent& e, const GrainParams& p) noexcept
{
    if (e.isNoteOn)
    {
        if (current != nullptr)
            voice.noteOn (e.note, std::clamp (e.velocity, 0.0f, 1.0f), *current, p);
    }
    else if (e.note == voice.getCurrentNote())
    {
        voice.noteOff();
    }
}

void Engine::process (float* left, float* right, int numSamples,
                      const NoteEvent* events, int numEvents, const GrainParams& p) noexcept
{
    swapInPendingBuffer();

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    // Render in segments between events so notes start sample-accurately.
    int pos = 0;
    for (int e = 0; e <= numEvents; ++e)
    {
        const int end = e < numEvents ? std::clamp (events[e].sampleOffset, pos, numSamples) : numSamples;
        if (end > pos)
            voice.render (left + pos, right + pos, end - pos, p, current);
        pos = end;

        if (e < numEvents)
            handleEvent (events[e], p);
    }
}

} // namespace silt
