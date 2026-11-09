#pragma once

#include "kit/Bench.h"
#include "ModPanel.h"
#include "PageLayout.h"
#include "PluginProcessor.h"
#include "Tour.h"

#include "SpaceScene.h"

namespace silt::ui
{
class PanelLook;
class ChainView;
class GrainScreen;
class SampleGainBox;
class DragOutHandle;
class RandomAmountBox;
class Grip;
class Divider;
}
class PresetManager;
namespace kit { class PresetBrowser; class PresetBar; namespace bench { class Strip; struct Moment; } }
using PresetBrowser = kit::PresetBrowser;
using PresetBar = kit::PresetBar;

// The panel, drawn natively in the plan §5 palette. A screen on top shows the
// sample and every sounding grain; below it, one page of controls at a time,
// at most 8 each so a page maps onto an 8-knob controller, built from the page
// table the controller also reads (PageLayout). Left of the page, the mod rail:
// the four modulators, always in view.
//
// Modulation gestures (docs/mod-section-design.md): drag a knob for its value,
// Alt-drag for the depth of the armed modulator (or the knob's only route),
// right-click for its routes; drag a chip onto a knob to make a route; keys
// 1–4 arm a modulator and Esc disarms; Cmd/Ctrl+Z undoes route changes.
class SiltEditor final : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropContainer,
                         private juce::Timer
{
public:
    explicit SiltEditor (SiltProcessor&);
    static inline bool tourOnFirstOpen = true;   // tests and the snapshot tool turn it off
    static inline int startTheme = -1;           // ≥ 0: this theme, not the saved one (the snapshot tool)
    static inline int startGrainView = -2;       // ≥ −1: this grain view, not the saved one (the snapshot tool)
    static inline juce::Point<float> startZoom { 0.0f, 1.0f };   // the waveform's view on opening (the snapshot tool)
    ~SiltEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    void showPage (int page);
    void refreshLayout();                          // re-reads which alternatives each slot shows
    void showRoutes (int modulatorFilter = -1);    // the route list; −1 shows every modulator
    void startTour();                              // from the ? button, and once on a first open
    void setTheme (int index);                     // one of themes(), kept for this computer
    bool routesShown() const;
    juce::String visibleParameterId (int page, int slot) const;   // for tests: what the slot shows
    juce::String displayText() const { return statusText(); }     // for tests: what the LCD says
    void showHelpFor (const juce::String& id);                     // hover help for a part (empty: none)
    juce::Slider* findKnob (const juce::String& parameterId);     // for tests
    void dragScreenEdge (int pixels);                              // the same, between the screen and the knobs
    void sendMoment();                                             // the Bench's SEND: bars of the output, or the sample
    void updateBench();                                            // from the timer; also for tests
    void openModPage (int modulator, bool wasShown);               // a chip's double-click, and Shift+1 to 4
    void setBenchSource (int barsOrZero);                          // what SEND sends; also for tests
    void takeMoment();                                             // the Bench's TAKE: a sent moment becomes the sample

    // Which modulator Alt-drag sets on a knob: the armed one; else the knob's
    // only route; else the last one used.
    static int depthModulatorFor (int armed, const std::array<float, SiltProcessor::numModulators>& amounts, int lastUsed);

    // A knob that moves ten times finer while Shift is held as the drag starts,
    // sets a modulation depth on Alt-drag, and takes a mod chip dropped on it.
    struct FineSlider final : juce::Slider, public juce::DragAndDropTarget
    {
        FineSlider() : juce::Slider (RotaryHorizontalVerticalDrag, TextBoxBelow) {}

        int target = -1;                                 // modulation target, −1: none
        std::function<int()> depthModulator;
        std::function<float (int modulator)> amountOf;
        std::function<void (int modulator, float amount)> setAmount;
        std::function<void (int modulator, bool dragging)> depthDragged;   // for the LCD readout
        std::function<void()> showMenu;                  // right-click
        std::function<void()> routeGesture;              // a new undo step begins
        std::function<void (int modulator)> togglePolarity;
        std::function<void (int modulator)> ringHover;   // −1: left the rings
        std::function<bool()> clearModulation;           // Delete / Backspace: false if there was none
        int dropHover = -1;                              // a chip held over it

        // The rings as last drawn (set while drawing): which modulator each ring
        // shows, and whether an armed modulator's hint ring follows them.
        juce::Point<float> ringCentre;
        float ringBase = 0.0f;
        std::vector<int> ringModulators;
        int armedHintModulator = -1;
        int hoverRing = -1;
        int modulatorAtRing (juce::Point<float>) const;  // −1: not on a ring

        // The depth gesture, also used directly by tests.
        void beginDepth (int modulator);
        void dragDepth (float pixelsUp, bool fine);
        void endDepth();
        bool isSettingDepth() const noexcept { return depthModulatorNow >= 0; }

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        int depthModulatorNow = -1;
        float depthRunning = 0.0f, depthPixels = 0.0f;   // a depth drag's amount so far, unrounded, and where it was
        juce::Point<float> dragLast;                     // a value drag: where the mouse last was
        float dragRunning = 0.0f;                        // and the value so far (0…1), unrounded
    };

private:
    struct Control
    {
        const silt::layout::Entry* entry = nullptr;
        std::unique_ptr<FineSlider> slider;
        std::unique_ptr<juce::ComboBox> menu;
        std::unique_ptr<juce::TextButton> button;
        std::unique_ptr<silt::ui::ModBanner> banner;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;

        juce::Component& component() const;
    };

    void timerCallback() override;
    void buildControl (const silt::layout::Entry&);
    void wireModulation (FineSlider&);
    void showKnobMenu (FineSlider&);
    void drawModulation (juce::Graphics&, juce::Slider&, juce::Point<float> centre, float radius, float position);
    void selectModulator (int m, bool arm);
    void updateVisibility();
    juce::Rectangle<int> slotBounds (int slot) const;
    juce::String statusText() const;
    void paintPreview (juce::Graphics&);
    juce::Rectangle<int> shapePreviewArea() const;
    float param (const char* id) const
    {
        const auto* value = silt.params.getRawParameterValue (id);
        jassert (value != nullptr);   // a typo in an id
        return value != nullptr ? value->load() : 0.0f;
    }
    // The value as heard: where modulation has it now, for a modulated control.
    float heard (const char* id) const
    {
        if (const int t = silt.modulationTarget (id); t >= 0)
            return silt.targetParameter (t).convertFrom0to1 (silt.modulatedValue01 (t));
        return param (id);
    }

    SiltProcessor& silt;
    std::unique_ptr<silt::ui::PanelLook> look;
    std::unique_ptr<silt::ui::GrainScreen> screen;
    std::unique_ptr<silt::ui::DragOutHandle> dragOut;
    std::unique_ptr<silt::ui::ChainView> chainView;
    silt::ui::ModActions modActions;
    std::unique_ptr<silt::ui::ModRail> rail;
    std::unique_ptr<silt::ui::RouteList> routeList;
    std::unique_ptr<PresetManager> presetLibrary;
    std::unique_ptr<PresetBar> presetBar;
    std::unique_ptr<PresetBrowser> presetBrowser;
public:
    PresetBrowser* getPresetBrowser() const { return presetBrowser.get(); }   // for tests
private:
    std::unique_ptr<silt::ui::SampleGainBox> sampleGainBox;
    juce::String presetShown;
    // The Bench: a moment an effect in the range sent, for this instrument to take.
    std::unique_ptr<kit::bench::Strip> benchStrip;
    juce::int64 momentTaken = 0, momentSent = 0;
    std::optional<kit::bench::Moment> waiting;
    double benchCheckedAt = 0.0;
    juce::ComboBox benchSource;
    std::vector<float> insetSeen;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<std::unique_ptr<Control>> controls;
    // A page key: a right-click (or Ctrl-click) opens its menu and never
    // presses the key, so the page and the lit key stay as they were.
    struct PageKey final : juce::TextButton
    {
        std::function<void()> onMenu;
        void mouseDown (const juce::MouseEvent& e) override
        {
            menuPress = e.mods.isPopupMenu();
            if (menuPress)
            {
                if (onMenu)
                    onMenu();
                return;
            }
            juce::TextButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! menuPress)
                juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! menuPress)
                juce::TextButton::mouseUp (e);
            menuPress = false;
        }
        bool menuPress = false;
    };
    std::array<PageKey, silt::layout::numPages> pageTabs;
    int page = 0;
    std::vector<bool> shownNow;   // per control: what updateVisibility last showed

    juce::TextButton armButton, loadButton { "LOAD" }, learnButton { "LEARN" }, routesButton { "MOD ROUTES" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::String flash;                                // a moment's message in the LCD
    juce::String loadNoteSeen;
    juce::uint32 flashUntil = 0;
    void stepUndo (bool redo);
    void chooseFile();   // LOAD's file chooser
    void updateScope();
    int resonatorHalfSeen = -1;
    std::unique_ptr<silt::ui::SpaceScene> spaceScene = std::make_unique<silt::ui::SpaceScene>();
    silt::ui::SpaceScene::Settings spaceSettings;
    void updateSpaceSettings (float seconds);
    double spaceTickAt = 0.0;
    bool spaceSceneShown = false;
    std::vector<silt::ui::Tour::Step> tourSteps();
    std::unique_ptr<silt::ui::Tour> tour;
    juce::TextButton tourButton { "?" };
    int pageBeforeTour = 0;
    int pageBeforeMod = 0;       // where a chip's double-click goes back to
    void restyle();
    void applyGrainLook();

    // Hover help: finds the part under the mouse (or its nearest parent) with
    // a "help" property, and passes its id on.
    struct HelpWatcher final : juce::MouseListener
    {
        std::function<void (const juce::String& id)> onHover;
        void mouseEnter (const juce::MouseEvent& e) override
        {
            for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
                if (c->getProperties().contains ("help"))
                {
                    if (onHover)
                        onHover (c->getProperties()["help"].toString());
                    return;
                }
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            if (onHover)
                onHover ({});
        }
    };
    HelpWatcher helpWatcher;
    juce::String hoverHelp;
    std::uint64_t overLimitSeen = 0;
    int editedCheck = 0;
    std::array<juce::Rectangle<int>, 3> keyGroups;   // the page keys' three groups
    int seenCheck = 0;
    bool windowSeen = true;
    juce::uint32 fullUntil = 0;
    bool hoverHelpOn = true;
    int grainView = -1;   // the ? menu's GRAIN VIEW: −1 matches the theme, else a GrainScreen::FieldLook
    static void styleValueBox (juce::Slider&);
    void randomise();        // the open page
    void resetPages (int which);   // RESET: one page, or all (−1)
    void showRandomMenu();
    juce::TextButton randomButton { "RANDOM" };
    std::unique_ptr<silt::ui::RandomAmountBox> randomAmountBox;
    std::vector<float> scopeIn, scopeOut;
    unsigned scopeSeen = 0;
    bool screenShowsScope = false;
    juce::ToggleButton monitorButton { "MONITOR" };
    juce::ComboBox captureBars;
    juce::TextButton resampleButton { "RESAMPLE" };   // ARM records Silt's own output
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> resampleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> captureBarsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, lcdArea, pagePanel, controlRow, railArea, tray, wellArea, sourcePanel, levelsPanel, chainPanel, benchArea;
    int flexibleHeight = 0, contentWidth = 0;          // what the tiled sections share
    juce::Image backdrop;                              // the panel's still parts, drawn once per size and theme
    std::unique_ptr<silt::ui::Divider> screenDivider;
    float layoutAtDragStart = 0.0f;
    struct SlotParts { juce::Rectangle<int> label, control, under; };
    SlotParts slotParts (int slot) const;
    static bool pageHasKey (int p) { return p != silt::layout::chain; }   // the chain is on the panel
    SlotParts partsOf (const silt::layout::Entry&) const;
    void makeGrips();
    static void drawSunk (juce::Graphics&, juce::Rectangle<float> area, juce::Colour face);
    void resetLayout();
    int seenGeneration = -1;
    bool dragHover = false, screenWasLive = false;
    int learnSeen = -1, armedSeen = -2, modShownSeen = -1, tabTintSeen = -2;
    int routedPages (int modulator) const;   // bits: the pages with a control it's routed to
    float modLevel01 (int modulator) const;   // its value now, 0…1
    float tabWashSeen = -1.0f;
    float levelDb = -100.0f;
    int timerTick = 0;
    juce::String statusSeen;
    juce::uint32 inputHeardAt = 0;   // when the input last had sound in it
    // No input for two seconds (no bus, or nothing routed to it): LIVE and ARM say where it comes from.
    bool inputSilent() const
    {
        return silt.captureSource.load() == SiltProcessor::CaptureSource::none || juce::Time::getMillisecondCounter() - inputHeardAt > 2000;
    }
    std::array<float, 7> previewSeen {};               // the values the SHAPE preview was last drawn with
    int lastUsedMod = 0;
    juce::String depthReadout;                         // shown in the LCD during a depth drag
    juce::String hoverHint;                            // shown in the LCD while over a modulation ring
    std::array<silt::GrainView, SiltProcessor::maxGrainViews> viewsScratch {};   // per editor, not shared

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltEditor)
};
