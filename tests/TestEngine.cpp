#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

#include <cmath>

using namespace silt;
using namespace silt::test;

namespace
{
// An engine prepared at the test rate, seeded and holding a buffer.
struct TestEngine : Engine
{
    explicit TestEngine (std::unique_ptr<SampleBuffer> buffer, std::uint64_t seed = 1)
    {
        prepare (sampleRate);
        setSeed (seed);
        setBuffer (std::move (buffer));
    }
};
} // namespace

TEST_CASE ("Silent with no note, and with no buffer")
{
    Engine engine;
    engine.prepare (sampleRate);
    const auto none = render (engine, {}, 4800, { { 0, 60, true } });
    CHECK (peak (none.left) == 0.0f);

    TestEngine withBuffer (makeSine());
    const auto idle = render (withBuffer, {}, 4800, {});
    CHECK (peak (idle.left) == 0.0f);
}

TEST_CASE ("A held note sounds, and falls silent after its release")
{
    TestEngine engine (makeSine());
    GrainParams p;
    p.releaseMs = 200.0;
    const auto out = render (engine, p, 48000, { { 0, 60, true }, { 24000, 60, false } });

    CHECK (peak ({ out.left.begin() + 4800, out.left.begin() + 24000 }) > 0.1f);
    // Release is 200 ms; after that the voice is idle and writes zeros.
    CHECK (peak ({ out.left.begin() + 24000 + 12000, out.left.end() }) == 0.0f);
    CHECK (engine.getActiveGrainCount() == 0);
}

TEST_CASE ("Output is click-free across the grain controls")
{
    // Peak of the 240 Hz test sine's second difference is ~0.0006; at +24 st
    // about 0.01. A step of 5% of full scale would show up as a click.
    constexpr float clickThreshold = 0.05f;

    for (double pitch : { -12.0, 0.0, 12.0, 24.0 })
        for (double size : { 10.0, 50.0, 200.0, 1000.0 })
            for (double rate : { 5.0, 20.0, 80.0 })
                for (double spray : { 0.0, 0.3 })
                    for (double reverse : { 0.0, 0.5 })
                    {
                        TestEngine engine (makeSine());
                        GrainParams p;
                        p.pitchSemitones = pitch;
                        p.sizeMs = size;
                        p.rateHz = rate;
                        p.spray = spray;
                        p.reverseChance = reverse;

                        const auto out = render (engine, p, 48000, { { 0, 60, true }, { 36000, 60, false } });

                        INFO ("pitch " << pitch << " size " << size << " rate " << rate
                                       << " spray " << spray << " reverse " << reverse);
                        CHECK (maxSecondDifference (out.left) < clickThreshold);
                        CHECK (maxSecondDifference (out.right) < clickThreshold);
                        CHECK (peak (out.left) < 1.5f);
                    }
}

TEST_CASE ("The click detector catches a real click")
{
    std::vector<float> x (1000);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.5f * (float) std::sin (0.03 * (double) i) + (i >= 500 ? 0.1f : 0.0f);
    CHECK (maxSecondDifference (x) > 0.05f);
}

TEST_CASE ("Grains never read across the buffer seam")
{
    // 1.003 s of 220 Hz doesn't end on a whole period, so the seam between the
    // last and first frame is a step. Grains near the end must stay clear of it.
    auto buffer = std::make_unique<SampleBuffer> ((int) (1.003 * sampleRate), sampleRate);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer->getNumFrames(); ++i)
            buffer->getWritePointer (ch)[i] = 0.5f * (float) std::sin (6.283185307179586 * 220.0 * i / sampleRate);

    TestEngine engine (std::move (buffer));
    GrainParams p;
    p.start = 0.95;
    p.sizeMs = 200.0;
    p.reverseChance = 0.5;
    p.pitchSemitones = 12.0;
    const auto out = render (engine, p, 48000, { { 0, 60, true } });
    CHECK (maxSecondDifference (out.left) < 0.05f);
}

TEST_CASE ("Chords use separate voices, and a 17th note steals one")
{
    TestEngine engine (makeSine());
    GrainParams p;
    std::vector<TimedNote> notes;
    for (int i = 0; i < 17; ++i)
        notes.push_back ({ i * 100, 48 + i, true });
    render (engine, p, 4800, notes);

    int sounding = 0;
    for (int i = 0; i < Engine::numVoices; ++i)
        sounding += engine.getVoice (i).isActive() ? 1 : 0;
    CHECK (sounding == Engine::numVoices);

    // The first note was stolen by the 17th.
    bool firstStillHeld = false;
    for (int i = 0; i < Engine::numVoices; ++i)
        firstStillHeld |= engine.getVoice (i).getCurrentNote() == 48;
    CHECK_FALSE (firstStillHeld);
}

TEST_CASE ("ADSR settles at the sustain level")
{
    GrainParams p;
    p.sizeMs = 200.0;
    p.decayMs = 50.0;

    const auto levelAt = [&p] (double sustain)
    {
        TestEngine engine (makeSine());
        p.sustain = sustain;
        const auto out = render (engine, p, 48000, { { 0, 60, true } });
        return peak ({ out.left.begin() + 24000, out.left.end() });
    };

    const float full = levelAt (1.0);
    const float half = levelAt (0.5);
    CHECK (half / full > 0.45f);
    CHECK (half / full < 0.55f);
}

TEST_CASE ("A looping playhead stays inside its region")
{
    TestEngine engine (makeSine (240.0, 2.0));
    GrainParams p;
    p.start = 0.5;
    p.length = 0.1;   // 0.2 s loop starting 1 s in
    render (engine, p, 96000, { { 0, 60, true } });

    const float pos = engine.getVoice (0).getPlayheadPosition();
    CHECK (pos >= 0.499f);   // the region starts at 0.5 × the last frame's index
    CHECK (pos < 0.6f);
}

TEST_CASE ("Same seed, same output; different seed, different output")
{
    GrainParams p;
    p.spray = 0.3;
    p.reverseChance = 0.5;

    TestEngine a (makeNoise(), 11);
    TestEngine b (makeNoise(), 11);
    TestEngine c (makeNoise(), 12);
    const std::vector<TimedNote> notes { { 0, 64, true } };

    const auto outA = render (a, p, 24000, notes);
    const auto outB = render (b, p, 24000, notes);
    const auto outC = render (c, p, 24000, notes);

    CHECK (outA.left == outB.left);
    CHECK (outA.right == outB.right);
    CHECK (outA.left != outC.left);
}

TEST_CASE ("Ten minutes of noise: no NaN, infinity or denormals")
{
    TestEngine engine (makeNoise());
    GrainParams p;
    p.pitchSemitones = 7.0;
    p.sizeMs = 150.0;
    p.rateHz = 40.0;
    p.spray = 0.5;
    p.reverseChance = 0.5;
    p.scan = 0.7;

    const int total = (int) (600 * sampleRate);
    std::vector<float> left (512), right (512);
    NoteEvent on { 0, 60, 1.0f, true };
    bool bad = false;

    for (int done = 0; done < total && ! bad; done += 512)
    {
        engine.process (left.data(), right.data(), 512, done == 0 ? &on : nullptr, done == 0 ? 1 : 0, p);
        for (int i = 0; i < 512; ++i)
            for (float v : { left[(size_t) i], right[(size_t) i] })
                bad |= ! std::isfinite (v) || std::fpclassify (v) == FP_SUBNORMAL;
    }
    CHECK_FALSE (bad);
}

TEST_CASE ("A stop playhead ends the note at the end of the sample")
{
    TestEngine engine (makeSine (240.0, 1.0));
    GrainParams p;
    p.start = 0.9;
    p.endMode = EndMode::stop;

    // 0.1 s to the end, 0.2 s release, then silence, with the key still held.
    const auto out = render (engine, p, 48000, { { 0, 60, true } });
    CHECK (peak ({ out.left.begin(), out.left.begin() + 4800 }) > 0.1f);
    CHECK (peak ({ out.left.begin() + 24000, out.left.end() }) == 0.0f);
}

TEST_CASE ("Swapping the buffer mid-note is click-free, and old buffers are retired")
{
    TestEngine engine (makeSine (240.0));
    GrainParams p;
    p.sizeMs = 200.0;

    // Note held; a 300 Hz sine (also a whole number of periods) arrives mid-note.
    auto first = render (engine, p, 24000, { { 0, 60, true } });
    auto second = makeSine (300.0);
    const auto* secondPtr = second.get();
    engine.setBuffer (std::move (second));
    const auto swap = render (engine, p, 24000, {});

    CHECK (engine.getCurrentBuffer() == secondPtr);
    first.left.insert (first.left.end(), swap.left.begin(), swap.left.end());
    CHECK (maxSecondDifference (first.left) < 0.05f);

    // The first buffer has been retired (grains finished). A third buffer swaps
    // in, but a fourth must wait until the message thread frees the retired one.
    auto third = makeSine (240.0);
    const auto* thirdPtr = third.get();
    engine.setBuffer (std::move (third));
    render (engine, p, 24000, {});
    CHECK (engine.getCurrentBuffer() == thirdPtr);

    auto fourth = makeSine (300.0);
    const auto* fourthPtr = fourth.get();
    engine.setBuffer (std::move (fourth));
    render (engine, p, 24000, {});
    CHECK (engine.getCurrentBuffer() == thirdPtr);

    engine.collectGarbage();
    render (engine, p, 24000, {});
    CHECK (engine.getCurrentBuffer() == fourthPtr);
}

TEST_CASE ("Keys transpose around the root note")
{
    // Zero-crossing count over a held note: an octave up doubles it.
    const auto crossings = [] (int note)
    {
        TestEngine engine (makeSine());
        GrainParams p;
        p.sizeMs = 200.0;
        const auto out = render (engine, p, 48000, { { 0, note, true } });
        int count = 0;
        for (size_t i = 9601; i < out.left.size(); ++i)
            count += (out.left[i - 1] < 0.0f) != (out.left[i] < 0.0f);
        return count;
    };

    const int root = crossings (60);
    const int octave = crossings (72);
    CHECK (std::abs (octave - 2 * root) < root / 20);
}
