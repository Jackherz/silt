#pragma once

#include "PluginProcessor.h"

// The chain, always on the panel: the four modules in the order they run, from any
// page. Drag a module to move it (one undo step); click one to open its page, or its
// switch to turn it on or off. The switch only says ON or OFF. A module that's on says in
// words why it's idle (MIX 0, OPEN…); otherwise a meter shows how much it's changing the
// sound, measured, and the arrow after it lights while sound is leaving it.
namespace silt::ui
{
class ChainView final : public juce::Component
{
public:
    explicit ChainView (SiltProcessor&);

    std::function<void (int module)> onOpen;   // a module was clicked
    std::function<void (int module)> onMenu;   // a module was right-clicked

    // The module whose page is open (−1: none) and, for RES, which of its pages of controls.
    void setOpen (int module, int half);
    juce::Rectangle<int> moduleBounds (int module) const;

    void update();   // the timer: repaints when the order or a module's state changed
    void updateMeters();   // the timer, each frame: the meters and arrows, from the audio
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static bool moduleActive (const SiltProcessor&, int module);
    static bool moduleOn (const SiltProcessor&, int module);
    // Why a module that's on isn't doing anything, or nothing if it is.
    static juce::String idleReason (const SiltProcessor&, int module);
    juce::Rectangle<float> switchArea (int position) const;   // a block's ON switch

private:
    std::array<int, SiltProcessor::numModules> currentOrder() const;
    juce::Rectangle<float> blockArea (int position) const;
    int positionAt (float x) const;

    SiltProcessor& silt;
    std::array<int, SiltProcessor::numModules> shownOrder {};
    std::array<bool, SiltProcessor::numModules> shownActive {}, shownOn {};
    std::array<float, SiltProcessor::numModules> changeShown {}, outputShown {};   // 0…1, as drawn
    std::array<int, SiltProcessor::numModules> meterPixels {}, arrowLit {};        // what was last drawn
    int dragging = -1;          // the position being dragged, or −1
    float dragOffset = 0.0f;    // pointer x − the block's left edge
    float dragX = 0.0f;         // the dragged block's left edge
    bool moved = false;
    int openModule = -1, openHalf = 0;
};
} // namespace silt::ui
