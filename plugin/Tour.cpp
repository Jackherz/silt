#include "Tour.h"
#include "Palette.h"
#include "Settings.h"

using namespace silt::ui;

namespace
{
constexpr int cardWidth = 380, pad = 18, buttonRow = 30;
} // namespace

Tour::Tour()
{
    setWantsKeyboardFocus (true);
    back.onClick = [this] { go (step - 1); };
    next.onClick = [this] { go (step + 1); };
    skip.onClick = [this] { go ((int) steps.size()); };
    for (auto* b : { &back, &next, &skip })
        addAndMakeVisible (b);
}

bool Tour::seen() { return settingsFile()->getBoolValue ("tourSeen", false); }

void Tour::markSeen()
{
    if (! remember)
        return;
    auto file = settingsFile();
    file->setValue ("tourSeen", true);
    file->saveIfNeeded();
}

void Tour::start (std::vector<Step> s)
{
    steps = std::move (s);
    if (steps.empty())
        return;
    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalBounds());
    setVisible (true);
    toFront (true);
    go (0);
}

void Tour::go (int index)
{
    if (index >= (int) steps.size())
    {
        stopTimer();
        setVisible (false);
        markSeen();
        if (onFinish)
            onFinish();
        return;
    }
    step = juce::jmax (0, index);
    if (steps[(size_t) step].page >= 0 && showPage)
        showPage (steps[(size_t) step].page);
    doneShown = false;
    back.setEnabled (step > 0);
    next.setButtonText (step + 1 == (int) steps.size() ? "DONE" : "NEXT");
    skip.setVisible (step + 1 < (int) steps.size());
    resized();
    repaint();
    grabKeyboardFocus();
    startTimerHz (10);
}

// Ticks the card once its TRY IT has been done.
void Tour::timerCallback()
{
    if (step >= (int) steps.size() || doneShown)
        return;
    if (const auto& done = steps[(size_t) step].done; done != nullptr && done())
    {
        doneShown = true;
        repaint();
    }
}

juce::RectangleList<int> Tour::spotAreas() const
{
    juce::RectangleList<int> out;
    if (step < (int) steps.size() && steps[(size_t) step].spots != nullptr)
        for (const auto& r : steps[(size_t) step].spots())
            out.add (r.expanded (6));
    return out;
}

bool Tour::hitTest (int x, int y)
{
    return ! spotAreas().containsPoint ({ x, y });
}

juce::TextLayout Tour::bodyLayout (float width) const
{
    juce::AttributedString text;
    text.setLineSpacing (3.0f);
    if (step < (int) steps.size())
    {
        const auto& s = steps[(size_t) step];
        text.append (s.body, panelFont (13.5f).withStyle (juce::Font::plain), ink);
        if (s.tryIt.isNotEmpty())
        {
            text.append ("\n\n", panelFont (8.0f), ink);
            if (doneShown)
                text.append (juce::String::charToString ((juce::juce_wchar) 0x2713) + "  NICE. PRESS NEXT WHEN YOU'RE READY.", panelFont (13.0f), juce::Colour (0xff4f7a2c));
            else
            {
                text.append ("TRY IT   ", panelFont (13.0f), rust);
                text.append (s.tryIt, panelFont (13.5f).withStyle (juce::Font::plain), ink);
            }
        }
    }
    juce::TextLayout layout;
    layout.createLayout (text, width);
    return layout;
}

// Somewhere it covers none of the lit parts: below, above or beside one of
// them, else in a corner, else centred.
juce::Rectangle<int> Tour::cardArea() const
{
    const auto bounds = getLocalBounds().reduced (12);
    const int w = juce::jmin (cardWidth, bounds.getWidth());
    const int h = pad + 24 + juce::roundToInt (bodyLayout ((float) (w - 2 * pad)).getHeight()) + 14 + buttonRow + pad;
    const auto spots = spotAreas();
    if (spots.isEmpty())
        return bounds.withSizeKeepingCentre (w, h);

    std::vector<juce::Rectangle<int>> candidates;
    for (const auto& r : spots)
    {
        const int x = r.getCentreX() - w / 2, y = r.getCentreY() - h / 2;
        candidates.push_back ({ x, r.getBottom() + 12, w, h });
        candidates.push_back ({ x, r.getY() - 12 - h, w, h });
        candidates.push_back ({ r.getRight() + 12, y, w, h });
        candidates.push_back ({ r.getX() - 12 - w, y, w, h });
    }
    for (const auto corner : { bounds.getBottomRight(), bounds.getBottomLeft(), bounds.getTopRight(), bounds.getTopLeft() })
        candidates.push_back (juce::Rectangle<int> (w, h).withCentre (corner));
    for (auto card : candidates)
    {
        card = card.constrainedWithin (bounds);
        if (! spots.intersectsRectangle (card))
            return card;
    }
    return bounds.withSizeKeepingCentre (w, h);
}

void Tour::resized()
{
    auto buttons = cardArea().reduced (pad).removeFromBottom (buttonRow - 4);
    next.setBounds (buttons.removeFromRight (78));
    buttons.removeFromRight (6);
    back.setBounds (buttons.removeFromRight (70));
    skip.setBounds (buttons.removeFromLeft (100));
}

void Tour::paint (juce::Graphics& g)
{
    // Dim everything but the lit parts.
    const auto spots = spotAreas();
    juce::Path shade;
    shade.addRectangle (getLocalBounds().toFloat());
    for (const auto& r : spots)
        shade.addRoundedRectangle (r.toFloat(), 8.0f);
    shade.setUsingNonZeroWinding (false);
    g.setColour (frame.withAlpha (0.72f));
    g.fillPath (shade);
    g.setColour (lcdGreen);
    for (const auto& r : spots)
        g.drawRoundedRectangle (r.toFloat(), 8.0f, 2.5f);

    const auto card = cardArea().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (card.translated (3.0f, 4.0f), 6.0f);
    g.setColour (bone);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (ink);
    g.drawRoundedRectangle (card, 6.0f, 2.0f);

    auto inner = card.reduced ((float) pad);
    auto titleRow = inner.removeFromTop (24.0f);
    g.setFont (monoFont (11.0f));
    g.setColour (inkSoft);
    g.drawText (juce::String (step + 1) + " / " + juce::String ((int) steps.size()), titleRow.removeFromRight (60.0f), juce::Justification::centredRight);
    g.setColour (rust);
    g.setFont (panelFont (17.0f));
    g.drawText (step < (int) steps.size() ? steps[(size_t) step].title.toUpperCase() : juce::String(), titleRow, juce::Justification::centredLeft);
    bodyLayout (inner.getWidth()).draw (g, inner.withTrimmedBottom ((float) buttonRow).withTrimmedTop (6.0f));
}

bool Tour::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey)
        go (step + 1);
    else if (key == juce::KeyPress::leftKey)
        go (step - 1);
    else if (key == juce::KeyPress::escapeKey)
        go ((int) steps.size());
    else
        return false;
    return true;
}
