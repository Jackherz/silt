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

TEST_CASE ("Synced grains start on the beat grid, across block boundaries")
{
    // 120 BPM at 48 kHz: 24000 samples per quarter. 1/16 grid = 6000 samples.
    TestEngine engine (makeSine());
    GrainParams p;
    p.gridSync = true;
    p.ppqPerSample = 1.0 / 24000.0;
    p.gridDivision = 0.25;
    p.rateHz = 8.0;

    // Note at ppq 0.1, two quarters long, in odd-sized blocks with the note mid-block.
    const double startPpq = 0.1;
    const int total = 48000, block = 97;
    std::vector<float> l (block), r (block);
    std::vector<int> onsets;
    std::uint64_t seen = 0;

    for (int done = 0; done < total; done += block)
    {
        const int len = std::min (block, total - done);
        p.gridPpq = startPpq + done * p.ppqPerSample;
        NoteEvent on { 0, 60, 1.0f, true };
        engine.process (l.data(), r.data(), len, done == 0 ? &on : nullptr, done == 0 ? 1 : 0, p);

        // At most one onset per 97-sample block at this spacing.
        if (engine.getVoice (0).getGrainsStarted() != seen)
        {
            seen = engine.getVoice (0).getGrainsStarted();
            onsets.push_back (done);
        }
    }

    // The note's own grain, then every grid line from ppq 0.25 to 2.0: 1 + 8.
    REQUIRE (onsets.size() == 9);
    CHECK (onsets[0] == 0);
    for (size_t i = 1; i < onsets.size(); ++i)
    {
        const int gridSample = (int) std::lround ((0.25 * (double) i - startPpq) * 24000.0);
        CHECK (std::abs (onsets[i] - gridSample) < block);
    }
}

TEST_CASE ("A transport jump re-locks the grid")
{
    TestEngine engine (makeSine());
    GrainParams p;
    p.gridSync = true;
    p.ppqPerSample = 1.0 / 24000.0;
    p.gridDivision = 1.0;

    std::vector<float> l (128), r (128);
    NoteEvent on { 0, 60, 1.0f, true };
    p.gridPpq = 0.5;
    engine.process (l.data(), r.data(), 128, &on, 1, p);
    const auto before = engine.getVoice (0).getGrainsStarted();

    // Jump to just before beat 8: the next grain comes at beat 8, not beat 1.
    p.gridPpq = 8.0 - 64.0 / 24000.0;
    engine.process (l.data(), r.data(), 128, nullptr, 0, p);
    CHECK (engine.getVoice (0).getGrainsStarted() == before + 1);
}

TEST_CASE ("Grains mode holds the grain count whatever the size")
{
    for (int count : { 1, 4, 8, 16 })
        for (double size : { 30.0, 200.0, 900.0 })
        {
            TestEngine engine (makeSine (240.0, 4.0));
            GrainParams p;
            p.grainCount = count;
            p.sizeMs = size;
            p.rateHz = 1.0;   // ignored in Grains mode

            std::vector<float> l (64), r (64);
            NoteEvent on { 0, 60, 1.0f, true };
            int lowest = 1000, highest = 0;
            for (int done = 0; done < 96000; done += 64)
            {
                engine.process (l.data(), r.data(), 64, done == 0 ? &on : nullptr, done == 0 ? 1 : 0, p);
                if (done > 48000)   // after the first second has filled up
                {
                    lowest = std::min (lowest, engine.getActiveGrainCount());
                    highest = std::max (highest, engine.getActiveGrainCount());
                }
            }

            INFO ("count " << count << " size " << size);
            CHECK (highest <= count + 1);
            CHECK (lowest >= count - 1);
        }
}

TEST_CASE ("Jitter breaks up the comb tone of evenly spaced grains")
{
    // 32 grains of 40 ms with a frozen playhead start every 60 samples, all
    // reading the same audio: the sum repeats every 60 samples (a 800 Hz tone).
    const auto correlationAtSpacing = [] (double jitter)
    {
        TestEngine engine (makeNoise (2.0));
        GrainParams p;
        p.grainCount = 32;
        p.sizeMs = 40.0;
        p.scan = 0.0;
        p.jitter = jitter;
        const auto out = render (engine, p, 96000, { { 0, 60, true } });

        const std::vector<float> x (out.left.begin() + 24000, out.left.end());
        constexpr size_t lag = 60;
        double same = 0.0, energy = 0.0;
        for (size_t i = 0; i + lag < x.size(); ++i)
        {
            same += (double) x[i] * x[i + lag];
            energy += (double) x[i] * x[i];
        }
        return same / energy;
    };

    const double clockwork = correlationAtSpacing (0.0);
    const double jittered = correlationAtSpacing (0.5);
    INFO ("clockwork " << clockwork << ", jittered " << jittered);
    CHECK (clockwork > 0.5);
    CHECK (jittered < 0.2);
}

TEST_CASE ("Random pan spreads grains across the stereo field; Spray alone doesn't")
{
    // The test sine is identical in both channels, so any L/R difference is panning.
    const auto stereoDifference = [] (double spray, double randomPan)
    {
        TestEngine engine (makeSine());
        GrainParams p;
        p.spray = spray;
        p.randomPan = randomPan;
        const auto out = render (engine, p, 24000, { { 0, 60, true } });
        float most = 0.0f;
        for (size_t i = 0; i < out.left.size(); ++i)
            most = std::max (most, std::abs (out.left[i] - out.right[i]));
        return most;
    };

    CHECK (stereoDifference (0.5, 0.0) == 0.0f);
    CHECK (stereoDifference (0.0, 1.0) > 0.05f);
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

TEST_CASE ("Grains only read inside the region, whatever their size and spray")
{
    // Loud noise everywhere except a silent stretch around the region.
    auto buffer = makeNoise (2.0);
    const int frames = buffer->getNumFrames();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = (int) (0.45 * frames); i < (int) (0.65 * frames); ++i)
            buffer->getWritePointer (ch)[i] = 0.0f;

    for (double size : { 20.0, 150.0, 1000.0 })
        for (double spray : { 0.0, 0.5, 1.0 })
            for (double pitch : { 0.0, 12.0 })
                for (double reverse : { 0.0, 1.0 })
                {
                    auto copy = std::make_unique<SampleBuffer> (frames, sampleRate);
                    for (int ch = 0; ch < 2; ++ch)
                        std::copy (buffer->getReadPointer (ch), buffer->getReadPointer (ch) + frames, copy->getWritePointer (ch));

                    TestEngine engine (std::move (copy));
                    GrainParams p;
                    p.start = 0.5;
                    p.length = 0.1;
                    p.sizeMs = size;
                    p.spray = spray;
                    p.pitchSemitones = pitch;
                    p.reverseChance = reverse;
                    const auto out = render (engine, p, 48000, { { 0, 60, true } });

                    INFO ("size " << size << " spray " << spray << " pitch " << pitch << " reverse " << reverse);
                    CHECK (peak (out.left) == 0.0f);
                    CHECK (peak (out.right) == 0.0f);
                }
}

TEST_CASE ("Grain starts keep moving through the whole region (no freeze at its end)")
{
    // A 1 s region, 400 ms grains, scan at real time: over one pass the grain
    // starts must keep advancing, including through the region's last 400 ms.
    TestEngine engine (makeSine (240.0, 2.0));
    GrainParams p;
    p.length = 0.5;
    p.sizeMs = 400.0;
    p.rateHz = 20.0;

    std::vector<float> l (128), r (128);
    NoteEvent on { 0, 60, 1.0f, true };
    std::vector<double> starts;
    std::uint64_t seen = 0;
    for (int done = 0; done < 47000; done += 128)
    {
        engine.process (l.data(), r.data(), 128, done == 0 ? &on : nullptr, done == 0 ? 1 : 0, p);
        if (engine.getVoice (0).getGrainsStarted() != seen)
        {
            seen = engine.getVoice (0).getGrainsStarted();
            starts.push_back (engine.getVoice (0).getLastGrainStart());
        }
    }

    REQUIRE (starts.size() >= 18);
    for (size_t i = 1; i < starts.size(); ++i)
        CHECK (starts[i] > starts[i - 1]);
}

TEST_CASE ("A looping region plays at Scan speed and reaches its end before it repeats")
{
    // Silent buffer with loud noise in the last fifth of a 1 s region.
    // At Scan 1 the loud part must sound 0.8–1.0 s into each pass, and the
    // start of the next pass must be quiet again.
    auto buffer = std::make_unique<SampleBuffer> (96000, sampleRate);
    Random random (5);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 38400; i < 48000; ++i)
            buffer->getWritePointer (ch)[i] = 0.5f * random.nextBipolar();

    TestEngine engine (std::move (buffer));
    GrainParams p;
    p.length = 0.5;          // region: the first second
    p.scan = 1.0;
    p.sizeMs = 300.0;
    p.rateHz = 12.0;
    p.fade = 0.5;
    p.slant = 0.0;
    p.ease = 0.0;
    const auto out = render (engine, p, 96000, { { 0, 60, true } });

    const auto rms = [&out] (double from, double to)
    {
        double sum = 0.0;
        const int a = (int) (from * sampleRate), b = (int) (to * sampleRate);
        for (int i = a; i < b; ++i)
            sum += (double) out.left[(size_t) i] * out.left[(size_t) i];
        return std::sqrt (sum / (b - a));
    };

    INFO ("quiet " << rms (0.2, 0.6) << ", loud " << rms (0.85, 0.95) << ", next pass " << rms (1.1, 1.5));
    CHECK (rms (0.85, 0.95) > 0.1);
    CHECK (rms (0.2, 0.6) < 0.01);
    CHECK (rms (1.1, 1.5) < 0.01);
}

TEST_CASE ("A shorter region changes the sound")
{
    GrainParams p;
    p.sizeMs = 1000.0;
    const auto renderWith = [&p] (double length)
    {
        TestEngine engine (makeNoise (2.0));
        p.length = length;
        return render (engine, p, 24000, { { 0, 60, true } });
    };
    CHECK (renderWith (1.0).left != renderWith (0.05).left);
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

// --- Live mode -------------------------------------------------------------------

namespace
{
// Runs a live engine over generated input, 128 samples at a time.
template <typename Input>
Rendered renderLive (Engine& engine, const GrainParams& p, int totalSamples, Input&& input, int freezeFrom = -1)
{
    Rendered out;
    out.left.resize ((size_t) totalSamples);
    out.right.resize ((size_t) totalSamples);
    std::vector<float> in (128);
    for (int start = 0; start < totalSamples; start += 128)
    {
        const int len = std::min (128, totalSamples - start);
        for (int i = 0; i < len; ++i)
            in[(size_t) i] = input (start + i);
        auto block = p;
        block.freeze = freezeFrom >= 0 && start >= freezeFrom;
        engine.process (out.left.data() + start, out.right.data() + start, len,
                        in.data(), in.data(), nullptr, 0, block);
    }
    return out;
}

struct LiveEngine : Engine
{
    LiveEngine() { prepare (sampleRate, 128); setSeed (3); }
};
} // namespace

TEST_CASE ("Live grains never read across the write head")
{
    // Until the ring fills, the audio just behind the write head is a sine and
    // just ahead of it is silence, so any grain crossing the write head clicks.
    for (double pitch : { 0.0, 12.0, 24.0 })
        for (double size : { 50.0, 400.0, 1500.0 })
            for (double delay : { 10.0, 300.0 })
            {
                LiveEngine engine;
                GrainParams p;
                p.live = true;
                p.liveDelayMs = delay;
                p.pitchSemitones = pitch;
                p.sizeMs = size;
                p.spray = 0.3;
                p.reverseChance = 0.5;
                const auto out = renderLive (engine, p, 96000, [] (int i)
                {
                    return 0.5f * (float) std::sin (6.283185307179586 * 230.3 * i / sampleRate);
                });

                // From 0.5 s: before that, 4x grains can reach back to where the
                // test sine itself starts abruptly from silence.
                INFO ("pitch " << pitch << " size " << size << " delay " << delay);
                const std::vector<float> settled (out.left.begin() + 24000, out.left.end());
                CHECK (maxSecondDifference (settled) < 0.05f);
                CHECK (peak (settled) > 0.05f);
            }
}

TEST_CASE ("Full live feedback stays bounded")
{
    LiveEngine engine;
    GrainParams p;
    p.live = true;
    p.feedback = 1.0;
    p.grainCount = 16;
    p.sizeMs = 150.0;
    p.liveDelayMs = 200.0;
    Random noise (9);
    const auto out = renderLive (engine, p, (int) (10 * sampleRate), [&noise] (int) { return 0.5f * noise.nextBipolar(); });

    bool finite = true;
    for (float v : out.left)
        finite &= std::isfinite (v);
    CHECK (finite);
    CHECK (peak (out.left) < 2.0f);
}

TEST_CASE ("Freeze holds the sound after the input stops")
{
    const auto lastSecond = [] (bool freeze)
    {
        LiveEngine engine;
        GrainParams p;
        p.live = true;
        p.liveDelayMs = 300.0;
        const int total = (int) (4 * sampleRate), inputEnds = (int) (2 * sampleRate);
        const auto out = renderLive (engine, p, total, [inputEnds] (int i)
        {
            return i < inputEnds ? 0.5f * (float) std::sin (6.283185307179586 * 240.0 * i / sampleRate) : 0.0f;
        }, freeze ? inputEnds : -1);
        return peak ({ out.left.end() - 48000, out.left.end() });
    };

    CHECK (lastSecond (true) > 0.1f);
    CHECK (lastSecond (false) < 0.01f);
}

TEST_CASE ("With no-notes playing off, live mode waits for notes")
{
    LiveEngine engine;
    GrainParams p;
    p.live = true;
    p.liveTrigger = LiveTrigger::off;
    const auto out = renderLive (engine, p, 48000, [] (int i) { return 0.5f * (float) std::sin (0.03 * i); });
    CHECK (peak (out.left) == 0.0f);
}

TEST_CASE ("Follow: the input's level plays the envelope")
{
    // Bursts: 0.3 s of sine, then 0.7 s of silence, three times. With a short
    // release the grains sound during each burst (after the delay) and stop
    // in the gaps; always-on would keep playing the ring through them.
    const auto run = [] (LiveTrigger trigger)
    {
        LiveEngine engine;
        GrainParams p;
        p.live = true;
        p.liveTrigger = trigger;
        p.liveDelayMs = 50.0;
        p.attackMs = 5.0;
        p.releaseMs = 50.0;
        return renderLive (engine, p, 3 * 48000, [] (int i)
        {
            const bool on = (i % 48000) < 14400;
            return on ? 0.5f * (float) std::sin (6.283185307179586 * 240.0 * i / sampleRate) : 0.0f;
        });
    };

    const auto rms = [] (const Rendered& out, double from, double to)
    {
        double sum = 0.0;
        for (int i = (int) (from * sampleRate); i < (int) (to * sampleRate); ++i)
            sum += (double) out.left[(size_t) i] * out.left[(size_t) i];
        return std::sqrt (sum / ((to - from) * sampleRate));
    };

    const auto follow = run (LiveTrigger::follow);
    CHECK (rms (follow, 1.1, 1.3) > 0.05);     // during the second burst
    CHECK (rms (follow, 1.6, 1.95) < 0.005);   // in the gap after it

    const auto always = run (LiveTrigger::always);
    CHECK (rms (always, 1.1, 1.3) > 0.05);
}
