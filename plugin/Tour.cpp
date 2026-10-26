#include "Tour.h"
#include "Palette.h"

using namespace silt::ui;

namespace
{
constexpr int cardWidth = 380, pad = 18, buttonRow = 30;

std::unique_ptr<juce::PropertiesFile> settings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Silt";
    options.folderName = "Silt Audio";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}
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

bool Tour::seen() { return settings()->getBoolValue ("tourSeen", false); }

void Tour::markSeen()
{
    if (! remember)
        return;
    auto file = settings();
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
        setVisible (false);
        markSeen();
        if (onFinish)
            onFinish();
        return;
    }
    step = juce::jmax (0, index);
    if (steps[(size_t) step].page >= 0 && showPage)
        showPage (steps[(size_t) step].page);
    back.setEnabled (step > 0);
    next.setButtonText (step + 1 == (int) steps.size() ? "DONE" : "NEXT");
    skip.setVisible (step + 1 < (int) steps.size());
    resized();
    repaint();
    grabKeyboardFocus();
}

juce::Rectangle<int> Tour::spotArea() const
{
    if (step >= (int) steps.size() || steps[(size_t) step].spot == nullptr)
        return {};
    return steps[(size_t) step].spot().expanded (6);
}

juce::TextLayout Tour::bodyLayout (float width) const
{
    juce::AttributedString text;
    text.append (step < (int) steps.size() ? steps[(size_t) step].body : juce::String(), panelFont (13.5f).withStyle (juce::Font::plain), ink);
    text.setLineSpacing (3.0f);
    juce::TextLayout layout;
    layout.createLayout (text, width);
    return layout;
}

// Below the spot if it fits, else above, else beside; centred with no spot.
juce::Rectangle<int> Tour::cardArea() const
{
    const auto bounds = getLocalBounds().reduced (12);
    const int w = juce::jmin (cardWidth, bounds.getWidth());
    const int h = pad + 24 + juce::roundToInt (bodyLayout ((float) (w - 2 * pad)).getHeight()) + 14 + buttonRow + pad;
    const auto spot = spotArea();
    if (spot.isEmpty())
        return bounds.withSizeKeepingCentre (w, h);

    juce::Rectangle<int> card (w, h);
    const int x = juce::jlimit (bounds.getX(), bounds.getRight() - w, spot.getCentreX() - w / 2);
    if (bounds.getBottom() - spot.getBottom() >= h + 12)
        card.setPosition (x, spot.getBottom() + 12);
    else if (spot.getY() - bounds.getY() >= h + 12)
        card.setPosition (x, spot.getY() - 12 - h);
    else if (bounds.getRight() - spot.getRight() >= w + 12)
        card.setPosition (spot.getRight() + 12, juce::jlimit (bounds.getY(), bounds.getBottom() - h, spot.getCentreY() - h / 2));
    else if (spot.getX() - bounds.getX() >= w + 12)
        card.setPosition (spot.getX() - 12 - w, juce::jlimit (bounds.getY(), bounds.getBottom() - h, spot.getCentreY() - h / 2));
    else
        card = bounds.withSizeKeepingCentre (w, h);
    return card;
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
    // Dim everything but the spot.
    const auto spot = spotArea().toFloat();
    juce::Path shade;
    shade.addRectangle (getLocalBounds().toFloat());
    if (! spot.isEmpty())
        shade.addRoundedRectangle (spot, 8.0f);
    shade.setUsingNonZeroWinding (false);
    g.setColour (ink.withAlpha (0.72f));
    g.fillPath (shade);
    if (! spot.isEmpty())
    {
        g.setColour (lcdGreen);
        g.drawRoundedRectangle (spot, 8.0f, 2.5f);
    }

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
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        go (step + 1);
    else if (key == juce::KeyPress::leftKey)
        go (step - 1);
    else if (key == juce::KeyPress::escapeKey)
        go ((int) steps.size());
    else
        return false;
    return true;
}
