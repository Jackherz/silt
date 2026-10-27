// Realtime safety: processBlock must not touch the heap. Every allocation in
// this test binary is counted while a thread is marked as the audio thread,
// and each scenario below has to get through its blocks without one.

#include <catch2/catch_test_macros.hpp>

#include "../plugin/PluginProcessor.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool onAudioThread = false;
std::atomic<int> audioAllocations { 0 };

void* allocate (std::size_t size)
{
    if (onAudioThread)
        ++audioAllocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
struct AudioRig
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;
    juce::AudioBuffer<float> buffer;

    AudioRig()
    {
        silt.enableAllBuses();
        silt.prepareToPlay (48000.0, 512);
        buffer.setSize (juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels()), 512);
        // The sample a new instance opens with arrives on the message thread.
        for (int i = 0; i < 300 && silt.getOverview().minimum.empty(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    void set (const char* id, float value)   // in the parameter's own units
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter (id));
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Blocks with some input and a few notes; returns allocations made in them.
    int run (int blocks, bool warmUp = true)
    {
        juce::Random r (1);
        const auto block = [&] (int b)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, 0.3f * (r.nextFloat() * 2.0f - 1.0f));
            juce::MidiBuffer midi;
            if (b % 20 == 0)
                for (const int note : { 36, 48, 55, 60 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), b % 7);
            if (b % 20 == 12)
                for (const int note : { 36, 48, 55, 60 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), 3);
            if (b % 33 == 5)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, b % 128), 0);
            silt.processBlock (buffer, midi);
        };
        if (warmUp)
            for (int b = 0; b < 40; ++b)
                block (b);   // first-time setup (a new chain order fading in, and so on) may settle first
        audioAllocations = 0;
        onAudioThread = true;
        for (int b = 0; b < blocks; ++b)
            block (b);
        onAudioThread = false;
        return audioAllocations.load();
    }
};
} // namespace

TEST_CASE ("Realtime: processBlock allocates nothing, across modes and effects")
{
    AudioRig rig;
    REQUIRE_FALSE (rig.silt.getOverview().minimum.empty());

    SECTION ("plain notes") { CHECK (rig.run (200) == 0); }

    SECTION ("every effect on, a new chain order, ON switches flipping")
    {
        rig.set ("resMix", 60.0f);
        rig.set ("resDrive", 50.0f);
        rig.set ("resDrift", 40.0f);
        rig.set ("filterMode", 2.0f);
        rig.set ("colorDrive", 50.0f);
        rig.set ("colorBits", 8.0f);
        rig.set ("colorRate", 8000.0f);
        rig.set ("colorComp", 50.0f);
        rig.set ("colorNoise", 30.0f);
        rig.set ("colorWow", 40.0f);
        rig.set ("spaceDelay", 50.0f);
        rig.set ("spaceReverb", 50.0f);
        rig.set ("spaceShimmer", 60.0f);
        rig.set ("spaceDuck", 2.0f);
        CHECK (rig.run (100) == 0);
        rig.set ("chainOrder", 12.0f);
        rig.set ("spaceOn", 0.0f);
        CHECK (rig.run (100, false) == 0);
        rig.set ("spaceOn", 1.0f);
        CHECK (rig.run (100, false) == 0);
    }

    SECTION ("per-voice modulation, cross-modulation and routes")
    {
        rig.set ("m1Voice", 1.0f);
        rig.set ("m1Type", 1.0f);
        rig.set ("m2Type", 2.0f);
        rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("spray"), 0.5f);
        rig.silt.setRouteAmount (1, rig.silt.modulationTarget ("filterCutoff"), 0.4f);
        rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("m2Rate"), 0.3f);
        CHECK (rig.run (200) == 0);
    }

    SECTION ("LIVE, FEED and SLICE")
    {
        rig.set ("keyMode", 1.0f);
        rig.set ("feedback", 60.0f);
        CHECK (rig.run (100) == 0);
        rig.set ("freeze", 1.0f);
        CHECK (rig.run (50, false) == 0);
        rig.set ("keyMode", 2.0f);
        rig.set ("freeze", 0.0f);
        CHECK (rig.run (100) == 0);
        rig.set ("keyMode", 3.0f);
        rig.set ("sliceMode", 1.0f);
        CHECK (rig.run (100) == 0);
    }

    SECTION ("capture armed and recording")
    {
        rig.silt.armCapture();
        CHECK (rig.run (200, false) == 0);
        rig.silt.cancelCapture();
    }
}

TEST_CASE ("Realtime: the allocation counter itself sees an allocation")
{
    audioAllocations = 0;
    onAudioThread = true;
    auto* p = new std::vector<float> (64);
    onAudioThread = false;
    delete p;
    CHECK (audioAllocations.load() >= 1);
}
