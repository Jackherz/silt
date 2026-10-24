#pragma once

#include "PluginProcessor.h"

// M0 test panel: shows what the host is sending. The real UI (plan §5) is a
// WebView, proven first in Spike B.
class SiltEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit SiltEditor (SiltProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SiltProcessor& silt;

    juce::ToggleButton monitorButton { "MONITOR INPUT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorAttachment;

    float levelDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltEditor)
};
