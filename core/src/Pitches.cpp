#include <silt/Pitches.h>
#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace silt
{

std::vector<double> findPitches (const std::vector<float>& mono, double sampleRate, int maxPitches)
{
    constexpr int size = 8192, maxFrames = 32;
    const int frames = mono.size() >= (size_t) size ? std::min (maxFrames, 1 + (int) (mono.size() - size) / (size / 2)) : 0;
    if (frames == 0 || maxPitches <= 0)
        return {};

    // The average magnitude spectrum, Hann-windowed frames spread evenly.
    std::vector<float> magnitude (size / 2, 0.0f);
    std::vector<std::complex<float>> buffer (size);
    const double spacing = frames > 1 ? (double) (mono.size() - size) / (frames - 1) : 0.0;
    for (int f = 0; f < frames; ++f)
    {
        const size_t start = (size_t) std::lround (f * spacing);
        for (int i = 0; i < size; ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) size);
            buffer[(size_t) i] = { mono[start + (size_t) i] * window, 0.0f };
        }
        fft (buffer);
        for (int k = 0; k < size / 2; ++k)
            magnitude[(size_t) k] += std::abs (buffer[(size_t) k]);
    }

    // Peaks, loudest first.
    struct Peak { double hz; float level; };
    std::vector<Peak> peaks;
    const double binHz = sampleRate / size;
    const int lo = std::max (2, (int) (55.0 / binHz)), hi = std::min (size / 2 - 2, (int) (4000.0 / binHz));
    for (int k = lo; k <= hi; ++k)
    {
        const float a = magnitude[(size_t) k - 1], b = magnitude[(size_t) k], c = magnitude[(size_t) k + 1];
        if (b > a && b >= c)
        {
            const double curve = (double) a - 2.0 * b + c;   // negative at a peak
            const double offset = curve < -1.0e-12 ? 0.5 * (a - c) / curve : 0.0;
            peaks.push_back ({ (k + std::clamp (offset, -0.5, 0.5)) * binHz, b });
        }
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.level > y.level; });

    std::vector<double> kept;
    const float floor = peaks.empty() ? 0.0f : peaks.front().level * 0.0316f;   // −30 dB
    for (const auto& p : peaks)
    {
        if (p.level < floor || (int) kept.size() >= maxPitches)
            break;
        const bool clear = std::none_of (kept.begin(), kept.end(), [&] (double h) { return std::abs (12.0 * std::log2 (p.hz / h)) < 0.6; });
        if (clear)
            kept.push_back (p.hz);
    }
    std::sort (kept.begin(), kept.end());
    return kept;
}

} // namespace silt
