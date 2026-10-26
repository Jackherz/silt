#pragma once

#include "PluginProcessor.h"

#include <functional>

// The modulation UI around the pages: a rail of four chips (one per modulator)
// beside every page, the MOD page's banner, and the route list.
// See docs/mod-section-design.md.
namespace silt::ui
{

// What the chips, banner and route list ask the editor to do.
struct ModActions
{
    std::function<void (int modulator, bool arm)> select;   // show it on MOD; arm it too if asked
    std::function<void (int modulator)> edit;               // select and open the MOD page
    std::function<void (int modulator)> showRoutes;         // −1: all
    std::function<juce::String (int target)> pageOf;        // "FILTER" for filterCutoff
};

// One modulator: its colour and number, type, a live meter, how many routes it
// has, ARMED (filled with its colour) and MUTED (dimmed). Click selects and arms
// (again: disarms), Shift-click only selects, drag onto a knob makes a route,
// right-click for the rest. The × that appears on hover clears its routes on a
// second click.
class ModChip final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ModChip (SiltProcessor&, ModActions&, int modulator);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { pendingClear = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void update();   // from the editor's timer: repaint if anything shown changed

private:
    void timerCallback() override { pendingClear = false; stopTimer(); repaint(); }
    void showMenu();
    juce::Rectangle<int> clearBox() const;
    void setModulatorOn (bool on);

    SiltProcessor& silt;
    ModActions& actions;
    const int m;
    bool dragging = false, pendingClear = false;
    float shownValue = 0.0f;
    int shownRoutes = -1, shownType = -1;
    bool shownArmed = false, shownMuted = false, shownSelected = false;
};

class ModRail final : public juce::Component
{
public:
    ModRail (SiltProcessor&, ModActions&);
    void resized() override;
    void update();

private:
    std::array<std::unique_ptr<ModChip>, SiltProcessor::numModulators> chips;
};

// The MOD page's slot 0: which modulator the page shows. Click for the next
// one; the controller's knob 1 picks it too.
class ModBanner final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModBanner (SiltProcessor&, ModActions&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    SiltProcessor& silt;
    ModActions& actions;
};

// Every route in one list: filter by modulator or page, drag a depth to change
// it (double-click to type), × removes one, CLEAR ALL (twice) removes them all.
// Lists any route, whatever control (or none) shows it.
class RouteList final : public juce::Component, private juce::Timer
{
public:
    RouteList (SiltProcessor&, ModActions&);
    ~RouteList() override;

    void setFilter (int modulator) { filterModulator = modulator; rebuild(); }
    void setCurrentPage (const juce::String& name) { currentPage = name; rebuild(); }
    void update();   // from the editor's timer while shown
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    void rebuild();
    void timerCallback() override { pendingClearAll = false; stopTimer(); clearAll.setButtonText ("CLEAR ALL"); }

    SiltProcessor& silt;
    ModActions& actions;
    int filterModulator = -1;
    bool thisPageOnly = false, pendingClearAll = false;
    juce::String currentPage;
    std::vector<SiltProcessor::Route> shown;

    std::array<juce::TextButton, SiltProcessor::numModulators + 1> filters;   // ALL, M1–M4
    juce::TextButton pageFilter { "THIS PAGE" }, clearAll { "CLEAR ALL" }, close { "CLOSE" };
    juce::Viewport viewport;
    juce::Component rows;
    std::vector<std::unique_ptr<Row>> rowComponents;
    juce::Rectangle<int> footer;
};

} // namespace silt::ui
