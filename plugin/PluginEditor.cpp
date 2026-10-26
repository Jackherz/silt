#include "PluginEditor.h"
#include "GrainScreen.h"
#include "ModColours.h"
#include "PanelLook.h"
#include "Presets.h"
#include "ScreenInsets.h"

using namespace silt::ui;
namespace layout = silt::layout;

namespace
{
constexpr int defaultWidth = 1040, defaultHeight = 600;
constexpr int slotsPerRow = 8, railWidth = 118;
constexpr float degreesPerUnit = 300.0f;   // a knob's travel
} // namespace

// --- FineSlider ------------------------------------------------------------------------

void SiltEditor::FineSlider::beginDepth (int modulator)
{
    if (routeGesture)
        routeGesture();
    depthModulatorNow = modulator;
    amountAtStart = amountOf (modulator);
    if (depthDragged)
        depthDragged (modulator, true);
}

void SiltEditor::FineSlider::dragDepth (float pixelsUp, bool fine)
{
    if (depthModulatorNow < 0)
        return;
    float amount = amountAtStart + pixelsUp / (fine ? 1000.0f : 150.0f);
    amount = std::abs (amount) < 0.02f ? 0.0f : juce::jlimit (-1.0f, 1.0f, amount);   // 0 is under a pixel wide otherwise
    setAmount (depthModulatorNow, amount);
    if (depthDragged)
        depthDragged (depthModulatorNow, true);
    repaint();
}

void SiltEditor::FineSlider::endDepth()
{
    if (depthModulatorNow >= 0 && depthDragged)
        depthDragged (depthModulatorNow, false);
    depthModulatorNow = -1;
}

// The rings are handles: a press on one drags its depth (Shift+Alt-click
// flips its polarity). With a modulator armed, the ring just outside the
// knob's routes is its handle, so dragging it makes the route.
int SiltEditor::FineSlider::modulatorAtRing (juce::Point<float> p) const
{
    const int rings = (int) ringModulators.size() + (armedHintModulator >= 0 ? 1 : 0);
    if (target < 0 || rings == 0 || ringBase <= 0.0f)
        return -1;
    const float d = p.getDistanceFrom (ringCentre);
    if (d < ringBase - 4.0f || d > ringBase + 3.0f * (float) (rings - 1) + 5.0f)
        return -1;
    const int ring = juce::jlimit (0, rings - 1, juce::roundToInt ((d - ringBase) / 3.0f));
    return ring < (int) ringModulators.size() ? ringModulators[(size_t) ring] : armedHintModulator;
}

void SiltEditor::FineSlider::mouseMove (const juce::MouseEvent& e)
{
    const int m = modulatorAtRing (e.position);
    setMouseCursor (m >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    const int ring = m < 0 ? -1 : (int) (std::find (ringModulators.begin(), ringModulators.end(), m) - ringModulators.begin());
    if (ring != hoverRing)
    {
        hoverRing = ring;
        repaint();
    }
    if (ringHover)
        ringHover (m);
    juce::Slider::mouseMove (e);
}

void SiltEditor::FineSlider::mouseExit (const juce::MouseEvent& e)
{
    if (hoverRing != -1)
    {
        hoverRing = -1;
        repaint();
    }
    if (ringHover)
        ringHover (-2);   // left the knob
    juce::Slider::mouseExit (e);
}

void SiltEditor::FineSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && showMenu != nullptr)
    {
        showMenu();
        return;
    }
    if (const int m = modulatorAtRing (e.position); m >= 0)
    {
        if (e.mods.isShiftDown() && e.mods.isAltDown())
        {
            if (togglePolarity)
                togglePolarity (m);
            return;
        }
        beginDepth (m);
        return;
    }
    if (e.mods.isAltDown() && target >= 0)
    {
        beginDepth (depthModulator());
        return;
    }
    setMouseDragSensitivity (e.mods.isShiftDown() ? 2500 : 250);
    juce::Slider::mouseDown (e);
}

void SiltEditor::FineSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (isSettingDepth())
        dragDepth ((float) -e.getDistanceFromDragStartY(), e.mods.isShiftDown());
    else if (! e.mods.isPopupMenu())
        juce::Slider::mouseDrag (e);
}

void SiltEditor::FineSlider::mouseUp (const juce::MouseEvent& e)
{
    if (isSettingDepth())
        endDepth();
    else if (! e.mods.isPopupMenu())
        juce::Slider::mouseUp (e);
}

void SiltEditor::FineSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown() && target >= 0)
    {
        if (routeGesture)
            routeGesture();
        setAmount (depthModulator(), 0.0f);   // remove that modulator's route
    }
    else
        juce::Slider::mouseDoubleClick (e);   // back to the default value
}

bool SiltEditor::FineSlider::isInterestedInDragSource (const SourceDetails& d)
{
    return target >= 0 && d.description.toString().startsWith ("mod:");
}

void SiltEditor::FineSlider::itemDragEnter (const SourceDetails& d)
{
    dropHover = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue() - 1;
    repaint();
}

void SiltEditor::FineSlider::itemDragExit (const SourceDetails&)
{
    dropHover = -1;
    repaint();
}

void SiltEditor::FineSlider::itemDropped (const SourceDetails& d)
{
    const int m = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue() - 1;
    dropHover = -1;
    if (m >= 0 && amountOf (m) == 0.0f)
    {
        if (routeGesture)
            routeGesture();
        setAmount (m, 0.25f);   // a musical starting depth
    }
    repaint();
}

int SiltEditor::depthModulatorFor (int armed, const std::array<float, SiltProcessor::numModulators>& amounts, int lastUsed)
{
    if (armed >= 0)
        return armed;
    int only = -1, count = 0;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
        if (amounts[(size_t) m] != 0.0f)
        {
            only = m;
            ++count;
        }
    return count == 1 ? only : juce::jlimit (0, SiltProcessor::numModulators - 1, lastUsed);
}

juce::Component& SiltEditor::Control::component() const
{
    if (menu != nullptr)
        return *menu;
    if (button != nullptr)
        return *button;
    if (banner != nullptr)
        return *banner;
    return *slider;
}

// --- editor -------------------------------------------------------------------------------

SiltEditor::SiltEditor (SiltProcessor& p)
    : AudioProcessorEditor (p), silt (p),
      look (std::make_unique<PanelLook>()), screen (std::make_unique<GrainScreen>())
{
    setLookAndFeel (look.get());
    look->drawModulation = [this] (juce::Graphics& g, juce::Slider& s, juce::Point<float> c, float r, float pos) { drawModulation (g, s, c, r, pos); };
    addAndMakeVisible (*screen);

    modActions.select = [this] (int m, bool arm) { selectModulator (m, arm); };
    modActions.edit = [this] (int m)
    {
        selectModulator (m, false);
        pageTabs[(size_t) layout::mod].setToggleState (true, juce::dontSendNotification);
        silt.currentPage.store (layout::mod);
        showPage (layout::mod);
    };
    modActions.showRoutes = [this] (int m) { showRoutes (m); };
    modActions.pageOf = [this] (int target)
    {
        const auto id = silt.targetParameter (target).getParameterID();
        for (const auto& e : layout::entries (SiltProcessor::isInstrument))
            if (e.id == id)
                return juce::String (layout::pageName (e.page));
        return juce::String();
    };

    rail = std::make_unique<ModRail> (silt, modActions);
    addAndMakeVisible (*rail);
    presetBar = std::make_unique<PresetBar> (silt);
    addAndMakeVisible (*presetBar);
    screen->drawInset = [this] (juce::Graphics& g, juce::Rectangle<float> area) { return drawPageInset (g, area, page, silt, *rail); };
    if (auto* gain = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter ("sampleGain")))
    {
        sampleGainBox = std::make_unique<SampleGainBox> (*gain, [this] { return silt.normalisingGainDb(); });
        screen->addAndMakeVisible (*sampleGainBox);
    }

    for (const auto& entry : layout::entries (SiltProcessor::isInstrument))
        buildControl (entry);
    shownNow.assign (controls.size(), false);

    routeList = std::make_unique<RouteList> (silt, modActions);
    routeList->onClose = [this] { routeList->setVisible (false); routesButton.setToggleState (false, juce::dontSendNotification); };
    addChildComponent (*routeList);

    // Page tabs.
    for (size_t i = 0; i < pageTabs.size(); ++i)
    {
        auto& tab = pageTabs[i];
        tab.setButtonText (layout::pageName ((int) i));
        tab.setRadioGroupId (1);
        tab.setClickingTogglesState (true);
        tab.onClick = [this, i]
        {
            if (pageTabs[i].getToggleState())
            {
                silt.currentPage.store ((int) i);   // the controller's knobs follow the page
                showPage ((int) i);
            }
        };
        addAndMakeVisible (tab);
    }

    // Capture and loading.
    captureBars.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    addAndMakeVisible (captureBars);
    captureBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "captureBars", captureBars);

    armButton.getProperties().set ("red", true);
    armButton.setColour (juce::TextButton::textColourOffId, bone);
    armButton.onClick = [this]
    {
        if (silt.captureState.load() == SiltProcessor::CaptureState::idle)
            silt.armCapture();
        else
            silt.cancelCapture();
    };
    addAndMakeVisible (armButton);

    // LOAD: a file, or one of the built-in samples.
    loadButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Load file...");
        menu.addSectionHeader ("FACTORY SAMPLES");
        const int current = silt.getFactorySample();
        for (int i = 0; i < SiltProcessor::numFactorySamples(); ++i)
            menu.addItem (100 + i, SiltProcessor::factorySampleName (i), true, i == current);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton), [this] (int chosen)
        {
            if (chosen >= 100)
                silt.loadFactorySample (chosen - 100);
            else if (chosen == 1)
                chooseFile();
        });
    };
    addAndMakeVisible (loadButton);

    // LEARN: turn the controller's knobs 1–8, then press its previous- and
    // next-page buttons. Pressing LEARN again cancels.
    learnButton.onClick = [this]
    {
        if (silt.learnStep.load() >= 0)
            silt.cancelLearn();
        else
            silt.startLearn();
    };
    addAndMakeVisible (learnButton);

    // UNDO / REDO: knob, menu, button and modulation changes (also Cmd/Ctrl+Z,
    // Shift+Cmd/Ctrl+Z, which some hosts keep for themselves).
    undoButton.onClick = [this] { stepUndo (false); };
    redoButton.onClick = [this] { stepUndo (true); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // ROUTES: every modulation route in one list, from any page.
    routesButton.onClick = [this]
    {
        if (routeList->isVisible())
            routeList->onClose();
        else
            showRoutes (-1);
    };
    addAndMakeVisible (routesButton);

    if constexpr (SiltProcessor::isInstrument)
    {
        addAndMakeVisible (monitorButton);
        monitorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "monitor", monitorButton);
    }

    setWantsKeyboardFocus (true);
    const int w = silt.editorWidth.load(), h = silt.editorHeight.load();   // before the limits resize us
    setResizable (true, true);
    setResizeLimits (860, 520, 1600, 1000);
    setSize (w > 0 ? juce::jlimit (860, 1600, w) : defaultWidth, h > 0 ? juce::jlimit (520, 1000, h) : defaultHeight);

    const int startPage = juce::jlimit (0, (int) pageTabs.size() - 1, silt.currentPage.load());
    pageTabs[(size_t) startPage].setToggleState (true, juce::dontSendNotification);
    showPage (startPage);
    startTimerHz (30);
}

SiltEditor::~SiltEditor()
{
    setLookAndFeel (nullptr);
}

// One control from the page table, attached to its parameter. Menus and
// buttons in slots or under them; actions and the banner do their own thing.
void SiltEditor::buildControl (const layout::Entry& entry)
{
    auto c = std::make_unique<Control>();
    c->entry = &entry;
    using Kind = layout::Kind;
    using APVTS = juce::AudioProcessorValueTreeState;

    switch (entry.kind)
    {
        case Kind::knob:
        {
            c->slider = std::make_unique<FineSlider>();
            auto& s = *c->slider;
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
            s.setComponentID (entry.id);   // seeds this knob's wear
            // Set on the slider itself: its text box is made before it joins the
            // editor, so it never sees the panel look's colours.
            s.setColour (juce::Slider::textBoxTextColourId, lcdText);
            s.setColour (juce::Slider::textBoxBackgroundColourId, lcdGreen);
            s.setColour (juce::Slider::textBoxOutlineColourId, ink);
            s.setColour (juce::Slider::textBoxHighlightColourId, rust.withAlpha (0.4f));
            c->sliderAttachment = std::make_unique<APVTS::SliderAttachment> (silt.params, entry.id, s);
            if (auto* parameter = silt.params.getParameter (entry.id))
                s.setDoubleClickReturnValue (true, s.proportionOfLengthToValue (parameter->getDefaultValue()));
            wireModulation (s);
            break;
        }
        case Kind::menu:
        case Kind::underMenu:
            c->menu = std::make_unique<juce::ComboBox>();
            c->menu->addItemList (entry.items, 1);
            c->menuAttachment = std::make_unique<APVTS::ComboBoxAttachment> (silt.params, entry.id, *c->menu);
            c->menu->onChange = [this] { refreshLayout(); };
            break;
        case Kind::button:
        case Kind::underButton:
            c->button = std::make_unique<juce::TextButton> (entry.text);
            c->button->setClickingTogglesState (true);
            c->buttonAttachment = std::make_unique<APVTS::ButtonAttachment> (silt.params, entry.id, *c->button);
            c->button->onClick = [this] { refreshLayout(); };
            break;
        case Kind::action:
            c->button = std::make_unique<juce::TextButton> (entry.text);
            if (entry.id == "!newPattern")
                c->button->onClick = [this] { silt.newPattern(); };
            else if (entry.id == "!assign")
            {
                c->button->setTooltip ("Arms the shown modulator for the controller: turn a knob on any page to set its depth");
                c->button->onClick = [this]
                {
                    const int shown = silt.modShown.load();
                    selectModulator (shown, true);
                };
            }
            break;
        case Kind::banner:
            c->banner = std::make_unique<ModBanner> (silt, modActions);
            break;
    }

    addChildComponent (c->component());
    controls.push_back (std::move (c));
}

// A knob's modulation hooks: Alt-drag depth, chip drops, right-click routes.
void SiltEditor::wireModulation (FineSlider& s)
{
    s.target = silt.modulationTarget (s.getComponentID());
    if (s.target < 0)
        return;
    const int target = s.target;
    s.depthModulator = [this, target]
    {
        std::array<float, SiltProcessor::numModulators> amounts {};
        for (int m = 0; m < SiltProcessor::numModulators; ++m)
            amounts[(size_t) m] = silt.routeAmount (m, target);
        return depthModulatorFor (silt.armedMod.load(), amounts, lastUsedMod);
    };
    s.amountOf = [this, target] (int m) { return silt.routeAmount (m, target); };
    s.routeGesture = [this] { silt.beginRouteGesture(); };
    s.setAmount = [this, target, &s] (int m, float amount)
    {
        silt.editRoutes ([&] { silt.setRouteAmount (m, target, amount); });
        lastUsedMod = m;
        s.repaint();
    };
    s.depthDragged = [this, target] (int m, bool dragging)
    {
        depthReadout = dragging ? "MOD " + juce::String (m + 1) + " > " + silt.targetParameter (target).getName (16).toUpperCase()
                                      + "  " + silt.depthText (m, target)
                                : juce::String();
        repaint (header);
    };
    s.showMenu = [this, &s] { showKnobMenu (s); };
    s.togglePolarity = [this, target, &s] (int m)
    {
        if (silt.routeAmount (m, target) == 0.0f)
            return;
        silt.beginRouteGesture();
        silt.editRoutes ([&] { silt.setRoutePolarity (m, target, silt.routeIsBipolar (m, target) ? SiltProcessor::unipolar : SiltProcessor::bipolar); });
        s.repaint();
    };
    s.ringHover = [this, target] (int m)
    {
        // Over a modulatable knob, the LCD says what the mouse can do there.
        juce::String hint;
        if (m >= 0 && silt.routeAmount (m, target) != 0.0f)
            hint = "MOD " + juce::String (m + 1) + ": DRAG RING FOR DEPTH, SHIFT+ALT-CLICK " + (silt.routeIsBipolar (m, target) ? "+" : juce::String::charToString ((juce::juce_wchar) 0x00b1));
        else if (m >= 0)
            hint = "MOD " + juce::String (m + 1) + ": DRAG THIS RING TO MODULATE";
        else if (m == -1)
        {
            bool routed = false;
            for (int other = 0; other < SiltProcessor::numModulators; ++other)
                routed = routed || silt.routeAmount (other, target) != 0.0f;
            hint = routed ? juce::String ("DRAG A COLOURED RING FOR ITS DEPTH")
                 : silt.armedMod.load() >= 0 ? juce::String()
                 : juce::String ("DROP A MOD CHIP HERE, OR ARM ONE AND DRAG THE RING");
        }
        if (hint != hoverHint)
        {
            hoverHint = hint;
            repaint (header);
        }
    };
}

// Right-click on a knob: each modulator's depth on it, in its colour, with
// quick depths to pick (or remove), then clear-all and the route list.
void SiltEditor::showKnobMenu (FineSlider& s)
{
    static constexpr float depths[] { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
    static const char* types[] { "WAVE", "RANDOM", "ENV", "FOLLOW", "MIDI", "MACRO" };
    const int target = s.target;
    juce::PopupMenu menu;
    bool any = false;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
    {
        const float amount = silt.routeAmount (m, target);
        juce::PopupMenu depthsMenu;
        for (size_t i = 0; i < std::size (depths); ++i)
            depthsMenu.addItem (100 * (m + 1) + (int) i, (depths[i] > 0 ? "+" : "") + juce::String (juce::roundToInt (depths[i] * 100.0f)) + "%",
                                true, juce::approximatelyEqual (amount, depths[i]));
        if (amount != 0.0f)
        {
            depthsMenu.addSeparator();
            const bool bipolar = silt.routeIsBipolar (m, target);
            depthsMenu.addItem (100 * (m + 1) + 97, juce::String::charToString ((juce::juce_wchar) 0x00b1) + " Bipolar: both ways round the knob", true, bipolar);
            depthsMenu.addItem (100 * (m + 1) + 98, "+ Unipolar: one way", true, ! bipolar);
            depthsMenu.addSeparator();
            depthsMenu.addItem (100 * (m + 1) + 99, "Remove");
            any = true;
        }
        juce::PopupMenu::Item item ("MOD " + juce::String (m + 1) + "  " + types[(int) silt.readModulatorParams (m).type] + "   "
                                    + (amount != 0.0f ? silt.depthText (m, target).upToFirstOccurrenceOf ("  ", false, false) : juce::String ("add")));
        item.subMenu = std::make_unique<juce::PopupMenu> (std::move (depthsMenu));
        item.colour = modColour (m).base;
        menu.addItem (std::move (item));
    }
    menu.addSeparator();
    menu.addItem (1, "Clear all modulation on this knob", any);
    menu.addItem (2, "Show all routes...");
    menu.addItem (3, "Undo " + silt.undoDescription().toLowerCase(), silt.canUndoRoutes());
    menu.addItem (4, "Redo " + silt.redoDescription().toLowerCase(), silt.canRedoRoutes());

    juce::Component::SafePointer<FineSlider> safe (&s);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&s), [this, target, safe] (int chosen)
    {
        if (chosen == 1 || chosen >= 100)
            silt.beginRouteGesture();
        if (chosen == 1)
            silt.editRoutes ([&] { silt.clearRoutesOnTarget (target); });
        else if (chosen == 2)
            showRoutes (-1);
        else if (chosen == 3)
            stepUndo (false);
        else if (chosen == 4)
            stepUndo (true);
        else if (chosen >= 100)
        {
            const int m = chosen / 100 - 1, i = chosen % 100;
            silt.editRoutes ([&]
            {
                if (i == 97 || i == 98)
                    silt.setRoutePolarity (m, target, i == 97 ? SiltProcessor::bipolar : SiltProcessor::unipolar);
                else
                    silt.setRouteAmount (m, target, i == 99 ? 0.0f : depths[i]);
            });
            lastUsedMod = m;
        }
        if (safe != nullptr)
            safe->repaint();
    });
}

// One step back or forward, with a word in the LCD about what changed.
void SiltEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  silt.loadFile (fc.getResult());
                          });
}

void SiltEditor::stepUndo (bool redo)
{
    const auto what = redo ? silt.redoDescription() : silt.undoDescription();
    if (redo ? silt.redoRoutes() : silt.undoRoutes())
    {
        flash = (redo ? "REDO: " : "UNDO: ") + what;
        flashUntil = juce::Time::getMillisecondCounter() + 1500;
        repaint();
    }
}

void SiltEditor::selectModulator (int m, bool arm)
{
    silt.modShown.store (m);
    if (arm)
        silt.armedMod.store (silt.armedMod.load() == m ? -1 : m);   // arming the armed one disarms
    refreshLayout();
    repaint (header);
}

bool SiltEditor::routesShown() const
{
    return routeList->isVisible();
}

void SiltEditor::showRoutes (int modulatorFilter)
{
    routeList->setCurrentPage (layout::pageName (page));
    routeList->setFilter (modulatorFilter);
    routeList->setVisible (true);
    routeList->toFront (false);
    routesButton.setToggleState (true, juce::dontSendNotification);
}

// Each route is an arc round the knob from its value to how far the modulator
// can take it (both ways for bipolar sources), in the modulator's colour and
// texture, numbered at its far end; a stop at the end shows where the range
// clamps it. A dot shows where modulation has the value now. Higher values are
// anticlockwise: the skirt turns so the value sits under the marker.
void SiltEditor::drawModulation (juce::Graphics& g, juce::Slider& slider, juce::Point<float> centre, float radius, float position)
{
    auto* fine = dynamic_cast<FineSlider*> (&slider);
    if (fine == nullptr || fine->target < 0)
        return;
    const int target = fine->target;
    const auto angleFor = [position] (float value) { return juce::degreesToRadians ((position - value) * degreesPerUnit); };
    const auto ringRadius = [radius] (int ring) { return radius + 3.0f * (float) ring; };
    auto& parameter = silt.targetParameter (target);
    const bool stepped = silt.isSteppedTarget (target);
    const auto quantise = [&] (float v)
    {
        return stepped ? parameter.convertTo0to1 (std::round (parameter.convertFrom0to1 (v))) : v;
    };

    const auto drawArc = [&] (int m, float lo, float hi, int ring, float alpha)
    {
        juce::Path arc, stroke;
        const float r = ringRadius (ring);
        arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleFor (lo), angleFor (hi), true);
        const juce::PathStrokeType type (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
        const auto dashes = modDashes (m);
        if (dashes.empty())
            type.createStrokedPath (stroke, arc);
        else
            type.createDashedStroke (stroke, arc, dashes.data(), (int) dashes.size());
        g.setColour (modColour (m).base.withAlpha (alpha));
        g.fillPath (stroke);
    };

    int ring = 0;
    bool anyLive = false;
    const int armed = silt.armedMod.load();
    fine->ringCentre = centre;
    fine->ringBase = radius;
    fine->ringModulators.clear();
    fine->armedHintModulator = -1;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
    {
        const float amount = silt.routeAmount (m, target);
        if (amount == 0.0f || ! silt.isModulatorEnabled (m))
            continue;   // muted: its ring is hidden (its routes stay)
        anyLive = true;
        // Which way it moves first: towards position + amount (up for a positive
        // depth, down for a negative one). That half is solid, with an arrow and
        // the number at its end; a bipolar route's other half is faint.
        const bool bipolar = silt.routeIsBipolar (m, target);
        const float rawFirst = position + amount, rawOther = bipolar ? position - amount : position;
        const float rawLo = juce::jmin (rawFirst, rawOther), rawHi = juce::jmax (rawFirst, rawOther);
        const float lo = quantise (juce::jlimit (0.0f, 1.0f, rawLo)), hi = quantise (juce::jlimit (0.0f, 1.0f, rawHi));
        const float first = quantise (juce::jlimit (0.0f, 1.0f, rawFirst)), other = quantise (juce::jlimit (0.0f, 1.0f, rawOther));
        const float alpha = armed == m || fine->hoverRing == ring ? 1.0f : 0.85f;
        if (bipolar)
            drawArc (m, juce::jmin (position, other), juce::jmax (position, other), ring, alpha * 0.35f);
        drawArc (m, juce::jmin (position, first), juce::jmax (position, first), ring, alpha);
        {
            // The arrowhead, pointing on round the ring the way the value goes.
            const float rr = ringRadius (ring), at = angleFor (first);
            const float onward = (amount >= 0.0f ? -1.0f : 1.0f) * 0.13f;   // higher values are anticlockwise
            juce::Path arrow;
            arrow.addTriangle (centre.getPointOnCircumference (rr - 3.2f, at), centre.getPointOnCircumference (rr + 3.2f, at),
                               centre.getPointOnCircumference (rr, at + onward));
            g.setColour (modColour (m).base.withAlpha (alpha));
            g.fillPath (arrow);
        }
        if (fine->hoverRing == ring)   // the ring under the mouse: a grip along it
        {
            juce::Path grip;
            const float rr = ringRadius (ring);
            grip.addCentredArc (centre.x, centre.y, rr, rr, 0.0f, angleFor (lo), angleFor (hi), true);
            g.setColour (modColour (m).base.brighter (0.4f).withAlpha (0.6f));
            g.strokePath (grip, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        fine->ringModulators.push_back (m);

        const float r = ringRadius (ring);
        // Clamped ends get a stop across the ring.
        for (const auto& [raw, end] : { std::pair<float, float> { rawLo, lo }, std::pair<float, float> { rawHi, hi } })
            if (raw < 0.0f || raw > 1.0f)
            {
                g.setColour (modColour (m).base);
                g.drawLine (juce::Line<float> (centre.getPointOnCircumference (r - 3.0f, angleFor (end)),
                                               centre.getPointOnCircumference (r + 3.0f, angleFor (end))), 1.6f);
            }
        // The number where it heads first: never colour alone.
        const float far = first;
        g.setColour (modColour (m).base.darker (0.3f));
        g.setFont (monoFont (8.0f));
        g.drawText (juce::String (m + 1), juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre.getPointOnCircumference (r + 6.0f, angleFor (far))),
                    juce::Justification::centred);
        ++ring;
    }

    // A chip held over the knob: where a +25% route would go.
    if (fine->dropHover >= 0 && silt.routeAmount (fine->dropHover, target) == 0.0f)
        drawArc (fine->dropHover, position, juce::jlimit (0.0f, 1.0f, position + 0.25f), ring, 0.6f);
    // Armed: a faint full ring on every knob it can reach but doesn't yet.
    else if (armed >= 0 && silt.routeAmount (armed, target) == 0.0f)
    {
        fine->armedHintModulator = armed;
        juce::Path circle, stroke;
        const float r = ringRadius (ring);
        circle.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleFor (0.0f), angleFor (1.0f), true);
        const float dashes[] { 3.0f, 2.5f };
        juce::PathStrokeType (1.8f).createDashedStroke (stroke, circle, dashes, 2);
        g.setColour (modColour (armed).base.withAlpha (0.8f));
        g.fillPath (stroke);
    }

    if (! anyLive)
        return;

    // Per-voice modulation on a grain parameter: a small pip per sounding voice,
    // just inside the rings, where that voice's own modulation has it.
    bool perVoiceRoute = false;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
        perVoiceRoute = perVoiceRoute || (silt.isModulatorPerVoice (m) && silt.isModulatorEnabled (m) && silt.routeAmount (m, target) != 0.0f);
    if (perVoiceRoute && silt.isVoiceTarget (target))
        for (int v = 0; v < silt::Engine::numVoices; ++v)
            if (silt.voiceShown[(size_t) v].load())
            {
                const auto pip = centre.getPointOnCircumference (ringRadius (0) - 4.0f, angleFor (silt.voiceModulated01[(size_t) v][(size_t) target].load()));
                g.setColour (bone.withAlpha (0.9f));
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (pip));
            }

    const auto dot = centre.getPointOnCircumference (ringRadius (0), angleFor (silt.modulatedValue01 (target)));
    g.setColour (bone);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
    g.setColour (ink);
    g.drawEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot), 1.0f);
}

void SiltEditor::showPage (int p)
{
    page = juce::jlimit (0, layout::numPages - 1, p);
    if (routeList->isVisible())
        routeList->onClose();   // choosing a page (tab or controller) goes to it
    updateVisibility();
}

void SiltEditor::refreshLayout()
{
    updateVisibility();
}

// Shows this page's controls: for each slot, the alternative the page table
// picks from the current parameter values.
void SiltEditor::updateVisibility()
{
    const auto context = silt.layoutContext();
    bool changed = false;
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto& c = *controls[i];
        const bool show = c.entry->page == page && c.entry->isShown (context);
        if (show && c.entry->dimmed != nullptr)
            c.component().setAlpha (c.entry->dimmed (context) ? 0.35f : 1.0f);
        if (show != shownNow[i] || c.component().isVisible() != show)
        {
            c.component().setVisible (show);
            shownNow[i] = show;
            changed = true;
        }
    }
    if (changed)
        repaint (pagePanel);
}

juce::String SiltEditor::visibleParameterId (int p, int slot) const
{
    for (const auto& c : controls)
        if (c->entry->page == p && c->entry->slot == slot && c->entry->inSlot() && c->component().isVisible())
            return c->entry->id;
    return {};
}

juce::Slider* SiltEditor::findKnob (const juce::String& id)
{
    for (auto& c : controls)
        if (c->slider != nullptr && c->entry->id == id)
            return c->slider.get();
    return nullptr;
}

juce::Rectangle<int> SiltEditor::slotBounds (int slot) const
{
    const int w = controlRow.getWidth() / slotsPerRow;
    return { controlRow.getX() + slot * w, controlRow.getY(), w, controlRow.getHeight() };
}

bool SiltEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && silt.isAudioFile (juce::File (files[0]));
}

void SiltEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (! files.isEmpty())
        silt.loadFile (juce::File (files[0]));
    repaint();
}

// 1–4 arm a modulator (again: disarm), Esc disarms and closes the route list.
// Not while a value is being typed.
bool SiltEditor::keyPressed (const juce::KeyPress& key)
{
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;
    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '4' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        selectModulator ((int) (c - '1'), true);
        return true;
    }
    // Cmd/Ctrl+Z undoes, with Shift redoes.
    if (key.getKeyCode() == 'Z' && key.getModifiers().isCommandDown())
    {
        stepUndo (key.getModifiers().isShiftDown());
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        silt.armedMod.store (-1);
        if (routeList->isVisible())
            routeList->onClose();
        repaint (header);
        return true;
    }
    return false;
}

juce::String SiltEditor::statusText() const
{
    if (depthReadout.isNotEmpty())
        return depthReadout;
    if (flash.isNotEmpty() && juce::Time::getMillisecondCounter() < flashUntil)
        return flash;
    if (hoverHint.isNotEmpty())
        return hoverHint;

    if (const int step = silt.learnStep.load(); step >= 0)
        return step < SiltProcessor::slotsPerPage ? "LEARN: TURN KNOB " + juce::String (step + 1)
             : step == 8 ? juce::String ("LEARN: PRESS PREVIOUS PAGE") : juce::String ("LEARN: PRESS NEXT PAGE");

    if (const int m = silt.armedMod.load(); m >= 0)
        return "MOD " + juce::String (m + 1) + " ARMED: DRAG A KNOB'S OUTER RING";

    using S = SiltProcessor::CaptureState;
    switch (silt.captureState.load())
    {
        case S::armed: return silt.hostPlaying.load() ? "ARMED: WAITING FOR NEXT BAR" : "ARMED: STARTING";
        case S::recording: return "REC " + juce::String (juce::roundToInt (silt.captureProgress.load() * 100.0f)) + "%";
        case S::done:
        case S::cancelRequested:
        case S::cancelled:
        case S::idle: break;
    }

    if (silt.lastLoadError.isNotEmpty())
        return silt.lastLoadError;

    juce::String source;
    switch (silt.captureSource.load())
    {
        case SiltProcessor::CaptureSource::sidechain: source = "SC"; break;
        case SiltProcessor::CaptureSource::mainInput: source = "IN"; break;
        case SiltProcessor::CaptureSource::none: source = "NO INPUT"; break;
    }
    const auto level = levelDb <= -60.0f ? juce::String ("-INF") : juce::String (juce::roundToInt (levelDb));
    return source + " " + level + " DB   " + juce::String (silt.hostBpm.load(), 1) + " BPM";
}

void SiltEditor::timerCallback()
{
    const auto oldStatus = statusText();
    const float db = juce::Decibels::gainToDecibels (silt.captureMeter.takePeak(), -100.0f);
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);

    if (const int generation = silt.getSampleGeneration(); generation != seenGeneration)
    {
        seenGeneration = generation;
        screen->setOverview (silt.getOverview());
    }
    const auto [regionStart, regionLength] = silt.shownRegion();
    screen->setRegion (regionStart, regionLength);
    screen->setTransientsShown (param ("snap") > 1.5f);
    screen->setDisplayGain (juce::Decibels::decibelsToGain (param ("sampleGain")));

    // The grains: the screen only repaints while something is moving.
    auto& views = viewsScratch;
    const int n = silt.readGrainViews (views);
    std::array<float, silt::Engine::numVoices> heads {};
    int voices = 0;
    for (size_t i = 0; i < heads.size(); ++i)
    {
        heads[i] = silt.voicePlayheads[i].load();
        voices += heads[i] >= 0.0f ? 1 : 0;
    }

    juce::String hud;
    if (n > 0)
        hud = juce::String (n) + (n == 1 ? " GRAIN   " : " GRAINS   ") + juce::String (voices) + (voices == 1 ? " VOICE" : " VOICES");
    const bool live = silt.liveMode.load();
    const bool frozen = param ("freeze") > 0.5f;
    if (live)
        screen->setLive (true, frozen, silt.liveWriteShare.load(), silt.getLivePeaks());
    else
        screen->setLive (false, false, 0.0f, {});
    const bool moved = screen->setActivity (views, n, heads, hud);
    if (moved || (live && ! frozen) || live != screenWasLive)
        screen->repaint();
    screenWasLive = live;

    // The controller can change page; learning shows its progress.
    if (const int controllerPage = silt.currentPage.load(); controllerPage != page)
    {
        pageTabs[(size_t) controllerPage].setToggleState (true, juce::dontSendNotification);
        showPage (controllerPage);
    }
    if (const int step = silt.learnStep.load(); step != learnSeen)
    {
        learnSeen = step;
        learnButton.setToggleState (step >= 0, juce::dontSendNotification);
        repaint (header);
        repaint (pagePanel);
    }

    // Parameters (automation, the controller, the rail) can switch which
    // alternative a slot shows.
    updateVisibility();

    // Modulation: the rail's meters, the header stripes, the knobs' live dots;
    // arming changes every knob's hint ring.
    rail->update();
    if (routeList->isVisible())
        routeList->update();
    const int armed = silt.armedMod.load(), shown = silt.modShown.load();
    if (armed != armedSeen || shown != modShownSeen)
    {
        armedSeen = armed;
        modShownSeen = shown;
        for (auto& c : controls)
        {
            if (c->slider != nullptr && c->slider->isVisible())
                c->slider->repaint();
            else if (c->banner != nullptr)
                c->banner->repaint();
            else if (c->entry->id == "!assign")
                c->button->setToggleState (armed >= 0 && armed == shown, juce::dontSendNotification);
        }
        repaint (header);
        repaint (pagePanel.withHeight (32));   // the MOD tab's underline
    }
    for (auto& c : controls)
        if (c->slider != nullptr && c->slider->isVisible() && c->slider->target >= 0)
            for (int m = 0; m < SiltProcessor::numModulators; ++m)
                if (silt.routeAmount (m, c->slider->target) != 0.0f)
                {
                    c->slider->repaint();
                    break;
                }
    // The screen's corner picture: redrawn when what it shows changes.
    if (auto key = insetKey (page, silt, *rail); key != insetSeen)
    {
        insetSeen = std::move (key);
        screen->repaintInset();
    }

    std::array<float, SiltProcessor::numModulators> stripes {};
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
        stripes[(size_t) m] = silt.modOutputs[(size_t) m].load();
    if (stripes != stripesSeen)
    {
        stripesSeen = stripes;
        repaint (stripesArea);
    }

    if (silt.presetName != presetShown)
    {
        presetShown = silt.presetName;
        presetBar->refresh();
    }

    undoButton.setEnabled (silt.canUndoRoutes());
    redoButton.setEnabled (silt.canRedoRoutes());

    const bool capturing = silt.captureState.load() != SiltProcessor::CaptureState::idle;
    armButton.setButtonText (capturing ? "CANCEL" : juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ARM")));

    if (statusText() != oldStatus)
        repaint (header);

    // Redraw the SHAPE preview only when its values change.
    if (page == layout::shape)
    {
        const std::array<float, 7> now { param ("fade"), param ("slant"), param ("ease"), param ("attack"),
                                         param ("decay"), param ("sustain"), param ("release") };
        if (now != previewSeen)
        {
            previewSeen = now;
            repaint (pagePanel);
        }
    }
}

// Fixed bands top and bottom (header, strip, page panel); the screen takes
// what's left, so the window can be resized.
void SiltEditor::resized()
{
    silt.editorWidth.store (getWidth());
    silt.editorHeight.store (getHeight());

    auto r = getLocalBounds().reduced (14);
    header = r.removeFromTop (44);
    stripesArea = header.withTrimmedLeft (130).withWidth (180).withSizeKeepingCentre (180, 19);
    lcdArea = header.withTrimmedLeft (header.getWidth() - juce::jmin (420, header.getWidth() * 2 / 5)).reduced (8, 7);
    presetBar->setBounds (juce::Rectangle<int>::leftTopRightBottom (stripesArea.getRight() + 16, header.getY(), lcdArea.getX() - 12, header.getBottom())
                              .withSizeKeepingCentre (juce::jmin (330, lcdArea.getX() - 12 - stripesArea.getRight() - 16), 26));
    r.removeFromTop (10);

    pagePanel = r.removeFromBottom (192);
    r.removeFromBottom (10);
    strip = r.removeFromBottom (34);
    r.removeFromBottom (10);
    screen->setBounds (r);

    auto row = strip;
    armButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (8);
    captureBars.setBounds (row.removeFromLeft (104).reduced (0, 4));
    row.removeFromLeft (8);
    loadButton.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (8);
    learnButton.setBounds (row.removeFromLeft (90));
    if constexpr (SiltProcessor::isInstrument)
    {
        monitorButton.setBounds (row.removeFromRight (104).reduced (0, 4));
        row.removeFromRight (8);
    }
    routesButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    redoButton.setBounds (row.removeFromRight (60));
    row.removeFromRight (4);
    undoButton.setBounds (row.removeFromRight (60));

    auto panel = pagePanel.reduced (2);
    auto tabs = panel.removeFromTop (28);
    const int tabWidth = tabs.getWidth() / (int) pageTabs.size();
    for (auto& tab : pageTabs)
        tab.setBounds (tabs.removeFromLeft (tabWidth).reduced (3, 3));
    panel.removeFromTop (2);
    railArea = panel.removeFromLeft (railWidth).reduced (6, 6);
    rail->setBounds (railArea);
    controlRow = panel;
    routeList->setBounds (controlRow.reduced (2, 0));

    for (auto& c : controls)
    {
        const auto& e = *c->entry;
        if (! e.inSlot())
        {
            c->component().setBounds (slotBounds (e.slot).removeFromBottom (20).withSizeKeepingCentre (e.width, 18).translated (e.dx, 0));
            continue;
        }
        auto cell = slotBounds (e.slot).withTrimmedTop (18).withTrimmedBottom (22).reduced (juce::jmin (10, slotBounds (0).getWidth() / 12), 0);
        if (c->menu != nullptr)
            c->menu->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24));
        else if (c->button != nullptr)
            c->button->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 12, 34));
        else if (c->banner != nullptr)
            c->banner->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 56));
        else
            c->slider->setBounds (cell);
    }
}

void SiltEditor::paintPreview (juce::Graphics& g)
{
    // A rule between the grain's envelope (slots 0–2) and the note's (3–6).
    g.setColour (ink);
    g.fillRect (slotBounds (3).getX() - 1, controlRow.getY() + 6, 2, controlRow.getHeight() - 12);

    // Slot 7: one grain's envelope above, one note's ADSR below.
    auto cell = slotBounds (slotsPerRow - 1).reduced (10, 8).toFloat();
    const auto drawCurve = [&g] (juce::Rectangle<float> box, const juce::Path& curve, const char* caption)
    {
        g.setColour (ink);
        g.fillRect (box);
        g.setColour (lcdGreen);
        g.strokePath (curve, juce::PathStrokeType (1.5f));
        g.setFont (monoFont (9.0f));
        g.drawText (caption, box.reduced (4, 2), juce::Justification::topLeft);
    };
    const auto plot = [] (juce::Rectangle<float> box) { return box.reduced (5.0f, 6.0f).withTrimmedTop (8.0f); };

    const auto grainBox = cell.removeFromTop (cell.getHeight() * 0.5f - 3.0f);
    cell.removeFromTop (6.0f);
    const auto noteBox = cell;

    juce::Path grainCurve;
    {
        const auto area = plot (grainBox);
        silt::HannWindow hann;
        silt::GrainEnvelope env;
        env.set (param ("fade") * 0.01, param ("slant") * 0.01, param ("ease") * 0.01, 1000, 0.0);
        for (int i = 0; i <= 100; ++i)
        {
            const float t = (float) i / 100.0f;
            const juce::Point<float> pt (area.getX() + t * area.getWidth(), area.getBottom() - env (hann, t) * area.getHeight());
            if (i == 0)
                grainCurve.startNewSubPath (pt);
            else
                grainCurve.lineTo (pt);
        }
    }

    juce::Path noteCurve;
    {
        // Attack, decay and release on a shared time scale, with a fixed sustain hold.
        const auto area = plot (noteBox);
        const float a = param ("attack"), d = param ("decay"), s = param ("sustain") * 0.01f, rel = param ("release");
        const float hold = 0.25f * (a + d + rel) + 1.0f;
        const float total = a + d + hold + rel;
        const auto at = [&] (float ms, float level)
        {
            return juce::Point<float> (area.getX() + ms / total * area.getWidth(), area.getBottom() - level * area.getHeight());
        };
        noteCurve.startNewSubPath (at (0.0f, 0.0f));
        noteCurve.lineTo (at (a, 1.0f));
        noteCurve.lineTo (at (a + d, s));
        noteCurve.lineTo (at (a + d + hold, s));
        noteCurve.lineTo (at (total, 0.0f));
    }

    drawCurve (grainBox, grainCurve, "GRAIN");
    drawCurve (noteBox, noteCurve, "NOTE");
}

// The header's four stripes are the four modulators, as meters: a dim track,
// filled from the left (or the centre, for bipolar sources) with each one's
// live value. Muted ones stay dim.
void SiltEditor::paintStripes (juce::Graphics& g)
{
    auto stripes = stripesArea;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
    {
        auto bar = stripes.removeFromTop (4).toFloat();
        stripes.removeFromTop (1);
        const auto& colour = modColour (m);
        g.setColour (colour.dim.withAlpha (0.55f));
        g.fillRect (bar);
        if (! silt.isModulatorEnabled (m))
            continue;
        const float v = juce::jlimit (-1.0f, 1.0f, silt.modOutputs[(size_t) m].load());
        g.setColour (colour.base);
        if (silt::Modulator::isBipolar (silt.readModulatorParams (m)))
        {
            const float mid = bar.getCentreX(), x = mid + v * bar.getWidth() * 0.5f;
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), bar.getY(), juce::jmax (mid, x) + 1.0f, bar.getBottom()));
        }
        else
            g.fillRect (bar.withWidth (juce::jmax (1.0f, bar.getWidth() * juce::jmax (0.0f, v))));
    }
}

void SiltEditor::paint (juce::Graphics& g)
{
    g.fillAll (chassis);

    // Header band: name, stripes, LCD.
    g.setColour (ink);
    g.fillRect (header);
    g.setColour (bone);
    g.setFont (panelFont (20.0f));
    g.drawText (juce::String (JucePlugin_Name).toUpperCase(), header.withTrimmedLeft (16), juce::Justification::centredLeft);
    paintStripes (g);

    const auto lcd = lcdArea;
    g.setColour (juce::Colour (0xff14110f));
    g.fillRect (lcd);
    g.setColour (lcdGreen);
    g.fillRect (lcd.reduced (3));
    g.setColour (lcdText);
    g.setFont (monoFont (14.0f));
    g.drawFittedText (statusText(), lcd.reduced (12, 0), juce::Justification::centredLeft, 1, 0.8f);

    // Page panel: a ruled grid of cells (UI inspiration: data sheets, diagnostics).
    g.setColour (ink);
    g.drawRect (pagePanel, 2);
    g.drawHorizontalLine (pagePanel.getY() + 30, (float) pagePanel.getX(), (float) pagePanel.getRight());
    g.setColour (ink.withAlpha (0.4f));
    g.drawVerticalLine (controlRow.getX(), (float) controlRow.getY() + 4.0f, (float) controlRow.getBottom() - 4.0f);
    g.setColour (ink.withAlpha (0.25f));
    for (int s = 1; s < slotsPerRow; ++s)
        g.drawVerticalLine (slotBounds (s).getX(), (float) controlRow.getY() + 6.0f, (float) controlRow.getBottom() - 6.0f);

    g.setColour (ink);
    g.setFont (panelFont (10.5f));
    for (auto& c : controls)
        if (c->entry->inSlot() && c->component().isVisible())
            g.drawText (c->entry->label, slotBounds (c->entry->slot).withHeight (18).translated (0, 2), juce::Justification::centred);

    if (page == layout::shape)
        paintPreview (g);

    // While learning, outline the slot whose knob to turn.
    if (const int step = silt.learnStep.load(); step >= 0 && step < SiltProcessor::slotsPerPage)
    {
        g.setColour (rust);
        g.drawRect (slotBounds (step).reduced (2), 3);
    }
}

void SiltEditor::paintOverChildren (juce::Graphics& g)
{
    // While a modulator is armed, the MOD tab carries its colour.
    if (const int armed = silt.armedMod.load(); armed >= 0)
    {
        const auto tab = pageTabs[(size_t) layout::mod].getBounds();
        g.setColour (modColour (armed).base);
        g.fillRect (tab.getX() + 2, tab.getBottom() - 3, tab.getWidth() - 4, 3);
    }
    if (dragHover)
    {
        g.setColour (rust);
        g.drawRect (getLocalBounds(), 4);
    }
}
