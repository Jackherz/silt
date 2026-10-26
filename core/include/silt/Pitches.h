#pragma once

#include <vector>

namespace silt
{

// The sample's strongest pitches, for the resonator's NOTES: SAMPLE: the
// magnitude spectrum averaged over frames spread through the sample, its
// peaks between 55 Hz and 4 kHz (placed between bins by a parabola), the
// loudest first, kept if within 30 dB of the loudest and at least 0.6 of a
// semitone from every one kept already. Returned low to high, at most
// maxPitches. Runs off the audio thread.
std::vector<double> findPitches (const std::vector<float>& mono, double sampleRate, int maxPitches);

} // namespace silt
