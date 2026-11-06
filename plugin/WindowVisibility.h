#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace silt::ui
{
// Whether anyone can see this component's window: not closed, minimised, on another
// Space or completely covered. A component without a window of its own (offscreen,
// in a test) counts as seen.
#if JUCE_MAC
bool windowIsSeen (juce::Component&);
#else
inline bool windowIsSeen (juce::Component& component) { return component.getPeer() == nullptr || component.isShowing(); }
#endif
}
