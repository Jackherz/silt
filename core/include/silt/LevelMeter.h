#pragma once

#include <atomic>

namespace silt
{

// Peak level of incoming audio, held until the UI reads it.
// process() runs on the audio thread; takePeak() on any other thread.
class LevelMeter
{
public:
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    // Returns the highest linear peak since the last call, then resets it.
    float takePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }

    void reset() noexcept { peak.store (0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> peak { 0.0f };
};

} // namespace silt
