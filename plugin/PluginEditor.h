#pragma once

#include "ModPanel.h"
#include "PageLayout.h"
#include "PluginProcessor.h"

namespace silt::ui
{
class PanelLook;
class GrainScreen;
}

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
    juce::String visibleParameterId (int page, int slot) const;   // for tests: what the slot shows
    juce::Slider* findKnob (const juce::String& parameterId);     // for tests

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
        int dropHover = -1;                              // a chip held over it

        // The depth gesture, also used directly by tests.
        void beginDepth (int modulator);
        void dragDepth (float pixelsUp, bool fine);
        void endDepth();
        bool isSettingDepth() const noexcept { return depthModulatorNow >= 0; }

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        int depthModulatorNow = -1;
        float amountAtStart = 0.0f;
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
    void paintStripes (juce::Graphics&);
    float param (const char* id) const
    {
        const auto* value = silt.params.getRawParameterValue (id);
        jassert (value != nullptr);   // a typo in an id
        return value != nullptr ? value->load() : 0.0f;
    }

    SiltProcessor& silt;
    std::unique_ptr<silt::ui::PanelLook> look;
    std::unique_ptr<silt::ui::GrainScreen> screen;
    silt::ui::ModActions modActions;
    std::unique_ptr<silt::ui::ModRail> rail;
    std::unique_ptr<silt::ui::RouteList> routeList;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<std::unique_ptr<Control>> controls;
    std::array<juce::TextButton, silt::layout::numPages> pageTabs;
    int page = 0;
    std::vector<bool> shownNow;   // per control: what updateVisibility last showed

    juce::TextButton armButton, loadButton { "LOAD" }, learnButton { "LEARN" }, routesButton { "ROUTES" };
    juce::ToggleButton monitorButton { "MONITOR" };
    juce::ComboBox captureBars;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> captureBarsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, stripesArea, strip, pagePanel, controlRow, railArea;
    int seenGeneration = -1;
    bool dragHover = false, screenWasLive = false;
    int learnSeen = -1, armedSeen = -2, modShownSeen = -1;
    float levelDb = -100.0f;
    std::array<float, 7> previewSeen {};               // the values the SHAPE preview was last drawn with
    std::array<float, SiltProcessor::numModulators> stripesSeen {};
    int lastUsedMod = 0;
    juce::String depthReadout;                         // shown in the LCD during a depth drag
    std::array<silt::GrainView, SiltProcessor::maxGrainViews> viewsScratch {};   // per editor, not shared

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltEditor)
};
