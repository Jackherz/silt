#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <KitFonts.h>

// The sheet every plug-in in the range is drawn on: one ink, two typefaces, hard
// offset shadows, square corners. A plug-in differs by its paper stock and nothing
// else, so the same control looks and behaves the same wherever you meet it.
namespace kit
{
struct Stock
{
    juce::Colour page;      // behind everything
    juce::Colour panel;     // a sheet on the page
    juce::Colour head;      // a panel's heading band
    juce::Colour ink;       // text, frames, controls
    juce::Colour soft;      // quiet text
    juce::Colour rule;      // hairlines and grouping
    juce::Colour sunk;      // the readout and value windows
    juce::Colour onSunk;
    juce::Colour shadow;    // the hard offset
    juce::Colour accent;    // the one colour a stock may have; ink if it has none
    juce::Colour onAccent;
    juce::Colour lit;       // what "on" looks like, on every switch and light; shadow reads on it
};

inline Stock cream()
{
    const juce::Colour ink (0xff181815), paper (0xfffaf9f3);
    return { juce::Colour (0xfff4f3ec), paper, juce::Colour (0xfff1f0e9), ink, juce::Colour (0xff5f5f57),
             juce::Colour (0xffb9b8ad), ink, paper, ink, ink, paper, juce::Colour (0xfff2cf7e) };
}

inline Stock graphite()
{
    const juce::Colour ink (0xffe9e2d0), dark (0xff151716);
    return { juce::Colour (0xff2e332f), juce::Colour (0xff262a27), juce::Colour (0xff1d201e), ink, juce::Colour (0xffb8b19f),
             juce::Colour (0xff4c524d), dark, ink, juce::Colour (0xff101211), juce::Colour (0xffe86a2c), dark, juce::Colour (0xffe86a2c) };
}

// VT323 for everything; Press Start 2P for the wordmark alone.
inline juce::Font label (float size)
{
    static auto face = juce::Typeface::createSystemTypefaceFor (KitFonts::VT323Regular_ttf, (size_t) KitFonts::VT323Regular_ttfSize);
    return juce::Font (juce::FontOptions (face).withHeight (size));
}

inline juce::Font wordmark (float size)
{
    static auto face = juce::Typeface::createSystemTypefaceFor (KitFonts::PressStart2PRegular_ttf, (size_t) KitFonts::PressStart2PRegular_ttfSize);
    return juce::Font (juce::FontOptions (face).withHeight (size));
}

inline int textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text) + 0.5f);
}

// NAME ........ FIGURE: ties a label to a value when there's no room for a rule.
inline void leader (juce::Graphics& g, int x1, int x2, int y, juce::Colour c)
{
    g.setColour (c);
    for (int x = x1; x < x2 - 2; x += 4)
        g.fillRect (x, y, 1, 1);
}

// A sheet: a hard offset, a fill and a frame.
inline void drawSheet (juce::Graphics& g, juce::Rectangle<int> r, const Stock& s, int offset = 3)
{
    g.setColour (s.shadow);
    g.fillRect (r.translated (offset, offset));
    g.setColour (s.panel);
    g.fillRect (r);
    g.setColour (s.ink);
    g.drawRect (r, 1);
}

constexpr int panelHeadHeight = 26;

// A numbered panel: its number in a solid block, the name, a dotted leader and a
// figure at the end. Every panel opens the same way, so a column reads as one document.
inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const Stock& s, const juce::String& index,
                       const juce::String& name, const juce::String& tail)
{
    drawSheet (g, r, s);
    auto head = r.removeFromTop (panelHeadHeight).reduced (1, 1);
    g.setColour (s.head);
    g.fillRect (head);
    g.setColour (s.ink);
    g.drawHorizontalLine (head.getBottom(), (float) r.getX(), (float) r.getRight());
    auto row = head.reduced (8, 0);
    const auto font = label (16.0f);
    g.setFont (font);
    const auto block = row.removeFromLeft (22).withSizeKeepingCentre (22, 16);
    g.fillRect (block);
    g.setColour (s.panel);
    g.drawText (index, block, juce::Justification::centred);
    row.removeFromLeft (6);
    g.setColour (s.ink);
    g.drawText (name, row.removeFromLeft (textWidth (font, name) + 2), juce::Justification::centredLeft);
    g.setColour (s.soft);
    const int tailWidth = textWidth (font, tail) + 2;
    g.drawText (tail, row.removeFromRight (tailWidth), juce::Justification::centredRight);
    leader (g, row.getX() + 4, row.getRight() - 4, head.getCentreY(), s.rule);
}

// The masthead: catalogue chip, wordmark, two lines saying what it is, and the rule
// under it all. Top left, always.
inline void drawMasthead (juce::Graphics& g, juce::Rectangle<int> area, const Stock& s, const juce::String& chip,
                          juce::Colour chipFill, juce::Colour chipText, const juce::String& word,
                          const juce::String& line1, const juce::String& line2)
{
    g.setColour (s.ink);
    g.fillRect (area.getX(), area.getBottom() - 2, area.getWidth(), 2);
    auto row = area.withTrimmedBottom (10);

    const auto chipFont = label (28.0f);
    const auto box = row.removeFromLeft (textWidth (chipFont, chip) + 18).withSizeKeepingCentre (textWidth (chipFont, chip) + 18, 36);
    g.setColour (chipFill);
    g.fillRect (box);
    g.setColour (chipText);
    g.setFont (chipFont);
    g.drawText (chip, box, juce::Justification::centred);
    row.removeFromLeft (16);

    const auto wordFont = wordmark (30.0f);
    g.setColour (s.ink);
    g.setFont (wordFont);
    const int wordWidth = textWidth (wordFont, word) + 4;
    g.drawText (word, row.removeFromLeft (wordWidth), juce::Justification::centredLeft);
    row.removeFromLeft (14);

    g.setColour (s.soft);
    g.setFont (label (17.0f));
    auto lines = row.withSizeKeepingCentre (row.getWidth(), 34);
    g.drawText (line1, lines.removeFromTop (17), juce::Justification::centredLeft);
    g.drawText (line2, lines, juce::Justification::centredLeft);
}

// The readout: top right, always. A sunk window with a title line and a status chip.
inline void drawReadout (juce::Graphics& g, juce::Rectangle<int> r, const Stock& s, const juce::String& title,
                         const juce::String& chip, const juce::String& value, const juce::String& meta, bool longText)
{
    g.setColour (s.shadow);
    g.fillRect (r.translated (3, 3));
    g.setColour (s.sunk);
    g.fillRect (r);
    g.setColour (s.ink);
    g.drawRect (r, 1);
    auto inner = r.reduced (10, 6);
    auto top = inner.removeFromTop (16);
    const auto small = label (15.0f);
    g.setFont (small);
    const auto chipBox = top.removeFromRight (textWidth (small, chip) + 10);
    g.setColour (s.onSunk);
    g.fillRect (chipBox);
    g.setColour (s.sunk);
    g.drawText (chip, chipBox, juce::Justification::centred);
    g.setColour (s.onSunk.withAlpha (0.75f));
    g.drawFittedText (title, top.withTrimmedRight (8), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (s.onSunk);
    if (longText)
    {
        g.setFont (label (17.0f));
        g.drawFittedText (value, inner, juce::Justification::topLeft, 3, 0.85f);
        return;
    }
    if (meta.isNotEmpty())
    {
        g.setColour (s.onSunk.withAlpha (0.75f));
        g.setFont (small);
        g.drawText (meta, inner.removeFromBottom (15), juce::Justification::centredLeft);
        g.setColour (s.onSunk);
    }
    g.setFont (label (28.0f));
    g.drawFittedText (value, inner, juce::Justification::centredLeft, 1, 0.6f);
}

// Knobs, keys, fields, switches and sliders, the same on every stock. The stock is
// read when drawing, so a plug-in can change its paper while open.
class Look : public juce::LookAndFeel_V4
{
public:
    explicit Look (const Stock& stockToUse) : s (stockToUse) { applyColours(); }

    void applyColours()
    {
        setColour (juce::ComboBox::backgroundColourId, s.panel);
        setColour (juce::ComboBox::outlineColourId, s.ink);
        setColour (juce::ComboBox::textColourId, s.ink);
        setColour (juce::ComboBox::arrowColourId, s.ink);
        setColour (juce::PopupMenu::backgroundColourId, s.panel);
        setColour (juce::PopupMenu::textColourId, s.ink);
        setColour (juce::PopupMenu::headerTextColourId, s.soft);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, s.ink);
        setColour (juce::PopupMenu::highlightedTextColourId, s.panel);
        setColour (juce::TextButton::buttonColourId, s.panel);
        setColour (juce::TextButton::textColourOffId, s.ink);
        setColour (juce::TextButton::textColourOnId, s.panel);
        setColour (juce::ToggleButton::textColourId, s.ink);
        setColour (juce::ToggleButton::tickColourId, s.ink);
        setColour (juce::Slider::trackColourId, s.ink);
        setColour (juce::Slider::backgroundColourId, s.rule);
        setColour (juce::Slider::thumbColourId, s.ink);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::AlertWindow::backgroundColourId, s.panel);
        setColour (juce::AlertWindow::textColourId, s.ink);
        setColour (juce::AlertWindow::outlineColourId, s.ink);
        setColour (juce::TextEditor::backgroundColourId, s.sunk);
        setColour (juce::TextEditor::textColourId, s.onSunk);
        setColour (juce::TextEditor::outlineColourId, s.ink);
        setColour (juce::TextEditor::focusedOutlineColourId, s.ink);
        setColour (juce::TextEditor::highlightColourId, s.onSunk.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, s.onSunk);
    }

    // The face of a knob: pale by default. The "dark" property on a slider asks for
    // the black disc that marks a primary control.
    juce::Colour knobFace (const juce::Slider& slider) const
    {
        return slider.getProperties().getWithDefault ("dark", false) ? s.shadow.interpolatedWith (s.ink, 0.02f) : paleFace();
    }
    juce::Colour knobPointer (const juce::Slider& slider) const
    {
        return slider.getProperties().getWithDefault ("dark", false) ? paleFace().brighter (0.2f) : juce::Colour (0xff181815);
    }

    // Where the marks end: what's drawn outside the knob starts from here.
    static float knobOuterRadius (juce::Rectangle<float> box)
    {
        const float u = juce::jmin (box.getWidth(), box.getHeight()) / 104.0f;
        return juce::jmin (box.getWidth(), box.getHeight()) * 0.5f - 15.0f * u + 11.0f * u;
    }

    // A flat disc with a thin pointer, five marks for a scale and a hard shadow. No
    // ring: on hover the marks turn to ink, which is the knob saying "this one".
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& slider) override
    {
        const auto box = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float u = juce::jmin (box.getWidth(), box.getHeight()) / 104.0f;
        const auto area = box.withSizeKeepingCentre (juce::jmin (box.getWidth(), box.getHeight()), juce::jmin (box.getWidth(), box.getHeight())).reduced (15.0f * u);
        const float r = area.getWidth() * 0.5f;
        const auto c = area.getCentre();
        const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
        g.setColour (slider.isMouseOverOrDragging() ? s.ink : s.soft.withAlpha (0.8f));
        for (int i = 0; i <= 4; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 4.0f;
            g.drawLine (c.x + (r + 6.0f * u) * std::sin (a), c.y - (r + 6.0f * u) * std::cos (a),
                        c.x + (r + 11.0f * u) * std::sin (a), c.y - (r + 11.0f * u) * std::cos (a), 1.3f * u);
        }
        g.setColour (s.shadow);
        g.fillEllipse (area.translated (2.6f * u, 3.0f * u));
        g.setColour (knobFace (slider));
        g.fillEllipse (area);
        g.setColour (s.shadow);
        g.drawEllipse (area, 1.2f * u);
        juce::Path pointer;
        pointer.addRectangle (-1.4f * u, -r * 0.83f, 2.8f * u, r * 0.83f);
        g.setColour (knobPointer (slider));
        g.fillPath (pointer, juce::AffineTransform::rotation (a0 + pos * (a1 - a0)).translated (c));
    }

    // A key: frame, hard shadow, and a solid block when it's on. The "accent" property
    // fills it in the stock's accent; "led" keeps it outlined with a small square that
    // says whether it's on.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        // The "flat" property: one cell of a row of keys inside a frame of its own.
        if (b.getProperties().getWithDefault ("flat", false))
        {
            g.setColour (b.getToggleState() ? s.ink : down ? s.ink.withAlpha (0.25f) : hover ? s.ink.withAlpha (0.1f) : juce::Colours::transparentBlack);
            g.fillRect (b.getLocalBounds());
            g.setColour (s.ink);
            g.drawVerticalLine (b.getWidth() - 1, 0.0f, (float) b.getHeight());
            return;
        }
        const auto r = b.getLocalBounds().toFloat().withTrimmedRight (2.0f).withTrimmedBottom (2.0f).reduced (0.5f);
        const bool enabled = b.isEnabled();
        const bool accent = b.getProperties().getWithDefault ("accent", false);
        const bool led = b.getProperties().getWithDefault ("led", false);
        const bool on = b.getToggleState() && ! led;
        const auto face = accent ? s.accent : on ? s.ink : s.panel;
        if (enabled)
        {
            g.setColour (s.shadow);
            g.fillRect (r.translated (2.0f, 2.0f));
        }
        g.setColour (down && enabled ? face.interpolatedWith (s.ink, 0.25f) : hover && enabled ? face.interpolatedWith (s.ink, 0.08f) : face);
        g.fillRect (r);
        g.setColour (enabled ? (accent ? s.accent : s.ink) : s.rule);
        g.drawRect (r, 1.5f);
        if (led)
        {
            const auto light = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
            g.setColour (s.ink);
            g.drawRect (light, 1.0f);
            if (b.getToggleState())
            {
                g.setColour (s.lit);
                g.fillRect (light.reduced (1.0f));
                g.setColour (s.ink);
                g.drawRect (light, 1.0f);
            }
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return label (juce::jlimit (13.0f, 18.0f, (float) h * 0.62f)); }

    // An "arrow" property (-1 or 1) draws a small triangle instead of text. Text
    // shrinks to fit rather than being cut short.
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool accent = b.getProperties().getWithDefault ("accent", false);
        const bool led = b.getProperties().getWithDefault ("led", false);
        const bool on = b.getToggleState() && ! led;
        const auto colour = ! b.isEnabled() ? s.rule : accent ? s.onAccent : on ? s.panel : s.ink;
        auto area = b.getProperties().getWithDefault ("flat", false) ? b.getLocalBounds() : b.getLocalBounds().withTrimmedRight (2).withTrimmedBottom (2);
        g.setColour (colour);
        if (const int arrow = b.getProperties().getWithDefault ("arrow", 0); arrow != 0)
        {
            const auto c = area.toFloat().getCentre();
            juce::Path p;
            p.addTriangle (c.x + 3.5f * (float) arrow, c.y, c.x - 3.0f * (float) arrow, c.y - 5.0f, c.x - 3.0f * (float) arrow, c.y + 5.0f);
            g.fillPath (p);
            return;
        }
        if (led)
            area.removeFromLeft (18);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), area.reduced (3, 0), juce::Justification::centred, 1, 0.7f);
    }

    // A field: frame, hard shadow, and the chevron in its own solid cell.
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> ((float) width, (float) height).withTrimmedRight (2.0f).withTrimmedBottom (2.0f).reduced (0.5f);
        g.setColour (s.shadow);
        g.fillRect (r.translated (2.0f, 2.0f));
        g.setColour (box.isMouseOver (true) ? s.panel.interpolatedWith (s.ink, 0.08f) : s.panel);
        g.fillRect (r);
        g.setColour (s.ink);
        g.drawRect (r, 1.5f);
        auto cell = r.removeFromRight (juce::jmin (r.getWidth(), r.getHeight()));
        g.fillRect (cell);
        g.setColour (s.panel);
        juce::Path chevron;
        const float cx = cell.getCentreX(), cy = cell.getCentreY();
        chevron.addTriangle (cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 3.5f);
        g.fillPath (chevron);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& text) override
    {
        text.setBounds (4, 0, box.getWidth() - box.getHeight() - 4, box.getHeight() - 2);
        text.setFont (getComboBoxFont (box));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override { return label (juce::jlimit (13.0f, 18.0f, (float) box.getHeight() * 0.62f)); }
    juce::Font getPopupMenuFont() override { return label (18.0f); }
    juce::Font getAlertWindowTitleFont() override { return label (20.0f); }
    juce::Font getAlertWindowMessageFont() override { return label (17.0f); }
    juce::Font getAlertWindowFont() override { return label (17.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

    // A switch: a sliding block and its name, in ink while it's on.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        const auto bounds = b.getLocalBounds().toFloat();
        // The "plate" property: a small plate that belongs to the control above it,
        // solid while it's on.
        if (b.getProperties().getWithDefault ("plate", false))
        {
            const auto r = bounds.reduced (0.5f);
            g.setColour (b.getToggleState() ? s.lit : hover ? s.panel.interpolatedWith (s.ink, 0.08f) : s.panel);
            g.fillRect (r);
            g.setColour (s.ink);
            g.drawRect (r, 1.0f);
            g.setColour (b.getToggleState() ? s.shadow : s.ink);
            g.setFont (label (15.0f));
            g.drawText (b.getButtonText().toUpperCase(), bounds, juce::Justification::centred);
            return;
        }
        // The "compact" property: a shorter track, for a row that has to fit five.
        const bool compact = b.getProperties().getWithDefault ("compact", false);
        const auto track = juce::Rectangle<float> (compact ? 16.0f : 24.0f, 14.0f).withCentre ({ compact ? 9.0f : 13.0f, bounds.getCentreY() });
        // Off is a dark track with a pale block on the left. On, the track lights and the
        // block goes dark on the right, so the state reads from across the room.
        const bool on = b.getToggleState();
        g.setColour (on ? s.lit : s.ink);
        g.fillRect (track);
        g.setColour (s.ink);
        g.drawRect (track, 1.0f);
        const auto handle = juce::Rectangle<float> (compact ? 6.0f : 9.0f, 10.0f).withCentre ({ on ? track.getRight() - (compact ? 4.5f : 6.0f) : track.getX() + (compact ? 4.5f : 6.0f), track.getCentreY() });
        g.setColour (on ? s.shadow : s.panel);
        g.fillRect (handle);
        if (b.hasKeyboardFocus (true) || hover)
        {
            g.setColour (s.soft);
            g.drawRect (track.expanded (2.0f), 1.0f);
        }
        const auto text = b.getButtonText().toUpperCase();
        if (text.isEmpty() || bounds.getWidth() < 30.0f)
            return;
        g.setColour (b.getToggleState() ? s.ink : s.soft);
        g.setFont (label (compact ? 15.0f : 16.0f));
        g.drawFittedText (text, bounds.withTrimmedLeft (compact ? 21.0f : 30.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    // A slider is a printed scale: one rule, five marks under it and a boxed tab.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, slider);
            return;
        }
        const float cy = (float) y + (float) h * 0.5f, left = (float) x + 1.0f, right = (float) (x + w) - 1.0f;
        g.setColour (s.rule);
        for (int i = 0; i <= 4; ++i)
            g.fillRect (left + (right - left) * (float) i / 4.0f - 0.5f, cy + 6.0f, 1.0f, 4.0f);
        g.setColour (s.ink);
        g.fillRect (left, cy - 1.0f, right - left, 2.0f);
        const auto tab = juce::Rectangle<float> (9.0f, 15.0f).withCentre ({ juce::jlimit (left, right, pos), cy - 1.0f });
        g.setColour (s.shadow);
        g.fillRect (tab.translated (2.0f, 2.0f));
        g.setColour (slider.isMouseOverOrDragging() ? s.panel.interpolatedWith (s.ink, 0.08f) : s.panel);
        g.fillRect (tab);
        g.setColour (s.ink);
        g.drawRect (tab, 1.0f);
        g.fillRect (tab.withSizeKeepingCentre (3.0f, 7.0f));
    }

protected:
    const Stock& s;

private:
    juce::Colour paleFace() const { return s.page.getPerceivedBrightness() < 0.5f ? juce::Colour (0xffefe8d6) : juce::Colour (0xffe8e5db); }
};
} // namespace kit
