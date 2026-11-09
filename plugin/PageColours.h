#pragma once

#include "Palette.h"

#include <iterator>

// Each page's colour: one meaning everywhere (its key, the band and labels on the well, its
// module in the chain). They walk from warm for the grains to cool for the effects, and stay
// clear of the modulators' own four. MOD and CHAIN have none, since MOD's colours are the
// modulators'. Never the only cue: the keys and tiles all carry the page's name.
namespace silt::ui
{
inline juce::Colour pageColour (int page)
{
    static constexpr juce::uint32 colours[] { 0xffdb94a0,   // GRAIN, rose
                                              0xffe39a70,   // SHAPE, clay
                                              0xffdcc98a,   // PITCH, straw
                                              0xffa9c27a,   // PLAY, lichen
                                              0xff52c2b0,   // RES, teal
                                              0xff6fb3dc,   // FILTER, sky
                                              0xff93a3e6,   // COLOR, periwinkle
                                              0xffc497cf }; // SPACE, orchid
    if (page < 0 || page >= (int) std::size (colours))
        return ink;
    const juce::Colour colour (colours[page]);
    // Light panels take the same hue, dark enough to read as text.
    return chassis.getPerceivedBrightness() < 0.5f ? colour : colour.darker (1.5f);
}
} // namespace silt::ui
