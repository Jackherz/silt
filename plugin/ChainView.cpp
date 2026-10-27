#include "ChainView.h"
#include "Palette.h"

using namespace silt::ui;

namespace
{
constexpr float endWidth = 62.0f, gap = 20.0f;

float value (const SiltProcessor& silt, const char* id) { return silt.params.getRawParameterValue (id)->load(); }

void arrow (juce::Graphics& g, float x0, float x1, float y)
{
    g.drawLine (x0 + 3.0f, y, x1 - 4.0f, y, 1.2f);
    juce::Path head;
    head.addTriangle (x1 - 3.0f, y, x1 - 8.0f, y - 3.5f, x1 - 8.0f, y + 3.5f);
    g.fillPath (head);
}
} // namespace

ChainView::ChainView (SiltProcessor& p) : silt (p)
{
    shownOrder = currentOrder();
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

std::array<int, SiltProcessor::numModules> ChainView::currentOrder() const
{
    return SiltProcessor::chainOrder (juce::roundToInt (value (silt, "chainOrder")));
}

bool ChainView::moduleOn (const SiltProcessor& silt, int module)
{
    return silt.params.getRawParameterValue (SiltProcessor::moduleOnId (module))->load() > 0.5f;
}

bool ChainView::moduleActive (const SiltProcessor& silt, int module)
{
    if (! moduleOn (silt, module))
        return false;
    switch (module)
    {
        case SiltProcessor::moduleResonator: return value (silt, "resMix") > 0.0f;
        case SiltProcessor::moduleFilter:    return value (silt, "filterMode") > 0.5f;
        case SiltProcessor::moduleColor:
            return value (silt, "colorMix") > 0.0f
                && (value (silt, "colorDrive") > 0.0f || value (silt, "colorBits") < 16.0f || value (silt, "colorRate") < silt::Color::rateOff
                    || value (silt, "colorComp") > 0.0f || value (silt, "colorNoise") > 0.0f || value (silt, "colorWow") > 0.0f
                    || std::abs (value (silt, "colorTone")) > 0.0f);
        default:
            return value (silt, "spaceDelay") > 0.0f || value (silt, "spaceReverb") > 0.0f || value (silt, "spaceFreeze") > 0.5f;
    }
}

void ChainView::update()
{
    std::array<bool, SiltProcessor::numModules> active {}, on {};
    for (int m = 0; m < SiltProcessor::numModules; ++m)
    {
        active[(size_t) m] = moduleActive (silt, m);
        on[(size_t) m] = moduleOn (silt, m);
    }
    const auto order = currentOrder();
    if (dragging < 0 && (order != shownOrder || active != shownActive || on != shownOn))
    {
        shownOrder = order;
        shownActive = active;
        shownOn = on;
        repaint();
    }
}

juce::Rectangle<float> ChainView::blockArea (int position) const
{
    auto row = getLocalBounds().toFloat().reduced (8.0f, 4.0f);
    row.removeFromTop (16.0f);
    row.removeFromLeft (endWidth + gap);
    row.removeFromRight (endWidth + gap);
    const float w = (row.getWidth() - gap * (SiltProcessor::numModules - 1)) / SiltProcessor::numModules;
    return { row.getX() + (float) position * (w + gap), row.getY() + 4.0f, w, row.getHeight() - 8.0f };
}

juce::Rectangle<float> ChainView::switchArea (int position) const
{
    const auto r = blockArea (position);
    return r.withTrimmedTop (r.getHeight() * 0.55f).withSizeKeepingCentre (juce::jmin (r.getWidth() - 12.0f, 54.0f), 16.0f);
}

int ChainView::positionAt (float x) const
{
    int best = 0;
    for (int p = 1; p < SiltProcessor::numModules; ++p)
        if (std::abs (blockArea (p).getCentreX() - x) < std::abs (blockArea (best).getCentreX() - x))
            best = p;
    return best;
}

void ChainView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (screenBlue);
    g.fillRect (bounds);
    g.setColour (screenLight.withAlpha (0.6f));
    g.drawRect (bounds, 1.0f);

    g.setFont (monoFont (10.0f));
    g.setColour (screenLime);
    g.drawText ("CHAIN", bounds.reduced (8.0f, 4.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
    g.setColour (screenPale.withAlpha (0.8f));
    g.drawText ("DRAG TO REORDER   CLICK TO OPEN   SWITCH: ON/OFF", bounds.reduced (8.0f, 4.0f).removeFromTop (12.0f), juce::Justification::centredRight);

    // The order as it would be with the dragged block dropped where it is now.
    auto order = shownOrder;
    if (dragging >= 0)
    {
        const int module = order[(size_t) dragging];
        const int to = positionAt (dragX + blockArea (0).getWidth() * 0.5f);
        std::vector<int> rest;
        for (int p = 0; p < SiltProcessor::numModules; ++p)
            if (p != dragging)
                rest.push_back (order[(size_t) p]);
        rest.insert (rest.begin() + to, module);
        std::copy (rest.begin(), rest.end(), order.begin());
    }

    const auto first = blockArea (0), last = blockArea (SiltProcessor::numModules - 1);
    const float y = first.getCentreY();
    const auto ends = [&] (juce::Rectangle<float> r, const char* text)
    {
        g.setColour (screenPale.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (screenPale);
        g.drawText (text, r, juce::Justification::centred);
    };
    g.setFont (monoFont (11.0f));
    const auto grains = juce::Rectangle<float> (first.getX() - gap - endWidth, first.getY() + 8.0f, endWidth, first.getHeight() - 16.0f);
    const auto out = juce::Rectangle<float> (last.getRight() + gap, last.getY() + 8.0f, endWidth, last.getHeight() - 16.0f);
    ends (grains, "GRAINS");
    ends (out, "OUT");

    g.setColour (screenPale.withAlpha (0.8f));
    arrow (g, grains.getRight(), first.getX(), y);
    for (int p = 0; p + 1 < SiltProcessor::numModules; ++p)
        arrow (g, blockArea (p).getRight(), blockArea (p + 1).getX(), y);
    arrow (g, last.getRight(), out.getX(), y);

    const auto block = [&] (juce::Rectangle<float> r, int module, bool lifted)
    {
        const bool active = shownActive[(size_t) module];
        g.setColour (lifted ? screenLight : screenLight.withAlpha (active ? 0.55f : 0.2f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (lifted ? juce::Colours::white : (active ? screenPale : screenPale.withAlpha (0.45f)));
        g.drawRoundedRectangle (r, 5.0f, lifted ? 2.0f : 1.2f);
        g.setFont (monoFont (juce::jlimit (9.0f, 13.0f, r.getWidth() / 8.0f)));
        g.setColour (active || lifted ? juce::Colours::white : screenPale.withAlpha (0.6f));
        g.drawText (SiltProcessor::moduleName (module), r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centred);
        // The ON switch: a pill with a light, lit while the module is on.
        const bool on = shownOn[(size_t) module];
        const auto pill = r.withTrimmedTop (r.getHeight() * 0.55f).withSizeKeepingCentre (juce::jmin (r.getWidth() - 12.0f, 54.0f), 16.0f);
        g.setColour (juce::Colours::black.withAlpha (on ? 0.25f : 0.4f));
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (on ? screenLime.withAlpha (0.8f) : screenPale.withAlpha (0.4f));
        g.drawRoundedRectangle (pill, 8.0f, 1.0f);
        const auto light = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ pill.getX() + 10.0f, pill.getCentreY() });
        g.setColour (on ? screenLime : screenPale.withAlpha (0.3f));
        g.fillEllipse (light);
        g.setFont (monoFont (9.0f));
        g.setColour (on ? screenLime : screenPale.withAlpha (0.6f));
        g.drawText (on ? "ON" : "OFF", pill.withTrimmedLeft (14.0f), juce::Justification::centred);
    };

    const int draggedModule = dragging >= 0 ? shownOrder[(size_t) dragging] : -1;
    for (int p = 0; p < SiltProcessor::numModules; ++p)
        if (order[(size_t) p] != draggedModule)
            block (blockArea (p), order[(size_t) p], false);
        else
        {
            g.setColour (screenPale.withAlpha (0.35f));   // where it will land
            g.drawRoundedRectangle (blockArea (p), 5.0f, 1.0f);
        }
    if (dragging >= 0)
        block (blockArea (0).withX (dragX).translated (0.0f, -3.0f), draggedModule, true);
}

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    moved = false;
    for (int p = 0; p < SiltProcessor::numModules; ++p)
        if (switchArea (p).expanded (2.0f).contains (e.position))
        {
            // The switch: one gesture, so it undoes like any button.
            auto* parameter = silt.params.getParameter (SiltProcessor::moduleOnId (shownOrder[(size_t) p]));
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
            parameter->endChangeGesture();
            update();
            return;
        }
    for (int p = 0; p < SiltProcessor::numModules; ++p)
        if (blockArea (p).contains (e.position))
        {
            dragging = p;
            dragOffset = e.position.x - blockArea (p).getX();
            dragX = blockArea (p).getX();
        }
}

void ChainView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    moved = moved || e.getDistanceFromDragStart() > 4;
    if (moved)
    {
        dragX = juce::jlimit (blockArea (0).getX(), blockArea (SiltProcessor::numModules - 1).getX(), e.position.x - dragOffset);
        repaint();
    }
}

void ChainView::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    const int from = dragging;
    dragging = -1;

    if (! moved)
    {
        if (onOpen != nullptr)
            onOpen (shownOrder[(size_t) from]);
        return;
    }

    const int to = positionAt (dragX + blockArea (0).getWidth() * 0.5f);
    if (to != from)
    {
        std::vector<int> order (shownOrder.begin(), shownOrder.end());
        const int module = order[(size_t) from];
        order.erase (order.begin() + from);
        order.insert (order.begin() + to, module);
        std::array<int, SiltProcessor::numModules> next {};
        std::copy (order.begin(), order.end(), next.begin());

        auto* parameter = silt.params.getParameter ("chainOrder");
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) SiltProcessor::chainOrderIndex (next)));
        parameter->endChangeGesture();
        shownOrder = next;
    }
    repaint();
}
