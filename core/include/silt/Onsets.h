#pragma once

#include <vector>

namespace silt
{

// Finds note and hit onsets in mono audio, by spectral flux: how much the
// spectrum gets louder from one short frame to the next, summed over
// frequency. A new note on sustained, overlapping keys changes the spectrum
// even when the overall level barely moves, which a level-only detector
// misses. Peaks of the flux that stand out from their surroundings (an
// adaptive threshold) count, at least 50 ms apart. Each is placed where its
// attack begins, then moved back to the zero crossing before it. The result
// always starts with frame 0. Runs off the audio thread.
std::vector<int> findOnsets (const std::vector<float>& mono, double sampleRate);

} // namespace silt
