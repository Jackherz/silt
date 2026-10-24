#include <silt/SampleBuffer.h>

#include <algorithm>

namespace silt
{

SampleBuffer::SampleBuffer (int frames, double rate)
    : numFrames (std::max (1, frames)), sampleRate (rate)
{
    for (auto& channel : data)
        channel.assign ((size_t) numFrames, 0.0f);
}

} // namespace silt
