#pragma once

#include "PluginProcessor.h"

// The panel, drawn natively in the plan §5 palette. A screen on top shows the
// sample and every sounding grain; below it, one page of controls at a time
// (GRAIN / SHAPE / PLAY), at most 8 each so a page maps onto an 8-knob controller.
class SiltEditor final : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit SiltEditor (SiltProcessor&);
    ~SiltEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum class Page { grain, shape, play };

    // One control in the page row: a knob or a menu, its label, and its slot.
    struct Control
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::ComboBox* menu = nullptr;    // set for menu slots instead of the slider
        juce::String label;
        Page page = Page::grain;
        int slot = 0;
        std::function<bool()> shown;       // alternatives sharing a slot
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        juce::Component& component() { return menu != nullptr ? static_cast<juce::Component&> (*menu) : slider; }
    };

    class PanelLook;
    class GrainScreen;

    void timerCallback() override;
    Control& addKnob (Page, int slot, const char* paramId, const juce::String& label);
    Control& addMenu (Page, int slot, juce::ComboBox&, const juce::String& label);
    void showPage (Page);
    void updateVisibility();
    juce::Rectangle<int> slotBounds (int slot) const;
    juce::String statusText() const;
    void paintPreview (juce::Graphics&);
    float param (const char* id) const { return silt.params.getRawParameterValue (id)->load(); }

    SiltProcessor& silt;
    std::unique_ptr<PanelLook> look;
    std::unique_ptr<GrainScreen> screen;

    std::vector<std::unique_ptr<Control>> controls;
    std::array<juce::TextButton, 3> pageTabs;
    Page page = Page::grain;

    juce::TextButton armButton, loadButton { "LOAD" }, sizeSyncButton { "SYNC" };
    juce::ToggleButton monitorButton { "MONITOR" };
    juce::ComboBox captureBars, endMode, rateMode;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> menuAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, strip, pagePanel, controlRow;
    int seenGeneration = -1;
    bool dragHover = false;
    float levelDb = -100.0f;
    std::array<float, 7> previewSeen {};   // the values the SHAPE preview was last drawn with

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltEditor)
};
