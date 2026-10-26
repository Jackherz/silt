#pragma once

#include "Palette.h"
#include "PluginProcessor.h"

namespace silt::ui
{
// RANDOM's amount, beside the button: how far RANDOM moves things. Drag up or
// down (Shift: finer), scroll, double-click for 50%.
class RandomAmountBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RandomAmountBox (SiltProcessor& p) : silt (p)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTooltip ("How far RANDOM moves things: a little (nudge) to all the way (anything goes). Drag, scroll, double-click for 50%");
    }

    void paint (juce::Graphics& g) override
    {
        // A sunk window, like the knobs' value boxes.
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 4.5f);
        g.setColour (lcdGreen.darker (0.8f));
        g.fillRect (r);
        const auto face = r.reduced (1.0f);
        g.setGradientFill (juce::ColourGradient (lcdGreen.darker (0.12f), 0.0f, face.getY(), lcdGreen.brighter (0.04f), 0.0f, face.getBottom(), false));
        g.fillRect (face);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, face.getY(), juce::Colours::transparentBlack, 0.0f, face.getY() + 4.0f, false));
        g.fillRect (face.withHeight (4.0f));
        g.setColour (lcdText);
        g.setFont (monoFont (13.0f));
        g.drawText (juce::String (juce::roundToInt (silt.randomAmount.load() * 100.0f)) + "%", r, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override { atStart = silt.randomAmount.load(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        set (atStart - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 1000.0f : 200.0f));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (0.5f); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        set (silt.randomAmount.load() + (wheel.deltaY > 0.0f ? 0.05f : -0.05f));
    }

    void set (float amount)
    {
        silt.randomAmount.store (juce::jlimit (0.01f, 1.0f, std::round (amount * 100.0f) / 100.0f));
        repaint();
    }

private:
    SiltProcessor& silt;
    float atStart = 0.5f;
};
} // namespace silt::ui
