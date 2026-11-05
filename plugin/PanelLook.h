#pragma once

#include "Palette.h"

#include <functional>

namespace silt::ui
{

// The kit's look, plus what only Silt has: modulation rings on the knobs, and value
// boxes under them that you can click to type in.
class PanelLook final : public kit::Look
{
public:
    PanelLook() : kit::Look (stock()) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& slider) override
    {
        kit::Look::drawRotarySlider (g, x, y, w, h, pos, a0, a1, slider);
        if (drawModulation != nullptr)
        {
            const auto box = juce::Rectangle<int> (x, y, w, h).toFloat();
            drawModulation (g, slider, box.getCentre(), kit::Look::knobOuterRadius (box) + 2.0f, pos);
        }
    }

    // Draws a knob's modulation over it (the editor knows the routes).
    std::function<void (juce::Graphics&, juce::Slider&, juce::Point<float>, float, float)> drawModulation;

    void clearKnobCache() {}   // nothing's cached now: the knob is drawn flat each time

    // The thin outline trays and menus are drawn with.
    static juce::Colour hairline() { return ink.withAlpha (0.32f); }

    // Value boxes: a sunk window.
    static void drawSunkBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, juce::Colour)
    {
        g.setColour (face);
        g.fillRect (r);
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        const auto background = label.findColour (juce::Label::backgroundColourId);
        if (background != lcdGreen)
        {
            LookAndFeel_V4::drawLabel (g, label);
            return;
        }
        drawSunkBox (g, label.getLocalBounds().toFloat(), background, label.findColour (juce::Label::outlineColourId));
        if (! label.isBeingEdited())
        {
            const auto font = monoFont (juce::jmin (12.0f, (float) label.getHeight() * 0.7f));
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (font);
            const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
            g.drawFittedText (label.getText(), area, label.getJustificationType(),
                              juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())), label.getMinimumHorizontalScale());
        }
    }

    // A knob's value box. Clicked to type a value, it stays the same window: the editor
    // is see-through, unoutlined, centred, in the same type.
    struct ValueBox final : juce::Label
    {
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}   // the knob takes the wheel

        juce::TextEditor* createEditorComponent() override
        {
            auto* ed = juce::Label::createEditorComponent();
            ed->setJustification (juce::Justification::centred);
            ed->setBorder ({});
            ed->setIndents (0, 0);
            for (const auto id : { juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId })
                ed->setColour (id, juce::Colours::transparentBlack);
            ed->setColour (juce::TextEditor::textColourId, lcdText);
            ed->setColour (juce::TextEditor::highlightColourId, rust.withAlpha (0.35f));
            ed->setColour (juce::TextEditor::highlightedTextColourId, lcdText);
            ed->setColour (juce::CaretComponent::caretColourId, lcdText);
            ed->applyFontToAllText (monoFont (juce::jmin (12.0f, (float) getHeight() * 0.7f)));
            return ed;
        }
    };

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = new ValueBox();
        l->setJustificationType (juce::Justification::centred);
        l->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
        l->setFont (monoFont (11.5f));
        l->setColour (juce::Label::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        l->setColour (juce::Label::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        l->setColour (juce::Label::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        return l;
    }
};

} // namespace silt::ui
