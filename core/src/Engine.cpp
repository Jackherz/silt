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

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& v : voices)
        v.prepare (sampleRate);

    maxBlock = std::max (1, maxBlockSize);
    live = std::make_unique<SampleBuffer> ((int) (ringSeconds * sampleRate), sampleRate);
    liveWrite = 0;
    lastOutLeft.assign ((size_t) maxBlock, 0.0f);
    lastOutRight.assign ((size_t) maxBlock, 0.0f);
    lastOutCount = 0;
    dcIn = dcOut = lowpass = {};
    heldNotes = 0;
    droneVoice = -1;

    const auto coefficient = [sampleRate] (double ms) { return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate))); };
    followerCoefficients = { coefficient (1.0), coefficient (40.0), coefficient (60.0), coefficient (300.0) };
    fastLevel = slowLevel = 0.0f;
    gateOpen = false;
    samplesSinceTrigger = 0;
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

void Engine::handleEvent (const NoteEvent& e, const GrainParams& p, const SampleBuffer* source) noexcept
{
    if (e.isNoteOn)
    {
        ++heldNotes;
        if (droneVoice >= 0)
        {
            voices[(size_t) droneVoice].noteOff();   // a played key takes over from RUN
            droneVoice = -1;
        }
        if (source != nullptr)
            voiceForNoteOn (e.note).noteOn (e.note, std::clamp (e.velocity, 0.0f, 1.0f), *source, p);
        return;
    }

    heldNotes = std::max (0, heldNotes - 1);
    for (size_t i = 0; i < voices.size(); ++i)
        if ((int) i != droneVoice && voices[i].getCurrentNote() == e.note && ! voices[i].isReleasing())
            voices[i].noteOff();
}

void Engine::startDrone (const GrainParams& p, const SampleBuffer& source, float velocity) noexcept
{
    auto& voice = droneVoice >= 0 ? voices[(size_t) droneVoice] : voiceForNoteOn (p.rootNote);
    voice.noteOn (p.rootNote, velocity, source, p);   // from its current level: retriggers don't click
    droneVoice = (int) (&voice - voices.data());
}

// Live mode with no keys held: a voice at the root note plays grains, so Silt
// works as a plain effect (LiveTrigger). Released when a key is played or the
// mode changes. In follow mode the input is the player: its level opens and
// releases the voice's ADSR, a jump of 6 dB over its recent level retriggers
// it, and the level at the trigger sets the velocity.
void Engine::updateDrone (const GrainParams& p, const SampleBuffer* source,
                          const float* inLeft, const float* inRight, int numSamples) noexcept
{
    const bool wanted = p.live && p.liveTrigger != LiveTrigger::off && heldNotes == 0 && source != nullptr;

    if (! wanted)
    {
        if (droneVoice >= 0)
            voices[(size_t) droneVoice].noteOff();
        droneVoice = -1;
        gateOpen = false;
        return;
    }

    if (p.liveTrigger == LiveTrigger::always)
    {
        if (droneVoice < 0 || voices[(size_t) droneVoice].isReleasing())
            startDrone (p, *source, 1.0f);
        return;
    }

    constexpr float openAt = 0.01f, closeAt = 0.005f;   // −40 dB, −46 dB
    const int minRetrigger = (int) (0.05 * voices[0].getSampleRate());
    bool trigger = false, release = false;
    float triggerLevel = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = inLeft != nullptr ? std::abs (inLeft[i]) : 0.0f;
        const float r = inRight != nullptr ? std::abs (inRight[i]) : l;
        const float x = std::max (l, r);
        fastLevel += (x > fastLevel ? followerCoefficients[0] : followerCoefficients[1]) * (x - fastLevel);
        slowLevel += (x > slowLevel ? followerCoefficients[2] : followerCoefficients[3]) * (x - slowLevel);
        ++samplesSinceTrigger;

        const bool onset = fastLevel > openAt && fastLevel > 2.0f * slowLevel && samplesSinceTrigger > minRetrigger;
        if ((! gateOpen && fastLevel > openAt) || (gateOpen && onset))
        {
            gateOpen = true;
            trigger = true;
            release = false;
            samplesSinceTrigger = 0;
        }
        else if (gateOpen && fastLevel < closeAt)
        {
            gateOpen = false;
            release = true;
            trigger = false;
        }
        triggerLevel = std::max (triggerLevel, fastLevel);
    }

    if (trigger)
        startDrone (p, *source, std::clamp (0.4f + 2.0f * triggerLevel, 0.4f, 1.0f));
    else if (release && droneVoice >= 0)
        voices[(size_t) droneVoice].noteOff();
}

namespace
{
// Linear to 0.8, then a soft knee to full scale: keeps feedback from running away.
float feedbackClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs (x);
    return a <= knee ? x : std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}
} // namespace

// Writes this block's input into the ring (plan §4 "Grain-Output feedback"):
// input plus the previous block's output × feedback, the feedback DC-blocked,
// low-passed and soft-clipped so it settles instead of running away.
void Engine::writeLive (const float* inLeft, const float* inRight, int numSamples, const GrainParams& p) noexcept
{
    const float fb = (float) std::clamp (p.feedback, 0.0, 1.0);
    const int frames = live->getNumFrames();
    const float* in[2] { inLeft, inRight != nullptr ? inRight : inLeft };
    const std::vector<float>* out[2] { &lastOutLeft, &lastOutRight };

    for (int ch = 0; ch < 2; ++ch)
    {
        float* ring = live->getWritePointer (ch);
        int w = liveWrite;
        for (int i = 0; i < numSamples; ++i)
        {
            float x = in[ch] != nullptr ? in[ch][i] : 0.0f;
            if (fb > 0.0f)
            {
                const float back = i < lastOutCount ? (*out[ch])[(size_t) i] : 0.0f;
                const float dc = back - dcIn[(size_t) ch] + 0.995f * dcOut[(size_t) ch];
                dcIn[(size_t) ch] = back;
                dcOut[(size_t) ch] = dc;
                lowpass[(size_t) ch] += 0.5f * (dc - lowpass[(size_t) ch]);
                x = feedbackClip (x + fb * lowpass[(size_t) ch]);
            }
            ring[w] = x;
            if (++w == frames)
                w = 0;
        }
    }
    liveWrite = (liveWrite + numSamples) % frames;
}

void Engine::allNotesOff() noexcept
{
    for (auto& v : voices)
        v.noteOff();
    heldNotes = 0;
    droneVoice = -1;
}

void Engine::process (float* left, float* right, int numSamples,
                      const float* inLeft, const float* inRight,
                      const NoteEvent* events, int numEvents, const GrainParams& p) noexcept
{
    swapInPendingBuffer();

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    // Live mode writes the whole block into the ring first. "Now" for grain
    // placement is where the write head was at each sample, so data up to the
    // block's end is always already there.
    const int now = liveWrite;
    if (p.live && ! p.freeze && live != nullptr)
        writeLive (inLeft, inRight, std::min (numSamples, maxBlock), p);
    const SampleBuffer* source = p.live ? live.get() : current;
    updateDrone (p, source, inLeft, inRight, numSamples);

    // The pitch pattern: steps in −1…1, fixed for a seed so it's the same every
    // time the set is opened.
    if (! patternMade || p.patternSeed != patternSeed)
    {
        Random r (0x9e3779b97f4a7c15ull ^ p.patternSeed);
        for (auto& step : pattern)
            step = r.nextBipolar();
        patternSeed = p.patternSeed;
        patternMade = true;
    }

    // Render in segments between events so notes start sample-accurately.
    auto segment = p;
    segment.liveMargin = maxBlock;
    segment.pattern = pattern.data();
    segment.patternSteps = std::clamp (p.patternSteps, 1, (int) pattern.size());
    int pos = 0;
    for (int e = 0; e <= numEvents; ++e)
    {
        const int end = e < numEvents ? std::clamp (events[e].sampleOffset, pos, numSamples) : numSamples;
        segment.gridPpq = p.gridPpq + pos * p.ppqPerSample;
        segment.liveNow = now + (p.freeze ? 0 : pos);
        if (end > pos)
            for (auto& v : voices)
                v.render (left + pos, right + pos, end - pos, segment, source);
        pos = end;

        if (e < numEvents)
            handleEvent (events[e], segment, source);
    }

    // Keep this block's output for next block's feedback.
    lastOutCount = std::min (numSamples, maxBlock);
    std::copy (left, left + lastOutCount, lastOutLeft.begin());
    std::copy (right, right + lastOutCount, lastOutRight.begin());
}

} // namespace silt
