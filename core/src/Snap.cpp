#include <silt/Snap.h>

#include <algorithm>
#include <cmath>

namespace silt
{

double nearestPoint (double frame, const std::vector<int>& points) noexcept
{
    if (points.empty())
        return frame;
    const auto after = std::lower_bound (points.begin(), points.end(), frame,
                                         [] (int point, double f) { return point < f; });
    if (after == points.begin())
        return *after;
    if (after == points.end())
        return points.back();
    const auto before = after - 1;
    return frame - *before <= *after - frame ? *before : *after;
}

SnappedRegion snapRegion (double begin, double end, int numFrames, Snap snap,
                          const std::vector<int>& zeroCrossings, const std::vector<int>& transients,
                          double minFrames) noexcept
{
    if (snap == Snap::off)
        return { begin, end };

    if (snap == Snap::zeroCrossing)
    {
        const double b = nearestPoint (begin, zeroCrossings);
        const double e = nearestPoint (end, zeroCrossings);
        return { b, e - b >= minFrames ? e : end };
    }

    // Transients: the start to the nearest one; the end to the nearest one after
    // the start (the sample's end counts as one).
    const double b = transients.empty() ? begin : nearestPoint (begin, transients);
    double e = numFrames, bestDistance = std::abs (numFrames - end);
    for (int t : transients)
        if (t >= b + minFrames && std::abs (t - end) < bestDistance)
        {
            e = t;
            bestDistance = std::abs (t - end);
        }
    return { b, e };
}

} // namespace silt
