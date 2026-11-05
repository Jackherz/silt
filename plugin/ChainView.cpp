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

    // The signal runs left to right between the blocks.
    g.setColour (ink);
    for (int p = 0; p + 1 < SiltProcessor::numModules; ++p)
    {
        const auto r = blockArea (p);
        const float x = r.getRight() + 2.0f, y = r.getCentreY();
        juce::Path head;
        head.addTriangle (x + 8.0f, y, x + 1.0f, y - 4.0f, x + 1.0f, y + 4.0f);
        g.fillPath (head);
    }

    const auto block = [&] (juce::Rectangle<float> r, int module, bool lifted)
    {
        const bool active = shownActive[(size_t) module], on = shownOn[(size_t) module];
        const bool open = module == openModule;
        const auto face = r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
        // Standing out is for a block that's doing something: the rest sit back, shadow and frame.
        if (active || lifted || open)
        {
            g.setColour (stock().shadow);
            g.fillRect (face.translated (2.0f, 2.0f));
        }
        g.setColour (lifted ? ink : stock().panel);
        g.fillRect (face);
        g.setColour (active || lifted || open ? ink : inkSoft.withAlpha (0.5f));
        g.drawRect (face, active || lifted || open ? 1.2f : 1.0f);
        // The top half opens its page, so the open page's block shows as a held key.
        const auto top = face.withBottom (face.getY() + face.getHeight() * 0.5f);
        if (open)
        {
            g.setColour (ink);
            g.fillRect (top);
        }
        g.setFont (kit::label (16.0f));
        g.setColour (lifted || open ? stock().panel : active ? ink : inkSoft);
        const juce::String half = open && module == SiltProcessor::moduleResonator ? (openHalf == 0 ? " 1/2" : " 2/2") : "";
        g.drawText (shortName (module) + half, top, juce::Justification::centred);
        // The switch: solid only while the module is changing the sound. On with nothing
        // to do, it's an outline that still says ON.
        const auto box = face.withTrimmedTop (face.getHeight() * 0.52f).reduced (5.0f, 4.0f);
        g.setColour (active ? ink : lifted ? stock().panel : on ? ink : inkSoft);
        if (active)
            g.fillRect (box);
        else
            g.drawRect (box, 1.0f);
        g.setFont (kit::label (14.0f));
        g.setColour (active ? stock().panel : lifted ? stock().panel : on ? ink : inkSoft);
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
