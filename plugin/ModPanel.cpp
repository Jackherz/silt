#include "ModPanel.h"
#include "PageLayout.h"
#include "ModColours.h"
#include "Palette.h"

namespace silt::ui
{

namespace
{
const char* typeName (int type)
{
    static const char* names[] { "WAVE", "RANDOM", "ENV", "FOLLOW", "MIDI", "MACRO" };
    return names[juce::jlimit (0, 5, type)];
}

int typeOf (SiltProcessor& silt, int m)
{
    return (int) silt.readModulatorParams (m).type;
}

// Signed: + moves the value up first, − down first. Polarity has its own button.
juce::String percentText (float amount)
{
    const int percent = juce::roundToInt (amount * 100.0f);
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

// The chip: swatch and number, type, badges, and a short trace of what the
// modulator has done over the last two seconds (its shape, not just its
// level). Bipolar sources trace round a centre line, the rest up from the
// bottom. Armed: an outline in its colour; shown on the MOD page: an ink one.
// What a chip says: where it's going, or what it is until it goes anywhere.
static juce::String chipTitle (SiltProcessor& silt, int m)
{
    for (const auto& route : silt.allRoutes())
        if (route.modulator == m)
        {
            const auto id = silt.targetParameter (route.target).getParameterID();
            for (const auto& e : silt::layout::entries (SiltProcessor::isInstrument))
                if (e.id == id && e.label.isNotEmpty())
                    return "TO " + e.label;
            return "TO " + silt.targetParameter (route.target).getName (12).toUpperCase();
        }
    if (typeOf (silt, m) != 0)
        return typeName (typeOf (silt, m));
    static const char* shapes[] { "SINE", "TRIANGLE", "SAW", "SQUARE" };
    return shapes[juce::jlimit (0, 3, juce::roundToInt (silt.params.getRawParameterValue ("m" + juce::String (m + 1) + "Shape")->load()))];
}

void ModChip::paint (juce::Graphics& g)
{
    const auto& colour = modColour (m);
    const bool armed = silt.armedMod.load() == m;
    const bool muted = ! silt.isModulatorEnabled (m);
    const bool selected = silt.modShown.load() == m;
    const int routes = silt.routeCount (m);
    auto r = getLocalBounds().toFloat().reduced (1.5f);

    // A rounded card in a hairline; armed: its colour; shown on MOD: ink.
    if (muted)
    {
        g.setColour (ink.withAlpha (0.04f));
        g.fillRect (r);
    }
    if (armed)
    {
        g.setColour (colour.base);
        g.drawRect (r, 2.0f);
    }
    else
    {
        g.setColour (selected ? ink.withAlpha (0.8f) : ink.withAlpha (0.32f));
        g.drawRect (r, selected ? 1.4f : 1.0f);
    }

    auto area = getLocalBounds().reduced (5, 3);
    auto top = area.removeFromTop (juce::jmin (14, area.getHeight() / 2));
    const auto text = muted ? inkSoft : ink;

    // Swatch with the number: colour plus a label, never colour alone.
    auto swatch = top.removeFromLeft (18).withSizeKeepingCentre (18, 13).toFloat();
    g.setColour (muted ? colour.dim : colour.base);
    g.fillEllipse (swatch.withSizeKeepingCentre (14.0f, 14.0f));
    g.setColour (colour.ink);
    g.setFont (monoFont (9.5f, Weight::medium));
    g.drawText (juce::String (m + 1), swatch, juce::Justification::centred);
    top.removeFromLeft (5);

    // Right side: the clear × on hover, else the route count, only when routed.
    const bool hovered = isMouseOver (true);
    if (hovered && routes > 0)
    {
        auto box = clearBox().toFloat();
        g.setColour (pendingClear ? signalRed : text.withAlpha (0.7f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) 0x00d7), box, juce::Justification::centred);
        top.removeFromRight (16);
    }
    else if (routes > 0)
    {
        auto badge = top.removeFromRight (16).withSizeKeepingCentre (16, 12).toFloat();
        g.setColour (muted ? colour.dim : colour.base);
        g.fillRect (badge);
        g.setColour (colour.ink);
        g.setFont (monoFont (9.5f));
        g.drawText (juce::String (routes), badge, juce::Justification::centred);
    }
    if (silt.isModulatorPerVoice (m))   // a V badge: each voice runs its own copy
    {
        top.removeFromRight (3);
        auto badge = top.removeFromRight (13).withSizeKeepingCentre (12, 11).toFloat();
        g.setColour (text);
        g.drawRect (badge, 1.0f);
        g.setFont (monoFont (8.5f));
        g.drawText ("V", badge, juce::Justification::centred);
    }

    g.setFont (panelFont (10.0f, Weight::regular, 0.24f));
    g.setColour (pendingClear ? signalRed : text);
    const auto label = pendingClear ? juce::String ("CLICK AGAIN")
                     : muted ? juce::String ("MUTED")
                     : chipTitle (silt, m);
    g.drawText (label, top, juce::Justification::centredLeft);

    // The trace.
    auto trace = area.reduced (2, 1).toFloat();
    trace.removeFromLeft (21.0f);
    const bool bipolar = silt::Modulator::isBipolar (silt.readModulatorParams (m));
    const float baseline = bipolar ? trace.getCentreY() : trace.getBottom();
    g.setColour (ink.withAlpha (0.25f));
    g.drawHorizontalLine ((int) baseline, trace.getX(), trace.getRight());
    juce::Path line;
    const int n = (int) history.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = history[(size_t) ((written + i) % n)];
        const float y = bipolar ? baseline - juce::jlimit (-1.0f, 1.0f, v) * trace.getHeight() * 0.5f
                                : baseline - juce::jlimit (0.0f, 1.0f, v) * trace.getHeight();
        const float x = trace.getX() + trace.getWidth() * (float) i / (float) (n - 1);
        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }
    g.setColour (muted ? colour.dim : colour.base);
    g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ModChip::update()
{
    const float v = silt.modOutputs[(size_t) m].load();
    // Record the trace; while anything in it moves, it scrolls.
    history[(size_t) written] = v;
    written = (written + 1) % (int) history.size();
    const auto [lo, hi] = std::minmax_element (history.begin(), history.end());
    if (*hi - *lo > 1.0e-4f)
    {
        repaint();
        return;
    }
    const int routes = silt.routeCount (m), type = typeOf (silt, m);
    const bool armed = silt.armedMod.load() == m, muted = ! silt.isModulatorEnabled (m), selected = silt.modShown.load() == m;
    const bool perVoice = silt.isModulatorPerVoice (m);
    // Hover too: a drag can take the mouse away without a mouseExit, which
    // would leave the chip drawn as if still under it.
    const bool hovered = isMouseOver (true);
    if (std::abs (v - shownValue) > 0.004f || routes != shownRoutes || type != shownType
        || armed != shownArmed || muted != shownMuted || selected != shownSelected || perVoice != shownPerVoice
        || hovered != shownHovered)
    {
        shownHovered = hovered;
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
            silt.beginRouteGesture();
            silt.editRoutes ([this] { silt.clearRoutesForModulator (m); });
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
    // Dragging this one while another is armed: this one takes over (armed and
    // shown), so the highlight follows the chip in hand.
    if (const int armed = silt.armedMod.load(); armed >= 0 && armed != m)
        actions.select (m, true);
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->startDragging ("mod:" + juce::String (m + 1), this, juce::ScaledImage (createComponentSnapshot (getLocalBounds())));
}

void ModChip::mouseUp (const juce::MouseEvent& e)
{
    if (dragging || e.mods.isPopupMenu())
        return;
    armedBeforeClick = silt.armedMod.load();
    shownBeforeClick = silt.modShown.load();
    actions.select (m, ! e.mods.isShiftDown());
}

// A double-click opens this modulator's MOD page, and from there goes back. It only looks:
// the arming its first click did is undone, and the press's release does nothing more.
void ModChip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || (clearBox().contains (e.getPosition()) && silt.routeCount (m) > 0))
        return;
    silt.armedMod.store (armedBeforeClick);
    dragging = true;
    if (actions.openPage)
        actions.openPage (m, shownBeforeClick == m);
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
                                                        {
                                                            silt.beginRouteGesture();
                                                            silt.editRoutes ([this] { silt.clearRoutesForModulator (m); });
                                                        }
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
    g.fillRect (r);
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
            const float typed = snapped (depth.getText().retainCharacters ("-0123456789.").getFloatValue() / 100.0f);
            silt.beginRouteGesture();
            silt.editRoutes ([&] { silt.setRouteAmount (route.modulator, route.target, typed); });
            refresh();
        };
        addAndMakeVisible (depth);
        polarity.setTooltip ("Bipolar (both ways round the knob) or unipolar (one way)");
        polarity.onClick = [this]
        {
            silt.beginRouteGesture();
            silt.editRoutes ([this] { silt.setRoutePolarity (route.modulator, route.target,
                                                             silt.routeIsBipolar (route.modulator, route.target) ? SiltProcessor::unipolar : SiltProcessor::bipolar); });
            refresh();
        };
        addAndMakeVisible (polarity);
        remove.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x00d7));
        remove.onClick = [this]
        {
            silt.beginRouteGesture();
            silt.editRoutes ([this] { silt.setRouteAmount (route.modulator, route.target, 0.0f); });
        };
        addAndMakeVisible (remove);
        refresh();
    }

    void refresh()
    {
        const bool bipolar = silt.routeIsBipolar (route.modulator, route.target);
        depth.setText (percentText (silt.routeAmount (route.modulator, route.target)), juce::dontSendNotification);
        polarity.setButtonText (bipolar ? juce::String::charToString ((juce::juce_wchar) 0x00b1) : juce::String ("+"));
        span = silt.depthSpan (route.modulator, route.target);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &depth)
        {
            amountRunning = silt.routeAmount (route.modulator, route.target);
            lastY = e.position.y;
            silt.beginRouteGesture();
            dragging = true;
        }
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &depth)
            return;
        // Each move adds its own change at the rate Shift sets now (pressed or let go mid-drag).
        amountRunning = juce::jlimit (-1.0f, 1.0f, amountRunning + (lastY - e.position.y) / (e.mods.isShiftDown() ? 1000.0f : 150.0f));
        lastY = e.position.y;
        silt.editRoutes ([&] { silt.setRouteAmount (route.modulator, route.target, snapped (amountRunning)); });
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
        g.fillRect (dot);
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
        r.removeFromLeft (82 + 30);   // the depth box and polarity
        g.setColour (inkSoft);
        g.setFont (monoFont (11.0f));
        g.drawText (span, r.withTrimmedRight (34), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        remove.setBounds (r.removeFromRight (26));
        depth.setBounds (r.withX (34 + 6 + 84 + 170).withWidth (72));
        polarity.setBounds (r.withX (34 + 6 + 84 + 170 + 76).withWidth (26));
    }

    const SiltProcessor::Route& getRoute() const noexcept { return route; }
    bool isDragging() const noexcept { return dragging; }

private:
    SiltProcessor& silt;
    ModActions& actions;
    SiltProcessor::Route route;
    juce::Label depth;
    juce::TextButton polarity, remove;
    juce::String span;
    float amountRunning = 0.0f, lastY = 0.0f;   // a drag's amount so far (unrounded) and where the mouse last was
    bool dragging = false;   // a depth at 0 mid-drag stays listed until the drag ends
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
            silt.beginRouteGesture();
            silt.editRoutes ([this]
            {
                for (int m = 0; m < SiltProcessor::numModulators; ++m)
                    silt.clearRoutesForModulator (m);
            });
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
    undo.onClick = [this] { silt.undoRoutes(); rebuild(); };
    redo.onClick = [this] { silt.redoRoutes(); rebuild(); };
    addAndMakeVisible (undo);
    addAndMakeVisible (redo);
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
    const bool dragging = std::any_of (rowComponents.begin(), rowComponents.end(), [] (const auto& row) { return row->isDragging(); });
    if (! same && ! dragging)
    {
        rebuild();
        return;
    }
    for (auto& row : rowComponents)
        row->refresh();
    undo.setEnabled (silt.canUndoRoutes());
    redo.setEnabled (silt.canRedoRoutes());
    repaint (footer);
}

void RouteList::paint (juce::Graphics& g)
{
    g.setColour (bone);
    g.fillRect (getLocalBounds());
    g.setColour (ink);
    g.drawRect (getLocalBounds(), 2);
    g.setFont (panelFont (12.0f));
    g.drawText ("MOD ROUTES", getLocalBounds().reduced (10, 4).removeFromTop (22), juce::Justification::centredLeft);

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
    head.removeFromLeft (112);   // the "MOD ROUTES" title
    close.setBounds (head.removeFromRight (64));
    head.removeFromRight (6);
    pageFilter.setBounds (head.removeFromRight (84));
    head.removeFromRight (6);
    for (auto& f : filters)
        f.setBounds (head.removeFromLeft (40).reduced (2, 0));
    footer = r.removeFromBottom (24);
    auto buttons = footer;
    clearAll.setBounds (buttons.removeFromRight (110).reduced (0, 2));
    buttons.removeFromRight (8);
    redo.setBounds (buttons.removeFromRight (64).reduced (0, 2));
    buttons.removeFromRight (4);
    undo.setBounds (buttons.removeFromRight (64).reduced (0, 2));
    r.removeFromTop (2);
    viewport.setBounds (r);

    const int rowHeight = 24;
    rows.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (r.getHeight(), (int) rowComponents.size() * rowHeight));
    for (size_t i = 0; i < rowComponents.size(); ++i)
        rowComponents[i]->setBounds (0, (int) i * rowHeight, rows.getWidth(), rowHeight);
}

} // namespace silt::ui
