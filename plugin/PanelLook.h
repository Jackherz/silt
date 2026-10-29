#pragma once

#include "Palette.h"

#include <functional>
#include <map>

namespace silt::ui
{

// Knobs with a printed scale, keys with a lip, LCD value boxes (plan §5 "Parts").
class PanelLook final : public juce::LookAndFeel_V4
{
public:
    PanelLook() { applyColours(); }

    // The palette's colours for JUCE's own parts; again after a theme change.
    void applyColours()
    {
        setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::outlineColourId, hairline());
        setColour (juce::ComboBox::arrowColourId, inkSoft);
        setColour (juce::PopupMenu::backgroundColourId, bone);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour (juce::PopupMenu::highlightedTextColourId, bone);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, ink);
        setColour (juce::ToggleButton::tickDisabledColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, chassis);
    }

    // The thin outline every key, menu and tray is drawn with.
    static juce::Colour hairline() { return ink.withAlpha (0.32f); }

    // Keys on the header band (always dark) draw in its text colour, not the
    // panel's ink: marked "onFrame", on themselves or a parent.
    static bool onHeader (const juce::Component& c)
    {
        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
            if (p->getProperties().getWithDefault ("onFrame", false))
                return true;
        return false;
    }
    static juce::Colour inkFor (const juce::Component& c) { return onHeader (c) ? onFrame : ink; }

    // A flat cream knob with depth: a recessed collar with ticks cut round
    // it, a domed cap lit from above-left that casts a shadow on the panel,
    // and a dot pressed into the cap at the value. The scale is fixed and the
    // cap turns: 0 at about 7 o'clock, full at 5 (270°). All but the dot is
    // drawn once per size and theme.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float, float, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        g.drawImage (faceFor (w, h, scale), bounds);

        const auto centre = bounds.getCentre().translated (0.0f, 3.0f);
        const float collar = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 8.0f;
        const float cap = collar * capShare;
        const float size = juce::jmax (4.0f, cap * 0.24f);
        const auto dot = juce::Rectangle<float> (size, size).withCentre (centre.translated (0.0f, -1.0f)
                             .getPointOnCircumference (cap * 0.62f, juce::degreesToRadians (-135.0f + 270.0f * pos)));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillEllipse (dot.translated (0.0f, 0.8f));   // its lower lip catches the light: it's pressed in
        g.setColour (knobDot);
        g.fillEllipse (dot);

        if (drawModulation != nullptr)
            drawModulation (g, slider, centre, collar + 4.0f, pos);
    }

    // Draws a knob's modulation over it (the editor knows the routes).
    std::function<void (juce::Graphics&, juce::Slider&, juce::Point<float>, float, float)> drawModulation;

    void clearKnobCache() { faces.clear(); }   // after a theme change

private:
    static constexpr float capShare = 0.68f;   // the cap's radius against the collar's
    std::map<juce::String, juce::Image> faces;

    const juce::Image& faceFor (int w, int h, float scale)
    {
        const auto key = juce::String (w) + "x" + juce::String (h) + "@" + juce::String (scale, 2);
        if (const auto it = faces.find (key); it != faces.end())
            return it->second;
        if (faces.size() >= 4)
            faces.clear();   // a window being resized passes through many sizes: keep just the latest few

        auto& image = faces[key];
        image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));

        const auto centre = juce::Point<float> ((float) w * 0.5f, (float) h * 0.5f + 3.0f);
        const float collar = (float) juce::jmin (w, h) * 0.5f - 8.0f;
        const float cap = collar * capShare;
        const auto collarBox = juce::Rectangle<float> (collar * 2.0f, collar * 2.0f).withCentre (centre);

        // The collar: a recess, shaded where its rim hides the light, with a
        // faint light lip along the bottom.
        g.setGradientFill (juce::ColourGradient (knobCollar.darker (0.4f), 0.0f, collarBox.getY(), knobCollar.brighter (0.12f), 0.0f, collarBox.getBottom(), false));
        g.fillEllipse (collarBox);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        juce::Path lip;
        lip.addCentredArc (centre.x, centre.y, collar - 0.5f, collar - 0.5f, 0.0f, juce::MathConstants<float>::pi * 0.6f, juce::MathConstants<float>::pi * 1.4f, true);
        g.strokePath (lip, juce::PathStrokeType (1.0f));

        // Ticks round the collar: 21, a long one every five.
        for (int n = 0; n <= 20; ++n)
        {
            const bool major = n % 5 == 0;
            const float angle = juce::degreesToRadians (-135.0f + 13.5f * (float) n);
            const float outer = collar - 3.0f, inner = outer - (major ? collar * 0.17f : collar * 0.09f);
            g.setColour (knobTick.withAlpha (major ? 0.9f : 0.45f));
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (inner, angle), centre.getPointOnCircumference (outer, angle)), major ? 1.6f : 1.1f);
        }

        // The cap's shadow, soft and cast down.
        const auto capCentre = centre.translated (0.0f, -1.0f);
        for (int i = 0; i < 4; ++i)
        {
            const float r = cap + 3.0f - (float) i;
            g.setColour (juce::Colours::black.withAlpha (0.13f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (capCentre.translated (0.0f, 3.5f)));
        }

        // The cap: domed, lit from above-left, darker toward its edge.
        const auto capBox = juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (capCentre);
        juce::ColourGradient dome (knobFace.brighter (0.35f), capCentre.x - cap * 0.3f, capCentre.y - cap * 0.4f,
                                   knobFace.darker (0.28f), capCentre.x + cap * 0.75f, capCentre.y + cap * 0.95f, true);
        dome.addColour (0.35, knobFace);
        g.setGradientFill (dome);
        g.fillEllipse (capBox);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        juce::Path shine;
        shine.addCentredArc (capCentre.x, capCentre.y, cap - 1.2f, cap - 1.2f, 0.0f, -juce::MathConstants<float>::pi * 0.45f, juce::MathConstants<float>::pi * 0.2f, true);
        g.strokePath (shine, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (capBox, 1.0f);
        return image;
    }

public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        // A key: a capsule drawn in a hairline. A key that's on (the page
        // shown) is filled in ink; RECORD-type keys (the "red" property) are
        // filled in the accent; switches (the "led" property) stay outlined
        // and show their state with a small light.
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = juce::jmin (r.getHeight(), r.getWidth()) * 0.5f;
        const bool red = b.getProperties().getWithDefault ("red", false);
        const bool led = b.getProperties().getWithDefault ("led", false);
        const bool on = b.getToggleState();
        const auto lines = inkFor (b);
        if (red)
        {
            g.setColour (down ? rust.darker (0.2f) : hover ? rust.brighter (0.1f) : rust);
            g.fillRoundedRectangle (r, radius);
        }
        else if (on && ! led)
        {
            g.setColour (down ? lines.darker (0.15f) : lines);
            g.fillRoundedRectangle (r, radius);
        }
        else
        {
            if (hover || down)
            {
                g.setColour (lines.withAlpha (down ? 0.14f : 0.06f));
                g.fillRoundedRectangle (r, radius);
            }
            g.setColour (lines.withAlpha (hover ? 0.55f : 0.32f));
            g.drawRoundedRectangle (r, radius, 1.0f);
        }
        if (led)
        {
            const auto light = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getX() + radius + 2.0f, r.getCentreY() });
            if (on)
            {
                g.setColour (screenLime.withAlpha (0.3f));
                g.fillEllipse (light.expanded (2.5f));
            }
            g.setColour (on ? screenLime : inkSoft.withAlpha (0.6f));
            g.fillEllipse (light);
        }
    }

    // Value boxes: a dark capsule, a little sunk.
    static void drawSunkBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, juce::Colour)
    {
        const float radius = juce::jmin (r.getHeight(), r.getWidth()) * 0.5f;
        g.setColour (face);
        g.fillRoundedRectangle (r, radius);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.25f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + 4.0f, false));
        g.fillRoundedRectangle (r, radius);
    }

    // Menus: a hairline capsule with a small chevron.
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;
        if (box.isMouseOver (true))
        {
            g.setColour (ink.withAlpha (0.06f));
            g.fillRoundedRectangle (r, radius);
        }
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, radius, 1.0f);
        juce::Path chevron;
        const float cx = (float) width - radius - 2.0f, cy = (float) height * 0.5f;
        chevron.startNewSubPath (cx - 3.0f, cy - 1.5f);
        chevron.lineTo (cx, cy + 1.5f);
        chevron.lineTo (cx + 3.0f, cy - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (chevron, juce::PathStrokeType (1.2f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        const int pad = box.getHeight() / 2;
        label.setBounds (pad, 1, box.getWidth() - pad * 2 - 6, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
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
            // Values in the readout face, whatever the label was given.
            const auto font = monoFont (juce::jmin (12.0f, (float) label.getHeight() * 0.7f));
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (font);
            const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
            g.drawFittedText (label.getText(), area, label.getJustificationType(),
                              juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())), label.getMinimumHorizontalScale());
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return panelFont (juce::jmin (12.0f, (float) h * 0.5f), Weight::regular, 0.22f); }

    // Key text sits on the key's face (lower when pressed in), clear of a
    // switch's light, and shrinks to fit rather than being cut short.
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        juce::ignoreUnused (down);
        // An arrow key (the "arrow" property, -1 or 1): a small triangle.
        if (const int arrow = b.getProperties().getWithDefault ("arrow", 0); arrow != 0)
        {
            const auto c = b.getLocalBounds().toFloat().getCentre();
            juce::Path p;
            p.addTriangle (c.x + 3.0f * (float) arrow, c.y, c.x - 2.5f * (float) arrow, c.y - 4.0f, c.x - 2.5f * (float) arrow, c.y + 4.0f);
            g.setColour (inkFor (b));
            g.fillPath (p);
            return;
        }
        const bool led = b.getProperties().getWithDefault ("led", false);
        const bool red = b.getProperties().getWithDefault ("red", false);
        auto area = b.getLocalBounds().toFloat();
        if (led)
            area.removeFromLeft (area.getHeight() * 0.5f + 8.0f);
        area = area.reduced (area.getHeight() * 0.35f, 0.0f);

        // Light, widely spaced capitals; shrunk to fit rather than cut short.
        const auto text = b.getButtonText();
        float size = juce::jmin (12.0f, (float) b.getHeight() * 0.42f);
        const auto fontAt = [] (float s) { return panelFont (s, Weight::regular, 0.22f); };
        const float width = juce::GlyphArrangement::getStringWidth (fontAt (size), text);
        if (width > area.getWidth() && width > 0.0f)
            size = juce::jmax (7.5f, size * area.getWidth() / width);

        const bool filled = red || (b.getToggleState() && ! led);
        const bool header = onHeader (b);
        const auto colour = filled ? (header ? frame : chassis) : (header ? onFrame : b.findColour (juce::TextButton::textColourOffId));
        g.setColour (colour.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.setFont (fontAt (size));
        g.drawText (text, area, juce::Justification::centred, false);
    }
    // Switches drawn as JUCE toggles (MONITOR): a capsule with a light, like the keys.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;
        if (hover || down)
        {
            g.setColour (ink.withAlpha (down ? 0.14f : 0.06f));
            g.fillRoundedRectangle (r, radius);
        }
        g.setColour (hover ? ink.withAlpha (0.55f) : hairline());
        g.drawRoundedRectangle (r, radius, 1.0f);
        const bool on = b.getToggleState();
        const auto light = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getX() + radius + 2.0f, r.getCentreY() });
        if (on)
        {
            g.setColour (screenLime.withAlpha (0.3f));
            g.fillEllipse (light.expanded (2.5f));
        }
        g.setColour (on ? screenLime : inkSoft.withAlpha (0.6f));
        g.fillEllipse (light);
        auto area = r.withTrimmedLeft (radius + 8.0f).reduced (radius * 0.6f, 0.0f);
        float size = juce::jmin (12.0f, r.getHeight() * 0.42f);
        const auto fontAt = [] (float s) { return panelFont (s, Weight::regular, 0.22f); };
        const float width = juce::GlyphArrangement::getStringWidth (fontAt (size), b.getButtonText());
        if (width > area.getWidth() && width > 0.0f)
            size = juce::jmax (7.5f, size * area.getWidth() / width);
        g.setColour (ink.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.setFont (fontAt (size));
        g.drawText (b.getButtonText(), area, juce::Justification::centred, false);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override { return panelFont (juce::jmin (11.0f, (float) box.getHeight() * 0.48f), Weight::regular, 0.18f); }
    juce::Font getPopupMenuFont() override { return panelFont (13.0f, Weight::regular, 0.08f); }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (monoFont (11.5f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }
};

} // namespace silt::ui
