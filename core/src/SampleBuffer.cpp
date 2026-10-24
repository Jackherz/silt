#include <silt/SampleBuffer.h>

#include <algorithm>
#include <cmath>

namespace silt
{

SampleBuffer::SampleBuffer (int frames, double rate)
    : numFrames (std::max (1, frames)), sampleRate (rate)
{
    for (auto& channel : data)
        channel.assign ((size_t) numFrames, 0.0f);
}

// Zero crossings: every sign change of the mono sum. Transients: frames of 512
// (hopping 256) whose level jumps 6 dB or more over the previous frame, above
// −45 dB, at local peaks of that jump and at least 50 ms apart; each placed at
// the first sample reaching a quarter of its frame's peak, then moved back to
// the zero crossing before it. The sample's start always counts as one.
void SampleBuffer::analyse()
{
    std::vector<float> mono ((size_t) numFrames);
    for (int i = 0; i < numFrames; ++i)
        mono[(size_t) i] = 0.5f * (data[0][(size_t) i] + data[1][(size_t) i]);

    zeroCrossings.clear();
    for (int i = 1; i < numFrames; ++i)
        if ((mono[(size_t) i - 1] < 0.0f) != (mono[(size_t) i] < 0.0f))
            zeroCrossings.push_back (i);

    constexpr int hop = 256, window = 512;
    const int frames = std::max (0, (numFrames - window) / hop + 1);
    std::vector<double> levelDb ((size_t) frames);
    for (int k = 0; k < frames; ++k)
    {
        double energy = 0.0;
        for (int i = k * hop; i < k * hop + window; ++i)
            energy += (double) mono[(size_t) i] * mono[(size_t) i];
        levelDb[(size_t) k] = 10.0 * std::log10 (energy / window + 1.0e-10);
    }

    std::vector<double> rise ((size_t) frames, 0.0);
    for (int k = 1; k < frames; ++k)
        rise[(size_t) k] = std::max (0.0, levelDb[(size_t) k] - levelDb[(size_t) k - 1]);

    transients.assign (1, 0);
    const int minSpacing = (int) (0.05 * sampleRate);
    for (int k = 1; k < frames; ++k)
    {
        const double r = rise[(size_t) k];
        const bool peak = r >= rise[(size_t) k - 1] && (k + 1 >= frames || r > rise[(size_t) k + 1]);
        if (! peak || r < 6.0 || levelDb[(size_t) k] < -45.0)
            continue;

        float loudest = 0.0f;
        for (int i = k * hop; i < k * hop + window; ++i)
            loudest = std::max (loudest, std::abs (mono[(size_t) i]));
        int onset = k * hop;
        while (onset < k * hop + window - 1 && std::abs (mono[(size_t) onset]) < 0.25f * loudest)
            ++onset;
        // Back to the zero crossing before it: at most 2 ms, and stopping at true
        // silence (where nothing crosses and the walk would never end).
        const int earliest = std::max (1, onset - (int) (0.002 * sampleRate));
        while (onset > earliest && mono[(size_t) onset - 1] != 0.0f
               && (mono[(size_t) onset - 1] < 0.0f) == (mono[(size_t) onset] < 0.0f))
            --onset;

        if (onset - transients.back() >= minSpacing)
            transients.push_back (onset);
    }
}

} // namespace silt
