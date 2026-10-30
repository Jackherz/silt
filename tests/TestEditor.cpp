// Editor-level tests: the panel the controller drives, and the modulation
// gestures. The editor is built headlessly, as the snapshot tool does.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../plugin/Palette.h"
#include "../plugin/PluginEditor.h"
#include "../plugin/Grip.h"
#include "../plugin/Help.h"

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
        SiltEditor::tourOnFirstOpen = false;
        silt::ui::Tour::remember = false;
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
    for (int keyMode = 0; keyMode < 4; ++keyMode)
        for (int sync = 0; sync < 2; ++sync)
        {
            rig.set ("keyMode", (float) keyMode);
            rig.set ("delaySync", (float) sync);
            rig.checkAgreement ("key mode / delay sync");
        }
    rig.set ("spaceSync", 1.0f);
    rig.checkAgreement ("space sync");
    rig.silt.resonatorHalf.store (1);
    rig.checkAgreement ("resonator, second half");
    rig.silt.resonatorHalf.store (0);

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
                rig.set ((id + "Voice").toRawUTF8(), 1.0f);
                rig.checkAgreement ("modulator type / sync, per voice");
                rig.set ((id + "Voice").toRawUTF8(), 0.0f);
            }
        rig.set ((id + "Type").toRawUTF8(), 0.0f);
        rig.set ((id + "Voice").toRawUTF8(), 1.0f);
        rig.editor->showPage (silt::layout::mod);
        CHECK (rig.editor->visibleParameterId (silt::layout::mod, 7) == id + "Spread");   // SPREAD in ASSIGN's place
        rig.set ((id + "Voice").toRawUTF8(), 0.0f);
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

TEST_CASE ("Dragging a route's depth through 0 in MOD ROUTES keeps the row and carries on past it")
{
    EditorRig rig;
    const int t = rig.silt.modulationTarget ("filterCutoff");
    rig.silt.setRouteAmount (0, t, 0.1f);
    rig.editor->showRoutes (-1);

    silt::ui::RouteList* list = nullptr;
    for (auto* child : rig.editor->getChildren())
        if (auto* l = dynamic_cast<silt::ui::RouteList*> (child))
            list = l;
    REQUIRE (list != nullptr);
    list->update();

    // The depth box: the route list's only editable label.
    juce::Label* depth = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->isEditableOnDoubleClick())
                depth = label;
            find (*child);
        }
    };
    find (*list);
    REQUIRE (depth != nullptr);

    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (float dy)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, { 10.0f, 10.0f + dy }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 depth, depth, now, { 10.0f, 10.0f }, now, 1, dy != 0.0f);
    };
    auto* row = depth->getParentComponent();   // the row listens to its depth box
    row->mouseDown (event (0.0f));
    for (const float dy : { 10.0f, 15.0f, 30.0f, 45.0f })   // 150 px is 100%: +10% through 0 to −20%
    {
        row->mouseDrag (event (dy));
        list->update();
    }
    CHECK_THAT (rig.silt.routeAmount (0, t), WithinAbs (-0.2, 1e-4));
    row->mouseUp (event (45.0f));

    // Released at 0, the route goes.
    row->mouseDown (event (0.0f));
    row->mouseDrag (event (-30.0f));
    list->update();
    row->mouseUp (event (-30.0f));
    CHECK (rig.silt.routeAmount (0, t) == 0.0f);
    list->update();
    CHECK (rig.silt.allRoutes().empty());
}

TEST_CASE ("The tour lets clicks through to the parts it lights, and nowhere else")
{
    EditorRig rig;
    rig.editor->setSize (1100, 700);
    rig.editor->startTour();
    silt::ui::Tour* tour = nullptr;
    for (auto* child : rig.editor->getChildren())
        if (auto* t = dynamic_cast<silt::ui::Tour*> (child))
            tour = t;
    REQUIRE (tour != nullptr);
    REQUIRE (tour->isRunning());

    tour->go (5);   // SPRAY (after Welcome, The screen, Your own sounds, Live input, Pages): its knob and the screen are lit
    auto* spray = rig.editor->findKnob ("spray");
    REQUIRE (spray != nullptr);
    const auto knob = spray->getBounds().getCentre();
    CHECK_FALSE (tour->hitTest (knob.x, knob.y));
    auto* pitch = rig.editor->findKnob ("pitch");
    const auto other = pitch->getBounds().getCentre();
    CHECK (tour->hitTest (other.x, other.y));

    tour->go (99);
    CHECK_FALSE (tour->isRunning());
}

TEST_CASE ("A theme changes the palette, and Cream puts it back")
{
    const auto cream = juce::Colour (silt::ui::themes()[0].chassis);
    silt::ui::applyTheme (1);
    CHECK (silt::ui::currentThemeIndex() == 1);
    CHECK (silt::ui::chassis != cream);
    CHECK (silt::ui::frame.getPerceivedBrightness() < 0.3f);   // the header stays dark in every theme
    silt::ui::applyTheme (0);
    CHECK (silt::ui::chassis == cream);
}

TEST_CASE ("The strip's controls fit without overlapping at the smallest size")
{
    EditorRig rig;
    rig.editor->setSize (860, 520);
    std::vector<juce::Rectangle<int>> boxes;
    for (const auto* text : { "LOAD", "LEARN", "RANDOM", "RESAMPLE", "UNDO", "REDO", "MOD ROUTES" })
        for (auto* child : rig.editor->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == text)
                boxes.push_back (b->getBounds());
    REQUIRE (boxes.size() == 7);
    for (size_t i = 0; i < boxes.size(); ++i)
        for (size_t j = i + 1; j < boxes.size(); ++j)
            CHECK_FALSE (boxes[i].intersects (boxes[j]));
}

TEST_CASE ("The screen's grip trades height with the knobs, is kept with the session, and double-click resets it")
{
    EditorRig rig;
    rig.editor->setSize (1040, 520);   // short: the well's height, not the column's width, limits the knobs
    auto* knob = rig.editor->findKnob ("pitch");
    REQUIRE (knob != nullptr);
    const int knobBefore = knob->getHeight();

    // The grip at the screen's bottom-right corner: drag it up 80 px.
    silt::ui::Grip* grip = nullptr;
    for (auto* child : rig.editor->getChildren())
        if (auto* g = dynamic_cast<silt::ui::Grip*> (child); g != nullptr && g->getMouseCursor() == juce::MouseCursor::UpDownResizeCursor)
            grip = g;
    REQUIRE (grip != nullptr);
    const float before = rig.silt.layoutWell.load();
    grip->onStart();
    grip->onDrag ({ 0, -80 });
    CHECK (rig.silt.layoutWell.load() > before);
    CHECK (knob->getHeight() > knobBefore);   // the knobs grow with their well

    // Kept with the session.
    juce::MemoryBlock state;
    rig.silt.getStateInformation (state);
    SiltProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_THAT (restored.layoutWell.load(), WithinAbs (rig.silt.layoutWell.load(), 1.0e-4));

    grip->onReset();
    CHECK_THAT (rig.silt.layoutWell.load(), WithinAbs (SiltProcessor::defaultLayout().well, 1.0e-6));
}

TEST_CASE ("Every control on every page has a line of hover help, short enough for the display")
{
    for (const bool instrument : { true, false })
        for (const auto& e : silt::layout::entries (instrument))
        {
            const auto text = silt::ui::helpFor (e.id);
            INFO (e.id);
            CHECK (text.isNotEmpty());
            CHECK (text.length() <= 48);
        }
}

TEST_CASE ("Over a knob the LCD gives its help; over a ring, the ring's hint; armed or not")
{
    EditorRig rig;
    rig.editor->setSize (1040, 600);
    auto* knob = dynamic_cast<SiltEditor::FineSlider*> (rig.editor->findKnob ("spray"));
    REQUIRE (knob != nullptr);
    rig.silt.setRouteAmount (0, rig.silt.modulationTarget ("spray"), 0.3f);

    for (const int armed : { -1, 1 })
    {
        rig.silt.armedMod.store (armed);
        INFO ("armed " << armed);

        // The knob itself: its own help.
        rig.editor->showHelpFor ("spray");
        knob->ringHover (-1);
        CHECK (rig.editor->displayText() == silt::ui::helpFor ("spray"));

        // One of its rings: what that ring does.
        knob->ringHover (0);
        CHECK (rig.editor->displayText().startsWith ("MOD 1:"));

        // Off it: the standing message comes back.
        knob->ringHover (-2);
        rig.editor->showHelpFor ({});
        if (armed >= 0)
            CHECK (rig.editor->displayText().contains ("ARMED"));
        else
            CHECK (rig.editor->displayText().contains ("BPM"));
    }
}

TEST_CASE ("A right-click on a page key opens its menu without pressing it or changing page")
{
    EditorRig rig;
    juce::TextButton* filterKey = nullptr, *grainKey = nullptr;
    for (auto* child : rig.editor->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getRadioGroupId() == 1)
        {
            if (b->getButtonText() == "FILTER")
                filterKey = b;
            if (b->getButtonText() == "GRAIN")
                grainKey = b;
        }
    REQUIRE (filterKey != nullptr);
    REQUIRE (grainKey != nullptr);
    REQUIRE (grainKey->getToggleState());

    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent right (source, { 5.0f, 5.0f }, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  filterKey, filterKey, now, { 5.0f, 5.0f }, now, 1, false);
    static_cast<juce::Component*> (filterKey)->mouseDown (right);   // as the mouse would
    static_cast<juce::Component*> (filterKey)->mouseUp (right);
    CHECK (grainKey->getToggleState());
    CHECK_FALSE (filterKey->getToggleState());
    CHECK (rig.silt.currentPage.load() == silt::layout::grain);
    juce::PopupMenu::dismissAllActiveMenus();
}

#include "../plugin/GrainScreen.h"

TEST_CASE ("Zoomed in, the bar under the waveform scrolls through the sample")
{
    juce::ScopedJuceInitialiser_GUI juce;
    silt::ui::GrainScreen screen;
    screen.setBounds (0, 0, 1000, 400);
    SiltProcessor::Overview overview;
    overview.minimum.assign (1000, -0.5f);
    overview.maximum.assign (1000, 0.5f);
    overview.seconds = 10.0;
    screen.setOverview (overview);
    screen.setView (0.0f, 0.25f);

    const auto track = screen.scrollTrack();
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (float x, float fromX)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, { x, track.getCentreY() }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &screen, &screen, now, { fromX, track.getCentreY() }, now, 1, x != fromX);
    };
    // Take the thumb at its middle and drag it a quarter of the way along.
    const float grab = track.getX() + track.getWidth() * 0.125f;
    screen.mouseDown (event (grab, grab));
    screen.mouseDrag (event (grab + track.getWidth() * 0.25f, grab));
    screen.mouseUp (event (grab + track.getWidth() * 0.25f, grab));
    CHECK (screen.getViewStart() == Catch::Approx (0.25f).margin (0.01f));
    CHECK (screen.getZoom() == Catch::Approx (4.0f).margin (0.01f));   // moving keeps the zoom

    // A click along the bar, off the thumb: the view centres there.
    const float far = track.getX() + track.getWidth() * 0.9f;
    screen.mouseDown (event (far, far));
    screen.mouseUp (event (far, far));
    CHECK (screen.getViewStart() == Catch::Approx (0.75f).margin (0.01f));   // as far as it goes
}

TEST_CASE ("Accessibility: every knob has a title and a description for screen readers")
{
    EditorRig rig;
    for (const char* id : { "spray", "filterCutoff", "resMix", "m1Rate" })
    {
        INFO (id);
        auto* knob = rig.editor->findKnob (id);
        REQUIRE (knob != nullptr);
        CHECK (knob->getTitle().isNotEmpty());
        CHECK (knob->getDescription().isNotEmpty());
    }
}

TEST_CASE ("Diagnostics: ARM and LIVE with no input say where the audio has to come from")
{
    EditorRig rig;
    rig.set ("keyMode", 1.0f);   // LIVE, with nothing coming in
    juce::AudioBuffer<float> buffer (juce::jmax (rig.silt.getTotalNumInputChannels(), rig.silt.getTotalNumOutputChannels()), 512);
    juce::MidiBuffer midi;
    buffer.clear();
    rig.silt.processBlock (buffer, midi);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2300);   // the editor's timer: two seconds of silence
    INFO (rig.editor->displayText() << " source " << (int) rig.silt.captureSource.load());
    CHECK (rig.editor->displayText().contains ("NEEDS INPUT"));
}

TEST_CASE ("Presets: every factory preset has a category; favourites, views and search filter the list")
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;
    juce::TemporaryFile folder;
    PresetManager presets (silt, folder.getFile());
    for (const auto& e : presets.list())
    {
        INFO (e.name);
        CHECK (PresetManager::categories().contains (e.category));
    }
    const auto count = [&] (const juce::String& view, const juce::String& search)
    {
        int n = 0;
        for (const auto& e : presets.list())
            n += presets.shows (e, view, search) ? 1 : 0;
        return n;
    };
    CHECK (count ("ALL", {}) == PresetManager::numFactoryPresets());
    CHECK (count ("SLICES", {}) == 2);
    CHECK (count ("ALL", "bloom") == 1);         // any case
    CHECK (count ("ALL", "rhythmic") == 3);      // a category matches too
    CHECK (count ("FAVOURITES", {}) == 0);
    presets.setFavourite ("Glass Cloud", true);
    presets.setFavourite ("Dark Drone", true);
    PresetManager again (silt, folder.getFile());   // kept in the folder
    CHECK (again.isFavourite ("Glass Cloud"));
    CHECK (count ("FAVOURITES", {}) == 2);
    presets.setFavourite ("Glass Cloud", false);
    CHECK (count ("FAVOURITES", {}) == 1);
    folder.getFile().deleteRecursively();
}

TEST_CASE ("Presets: a user preset saves with a category, and can be renamed, refiled and binned")
{
    juce::ScopedJuceInitialiser_GUI juce;
    SiltProcessor silt;
    juce::TemporaryFile folder;
    PresetManager presets (silt, folder.getFile());
    REQUIRE (presets.save ("Mine", "PADS"));
    const auto find = [&] (const juce::String& name)
    {
        for (const auto& e : presets.list())
            if (e.name == name)
                return std::optional<PresetManager::Entry> (e);
        return std::optional<PresetManager::Entry>();
    };
    REQUIRE (find ("Mine").has_value());
    CHECK (find ("Mine")->category == "PADS");
    presets.setFavourite ("Mine", true);

    REQUIRE (presets.rename (*find ("Mine"), "Yours"));
    CHECK_FALSE (find ("Mine").has_value());
    REQUIRE (find ("Yours").has_value());
    CHECK (presets.isFavourite ("Yours"));   // the star follows the name
    CHECK (silt.presetName == "Yours");

    REQUIRE (presets.setCategory (*find ("Yours"), "DRONES"));
    CHECK (find ("Yours")->category == "DRONES");
    CHECK_FALSE (presets.rename (*find ("Glass Cloud"), "x"));   // factory presets stay as they are

    PresetManager::toTrash = false;   // not the real bin
    REQUIRE (presets.remove (*find ("Yours")));
    PresetManager::toTrash = true;
    CHECK_FALSE (find ("Yours").has_value());
    CHECK_FALSE (presets.isFavourite ("Yours"));
    folder.getFile().deleteRecursively();
}

TEST_CASE ("Preset browser: clicking plays a preset; CANCEL puts the sound back, KEEP is one undo step")
{
    EditorRig rig;
    auto* browser = rig.editor->getPresetBrowser();
    REQUIRE (browser != nullptr);
    rig.set ("spray", 37.0f);   // the sound before
    const auto spray = [&] { return rig.silt.params.getParameter ("spray")->getValue(); };
    const float before = spray();

    browser->open();
    browser->setSearch ("glass");
    REQUIRE (browser->numShown() == 1);
    browser->choose (0);
    CHECK (rig.silt.presetName == "Glass Cloud");
    CHECK (spray() != before);
    browser->setSearch ("drone");
    browser->choose (0);
    CHECK (rig.silt.presetName == "Dark Drone");
    browser->close (false);   // CANCEL
    CHECK (spray() == Catch::Approx (before));
    CHECK (rig.silt.presetName != "Dark Drone");
    CHECK_FALSE (browser->isVisible());

    rig.silt.clearUndoHistory();
    browser->open();
    browser->setView ("PADS");
    REQUIRE (browser->numShown() >= 2);
    browser->choose (0);
    browser->choose (1);   // two auditions...
    const auto kept = rig.silt.presetName;
    browser->close (true);   // ...one KEEP
    CHECK (rig.silt.presetName == kept);
    REQUIRE (rig.silt.undoRoutes());   // a single step back
    CHECK (spray() == Catch::Approx (before));
    CHECK_FALSE (rig.silt.canUndoRoutes());
}

TEST_CASE ("Preset browser: choosing a category lights only its key")
{
    EditorRig rig;
    auto* browser = rig.editor->getPresetBrowser();
    browser->open();
    std::map<juce::String, juce::Button*> keys;
    for (auto* child : browser->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getRadioGroupId() == 2)
            keys[b->getButtonText()] = b;
    REQUIRE (keys.count ("ALL") == 1);
    REQUIRE (keys.count ("PADS") == 1);
    keys["PADS"]->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (keys["PADS"]->getToggleState());
    CHECK_FALSE (keys["ALL"]->getToggleState());
    int lit = 0;
    for (auto& [name, key] : keys)
        lit += key->getToggleState() ? 1 : 0;
    CHECK (lit == 1);
    browser->close (false);
}

TEST_CASE ("Delete on a clicked knob removes its modulation, as one undo step")
{
    EditorRig rig;
    const int t = rig.silt.modulationTarget ("spray");
    rig.silt.setRouteAmount (0, t, 0.4f);
    rig.silt.setRouteAmount (2, t, -0.2f);
    rig.silt.clearUndoHistory();
    auto* knob = rig.editor->findKnob ("spray");
    REQUIRE (knob != nullptr);
    CHECK (knob->getWantsKeyboardFocus());
    CHECK (knob->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (rig.silt.routeAmount (0, t) == 0.0f);
    CHECK (rig.silt.routeAmount (2, t) == 0.0f);
    CHECK (rig.editor->displayText().startsWith ("MODULATION REMOVED"));
    REQUIRE (rig.silt.undoRoutes());
    CHECK (rig.silt.routeAmount (0, t) == Catch::Approx (0.4f));
    CHECK (rig.silt.routeAmount (2, t) == Catch::Approx (-0.2f));
    // With nothing to remove, the key passes on.
    CHECK_FALSE (rig.editor->findKnob ("pitch")->keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
}

TEST_CASE ("Preset browser: the star favourites a preset without playing it")
{
    EditorRig rig;
    auto* browser = rig.editor->getPresetBrowser();
    browser->open();
    browser->setSearch ("tape choir");
    REQUIRE (browser->numShown() == 1);
    const bool was = browser->isFavourite (0);
    const auto before = rig.silt.presetName;
    browser->toggleFavourite (0);
    CHECK (browser->isFavourite (0) != was);
    CHECK (rig.silt.presetName == before);   // not played
    browser->setSearch ({});
    browser->setView ("FAVOURITES");
    bool listed = false;
    for (int i = 0; i < browser->numShown(); ++i)
        listed = listed || browser->shownName (i) == "Tape Choir";
    CHECK (listed != was);
    browser->setView ("ALL");
    browser->setSearch ("tape choir");
    browser->toggleFavourite (0);   // as it was
    CHECK (browser->isFavourite (0) == was);
    browser->close (false);
}

#include "../plugin/SpaceScene.h"

// Not run with the suite: renders SPACE's sky partway through a note's light
// echoes, to SILT_SCENE_OUT/scene-*.png, to look at.
TEST_CASE ("SPACE sky: a frame of the light echoes", "[.][scene]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto out = juce::SystemStats::getEnvironmentVariable ("SILT_SCENE_OUT", {});
    for (const bool ping : { false, true })
    {
        silt::ui::SpaceScene scene;
        silt::ui::SpaceScene::Settings s;
        s.delay = 0.7f; s.feedback = 0.6f; s.echoSeconds = 0.3f; s.reverb = 0.4f; s.decay = 0.6f; s.size = 0.6f; s.tone = 0.5f;
        s.pingPong = ping;
        std::vector<float> envelope (1000);
        for (size_t i = 0; i < envelope.size(); ++i)
            envelope[i] = 0.3f + 0.4f * std::abs (std::sin ((float) i * 0.01f));
        juce::Image image (juce::Image::RGB, 1000, 380, true);
        const juce::Rectangle<int> area (0, 0, 1000, 380), wave (0, 20, 1000, 200);
        s.sinceNote = 5.0f;
        scene.advance (s, 0.03f, envelope);
        s.sinceNote = 0.0f;   // a note
        scene.advance (s, 0.03f, envelope);
        for (float t = 0.0f; t < 1.4f; t += 1.0f / 30.0f)
        {
            s.sinceNote += 1.0f / 30.0f;
            scene.advance (s, 1.0f / 30.0f, envelope);
        }
        juce::Graphics g (image);
        scene.drawBehind (g, area, wave, envelope, s);
        scene.drawOver (g, area, wave, s);
        if (out.isNotEmpty())
        {
            juce::FileOutputStream stream (juce::File (out).getChildFile (ping ? "scene-pingpong.png" : "scene-stereo.png"));
            stream.setPosition (0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
    }
    SUCCEED();
}

TEST_CASE ("A value box being typed into stays the capsule: see-through, unoutlined, centred editor")
{
    EditorRig rig;
    auto* knob = rig.editor->findKnob ("spray");
    REQUIRE (knob != nullptr);
    juce::Label* box = nullptr;
    for (auto* child : knob->getChildren())
        if (auto* l = dynamic_cast<juce::Label*> (child))
            box = l;
    REQUIRE (box != nullptr);
    rig.editor->setVisible (true);
    box->showEditor();
    auto* ed = box->getCurrentTextEditor();
    REQUIRE (ed != nullptr);
    CHECK (ed->findColour (juce::TextEditor::backgroundColourId).getAlpha() == 0);
    CHECK (ed->findColour (juce::TextEditor::outlineColourId).getAlpha() == 0);
    CHECK (ed->findColour (juce::TextEditor::focusedOutlineColourId).getAlpha() == 0);
    CHECK (ed->getJustificationType() == juce::Justification::centred);
    CHECK (ed->getFont().getTypefacePtr() == silt::ui::siltTypeface (true, silt::ui::Weight::regular));
    box->hideEditor (true);
}
