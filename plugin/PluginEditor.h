#pragma once

#include "PluginProcessor.h"

// M2 working panel, drawn natively in the plan §5 palette: sample view, capture
// and load, and every parameter as a knob. The finished UI (plan §5) moves to
// a WebView once Spike B proves it out.
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
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class PanelLook;
    class SampleView;

    void timerCallback() override;
    void addKnob (std::vector<std::unique_ptr<Knob>>& row, const char* paramId, const juce::String& label);
    void layoutRow (std::vector<std::unique_ptr<Knob>>& row, juce::Rectangle<int> area);
    juce::String statusText() const;

    SiltProcessor& silt;
    std::unique_ptr<PanelLook> look;
    std::unique_ptr<SampleView> sampleView;

    std::vector<std::unique_ptr<Knob>> grainKnobs, regionKnobs, voiceKnobs, outKnobs;

    // Alternatives shown in place of the Size and Rate knobs: Size and Rate in note
    // values (SYNC, Rate mode SYNC), Rate as a grain count and Size on its short
    // Grains-mode range (Rate mode GRAINS).
    std::vector<std::unique_ptr<Knob>> syncedKnobs;
    juce::TextButton sizeSyncButton { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sizeSyncAttachment;
    juce::ComboBox rateMode;   // FREE (Hz), SYNC (note value) or GRAINS (count)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateModeAttachment;

    juce::TextButton armButton { "ARM" }, loadButton { "LOAD" };
    juce::ComboBox captureBars, endMode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> captureBarsAttachment, endModeAttachment;
    juce::ToggleButton monitorButton { "MONITOR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, captureRow, grainSection, regionSection, voiceSection, outSection;
    int seenGeneration = -1;
    bool dragHover = false;
    float levelDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltEditor)
};
