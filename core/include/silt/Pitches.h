#pragma once

#include <vector>

namespace silt
{

// One of a sample's spectral peaks: where it sits, and how strong against the strongest (0...1).
struct SampleMode
{
    double hz = 0.0;
    float level = 0.0f;
};

// The sample's own modes, for the resonator's NOTES: SAMPLE: the magnitude spectrum averaged
// over frames spread through the sample, its peaks between 40 Hz and 16 kHz (placed between
// bins by a parabola), the loudest first, kept if within 60 dB of the loudest and at least 0.3
// of a semitone from every one kept already. A peak must top the bins either side of it, or a
// strong partial's window sidelobes would count. Returned low to high, at most maxModes.
// Runs off the audio thread.
std::vector<SampleMode> findModes (const std::vector<float>& mono, double sampleRate, int maxModes);

} // namespace silt
