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
    CHECK (rig.silt.currentPage.load() == SiltProcessor::numPages - 1);   // wrapped from GRAIN back to the last page
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

// --- Modulation -----------------------------------------------------------------------

namespace
{
void runBlocks (Rig& rig, int blocks, juce::MidiBuffer midi = {})
{
    juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), 512);
    for (int i = 0; i < blocks; ++i)
    {
        buffer.clear();
        rig.silt.processBlock (buffer, midi);
        midi.clear();
    }
}
} // namespace

TEST_CASE ("Modulation targets the continuous and stepped parameters")
{
    Rig rig;
    CHECK (rig.silt.modulationTarget ("filterCutoff") >= 0);
    CHECK (rig.silt.modulationTarget ("pitch") >= 0);
    CHECK (rig.silt.modulationTarget ("spaceReverb") >= 0);
    CHECK (rig.silt.modulationTarget ("keyMode") < 0);
    CHECK (rig.silt.modulationTarget ("patternSeed") < 0);
    CHECK (rig.silt.modulationTarget ("m1Rate") < 0);
    // Switch-like parameters (choices, bools) aren't targets; stepped ones are.
    for (const auto* id : { "scale", "key", "endMode", "resNotes", "liveRun", "spaceMode", "snap", "freeze", "resFollow", "spaceFreeze", "sizeDiv" })
    {
        INFO (id);
        CHECK (rig.silt.modulationTarget (id) < 0);
    }
    CHECK (rig.silt.modulationTarget ("heads") >= 0);
    CHECK (rig.silt.isSteppedTarget (rig.silt.modulationTarget ("heads")));
    CHECK_FALSE (rig.silt.isSteppedTarget (rig.silt.modulationTarget ("pitch")));
    CHECK (rig.silt.numTargets() < SiltProcessor::maxTargets);
}

TEST_CASE ("An armed controller knob on a menu slot creates no route (review F1)")
{
    Rig rig;
    rig.silt.currentPage.store (2);   // PITCH: knob 2 is SCALE, a menu
    rig.silt.armedMod.store (0);
    rig.cc (71, 127);
    CHECK (rig.silt.allRoutes().empty());
}

TEST_CASE ("Routes list every non-zero pair; clearing a modulator touches only it")
{
    Rig rig;
    const int cutoff = rig.silt.modulationTarget ("filterCutoff"), pitch = rig.silt.modulationTarget ("pitch");
    rig.silt.setRouteAmount (0, cutoff, 0.4f);
    rig.silt.setRouteAmount (2, cutoff, -0.2f);
    rig.silt.setRouteAmount (2, pitch, 0.1f);
    const auto routes = rig.silt.allRoutes();
    REQUIRE (routes.size() == 3);
    int listed = 0;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
        for (int t = 0; t < rig.silt.numTargets(); ++t)
            if (rig.silt.routeAmount (m, t) != 0.0f)
            {
                ++listed;
                const bool found = std::any_of (routes.begin(), routes.end(), [=] (const auto& r) { return r.modulator == m && r.target == t; });
                CHECK (found);
            }
    CHECK (listed == 3);
    CHECK (rig.silt.routeCount (2) == 2);

    rig.silt.clearRoutesForModulator (2);
    CHECK (rig.silt.routeCount (2) == 0);
    CHECK (rig.silt.routeAmount (0, cutoff) == 0.4f);
    CHECK (rig.silt.depthText (0, cutoff).startsWith (juce::String::charToString ((juce::juce_wchar) 0x00b1) + "40%"));
}

TEST_CASE ("A muted modulator contributes nothing, keeps its routes, and saves")
{
    juce::MemoryBlock state;
    {
        Rig rig;
        rig.set ("filterCutoff", 0.5f);
        rig.set ("m1Shape", 1.0f);   // square: always ±1, so any contribution shows
        const int t = rig.silt.modulationTarget ("filterCutoff");
        rig.silt.setRouteAmount (0, t, 0.3f);
        rig.set ("m1Enable", 0.0f);
        for (int i = 0; i < 20; ++i)
        {
            runBlocks (rig, 1);
            CHECK_THAT (rig.silt.modulatedValue01 (t), WithinAbs (0.5, 1.0e-6));
        }
        CHECK (rig.silt.routeAmount (0, t) == 0.3f);
        rig.silt.getStateInformation (state);
    }
    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_FALSE (restored.silt.isModulatorEnabled (0));
    CHECK (restored.silt.routeAmount (0, restored.silt.modulationTarget ("filterCutoff")) == 0.3f);
}

TEST_CASE ("A repeated note-on doesn't leave the ENVELOPE stuck held (review F13)")
{
    Rig rig;
    rig.set ("m1Type", rig.silt.params.getParameter ("m1Type")->convertTo0to1 (2.0f));
    rig.set ("filterCutoff", 0.0f);
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (0, t, 1.0f);

    juce::MidiBuffer twice;
    twice.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    twice.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 100);
    runBlocks (rig, 30, twice);
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    runBlocks (rig, 100, off);
    CHECK (rig.silt.modulatedValue01 (t) == 0.0f);
}

TEST_CASE ("A WAVE route swings its target round the knob's value, clamped to the range")
{
    Rig rig;
    rig.set ("filterCutoff", 0.5f);
    rig.set ("m1Rate", rig.silt.params.getParameter ("m1Rate")->convertTo0to1 (4.0f));   // 4 Hz sine
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (0, t, 0.3f);

    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < 100; ++i)   // ~1 s
    {
        runBlocks (rig, 1);
        lo = std::min (lo, rig.silt.modulatedValue01 (t));
        hi = std::max (hi, rig.silt.modulatedValue01 (t));
    }
    CHECK_THAT (lo, WithinAbs (0.2, 0.02));
    CHECK_THAT (hi, WithinAbs (0.8, 0.02));
    CHECK_THAT (rig.value ("filterCutoff"), WithinAbs (0.5, 1.0e-6));   // the knob itself doesn't move

    rig.silt.setRouteAmount (0, t, 1.0f);
    lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        runBlocks (rig, 1);
        lo = std::min (lo, rig.silt.modulatedValue01 (t));
        hi = std::max (hi, rig.silt.modulatedValue01 (t));
    }
    CHECK (lo == 0.0f);
    CHECK (hi == 1.0f);

    rig.silt.clearRoutesOnTarget (t);
    runBlocks (rig, 1);
    CHECK_THAT (rig.silt.modulatedValue01 (t), WithinAbs (0.5, 1.0e-6));
}

TEST_CASE ("An ENVELOPE route opens with a note and closes after it")
{
    Rig rig;
    rig.set ("m2Type", rig.silt.params.getParameter ("m2Type")->convertTo0to1 (2.0f));   // envelope
    rig.set ("filterCutoff", 0.0f);
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (1, t, 1.0f);

    runBlocks (rig, 4);
    CHECK (rig.silt.modulatedValue01 (t) == 0.0f);

    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    runBlocks (rig, 30, on);   // past a 10 ms attack and 300 ms decay: at sustain 50%
    CHECK_THAT (rig.silt.modulatedValue01 (t), WithinAbs (0.5, 0.02));

    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    runBlocks (rig, 100, off);   // 500 ms release
    CHECK (rig.silt.modulatedValue01 (t) == 0.0f);
}

TEST_CASE ("With ASSIGN on, a controller knob sets the amount on its slot's parameter")
{
    Rig rig;
    rig.silt.currentPage.store (5);   // FILTER: knob 2 is CUTOFF
    rig.silt.armedMod.store (2);
    const int t = rig.silt.modulationTarget ("filterCutoff");
    const float before = rig.value ("filterCutoff");
    rig.cc (71, 127);
    CHECK_THAT (rig.silt.routeAmount (2, t), WithinAbs (1.0, 1.0e-6));
    rig.cc (71, 0);
    CHECK_THAT (rig.silt.routeAmount (2, t), WithinAbs (-64.0 / 63.0 < -1.0 ? -1.0 : -64.0 / 63.0, 1.0e-6));
    rig.cc (71, 64);
    CHECK (rig.silt.routeAmount (2, t) == 0.0f);
    CHECK (rig.value ("filterCutoff") == before);   // the value itself is left alone

    // On the MOD page, ASSIGN's button toggles it and knob 1 picks the modulator.
    rig.silt.armedMod.store (-1);
    rig.silt.currentPage.store (SiltProcessor::modPage);
    rig.cc (70, 127);
    CHECK (rig.silt.modShown.load() == 3);
    rig.cc (77, 127);
    CHECK (rig.silt.armedMod.load() == 3);
    rig.cc (77, 0);
    rig.cc (77, 127);
    CHECK (rig.silt.armedMod.load() == -1);
}

TEST_CASE ("Routes and the modulator shown are saved with the set")
{
    juce::MemoryBlock state;
    {
        Rig rig;
        rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("pitch"), 0.25f);
        rig.silt.setRouteAmount (3, rig.silt.modulationTarget ("spaceReverb"), -0.5f);
        rig.silt.modShown.store (2);
        rig.silt.getStateInformation (state);
    }
    Rig restored;
    restored.silt.setRouteAmount (1, restored.silt.modulationTarget ("filterReso"), 0.9f);   // must be cleared
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_THAT (restored.silt.routeAmount (0, restored.silt.modulationTarget ("pitch")), WithinAbs (0.25, 1.0e-4));
    CHECK_THAT (restored.silt.routeAmount (3, restored.silt.modulationTarget ("spaceReverb")), WithinAbs (-0.5, 1.0e-4));
    CHECK (restored.silt.routeAmount (1, restored.silt.modulationTarget ("filterReso")) == 0.0f);
    CHECK (restored.silt.modShown.load() == 2);
}
