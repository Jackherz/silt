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

    // A guitar control knob, seen from above: a black skirt printed 0–10 that
    // turns with the value, a gold metal cap, and a fixed marker at the top.
    // The number under the marker is the value, as on a guitar.
    //
    // Each knob is worn in its own way, like well-used, badly kept gear: dust
    // settled round its base (more below), a tarnished cap with scratches and a
    // fingertip-polished edge, faded numbers and nicks in the skirt. The wear
    // comes from a seed per parameter, so it's the same every time.
    //
    // Drawing all that wear is costly, so it's drawn once per knob into two
    // images: a fixed one (shadow, dust, skirt, the cap's shading) and a turning
    // one (rim nicks and the marks on the cap), which is pasted rotated to the
    // knob's angle. Only the numbers (kept upright) and the marker are drawn live.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float, float, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto& cache = cacheFor (slider, w, h, scale);

        const auto centre = bounds.getCentre().translated (0.0f, 3.0f);
        const float skirt = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 8.0f;
        const float cap = skirt * 0.6f;
        const float turn = juce::degreesToRadians (pos * 10.0f * degreesPerNumber);

        g.drawImage (cache.fixed, bounds);
        g.drawImageTransformed (cache.turning, juce::AffineTransform::scale (1.0f / scale)
                                                   .translated (bounds.getX(), bounds.getY())
                                                   .rotated (turn, centre.x, centre.y));

        // Numbers 0–10 round the skirt, turning with the value: the current
        // value's number sits at 12 o'clock, under the marker.
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (7.0f, 10.0f, skirt * 0.24f), juce::Font::bold)));
        const float numberRadius = (skirt + cap) * 0.5f + 1.0f;
        for (int n = 0; n <= 10; ++n)
        {
            const float angle = juce::degreesToRadians (((float) n - 10.0f * pos) * -degreesPerNumber);
            g.setColour (juce::Colour (0xffe4d9bd).withAlpha (cache.numberAlpha[(size_t) n]));
            g.drawText (juce::String (n), juce::Rectangle<float> (16.0f, 10.0f).withCentre (centre.getPointOnCircumference (numberRadius, angle)),
                        juce::Justification::centred);
        }

        // The fixed marker above the knob.
        juce::Path marker;
        const float top = centre.y - skirt - 2.0f;
        marker.addTriangle (centre.x - 4.0f, top - 5.0f, centre.x + 4.0f, top - 5.0f, centre.x, top);
        g.setColour (rust);
        g.fillPath (marker);

        if (drawModulation != nullptr)
            drawModulation (g, slider, centre, skirt + 4.0f, pos);
    }

    // Draws a knob's modulation over it (the editor knows the routes).
    std::function<void (juce::Graphics&, juce::Slider&, juce::Point<float>, float, float)> drawModulation;

private:
    static constexpr float degreesPerNumber = 30.0f;   // 0–10 over 300°, like a guitar knob

    struct KnobCache
    {
        juce::Image fixed, turning;
        std::array<float, 11> numberAlpha {};
        int width = 0, height = 0;
        float scale = 0.0f;
    };
    std::map<juce::String, KnobCache> knobCaches;

    const KnobCache& cacheFor (juce::Slider& slider, int w, int h, float scale)
    {
        auto& cache = knobCaches[slider.getComponentID()];
        if (cache.width == w && cache.height == h && juce::approximatelyEqual (cache.scale, scale))
            return cache;

        cache.width = w;
        cache.height = h;
        cache.scale = scale;
        const int pw = juce::roundToInt ((float) w * scale), ph = juce::roundToInt ((float) h * scale);
        cache.fixed = juce::Image (juce::Image::ARGB, pw, ph, true);
        cache.turning = juce::Image (juce::Image::ARGB, pw, ph, true);

        juce::Random wear (slider.getComponentID().hashCode64());
        const auto spot = [&wear] (float lo, float hi) { return lo + wear.nextFloat() * (hi - lo); };
        const auto centre = juce::Point<float> ((float) w * 0.5f, (float) h * 0.5f + 3.0f);
        const float skirt = (float) juce::jmin (w, h) * 0.5f - 8.0f;
        const float cap = skirt * 0.6f;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        {
            juce::Graphics g (cache.fixed);
            g.addTransform (juce::AffineTransform::scale (scale));

            // Dust on the panel round the base: specks and a few fibres, heavier below.
            for (int i = 0; i < 80; ++i)
            {
                const float angle = spot (0.0f, twoPi);
                const float below = 0.5f + 0.5f * std::cos (angle);   // 1 at 6 o'clock
                if (wear.nextFloat() > 0.35f + 0.65f * below)
                    continue;
                const auto at = centre.getPointOnCircumference (skirt + spot (-0.5f, 9.0f), angle);
                const float size = spot (0.7f, 2.2f);
                g.setColour (juce::Colour (0xff7d7061).withAlpha (spot (0.25f, 0.6f)));
                g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (at));
            }
            for (int i = 0; i < 3; ++i)
            {
                const float angle = spot (2.0f, 4.3f);   // lower half
                const auto from = centre.getPointOnCircumference (skirt + spot (1.0f, 5.0f), angle);
                g.setColour (juce::Colour (0xff8a7d6c).withAlpha (0.35f));
                g.drawLine (juce::Line<float> (from, from.translated (spot (-5.0f, 5.0f), spot (-2.0f, 2.0f))), 0.6f);
            }

            // Shadow, skirt and its rim.
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre.translated (1.5f, 2.5f)));
            g.setColour (knobCap);
            g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre));
            g.setColour (knobSkirt);
            g.drawEllipse (juce::Rectangle<float> (skirt * 2.0f - 1.0f, skirt * 2.0f - 1.0f).withCentre (centre), 1.0f);

            // The gold cap: old brass, darker toward the rim, no shine. Its shading
            // is here, not in the turning layer, so the light doesn't turn with it.
            const auto capBounds = juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (centre);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9b178), centre.x - cap * 0.3f, centre.y - cap * 0.4f,
                                                     juce::Colour (0xff7a6236), capBounds.getRight(), capBounds.getBottom(), true));
            g.fillEllipse (capBounds);
            g.setColour (juce::Colour (0xff4f3f22));
            g.drawEllipse (capBounds, 1.2f);
        }

        // Some numbers are rubbed thin.
        for (auto& alpha : cache.numberAlpha)
            alpha = wear.nextFloat() < 0.3f ? spot (0.35f, 0.6f) : spot (0.75f, 0.95f);

        {
            juce::Graphics g (cache.turning);
            g.addTransform (juce::AffineTransform::scale (scale));

            // Nicks in the skirt's rim.
            for (int i = 0; i < 3; ++i)
            {
                const float angle = spot (0.0f, twoPi);
                juce::Path chip;
                chip.addCentredArc (centre.x, centre.y, skirt - 1.0f, skirt - 1.0f, 0.0f, angle, angle + spot (0.08f, 0.22f), true);
                g.setColour (juce::Colour (0xff6b6157).withAlpha (0.7f));
                g.strokePath (chip, juce::PathStrokeType (1.4f));
            }

            // The marks on the cap, which turn with it.
            const auto capBounds = juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (centre);

            // Tarnish: a few broad, faint patches that dull the brass unevenly.
            for (int i = 0; i < 3; ++i)
            {
                const auto at = centre.getPointOnCircumference (cap * spot (0.2f, 0.7f), spot (0.0f, twoPi));
                const float size = cap * spot (0.8f, 1.3f);
                for (float layer : { 1.0f, 0.65f })   // two layers soften the edge
                {
                    g.setColour (juce::Colour (0xff4a3a1e).withAlpha (spot (0.03f, 0.06f)));
                    g.fillEllipse (juce::Rectangle<float> (size * layer, size * layer * 0.8f).withCentre (at));
                }
            }

            // Scratches across the cap, and an edge fingertips have polished.
            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path capShape;
                capShape.addEllipse (capBounds);
                g.reduceClipRegion (capShape);
                for (int i = 0; i < 6; ++i)
                {
                    const float angle = spot (0.0f, twoPi);
                    const auto from = centre.getPointOnCircumference (cap * spot (0.0f, 0.9f), angle);
                    const auto to = from.getPointOnCircumference (cap * spot (0.3f, 1.1f), angle + spot (1.0f, 2.2f));
                    g.setColour ((i % 2 == 0 ? juce::Colour (0xfff0e2b4) : juce::Colour (0xff3e3018)).withAlpha (spot (0.12f, 0.3f)));
                    g.drawLine (juce::Line<float> (from, to), spot (0.4f, 0.8f));
                }
                juce::Path rub;
                const float rubAt = spot (-0.8f, 0.8f);
                rub.addCentredArc (centre.x, centre.y, cap - 1.5f, cap - 1.5f, 0.0f, rubAt, rubAt + spot (0.6f, 1.2f), true);
                g.setColour (juce::Colour (0xffe6d39a).withAlpha (0.35f));
                g.strokePath (rub, juce::PathStrokeType (1.5f));
            }
        }
        return cache;
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
    void drawSunkBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, juce::Colour edge)
    {
        g.setColour (edge);
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
