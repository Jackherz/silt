#include "ModPanel.h"
#include "ModColours.h"
#include "Palette.h"

namespace silt::ui
{

namespace
{
const char* typeName (int type)
{
    static const char* names[] { "WAVE", "RANDOM", "ENV", "FOLLOW", "MIDI" };
    return names[juce::jlimit (0, 4, type)];
}

int typeOf (SiltProcessor& silt, int m)
{
    return (int) silt.readModulatorParams (m).type;
}

juce::String percentText (float amount, bool bipolar)
{
    const int percent = juce::roundToInt (amount * 100.0f);
    if (bipolar)
        return juce::String::charToString ((juce::juce_wchar) 0x00b1) + juce::String (std::abs (percent)) + "%";
    return (percent > 0 ? "+" : "") + juce::String (percent) + "%";
}

// Depth drags snap to zero near it: 0 is otherwise under a pixel wide.
float snapped (float amount) { return std::abs (amount) < 0.02f ? 0.0f : juce::jlimit (-1.0f, 1.0f, amount); }
} // namespace

// --- ModChip --------------------------------------------------------------------------

ModChip::ModChip (SiltProcessor& p, ModActions& a, int modulator) : silt (p), actions (a), m (modulator)
{
    setRepaintsOnMouseActivity (true);
    setTooltip ("MOD " + juce::String (m + 1) + ": click to arm, shift-click to select, drag onto a knob, right-click for more");
}

juce::Rectangle<int> ModChip::clearBox() const
{
    return getLocalBounds().reduced (3).removeFromRight (14).removeFromTop (14);
}

void ModChip::paint (juce::Graphics& g)
{
    const auto& colour = modColour (m);
    const bool armed = silt.armedMod.load() == m;
    const bool muted = ! silt.isModulatorEnabled (m);
    const bool selected = silt.modShown.load() == m;
    const int routes = silt.routeCount (m);
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (armed ? colour.base : (muted ? chassis.darker (0.08f) : bone));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (selected ? ink : boneLip);
    g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);

    auto area = getLocalBounds().reduced (5, 3);
    auto top = area.removeFromTop (juce::jmax (12, area.getHeight() - 8));
    const auto text = armed ? colour.ink : (muted ? inkSoft : ink);

    // Swatch with the number: colour plus a label, never colour alone.
    auto swatch = top.removeFromLeft (20).withSizeKeepingCentre (20, 14).toFloat();
    g.setColour (muted ? colour.dim : colour.base);
    g.fillRoundedRectangle (swatch, 2.0f);
    if (armed)
    {
        g.setColour (colour.ink);
        g.drawRoundedRectangle (swatch, 2.0f, 1.0f);
    }
    g.setColour (colour.ink);
    g.setFont (monoFont (10.0f));
    g.drawText (juce::String (m + 1), swatch, juce::Justification::centred);
    top.removeFromLeft (5);

    const bool hovered = isMouseOver (true);
    if (hovered && routes > 0)
    {
        auto box = clearBox().toFloat();
        g.setColour (pendingClear ? signalRed : text.withAlpha (0.7f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) 0x00d7), box, juce::Justification::centred);
        top.removeFromRight (16);
    }
    else
    {
        g.setColour (text);
        g.setFont (monoFont (10.0f));
        g.drawText (juce::String (routes), top.removeFromRight (16), juce::Justification::centredRight);
    }
    if (silt.isModulatorPerVoice (m))   // a V badge: each voice runs its own copy
    {
        auto badge = top.removeFromRight (13).withSizeKeepingCentre (12, 11).toFloat();
        g.setColour (text);
        g.drawRoundedRectangle (badge, 2.0f, 1.0f);
        g.setFont (monoFont (8.5f));
        g.drawText ("V", badge, juce::Justification::centred);
        top.removeFromRight (2);
    }

    g.setFont (monoFont (11.0f));
    g.setColour (pendingClear ? signalRed : text);
    const auto label = pendingClear ? juce::String ("CLICK AGAIN")
                     : muted ? juce::String ("MUTED")
                     : juce::String (typeName (typeOf (silt, m)));   // filled with its colour = armed
    g.drawText (label, top, juce::Justification::centredLeft);

    // The live meter: bipolar sources from the centre, the rest from the left.
    auto meter = area.removeFromBottom (5).toFloat();
    g.setColour ((armed ? colour.ink : ink).withAlpha (0.15f));
    g.fillRect (meter);
    const float v = silt.modOutputs[(size_t) m].load();
    const bool bipolar = silt::Modulator::isBipolar (silt.readModulatorParams (m));
    g.setColour (armed ? colour.ink : (muted ? colour.dim : colour.base));
    if (bipolar)
    {
        const float mid = meter.getCentreX(), x = mid + juce::jlimit (-1.0f, 1.0f, v) * meter.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), meter.getY(), juce::jmax (mid, x), meter.getBottom()));
        g.fillRect (mid - 0.5f, meter.getY() - 1.0f, 1.0f, meter.getHeight() + 2.0f);
    }
    else
        g.fillRect (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, v)));
}

void ModChip::update()
{
    const float v = silt.modOutputs[(size_t) m].load();
    const int routes = silt.routeCount (m), type = typeOf (silt, m);
    const bool armed = silt.armedMod.load() == m, muted = ! silt.isModulatorEnabled (m), selected = silt.modShown.load() == m;
    const bool perVoice = silt.isModulatorPerVoice (m);
    if (std::abs (v - shownValue) > 0.004f || routes != shownRoutes || type != shownType
        || armed != shownArmed || muted != shownMuted || selected != shownSelected || perVoice != shownPerVoice)
    {
        shownPerVoice = perVoice;
        shownValue = v;
        shownRoutes = routes;
        shownType = type;
        shownArmed = armed;
        shownMuted = muted;
        shownSelected = selected;
        repaint();
    }
}

void ModChip::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (clearBox().contains (e.getPosition()) && silt.routeCount (m) > 0)
    {
        if (pendingClear)
        {
            silt.clearRoutesForModulator (m);
            pendingClear = false;
            stopTimer();
        }
        else
        {
            pendingClear = true;
            startTimer (2500);
        }
        dragging = true;   // so mouseUp doesn't also select
        repaint();
    }
}

void ModChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 5)
        return;
    dragging = true;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->startDragging ("mod:" + juce::String (m + 1), this, juce::ScaledImage (createComponentSnapshot (getLocalBounds())));
}

void ModChip::mouseUp (const juce::MouseEvent& e)
{
    if (dragging || e.mods.isPopupMenu())
        return;
    actions.select (m, ! e.mods.isShiftDown());
}

void ModChip::setModulatorOn (bool on)
{
    if (auto* p = silt.params.getParameter ("m" + juce::String (m + 1) + "Enable"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void ModChip::showMenu()
{
    const int routes = silt.routeCount (m);
    const bool muted = ! silt.isModulatorEnabled (m);
    juce::PopupMenu menu;
    menu.addColouredItem (1, "Edit MOD " + juce::String (m + 1), modColour (m).base, true);
    menu.addItem (2, muted ? "Unmute" : "Mute");
    menu.addItem (3, "Show routes", routes > 0);
    menu.addItem (4, "Clear " + juce::String (routes) + (routes == 1 ? " route..." : " routes..."), routes > 0);
    menu.addSeparator();
    menu.addItem (5, "Mute all others");
    menu.addItem (6, "Unmute all");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, routes] (int chosen)
    {
        switch (chosen)
        {
            case 1: actions.edit (m); break;
            case 2: setModulatorOn (! silt.isModulatorEnabled (m)); break;
            case 3: actions.showRoutes (m); break;
            case 4:
            {
                // No undo, so ask.
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear MOD " + juce::String (m + 1),
                                                    "Remove its " + juce::String (routes) + (routes == 1 ? " route?" : " routes?"),
                                                    "Clear", "Cancel", this,
                                                    juce::ModalCallbackFunction::create ([this] (int ok)
                                                    {
                                                        if (ok != 0)
                                                            silt.clearRoutesForModulator (m);
                                                    }));
                break;
            }
            case 5:
            case 6:
                for (int other = 0; other < SiltProcessor::numModulators; ++other)
                    if (auto* p = silt.params.getParameter ("m" + juce::String (other + 1) + "Enable"))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (chosen == 6 || other == m ? 1.0f : 0.0f);
                        p->endChangeGesture();
                    }
                break;
            default: break;
        }
        repaint();
    });
}

// --- ModRail -------------------------------------------------------------------------

ModRail::ModRail (SiltProcessor& silt, ModActions& actions)
{
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
    {
        chips[(size_t) m] = std::make_unique<ModChip> (silt, actions, m);
        addAndMakeVisible (*chips[(size_t) m]);
    }
}

void ModRail::resized()
{
    auto r = getLocalBounds();
    const int gap = 4, h = (r.getHeight() - gap * (SiltProcessor::numModulators - 1)) / SiltProcessor::numModulators;
    for (auto& chip : chips)
    {
        chip->setBounds (r.removeFromTop (h));
        r.removeFromTop (gap);
    }
}

void ModRail::update()
{
    for (auto& chip : chips)
        chip->update();
}

// --- ModBanner -----------------------------------------------------------------------

ModBanner::ModBanner (SiltProcessor& p, ModActions& a) : silt (p), actions (a)
{
    setTooltip ("The modulator this page shows: click for the next, or pick one on the rail");
}

void ModBanner::paint (juce::Graphics& g)
{
    const int m = silt.modShown.load();
    const auto& colour = modColour (m);
    const bool muted = ! silt.isModulatorEnabled (m);
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (muted ? colour.dim : colour.base);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colour.ink);
    auto text = r.reduced (6.0f, 4.0f);
    g.setFont (monoFont (18.0f));
    g.drawText ("MOD " + juce::String (m + 1), text.removeFromTop (text.getHeight() * 0.5f), juce::Justification::centred);
    g.setFont (monoFont (10.0f));
    const int routes = silt.routeCount (m);
    g.drawText (juce::String (typeName (typeOf (silt, m))) + "  " + juce::String (routes) + (routes == 1 ? " ROUTE" : " ROUTES")
                    + (muted ? "  MUTED" : ""),
                text, juce::Justification::centred);
}

void ModBanner::mouseUp (const juce::MouseEvent&)
{
    actions.select ((silt.modShown.load() + 1) % SiltProcessor::numModulators, false);
    repaint();
}

// --- RouteList ------------------------------------------------------------------------

// One route: colour and number, page, parameter, depth (drag it, or double-click
// to type), what it spans in the parameter's units, and × to remove it.
class RouteList::Row final : public juce::Component
{
public:
    Row (SiltProcessor& p, ModActions& a, SiltProcessor::Route r) : silt (p), actions (a), route (r)
    {
        depth.setEditable (false, true, false);
        depth.setJustificationType (juce::Justification::centred);
        depth.setFont (monoFont (12.0f));
        depth.setColour (juce::Label::textColourId, lcdText);
        depth.setColour (juce::Label::backgroundColourId, lcdGreen);
        depth.setColour (juce::Label::outlineColourId, ink);
        depth.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        depth.addMouseListener (this, false);
        depth.onTextChange = [this]
        {
            silt.setRouteAmount (route.modulator, route.target, snapped (depth.getText().retainCharacters ("-0123456789.").getFloatValue() / 100.0f));
            refresh();
        };
        addAndMakeVisible (depth);
        remove.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x00d7));
        remove.onClick = [this] { silt.setRouteAmount (route.modulator, route.target, 0.0f); };
        addAndMakeVisible (remove);
        refresh();
    }

    void refresh()
    {
        const bool bipolar = silt::Modulator::isBipolar (silt.readModulatorParams (route.modulator));
        depth.setText (percentText (silt.routeAmount (route.modulator, route.target), bipolar), juce::dontSendNotification);
        span = silt.depthText (route.modulator, route.target).fromFirstOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &depth)
            amountAtStart = silt.routeAmount (route.modulator, route.target);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &depth)
            return;
        const float amount = amountAtStart - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 1000.0f : 150.0f);
        silt.setRouteAmount (route.modulator, route.target, snapped (amount));
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& colour = modColour (route.modulator);
        auto r = getLocalBounds();
        g.setColour (ink.withAlpha (0.12f));
        g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) r.getWidth());
        auto dot = r.removeFromLeft (34).reduced (4, 4).toFloat();
        g.setColour (silt.isModulatorEnabled (route.modulator) ? colour.base : colour.dim);
        g.fillRoundedRectangle (dot, 2.0f);
        g.setColour (colour.ink);
        g.setFont (monoFont (10.0f));
        g.drawText ("M" + juce::String (route.modulator + 1), dot, juce::Justification::centred);

        r.removeFromLeft (6);
        g.setColour (inkSoft);
        g.setFont (monoFont (10.0f));
        g.drawText (actions.pageOf (route.target), r.removeFromLeft (84), juce::Justification::centredLeft);
        g.setColour (ink);
        g.setFont (panelFont (12.0f));
        g.drawText (silt.targetParameter (route.target).getName (40).toUpperCase(), r.removeFromLeft (170), juce::Justification::centredLeft);
        r.removeFromLeft (82);   // the depth box
        g.setColour (inkSoft);
        g.setFont (monoFont (11.0f));
        g.drawText (span, r.withTrimmedRight (34), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        remove.setBounds (r.removeFromRight (26));
        depth.setBounds (r.withX (34 + 6 + 84 + 170).withWidth (72));
    }

    const SiltProcessor::Route& getRoute() const noexcept { return route; }

private:
    SiltProcessor& silt;
    ModActions& actions;
    SiltProcessor::Route route;
    juce::Label depth;
    juce::TextButton remove;
    juce::String span;
    float amountAtStart = 0.0f;
};

RouteList::RouteList (SiltProcessor& p, ModActions& a) : silt (p), actions (a)
{
    for (size_t i = 0; i < filters.size(); ++i)
    {
        auto& f = filters[i];
        f.setButtonText (i == 0 ? juce::String ("ALL") : "M" + juce::String ((int) i));
        f.setClickingTogglesState (true);
        f.setRadioGroupId (77);
        f.onClick = [this, i] { setFilter ((int) i - 1); };
        if (i > 0)
            f.setColour (juce::TextButton::buttonOnColourId, modColour ((int) i - 1).base);
        addAndMakeVisible (f);
    }
    filters[0].setToggleState (true, juce::dontSendNotification);
    pageFilter.setClickingTogglesState (true);
    pageFilter.onClick = [this] { thisPageOnly = pageFilter.getToggleState(); rebuild(); };
    addAndMakeVisible (pageFilter);
    clearAll.onClick = [this]
    {
        if (pendingClearAll)
        {
            for (int m = 0; m < SiltProcessor::numModulators; ++m)
                silt.clearRoutesForModulator (m);
            timerCallback();
            rebuild();
        }
        else
        {
            pendingClearAll = true;
            clearAll.setButtonText ("CLICK AGAIN");
            startTimer (2500);
        }
    };
    addAndMakeVisible (clearAll);
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

RouteList::~RouteList() = default;

void RouteList::rebuild()
{
    for (size_t i = 0; i < filters.size(); ++i)
        filters[i].setToggleState ((int) i - 1 == filterModulator, juce::dontSendNotification);

    shown.clear();
    for (const auto& route : silt.allRoutes())
        if ((filterModulator < 0 || route.modulator == filterModulator)
            && (! thisPageOnly || actions.pageOf (route.target) == currentPage))
            shown.push_back (route);

    rowComponents.clear();
    for (const auto& route : shown)
    {
        rowComponents.push_back (std::make_unique<Row> (silt, actions, route));
        rows.addAndMakeVisible (*rowComponents.back());
    }
    resized();
    repaint();
}

void RouteList::update()
{
    // Rebuild when routes come or go; otherwise just refresh the depths.
    std::vector<SiltProcessor::Route> now;
    for (const auto& route : silt.allRoutes())
        if ((filterModulator < 0 || route.modulator == filterModulator)
            && (! thisPageOnly || actions.pageOf (route.target) == currentPage))
            now.push_back (route);
    const bool same = now.size() == shown.size()
                   && std::equal (now.begin(), now.end(), shown.begin(), [] (const auto& a, const auto& b)
                                  { return a.modulator == b.modulator && a.target == b.target; });
    if (! same)
    {
        rebuild();
        return;
    }
    for (auto& row : rowComponents)
        row->refresh();
    repaint (footer);
}

void RouteList::paint (juce::Graphics& g)
{
    g.setColour (bone);
    g.fillRect (getLocalBounds());
    g.setColour (ink);
    g.drawRect (getLocalBounds(), 2);
    g.setFont (panelFont (12.0f));
    g.drawText ("ROUTES", getLocalBounds().reduced (10, 4).removeFromTop (22), juce::Justification::centredLeft);

    if (shown.empty())
    {
        g.setColour (inkSoft);
        g.setFont (monoFont (12.0f));
        g.drawText ("NO ROUTES. ALT-DRAG A KNOB, OR DRAG A MOD CHIP ONTO ONE.", viewport.getBounds(), juce::Justification::centred);
    }

    int muted = 0;
    for (int m = 0; m < SiltProcessor::numModulators; ++m)
        muted += silt.isModulatorEnabled (m) ? 0 : 1;
    const int total = (int) silt.allRoutes().size();
    g.setColour (ink);
    g.setFont (monoFont (11.0f));
    g.drawText (juce::String (total) + (total == 1 ? " ROUTE" : " ROUTES") + "   " + juce::String (muted) + " MUTED",
                footer.withTrimmedLeft (10), juce::Justification::centredLeft);
}

void RouteList::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto head = r.removeFromTop (22);
    head.removeFromLeft (70);
    close.setBounds (head.removeFromRight (64));
    head.removeFromRight (6);
    pageFilter.setBounds (head.removeFromRight (84));
    head.removeFromRight (6);
    for (auto& f : filters)
        f.setBounds (head.removeFromLeft (40).reduced (2, 0));
    footer = r.removeFromBottom (24);
    clearAll.setBounds (footer.withTrimmedLeft (footer.getWidth() - 110).reduced (0, 2));
    r.removeFromTop (2);
    viewport.setBounds (r);

    const int rowHeight = 24;
    rows.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (r.getHeight(), (int) rowComponents.size() * rowHeight));
    for (size_t i = 0; i < rowComponents.size(); ++i)
        rowComponents[i]->setBounds (0, (int) i * rowHeight, rows.getWidth(), rowHeight);
}

} // namespace silt::ui
