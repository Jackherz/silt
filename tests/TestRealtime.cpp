// Realtime safety: processBlock must not touch the heap. Every allocation in
// this test binary is counted while a thread is marked as the audio thread,
// and each scenario below has to get through its blocks without one.

#include <catch2/catch_test_macros.hpp>

#include <iostream>

#include "../plugin/PluginProcessor.h"
#include "../plugin/Presets.h"

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
        rig.set ("filterMode", 1.0f);
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

    SECTION ("PER GRAIN routes, rolling every grain")
    {
        rig.set ("m1Type", 6.0f);
        rig.set ("m1GrainSpread", 2.0f);
        rig.set ("m1Voice", 1.0f);   // no effect on this type
        for (const auto* id : { "pitch", "size", "rate", "spray", "reverse", "randPan", "fade" })
            rig.silt.setRouteAmount (0, rig.silt.modulationTarget (id), 0.4f);
        CHECK (rig.run (200) == 0);
    }

    SECTION ("BOUNCE, synced SCAN and FREEZE on a sample")
    {
        rig.set ("endMode", 3.0f);
        rig.set ("scan", 2.0f);
        CHECK (rig.run (100) == 0);
        rig.set ("scanSync", 1.0f);
        CHECK (rig.run (100, false) == 0);
        rig.set ("freeze", 1.0f);
        CHECK (rig.run (50, false) == 0);
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

// Not run with the suite: `silt-plugin-tests "[benchmark]"`. How much of one
// core processBlock takes, typical and worst case, at a few block sizes.
TEST_CASE ("Benchmark: CPU for typical and worst-case settings", "[.][benchmark]")
{
    const auto measure = [] (double rate, int blockSize, bool worst)
    {
        juce::ScopedJuceInitialiser_GUI juce;
        SiltProcessor silt;
        silt.enableAllBuses();
        silt.prepareToPlay (rate, blockSize);
        for (int i = 0; i < 300 && silt.getOverview().minimum.empty(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto set = [&] (const char* id, float value)
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter (id));
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        int notes = 4;
        if (worst)
        {
            notes = 16;
            set ("rate", 100.0f);   // the densest grains
            set ("size", 500.0f);
            set ("heads", 8.0f);
            set ("resMix", 60.0f);
            set ("resDrive", 50.0f);
            set ("filterMode", 1.0f);
            set ("colorDrive", 50.0f);
            set ("colorComp", 50.0f);
            set ("colorWow", 40.0f);
            set ("spaceDelay", 50.0f);
            set ("spaceReverb", 50.0f);
            set ("spaceShimmer", 60.0f);
            set ("m1Voice", 1.0f);
            set ("m2Voice", 1.0f);
            silt.setRouteAmount (0, silt.modulationTarget ("spray"), 0.5f);
            silt.setRouteAmount (1, silt.modulationTarget ("pitch"), 0.1f);
        }
        juce::AudioBuffer<float> buffer (juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels()), blockSize);
        juce::MidiBuffer start;
        for (int n = 0; n < notes; ++n)
            start.addEvent (juce::MidiMessage::noteOn (1, 48 + n * 3, (juce::uint8) 100), 0);
        const int blocks = (int) (4.0 * rate / blockSize);   // four seconds
        double seconds = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi = start;
            const auto t0 = juce::Time::getHighResolutionTicks();
            silt.processBlock (buffer, midi);
            seconds += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        }
        if (worst)
            std::printf ("  (%d grains sounding at the end)\n", silt.activeGrains.load());
        return 100.0 * seconds / 4.0;
    };
    for (const bool worst : { false, true })
        for (const double rate : { 48000.0, 96000.0 })
            for (const int block : { 32, 128 })
                std::printf ("%-7s %5.0f Hz  %3d-sample blocks: %5.1f%% of one core\n", worst ? "worst" : "typical", rate, block, measure (rate, block, worst));
    SUCCEED();
}

// Not run with the suite: `silt-plugin-tests "[factory-density]"`. Which factory sounds a
// six-note chord pushes over the grain budget: the only ones its limit can change.
TEST_CASE ("Factory presets against the grain budget", "[.][factory-density]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    for (int i = 0; i < PresetManager::numFactoryPresets(); ++i)
    {
        SiltProcessor silt;
        silt.enableAllBuses();
        silt.prepareToPlay (48000.0, 128);
        // The sample a new instance opens with arrives on the message thread.
        for (int wait = 0; wait < 300 && silt.getOverview().minimum.empty(); ++wait)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        REQUIRE_FALSE (silt.getOverview().minimum.empty());
        PresetManager library (silt);
        const auto xml = PresetManager::factoryPreset (i);
        silt.applyPresetValues (*xml, library.factoryName (i));
        juce::AudioBuffer<float> buffer (juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels()), 128);
        juce::MidiBuffer midi;
        for (const int note : { 48, 55, 60, 64, 67, 71 })
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        int most = 0;
        float peak = 0.0f;
        double energy = 0.0;
        const int blocks = (int) (6.0 * 48000.0 / 128.0);
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            silt.processBlock (buffer, midi);
            midi.clear();
            std::array<silt::GrainView, SiltProcessor::maxGrainViews> views;
            most = juce::jmax (most, silt.readGrainViews (views));
            peak = juce::jmax (peak, buffer.getMagnitude (0, 128));
            energy += (double) buffer.getRMSLevel (0, 0, 128) * buffer.getRMSLevel (0, 0, 128);
        }
        std::cout << (silt.grainsOverLimit.load() > 0 ? "OVER  " : "ok    ") << library.factoryName (i) << "  " << most << " grains  over " << (long long) silt.grainsOverLimit.load()
 << "  peak " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB  rms " << juce::String (juce::Decibels::gainToDecibels ((float) std::sqrt (energy / blocks)), 1) << " dB\n";
    }
}

TEST_CASE ("Grain budget: dense, many-note settings stay within it, per-voice modulation or not")
{
    for (const bool perVoice : { false, true })
    {
        juce::ScopedJuceInitialiser_GUI juce;
        SiltProcessor silt;
        silt.enableAllBuses();
        silt.prepareToPlay (48000.0, 256);
        for (int i = 0; i < 300 && silt.getOverview().minimum.empty(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto set = [&] (const char* id, float value)
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter (id));
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        set ("rate", 100.0f);
        set ("size", 500.0f);
        if (perVoice)
        {
            set ("m1Voice", 1.0f);
            silt.setRouteAmount (0, silt.modulationTarget ("spray"), 0.3f);
        }
        juce::AudioBuffer<float> buffer (juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels()), 256);
        juce::MidiBuffer chord;
        for (int n = 0; n < 16; ++n)
            chord.addEvent (juce::MidiMessage::noteOn (1, 40 + n * 2, (juce::uint8) 100), 0);
        int most = 0;
        for (int b = 0; b < 400; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi = chord;
            silt.processBlock (buffer, midi);
            most = std::max (most, silt.activeGrains.load());
        }
        INFO ("per voice " << perVoice);
        CHECK (most <= silt::Engine::grainBudget48k + 16);   // each voice may be one over as the share changes
        CHECK (most > silt::Engine::grainBudget48k / 2);     // and the budget does get used
    }
}
