#include "PluginEditor.h"
#include "ChainView.h"
#include "GrainScreen.h"
#include "Grip.h"
#include "Help.h"
#include "ModColours.h"
#include "PanelLook.h"
#include "Presets.h"
#include "RandomAmountBox.h"
#include "Settings.h"
#include "ScreenInsets.h"

using namespace silt::ui;
namespace layout = silt::layout;

namespace
{
constexpr int defaultWidth = 1040, defaultHeight = 600;
constexpr int slotsPerRow = 8;
constexpr float degreesPerUnit = 270.0f;   // a knob's travel, from about 7 o'clock to 5
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
    applyTheme (startTheme >= 0 ? startTheme : settingsFile()->getIntValue ("theme", defaultTheme));
    grainView = startGrainView >= -1 ? startGrainView : settingsFile()->getIntValue ("grainView", -1);
    applyGrainLook();
    look->applyColours();
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
    screen->drawStrip = [this] (juce::Graphics& g, juce::Rectangle<float> area) { return page == layout::space && drawSpaceTimeline (g, area, silt); };
    if (auto* gain = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter ("sampleGain")))
    {
        sampleGainBox = std::make_unique<SampleGainBox> (*gain, [this] { return silt.normalisingGainDb(); });
        screen->addAndMakeVisible (*sampleGainBox);
    }

    // DRAG: the sound out to the DAW, as a WAV kept in Music/Silt/Takes (a
    // file that stays, so the clip it becomes doesn't lose its audio).
    dragOut = std::make_unique<DragOutHandle>();
    dragOut->onDragOut = [this]
    {
        const auto name = juce::File::createLegalFileName (silt.audioName());
        const auto file = SiltProcessor::takesFolder().getNonexistentChildFile (name, ".wav", false);
        if (silt.writeAudioFile (file))
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, dragOut.get());
    };
    screen->addAndMakeVisible (*dragOut);

    // SLICE: move, add and remove slices on the waveform (MANUAL from then on).
    // Each drag, add or remove is one undo step.
    screen->onSliceDrag = [this] (bool starting) { starting ? silt.beginSliceEdit() : silt.endSliceEdit(); };
    screen->onSliceMove = [this] (int index, float share) { silt.moveSlice (index, share); screen->setSlices (silt.sliceMarks()); };
    screen->onSliceAdd = [this] (float share)
    {
        silt.beginSliceEdit();
        silt.addSlice (share);
        silt.endSliceEdit();
        screen->setSlices (silt.sliceMarks());
    };
    screen->onSliceRemove = [this] (int index)
    {
        silt.beginSliceEdit();
        silt.removeSlice (index);
        silt.endSliceEdit();
        screen->setSlices (silt.sliceMarks());
    };

    // Right-click the screen: the sample tools.
    screen->onMenu = [this]
    {
        const bool hasSample = silt.getOverview().seconds > 0.0;
        const bool live = silt.liveMode.load();
        juce::PopupMenu menu;
        menu.addSectionHeader ("SAMPLE");
        menu.addItem (1, "Trim to the region", hasSample && ! live);
        menu.addItem (2, "Reverse", hasSample && ! live);
        menu.addItem (3, "Normalise to -1 dB", hasSample && ! live);
        menu.addSeparator();
        menu.addItem (4, live ? "Save the last 8 seconds as WAV..." : "Save as WAV...", hasSample || live);
        menu.addItem (5, "Show the takes folder");
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this] (int chosen)
        {
            if (chosen >= 1 && chosen <= 3)
                silt.editSample (chosen == 1 ? SiltProcessor::SampleEdit::trimToRegion
                                 : chosen == 2 ? SiltProcessor::SampleEdit::reverse : SiltProcessor::SampleEdit::normalise);
            else if (chosen == 4)
            {
                chooser = std::make_unique<juce::FileChooser> ("Save as WAV", SiltProcessor::takesFolder().getChildFile (juce::File::createLegalFileName (silt.audioName()) + ".wav"), "*.wav");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          if (fc.getResult() != juce::File())
                                              silt.writeAudioFile (fc.getResult().withFileExtension ("wav"));
                                      });
            }
            else if (chosen == 5)
            {
                SiltProcessor::takesFolder().createDirectory();
                SiltProcessor::takesFolder().revealToUser();
            }
        });
    };

    for (const auto& entry : layout::entries (SiltProcessor::isInstrument))
        buildControl (entry);
    shownNow.assign (controls.size(), false);

    // CHAIN: the module order, over the screen's lower part; a click opens a module's page.
    chainView = std::make_unique<ChainView> (silt);
    chainView->onOpen = [this] (int module)
    {
        static constexpr int pages[] { layout::resonator, layout::filter, layout::color, layout::space };
        const int target = pages[juce::jlimit (0, 3, module)];
        pageTabs[(size_t) target].setToggleState (true, juce::dontSendNotification);
        silt.currentPage.store (target);
        showPage (target);
    };
    addChildComponent (*chainView);

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
        // Right-click (or Ctrl-click): that page's RESET menu. The key itself
        // ignores it, so it neither presses nor changes the page.
        tab.onMenu = [this, i]
        {
            juce::PopupMenu::dismissAllActiveMenus();   // one menu at a time
            juce::PopupMenu menu;
            menu.addItem (1, "Reset " + juce::String (layout::pageName ((int) i)) + ((int) i == layout::mod ? " " + juce::String (silt.modShown.load() + 1) : juce::String()));
            menu.addItem (2, "Reset all pages");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pageTabs[i]), [this, i] (int chosen)
            {
                if (chosen > 0)
                    resetPages (chosen == 2 ? -1 : (int) i);
            });
        };
        tab.onClick = [this, i]
        {
            if (pageTabs[i].getToggleState())
            {
                // RESONATOR again: its other half.
                if ((int) i == layout::resonator && page == layout::resonator)
                    silt.resonatorHalf.store (1 - silt.resonatorHalf.load());
                silt.currentPage.store ((int) i);   // the controller's knobs follow the page
                showPage ((int) i);
            }
        };
        if ((int) i == layout::resonator)
            tab.setTooltip ("RESONATOR has two pages of controls: click the tab again for the other. Right-click: reset");
        else
            tab.setTooltip ("Right-click: reset this page or all of them");
        addAndMakeVisible (tab);
    }

    // Capture and loading.
    captureBars.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    addAndMakeVisible (captureBars);
    captureBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "captureBars", captureBars);
    resampleButton.setClickingTogglesState (true);
    for (auto* key : { &resampleButton, &learnButton })
    {
        key->getProperties().set ("led", true);   // switches: a light shows they're on
        key->setColour (juce::TextButton::textColourOnId, ink);
    }
    resampleButton.setTooltip ("RESAMPLE: ARM records what Silt itself is playing (from the next bar) as the new sample");
    addAndMakeVisible (resampleButton);
    resampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "captureFeed", resampleButton);

    armButton.getProperties().set ("red", true);
    armButton.setColour (juce::TextButton::textColourOffId, onFrame);
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

    // RANDOM: the open page's controls, by the amount beside it; right-click
    // (or Ctrl-click) for amounts and what to leave out.
    randomButton.setTooltip ("Randomise this page's controls. Right-click: amount, what to leave out, and reset");
    randomButton.onClick = [this]
    {
        if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
            showRandomMenu();
        else
            randomise();
    };
    addAndMakeVisible (randomButton);
    randomAmountBox = std::make_unique<RandomAmountBox> (silt);
    addAndMakeVisible (*randomAmountBox);

    // UNDO / REDO: knob, menu, button and modulation changes (also Cmd/Ctrl+Z,
    // Shift+Cmd/Ctrl+Z, which some hosts keep for themselves).
    undoButton.onClick = [this] { stepUndo (false); };
    redoButton.onClick = [this] { stepUndo (true); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // MOD ROUTES: every modulation route in one list, from any page.
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

    makeGrips();

    // Hover help: every part says what it is; the LCD shows it under the mouse.
    hoverHelpOn = settingsFile()->getBoolValue ("hoverHelp", true);
    {
        const std::initializer_list<std::pair<juce::Component*, const char*>> parts {
            { &armButton, "!arm" }, { &captureBars, "!bars" }, { &resampleButton, "!resample" }, { &loadButton, "!load" },
            { &learnButton, "!learn" }, { &randomButton, "!random" }, { randomAmountBox.get(), "!randomAmount" },
            { &undoButton, "!undo" }, { &redoButton, "!redo" }, { &routesButton, "!routes" }, { &monitorButton, "!monitor" },
            { &tourButton, "!tour" }, { screen.get(), "!screen" }, { dragOut.get(), "!drag" }, { rail.get(), "!chip" },
            { presetBar.get(), "!preset" }, { screenGrip.get(), "!grip" }, { railGrip.get(), "!grip" }, { insetGrip.get(), "!grip" },
        };
        for (const auto& [part, id] : parts)
            if (part != nullptr)
                part->getProperties().set ("help", id);
        if (sampleGainBox != nullptr)
            sampleGainBox->getProperties().set ("help", "!gain");
        for (auto& tab : pageTabs)
            tab.getProperties().set ("help", "!tab");
    }
    helpWatcher.onHover = [this] (const juce::String& id) { showHelpFor (id); };
    addMouseListener (&helpWatcher, true);
    screen->setView (startZoom.x, startZoom.y);

    setWantsKeyboardFocus (true);
    const int w = silt.editorWidth.load(), h = silt.editorHeight.load();   // before the limits resize us
    setResizable (true, true);
    setResizeLimits (860, 520, 1600, 1000);
    setSize (w > 0 ? juce::jlimit (860, 1600, w) : defaultWidth, h > 0 ? juce::jlimit (520, 1000, h) : defaultHeight);

    const int startPage = juce::jlimit (0, (int) pageTabs.size() - 1, silt.currentPage.load());
    pageTabs[(size_t) startPage].setToggleState (true, juce::dontSendNotification);
    showPage (startPage);

    // The tour: ? in the header, and once by itself the first time.
    tourButton.setTooltip ("The tour, and the colour theme");
    tourButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Take the tour");
        menu.addSectionHeader ("THEME");
        for (size_t i = 0; i < themes().size(); ++i)
            menu.addItem (10 + (int) i, themes()[i].name, true, (int) i == currentThemeIndex());
        static const char* looks[] { "Dots", "Stars", "Shore", "Film", "Clouds" };
        menu.addItem (2, "Hover help in the display", true, hoverHelpOn);
        menu.addSectionHeader ("GRAIN VIEW");
        menu.addItem (30, "Match the theme", true, grainView < 0);
        for (int i = 0; i < (int) std::size (looks); ++i)
            menu.addItem (31 + i, looks[i], true, grainView == i);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tourButton), [this] (int chosen)
        {
            if (chosen == 1)
                startTour();
            else if (chosen == 2)
            {
                hoverHelpOn = ! hoverHelpOn;
                auto file = settingsFile();
                file->setValue ("hoverHelp", hoverHelpOn);
                file->saveIfNeeded();
                repaint (lcdArea);
            }
            else if (chosen >= 30)
            {
                grainView = chosen - 31;   // −1: match the theme
                auto file = settingsFile();
                file->setValue ("grainView", grainView);
                file->saveIfNeeded();
                applyGrainLook();
            }
            else if (chosen >= 10)
                setTheme (chosen - 10);
        });
    };
    addAndMakeVisible (tourButton);
    tour = std::make_unique<Tour>();
    tour->showPage = [this] (int target)
    {
        pageTabs[(size_t) target].setToggleState (true, juce::dontSendNotification);
        silt.currentPage.store (target);
        showPage (target);
    };
    tour->onFinish = [this] { tour->showPage (pageBeforeTour); };
    addChildComponent (*tour);
    if (tourOnFirstOpen && ! Tour::seen())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SiltEditor> (this)]
        {
            if (safe != nullptr && safe->isShowing())
                safe->startTour();
        });

    startTimerHz (30);
}

SiltEditor::~SiltEditor()
{
    removeMouseListener (&helpWatcher);
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
            styleValueBox (s);
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

    c->component().getProperties().set ("help", entry.id);
    addChildComponent (c->component());
    controls.push_back (std::move (c));
}

// A knob's modulation hooks: Alt-drag depth, chip drops, right-click routes.
void SiltEditor::wireModulation (FineSlider& s)
{
    s.target = silt.modulationTarget (s.getComponentID());
    s.showMenu = [this, &s] { showKnobMenu (s); };   // every knob: RANDOM's exclude, and routes on targets
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
        // Over the knob itself (m == −1) there's no ring hint: the LCD shows
        // the knob's own hover help instead.
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
    const auto id = s.getComponentID();
    juce::PopupMenu menu;
    bool any = false;
    for (int m = 0; m < SiltProcessor::numModulators && target >= 0; ++m)
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
    if (target >= 0)
    {
        menu.addSeparator();
        menu.addItem (1, "Clear all modulation on this knob", any);
        menu.addItem (2, "Show all routes...");
    }
    menu.addSeparator();
    menu.addItem (5, "Leave out of RANDOM", true, silt.isRandomExcluded (id));
    menu.addSeparator();
    menu.addItem (3, "Undo " + silt.undoDescription().toLowerCase(), silt.canUndoRoutes());
    menu.addItem (4, "Redo " + silt.redoDescription().toLowerCase(), silt.canRedoRoutes());

    juce::Component::SafePointer<FineSlider> safe (&s);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&s), [this, target, id, safe] (int chosen)
    {
        if (chosen == 5)
        {
            silt.setRandomExcluded (id, ! silt.isRandomExcluded (id));
            repaint (pagePanel);
            return;
        }
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

// RESONATOR, FILTER, COLOR: the scope taps the module, reads ~21 ms past the latest rising zero
// crossing, and draws what goes in against what comes out.
void SiltEditor::updateScope()
{
    const int module = page == layout::resonator ? SiltProcessor::moduleResonator
                     : page == layout::filter    ? SiltProcessor::moduleFilter
                     : page == layout::color     ? SiltProcessor::moduleColor
                                                 : -1;
    silt.scopeModule.store (module);
    if (module < 0)
    {
        screen->hideScope();
        screenShowsScope = false;
        return;
    }
    scopeIn.resize (4096);
    scopeOut.resize (4096);
    const unsigned written = silt.readScope (scopeIn.data(), scopeOut.data(), (int) scopeIn.size());
    if (written == scopeSeen && screenShowsScope)
        return;
    scopeSeen = written;
    screenShowsScope = true;
    screen->setScope (scopeIn, scopeOut, juce::roundToInt (silt.getSampleRate() > 0.0 ? silt.getSampleRate() * 0.021 : 1008.0),
                      SiltProcessor::moduleName (module));
}

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

void SiltEditor::setTheme (int index)
{
    applyTheme (index);
    {
        auto file = settingsFile();
        file->setValue ("theme", currentThemeIndex());
        file->saveIfNeeded();
    }
    restyle();
}

// Colours set on parts when they were made: set again for the current theme.
void SiltEditor::restyle()
{
    look->applyColours();
    look->clearKnobCache();
    armButton.setColour (juce::TextButton::textColourOffId, onFrame);
    for (auto* key : { &resampleButton, &learnButton })
        key->setColour (juce::TextButton::textColourOnId, ink);
    for (auto& c : controls)
        if (c->slider != nullptr)
            styleValueBox (*c->slider);
    routeList->restyle();
    screen->setOverview (silt.getOverview());   // the waveform is cached in the old colours
    backdrop = {};
    screen->restyle();
    applyGrainLook();
    sendLookAndFeelChange();
    repaint();
}

void SiltEditor::showHelpFor (const juce::String& id)
{
    const auto text = id.isEmpty() ? juce::String() : helpFor (id);
    if (text != hoverHelp)
    {
        hoverHelp = text;
        repaint (lcdArea);
    }
}

// The grain field's look: the one chosen in the ? menu, or the theme's own.
void SiltEditor::applyGrainLook()
{
    const int chosen = grainView >= 0 ? grainView : themes()[(size_t) currentThemeIndex()].grainLook;
    screen->setFieldLook ((GrainScreen::FieldLook) juce::jlimit (0, 4, chosen));
}

void SiltEditor::styleValueBox (juce::Slider& s)
{
    s.setColour (juce::Slider::textBoxTextColourId, lcdText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, lcdGreen);
    s.setColour (juce::Slider::textBoxOutlineColourId, ink);
    s.setColour (juce::Slider::textBoxHighlightColourId, rust.withAlpha (0.4f));
}

// The tiled sections' grips. Each drag works from the shares at its start,
// so the section follows the mouse; the processor keeps them for the session.
void SiltEditor::makeGrips()
{
    const auto start = [this]
    {
        layoutAtDragStart = { silt.layoutWell.load(), silt.layoutRail.load(), silt.layoutField.load(), silt.layoutInset.load() };
    };
    const auto relayout = [this] { resized(); repaint(); };

    screenGrip = std::make_unique<Grip> (Grip::Corner::bottomRight, juce::MouseCursor::UpDownResizeCursor, true);
    screenGrip->setTooltip ("Drag to share the height between the screen and the knobs; double-click to reset");
    screenGrip->onStart = start;
    screenGrip->onDrag = [this, relayout] (juce::Point<int> d)
    {
        silt.layoutWell.store (juce::jlimit (0.1f, 0.9f, layoutAtDragStart.well - (float) d.y / (float) juce::jmax (1, flexibleHeight)));
        relayout();
    };
    screenGrip->onReset = [this] { resetLayout (0); };

    railGrip = std::make_unique<Grip> (Grip::Corner::bottomRight, juce::MouseCursor::LeftRightResizeCursor, false);
    railGrip->setTooltip ("Drag to share the width between the modulators and the knobs; double-click to reset");
    railGrip->onStart = start;
    railGrip->onDrag = [this, relayout] (juce::Point<int> d)
    {
        silt.layoutRail.store (juce::jlimit (0.1f, 0.4f, layoutAtDragStart.rail + (float) d.x / (float) juce::jmax (1, contentWidth)));
        relayout();
    };
    railGrip->onReset = [this] { resetLayout (1); };

    insetGrip = std::make_unique<Grip> (Grip::Corner::topLeft, juce::MouseCursor::TopLeftCornerResizeCursor, true);
    insetGrip->setTooltip ("Drag to size the corner picture and the grain field; double-click to reset");
    insetGrip->onStart = start;
    insetGrip->onDrag = [this, relayout] (juce::Point<int> d)
    {
        const float width = (float) juce::jmax (1, screen->getWidth() - 60), height = (float) juce::jmax (1, screen->getHeight() - 80);
        silt.layoutInset.store (juce::jlimit (0.15f, 0.7f, layoutAtDragStart.inset - (float) d.x / width));
        silt.layoutField.store (juce::jlimit (0.15f, 0.85f, layoutAtDragStart.field - (float) d.y / height));
        relayout();
    };
    insetGrip->onReset = [this] { resetLayout (2); };

    for (auto* grip : { screenGrip.get(), railGrip.get(), insetGrip.get() })
        addAndMakeVisible (grip);
}

void SiltEditor::resetLayout (int grip)
{
    const auto d = SiltProcessor::defaultLayout();
    if (grip == 0)
        silt.layoutWell.store (d.well);
    else if (grip == 1)
        silt.layoutRail.store (d.rail);
    else
    {
        silt.layoutField.store (d.field);
        silt.layoutInset.store (d.inset);
    }
    resized();
    repaint();
}

void SiltEditor::startTour()
{
    if (routeList->isVisible())
        routeList->onClose();
    pageBeforeTour = page;
    tour->start (tourSteps());
}

// What the tour says, where it points, and what it asks you to try. The
// TRY IT checks compare with how things were when the tour began.
std::vector<Tour::Step> SiltEditor::tourSteps()
{
    using Spots = juce::RectangleList<int>;
    const auto boundsOf = [] (std::initializer_list<const juce::Component*> parts)
    {
        juce::Rectangle<int> r;
        for (const auto* c : parts)
            r = r.isEmpty() ? c->getBounds() : r.getUnion (c->getBounds());
        return r;
    };
    const int generation = silt.getSampleGeneration();
    const float spray = param ("spray"), bits = param ("colorBits"), order = param ("chainOrder");
    const auto routes = silt.allRoutes().size();
    const auto preset = silt.presetName;

    std::vector<Tour::Step> steps;
    steps.push_back ({ "Welcome to Silt",
                       "Silt cuts a sound into tiny pieces, called grains, and plays lots of them at once, overlapping. "
                       "Change how long the grains are, how many there are and where in the sound they come from, and one "
                       "sample turns into pads, textures, rhythms or drones.\n\nThis tour takes a minute or two. The lit part "
                       "of each step is live, so try things as you go.", {}, -1, {}, {} });
    steps.push_back ({ "The screen",
                       "This is your sample. While you play, each white line is a grain sounding right now, and the shaded "
                       "band is the part of the sample grains are taken from.",
                       [this] { return Spots (screen->getBounds()); }, -1,
                       "Play a note or a chord on your keyboard (or a MIDI clip) and watch the grains appear.",
                       [this] { return silt.activeGrains.load() > 0; } });
    if constexpr (SiltProcessor::isInstrument)
        steps.push_back ({ "Your own sounds",
                           "LOAD opens an audio file, or one of the built-in samples. You can also drop a file on the screen, "
                           "or press ARM to record from your DAW's sidechain input (for the number of bars beside it).",
                           [this, boundsOf] { return Spots (boundsOf ({ &armButton, &loadButton })); }, -1,
                           "Click LOAD and pick a different factory sample.",
                           [this, generation] { return silt.getSampleGeneration() != generation; } });
    else
        steps.push_back ({ "Your own sounds",
                           "Silt FX turns the audio on its track into grains as it plays (MODE: LIVE on the PLAY page). "
                           "To play a sample instead, LOAD one or drop a file on the screen, set MODE to PITCH and play notes.",
                           [this, boundsOf] { return Spots (boundsOf ({ &armButton, &loadButton })); }, -1,
                           "Click LOAD and pick a factory sample.",
                           [this, generation] { return silt.getSampleGeneration() != generation; } });
    steps.push_back ({ "Pages",
                       "Each tab is a page of up to eight controls, the same eight your MIDI controller's knobs move. GRAIN and "
                       "SHAPE make the grains, PITCH and PLAY decide how notes play them, and the rest are effects and movement.",
                       [this, boundsOf] { return Spots (boundsOf ({ &pageTabs.front(), &pageTabs.back() })); }, layout::grain,
                       "Click another tab to look around.",
                       [this] { return page != layout::grain; } });
    steps.push_back ({ "Spray",
                       "SPRAY scatters each grain to a random spot near where it would play, so the sound blurs and shimmers. "
                       "Drag a knob up or down to turn it, hold Shift for fine moves, and double-click to put it back.",
                       [this] { Spots s (screen->getBounds()); s.add (slotBounds (3)); return s; }, layout::grain,
                       "Drag SPRAY up while you play, and watch the grains spread out across the waveform.",
                       [this, spray] { return std::abs (param ("spray") - spray) > 5.0f; } });
    steps.push_back ({ "Slices",
                       "MODE: SLICE cuts the sample into pieces, one per key: C1 plays the first, C#1 the next, and keys "
                       "past the last stay quiet. Drag a slice line to move it, double-click to add or remove one. Scroll "
                       "on the waveform to zoom in, or drag the ZOOM label up and down; double-click it to see it all.",
                       [this]
                       {
                           Spots s (screen->getBounds());
                           for (const auto& c : controls)
                               if (c->entry->id == "keyMode" && c->component().isVisible())
                                   s.add (c->component().getBounds());
                           return s;
                       },
                       layout::play,
                       "Set MODE to SLICE, play C1 and up, then drag a slice line.",
                       [this] { return juce::roundToInt (param ("keyMode")) == 3; } });
    steps.push_back ({ "See what you hear",
                       "RESONATOR, FILTER, COLOR and SPACE shape the sound after the grains. On the first three, the strip on "
                       "the screen shows the sound going in (faint) and coming out (bright).",
                       [this] { Spots s (screen->scopeArea().translated (screen->getX(), screen->getY())); s.add (slotBounds (2)); return s; },
                       layout::color,
                       "Play a note and turn BITS down: the bright line breaks into steps as the sound gets crunchier.",
                       [this, bits] { return param ("colorBits") < bits - 1.0f; } });
    steps.push_back ({ "Chain",
                       "The effects run in this order. Moving one changes the sound: grit added after the reverb is harsher "
                       "than grit the reverb smooths over.",
                       [this] { return Spots (chainView->getBounds()); }, layout::chain,
                       "Drag COLOR to the end of the chain. Click a block to open its page.",
                       [this, order] { return ! juce::approximatelyEqual (param ("chainOrder"), order); } });
    steps.push_back ({ "Movement",
                       "These four are modulators: waves, random, envelopes and more. Connect one to a knob and the knob moves "
                       "by itself. Rings round a knob show what's moving it, and MOD ROUTES lists every connection.",
                       [this] { Spots s (railArea); s.add (controlRow); return s; }, layout::mod,
                       "Drag MOD 1 from the left onto any knob.",
                       [this, routes] { return silt.allRoutes().size() > routes; } });
    steps.push_back ({ "Happy accidents",
                       "RANDOM shakes up the page that's open. The box beside it sets how far, from a nudge to anything goes. "
                       "Right-click a knob to keep RANDOM away from it.",
                       [this, boundsOf] { return Spots (boundsOf ({ &randomButton, randomAmountBox.get(), &undoButton, &redoButton })); }, -1,
                       "Press RANDOM, listen, then press UNDO to go back.",
                       [this] { return silt.canRedoRoutes() || silt.undoDescription().startsWith ("RANDOM"); } });
    steps.push_back ({ "Sounds",
                       "Step through the factory presets with the arrows, or SAVE your own. A preset keeps every setting and the "
                       "modulation, but not the sample, so the same preset sounds new on each sample.",
                       [this] { return Spots (presetBar->getBounds()); }, -1,
                       "Click the right arrow to hear the next preset.",
                       [this, preset] { return silt.presetName != preset; } });
    steps.push_back ({ "That's the tour",
                       "A good way in: pick a preset, play a chord, then press RANDOM at 25% on a page and keep what you like.\n\n"
                       "This ? button brings the tour back any time, and changes the colour theme.",
                       [this] { return Spots (tourButton.getBounds()); }, -1, {}, {} });
    return steps;
}

void SiltEditor::randomise()
{
    const int changed = silt.randomisePage (page);
    const juce::String name (layout::pageName (page));
    flash = changed > 0 ? "RANDOM " + name + ": " + juce::String (changed) + (changed == 1 ? " CHANGE" : " CHANGES")
                        : "RANDOM " + name + ": NOTHING TO CHANGE";
    flashUntil = juce::Time::getMillisecondCounter() + 1500;
    repaint();
}

// RESET: one page (or every page, −1) back to its defaults, as one undo step.
void SiltEditor::resetPages (int which)
{
    const int changed = which < 0 ? silt.resetAllPages() : silt.resetPage (which);
    const juce::String name = which < 0 ? juce::String ("ALL PAGES") : juce::String (layout::pageName (which));
    flash = changed > 0 ? "RESET " + name + ": " + juce::String (changed) + (changed == 1 ? " CHANGE" : " CHANGES")
                        : "RESET " + name + ": ALREADY AT DEFAULTS";
    flashUntil = juce::Time::getMillisecondCounter() + 1500;
    refreshLayout();
    repaint();
}

void SiltEditor::showRandomMenu()
{
    static constexpr int amounts[] { 10, 25, 50, 75, 100 };
    const auto entries = silt.randomisable (page);
    juce::PopupMenu menu, leaveOut;
    const int now = juce::roundToInt (silt.randomAmount.load() * 100.0f);
    menu.addSectionHeader ("AMOUNT");
    for (size_t i = 0; i < std::size (amounts); ++i)
        menu.addItem (1 + (int) i, juce::String (amounts[i]) + (amounts[i] <= 10 ? "%  nudge" : amounts[i] == 100 ? "%  anything goes" : "%"),
                      true, now == amounts[i]);
    for (size_t i = 0; i < entries.size(); ++i)
        leaveOut.addItem (100 + (int) i, entries[i]->label, true, silt.isRandomExcluded (entries[i]->id));
    leaveOut.addSeparator();
    leaveOut.addItem (10, "Leave all out");
    leaveOut.addItem (11, "Include all");
    menu.addSeparator();
    menu.addSubMenu ("Leave out on " + juce::String (layout::pageName (page)), leaveOut, ! entries.empty());
    menu.addSeparator();
    menu.addItem (20, "Reset " + juce::String (layout::pageName (page)) + (page == layout::mod ? " " + juce::String (silt.modShown.load() + 1) : juce::String()));
    menu.addItem (21, "Reset all pages");

    juce::PopupMenu::dismissAllActiveMenus();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomButton), [this, entries] (int chosen)
    {
        if (chosen == 20 || chosen == 21)
            resetPages (chosen == 21 ? -1 : page);
        else if (chosen >= 1 && chosen <= (int) std::size (amounts))
            randomAmountBox->set ((float) amounts[chosen - 1] / 100.0f);
        else if (chosen == 10 || chosen == 11)
            for (const auto* e : entries)
                silt.setRandomExcluded (e->id, chosen == 10);
        else if (chosen >= 100 && chosen - 100 < (int) entries.size())
        {
            const auto& id = entries[(size_t) chosen - 100]->id;
            silt.setRandomExcluded (id, ! silt.isRandomExcluded (id));
        }
        repaint (pagePanel);
    });
}

// One step back or forward, with a word in the LCD about what changed.
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
// clamps it. A dot shows where modulation has the value now. The scale is
// fixed, as the knob's ticks are: higher values clockwise.
void SiltEditor::drawModulation (juce::Graphics& g, juce::Slider& slider, juce::Point<float> centre, float radius, float position)
{
    auto* fine = dynamic_cast<FineSlider*> (&slider);
    if (fine == nullptr || fine->target < 0)
        return;
    const int target = fine->target;
    juce::ignoreUnused (position);
    const auto angleFor = [] (float value) { return juce::degreesToRadians (-0.5f * degreesPerUnit + value * degreesPerUnit); };
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
    chainView->setVisible (page == layout::chain);
    if (insetGrip != nullptr)
        insetGrip->setVisible (pageHasInset (page));   // only where there's a corner picture to size
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

// A slot's label, knob (with its value box) and the small control under it,
// kept together and centred in the column: the knob as big as the column's
// width or the well's height allows, the text and value box their own size.
SiltEditor::SlotParts SiltEditor::slotParts (int slot) const
{
    constexpr int labelHeight = 18, valueBox = 20, underRow = 24;
    const auto column = slotBounds (slot);
    const int knob = juce::jlimit (40, 170, juce::jmin (column.getWidth() - 16, column.getHeight() - labelHeight - valueBox - underRow - 8));
    auto block = column.withSizeKeepingCentre (column.getWidth(), juce::jmin (column.getHeight(), labelHeight + knob + valueBox + underRow));
    SlotParts parts;
    parts.label = block.removeFromTop (labelHeight).translated (0, 2);
    parts.under = block.removeFromBottom (underRow);
    parts.control = block.reduced (juce::jmin (10, column.getWidth() / 12), 0);
    return parts;
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

    if (const int step = silt.learnStep.load(); step >= 0)
        return step < SiltProcessor::slotsPerPage ? "LEARN: TURN KNOB " + juce::String (step + 1)
             : step == 8 ? juce::String ("LEARN: PRESS PREVIOUS PAGE") : juce::String ("LEARN: PRESS NEXT PAGE");

    // Under the mouse: a modulation ring's hint, else the part's own help.
    // Both outrank the standing messages below, which come back on leaving.
    if (hoverHint.isNotEmpty())
        return hoverHint;
    if (hoverHelpOn && hoverHelp.isNotEmpty())
        return hoverHelp;

    if (const int m = silt.armedMod.load(); m >= 0)
        return "MOD " + juce::String (m + 1) + " ARMED: DRAG A KNOB'S OUTER RING";

    using S = SiltProcessor::CaptureState;
    switch (silt.captureState.load())
    {
        case S::armed: return juce::String (param ("captureFeed") > 0.5f ? "RESAMPLE " : "") + (silt.hostPlaying.load() ? "ARMED: WAITING FOR NEXT BAR" : "ARMED: STARTING");
        case S::recording: return juce::String (param ("captureFeed") > 0.5f ? "RESAMPLE " : "") + "REC " + juce::String (juce::roundToInt (silt.captureProgress.load() * 100.0f)) + "%";
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
    screen->setSlices (silt.sliceMarks());
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
        screen->setLive (true, frozen, silt.liveWriteShare.load(), silt.getLivePeaks(), juce::roundToInt (param ("keyMode")) == 2);
    else
        screen->setLive (false, false, 0.0f, {});
    const bool moved = screen->setActivity (views, n, heads, hud);
    if (moved || (live && ! frozen) || live != screenWasLive)
        screen->repaint();
    screenWasLive = live;

    // The live scope, on pages whose module it shows: only redrawn when new audio arrived.
    updateScope();
    if (page == layout::space)
    {
        // SPACE's timeline: redrawn as the trace grows (and when the settings move, below).
        const unsigned trace = silt.spaceTraceCount.load();
        if (trace != spaceTraceSeen || page != spacePageSeen)
            screen->repaintStrip();
        spaceTraceSeen = trace;
    }
    if (page != spacePageSeen)
        screen->repaintStrip();
    spacePageSeen = page;
    if (chainView->isVisible())
        chainView->update();

    // RESONATOR's tab says which half is showing.
    if (const int half = silt.resonatorHalf.load(); half != resonatorHalfSeen)
    {
        resonatorHalfSeen = half;
        pageTabs[(size_t) layout::resonator].setButtonText (juce::String (layout::pageName (layout::resonator)) + (half == 0 ? " 1/2" : " 2/2"));
        if (page == layout::resonator)
            screen->repaintInset();
    }

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
        if (page == layout::space)
            screen->repaintStrip();
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

// One grid for the whole panel: a left column as wide as the mod rail, then
// the 8 knob columns; every row (header, strip, knobs) sits on those edges.
// Top to bottom: the header, the screen, the strip, the page keys' tray and
// the knob well. The header, strip and tray keep their height; the screen and
// the well share the rest, as their grip sets (text stays its size, knobs grow).
void SiltEditor::resized()
{
    silt.editorWidth.store (getWidth());
    silt.editorHeight.store (getHeight());
    backdrop = {};
    constexpr int gap = 8, stripHeight = 32, trayHeight = 36, minWell = 150, minScreen = 170, minRail = 150;

    auto r = getLocalBounds().reduced (14);
    contentWidth = r.getWidth();
    const int railWidth = juce::jlimit (minRail, juce::jmax (minRail, contentWidth * 2 / 5), juce::roundToInt ((float) contentWidth * silt.layoutRail.load()));
    const int columnsX = r.getX() + railWidth + gap;
    const int columnWidth = (r.getRight() - columnsX) / slotsPerRow;
    const auto column = [&] (juce::Rectangle<int> row, int first, int span = 1)
    {
        return row.withX (columnsX + first * columnWidth).withWidth (span * columnWidth);
    };

    header = r.removeFromTop (44);
    r.removeFromTop (gap);
    flexibleHeight = r.getHeight() - stripHeight - trayHeight - 3 * gap;
    const int wellHeight = juce::jlimit (minWell, juce::jmax (minWell, flexibleHeight - minScreen),
                                         juce::roundToInt ((float) flexibleHeight * silt.layoutWell.load()));
    wellArea = r.removeFromBottom (wellHeight);
    r.removeFromBottom (gap);
    tray = r.removeFromBottom (trayHeight);
    r.removeFromBottom (gap);
    strip = r.removeFromBottom (stripHeight);
    r.removeFromBottom (gap);
    screen->setSplit (silt.layoutField.load(), silt.layoutInset.load());
    screen->setBounds (r);
    chainView->setBounds (screen->rollArea().translated (r.getX(), r.getY()));
    pagePanel = tray.getUnion (wellArea);

    // Header: the name and stripes over the left column, presets over
    // columns 1–4, ? and the LCD over 5–8.
    stripesArea = header.withX (header.getX() + 70).withWidth (juce::jmax (40, railWidth - 82)).withSizeKeepingCentre (juce::jmax (40, railWidth - 82), 19);
    presetBar->setBounds (column (header, 0, 4).reduced (4, 0).withSizeKeepingCentre (column (header, 0, 4).getWidth() - 8, 26));
    lcdArea = column (header, 4, 4).reduced (4, 7);
    tourButton.setBounds (lcdArea.removeFromLeft (30).withSizeKeepingCentre (28, 28));
    lcdArea.removeFromLeft (8);
    lcdArea.removeFromRight (4);

    // The strip, on the grid: getting sound in over the left column, then a
    // column (or two) per control. Keys sit 4 px in from their column's edges.
    {
        auto left = strip.withWidth (railWidth);
        armButton.setBounds (left.removeFromLeft (juce::jmin (96, left.getWidth() * 45 / 100)).withTrimmedBottom (3));
        left.removeFromLeft (6);
        captureBars.setBounds (left.reduced (0, 3));
        const auto key = [&] (juce::Component& c, int first, int span = 1) { c.setBounds (column (strip, first, span).reduced (4, 0).withTrimmedBottom (3)); };
        key (resampleButton, 0);
        key (loadButton, 1);
        key (learnButton, 2);
        auto random = column (strip, 3, 2).reduced (4, 0).withTrimmedBottom (3);
        randomAmountBox->setBounds (random.removeFromRight (56).withTrimmedBottom (-3));
        random.removeFromRight (6);
        randomButton.setBounds (random);
        auto undoRedo = column (strip, 5).reduced (4, 0).withTrimmedBottom (3);
        undoButton.setBounds (undoRedo.removeFromLeft (undoRedo.getWidth() / 2 - 2));
        undoRedo.removeFromLeft (4);
        redoButton.setBounds (undoRedo);
        key (routesButton, 6);
        if constexpr (SiltProcessor::isInstrument)
            monitorButton.setBounds (column (strip, 7).reduced (6, 4));
    }

    // The page keys, in their own tray across the full width, in three groups
    // (sound, effects, movement): not on the knob columns, so a key never looks
    // like a label for the knob under it.
    {
        auto keys = tray.reduced (8, 5).withTrimmedBottom (3);
        constexpr int groupGap = 14, keyGap = 6;
        const int sizes[] { 4, 4, 2 };
        const int keyWidth = (keys.getWidth() - 2 * groupGap - ((int) pageTabs.size() - 3) * keyGap) / (int) pageTabs.size();
        size_t t = 0;
        for (int g = 0; g < 3; ++g)
        {
            for (int k = 0; k < sizes[g]; ++k, ++t)
            {
                pageTabs[t].setBounds (keys.removeFromLeft (keyWidth));
                if (k + 1 < sizes[g])
                    keys.removeFromLeft (keyGap);
            }
            keys.removeFromLeft (groupGap);
        }
    }

    // The well: the rail in the left column, the knobs one per column.
    railArea = wellArea.withWidth (railWidth).reduced (8, 8);
    rail->setBounds (railArea);
    controlRow = wellArea.withX (columnsX).withWidth (columnWidth * slotsPerRow).withTrimmedTop (4).withTrimmedBottom (2);
    routeList->setBounds (wellArea.withX (columnsX).withRight (wellArea.getRight()).reduced (2, 2));
    if (tour != nullptr && tour->isRunning())   // the first layout comes before the tour exists
        tour->setBounds (getLocalBounds());

    // Grips: the screen's bottom-right corner (screen ↔ well), the rail's
    // bottom-right corner (rail ↔ knob columns), the corner picture's top-left
    // corner (picture and grain field ↔ the rest of the screen).
    screenGrip->setBounds (juce::Rectangle<int> (16, 16).withPosition (screen->getRight() - 22, screen->getBottom() - 22));
    railGrip->setBounds (juce::Rectangle<int> (16, 16).withPosition (wellArea.getX() + railWidth - 14, wellArea.getBottom() - 16));
    insetGrip->setBounds (juce::Rectangle<int> (16, 16).withPosition (screen->getInsetTopLeft() + screen->getPosition()).translated (-2, -2));

    for (auto& c : controls)
    {
        const auto& e = *c->entry;
        const auto slot = slotParts (e.slot);
        if (! e.inSlot())
        {
            c->component().setBounds (slot.under.withSizeKeepingCentre (e.width, 18).translated (e.dx, 0));
            continue;
        }
        const auto cell = slot.control;
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

// A recess: darker at the top where the rim shades it, a light lip below.
void SiltEditor::drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    g.setGradientFill (juce::ColourGradient (chassis.darker (0.16f), 0.0f, area.getY(), chassis.darker (0.02f), 0.0f, area.getY() + area.getHeight() * 0.35f, false));
    g.fillRoundedRectangle (area, corner);
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.28f), 0.0f, area.getY(), juce::Colours::transparentBlack, 0.0f, area.getY() + 7.0f, false);
    g.setGradientFill (shade);
    g.fillRoundedRectangle (area.withHeight (8.0f), corner);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawHorizontalLine ((int) area.getBottom(), area.getX() + corner, area.getRight() - corner);
}

// A sunk display (the LCD, value boxes): shaded along its top edge.
void SiltEditor::drawSunk (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour face)
{
    g.setGradientFill (juce::ColourGradient (face.darker (0.12f), 0.0f, area.getY(), face.brighter (0.04f), 0.0f, area.getBottom(), false));
    g.fillRect (area);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0.0f, area.getY(), juce::Colours::transparentBlack, 0.0f, area.getY() + 4.0f, false));
    g.fillRect (area.withHeight (4.0f));
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
        g.setColour (lcdGreen.darker (0.8f));
        g.fillRect (box);
        drawSunk (g, box.reduced (1.0f), lcdGreen);
        g.setColour (lcdText);
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
    // What doesn't move (the lit chassis, the header band, the LCD's well, the
    // tray and the knob well) is drawn once per size and theme.
    if (backdrop.getWidth() != getWidth() || backdrop.getHeight() != getHeight())
    {
        backdrop = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
        juce::Graphics b (backdrop);
        b.setGradientFill (juce::ColourGradient (chassis.brighter (0.06f), 0.0f, 0.0f, chassis.darker (0.08f), 0.0f, (float) getHeight(), false));
        b.fillAll();
        const auto band = header.toFloat();
        b.setGradientFill (juce::ColourGradient (frame.brighter (0.2f), 0.0f, band.getY(), frame.darker (0.25f), 0.0f, band.getBottom(), false));
        b.fillRoundedRectangle (band, 5.0f);
        b.setColour (juce::Colours::white.withAlpha (0.1f));
        b.drawHorizontalLine ((int) band.getY() + 1, band.getX() + 4.0f, band.getRight() - 4.0f);
        b.setColour (juce::Colour (0xff0d0b0a));
        b.fillRoundedRectangle (lcdArea.toFloat(), 3.0f);
        drawSunk (b, lcdArea.reduced (3).toFloat(), lcdGreen);
        drawRecess (b, tray.toFloat(), 6.0f);
        drawRecess (b, wellArea.toFloat(), 8.0f);
    }
    g.drawImageAt (backdrop, 0, 0);
    g.setColour (onFrame);
    g.setFont (panelFont (20.0f));
    g.drawText (juce::String (JucePlugin_Name).toUpperCase(), header.withTrimmedLeft (16), juce::Justification::centredLeft);
    paintStripes (g);

    const auto lcd = lcdArea;
    g.setColour (lcdText);
    g.setFont (monoFont (14.0f));
    g.drawFittedText (statusText(), lcd.reduced (12, 0), juce::Justification::centredLeft, 1, 0.8f);

    g.setColour (ink.withAlpha (0.18f));
    g.drawVerticalLine (controlRow.getX() - 1, (float) wellArea.getY() + 10.0f, (float) wellArea.getBottom() - 10.0f);
    g.setColour (ink.withAlpha (0.1f));
    for (int s = 1; s < slotsPerRow; ++s)
        g.drawVerticalLine (slotBounds (s).getX(), (float) controlRow.getY() + 8.0f, (float) controlRow.getBottom() - 8.0f);

    g.setColour (ink);
    g.setFont (panelFont (10.5f));
    for (auto& c : controls)
        if (c->entry->inSlot() && c->component().isVisible())
        {
            const auto labelArea = slotParts (c->entry->slot).label;
            g.drawText (c->entry->label, labelArea, juce::Justification::centred);
            if (silt.isRandomExcluded (c->entry->id))
            {
                // Left out of RANDOM: a small crossed-out die after the label.
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), c->entry->label);
                const auto die = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) labelArea.getCentreX() + w * 0.5f + 9.0f, (float) labelArea.getCentreY() });
                g.setColour (inkSoft);
                g.drawRoundedRectangle (die, 1.5f, 1.0f);
                g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (die.getCentre()));
                g.setColour (rust);
                g.drawLine (die.getX() - 1.5f, die.getBottom() + 1.5f, die.getRight() + 1.5f, die.getY() - 1.5f, 1.3f);
                g.setColour (ink);
            }
        }

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
