#pragma once

#include "PageColours.h"
#include "Palette.h"

#include <functional>

namespace silt::ui
{

// Silt's own look on the kit's: frameless rounded keys and menus, knobs inside an arc that
// shows the value, modulation rings round them, and value boxes you can click to type in.
class PanelLook final : public kit::Look
{
public:
    PanelLook() : kit::Look (stock()) {}

    // A flat disc with a pointer, inside an arc that fills to the value (from the middle
    // on a knob whose range runs either side of zero). The modulation rings sit outside it.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& slider) override
    {
        const auto box = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float side = juce::jmin (box.getWidth(), box.getHeight()), u = side / 104.0f;
        const auto area = box.withSizeKeepingCentre (side, side).reduced (15.0f * u);
        const float r = area.getWidth() * 0.5f, track = r + 8.0f * u;
        const auto c = area.getCentre();
        const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
        const auto arc = [&] (float from, float to, juce::Colour colour)
        {
            juce::Path path;
            path.addCentredArc (c.x, c.y, track, track, 0.0f, a0 + from * (a1 - a0), a0 + to * (a1 - a0), true);
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (2.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float origin = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
        arc (0.0f, 1.0f, s.ink.withAlpha (0.16f));
        arc (juce::jmin (origin, pos), juce::jmax (origin, pos),
             slider.isColourSpecified (juce::Slider::rotarySliderFillColourId) ? slider.findColour (juce::Slider::rotarySliderFillColourId)
                                                                                : s.ink.withAlpha (slider.isMouseOverOrDragging() ? 1.0f : 0.82f));
        g.setColour (s.shadow.withAlpha (0.45f));
        g.fillEllipse (area.translated (0.0f, 2.0f * u));
        g.setColour (knobFace (slider));
        g.fillEllipse (area);
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f * u, -r * 0.86f, 3.0f * u, r * 0.5f, 1.5f * u);
        g.setColour (knobPointer (slider));
        g.fillPath (pointer, juce::AffineTransform::rotation (a0 + pos * (a1 - a0)).translated (c));
        if (drawModulation != nullptr)
            drawModulation (g, slider, c, kit::Look::knobOuterRadius (box) + 2.0f, pos);
    }

    // A key is a filled, rounded plate with no frame: faint at rest, stronger under the
    // mouse, solid while it's on. "accent" fills it in the accent; "led" keeps it faint with
    // a light that says whether it's on; a page's key ("page") wears its colour, as a strip
    // along the top and the whole key while it's open.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        const auto& properties = b.getProperties();
        const bool flat = properties.getWithDefault ("flat", false), led = properties.getWithDefault ("led", false);
        const bool accent = properties.getWithDefault ("accent", false), page = properties.contains ("page");
        const bool on = b.getToggleState() && ! led, enabled = b.isEnabled();
        const auto r = (flat ? b.getLocalBounds() : b.getLocalBounds().withTrimmedRight (2).withTrimmedBottom (2)).toFloat().reduced (1.0f);
        const auto tint = page ? pageColour ((int) properties["page"]) : accent ? s.accent : s.ink;
        g.setColour (on || accent ? tint.interpolatedWith (s.panel, down ? 0.2f : hover ? 0.1f : 0.0f)
                                  : s.ink.withAlpha (! enabled ? 0.04f : down ? 0.22f : hover ? 0.14f : 0.08f));
        g.fillRoundedRectangle (r, corner);
        if (page && ! on)
        {
            g.setColour (tint.withAlpha (0.85f));
            g.fillRoundedRectangle (r.withHeight (3.0f), 1.5f);
        }
        if (led)
        {
            g.setColour (b.getToggleState() ? s.lit : s.ink.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 10.0f, r.getCentreY() }));
        }
    }

    // A menu: the same plate, with a chevron.
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> ((float) width, (float) height).withTrimmedRight (2.0f).withTrimmedBottom (2.0f).reduced (1.0f);
        g.setColour (s.ink.withAlpha (box.isMouseOver (true) ? 0.14f : 0.08f));
        g.fillRoundedRectangle (r, corner);
        const float cx = r.getRight() - r.getHeight() * 0.5f, cy = r.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (s.ink.withAlpha (0.8f));
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Names in the sans face; values and readouts keep the display's monospace.
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return panelFont (juce::jlimit (10.0f, 12.5f, (float) h * 0.42f)); }
    juce::Font getComboBoxFont (juce::ComboBox& box) override { return panelFont (juce::jlimit (10.0f, 12.5f, (float) box.getHeight() * 0.42f)); }
    juce::Font getPopupMenuFont() override { return panelFont (13.0f); }

    static constexpr float corner = 4.0f;

    // Draws a knob's modulation over it (the editor knows the routes).
    std::function<void (juce::Graphics&, juce::Slider&, juce::Point<float>, float, float)> drawModulation;

    void clearKnobCache() {}   // nothing's cached now: the knob is drawn flat each time

    // The thin outline trays and menus are drawn with.
    static juce::Colour hairline() { return ink.withAlpha (0.32f); }

    // Value boxes: a sunk window.
    static void drawSunkBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, juce::Colour)
    {
        g.setColour (face);
        g.fillRoundedRectangle (r, 3.0f);
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
