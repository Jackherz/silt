#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The first-run tour: a few cards, each pointing at parts of the panel.
// Everything else dims; the lit parts stay live, so you can try them while
// the card says what to do, and it ticks once you have. NEXT, BACK and SKIP
// (or the arrow keys, Return and Esc) move through it. It opens by itself the
// first time Silt is opened on a computer, and from the ? menu after.
namespace silt::ui
{
class Tour final : public juce::Component, private juce::Timer
{
public:
    struct Step
    {
        juce::String title, body;
        std::function<juce::RectangleList<int>()> spots;   // in the parent's space; null: none, card centred
        int page = -1;                                     // a page to show first, or −1
        juce::String tryIt;                                // what to do, or empty
        std::function<bool()> done;                        // whether it's been done
    };

    Tour();

    void start (std::vector<Step>);
    bool isRunning() const noexcept { return isVisible(); }
    int currentStep() const noexcept { return step; }
    void go (int index);   // past the end: finish

    std::function<void (int page)> showPage;
    std::function<void()> onFinish;   // finished or skipped

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool hitTest (int x, int y) override;   // the lit parts let clicks through

    // Has this computer seen the tour? Kept in Silt's settings file, not the session.
    static bool seen();
    static void markSeen();
    static inline bool remember = true;   // tests and the snapshot tool leave the settings alone

private:
    void timerCallback() override;
    juce::RectangleList<int> spotAreas() const;
    juce::Rectangle<int> cardArea() const;
    juce::TextLayout bodyLayout (float width) const;

    std::vector<Step> steps;
    int step = 0;
    bool doneShown = false;
    juce::TextButton back { "BACK" }, next { "NEXT" }, skip { "SKIP TOUR" };
};
} // namespace silt::ui
