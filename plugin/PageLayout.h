#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// The panel's pages, one table for both the editor (which builds its controls
// from it) and the 8-knob controller (SiltProcessor::slotParameter), so a knob
// always moves the control drawn in its slot.
//
// Each entry is a control in a page's slot, or a small control under a slot
// (SYNC, mode menus) that the controller doesn't reach. Several entries can
// share a slot: `shown` picks the one in use from the current parameter values.
namespace silt::layout
{
enum Page { grain, shape, pitch, play, resonator, filter, color, space, mod, chain, numPages };
const char* pageName (int page);

enum class Kind
{
    knob, menu, button, action, banner,   // in a slot
    underButton, underMenu                // under a slot, mouse only
};

// What `shown` can look at.
struct Context
{
    std::function<float (const juce::String& id)> value;
    int modShown = 0;
    int resonatorHalf = 0;   // which of RESONATOR's pages of controls: 0 to 3
    bool delayForced = false;   // the grains reach further back than DELAY asks
    float v (const juce::String& id) const { return value (id); }
};

// When an entry shows (or greys out), as data: the editor and the controller
// evaluate it, and the browser version reads the same rules from layout.json.
// A parameter id starting "@" is the shown modulator's ("@Type": m1Type for
// modulator 1).
struct Rule
{
    enum class Op { none, all, any, negate, is, in, on, off, atLeast, modShown, resonatorHalf, delayForced };
    Op op = Op::none;          // none: no rule (always shown, never dimmed)
    juce::String id;           // is, in, on, off, atLeast
    std::vector<int> values;   // is (one), in (any of), modShown and resonatorHalf (one)
    float threshold = 0.0f;    // atLeast
    std::vector<Rule> parts;   // all, any, negate (one)

    bool operator() (const Context&) const;
    bool isNone() const noexcept { return op == Op::none; }
};
inline bool operator== (const Rule& r, std::nullptr_t) noexcept { return r.isNone(); }

struct Entry
{
    int page = 0, slot = 0;
    Kind kind = Kind::knob;
    juce::String id;           // parameter id; "!name" for an action or the banner
    juce::String label;        // above the slot
    juce::StringArray items;   // menus
    juce::String text;         // button text
    int width = 0;             // under-slot controls
    Rule shown;                                   // none: always
    Rule dimmed;                                  // shown, but has no effect in the current state
    int dx = 0;                                   // under-slot controls: offset from the slot's centre
    bool alwaysShown = false;                     // on the panel beside the screen, whatever the page; the controller still finds it in its slot

    bool inSlot() const { return kind != Kind::underButton && kind != Kind::underMenu; }
    bool isShown (const Context& c) const { return shown == nullptr || shown (c); }
};

const std::vector<Entry>& entries (bool instrument);

// The id the controller's knob moves in a page's slot; "" when the slot is empty.
juce::String slotParameter (bool instrument, int page, int slot, const Context&);
} // namespace silt::layout
