#include "ChainView.h"
#include "Palette.h"

using namespace silt::ui;

namespace
{
constexpr float gap = 12.0f;

float value (const SiltProcessor& silt, const char* id) { return silt.params.getRawParameterValue (id)->load(); }

const char* shortName (int module)
{
    static const char* names[] { "RES", "FILTER", "COLOR", "SPACE" };
    return names[juce::jlimit (0, 3, module)];
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
        case SiltProcessor::moduleFilter:
            return ! SiltProcessor::filterIsOpen (value (silt, "filterCutoff"), value (silt, "filterReso") * 0.01f, value (silt, "filterMorph"), value (silt, "filterDrive") * 0.01f,
                                                  value (silt, "filterEnv") * 0.01f, value (silt, "filterKey") * 0.01f, value (silt, "filterMix") * 0.01f);
        case SiltProcessor::moduleColor:
            return value (silt, "colorMix") > 0.0f
                && (value (silt, "colorDrive") > 0.0f || value (silt, "colorBits") < 15.99f || value (silt, "colorRate") < silt::Color::rateOff * 0.999f
                    || value (silt, "colorComp") > 0.0f || value (silt, "colorNoise") > 0.0f || value (silt, "colorWow") > 0.0f
                    || std::abs (value (silt, "colorTone")) > 0.05f);
        default:
            return value (silt, "spaceDelay") > 0.0f || value (silt, "spaceReverb") > 0.0f || value (silt, "spaceFreeze") > 0.5f;
    }
}

juce::String ChainView::idleReason (const SiltProcessor& silt, int module)
{
    if (! moduleOn (silt, module) || moduleActive (silt, module))
        return {};
    switch (module)
    {
        case SiltProcessor::moduleResonator: return "MIX 0";
        case SiltProcessor::moduleFilter:    return value (silt, "filterMix") <= 0.0f ? "MIX 0" : "OPEN";
        case SiltProcessor::moduleColor:     return value (silt, "colorMix") <= 0.0f ? "MIX 0" : "NEUTRAL";
        default:                             return "LEVELS 0";
    }
}

void ChainView::updateMeters()
{
    const auto toShown = [] (float peak) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f); };
    bool changed = false;
    for (int m = 0; m < SiltProcessor::numModules; ++m)
    {
        const auto i = (size_t) m;
        // Up at once, down at about 30 dB a second at this frame rate.
        changeShown[i] = juce::jmax (toShown (silt.moduleChangePeak[i].exchange (0.0f, std::memory_order_relaxed)), changeShown[i] - 0.016f);
        outputShown[i] = juce::jmax (toShown (silt.moduleOutputPeak[i].exchange (0.0f, std::memory_order_relaxed)), outputShown[i] - 0.016f);
        const int pixels = juce::roundToInt (changeShown[i] * 40.0f);
        const int lit = outputShown[i] > 0.0f ? 1 : 0;
        if (pixels != meterPixels[i] || lit != arrowLit[i])
        {
            meterPixels[i] = pixels;
            arrowLit[i] = lit;
            changed = true;
        }
    }
    if (changed && dragging < 0)
        repaint();
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

void ChainView::setOpen (int module, int half)
{
    if (module != openModule || half != openHalf)
    {
        openModule = module;
        openHalf = half;
        repaint();
    }
}

juce::Rectangle<int> ChainView::moduleBounds (int module) const
{
    for (int p = 0; p < SiltProcessor::numModules; ++p)
        if (shownOrder[(size_t) p] == module)
            return blockArea (p).toNearestInt().translated (getX(), getY());
    return {};
}

juce::Rectangle<float> ChainView::blockArea (int position) const
{
    const auto row = getLocalBounds().toFloat();
    const float w = (row.getWidth() - gap * (SiltProcessor::numModules - 1)) / SiltProcessor::numModules;
    return { row.getX() + (float) position * (w + gap), row.getY(), w, row.getHeight() };
}

juce::Rectangle<float> ChainView::switchArea (int position) const
{
    const auto r = blockArea (position);
    return r.withTrimmedTop (r.getHeight() * 0.52f).reduced (5.0f, 4.0f);
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

    // The signal runs left to right between the blocks: lit while sound is leaving the one before.
    for (int p = 0; p + 1 < SiltProcessor::numModules; ++p)
    {
        const auto r = blockArea (p);
        const float x = r.getRight() + 2.0f, y = r.getCentreY();
        juce::Path head;
        head.addTriangle (x + 8.0f, y, x + 1.0f, y - 4.0f, x + 1.0f, y + 4.0f);
        g.setColour (arrowLit[(size_t) order[(size_t) p]] != 0 ? ink : inkSoft.withAlpha (0.35f));
        g.fillPath (head);
    }

    const auto block = [&] (juce::Rectangle<float> r, int module, bool lifted)
    {
        const auto i = (size_t) module;
        const bool on = shownOn[i], open = module == openModule;
        const bool working = shownActive[i] || (on && changeShown[i] > 0.0f);   // the meter knows about modulated mixes too
        const auto face = r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
        // Standing out is for a block that's doing something: the rest sit back, shadow and frame.
        if (working || lifted || open)
        {
            g.setColour (stock().shadow);
            g.fillRect (face.translated (2.0f, 2.0f));
        }
        g.setColour (lifted ? ink : stock().panel);
        g.fillRect (face);
        g.setColour (working || lifted || open ? ink : inkSoft.withAlpha (0.5f));
        g.drawRect (face, working || lifted || open ? 1.2f : 1.0f);
        // The top half opens its page, so the open page's block shows as a held key.
        const auto top = face.withBottom (face.getY() + face.getHeight() * 0.5f);
        if (open)
        {
            g.setColour (ink);
            g.fillRect (top);
        }
        const auto onTop = lifted || open ? stock().panel : ink;
        // Below the name: why it's idle, or how much it's changing the sound.
        const auto nameArea = top.withTrimmedBottom (top.getHeight() * 0.3f);
        auto status = top.withTop (nameArea.getBottom()).withTrimmedBottom (4.0f).reduced (6.0f, 0.0f);
        g.setFont (kit::label (16.0f));
        g.setColour (working || lifted || open ? onTop : inkSoft);
        const juce::String half = open && module == SiltProcessor::moduleResonator ? (openHalf == 0 ? " 1/2" : " 2/2") : "";
        g.drawText (shortName (module) + half, nameArea, juce::Justification::centredBottom);
        if (working)
        {
            const auto track = status.withSizeKeepingCentre (status.getWidth(), 5.0f);
            g.setColour (onTop.withAlpha (0.25f));
            g.fillRect (track);
            g.setColour (onTop);
            g.fillRect (track.withWidth (track.getWidth() * changeShown[i]));
        }
        else if (on)
        {
            g.setFont (kit::label (10.0f));
            g.setColour (open || lifted ? onTop.withAlpha (0.8f) : inkSoft);
            g.drawText (idleReason (silt, module), status, juce::Justification::centred);
        }
        // The switch says one thing: on or off.
        const auto box = face.withTrimmedTop (face.getHeight() * 0.52f).reduced (5.0f, 4.0f);
        g.setColour (lifted ? stock().panel : on ? ink : inkSoft);
        if (on && ! lifted)
            g.fillRect (box);
        else
            g.drawRect (box, 1.0f);
        g.setFont (kit::label (14.0f));
        g.setColour (lifted ? stock().panel : on ? stock().panel : inkSoft);
        g.drawText (on ? "ON" : "OFF", box, juce::Justification::centred);
    };

    const int draggedModule = dragging >= 0 ? shownOrder[(size_t) dragging] : -1;
    for (int p = 0; p < SiltProcessor::numModules; ++p)
        if (order[(size_t) p] != draggedModule)
            block (blockArea (p), order[(size_t) p], false);
        else
        {
            g.setColour (inkSoft);   // where it will land
            g.drawRect (blockArea (p).withTrimmedRight (2.0f).withTrimmedBottom (2.0f), 1.0f);
        }
    if (dragging >= 0)
        block (blockArea (0).withX (dragX), draggedModule, true);
}

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    moved = false;
    if (e.mods.isPopupMenu())
    {
        for (int p = 0; p < SiltProcessor::numModules; ++p)
            if (blockArea (p).contains (e.position) && onMenu != nullptr)
                onMenu (shownOrder[(size_t) p]);
        return;
    }
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
