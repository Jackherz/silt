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
enum Page { grain, shape, pitch, play, resonator, filter, color, space, mod, numPages };
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
    float v (const juce::String& id) const { return value (id); }
};

struct Entry
{
    int page = 0, slot = 0;
    Kind kind = Kind::knob;
    juce::String id;           // parameter id; "!name" for an action or the banner
    juce::String label;        // above the slot
    juce::StringArray items;   // menus
    juce::String text;         // button text
    int width = 0;             // under-slot controls
    std::function<bool (const Context&)> shown;   // null: always
    std::function<bool (const Context&)> dimmed;  // shown, but has no effect in the current state

    bool inSlot() const { return kind != Kind::underButton && kind != Kind::underMenu; }
    bool isShown (const Context& c) const { return shown == nullptr || shown (c); }
};

const std::vector<Entry>& entries (bool instrument);

// The id the controller's knob moves in a page's slot; "" when the slot is empty.
juce::String slotParameter (bool instrument, int page, int slot, const Context&);
} // namespace silt::layout
