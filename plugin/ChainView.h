#pragma once

#include "PluginProcessor.h"

// The CHAIN page's picture, over the screen's grain roll: the grains, the four
// modules in the order they run, then the output. Drag a module to move it
// (one undo step); click one to open its page. Modules that aren't doing
// anything (no mix, filter off…) are drawn faint.
namespace silt::ui
{
class ChainView final : public juce::Component
{
public:
    explicit ChainView (SiltProcessor&);

    std::function<void (int module)> onOpen;   // a module was clicked

    void update();   // the timer: repaints when the order or a module's state changed
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static bool moduleActive (const SiltProcessor&, int module);

private:
    std::array<int, SiltProcessor::numModules> currentOrder() const;
    juce::Rectangle<float> blockArea (int position) const;
    int positionAt (float x) const;

    SiltProcessor& silt;
    std::array<int, SiltProcessor::numModules> shownOrder {};
    std::array<bool, SiltProcessor::numModules> shownActive {};
    int dragging = -1;          // the position being dragged, or −1
    float dragOffset = 0.0f;    // pointer x − the block's left edge
    float dragX = 0.0f;         // the dragged block's left edge
    bool moved = false;
};
} // namespace silt::ui
