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
        setColour (juce::ComboBox::backgroundColourId, lcdGreen);
        setColour (juce::ComboBox::textColourId, lcdText);
        setColour (juce::ComboBox::outlineColourId, ink);
        setColour (juce::ComboBox::arrowColourId, lcdText);
        setColour (juce::PopupMenu::backgroundColourId, bone);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour (juce::PopupMenu::highlightedTextColourId, bone);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, ink);
        setColour (juce::ToggleButton::tickDisabledColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, bone);
    }

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
        // A key: a lip below, a face lit from above with a bright top edge.
        // Switches (the "led" property) stay up and show their state with a
        // light; other keys that are on look pressed in, dark.
        auto r = b.getLocalBounds().toFloat();
        const bool red = b.getProperties().getWithDefault ("red", false);
        const bool led = b.getProperties().getWithDefault ("led", false);
        const bool on = b.getToggleState();
        const bool pressedIn = (on && ! led && ! red) || down;
        const auto cap = red ? signalRed : (on && ! led ? ink : bone);
        const auto lip = red ? rustLip : (on && ! led ? knobCap : boneLip);

        if (! pressedIn)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 3.0f);   // a soft shadow under the lip
        }
        g.setColour (lip);
        g.fillRoundedRectangle (r.withTrimmedBottom (pressedIn ? 0.0f : 1.0f), 3.0f);

        const auto face = r.withTrimmedBottom (pressedIn ? 1.0f : 4.0f).withTrimmedTop (pressedIn ? 2.0f : 0.0f);
        const auto top = hover && ! pressedIn ? cap.brighter (0.16f) : cap.brighter (0.1f);
        g.setGradientFill (juce::ColourGradient (pressedIn ? cap.darker (0.2f) : top, 0.0f, face.getY(),
                                                 pressedIn ? cap.brighter (0.05f) : cap.darker (0.06f), 0.0f, face.getBottom(), false));
        g.fillRoundedRectangle (face, 3.0f);
        if (pressedIn)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, face.getY(), juce::Colours::transparentBlack, 0.0f, face.getY() + 4.0f, false));
            g.fillRoundedRectangle (face.withHeight (5.0f), 3.0f);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (red ? 0.3f : 0.6f));
            g.drawHorizontalLine ((int) face.getY() + 1, face.getX() + 3.0f, face.getRight() - 3.0f);
        }
        g.setColour (cap.darker (0.5f).withAlpha (0.5f));
        g.drawRoundedRectangle (face.reduced (0.5f), 3.0f, 1.0f);

        if (led)
        {
            const auto light = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ face.getX() + 10.0f, face.getCentreY() });
            if (on)
            {
                g.setColour (screenLime.withAlpha (0.35f));
                g.fillEllipse (light.expanded (3.0f));
            }
            g.setColour (on ? screenLime : boneLip.darker (0.3f));
            g.fillEllipse (light);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (light, 0.8f);
        }
    }

    // Value boxes and menus: sunk displays, shaded along the top edge.
    void drawSunkBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, juce::Colour)
    {
        g.setColour (face.darker (0.8f));   // its own edge, a shade of the window: no bright outline on a dark theme
        g.fillRect (r);
        const auto inner = r.reduced (1.0f);
        g.setGradientFill (juce::ColourGradient (face.darker (0.12f), 0.0f, inner.getY(), face.brighter (0.04f), 0.0f, inner.getBottom(), false));
        g.fillRect (inner);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, inner.getY(), juce::Colours::transparentBlack, 0.0f, inner.getY() + 4.0f, false));
        g.fillRect (inner.withHeight (4.0f));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        drawSunkBox (g, juce::Rectangle<float> ((float) width, (float) height), box.findColour (juce::ComboBox::backgroundColourId),
                     box.findColour (juce::ComboBox::outlineColourId));
        juce::Path chevron;
        const float cx = (float) width - 13.0f, cy = (float) height * 0.5f;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (chevron, juce::PathStrokeType (1.6f));
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
            const auto font = getLabelFont (label);
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (font);
            const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
            g.drawFittedText (label.getText(), area, label.getJustificationType(),
                              juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())), label.getMinimumHorizontalScale());
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return panelFont (juce::jmin (12.0f, (float) h * 0.55f)); }

    // Key text sits on the key's face (lower when pressed in), clear of a
    // switch's light, and shrinks to fit rather than being cut short.
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const bool led = b.getProperties().getWithDefault ("led", false);
        const bool red = b.getProperties().getWithDefault ("red", false);
        const bool pressedIn = (b.getToggleState() && ! led && ! red) || down;
        auto area = b.getLocalBounds().toFloat().withTrimmedBottom (pressedIn ? 1.0f : 4.0f).withTrimmedTop (pressedIn ? 2.0f : 0.0f);
        if (led)
            area.removeFromLeft (16.0f);
        area = area.reduced (4.0f, 0.0f);

        const auto text = b.getButtonText();
        float size = juce::jmin (12.0f, (float) b.getHeight() * 0.55f);
        const float width = juce::GlyphArrangement::getStringWidth (panelFont (size), text);
        if (width > area.getWidth() && width > 0.0f)
            size = juce::jmax (8.0f, size * area.getWidth() / width);

        const auto colour = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        g.setColour (colour.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.setFont (panelFont (size));
        g.drawText (text, area, juce::Justification::centred, false);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return monoFont (12.0f); }
    juce::Font getPopupMenuFont() override { return monoFont (13.0f); }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (monoFont (12.0f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }
};

} // namespace silt::ui
