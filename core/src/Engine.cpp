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
int Engine::voiceForNoteOn (int note) noexcept
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
    return best;
}

// Starts a note on a voice, with that voice's own parameters when per-voice
// modulation is on (the provider resets the voice's modulators first, so a
// stolen voice doesn't carry the old note's envelope).
void Engine::startNote (int voice, int note, float velocity, const SampleBuffer& source, const GrainParams& p) noexcept
{
    if (voiceParams != nullptr)
        voiceParams->noteStarted (voice, note, velocity);   // always: it tracks which notes each voice holds
    if (voiceParams != nullptr && voiceParams->isActive())
    {
        auto own = p;
        voiceParams->apply (voice, own);
        voices[(size_t) voice].noteOn (note, velocity, source, own);
        return;
    }
    voices[(size_t) voice].noteOn (note, velocity, source, p);
}

void Engine::releaseVoice (int voice) noexcept
{
    voices[(size_t) voice].noteOff();
    if (voiceParams != nullptr)
        voiceParams->noteReleased (voice);
}

void Engine::handleEvent (const NoteEvent& e, const GrainParams& p, const SampleBuffer* source) noexcept
{
    if (e.isNoteOn)
    {
        ++heldNotes;
        if (droneVoice >= 0)
        {
            releaseVoice (droneVoice);   // a played key takes over from RUN
            droneVoice = -1;
        }
        if (source != nullptr)
            startNote (voiceForNoteOn (e.note), e.note, std::clamp (e.velocity, 0.0f, 1.0f), *source, p);
        return;
    }

    heldNotes = std::max (0, heldNotes - 1);
    for (size_t i = 0; i < voices.size(); ++i)
        if ((int) i != droneVoice && voices[i].getCurrentNote() == e.note && ! voices[i].isReleasing())
            releaseVoice ((int) i);
}

void Engine::startDrone (const GrainParams& p, const SampleBuffer& source, float velocity) noexcept
{
    const int voice = droneVoice >= 0 ? droneVoice : voiceForNoteOn (p.rootNote);
    startNote (voice, p.rootNote, velocity, source, p);   // from its current level: retriggers don't click
    droneVoice = voice;
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
    // FEED has no input to follow: FOLLOW plays as ALWAYS.
    const auto mode = p.selfFeed && p.liveTrigger == LiveTrigger::follow ? LiveTrigger::always : p.liveTrigger;

    if (! wanted)
    {
        if (droneVoice >= 0)
            releaseVoice (droneVoice);
        droneVoice = -1;
        gateOpen = false;
        return;
    }

    if (mode == LiveTrigger::always)
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
        releaseVoice (droneVoice);
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
// low-passed and soft-clipped so it settles instead of running away. FEED
// leaves the input out: the ring holds only what the grains played.
void Engine::writeLive (const float* inLeft, const float* inRight, int numSamples, const GrainParams& p) noexcept
{
    const float fb = (float) std::clamp (p.feedback, 0.0, 1.2);
    float gain = fb;
    if (p.selfFeed)
    {
        inLeft = inRight = nullptr;

        // Grains don't give back the level they read (gaps between them, their
        // envelopes, sparse settings): compare the last block's output with the
        // ring where the grains read, and make the difference up, so 100% holds,
        // less fades and more grows into the clip at any density.
        const int frames = live->getNumFrames();
        const int count = std::min (lastOutCount, numSamples);
        if (count > 0 && fb > 0.0f)
        {
            const int delay = (int) (p.liveDelayMs * 0.001 * live->getSampleRate());
            double outPower = 0.0, ringPower = 0.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* ring = live->getReadPointer (ch);
                const auto& out = ch == 0 ? lastOutLeft : lastOutRight;
                for (int i = 0; i < count; ++i)
                {
                    outPower += (double) out[(size_t) i] * out[(size_t) i];
                    const float r = ring[((liveWrite - delay - count + i) % frames + frames) % frames];
                    ringPower += (double) r * r;
                }
            }
            if (outPower > 1.0e-12 && ringPower > 1.0e-12)
            {
                const float target = (float) std::clamp (std::sqrt (ringPower / outPower), 1.0, 4.0);
                feedMakeup += (1.0f - std::exp (-(float) count / (0.3f * (float) live->getSampleRate()))) * (target - feedMakeup);
            }
        }
        gain = fb * feedMakeup;
    }
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
                x = feedbackClip (x + gain * lowpass[(size_t) ch]);
            }
            ring[w] = x;
            if (++w == frames)
                w = 0;
        }
    }
    liveWrite = (liveWrite + numSamples) % frames;
}

// FEED starting: a loop with nothing in it stays silent, so fill the ring
// with the sample (repeated if it's shorter), placed so the grains, DELAY
// behind the write head, start reading at its START: the part chosen, not
// whatever (often a quiet tail) happens to end the file.
void Engine::seedLive (const GrainParams& p) noexcept
{
    if (current == nullptr || live == nullptr || current->getNumFrames() == 0)
        return;
    const int frames = live->getNumFrames(), length = current->getNumFrames();
    const auto delay = (long long) (p.liveDelayMs * 0.001 * live->getSampleRate());
    const auto startFrame = (long long) (std::clamp (p.start, 0.0, 1.0) * (length - 1));
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* from = current->getReadPointer (ch);
        float* ring = live->getWritePointer (ch);
        for (int back = 1; back <= frames; ++back)   // `back` frames behind the write head
        {
            const auto index = ((startFrame + delay - back) % length + length) % length;
            ring[((liveWrite - back) % frames + frames) % frames] = from[index];
        }
    }
    dcIn = dcOut = lowpass = {};
    feedMakeup = 1.0f;
}

void Engine::allNotesOff() noexcept
{
    for (int v = 0; v < numVoices; ++v)
        releaseVoice (v);
    heldNotes = 0;
    droneVoice = -1;
}

void Engine::process (float* left, float* right, int numSamples,
                      const float* inLeft, const float* inRight,
                      const NoteEvent* events, int numEvents, const GrainParams& p) noexcept
{
    // Longer than prepared for: in pieces, so the ring, the grains' clock and
    // the feedback stay in step (a ring write never runs past maxBlock).
    if (numSamples > maxBlock)
    {
        std::array<NoteEvent, 128> part {};
        int e = 0;
        for (int start = 0; start < numSamples; start += maxBlock)
        {
            const int len = std::min (maxBlock, numSamples - start);
            int n = 0;
            for (; e < numEvents && events[e].sampleOffset < start + len && n < (int) part.size(); ++e, ++n)
            {
                part[(size_t) n] = events[e];
                part[(size_t) n].sampleOffset = std::max (0, events[e].sampleOffset - start);   // a full batch spills to the next piece
            }
            auto piece = p;
            piece.gridPpq = p.gridPpq + start * p.ppqPerSample;
            process (left + start, right + start, len, inLeft != nullptr ? inLeft + start : nullptr,
                     inRight != nullptr ? inRight + start : nullptr, part.data(), n, piece);
        }
        return;
    }

    swapInPendingBuffer();

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    // Live mode writes the whole block into the ring first. "Now" for grain
    // placement is where the write head was at each sample, so data up to the
    // block's end is always already there.
    if (p.live && p.selfFeed && ! wasSelfFeeding)
        seedLive (p);
    wasSelfFeeding = p.live && p.selfFeed;
    const int now = liveWrite;
    if (p.live && ! p.freeze && live != nullptr)
        writeLive (inLeft, inRight, numSamples, p);
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
        {
            if (voiceParams != nullptr && voiceParams->isActive())
                for (int v = 0; v < numVoices; ++v)
                {
                    auto own = segment;   // ~400 bytes; grains latch it when they start
                    voiceParams->apply (v, own);
                    voices[(size_t) v].render (left + pos, right + pos, end - pos, own, source);
                }
            else
                for (auto& v : voices)
                    v.render (left + pos, right + pos, end - pos, segment, source);
        }
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
