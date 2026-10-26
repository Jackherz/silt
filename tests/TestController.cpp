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
    rig.set ("keyMode", rig.silt.params.getParameter ("keyMode")->convertTo0to1 (1.0f));    // LIVE
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
    CHECK (rig.silt.modulationTarget ("m1Rate") >= 0);    // cross-modulation
    CHECK (rig.silt.modulationTarget ("m1Macro") < 0);   // a plain knob
    CHECK (rig.silt.modulationTarget ("m1Type") < 0);
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
    CHECK (rig.silt.depthText (0, cutoff).endsWith ("(+40%)"));
    rig.silt.setRouteAmount (0, cutoff, -0.4f);
    CHECK (rig.silt.depthText (0, cutoff).endsWith ("(-40%)"));
    rig.silt.setRouteAmount (0, cutoff, 0.4f);
    CHECK (rig.silt.depthText (0, cutoff).startsWith (rig.silt.depthSpan (0, cutoff)));
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

// --- Per-voice modulation ------------------------------------------------------------

namespace
{
// Loads a two-second sine as the sample, through the real loader.
void loadSine (Rig& rig)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("silt-test", ".wav");
    {
        juce::AudioBuffer<float> sine (1, 96000);
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample (0, i, 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / 48000.0f));
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
        writer->writeFromAudioSampleBuffer (sine, 0, sine.getNumSamples());
    }
    rig.silt.loadFile (file);
    // The factory sample a new instance opens with may land first: wait for this one.
    const auto loaded = [&] { return rig.silt.getOverview().name == file.getFileNameWithoutExtension(); };
    for (int i = 0; i < 300 && ! loaded(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    file.deleteFile();
    REQUIRE (loaded());
    runBlocks (rig, 2);   // the engine picks the buffer up
}

int soundingVoices (Rig& rig)
{
    int n = 0;
    for (auto& shown : rig.silt.voiceShown)
        n += shown.load() ? 1 : 0;
    return n;
}
} // namespace

TEST_CASE ("Per voice off: no voice values; on: each note runs its own ENVELOPE")
{
    Rig rig;
    loadSine (rig);
    rig.set ("m1Type", rig.silt.params.getParameter ("m1Type")->convertTo0to1 (2.0f));   // envelope
    rig.set ("m1Attack", rig.silt.params.getParameter ("m1Attack")->convertTo0to1 (200.0f));
    const int t = rig.silt.modulationTarget ("spray");
    rig.silt.setRouteAmount (0, t, 1.0f);

    juce::MidiBuffer first;
    first.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    runBlocks (rig, 10, first);
    CHECK (soundingVoices (rig) == 0);   // shared modulation: no per-voice values

    rig.set ("m1Voice", 1.0f);
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    runBlocks (rig, 100, off);   // let it finish

    juce::MidiBuffer again;
    again.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    runBlocks (rig, 34, again);   // 363 ms: past the 200 ms attack, decaying
    juce::MidiBuffer second;
    second.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
    runBlocks (rig, 4, second);   // 43 ms into the second note's 200 ms attack
    REQUIRE (soundingVoices (rig) == 2);
    std::vector<float> levels;
    for (int v = 0; v < silt::Engine::numVoices; ++v)
        if (rig.silt.voiceShown[(size_t) v].load())
            levels.push_back (rig.silt.voiceModulated01[(size_t) v][(size_t) t].load());
    std::sort (levels.begin(), levels.end());
    INFO ("voice levels " << levels[0] << ", " << levels[1]);
    CHECK (levels[0] < 0.4f);   // the new note, early in its attack
    CHECK (levels[1] > 0.6f);   // the earlier note, decaying from its peak
}

TEST_CASE ("Per-voice RANDOM decorrelates voices started together")
{
    Rig rig;
    loadSine (rig);
    rig.set ("m2Type", rig.silt.params.getParameter ("m2Type")->convertTo0to1 (1.0f));   // random
    rig.set ("m2Voice", 1.0f);
    const int t = rig.silt.modulationTarget ("pitch");
    rig.silt.setRouteAmount (1, t, 0.5f);

    juce::MidiBuffer chord;
    for (const int note : { 60, 64, 67 })
        chord.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    runBlocks (rig, 100, chord);   // about a second
    std::vector<float> values;
    for (int v = 0; v < silt::Engine::numVoices; ++v)
        if (rig.silt.voiceShown[(size_t) v].load())
            values.push_back (rig.silt.voiceModulated01[(size_t) v][(size_t) t].load());
    REQUIRE (values.size() == 3);
    CHECK (values[0] != values[1]);
    CHECK (values[1] != values[2]);
}

TEST_CASE ("Per-voice routes don't reach effect parameters, which follow the shared modulator")
{
    Rig rig;
    rig.set ("m1Voice", 1.0f);
    CHECK_FALSE (rig.silt.isVoiceTarget (rig.silt.modulationTarget ("filterCutoff")));
    CHECK (rig.silt.isVoiceTarget (rig.silt.modulationTarget ("spray")));
}

TEST_CASE ("A MACRO modulator moves its routes with its knob")
{
    Rig rig;
    rig.set ("m3Type", rig.silt.params.getParameter ("m3Type")->convertTo0to1 (5.0f));   // macro
    rig.set ("filterCutoff", 0.2f);
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (2, t, 0.5f);
    runBlocks (rig, 2);
    CHECK_THAT (rig.silt.modulatedValue01 (t), WithinAbs (0.2, 1.0e-6));
    rig.set ("m3Macro", 1.0f);   // knob fully up
    runBlocks (rig, 2);
    CHECK_THAT (rig.silt.modulatedValue01 (t), WithinAbs (0.7, 1.0e-5));
    // The MOD page's knob 4 is the macro when the type is MACRO.
    rig.silt.modShown.store (2);
    CHECK (rig.silt.slotParameter (SiltProcessor::modPage, 3) == "m3Macro");
}

TEST_CASE ("Cross-modulation: one modulator speeds up another, and self-modulation stays sane")
{
    Rig rig;
    const auto crossings = [&rig] (int blocks)
    {
        int n = 0;
        float last = rig.silt.modOutputs[0].load();
        for (int i = 0; i < blocks; ++i)
        {
            runBlocks (rig, 1);
            const float now = rig.silt.modOutputs[0].load();
            n += (last < 0.0f) != (now < 0.0f);
            last = now;
        }
        return n;
    };
    // M1: a 1 Hz sine. 2 s: about 4 sign changes.
    const int alone = crossings (188);
    INFO ("alone " << alone);
    CHECK (alone <= 6);

    // M2 as a MACRO, fully up, routed +40% onto M1's rate: much faster.
    rig.set ("m2Type", rig.silt.params.getParameter ("m2Type")->convertTo0to1 (5.0f));
    rig.set ("m2Macro", 1.0f);
    rig.silt.setRouteAmount (1, rig.silt.modulationTarget ("m1Rate"), 0.4f);
    const int faster = crossings (188);
    INFO ("faster " << faster);
    CHECK (faster > 3 * std::max (1, alone));

    // M1 on its own rate: still finite and in range.
    rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("m1Rate"), 1.0f);
    for (int i = 0; i < 200; ++i)
    {
        runBlocks (rig, 1);
        const float v = rig.silt.modOutputs[0].load();
        REQUIRE (std::isfinite (v));
        REQUIRE (std::abs (v) <= 1.0f);
    }
}

TEST_CASE ("Route edits undo and redo; a gesture is one step")
{
    Rig rig;
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.beginRouteGesture();
    for (const float a : { 0.1f, 0.2f, 0.3f })   // a drag
        rig.silt.editRoutes ([&] { rig.silt.setRouteAmount (0, t, a); });
    rig.silt.beginRouteGesture();
    rig.silt.editRoutes ([&] { rig.silt.clearRoutesForModulator (0); });
    CHECK (rig.silt.routeAmount (0, t) == 0.0f);

    REQUIRE (rig.silt.undoRoutes());   // the clear
    CHECK (rig.silt.routeAmount (0, t) == 0.3f);
    REQUIRE (rig.silt.undoRoutes());   // the whole drag
    CHECK (rig.silt.routeAmount (0, t) == 0.0f);
    CHECK_FALSE (rig.silt.canUndoRoutes());
    REQUIRE (rig.silt.redoRoutes());
    CHECK (rig.silt.routeAmount (0, t) == 0.3f);
}

// --- Polarity and undo of values -----------------------------------------------------

TEST_CASE ("A route's polarity: unipolar goes one way from a bipolar source, bipolar both ways from a unipolar one")
{
    Rig rig;
    rig.set ("filterCutoff", 0.5f);
    rig.set ("m1Shape", 1.0f);   // square: −1 or +1
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (0, t, 0.3f);
    CHECK (rig.silt.routeIsBipolar (0, t));
    rig.silt.setRoutePolarity (0, t, SiltProcessor::unipolar);
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        runBlocks (rig, 1);
        lo = std::min (lo, rig.silt.modulatedValue01 (t));
        hi = std::max (hi, rig.silt.modulatedValue01 (t));
    }
    CHECK_THAT (lo, WithinAbs (0.5, 1.0e-4));
    CHECK_THAT (hi, WithinAbs (0.8, 1.0e-4));

    // MACRO is unipolar; set the route bipolar and the knob's middle is "no change".
    rig.set ("m2Type", rig.silt.params.getParameter ("m2Type")->convertTo0to1 (5.0f));
    rig.set ("m2Macro", 0.5f);
    const int reso = rig.silt.modulationTarget ("filterReso");
    rig.silt.setRouteAmount (1, reso, 0.4f);
    rig.silt.setRoutePolarity (1, reso, SiltProcessor::bipolar);
    runBlocks (rig, 2);
    CHECK_THAT (rig.silt.modulatedValue01 (reso), WithinAbs (rig.value ("filterReso"), 1.0e-5));
    rig.set ("m2Macro", 0.0f);
    runBlocks (rig, 2);
    CHECK_THAT (rig.silt.modulatedValue01 (reso), WithinAbs (juce::jmax (0.0f, rig.value ("filterReso") - 0.4f), 1.0e-5));

    // Saved with the set; removing the route forgets it.
    juce::MemoryBlock state;
    rig.silt.getStateInformation (state);
    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.silt.routePolarityOf (0, t) == SiltProcessor::unipolar);
    CHECK (restored.silt.routePolarityOf (1, reso) == SiltProcessor::bipolar);
    restored.silt.setRouteAmount (0, t, 0.0f);
    restored.silt.beginRouteGesture();   // removed for good once the next edit starts
    CHECK (restored.silt.routePolarityOf (0, t) == SiltProcessor::natural);
}

TEST_CASE ("Knob, menu and button changes undo; automation and preset loads don't record")
{
    Rig rig;
    auto* spray = rig.silt.params.getParameter ("spray");
    auto* reverse = rig.silt.params.getParameter ("reverse");
    const auto gesture = [] (juce::AudioProcessorParameter* p, float v)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    };

    gesture (spray, 0.6f);
    reverse->setValueNotifyingHost (0.9f);   // automation: no gesture, not recorded
    gesture (reverse, 0.2f);
    CHECK (rig.silt.undoDescription() == "REVERSE");

    REQUIRE (rig.silt.undoRoutes());
    CHECK (reverse->getValue() == 0.9f);
    REQUIRE (rig.silt.undoRoutes());
    CHECK (spray->getValue() == 0.0f);
    CHECK_FALSE (rig.silt.canUndoRoutes());
    REQUIRE (rig.silt.redoRoutes());
    CHECK (spray->getValue() == 0.6f);

    // A controller turn: many quick gestures on one parameter make one step.
    rig.silt.clearUndoHistory();
    for (const float v : { 0.1f, 0.2f, 0.3f, 0.4f })
        gesture (spray, v);
    REQUIRE (rig.silt.undoRoutes());
    CHECK (spray->getValue() == 0.6f);
    CHECK_FALSE (rig.silt.canUndoRoutes());

    // Values and routes share one history.
    rig.silt.beginRouteGesture();
    rig.silt.editRoutes ([&] { rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("pitch"), 0.2f); });
    gesture (spray, 0.9f);
    REQUIRE (rig.silt.undoRoutes());
    CHECK (spray->getValue() == 0.6f);
    REQUIRE (rig.silt.undoRoutes());
    CHECK (rig.silt.allRoutes().empty());

    // A preset load sets every value without recording and starts the history over.
    rig.silt.applyPreset (*rig.silt.capturePreset ("x"), "x");
    CHECK_FALSE (rig.silt.canUndoRoutes());
}

TEST_CASE ("Sample gain scales the grains; normalise peaks the sample at -1 dB")
{
    const auto render = [] (float gainDb)
    {
        Rig rig;
        loadSine (rig);
        rig.set ("sampleGain", rig.silt.params.getParameter ("sampleGain")->convertTo0to1 (gainDb));
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), 512);
        double sum = 0.0;
        for (int i = 0; i < 60; ++i)
        {
            buffer.clear();
            rig.silt.processBlock (buffer, on);
            on.clear();
            if (i >= 20)
                sum += buffer.getRMSLevel (0, 0, 512);
        }
        return std::make_pair (sum, rig.silt.normalisingGainDb());
    };
    const auto [flat, normalise] = render (0.0f);
    const auto [softer, unused] = render (-6.0206f);   // ×0.5 (turning up would meet the output's safety clip)
    CHECK_THAT (softer / flat, WithinAbs (0.5, 0.005));
    CHECK_THAT (normalise, WithinAbs (-1.0 - juce::Decibels::gainToDecibels (0.5), 0.05));   // the test sine peaks at 0.5

    Rig rig;
    CHECK (rig.silt.modulationTarget ("sampleGain") < 0);
    CHECK_FALSE (rig.silt.capturePreset ("x")->getChildByName ("PARAMS")->hasAttribute ("sampleGain"));
}

TEST_CASE ("WAVE RATE runs through zero: STOP in the middle, backwards below")
{
    Rig rig;
    auto* rate = dynamic_cast<juce::RangedAudioParameter*> (rig.silt.params.getParameter ("m1Rate"));
    REQUIRE (rate != nullptr);
    CHECK_THAT (rate->convertFrom0to1 (0.5f), WithinAbs (0.0, 1.0e-6));
    CHECK (rate->getText (rate->convertTo0to1 (0.0f), 0) == "STOP");
    CHECK (rate->getText (rate->convertTo0to1 (-2.0f), 0) == "-2.00 Hz");
    CHECK (rate->getText (rate->convertTo0to1 (1.0f), 0) == "1.00 Hz");
    for (const float hz : { -50.0f, -3.0f, -0.2f, 0.2f, 1.0f, 7.5f, 50.0f })
        CHECK_THAT (rate->convertFrom0to1 (rate->convertTo0to1 (hz)), WithinAbs (hz, 1.0e-3 * std::abs (hz)));
    // Fine near zero: the middle 20% of the knob stays within ±0.4 Hz; half way out, 6 Hz.
    CHECK (std::abs (rate->convertFrom0to1 (0.6f)) <= 0.41f);
    CHECK (std::abs (rate->convertFrom0to1 (0.4f)) <= 0.41f);
    CHECK_THAT (rate->convertFrom0to1 (0.75f), WithinAbs (6.25, 0.01));

    // REV sits with SYNC on the MOD page for a synced WAVE only.
    rig.set ("m1Sync", 1.0f);
    const auto context = rig.silt.layoutContext();
    bool revShown = false;
    for (const auto& e : silt::layout::entries (SiltProcessor::isInstrument))
        if (e.id == "m1Rev")
            revShown = e.isShown (context);
    CHECK (revShown);
}

namespace
{
bool waitFor (const std::function<bool()>& done)
{
    for (int i = 0; i < 300 && ! done(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    return done();
}

float rootNote (Rig& rig)
{
    auto* root = rig.silt.params.getParameter ("root");
    return root->convertFrom0to1 (root->getValue());
}
} // namespace

TEST_CASE ("A new instance opens with the first factory sample, in its key; a state keeps it by number")
{
    Rig rig;
    REQUIRE (waitFor ([&] { return rig.silt.getFactorySample() == 0; }));
    CHECK (rig.silt.getOverview().name == SiltProcessor::factorySampleName (0));
    CHECK (rig.silt.getOverview().seconds > 5.0);
    CHECK (rootNote (rig) == 64.0f);   // E
    CHECK (rig.silt.capturePreset ("x")->getChildByName ("PARAMS")->hasAttribute ("root") == false);

    juce::MemoryBlock state;
    rig.silt.getStateInformation (state);
    CHECK (state.getSize() < 100000);   // no audio: it's built in

    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.silt.getFactorySample() == 0);
    CHECK (restored.silt.getOverview().seconds > 5.0);
}

TEST_CASE ("LOAD's factory samples set ROOT; a file sets it back to C; a restored state isn't replaced")
{
    Rig rig;
    rig.silt.loadFactorySample (2);
    REQUIRE (waitFor ([&] { return rig.silt.getFactorySample() == 2; }));
    CHECK (rig.silt.getOverview().name == "Rain Bells");
    CHECK (rootNote (rig) == 63.0f);

    loadSine (rig);
    CHECK (rig.silt.getFactorySample() == -1);
    CHECK (rootNote (rig) == 60.0f);

    // A state with a user sample, restored before the default sample is ready, stays.
    juce::MemoryBlock state;
    rig.silt.getStateInformation (state);
    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    CHECK (restored.silt.getFactorySample() == -1);
    CHECK_THAT (restored.silt.getOverview().seconds, WithinAbs (2.0, 0.01));
}

TEST_CASE ("CHAIN: 24 distinct orders, the first the classic one; a knob on the CHAIN page moves it")
{
    CHECK (SiltProcessor::chainOrder (0) == std::array<int, 4> { 0, 1, 2, 3 });
    juce::StringArray names;
    for (int i = 0; i < SiltProcessor::numChainOrders; ++i)
    {
        CHECK (SiltProcessor::chainOrderIndex (SiltProcessor::chainOrder (i)) == i);
        names.addIfNotAlreadyThere (SiltProcessor::chainOrderName (i));
    }
    CHECK (names.size() == SiltProcessor::numChainOrders);
    CHECK (SiltProcessor::chainOrderName (0) == "R>F>C>S");

    Rig rig;
    CHECK (rig.silt.slotParameter (silt::layout::chain, 0) == "chainOrder");
    CHECK (rig.silt.modulationTarget ("chainOrder") < 0);
    CHECK (rig.silt.capturePreset ("x")->getChildByName ("PARAMS")->hasAttribute ("chainOrder"));
}

TEST_CASE ("CHAIN order changes the sound: noise after a low-pass is brighter than noise before it")
{
    // High-frequency energy (first differences) of the output with a dark
    // low-pass and COLOR noise, in a given order.
    const auto brightness = [] (const std::array<int, 4>& order)
    {
        Rig rig;
        loadSine (rig);
        const auto setPlain = [&] (const char* id, float v)
        {
            auto* p = rig.silt.params.getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setPlain ("filterMode", 2.0f);
        setPlain ("filterCutoff", 200.0f);
        setPlain ("colorNoise", 100.0f);
        setPlain ("chainOrder", (float) SiltProcessor::chainOrderIndex (order));
        rig.silt.prepareToPlay (48000.0, 512);   // starts in the order, without the fade

        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), 512);
        double energy = 0.0;
        for (int b = 0; b < 60; ++b)
        {
            buffer.clear();
            rig.silt.processBlock (buffer, on);
            on.clear();
            if (b >= 20)
                for (int i = 1; i < 512; ++i)
                    energy += std::pow (buffer.getSample (0, i) - buffer.getSample (0, i - 1), 2.0f);
        }
        return energy;
    };
    const double noiseLast = brightness ({ 0, 1, 2, 3 });    // filter, then COLOR's noise
    const double noiseFirst = brightness ({ 0, 2, 1, 3 });   // COLOR's noise, then the filter
    CHECK (noiseLast > noiseFirst * 4.0);
}

TEST_CASE ("The scope sees the sound going into the tapped module and out of it")
{
    Rig rig;
    loadSine (rig);
    rig.set ("colorBits", rig.silt.params.getParameter ("colorBits")->convertTo0to1 (3.0f));
    rig.silt.scopeModule.store (SiltProcessor::moduleColor);

    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), 512);
    for (int b = 0; b < 20; ++b)
    {
        buffer.clear();
        rig.silt.processBlock (buffer, on);
        on.clear();
    }
    std::vector<float> in (2048), out (2048);
    CHECK (rig.silt.readScope (in.data(), out.data(), 2048) >= 2048u);

    float peak = 0.0f, difference = 0.0f;
    for (size_t i = 0; i < in.size(); ++i)
    {
        peak = std::max (peak, std::abs (in[i]));
        difference = std::max (difference, std::abs (out[i] - in[i]));
    }
    CHECK (peak > 0.01f);
    CHECK (difference > 0.005f);   // 3 bits: the output is visibly steppier
}

TEST_CASE ("A depth dragged through 0 keeps its route's polarity; a removed route starts natural next time")
{
    Rig rig;
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.beginRouteGesture();
    rig.silt.editRoutes ([&] { rig.silt.setRouteAmount (0, t, 0.4f); });
    rig.silt.setRoutePolarity (0, t, SiltProcessor::unipolar);   // MOD 1 is a WAVE: naturally bipolar
    REQUIRE_FALSE (rig.silt.routeIsBipolar (0, t));

    // One drag, from +40% through 0 to −30%.
    rig.silt.beginRouteGesture();
    for (const float amount : { 0.2f, 0.0f, -0.3f })
        rig.silt.editRoutes ([&] { rig.silt.setRouteAmount (0, t, amount); });
    CHECK_THAT (rig.silt.routeAmount (0, t), WithinAbs (-0.3, 1e-6));
    CHECK_FALSE (rig.silt.routeIsBipolar (0, t));

    // Left at 0, it's gone; made again later, it starts natural.
    rig.silt.beginRouteGesture();
    rig.silt.editRoutes ([&] { rig.silt.setRouteAmount (0, t, 0.0f); });
    rig.silt.beginRouteGesture();
    rig.silt.editRoutes ([&] { rig.silt.setRouteAmount (0, t, 0.5f); });
    CHECK (rig.silt.routeIsBipolar (0, t));

    // Undo goes back through the drag with its polarity.
    rig.silt.undoRoutes();
    rig.silt.undoRoutes();
    CHECK_THAT (rig.silt.routeAmount (0, t), WithinAbs (-0.3, 1e-6));
    CHECK_FALSE (rig.silt.routeIsBipolar (0, t));
}

TEST_CASE ("RANDOM moves a page's slot controls by the amount, skips the excluded ones and is one undo step")
{
    Rig rig;
    const int page = silt::layout::grain;
    std::map<juce::String, float> before;
    for (const auto* e : rig.silt.randomisable (page))
        before[e->id] = rig.value (e->id.toRawUTF8());
    REQUIRE (before.count ("spray") == 1);
    CHECK (before.count ("rateMode") == 0);   // the mode under a slot stays

    rig.silt.setRandomExcluded ("spray", true);
    rig.silt.randomAmount.store (0.1f);
    const int changed = rig.silt.randomisePage (page);
    CHECK (changed > 0);
    CHECK (rig.value ("spray") == before["spray"]);
    for (const auto& [id, value] : before)
        CHECK (std::abs (rig.value (id.toRawUTF8()) - value) <= 0.1f + 1.0e-4f);   // a knob goes at most 10% of the way

    // One step back restores them all.
    rig.silt.undoRoutes();
    for (const auto& [id, value] : before)
        CHECK_THAT (rig.value (id.toRawUTF8()), WithinAbs (value, 1.0e-6));

    // At 100% the knobs land anywhere; menus and switches too.
    rig.silt.randomAmount.store (1.0f);
    float moved = 0.0f;
    for (int i = 0; i < 5; ++i)
    {
        rig.silt.randomisePage (page);
        moved = std::max (moved, std::abs (rig.value ("pitch") - before["pitch"]));
    }
    CHECK (moved > 0.2f);

    // Exclusions and the amount are kept with the session.
    juce::MemoryBlock state;
    rig.silt.getStateInformation (state);
    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.silt.isRandomExcluded ("spray"));
    CHECK (restored.silt.isRandomExcluded ("m1Type"));
    CHECK_THAT (restored.silt.randomAmount.load(), WithinAbs (1.0, 1e-6));
}

TEST_CASE ("RANDOM on the MOD page leaves the modulator types alone by default")
{
    Rig rig;
    rig.silt.randomAmount.store (1.0f);
    for (int i = 0; i < 10; ++i)
        rig.silt.randomisePage (silt::layout::mod);
    CHECK (rig.value ("m1Type") == 0.0f);
}

TEST_CASE ("The controller's page buttons step through both halves of RESONATOR")
{
    Rig rig;
    rig.silt.currentPage.store (silt::layout::play);
    const auto next = [&] { rig.cc (rig.silt.nextPageCC.load(), 127); rig.cc (rig.silt.nextPageCC.load(), 0); };
    const auto previous = [&] { rig.cc (rig.silt.previousPageCC.load(), 127); rig.cc (rig.silt.previousPageCC.load(), 0); };
    next();
    CHECK (rig.silt.currentPage.load() == silt::layout::resonator);
    CHECK (rig.silt.resonatorHalf.load() == 0);
    CHECK (rig.silt.slotParameter (silt::layout::resonator, 0) == "resTune");
    next();
    CHECK (rig.silt.currentPage.load() == silt::layout::resonator);
    CHECK (rig.silt.resonatorHalf.load() == 1);
    CHECK (rig.silt.slotParameter (silt::layout::resonator, 0) == "resReso");
    next();
    CHECK (rig.silt.currentPage.load() == silt::layout::filter);
    previous();
    CHECK (rig.silt.currentPage.load() == silt::layout::resonator);
    CHECK (rig.silt.resonatorHalf.load() == 1);   // back in: its second half
    previous();
    CHECK (rig.silt.resonatorHalf.load() == 0);
}

TEST_CASE ("SPACE's live trace restarts at each note and shows what SPACE adds")
{
    Rig rig;
    loadSine (rig);
    rig.set ("spaceReverb", 0.5f);
    rig.set ("spaceDecay", rig.silt.params.getParameter ("spaceDecay")->convertTo0to1 (2.0f));
    runBlocks (rig, 20);
    REQUIRE (rig.silt.spaceTraceCount.load() > 100u);   // counting with no note yet

    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), 512);
    buffer.clear();
    rig.silt.processBlock (buffer, on);
    CHECK (rig.silt.spaceTraceCount.load() == 8u);   // 512 samples since the note: 8 steps of 64
    juce::MidiBuffer none;
    float loudest = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        buffer.clear();
        rig.silt.processBlock (buffer, none);
    }
    for (unsigned i = 0; i < rig.silt.spaceTraceCount.load(); ++i)
        loudest = std::max (loudest, rig.silt.spaceTraceAt (i));
    CHECK (loudest > 0.001f);
}

TEST_CASE ("Per-voice SPREAD sets a chord's waves apart; at 0 they move together")
{
    // The three voices' modulated pitch, a moment after a chord starts.
    const auto voices = [] (float spread)
    {
        Rig rig;
        loadSine (rig);
        rig.set ("m1Voice", 1.0f);   // MOD 1 is a 1 Hz sine
        rig.set ("m1Spread", spread);
        const int t = rig.silt.modulationTarget ("pitch");
        rig.silt.setRouteAmount (0, t, 0.5f);
        juce::MidiBuffer chord;
        for (const int note : { 60, 64, 67 })
            chord.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        runBlocks (rig, 20, chord);
        std::vector<float> values;
        for (int v = 0; v < silt::Engine::numVoices; ++v)
            if (rig.silt.voiceShown[(size_t) v].load())
                values.push_back (rig.silt.voiceModulated01[(size_t) v][(size_t) t].load());
        return values;
    };
    const auto together = voices (0.0f);
    REQUIRE (together.size() == 3);
    CHECK_THAT (together[0], WithinAbs (together[1], 1.0e-5));
    CHECK_THAT (together[1], WithinAbs (together[2], 1.0e-5));

    const auto apart = voices (1.0f);
    REQUIRE (apart.size() == 3);
    CHECK (std::abs (apart[0] - apart[1]) > 0.05f);
    CHECK (std::abs (apart[1] - apart[2]) > 0.05f);
}

TEST_CASE ("RESAMPLE records Silt's own output and plays it as the new sample")
{
    Rig rig;
    loadSine (rig);
    rig.set ("captureFeed", 1.0f);
    rig.set ("captureBars", 0.0f);   // 1 bar: 2 s at 120 BPM
    rig.silt.armCapture();
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    runBlocks (rig, 200, on);   // about 2.1 s; no host playing, so it starts at once
    const auto loaded = [&] { return rig.silt.getOverview().name.startsWith ("RESAMPLE"); };
    for (int i = 0; i < 300 && ! loaded(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    REQUIRE (loaded());
    const auto overview = rig.silt.getOverview();
    CHECK_THAT (overview.seconds, WithinAbs (2.0, 0.01));
    CHECK (*std::max_element (overview.maximum.begin(), overview.maximum.end()) > 0.05f);   // it heard the grains
    CHECK_FALSE (rig.silt.capturePreset ("x")->getChildByName ("PARAMS")->hasAttribute ("captureFeed"));
}

TEST_CASE ("UNDO takes a recorded or loaded sample back to the one before, and REDO brings it back")
{
    Rig rig;
    REQUIRE (waitFor ([&] { return rig.silt.getFactorySample() == 0; }));   // the opening sample: not a step
    CHECK_FALSE (rig.silt.canUndoRoutes());
    loadSine (rig);
    const auto sineName = rig.silt.getOverview().name;
    CHECK (rig.silt.canUndoRoutes());

    rig.set ("captureFeed", 1.0f);
    rig.set ("captureBars", 0.0f);
    rig.silt.armCapture();
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    runBlocks (rig, 200, on);
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name.startsWith ("RESAMPLE"); }));
    const auto feedName = rig.silt.getOverview().name;
    CHECK (rig.silt.undoDescription().startsWith ("SAMPLE RESAMPLE"));

    rig.silt.undoRoutes();
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name == sineName; }));
    CHECK_THAT (rig.silt.getOverview().seconds, WithinAbs (2.0, 0.01));

    rig.silt.redoRoutes();
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name == feedName; }));

    // All the way back: the factory sample the instance opened with, in its key.
    rig.silt.undoRoutes();
    rig.silt.undoRoutes();
    REQUIRE (waitFor ([&] { return rig.silt.getFactorySample() == 0; }));
    CHECK (rootNote (rig) == 64.0f);
}

TEST_CASE ("Fuzz: every parameter at random, notes coming and going, odd block sizes and rates: always finite and in range")
{
    juce::Random random (20261027);
    for (const double rate : { 44100.0, 96000.0 })
        for (const int block : { 1, 37, 333, 2048 })
        {
            Rig rig;
            loadSine (rig);
            rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("pitch"), 0.5f);
            rig.silt.setRouteAmount (1, rig.silt.modulationTarget ("filterCutoff"), -0.7f);
            rig.silt.prepareToPlay (rate, block);
            juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), block);
            const int blocks = (int) (rate * 0.6) / block + 1;
            bool finite = true;
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                // Every few blocks: a handful of parameters jump anywhere (modes and switches too).
                if (b % juce::jmax (1, 4096 / block) == 0)
                    for (int i = 0; i < 12; ++i)
                    {
                        auto* p = rig.silt.getParameters()[random.nextInt (rig.silt.getParameters().size())];
                        p->setValue (random.nextFloat());
                    }
                juce::MidiBuffer midi;
                if (random.nextInt (8) == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36 + random.nextInt (48), (juce::uint8) (1 + random.nextInt (126))), random.nextInt (block));
                if (random.nextInt (8) == 0)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 36 + random.nextInt (48)), random.nextInt (block));
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < block; ++i)
                        buffer.setSample (ch, i, 0.3f * std::sin ((float) (b * block + i) * 0.05f));   // something on the inputs
                rig.silt.processBlock (buffer, midi);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < block; ++i)
                    {
                        const float v = buffer.getSample (ch, i);
                        finite = finite && std::isfinite (v);
                        peak = std::max (peak, std::abs (v));
                    }
            }
            INFO ("rate " << rate << ", block " << block);
            CHECK (finite);
            CHECK (peak <= 1.0f);
        }
}

TEST_CASE ("Sample tools: trim to the region, reverse, normalise; each an undo step; and the sound written as a WAV")
{
    Rig rig;
    loadSine (rig);   // 2 s at 48 kHz, peaking at 0.5
    const auto name = rig.silt.getOverview().name;

    // Trim to the middle second: START and LENGTH then cover all of it.
    rig.set ("start", 0.25f);
    rig.set ("length", 0.5f);
    rig.silt.editSample (SiltProcessor::SampleEdit::trimToRegion);
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name.endsWith ("TRIM"); }));
    CHECK_THAT (rig.silt.getOverview().seconds, WithinAbs (1.0, 0.01));
    CHECK_THAT (rig.value ("start"), WithinAbs (0.0, 1e-6));
    CHECK_THAT (rig.value ("length"), WithinAbs (1.0, 1e-6));

    // Undo puts the whole sample back.
    rig.silt.undoRoutes();
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name == name; }));
    CHECK_THAT (rig.silt.getOverview().seconds, WithinAbs (2.0, 0.01));

    // Normalise: the peak at −1 dB.
    rig.silt.editSample (SiltProcessor::SampleEdit::normalise);
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name.endsWith ("NORM"); }));
    const auto overview = rig.silt.getOverview();
    CHECK_THAT (*std::max_element (overview.maximum.begin(), overview.maximum.end()), WithinAbs (juce::Decibels::decibelsToGain (-1.0f), 0.01));

    // Reverse, then written out: the file starts where the sample ended.
    rig.silt.editSample (SiltProcessor::SampleEdit::reverse);
    REQUIRE (waitFor ([&] { return rig.silt.getOverview().name.endsWith ("REV"); }));
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("silt-out", ".wav");
    REQUIRE (rig.silt.writeAudioFile (file));
    std::unique_ptr<juce::AudioFormatReader> reader (juce::WavAudioFormat().createReaderFor (file.createInputStream().release(), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->lengthInSamples == 96000);
    CHECK (reader->numChannels == 2);
    file.deleteFile();
}

TEST_CASE ("SLICE: moving, adding and removing a slice makes them MANUAL, kept with the session")
{
    Rig rig;
    loadSine (rig);
    rig.set ("keyMode", rig.silt.params.getParameter ("keyMode")->convertTo0to1 (3.0f));     // SLICE
    rig.set ("sliceCount", rig.silt.params.getParameter ("sliceCount")->convertTo0to1 (4.0f));
    runBlocks (rig, 1);   // the count reaches its modulated copy
    REQUIRE (rig.silt.sliceMarks().size() == 4);   // equal quarters

    rig.silt.moveSlice (1, 0.4f);
    CHECK (juce::roundToInt (rig.silt.params.getParameter ("sliceMode")->convertFrom0to1 (rig.value ("sliceMode"))) == 2);   // MANUAL
    auto marks = rig.silt.sliceMarks();
    REQUIRE (marks.size() == 4);
    CHECK_THAT (marks[1], WithinAbs (0.4, 1e-4));
    CHECK_THAT (marks[2], WithinAbs (0.5, 1e-4));

    rig.silt.moveSlice (1, 0.9f);   // can't pass its neighbour
    CHECK (rig.silt.sliceMarks()[1] < rig.silt.sliceMarks()[2]);

    rig.silt.addSlice (0.9f);
    CHECK (rig.silt.sliceMarks().size() == 5);
    rig.silt.removeSlice (0);
    marks = rig.silt.sliceMarks();
    REQUIRE (marks.size() == 4);

    juce::MemoryBlock state;
    rig.silt.getStateInformation (state);
    Rig restored;
    restored.silt.setStateInformation (state.getData(), (int) state.getSize());
    const auto again = restored.silt.manualSlices();
    REQUIRE (again.size() == marks.size());
    for (size_t i = 0; i < marks.size(); ++i)
        CHECK_THAT (again[i], WithinAbs (marks[i], 1e-5));
}
