#pragma once

#include <vector>

namespace silt
{

// Where region edges snap to (after Ableton Simpler): nowhere, the nearest zero
// crossing (click-free loops), or the nearest transient (slice-like regions).
enum class Snap { off, zeroCrossing, transient };

// The nearest of a sorted list of frames, or frame itself if the list is empty.
double nearestPoint (double frame, const std::vector<int>& points) noexcept;

struct SnappedRegion { double begin, end; };

// A region [begin, end) of a numFrames sample with its edges snapped. Zero
// crossings: both edges to the nearest crossing. Transients: the start to the
// nearest transient, the end to the nearest transient after it (or the sample's
// end). An edge that would make the region shorter than minFrames stays put.
SnappedRegion snapRegion (double begin, double end, int numFrames, Snap,
                          const std::vector<int>& zeroCrossings, const std::vector<int>& transients,
                          double minFrames = 64.0) noexcept;

} // namespace silt
