#pragma once

#include <vector>

namespace silt
{

// A stereo sample the grains read from: a capture or a loaded file.
// Built and freed off the audio thread; read-only once handed to the engine.
// Mono material is stored twice so the read path never branches on channels.
class SampleBuffer
{
public:
    SampleBuffer (int numFrames, double sampleRate);

    int getNumFrames() const noexcept { return numFrames; }
    double getSampleRate() const noexcept { return sampleRate; }

    float* getWritePointer (int channel) noexcept { return data[(size_t) channel].data(); }
    const float* getReadPointer (int channel) const noexcept { return data[(size_t) channel].data(); }

private:
    int numFrames;
    double sampleRate;
    std::vector<float> data[2];
};

} // namespace silt
