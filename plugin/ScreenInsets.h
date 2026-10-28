#pragma once

#include "ModPanel.h"
#include "PluginProcessor.h"

#include <vector>

// The screen's corner picture for the open page: a diagram says "24 dB low
// pass at 591 Hz" faster than three words. FILTER: the response curve.
// RESONATOR: the bands. COLOR: the transfer curve. SPACE: the delay taps then
// the reverb tail. PITCH: the scale and the pattern. MOD: the shown
// modulator's shape. They read the modulated values, so they move with it.
namespace silt::ui
{
bool pageHasInset (int page);

// Draws the inset; false (nothing drawn) for pages without one.
bool drawPageInset (juce::Graphics&, juce::Rectangle<float> area, int page, const SiltProcessor&, const ModRail&);


// What the inset depends on, so the editor repaints it only when that changes.
std::vector<float> insetKey (int page, const SiltProcessor&, const ModRail&);
} // namespace silt::ui
