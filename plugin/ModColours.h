#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

// The four modulators' colours: one palette, one meaning everywhere (rings,
// rail chips, menus, the route list, the header stripes). Colour is never the
// only cue: every place also shows the modulator's number, and each has its
// own ring texture.
namespace silt::ui
{
struct ModColour
{
    juce::Colour base;    // rings, chips, menu items
    juce::Colour dim;     // muted state
    juce::Colour ink;     // text drawn on top of base
    const char* name;
};

inline const std::array<ModColour, 4> modColours { {
    { juce::Colour (0xffa64826), juce::Colour (0xff5c2714), juce::Colour (0xffefe6cf), "RUST" },
    { juce::Colour (0xffd29a2c), juce::Colour (0xff7a5513), juce::Colour (0xff2a2420), "BRASS" },
    { juce::Colour (0xff6f8f4e), juce::Colour (0xff3a4d28), juce::Colour (0xffefe6cf), "MOSS" },
    { juce::Colour (0xff4f62a8), juce::Colour (0xff2a3560), juce::Colour (0xffefe6cf), "COBALT" },
} };

inline const ModColour& modColour (int m) noexcept
{
    return modColours[(size_t) juce::jlimit (0, (int) modColours.size() - 1, m)];
}

// Ring textures: solid, long dash, short dash, dotted (empty = solid).
inline std::vector<float> modDashes (int m)
{
    switch (m)
    {
        case 1:  return { 7.0f, 3.0f };
        case 2:  return { 3.0f, 2.5f };
        case 3:  return { 1.2f, 2.2f };
        default: return {};
    }
}
} // namespace silt::ui
