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
    for (auto& v : voices)
        v.prepare (sampleRate);
}

void Engine::setSeed (std::uint64_t seed) noexcept
{
    // Each voice gets its own stream, so chords don't scatter in lockstep.
    for (size_t i = 0; i < voices.size(); ++i)
        voices[i].setSeed (seed * 0x9e3779b97f4a7c15ull + i + 1);
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

int Engine::getActiveGrainCount() const noexcept
{
    int count = 0;
    for (const auto& v : voices)
        count += v.getActiveGrainCount();
    return count;
}

void Engine::swapInPendingBuffer() noexcept
{
    if (previous != nullptr && retired.load (std::memory_order_acquire) == nullptr)
    {
        int stillReading = 0;
        for (const auto& v : voices)
            stillReading += v.countGrainsUsing (previous);

        if (stillReading == 0)
        {
            retired.store (previous, std::memory_order_release);
            previous = nullptr;
        }
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

// Same key again: that voice. Else a silent voice, else the oldest released
// one, else the oldest of all.
GrainVoice& Engine::voiceForNoteOn (int note) noexcept
{
    int best = -1;
    const auto older = [this, &best] (int i) { return best < 0 || startedAt[(size_t) i] < startedAt[(size_t) best]; };

    for (int i = 0; i < numVoices; ++i)
        if (voices[(size_t) i].isActive() && ! voices[(size_t) i].isReleasing() && voices[(size_t) i].getCurrentNote() == note)
            best = i;

    if (best < 0)
        for (int i = 0; i < numVoices; ++i)
            if (! voices[(size_t) i].isActive() && older (i))
                best = i;

    if (best < 0)
        for (int i = 0; i < numVoices; ++i)
            if (voices[(size_t) i].isReleasing() && older (i))
                best = i;

    if (best < 0)
        for (int i = 0; i < numVoices; ++i)
            if (older (i))
                best = i;

    startedAt[(size_t) best] = ++noteCounter;
    return voices[(size_t) best];
}

void Engine::handleEvent (const NoteEvent& e, const GrainParams& p) noexcept
{
    if (e.isNoteOn)
    {
        if (current != nullptr)
            voiceForNoteOn (e.note).noteOn (e.note, std::clamp (e.velocity, 0.0f, 1.0f), *current, p);
        return;
    }

    for (auto& v : voices)
        if (v.getCurrentNote() == e.note && ! v.isReleasing())
            v.noteOff();
}

void Engine::allNotesOff() noexcept
{
    for (auto& v : voices)
        v.noteOff();
}

void Engine::process (float* left, float* right, int numSamples,
                      const NoteEvent* events, int numEvents, const GrainParams& p) noexcept
{
    swapInPendingBuffer();

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    // Render in segments between events so notes start sample-accurately.
    auto segment = p;
    int pos = 0;
    for (int e = 0; e <= numEvents; ++e)
    {
        const int end = e < numEvents ? std::clamp (events[e].sampleOffset, pos, numSamples) : numSamples;
        segment.gridPpq = p.gridPpq + pos * p.ppqPerSample;
        if (end > pos)
            for (auto& v : voices)
                v.render (left + pos, right + pos, end - pos, segment, current);
        pos = end;

        if (e < numEvents)
            handleEvent (events[e], p);
    }
}

} // namespace silt
