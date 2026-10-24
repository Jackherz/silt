// Plug-in level tests: the 8-knob controller mapping, driven through the real
// processor with MIDI CC messages.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../plugin/PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
struct Rig
{
    // JUCE's message manager, made before the processor and gone after it. A
    // global one is torn down at exit while the processor's timer thread may
    // still run, which crashes some runs.
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;

    Rig()
    {
        silt.enableAllBuses();
        silt.prepareToPlay (48000.0, 512);
    }

    // Sends one CC through processBlock, then applies it as the timer would.
    void cc (int number, int value)
    {
        juce::AudioBuffer<float> buffer (juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels()), 512);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, number, value), 0);
        silt.processBlock (buffer, midi);
        silt.applyControllerMoves();
    }

    float value (const char* id) { return silt.params.getParameter (id)->getValue(); }
    void set (const char* id, float normalised) { silt.params.getParameter (id)->setValueNotifyingHost (normalised); }
};
} // namespace

TEST_CASE ("Knob 1 moves the current page's first slot")
{
    Rig rig;
    rig.cc (70, 64);   // pitch sits at the middle, so the knob picks it up at once
    rig.cc (70, 127);
    CHECK_THAT (rig.value ("pitch"), WithinAbs (1.0, 1e-4));

    rig.silt.currentPage.store (2);   // PITCH page: knob 1 is RND PITCH
    rig.cc (70, 0);
    rig.cc (70, 40);
    CHECK_THAT (rig.value ("randPitch"), WithinAbs (40.0 / 127.0, 1e-3));
    CHECK_THAT (rig.value ("pitch"), WithinAbs (1.0, 1e-4));
}

TEST_CASE ("Pickup: a knob takes over only once it reaches the value")
{
    Rig rig;
    rig.set ("spray", 0.5f);
    rig.cc (73, 0);    // knob 4 = SPRAY, far below the value
    rig.cc (73, 30);
    CHECK_THAT (rig.value ("spray"), WithinAbs (0.5, 1e-3));   // not yet
    rig.cc (73, 70);   // crossed 0.5
    CHECK_THAT (rig.value ("spray"), WithinAbs (70.0 / 127.0, 1e-3));
    rig.cc (73, 20);   // and now it follows
    CHECK_THAT (rig.value ("spray"), WithinAbs (20.0 / 127.0, 1e-3));
}

TEST_CASE ("Page buttons step pages on each press, wrapping")
{
    Rig rig;
    rig.cc (79, 127);
    rig.cc (79, 0);
    CHECK (rig.silt.currentPage.load() == 1);
    rig.cc (79, 127);
    rig.cc (79, 127);   // held, not pressed again
    CHECK (rig.silt.currentPage.load() == 2);
    rig.cc (79, 0);
    rig.cc (78, 127);
    CHECK (rig.silt.currentPage.load() == 1);
    rig.cc (78, 0);
    rig.cc (78, 127);
    rig.cc (78, 0);
    rig.cc (78, 127);
    CHECK (rig.silt.currentPage.load() == 3);   // wrapped from GRAIN back to PLAY
}

TEST_CASE ("Slots follow the modes: a knob moves whichever control is shown")
{
    Rig rig;
    rig.silt.currentPage.store (0);
    rig.set ("rateMode", 1.0f);   // GRAINS
    CHECK (rig.silt.slotParameter (0, 2) == "grains");
    CHECK (rig.silt.slotParameter (0, 1) == "grainSize");

    rig.silt.currentPage.store (3);
    rig.set ("keyMode", 1.0f);    // LIVE
    CHECK (rig.silt.slotParameter (3, 3) == "freeze");
    rig.cc (73, 0);
    rig.cc (73, 127);
    CHECK (rig.value ("freeze") > 0.5f);
}

TEST_CASE ("Learn takes knobs 1-8, then the page buttons")
{
    Rig rig;
    rig.silt.startLearn();
    for (int knob = 0; knob < 8; ++knob)
        for (int v : { 10, 20, 30 })   // a turned knob sends several messages
            rig.cc (20 + knob, v);
    rig.cc (40, 127);
    rig.cc (40, 0);    // release of the same button
    rig.cc (41, 127);

    CHECK (rig.silt.learnStep.load() == -1);
    for (int knob = 0; knob < 8; ++knob)
        CHECK (rig.silt.knobCCs[(size_t) knob].load() == 20 + knob);
    CHECK (rig.silt.previousPageCC.load() == 40);
    CHECK (rig.silt.nextPageCC.load() == 41);

    rig.cc (41, 0);
    rig.cc (41, 127);
    CHECK (rig.silt.currentPage.load() == 1);
}

TEST_CASE ("The mapping and page are saved with the set")
{
    juce::MemoryBlock state;
    {
        Rig rig;
        rig.silt.knobCCs[3].store (99);
        rig.silt.nextPageCC.store (55);
        rig.silt.currentPage.store (2);
        rig.silt.getStateInformation (state);
    }
    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.silt.knobCCs[3].load() == 99);
    CHECK (restored.silt.nextPageCC.load() == 55);
    CHECK (restored.silt.currentPage.load() == 2);
}
