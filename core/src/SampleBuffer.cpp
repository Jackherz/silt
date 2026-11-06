#include <silt/SampleBuffer.h>
#include <silt/Onsets.h>
#include <silt/Pitches.h>

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

// Zero crossings: every sign change of the mono sum. Transients: note and hit
// onsets, by spectral flux (findOnsets).
void SampleBuffer::analyse()
{
    std::vector<float> mono ((size_t) numFrames);
    for (int i = 0; i < numFrames; ++i)
        mono[(size_t) i] = 0.5f * (data[0][(size_t) i] + data[1][(size_t) i]);

    zeroCrossings.clear();
    for (int i = 1; i < numFrames; ++i)
        if ((mono[(size_t) i - 1] < 0.0f) != (mono[(size_t) i] < 0.0f))
            zeroCrossings.push_back (i);

    transients = findOnsets (mono, sampleRate);
    modes = findModes (mono, sampleRate, 128);
}

} // namespace silt
