#include <silt/Onsets.h>
#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace silt
{

namespace
{
constexpr int fftSize = 1024, hop = 256;

// Where a sharp attack near frame k begins: the steepest rise of a 64-sample
// envelope around the frame, walked back while the level is still climbing.
// −1 if there's no sharp attack (the level doesn't at least double).
int attackStart (const std::vector<float>& mono, int k)
{
    constexpr int block = 64;
    const int from = std::max (0, k * hop - hop);
    const int to = std::min ((int) mono.size(), k * hop + fftSize);
    std::vector<float> envelope;
    for (int i = from; i + block <= to; i += block)
    {
        float peak = 0.0f;
        for (int j = i; j < i + block; ++j)
            peak = std::max (peak, std::abs (mono[(size_t) j]));
        envelope.push_back (peak);
    }
    if (envelope.size() < 2)
        return k * hop;

    size_t steepest = 1;
    for (size_t b = 1; b < envelope.size(); ++b)
        if (envelope[b] - envelope[b - 1] > envelope[steepest] - envelope[steepest - 1])
            steepest = b;
    const float biggestRise = envelope[steepest] - envelope[steepest - 1];
    // Only a sharp attack counts: the level at least doubles over the rise.
    if (envelope[steepest] < 2.0f * envelope[steepest - 1] + 1.0e-6f)
        return -1;
    while (steepest > 1 && envelope[steepest - 1] - envelope[steepest - 2] > 0.1f * biggestRise)
        --steepest;

    // Within that block, the first sample past a quarter of the way up.
    const int start = from + (int) steepest * block;
    const float before = envelope[steepest - 1];
    const float threshold = before + 0.25f * (envelope[steepest] - before);
    for (int i = start; i < start + block; ++i)
        if (std::abs (mono[(size_t) i]) >= threshold)
            return i;
    return start;
}
} // namespace

std::vector<int> findOnsets (const std::vector<float>& mono, double sampleRate)
{
    std::vector<int> onsets { 0 };
    const int numFrames = (int) mono.size();
    const int frames = numFrames >= fftSize ? (numFrames - fftSize) / hop + 1 : 0;
    if (frames < 3)
        return onsets;

    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) fftSize);

    // Spectral flux on log-compressed magnitudes, measured against each bin's
    // recent peak (held, decaying about 10 dB per 200 ms) rather than the last
    // frame, and maximum-filtered over ±1 bin (after SuperFlux, Böck & Widmer
    // 2013). A decaying note never beats its peak, and overtones of overlapping
    // notes that beat (fade and return) only climb back to it; a new note's
    // partials go above anything recent. Quiet frames (below −55 dB) count as silence.
    const size_t bins = fftSize / 2;
    constexpr float holdDecay = 0.97f;   // per hop
    std::vector<float> flux ((size_t) frames, 0.0f), magnitude (bins), held (bins, 0.0f);
    std::vector<std::complex<float>> spectrum ((size_t) fftSize);
    const auto compress = [] (float m) { return std::log1p (100.0f * m); };
    for (int k = 0; k < frames; ++k)
    {
        double energy = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float s = mono[(size_t) (k * hop + i)];
            energy += (double) s * s;
            spectrum[(size_t) i] = { s * window[(size_t) i], 0.0f };
        }
        const bool audible = 10.0 * std::log10 (energy / fftSize + 1.0e-12) > -55.0;
        fft (spectrum);
        for (size_t b = 0; b < bins; ++b)
            magnitude[b] = audible ? std::abs (spectrum[b]) / (float) fftSize : 0.0f;

        float sum = 0.0f;
        for (size_t b = 1; b + 1 < bins; ++b)
        {
            const float reference = std::max ({ held[b - 1], held[b], held[b + 1] });
            sum += std::max (0.0f, compress (magnitude[b]) - compress (reference));
        }
        flux[(size_t) k] = k > 0 ? sum : 0.0f;
        for (size_t b = 0; b < bins; ++b)
            held[b] = std::max (held[b] * holdDecay, magnitude[b]);
    }

    // Adaptive threshold: a peak must clear 1.5× the local average (±10 frames,
    // about ±50 ms) plus 15 % of the loudest flux, and be the largest within ±3
    // frames. (25 % missed quieter kicks; 10 % let beating overtones through.)
    const float loudest = *std::max_element (flux.begin(), flux.end());
    const int minSpacing = (int) (0.05 * sampleRate);
    for (int k = 1; k < frames; ++k)
    {
        double local = 0.0;
        int count = 0;
        for (int j = std::max (0, k - 10); j <= std::min (frames - 1, k + 10); ++j, ++count)
            local += flux[(size_t) j];
        const float threshold = 1.5f * (float) (local / count) + 0.15f * loudest;
        if (flux[(size_t) k] <= threshold)
            continue;
        bool largest = true;
        for (int j = std::max (0, k - 3); j <= std::min (frames - 1, k + 3) && largest; ++j)
            largest = j == k || flux[(size_t) j] < flux[(size_t) k] || (flux[(size_t) j] == flux[(size_t) k] && j > k);
        if (! largest)
            continue;

        // When it starts: the flux's rising edge (the earliest frame within the
        // last 6 that already shows a third of this peak) puts the new sound in the
        // back part of that frame's window. A sharp attack in the waveform at or
        // just before that (drums, plucks) pins it down exactly; on overlapping
        // notes a "sharp" rise found later is partway up the attack, so the edge
        // estimate stands.
        int first = k;
        while (first > std::max (1, k - 6) && flux[(size_t) first - 1] > flux[(size_t) k] / 3.0f)
            --first;
        int onset = std::min (numFrames - 1, first * hop + (int) (0.6f * fftSize));
        if (const int sharp = attackStart (mono, k);
            sharp >= 0 && sharp >= onset - (int) (0.012 * sampleRate) && sharp <= onset + (int) (0.003 * sampleRate))
            onset = sharp;   // only an attack at or just before the edge: later ones are mid-attack
        // Back to the zero crossing before it: at most 2 ms, and stopping at true
        // silence (where nothing crosses and the walk would never end).
        const int earliest = std::max (1, onset - (int) (0.002 * sampleRate));
        while (onset > earliest && mono[(size_t) onset - 1] != 0.0f
               && (mono[(size_t) onset - 1] < 0.0f) == (mono[(size_t) onset] < 0.0f))
            --onset;

        if (onset - onsets.back() >= minSpacing)
            onsets.push_back (onset);
    }
    return onsets;
}

} // namespace silt
