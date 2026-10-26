#pragma once

#include "Palette.h"

namespace silt::ui
{
// A section's corner grip: drag it to give the section more or less room
// (its neighbours give way), double-click to put the split back. Drawn as
// three ridges across the corner, brighter under the mouse.
class Grip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Corner { bottomRight, topLeft };

    Grip (Corner c, juce::MouseCursor::StandardCursorType cursor, bool onDark) : corner (c), dark (onDark)
    {
        setMouseCursor (cursor);
        setRepaintsOnMouseActivity (true);
        setSize (16, 16);
    }

    std::function<void()> onStart;
    std::function<void (juce::Point<int> fromStart)> onDrag;
    std::function<void()> onReset;

    void paint (juce::Graphics& g) override
    {
        const auto colour = (dark ? screenPale : inkSoft).withAlpha (isMouseOverOrDragging() ? 0.95f : 0.55f);
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        for (int i = 1; i <= 3; ++i)
        {
            const float d = b.getWidth() * (float) i / 3.0f;
            // A dark groove under a light ridge, so each line reads as raised.
            const auto line = corner == Corner::bottomRight
                ? juce::Line<float> (b.getRight() - d, b.getBottom(), b.getRight(), b.getBottom() - d)
                : juce::Line<float> (b.getX(), b.getY() + d, b.getX() + d, b.getY());
            g.setColour (juce::Colours::black.withAlpha (dark ? 0.5f : 0.25f));
            g.drawLine (line.withShortenedStart (0.5f).withShortenedEnd (0.5f), 2.0f);
            g.setColour (colour);
            g.drawLine (line, 1.2f);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onStart)
            onStart();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onDrag)
            onDrag (e.getOffsetFromDragStart());
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onReset)
            onReset();
    }

private:
    Corner corner;
    bool dark;
};
} // namespace silt::ui
