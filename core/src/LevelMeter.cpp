#include <silt/LevelMeter.h>

#include <algorithm>
#include <cmath>

namespace silt
{

void LevelMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    float blockPeak = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            blockPeak = std::max (blockPeak, std::abs (channels[ch][i]));

    // A takePeak() landing between the load and the store loses one block's
    // peak, which a meter refreshed at 30 Hz never shows.
    if (blockPeak > peak.load (std::memory_order_relaxed))
        peak.store (blockPeak, std::memory_order_relaxed);
}

} // namespace silt
