#pragma once

#include "PluginProcessor.h"

// The panel, drawn natively in the plan §5 palette. A screen on top shows the
// sample and every sounding grain; below it, one page of controls at a time
// (GRAIN / SHAPE / PITCH / PLAY / RESONATOR / FILTER / COLOR / SPACE / MOD), at most 8 each so a page maps onto an 8-knob controller.
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
    enum class Page { grain, shape, pitch, play, resonator, filter, color, space, mod };

    // One control in the page row: a knob or a menu, its label, and its slot.
    // A knob that moves ten times finer while Shift is held as the drag starts.
    // While ASSIGN is on, dragging sets the modulation amount instead of the
    // value; right-click offers to clear the knob's modulation.
    struct FineSlider final : juce::Slider
    {
        FineSlider() : juce::Slider (RotaryHorizontalVerticalDrag, TextBoxBelow) {}

        std::function<bool()> assigning;               // dragging sets an amount
        std::function<float()> amount;                 // the amount being set
        std::function<void (float)> setAmount;
        std::function<void()> showMenu;                // right-click

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && showMenu != nullptr)
            {
                showMenu();
                return;
            }
            settingAmount = assigning != nullptr && assigning();
            if (settingAmount)
            {
                amountAtStart = amount();
                return;
            }
            setMouseDragSensitivity (e.mods.isShiftDown() ? 2500 : 250);
            juce::Slider::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (settingAmount)
                setAmount (juce::jlimit (-1.0f, 1.0f, amountAtStart - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 1000.0f : 150.0f)));
            else
                juce::Slider::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! settingAmount)
                juce::Slider::mouseUp (e);
            settingAmount = false;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (assigning != nullptr && assigning())
                setAmount (0.0f);   // ASSIGN on: double-click removes this modulator's route
            else
                juce::Slider::mouseDoubleClick (e);
        }

    private:
        bool settingAmount = false;
        float amountAtStart = 0.0f;
    };

    struct Control
    {
        FineSlider slider;
        juce::ComboBox* menu = nullptr;    // set for menu slots instead of the slider
        juce::Button* button = nullptr;    // set for button slots instead of the slider
        juce::String label;
        Page page = Page::grain;
        int slot = 0;
        std::function<bool()> shown;       // alternatives sharing a slot
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        juce::Component& component()
        {
            if (menu != nullptr)
                return *menu;
            if (button != nullptr)
                return *button;
            return slider;
        }
    };

    class PanelLook;
    class GrainScreen;

    void timerCallback() override;
    Control& addKnob (Page, int slot, const char* paramId, const juce::String& label);
    Control& addMenu (Page, int slot, juce::ComboBox&, const juce::String& label);
    Control& addButton (Page, int slot, juce::Button&, const char* paramId, const juce::String& label);
    Control& addAction (Page, int slot, juce::Button&, const juce::String& label);
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
    std::array<juce::TextButton, 9> pageTabs;
    Page page = Page::grain;

    juce::TextButton armButton, loadButton { "LOAD" }, learnButton { "LEARN" }, sizeSyncButton { "SYNC" }, delaySyncButton { "SYNC" };
    juce::TextButton freezeButton { "FREEZE" }, newPatternButton { "NEW" }, followButton { "FOLLOW" };
    juce::TextButton spaceSyncButton { "SYNC" }, spaceFreezeButton { "FREEZE" };
    juce::ComboBox modSelect;
    std::array<juce::ComboBox, SiltProcessor::numModulators> modType, modSource;
    std::array<juce::TextButton, SiltProcessor::numModulators> modSync, modRetrig, modRepeat;
    juce::TextButton assignButton { "ASSIGN" };
    int modShownSeen = -1;
    void wireModulation (Control&);
    void drawModulation (juce::Graphics&, juce::Slider&, juce::Point<float> centre, float radius, float position);
    juce::ToggleButton monitorButton { "MONITOR" };
    juce::ComboBox captureBars, endMode, rateMode, keyMode, liveTrigger, scaleMenu, keyMenu, snapMenu, resNotes, filterMode, spaceMode;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> menuAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> header, strip, pagePanel, controlRow;
    int seenGeneration = -1;
    bool dragHover = false, screenWasLive = false;
    int learnSeen = -1;
    float levelDb = -100.0f;
    std::array<float, 7> previewSeen {};   // the values the SHAPE preview was last drawn with

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltEditor)
};
