#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The first-run tour: a few cards, each pointing at one part of the panel.
// Everything else dims; the part being explained stays lit. NEXT, BACK and
// SKIP (or the arrow keys, Return and Esc) move through it. It opens by itself
// the first time Silt is opened on a computer, and from the ? button after.
namespace silt::ui
{
class Tour final : public juce::Component
{
public:
    struct Step
    {
        juce::String title, body;
        std::function<juce::Rectangle<int>()> spot;   // in the parent's space; null: no spot, card centred
        int page = -1;                                // a page to show first, or −1
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

    // Has this computer seen the tour? Kept in Silt's settings file, not the session.
    static bool seen();
    static void markSeen();
    static inline bool remember = true;   // tests and the snapshot tool leave the settings alone

private:
    juce::Rectangle<int> spotArea() const;
    juce::Rectangle<int> cardArea() const;
    juce::TextLayout bodyLayout (float width) const;

    std::vector<Step> steps;
    int step = 0;
    juce::TextButton back { "BACK" }, next { "NEXT" }, skip { "SKIP TOUR" };
};
} // namespace silt::ui
