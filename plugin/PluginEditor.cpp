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
#include "SpaceScene.h"

using namespace silt::ui;
namespace layout = silt::layout;

namespace
{
constexpr int defaultWidth = 1200, defaultHeight = 820;
constexpr int slotsPerRow = 8;
constexpr int minSideWidth = 240;      // the right column, as narrow as its panels still read
constexpr float maxSideShare = 0.5f;   // and as wide: half the panel
constexpr float degreesPerUnit = 270.0f;   // a knob's travel, from about 7 o'clock to 5
} // namespace

// --- FineSlider ------------------------------------------------------------------------

void SiltEditor::FineSlider::beginDepth (int modulator)
{
    if (routeGesture)
        routeGesture();
    depthModulatorNow = modulator;
    depthRunning = amountOf (modulator);
    depthPixels = 0.0f;
    if (depthDragged)
        depthDragged (modulator, true);
}

void SiltEditor::FineSlider::dragDepth (float pixelsUp, bool fine)
{
    if (depthModulatorNow < 0)
        return;
    // Each move adds its own change, at the rate Shift sets now, so pressing
    // or letting go of Shift mid-drag changes the speed without a jump.
    depthRunning = juce::jlimit (-1.0f, 1.0f, depthRunning + (pixelsUp - depthPixels) / (fine ? 1000.0f : 150.0f));
    depthPixels = pixelsUp;
    const float amount = std::abs (depthRunning) < 0.02f ? 0.0f : depthRunning;   // 0 is under a pixel wide otherwise
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

// Delete or Backspace on a clicked knob: its modulation goes (one undo step).
bool SiltEditor::FineSlider::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && clearModulation != nullptr && clearModulation())
        return true;
    return juce::Slider::keyPressed (key);
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
    // The value's own drag: Slider starts and ends it (the gesture, one undo
    // step); the moves are ours, so Shift can turn fine on or off mid-drag.
    dragLast = e.position;
    dragRunning = (float) valueToProportionOfLength (getValue());
    juce::Slider::mouseDown (e);
}

void SiltEditor::FineSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (isSettingDepth())
        dragDepth ((float) -e.getDistanceFromDragStartY(), e.mods.isShiftDown());
    else if (! e.mods.isPopupMenu() && isEnabled())
    {
        // Up or right raises it; 250 px from end to end, ten times finer with Shift (held now).
        const float pixels = (dragLast.y - e.position.y) + (e.position.x - dragLast.x);
        dragLast = e.position;
        dragRunning = juce::jlimit (0.0f, 1.0f, dragRunning + pixels / (e.mods.isShiftDown() ? 2500.0f : 250.0f));
        setValue (proportionOfLengthToValue (dragRunning), juce::sendNotificationSync);
    }
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
    // On a ring: that modulator comes off this knob, as a double-click resets anything else.
    if (const int m = modulatorAtRing (e.position); m >= 0 && m != armedHintModulator)
    {
        if (routeGesture)
            routeGesture();
        setAmount (m, 0.0f);
        repaint();
        return;
    }
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
    modActions.openPage = [this] (int m, bool wasShown) { openModPage (m, wasShown); };
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
    presetLibrary = std::make_unique<PresetManager> (silt);
    presetBar = std::make_unique<PresetBar> (*presetLibrary);
    addAndMakeVisible (*presetBar);
    presetBrowser = std::make_unique<PresetBrowser> (*presetLibrary, stock());
    addChildComponent (*presetBrowser);
    presetBar->onBrowse = [this] { presetBrowser->isVisible() ? presetBrowser->close (true) : presetBrowser->open(); };
    presetBrowser->onClosed = [this] { presetBar->refresh(); refreshLayout(); };
    screen->drawInset = [this] (juce::Graphics& g, juce::Rectangle<float> area) { return drawPageInset (g, area, page, silt, *rail); };
    // SPACE: the waveform adrift in a night sky (the scene fades in with DELAY and REVERB).
    screen->drawAroundWave = [this] (juce::Graphics& g, juce::Rectangle<int> scene, juce::Rectangle<int> wave, const std::vector<float>& envelope, bool over)
    {
        if (page != layout::space || ! SpaceScene::visible (spaceSettings))
            return false;
        if (over)
            spaceScene->drawOver (g, scene, wave, spaceSettings);
        else
            spaceScene->drawBehind (g, scene, wave, envelope, spaceSettings);
        return true;
    };
    // Silt FX has no sample: no GAIN, DRAG, LOAD, ARM or RESAMPLE.
    if (auto* gain = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter ("sampleGain")); gain != nullptr && SiltProcessor::isInstrument)
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
    if constexpr (SiltProcessor::isInstrument)
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
        if constexpr (SiltProcessor::isInstrument)
        {
            menu.addSectionHeader ("SAMPLE");
            menu.addItem (1, "Trim to the region", hasSample && ! live);
            menu.addItem (2, "Reverse", hasSample && ! live);
            menu.addItem (3, "Normalise to -1 dB", hasSample && ! live);
            menu.addSeparator();
        }
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

    // The chain, always on the panel; a click opens a module's page.
    chainView = std::make_unique<ChainView> (silt);
    chainView->onOpen = [this] (int module)
    {
        static constexpr int pages[] { layout::resonator, layout::filter, layout::color, layout::space };
        const int target = pages[juce::jlimit (0, 3, module)];
        if (target == layout::resonator && page == layout::resonator)
            silt.resonatorHalf.store (1 - silt.resonatorHalf.load());   // RES again: its other half
        pageTabs[(size_t) target].setToggleState (true, juce::dontSendNotification);
        silt.currentPage.store (target);
        showPage (target);
    };
    chainView->onMenu = [this] (int module)
    {
        static constexpr int pages[] { layout::resonator, layout::filter, layout::color, layout::space };
        pageTabs[(size_t) pages[juce::jlimit (0, 3, module)]].onMenu();
    };
    addAndMakeVisible (*chainView);

    // The Bench: SEND records what you hear and passes it on. An effect can send here,
    // too, and the instrument can take that as its sample. Silt FX has no sample to take into.
    benchStrip = std::make_unique<kit::bench::Strip> (stock(), SiltProcessor::isInstrument ? "I-01" : "I-01 FX", SiltProcessor::isInstrument ? "SILT" : "SILT FX", "SILT");
    benchStrip->reservedRight = 132;
    benchStrip->onTake = [this] { takeMoment(); };
    benchStrip->onSend = [this] { sendMoment(); };
    benchStrip->setTakeShown (SiltProcessor::isInstrument);
    addAndMakeVisible (*benchStrip);
    benchSource.addItem ("4 BARS OUT", 4);
    benchSource.addItem ("8 BARS OUT", 8);
    benchSource.addItem ("16 BARS OUT", 16);
    benchSource.addItem (SiltProcessor::isInstrument ? "THE SAMPLE" : "LAST 8 S IN", 1);
    benchSource.setSelectedId (4, juce::dontSendNotification);
    benchSource.setTitle ("What SEND sends");
    benchSource.setTooltip ("What SEND sends: the exact output for that many bars, from the next bar line");
    addAndMakeVisible (benchSource);
    updateBench();

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
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pageTabs[i].isVisible() ? static_cast<juce::Component*> (&pageTabs[i]) : chainView.get()), [this, i] (int chosen)
            {
                if (chosen > 0)
                    resetPages (chosen == 2 ? -1 : (int) i);
            });
        };
        tab.onClick = [this, i]
        {
            if (pageTabs[i].getToggleState())
            {
                silt.currentPage.store ((int) i);   // the controller's knobs follow the page
                showPage ((int) i);
            }
        };
        tab.setTooltip ("Right-click: reset this page or all of them");
        tab.getProperties().set ("flat", true);
        // The chain is always on the panel and its blocks open the effects' pages, so those
        // need no key; a controller can still page to them.
        if (pageHasKey ((int) i))
            addAndMakeVisible (tab);
        else
            addChildComponent (tab);
    }

    // Capture and loading.
    captureBars.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    if constexpr (SiltProcessor::isInstrument)
        addAndMakeVisible (captureBars);
    captureBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "captureBars", captureBars);
    resampleButton.setClickingTogglesState (true);
    for (auto* key : { &resampleButton, &learnButton })
    {
        key->getProperties().set ("led", true);   // switches: a light shows they're on
        key->setColour (juce::TextButton::textColourOnId, ink);
    }
    resampleButton.setTooltip ("RESAMPLE: ARM records what Silt itself is playing (from the next bar) as the new sample");
    if constexpr (SiltProcessor::isInstrument)
        addAndMakeVisible (resampleButton);
    resampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "captureFeed", resampleButton);

    armButton.onClick = [this]
    {
        if (silt.captureState.load() == SiltProcessor::CaptureState::idle)
            silt.armCapture();
        else
            silt.cancelCapture();
    };
    if constexpr (SiltProcessor::isInstrument)
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
    if constexpr (SiltProcessor::isInstrument)
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
    undoButton.setTooltip ("A step back (Cmd/Ctrl+Z): knobs, menus, routes, samples (loads, takes, trim, reverse, normalise), slices, RANDOM, RESET and presets. "
                           "Not undone: host automation, and opening a project");
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
            { presetBar.get(), "!preset" }, { chainView.get(), "!chain" }, { benchStrip.get(), "!bench" }, { screenDivider.get(), "!grip" }, { sideDivider.get(), "!grip" },
        };
        for (const auto& [part, id] : parts)
            if (part != nullptr)
            {
                part->getProperties().set ("help", id);
                part->setDescription (helpFor (id));   // for screen readers too
            }
        if (sampleGainBox != nullptr)
            sampleGainBox->getProperties().set ("help", "!gain");
        for (auto& tab : pageTabs)
            tab.getProperties().set ("help", "!tab");
    }
    helpWatcher.onHover = [this] (const juce::String& id) { showHelpFor (id); };
    addMouseListener (&helpWatcher, true);
    screen->setView (startZoom.x, startZoom.y);
    // The parts made before they joined the panel (knobs make their value
    // boxes as they're made) take on the panel's look.
    sendLookAndFeelChange();

    setWantsKeyboardFocus (true);
    const int w = silt.editorWidth.load(), h = silt.editorHeight.load();   // before the limits resize us
    setResizable (true, true);
    setResizeLimits (1000, 780, 1800, 1200);
    setSize (w > 0 ? juce::jlimit (1000, 1800, w) : defaultWidth, h > 0 ? juce::jlimit (780, 1200, h) : defaultHeight);

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

    startTimerHz (60);
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
    // For screen readers: the label, and the hover help as the description.
    c->component().setTitle (entry.label.isNotEmpty() ? entry.label : entry.text.isNotEmpty() ? entry.text : entry.id);
    c->component().setDescription (helpFor (entry.id));
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
        if (amount != 0.0f && ! silt.routeCanWork (m, target))
        {
            flash = "PER GRAIN ONLY REACHES GRAIN SETTINGS";
            flashUntil = juce::Time::getMillisecondCounter() + 2000;
            repaint (lcdArea);
            return;
        }
        silt.editRoutes ([&] { silt.setRouteAmount (m, target, amount); });
        lastUsedMod = m;
        s.repaint();
    };
    s.depthDragged = [this, target] (int m, bool dragging)
    {
        depthReadout = dragging ? "MOD " + juce::String (m + 1) + " > " + silt.targetParameter (target).getName (16).toUpperCase()
                                      + "  " + silt.depthText (m, target)
                                : juce::String();
        repaint (lcdArea);
    };
    s.setWantsKeyboardFocus (true);   // clicked, it hears Delete
    s.clearModulation = [this, target, &s]
    {
        bool any = false;
        for (int m = 0; m < SiltProcessor::numModulators; ++m)
            any = any || silt.routeAmount (m, target) != 0.0f;
        if (! any)
            return false;
        silt.beginRouteGesture();
        silt.editRoutes ([&] { silt.clearRoutesOnTarget (target); });
        flash = "MODULATION REMOVED: " + silt.targetParameter (target).getName (24).toUpperCase();
        flashUntil = juce::Time::getMillisecondCounter() + 1500;
        s.repaint();
        repaint (lcdArea);
        return true;
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
            repaint (lcdArea);
        }
    };
}

// Right-click on a knob: each modulator's depth on it, in its colour, with
// quick depths to pick (or remove), then clear-all and the route list.
void SiltEditor::showKnobMenu (FineSlider& s)
{
    static constexpr float depths[] { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
    static const char* types[] { "WAVE", "RANDOM", "ENV", "FOLLOW", "MIDI", "MACRO", "PER GRAIN" };
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
                else if (i == 99 || silt.routeCanWork (m, target))
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
        layoutAtDragStart = { silt.layoutWell.load(), silt.layoutSide.load() };
    };
    const auto relayout = [this] { resized(); repaint(); };

    screenDivider = std::make_unique<Divider> (false);
    screenDivider->setTooltip ("Drag to share the height between the screen and the knobs; double-click to reset");
    screenDivider->onStart = start;
    screenDivider->onDrag = [this] (int pixels) { dragScreenEdge (pixels); };
    screenDivider->onReset = [this] { resetLayout (0); };
    addAndMakeVisible (*screenDivider);

    sideDivider = std::make_unique<Divider> (true);
    sideDivider->setTooltip ("Drag to make the right column wider or narrower; double-click to reset");
    sideDivider->onStart = start;
    sideDivider->onDrag = [this] (int pixels) { dragSideColumn (pixels); };
    sideDivider->onReset = [this] { resetLayout (3); };
    addAndMakeVisible (*sideDivider);

}

void SiltEditor::dragScreenEdge (int pixels)
{
    silt.layoutWell.store (juce::jlimit (0.1f, 0.9f, layoutAtDragStart.well - (float) pixels / (float) juce::jmax (1, flexibleHeight)));
    resized();
    repaint();
}

void SiltEditor::dragSideColumn (int pixels)
{
    // Dragging left widens the column. The limits are the ones resized() enforces, so
    // the share never runs on past what the panel will show.
    const float width = (float) juce::jmax (1, contentWidth);
    silt.layoutSide.store (juce::jlimit (minSideWidth / width, maxSideShare, layoutAtDragStart.side - (float) pixels / width));
    resized();
    repaint();
}

void SiltEditor::resetLayout (int grip)
{
    const auto d = SiltProcessor::defaultLayout();
    if (grip == 0)
        silt.layoutWell.store (d.well);
    else
        silt.layoutSide.store (d.side);
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
    const float input = SiltProcessor::isInstrument ? 0.0f : param ("inputGain");
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
                       SiltProcessor::isInstrument
                           ? "This is your sample. While you play, each white line is a grain sounding right now, and the shaded "
                             "band is the part of the sample grains are taken from."
                           : "This is the last few seconds of your track's audio. While you play, each white line is a grain "
                             "sounding right now, reaching back into it.",
                       [this] { return Spots (screen->getBounds()); }, -1,
                       SiltProcessor::isInstrument ? "Play a note or a chord on your keyboard (or a MIDI clip) and watch the grains appear."
                                                   : "Play your track and watch the grains appear.",
                       [this] { return silt.activeGrains.load() > 0; } });
    if constexpr (SiltProcessor::isInstrument)
        steps.push_back ({ "Your own sounds",
                           "LOAD opens an audio file, or one of the built-in samples. You can also drop a file on the screen, "
                           "or press ARM to record from your DAW's sidechain input (for the number of bars beside it).",
                           [this, boundsOf] { return Spots (boundsOf ({ &armButton, &loadButton })); }, -1,
                           "Click LOAD and pick a different factory sample.",
                           [this, generation] { return silt.getSampleGeneration() != generation; } });
    if constexpr (SiltProcessor::isInstrument)
        steps.push_back ({ "Live input",
                           "Silt can also make grains from audio as it arrives. In your DAW, pick a track in Silt's sidechain "
                           "(in Live, the Sidechain menu on the device), then set MODE to LIVE on the PLAY page. The display "
                           "shows SC and a level while audio comes in, and says so when none does.",
                           [this]
                           {
                               Spots s (lcdArea);
                               for (const auto& c : controls)
                                   if (c->entry->id == "keyMode" && c->component().isVisible())
                                       s.add (c->component().getBounds());
                               return s;
                           },
                           layout::play, {}, {} });
    else
        steps.push_back ({ "Live input",
                           "Silt FX turns the audio on its track into grains as it plays. The display shows the input's level. "
                           "LEVELS is always on screen: INPUT sets how loud the track goes into the grains, and MIX the dry track "
                           "against them, both going on through the chain. On the PLAY page, DELAY sets how far back the grains "
                           "reach and NO NOTES what plays when no notes arrive. For samples, slices and FEED, use Silt, the instrument.",
                           [this] { Spots s (lcdArea); s.add (levelsPanel); return s; }, layout::play,
                           "Turn INPUT up or down while your track plays.",
                           [this, input] { return std::abs (param ("inputGain") - input) > 1.0f; } });
    steps.push_back ({ "Pages",
                       "Each tab is a page of up to eight controls, the same eight your MIDI controller's knobs move. GRAIN and "
                       "SHAPE make the grains, PITCH and PLAY decide how notes play them, and MOD is movement.",
                       [this, boundsOf] { return Spots (boundsOf ({ &pageTabs.front(), &pageTabs.back() })); }, layout::grain,
                       "Click another tab to look around.",
                       [this] { return page != layout::grain; } });
    steps.push_back ({ "Spray",
                       "SPRAY scatters each grain to a random spot near where it would play, so the sound blurs and shimmers. "
                       "Drag a knob up or down to turn it, hold Shift for fine moves, and double-click to put it back.",
                       [this] { Spots s (screen->getBounds()); s.add (slotBounds (3)); return s; }, layout::grain,
                       "Drag SPRAY up while you play, and watch the grains spread out across the waveform.",
                       [this, spray] { return std::abs (param ("spray") - spray) > 5.0f; } });
    if constexpr (SiltProcessor::isInstrument)
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
                           [this] { return silt.playMode() == SiltProcessor::modeSlice; } });
    steps.push_back ({ "See what you hear",
                       "RES, FILTER, COLOR and SPACE shape the sound after the grains: click one in the chain for its page. On the first three, the strip on "
                       "the screen shows the sound going in (faint) and coming out (bright).",
                       [this] { Spots s (screen->scopeArea().translated (screen->getX(), screen->getY())); s.add (slotBounds (2)); return s; },
                       layout::color,
                       "Play a note and turn BITS down: the bright line breaks into steps as the sound gets crunchier.",
                       [this, bits] { return param ("colorBits") < bits - 1.0f; } });
    steps.push_back ({ "Chain",
                       "The effects run in this order, and a switch only lights up while its effect is changing the sound. Moving "
                       "one changes the sound too: grit added after the reverb is harsher than grit the reverb smooths over.",
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

// SPACE's scene settings, from the module's controls and what it's doing now.
void SiltEditor::updateSpaceSettings (float seconds)
{
    auto& s = spaceSettings;
    const float decaySeconds = juce::jmax (0.3f, param ("spaceDecay"));
    s.decay = juce::jlimit (0.0f, 1.0f, std::log (decaySeconds / 0.3f) / std::log (100.0f));
    s.size = param ("spaceSize") * 0.01f;
    s.reverb = param ("spaceReverb") * 0.01f;
    s.tone = param ("spaceTone") * 0.01f;
    s.shimmer = param ("spaceShimmer") * 0.01f;
    s.delay = param ("spaceDelay") * 0.01f;
    s.feedback = param ("spaceFeedback") * 0.01f;
    s.echoSeconds = param ("spaceSync") > 0.5f
                      ? (float) (SiltProcessor::divisionQuarters (juce::roundToInt (param ("spaceDiv"))) * 60.0 / juce::jlimit (20.0, 400.0, silt.hostBpm.load()))
                      : param ("spaceTime") * 0.001f;
    s.pingPong = juce::roundToInt (param ("spaceMode")) == 0;
    s.frozen = param ("spaceFreeze") > 0.5f;

    // The reverb's controls only show while there's reverb: at 0% the sky is
    // neutral, and they come in over the first 20%.
    const float reverbIn = juce::jlimit (0.0f, 1.0f, s.reverb * 5.0f);
    s.decay = 0.5f + (s.decay - 0.5f) * reverbIn;
    s.size = 0.5f + (s.size - 0.5f) * reverbIn;
    s.tone = 0.5f + (s.tone - 0.5f) * reverbIn;
    s.shimmer *= reverbIn;
    s.frozen = s.frozen && s.reverb > 0.0f;

    // What SPACE is adding now (its trace), and the time since the latest note.
    const double rate = silt.getSampleRate() > 0.0 ? silt.getSampleRate() : 48000.0;
    const unsigned count = silt.spaceTraceCount.load (std::memory_order_acquire);
    float peak = 0.0f;
    for (unsigned i = count > 8 ? count - 8 : 0; i < count; ++i)
        peak = juce::jmax (peak, silt.spaceTraceAt (i));
    s.wet = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 54.0f) / 48.0f);
    s.sinceNote = (float) (count * SiltProcessor::spaceTraceStep / rate);
    for (const auto& head : silt.voicePlayheads)   // where a note is playing, for its constellation
        if (const float at = head.load(); at >= 0.0f)
        {
            s.notePlace = at;
            break;
        }

    // DUCK: dips while a note sounds, back over about half a second.
    static constexpr float duckAmounts[] { 0.0f, 0.35f, 0.65f, 1.0f };
    bool sounding = false;
    for (const auto& head : silt.voicePlayheads)
        sounding = sounding || head.load() >= 0.0f;
    const float target = sounding ? 1.0f - 0.8f * duckAmounts[juce::jlimit (0, 3, juce::roundToInt (param ("spaceDuck")))] : 1.0f;
    s.duck += (target - s.duck) * juce::jmin (1.0f, seconds * (target < s.duck ? 30.0f : 7.0f));
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
    menu.addSectionHeader ("CHARACTER");
    static const char* const characters[] { "Anything", "Tonal: stays in tune", "Rhythmic: short, regular", "Texture: dense, scattered" };
    for (int i = 0; i < 4; ++i)
        menu.addItem (30 + i, characters[i], true, silt.randomCharacter.load() == i);
    menu.addSeparator();
    menu.addSubMenu ("Leave out on " + juce::String (layout::pageName (page)), leaveOut, ! entries.empty());
    menu.addSeparator();
    menu.addItem (20, "Reset " + juce::String (layout::pageName (page)) + (page == layout::mod ? " " + juce::String (silt.modShown.load() + 1) : juce::String()));
    menu.addItem (21, "Reset all pages");

    juce::PopupMenu::dismissAllActiveMenus();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomButton), [this, entries] (int chosen)
    {
        if (chosen >= 30 && chosen < 34)
            silt.randomCharacter.store (chosen - 30);
        else if (chosen == 20 || chosen == 21)
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

// A modulator's page from anywhere, and from there, back to where you were.
void SiltEditor::openModPage (int m, bool wasShown)
{
    const bool leaving = page == layout::mod && wasShown;
    if (page != layout::mod)
        pageBeforeMod = page;
    selectModulator (m, false);
    const int target = leaving ? pageBeforeMod : layout::mod;
    pageTabs[(size_t) target].setToggleState (true, juce::dontSendNotification);
    silt.currentPage.store (target);
    showPage (target);
}

void SiltEditor::selectModulator (int m, bool arm)
{
    silt.modShown.store (m);
    if (arm)
        silt.armedMod.store (silt.armedMod.load() == m ? -1 : m);   // arming the armed one disarms
    refreshLayout();
    repaint (lcdArea);
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
    static constexpr int modules[] { SiltProcessor::moduleResonator, SiltProcessor::moduleFilter, SiltProcessor::moduleColor, SiltProcessor::moduleSpace };
    int open = -1;
    if (page == layout::resonator) open = modules[0];
    if (page == layout::filter) open = modules[1];
    if (page == layout::color) open = modules[2];
    if (page == layout::space) open = modules[3];
    chainView->setOpen (open, silt.resonatorHalf.load());
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
        const bool show = (c.entry->alwaysShown || c.entry->page == page) && c.entry->isShown (context);
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
SiltEditor::SlotParts SiltEditor::partsOf (const layout::Entry& entry) const
{
    if (! entry.alwaysShown)
        return slotParts (entry.slot);
    // The levels panel: INPUT, MIX, OUTPUT, the way the signal goes. No row under a knob here.
    constexpr int labelHeight = 18;
    const auto inner = levelsPanel.withTrimmedTop (kit::panelHeadHeight).reduced (6, 3);
    const int place = SiltProcessor::slotsPerPage - 1 - entry.slot, w = inner.getWidth() / 3;
    auto block = juce::Rectangle<int> (inner.getX() + place * w, inner.getY(), w, inner.getHeight());
    SlotParts parts;
    parts.label = block.removeFromTop (labelHeight);
    parts.control = block;
    return parts;
}

SiltEditor::SlotParts SiltEditor::slotParts (int slot) const
{
    constexpr int labelHeight = 18, valueBox = 20, underRow = 24;
    const auto column = slotBounds (slot);
    const int knob = juce::jlimit (40, 92, juce::jmin (column.getWidth() - 16, column.getHeight() - labelHeight - valueBox - underRow - 8));
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
    return SiltProcessor::isInstrument && files.size() == 1 && silt.isAudioFile (juce::File (files[0]));
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
    // Shift+1 to 4: that modulator's page, the same as double-clicking its chip.
    if (const int code = key.getKeyCode(); code >= '1' && code <= '4' && key.getModifiers().isShiftDown() && ! key.getModifiers().isCommandDown())
    {
        const int m = code - '1';
        openModPage (m, silt.modShown.load() == m);
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
        repaint (lcdArea);
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
        case S::armed:
            // Nothing to record: say where the audio has to come from.
            if (param ("captureFeed") <= 0.5f && inputSilent())
                return SiltProcessor::isInstrument ? "ARMED, NO INPUT: PICK A TRACK IN SILT'S SIDECHAIN" : "ARMED, NO INPUT: CHECK THE TRACK'S INPUT";
            return juce::String (param ("captureFeed") > 0.5f ? "RESAMPLE " : "") + (silt.hostPlaying.load() ? "ARMED: WAITING FOR NEXT BAR" : "ARMED: STARTING");
        case S::recording: return juce::String (param ("captureFeed") > 0.5f ? "RESAMPLE " : "") + "REC " + juce::String (juce::roundToInt (silt.captureProgress.load() * 100.0f)) + "%";
        case S::done:
        case S::cancelRequested:
        case S::cancelled:
        case S::idle: break;
    }

    if (silt.lastLoadError.isNotEmpty())
        return silt.lastLoadError;

    // LIVE with nothing coming in: there's nothing for the grains to read.
    if (silt.playMode() == SiltProcessor::modeLive && inputSilent())
        return SiltProcessor::isInstrument ? "LIVE NEEDS INPUT: PICK A TRACK IN SILT'S SIDECHAIN" : "LIVE NEEDS INPUT: CHECK THE TRACK'S INPUT";

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
    // Turn about at 60 Hz, so the things that move never redraw in the same frame: macOS joins
    // what's dirty in one frame into a single rectangle, and the screen at the top with the
    // modulators at the bottom would repaint everything between them. The screen and the
    // modulators (with the knobs they move) each get 30 Hz, and everything that has to look
    // its parameters up by name runs at 10 Hz.
    timerTick = (timerTick + 1) % 6;
    const bool modulationTick = timerTick % 2 == 1, screenTick = ! modulationTick, slowTick = timerTick == 0;

    if (screenTick)
    {
        const float db = juce::Decibels::gainToDecibels (silt.captureMeter.takePeak(), -100.0f);
        levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);
        if (db > -60.0f)
            inputHeardAt = juce::Time::getMillisecondCounter();
    }

    if (silt.loadNote != loadNoteSeen)
    {
        // Something worth knowing about the last load: a moment in the LCD.
        loadNoteSeen = silt.loadNote;
        if (loadNoteSeen.isNotEmpty())
        {
            flash = loadNoteSeen;
            flashUntil = juce::Time::getMillisecondCounter() + 3000;
        }
    }
    if (const int generation = silt.getSampleGeneration(); generation != seenGeneration)
    {
        seenGeneration = generation;
        screen->setOverview (silt.getOverview());
    }
    if (slowTick)
    {
        const auto [regionStart, regionLength] = silt.shownRegion();
        screen->setRegion (regionStart, regionLength);
        screen->setTransientsShown (param ("snap") > 1.5f);
        screen->setSlices (silt.sliceMarks());
        screen->setDisplayGain (juce::Decibels::decibelsToGain (param ("sampleGain")));
    }

    // The grains: the screen only repaints while something is moving.
    if (screenTick)
    {
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
    {
        hud = juce::String (n) + (n == 1 ? " GRAIN   " : " GRAINS   ") + juce::String (voices) + (voices == 1 ? " VOICE" : " VOICES");
        // Out of room: say so for a moment after the last grain had to make way.
        if (const auto over = silt.grainsOverLimit.load (std::memory_order_relaxed); over != overLimitSeen)
        {
            overLimitSeen = over;
            fullUntil = juce::Time::getMillisecondCounter() + 1000;
        }
        if (juce::Time::getMillisecondCounter() < fullUntil)
            hud += "   FULL";
    }
    const bool live = silt.liveMode.load();
    const bool frozen = param ("freeze") > 0.5f;
    if (live)
        screen->setLive (true, frozen, silt.liveWriteShare.load(), silt.getLivePeaks(), silt.playMode() == SiltProcessor::modeFeed);
    else
        screen->setLive (false, false, 0.0f, {});
    const bool moved = screen->setActivity (views, n, heads, hud);
    if (moved || (live && ! frozen) || live != screenWasLive)
        screen->repaint();
    screenWasLive = live;

    // The live scope, on pages whose module it shows: only redrawn when new audio arrived.
    updateScope();
    // SPACE's sky: moved on and redrawn while it shows, cleared when it goes.
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        const float seconds = (float) juce::jlimit (0.0, 0.1, (nowMs - spaceTickAt) * 0.001);
        spaceTickAt = nowMs;
        updateSpaceSettings (seconds);
        const bool shown = page == layout::space && SpaceScene::visible (spaceSettings) && ! silt.liveMode.load();
        if (shown)
        {
            spaceScene->advance (spaceSettings, seconds, screen->getWaveEnvelope());
            screen->repaintWave();
        }
        else if (spaceSceneShown)
            screen->repaintWave();
        spaceSceneShown = shown;
    }
    }
    if (screenTick)
        updateBench();
    if (slowTick)
        chainView->update();

    // RESONATOR's tab says which half is showing.
    if (const int half = silt.resonatorHalf.load(); half != resonatorHalfSeen)
    {
        resonatorHalfSeen = half;
        chainView->setOpen (page == layout::resonator ? (int) SiltProcessor::moduleResonator : -1, half);
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
        repaint (lcdArea);
        repaint (pagePanel);
    }

    // Parameters (automation, the controller, the rail) can switch which
    // alternative a slot shows.
    if (slowTick)
        updateVisibility();

    // Modulation: the rail's meters, the header stripes, the knobs' live dots;
    // arming changes every knob's hint ring.
    if (modulationTick)
    {
    rail->update();
    if (routeList->isVisible())
        routeList->update();
    const int armed = silt.armedMod.load(), shown = silt.modShown.load();
    // The page keys' tint follows the armed modulator's routes (and the page shown).
    if (const int tint = armed < 0 ? -1 : routedPages (armed) * 16 + page; tint != tabTintSeen)
    {
        tabTintSeen = tint;
        repaint (tray);
    }
    else if (tint > 0 && (tint >> 4) != 0)
    {
        // The wash follows the modulator: redraw when it has moved.
        if (const float level = modLevel01 (armed); std::abs (level - tabWashSeen) > 0.01f)
        {
            tabWashSeen = level;
            repaint (tray);
        }
    }
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
        repaint (lcdArea);
        repaint (pagePanel.withHeight (32));   // the MOD tab's underline
    }
    // A chip's drop preview on a knob: gone once no drag is under way, even if
    // the drag ended without telling the knob (the mouse let go elsewhere).
    if (! isDragAndDropActive())
        for (auto& c : controls)
            if (c->slider != nullptr && c->slider->dropHover >= 0)
            {
                c->slider->dropHover = -1;
                c->slider->repaint();
            }
    for (auto& c : controls)
        if (c->slider != nullptr && c->slider->isVisible() && c->slider->target >= 0)
            for (int m = 0; m < SiltProcessor::numModulators; ++m)
                if (silt.routeAmount (m, c->slider->target) != 0.0f)
                {
                    c->slider->repaint();
                    break;
                }
    }

    if (! slowTick)
        return;

    // The screen's corner picture: redrawn when what it shows changes.
    if (auto key = insetKey (page, silt, *rail); key != insetSeen)
    {
        insetSeen = std::move (key);
        screen->repaintInset();
    }

    // The bar marks a sound that's moved from its preset, checked a few times a second.
    const bool checkEdited = ++editedCheck % 20 == 0;
    if (silt.presetName != presetShown || checkEdited)
    {
        presetShown = silt.presetName;
        presetBar->refresh();
    }

    undoButton.setEnabled (silt.canUndoRoutes());
    redoButton.setEnabled (silt.canRedoRoutes());

    const bool capturing = silt.captureState.load() != SiltProcessor::CaptureState::idle;
    armButton.setButtonText (capturing ? "CANCEL" : juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ARM")));
    if (capturing != (bool) armButton.getProperties().getWithDefault ("accent", false))
    {
        armButton.getProperties().set ("accent", capturing);   // the panel's orange is for what's recording
        armButton.repaint();
    }

    if (auto status = statusText(); status != statusSeen)
    {
        statusSeen = std::move (status);
        repaint (lcdArea);
    }

    // Redraw the SHAPE preview only when its values change.
    if (page == layout::shape)
    {
        const std::array<float, 7> now { heard ("fade"), heard ("slant"), heard ("ease"), heard ("attack"),
                                         heard ("decay"), heard ("sustain"), heard ("release") };
        if (now != previewSeen)
        {
            previewSeen = now;
            repaint (pagePanel);
        }
    }
}

// One grid for the whole panel: a left column as wide as the mod rail, then
// the 8 knob columns; every row (header, keys, knobs) sits on those edges.
// Top to bottom: the header, the screen, the row of keys and
// the knob well. The header and keys keep their height; the screen and
// the well share the rest, as their grip sets (text stays its size, knobs grow).
void SiltEditor::resized()
{
    silt.editorWidth.store (getWidth());
    silt.editorHeight.store (getHeight());
    backdrop = {};
    constexpr int gap = 10, trayHeight = 34, minWell = 170;

    auto r = getLocalBounds().reduced (24);
    contentWidth = r.getWidth();

    // The sheet every plug-in in the range shares: masthead, main sheet with the
    // readout column to its right, and the Bench along the bottom.
    header = r.removeFromTop (72);
    r.removeFromTop (12);
    {
        auto bar = header.withHeight (36).withY (header.getY() + 6);
        tourButton.setBounds (bar.removeFromRight (36));
        bar.removeFromRight (8);
        presetBar->setBounds (bar.removeFromRight (juce::jmin (400, bar.getWidth() / 2)));
    }
    if (benchStrip != nullptr)
    {
        benchArea = r.removeFromBottom (50);
        r.removeFromBottom (12);
        benchStrip->setBounds (benchArea);
        benchSource.setBounds (benchStrip->reservedArea().translated (benchArea.getX(), benchArea.getY()));
    }

    // The well runs the full width: the modulators, then the knobs they get dragged onto.
    constexpr int minScreen = 310;
    flexibleHeight = r.getHeight() - trayHeight - 2 * gap;
    const int wellHeight = juce::jlimit (minWell, juce::jmax (minWell, flexibleHeight - minScreen),
                                         juce::roundToInt ((float) flexibleHeight * silt.layoutWell.load()));
    wellArea = r.removeFromBottom (wellHeight);
    r.removeFromBottom (gap);
    // The page keys, then the everyday keys, on one row.
    auto keyRow = r.removeFromBottom (trayHeight);
    r.removeFromBottom (gap);

    // Beside the screen: the readout, then what feeds the sound and the order it runs in.
    const int sideWidth = juce::jlimit (minSideWidth, juce::jmax (minSideWidth, juce::roundToInt ((float) contentWidth * maxSideShare)), juce::roundToInt ((float) contentWidth * silt.layoutSide.load()));
    auto side = r.removeFromRight (sideWidth);
    r.removeFromRight (12);
    sideDivider->setBounds (side.getX() - 11, side.getY(), 10, side.getHeight());

    lcdArea = side.removeFromTop (84);
    side.removeFromTop (gap);
    if constexpr (SiltProcessor::isInstrument)
    {
        sourcePanel = side.removeFromTop (kit::panelHeadHeight + 90);
        side.removeFromTop (gap);
        auto inner = sourcePanel.withTrimmedTop (kit::panelHeadHeight).reduced (10, 8);
        auto row = inner.removeFromTop (34);
        armButton.setBounds (row.removeFromLeft (96));
        row.removeFromLeft (8);
        captureBars.setBounds (row);
        inner.removeFromTop (6);
        const int third = (inner.getWidth() - 12) / 3;
        resampleButton.setBounds (inner.removeFromLeft (third + 16));
        inner.removeFromLeft (6);
        loadButton.setBounds (inner.removeFromLeft (third - 16));
        inner.removeFromLeft (6);
        monitorButton.setBounds (inner);
    }
    else
    {
        levelsPanel = side.removeFromTop (kit::panelHeadHeight + 100);
        side.removeFromTop (gap);
    }
    chainPanel = side;
    chainView->setBounds (chainPanel.withTrimmedTop (kit::panelHeadHeight).reduced (10, 7));

    screen->setSplit (0.36f, 0.33f);
    screen->setBounds (r);
    if (presetBrowser != nullptr)
        presetBrowser->setBounds (r.withBottom (wellArea.getBottom()).withTrimmedRight (-3).withTrimmedBottom (-3));
    pagePanel = tray.getUnion (wellArea);

    // The everyday keys on the right of the row, the step back last.
    {
        auto keys = keyRow;
        redoButton.setBounds (keys.removeFromRight (62));
        keys.removeFromRight (4);
        undoButton.setBounds (keys.removeFromRight (62));
        keys.removeFromRight (12);
        routesButton.setBounds (keys.removeFromRight (104));
        keys.removeFromRight (4);
        randomAmountBox->setBounds (keys.removeFromRight (52).withTrimmedBottom (2));
        keys.removeFromRight (4);
        randomButton.setBounds (keys.removeFromRight (84));
        keys.removeFromRight (4);
        learnButton.setBounds (keys.removeFromRight (84));
        keys.removeFromRight (12);
        tray = keys.withWidth (juce::jmin (keys.getWidth(), 5 * 120));
    }

    // The page keys: what makes the grains, then movement. The effects' pages are the
    // chain's blocks.
    {
        const auto keys = tray.reduced (1);
        int count = 0;
        for (size_t t = 0; t < pageTabs.size(); ++t)
            count += pageHasKey ((int) t) ? 1 : 0;
        int shown = 0;
        for (size_t t = 0; t < pageTabs.size(); ++t)
        {
            if (! pageHasKey ((int) t))
                continue;
            const int left = keys.getX() + keys.getWidth() * shown / count, right = keys.getX() + keys.getWidth() * (shown + 1) / count;
            pageTabs[t].setBounds (keys.withLeft (left).withRight (right));
            ++shown;
        }
    }

    // The well: the modulators, then the knobs, one per column.
    const int railWidth = juce::jlimit (150, 280, contentWidth / 6);
    railArea = wellArea.withWidth (railWidth).reduced (8, 8);
    rail->setBounds (railArea);
    controlRow = wellArea.withTrimmedLeft (railWidth + 8).withTrimmedTop (2).withTrimmedBottom (2);
    routeList->setBounds (controlRow.reduced (0, 2));
    if (tour != nullptr && tour->isRunning())   // the first layout comes before the tour exists
        tour->setBounds (getLocalBounds());

    // The bar under the screen shares its height with the knobs.
    screenDivider->setBounds (wellArea.withY (screen->getBottom() + 3).withHeight (7));

    for (auto& c : controls)
    {
        const auto& e = *c->entry;
        const auto slot = partsOf (e);
        if (! e.inSlot())
        {
            c->component().setBounds (slot.under.withSizeKeepingCentre (e.width, 18).translated (e.dx, 0));
            continue;
        }
        const auto cell = slot.control;
        if (c->menu != nullptr)
            c->menu->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 26));
        else if (c->button != nullptr)
            c->button->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 12, 34));
        else if (c->banner != nullptr)
            c->banner->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 56));
        else
            c->slider->setBounds (cell);
    }
}

static const juce::String& benchChip()
{
    static const juce::String chip (SiltProcessor::isInstrument ? "I-01" : "I-01 FX");
    return chip;
}

static const juce::String& benchName()
{
    static const juce::String name (SiltProcessor::isInstrument ? "SILT" : "SILT FX");
    return name;
}

// The Bench: the bars when they're in, and what's waiting, looked for once a second.
void SiltEditor::updateBench()
{
    using Recorder = kit::bench::OutputRecorder;
    if (silt.outputRecorder.state() == Recorder::State::done)
    {
        juce::AudioBuffer<float> moment;
        double rate = 0.0;
        if (silt.outputRecorder.take (moment, rate) && kit::bench::send (moment, rate, benchChip(), benchName(), "SILT"))
        {
            momentSent = juce::Time::currentTimeMillis();
            flash = "BARS SENT";
            flashUntil = juce::Time::getMillisecondCounter() + 2000;
        }
        benchCheckedAt = 0.0;
    }
    const auto recording = silt.outputRecorder.state();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const bool busy = recording == Recorder::State::waiting || recording == Recorder::State::recording;
    if (! busy && now - benchCheckedAt < 1000.0)
        return;
    if (now - benchCheckedAt >= 1000.0)
    {
        benchCheckedAt = now;
        waiting = kit::bench::latest (benchChip());
    }
    const bool fresh = SiltProcessor::isInstrument && waiting.has_value() && waiting->stamp != momentTaken;
    const auto state = silt.audioName().toUpperCase();
    const bool bars = benchSource.getSelectedId() > 1;
    const juce::String text = recording == Recorder::State::waiting ? juce::String ("WAITING FOR THE NEXT BAR")
                            : recording == Recorder::State::recording ? "RECORDING " + juce::String (juce::roundToInt (silt.outputRecorder.progress() * 100.0f)) + "%"
                            : fresh ? "FROM " + waiting->fromChip + " " + waiting->fromName + " / " + juce::String (waiting->seconds, 2) + " S"
                            : momentSent != 0 ? juce::String ("SENT: IT'S IN THE FOLDER") : juce::String ("NOTHING WAITING");
    benchStrip->set (state, text, busy ? "STOP" : "SEND >", busy || bars || silt.getOverview().seconds > 0.0 || silt.liveMode.load(), fresh);
}

// A moment becomes the sample. Its file stays where it was sent, for the Finder.
void SiltEditor::takeMoment()
{
    const auto moment = kit::bench::latest (benchChip());
    if (! moment.has_value())
        return;
    silt.loadFile (moment->audio);
    momentTaken = moment->stamp;
    benchCheckedAt = 0.0;
    updateBench();
}

// SEND: records bars of what you hear and sends them when they're in, or sends what's
// on the screen now. Again while recording stops it.
void SiltEditor::sendMoment()
{
    using Recorder = kit::bench::OutputRecorder;
    const auto state = silt.outputRecorder.state();
    if (state == Recorder::State::waiting || state == Recorder::State::recording)
    {
        silt.outputRecorder.cancel();
        return;
    }
    if (const int bars = benchSource.getSelectedId(); bars > 1)
    {
        const double quarters = (double) silt.timeSigNumerator.load() * 4.0 / (double) juce::jmax (1, silt.timeSigDenominator.load());
        silt.outputRecorder.arm (bars, quarters, silt.hostBpm.load(), silt.preparedSampleRate());
    }
    else
    {
        const auto file = kit::bench::newAudioFile ("SILT", silt.audioName());
        if (silt.writeAudioFile (file) && kit::bench::publish (file, silt.getOverview().seconds, benchChip(), benchName()))
            momentSent = juce::Time::currentTimeMillis();
    }
    benchCheckedAt = 0.0;
    updateBench();
}

void SiltEditor::setBenchSource (int barsOrZero)
{
    benchSource.setSelectedId (barsOrZero > 1 ? barsOrZero : 1, juce::sendNotificationSync);
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
    g.setColour (ink.withAlpha (0.2f));
    g.drawVerticalLine (slotBounds (3).getX(), (float) controlRow.getY() + 8.0f, (float) controlRow.getBottom() - 8.0f);

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
        env.set (heard ("fade") * 0.01, heard ("slant") * 0.01, heard ("ease") * 0.01, 1000, 0.0);
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
        const float a = heard ("attack"), d = heard ("decay"), s = heard ("sustain") * 0.01f, rel = heard ("release");
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

void SiltEditor::paint (juce::Graphics& g)
{
    // What doesn't move is drawn once per size and theme, at the screen's own resolution so it's
    // as sharp as what's drawn fresh beside it.
    const float pixels = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int backdropWidth = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixels));
    const int backdropHeight = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixels));
    if (backdrop.getWidth() != backdropWidth || backdrop.getHeight() != backdropHeight)
    {
        backdrop = juce::Image (juce::Image::RGB, backdropWidth, backdropHeight, false);
        juce::Graphics b (backdrop);
        b.addTransform (juce::AffineTransform::scale (pixels));
        const auto& s = stock();
        b.fillAll (s.page);
        const bool instrument = SiltProcessor::isInstrument;
        kit::drawMasthead (b, header, s, instrument ? "I-01" : "I-01 FX", s.accent, s.onAccent, juce::String (JucePlugin_Name).toUpperCase() + ".",
                           "GLYPH AUDIO", instrument ? "GRANULAR SAMPLER" : "GRANULAR, LIVE");
        b.setColour (s.shadow);
        b.fillRect (screen->getBounds().translated (3, 3));
        if constexpr (SiltProcessor::isInstrument)
            kit::drawPanel (b, sourcePanel, s, "01", "SOURCE", "SAMPLE");
        if (! instrument)
            kit::drawPanel (b, levelsPanel, s, "01", "LEVELS", "IN TO OUT");
        kit::drawPanel (b, chainPanel, s, "02", "CHAIN", "DRAG TO REORDER");
        // The page keys' frame, and the well.
        kit::drawSheet (b, tray, s, 2);
        kit::drawSheet (b, wellArea, s);
    }
    {
        const juce::Graphics::ScopedSaveState saved (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed (backdrop, juce::AffineTransform::scale ((float) getWidth() / (float) backdrop.getWidth(),
                                                                        (float) getHeight() / (float) backdrop.getHeight()));
    }

    const auto status = statusText();
    kit::drawReadout (g, lcdArea, stock(), juce::String (JucePlugin_Name).toUpperCase(), "LIVE", status, {}, status.length() > 26);

    // One rule, between the modulators and the knobs: the columns are clear from what's in them.
    g.setColour (ink.withAlpha (0.2f));
    g.drawVerticalLine (slotBounds (0).getX(), (float) controlRow.getY() + 8.0f, (float) controlRow.getBottom() - 8.0f);

    g.setColour (ink);
    const auto labelFont = kit::label (17.0f);
    g.setFont (labelFont);
    for (auto& c : controls)
        if (c->entry->inSlot() && c->component().isVisible())
        {
            const auto labelArea = partsOf (*c->entry).label;
            g.drawText (c->entry->label, labelArea, juce::Justification::centred);
            const float labelWidth = (float) kit::textWidth (labelFont, c->entry->label);
            g.setColour (ink);
            if (silt.isRandomExcluded (c->entry->id))
            {
                // Left out of RANDOM: a small crossed-out die after the label.
                const auto die = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) labelArea.getCentreX() + labelWidth * 0.5f + 9.0f, (float) labelArea.getCentreY() });
                g.setColour (inkSoft);
                g.drawRect (die, 1.0f);
                g.fillRect (juce::Rectangle<float> (2.0f, 2.0f).withCentre (die.getCentre()));
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

// A modulator's value now as 0…1 (bipolar ones from their −1…1), 0 if muted.
float SiltEditor::modLevel01 (int modulator) const
{
    const float v = juce::jlimit (-1.0f, 1.0f, silt.modOutputs[(size_t) modulator].load());
    return silt::Modulator::isBipolar (silt.readModulatorParams (modulator)) ? 0.5f * (v + 1.0f) : juce::jmax (0.0f, v);
}

// The pages (as bits) holding a parameter the modulator is routed to.
int SiltEditor::routedPages (int modulator) const
{
    int pages = 0;
    if (modulator < 0)
        return pages;
    for (const auto& route : silt.allRoutes())
        if (route.modulator == modulator)
        {
            const auto id = silt.targetParameter (route.target).getParameterID();
            for (const auto& e : layout::entries (SiltProcessor::isInstrument))
                if (e.id == id)
                    pages |= 1 << e.page;
        }
    return pages;
}

void SiltEditor::paintOverChildren (juce::Graphics& g)
{
    // While a modulator is armed, the MOD tab carries its colour, and so do
    // the other pages with a control it's routed to: underlined, and tinted
    // with a wash that rises and falls with the modulator itself.
    if (const int armed = silt.armedMod.load(); armed >= 0)
    {
        const auto colour = modColour (armed).base;
        const int routed = routedPages (armed);
        const float wash = 0.06f + 0.4f * modLevel01 (armed);
        for (int p = 0; p < layout::numPages; ++p)
        {
            if (! pageTabs[(size_t) p].isVisible())
                continue;
            const auto tab = pageTabs[(size_t) p].getBounds();
            if (p != page && p != layout::mod && (routed & (1 << p)) != 0)
            {
                g.setColour (colour.withAlpha (wash));
                g.fillRect (tab.toFloat().reduced (1.0f));
            }
            if (p == layout::mod || (p != page && (routed & (1 << p)) != 0))
            {
                g.setColour (colour);
                g.fillRect (tab.getX() + 2, tab.getBottom() - 3, tab.getWidth() - 4, 3);
            }
        }
    }
    if (const int armed = silt.armedMod.load(); armed >= 0)
    {
        static constexpr std::pair<int, int> effects[] { { layout::resonator, SiltProcessor::moduleResonator }, { layout::filter, SiltProcessor::moduleFilter },
                                                         { layout::color, SiltProcessor::moduleColor }, { layout::space, SiltProcessor::moduleSpace } };
        const int routed = routedPages (armed);
        for (const auto& [effectPage, module] : effects)
            if (effectPage != page && (routed & (1 << effectPage)) != 0)
            {
                const auto block = chainView->moduleBounds (module);
                g.setColour (modColour (armed).base);
                g.fillRect (block.getX() + 2, block.getBottom() - 5, block.getWidth() - 6, 3);
            }
    }
    if (dragHover)
    {
        g.setColour (rust);
        g.drawRect (getLocalBounds(), 4);
    }
}
