#include <silt/Pitches.h>
#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace silt
{

std::vector<SampleMode> findModes (const std::vector<float>& mono, double sampleRate, int maxModes)
{
    constexpr int size = 8192, maxFrames = 32;
    const int frames = mono.size() >= (size_t) size ? std::min (maxFrames, 1 + (int) (mono.size() - size) / (size / 2)) : 0;
    if (frames == 0 || maxModes <= 0)
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
    constexpr int reach = 4;   // a Hann window's main lobe and first sidelobe, in bins
    std::vector<SampleMode> peaks;
    const double binHz = sampleRate / size;
    const int lo = std::max (reach + 1, (int) (40.0 / binHz)), hi = std::min (size / 2 - reach - 2, (int) (16000.0 / binHz));
    for (int k = lo; k <= hi; ++k)
    {
        const float b = magnitude[(size_t) k];
        bool highest = b > 0.0f;
        for (int d = -reach; d <= reach && highest; ++d)
            highest = d == 0 || magnitude[(size_t) (k + d)] <= b;
        if (highest)
        {
            const float a = magnitude[(size_t) k - 1], c = magnitude[(size_t) k + 1];
            const double curve = (double) a - 2.0 * b + c;   // negative at a peak
            const double offset = curve < -1.0e-12 ? 0.5 * (a - c) / curve : 0.0;
            peaks.push_back ({ (k + std::clamp (offset, -0.5, 0.5)) * binHz, b });
        }
    }
    std::sort (peaks.begin(), peaks.end(), [] (const SampleMode& x, const SampleMode& y) { return x.level > y.level; });

    std::vector<SampleMode> kept;
    const float loudest = peaks.empty() ? 1.0f : peaks.front().level;
    for (const auto& p : peaks)
    {
        if (p.level < loudest * 0.001f || (int) kept.size() >= maxModes)   // -60 dB
            break;
        const bool clear = std::none_of (kept.begin(), kept.end(), [&] (const SampleMode& m) { return std::abs (12.0 * std::log2 (p.hz / m.hz)) < 0.3; });
        if (clear)
            kept.push_back ({ p.hz, p.level / loudest });
    }
    std::sort (kept.begin(), kept.end(), [] (const SampleMode& x, const SampleMode& y) { return x.hz < y.hz; });
    return kept;
}

} // namespace silt
