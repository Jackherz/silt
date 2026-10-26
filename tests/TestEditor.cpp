// Editor-level tests: the panel the controller drives, and the modulation
// gestures. The editor is built headlessly, as the snapshot tool does.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../plugin/PluginEditor.h"

using Catch::Matchers::WithinAbs;

namespace
{
struct EditorRig
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;
    std::unique_ptr<SiltEditor> editor;

    EditorRig()
    {
        silt.enableAllBuses();
        silt.prepareToPlay (48000.0, 512);
        editor.reset (dynamic_cast<SiltEditor*> (silt.createEditor()));
    }
    ~EditorRig() { editor = nullptr; }

    void set (const char* id, float value)   // in the parameter's own units
    {
        auto* p = silt.params.getParameter (id);
        p->setValueNotifyingHost (dynamic_cast<juce::RangedAudioParameter*> (p)->convertTo0to1 (value));
    }

    // Every page and slot: the control the editor shows is what the controller moves.
    void checkAgreement (const char* context)
    {
        editor->refreshLayout();
        for (int page = 0; page < SiltProcessor::numPages; ++page)
        {
            editor->showPage (page);
            for (int slot = 0; slot < SiltProcessor::slotsPerPage; ++slot)
            {
                INFO (context << ": page " << silt::layout::pageName (page) << ", slot " << slot);
                CHECK (editor->visibleParameterId (page, slot) == silt.slotParameter (page, slot));
            }
        }
    }
};
} // namespace

TEST_CASE ("Every slot's shown control is the one the controller moves, in every mode")
{
    EditorRig rig;
    rig.checkAgreement ("defaults");

    for (int rateMode = 0; rateMode < 3; ++rateMode)
        for (int sync = 0; sync < 2; ++sync)
        {
            rig.set ("rateMode", (float) rateMode);
            rig.set ("sizeSync", (float) sync);
            rig.checkAgreement ("rate mode / size sync");
        }
    for (int keyMode = 0; keyMode < 2; ++keyMode)
        for (int sync = 0; sync < 2; ++sync)
        {
            rig.set ("keyMode", (float) keyMode);
            rig.set ("delaySync", (float) sync);
            rig.checkAgreement ("key mode / delay sync");
        }
    rig.set ("spaceSync", 1.0f);
    rig.checkAgreement ("space sync");

    for (int m = 0; m < SiltProcessor::numModulators; ++m)
    {
        rig.silt.modShown.store (m);
        const auto id = "m" + juce::String (m + 1);
        for (int type = 0; type < 6; ++type)
            for (int sync = 0; sync < 2; ++sync)
            {
                rig.set ((id + "Type").toRawUTF8(), (float) type);
                rig.set ((id + "Sync").toRawUTF8(), (float) sync);
                rig.checkAgreement ("modulator type / sync");
            }
    }
}

TEST_CASE ("Every slot shows exactly one control at a time")
{
    EditorRig rig;
    const auto context = rig.silt.layoutContext();
    for (int page = 0; page < SiltProcessor::numPages; ++page)
        for (int slot = 0; slot < SiltProcessor::slotsPerPage; ++slot)
        {
            int shown = 0;
            for (const auto& e : silt::layout::entries (SiltProcessor::isInstrument))
                shown += e.page == page && e.slot == slot && e.inSlot() && e.isShown (context) ? 1 : 0;
            INFO ("page " << page << ", slot " << slot);
            CHECK (shown <= 1);
        }
}

TEST_CASE ("Alt-drag picks the armed modulator, else the knob's only route, else the last used")
{
    using A = std::array<float, SiltProcessor::numModulators>;
    CHECK (SiltEditor::depthModulatorFor (2, A { 0.3f, 0.0f, 0.0f, 0.0f }, 1) == 2);
    CHECK (SiltEditor::depthModulatorFor (-1, A { 0.0f, 0.0f, 0.4f, 0.0f }, 1) == 2);
    CHECK (SiltEditor::depthModulatorFor (-1, A { 0.2f, 0.0f, 0.4f, 0.0f }, 1) == 1);
    CHECK (SiltEditor::depthModulatorFor (-1, A {}, 3) == 3);
}

TEST_CASE ("A depth drag makes one route and leaves the knob's value alone")
{
    EditorRig rig;
    rig.editor->showPage (silt::layout::filter);
    auto* knob = dynamic_cast<SiltEditor::FineSlider*> (rig.editor->findKnob ("filterCutoff"));
    REQUIRE (knob != nullptr);
    const float before = rig.silt.params.getParameter ("filterCutoff")->getValue();
    const int t = rig.silt.modulationTarget ("filterCutoff");

    rig.silt.armedMod.store (1);
    knob->beginDepth (knob->depthModulator());
    knob->dragDepth (60.0f, false);   // 60 px up: +40%
    knob->endDepth();

    CHECK_THAT (rig.silt.routeAmount (1, t), WithinAbs (0.4, 1.0e-4));
    CHECK (rig.silt.allRoutes().size() == 1);
    CHECK (rig.silt.params.getParameter ("filterCutoff")->getValue() == before);

    // Near zero snaps to zero, which removes the route.
    knob->beginDepth (1);
    knob->dragDepth (-59.0f, false);
    knob->endDepth();
    CHECK (rig.silt.allRoutes().empty());
}

TEST_CASE ("Dropping a mod chip on a knob makes a +25% route; switch-like controls take none")
{
    EditorRig rig;
    auto* knob = dynamic_cast<SiltEditor::FineSlider*> (rig.editor->findKnob ("spray"));
    REQUIRE (knob != nullptr);
    juce::DragAndDropTarget::SourceDetails details (juce::var ("mod:3"), nullptr, {});
    REQUIRE (knob->isInterestedInDragSource (details));
    knob->itemDropped (details);
    CHECK_THAT (rig.silt.routeAmount (2, rig.silt.modulationTarget ("spray")), WithinAbs (0.25, 1.0e-6));

    // A knob that isn't a modulation target doesn't accept the drop.
    auto* modKnob = dynamic_cast<SiltEditor::FineSlider*> (rig.editor->findKnob ("m1Macro"));
    REQUIRE (modKnob != nullptr);
    CHECK_FALSE (modKnob->isInterestedInDragSource (details));
}

TEST_CASE ("The editor's size is kept with the set")
{
    juce::MemoryBlock state;
    {
        EditorRig rig;
        rig.editor->setSize (1200, 700);
        rig.silt.getStateInformation (state);
    }
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
    CHECK (editor->getWidth() == 1200);
    CHECK (editor->getHeight() == 700);
}

// --- Presets ------------------------------------------------------------------------------

#include "../plugin/Presets.h"

TEST_CASE ("A preset carries the sound: values and routes, not session settings")
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor a, b;
    a.params.getParameter ("spray")->setValueNotifyingHost (0.7f);
    a.params.getParameter ("captureBars")->setValueNotifyingHost (1.0f);
    a.setRouteAmount (1, a.modulationTarget ("filterCutoff"), -0.3f);
    const auto xml = a.capturePreset ("Mine");
    CHECK_FALSE (xml->getChildByName ("PARAMS")->hasAttribute ("captureBars"));

    b.params.getParameter ("pitch")->setValueNotifyingHost (0.9f);   // must go back to its default
    b.setRouteAmount (3, b.modulationTarget ("pitch"), 0.5f);        // must be cleared
    b.applyPreset (*xml, "Mine");
    CHECK_THAT (b.params.getParameter ("spray")->getValue(), WithinAbs (0.7, 1.0e-3));
    CHECK (b.params.getParameter ("pitch")->getValue() == b.params.getParameter ("pitch")->getDefaultValue());
    CHECK_THAT (b.routeAmount (1, b.modulationTarget ("filterCutoff")), WithinAbs (-0.3, 1.0e-4));
    CHECK (b.routeAmount (3, b.modulationTarget ("pitch")) == 0.0f);
    CHECK (b.presetName == "Mine");
}

TEST_CASE ("Every factory preset names real parameters and real routes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;
    for (int i = 0; i < PresetManager::numFactoryPresets(); ++i)
    {
        const auto xml = PresetManager::factoryPreset (i);
        INFO (xml->getStringAttribute ("name"));
        const auto* values = xml->getChildByName ("PARAMS");
        for (int a = 0; a < values->getNumAttributes(); ++a)
        {
            INFO (values->getAttributeName (a));
            CHECK (silt.params.getParameter (values->getAttributeName (a)) != nullptr);
        }
        for (const auto& route : juce::StringArray::fromTokens (xml->getStringAttribute ("routes"), ";", ""))
        {
            INFO (route);
            CHECK (silt.modulationTarget (route.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false)) >= 0);
        }
    }
}

TEST_CASE ("User presets save, list, load and step")
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;
    juce::TemporaryFile folder;
    PresetManager presets (silt, folder.getFile());

    silt.params.getParameter ("spray")->setValueNotifyingHost (0.4f);
    REQUIRE (presets.save ("Zeta"));
    silt.params.getParameter ("spray")->setValueNotifyingHost (0.9f);
    REQUIRE (presets.save ("Alpha"));

    const auto list = presets.list();
    REQUIRE ((int) list.size() == PresetManager::numFactoryPresets() + 2);
    CHECK (list[(size_t) PresetManager::numFactoryPresets()].name == "Alpha");   // users after factory, by name

    REQUIRE (presets.load (list.back()));   // Zeta
    CHECK_THAT (silt.params.getParameter ("spray")->getValue(), WithinAbs (0.4, 1.0e-3));
    REQUIRE (presets.step (1));             // wraps to the first factory preset
    CHECK (silt.presetName == "Init");
    REQUIRE (presets.step (-1));
    CHECK (silt.presetName == "Zeta");
    folder.getFile().deleteRecursively();
}

TEST_CASE ("A knob's coloured ring is a handle for its route's depth")
{
    EditorRig rig;
    rig.editor->showPage (silt::layout::filter);
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (2, t, 0.3f);
    auto* knob = dynamic_cast<SiltEditor::FineSlider*> (rig.editor->findKnob ("filterCutoff"));
    REQUIRE (knob != nullptr);
    rig.editor->createComponentSnapshot (rig.editor->getLocalBounds());   // draws the rings
    REQUIRE (knob->ringModulators.size() == 1);

    const auto onRing = knob->ringCentre.translated (0.0f, -knob->ringBase);
    CHECK (knob->modulatorAtRing (onRing) == 2);
    CHECK (knob->modulatorAtRing (knob->ringCentre) == -1);   // the knob itself: its value

    // Armed, the next ring out belongs to the armed modulator.
    rig.silt.armedMod.store (0);
    rig.editor->createComponentSnapshot (rig.editor->getLocalBounds());
    CHECK (knob->modulatorAtRing (knob->ringCentre.translated (0.0f, -(knob->ringBase + 3.0f))) == 0);
}

TEST_CASE ("Choosing a page while the route list is open goes to that page")
{
    EditorRig rig;
    rig.editor->showPage (silt::layout::filter);
    rig.editor->showRoutes (-1);
    REQUIRE (rig.editor->routesShown());
    rig.editor->showPage (silt::layout::grain);
    CHECK_FALSE (rig.editor->routesShown());
    CHECK (rig.editor->visibleParameterId (silt::layout::grain, 0) == "pitch");
}
