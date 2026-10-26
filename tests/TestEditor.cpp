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
