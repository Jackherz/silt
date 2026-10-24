#pragma once

#include <silt/Engine.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace silt::test
{

constexpr double sampleRate = 48000.0;

// 240 Hz has a whole number of periods in any whole number of seconds at
// 48 kHz, so a looping grain crossing the buffer seam stays continuous.
inline std::unique_ptr<SampleBuffer> makeSine (double freq = 240.0, double seconds = 2.0, float amp = 0.5f)
{
    auto buffer = std::make_unique<SampleBuffer> ((int) (seconds * sampleRate), sampleRate);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer->getNumFrames(); ++i)
            buffer->getWritePointer (ch)[i] = amp * (float) std::sin (6.283185307179586 * freq * i / sampleRate);
    return buffer;
}

inline std::unique_ptr<SampleBuffer> makeNoise (double seconds = 4.0, std::uint64_t seed = 7)
{
    auto buffer = std::make_unique<SampleBuffer> ((int) (seconds * sampleRate), sampleRate);
    Random random (seed);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer->getNumFrames(); ++i)
            buffer->getWritePointer (ch)[i] = 0.5f * random.nextBipolar();
    return buffer;
}

struct Rendered
{
    std::vector<float> left, right;
};

// Runs the engine in 128-sample blocks. Events are (sample, note, on).
struct TimedNote { int sample; int note; bool on; };

inline Rendered render (Engine& engine, const GrainParams& params, int totalSamples,
                        const std::vector<TimedNote>& notes, int blockSize = 128)
{
    Rendered out;
    out.left.resize ((size_t) totalSamples);
    out.right.resize ((size_t) totalSamples);

    std::vector<NoteEvent> events;
    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int len = std::min (blockSize, totalSamples - start);
        events.clear();
        for (const auto& n : notes)
            if (n.sample >= start && n.sample < start + len)
                events.push_back ({ n.sample - start, n.note, 1.0f, n.on });

        engine.process (out.left.data() + start, out.right.data() + start, len,
                        events.data(), (int) events.size(), params);
    }
    return out;
}

inline float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max (p, std::abs (v));
    return p;
}

// Largest second difference |x[n] − 2x[n−1] + x[n−2]|. Smooth band-limited
// audio keeps this small; a click (a step in the waveform) makes it jump to
// the size of the step.
inline float maxSecondDifference (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 2; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
    return m;
}

} // namespace silt::test
