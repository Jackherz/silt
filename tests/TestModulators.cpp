#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <silt/Modulator.h>

#include <cmath>
#include <vector>

using namespace silt;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 64;

ModContext context()
{
    ModContext c;
    c.numSamples = block;
    c.sampleRate = rate;
    return c;
}

// Runs a modulator for some seconds, a block at a time; returns each block's value.
std::vector<float> run (Modulator& m, const ModulatorParams& p, double seconds, ModContext c = context())
{
    std::vector<float> out;
    for (int i = 0; i < (int) (seconds * rate / block); ++i)
    {
        out.push_back (m.advance (p, c));
        c.noteStarted = false;
    }
    return out;
}
} // namespace

TEST_CASE ("Wave: a 1 Hz sine follows sin(2 pi t)")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    const auto out = run (m, p, 2.0);
    for (size_t i = 0; i < out.size(); i += 37)
    {
        const double t = (double) (i + 1) * block / rate;
        CHECK_THAT (out[i], WithinAbs (std::sin (2.0 * 3.14159265358979 * t), 1.0e-3));
    }
}

TEST_CASE ("Wave: shapes stay in range and blend")
{
    for (const float shape : { 0.0f, 0.5f, 1.0f, 1.7f, 2.0f, 2.5f, 3.0f })
    {
        Modulator m;
        m.reset();
        ModulatorParams p;
        p.shape = shape;
        p.rateHz = 3.0f;
        float lo = 1.0f, hi = -1.0f;
        for (const float v : run (m, p, 1.0))
        {
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        INFO ("shape " << shape << ": " << lo << " to " << hi);
        CHECK (lo >= -1.0f);
        CHECK (hi <= 1.0f);
        CHECK (hi - lo > 1.0f);
    }
    // A square only takes its two values.
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.shape = 3.0f;
    for (const float v : run (m, p, 1.0))
        CHECK (std::abs (v) == 1.0f);
}

TEST_CASE ("Wave: synced to a playing host, it lines up with the beat")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.sync = true;
    p.syncQuarters = 1.0;
    p.shape = 2.0f;   // saw: the value says where in the cycle it is
    auto c = context();
    c.hostPlaying = true;
    for (const double ppq : { 0.0, 0.25, 0.5, 3.75, 10.5 })
    {
        c.ppqEnd = ppq;
        CHECK_THAT (m.advance (p, c), WithinAbs (2.0 * (ppq - std::floor (ppq)) - 1.0, 1.0e-6));
    }
}

TEST_CASE ("Wave: retrigger restarts it on each note")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.retrigger = true;
    p.shape = 2.0f;
    run (m, p, 0.37);
    auto c = context();
    c.noteStarted = true;
    const float first = m.advance (p, c);
    CHECK (first < -0.99f);   // just past the start of the saw
}

TEST_CASE ("Random: stepped holds between steps; smooth glides; both stay in range")
{
    Modulator m;
    m.reset (7);
    ModulatorParams p;
    p.type = ModType::random;
    p.rateHz = 4.0f;
    p.smooth = 0.0f;
    const auto stepped = run (m, p, 3.0);
    int changes = 0;
    for (size_t i = 1; i < stepped.size(); ++i)
        changes += stepped[i] != stepped[i - 1];
    INFO ("changes: " << changes);
    CHECK (std::abs (changes - 12) <= 1);   // 4 a second

    m.reset (7);
    p.smooth = 1.0f;
    const auto smooth = run (m, p, 3.0);
    float biggest = 0.0f;
    for (size_t i = 1; i < smooth.size(); ++i)
        biggest = std::max (biggest, std::abs (smooth[i] - smooth[i - 1]));
    CHECK (biggest < 0.05f);
    for (const float v : smooth)
        CHECK (std::abs (v) <= 1.0f);
}

TEST_CASE ("Envelope: attack, decay to sustain, release, and repeat")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.type = ModType::envelope;
    p.attackMs = 100.0f;
    p.decayMs = 100.0f;
    p.sustain = 0.5f;
    p.releaseMs = 200.0f;
    auto c = context();
    c.heldNotes = 1;
    c.noteStarted = true;
    const auto held = run (m, p, 0.5, c);
    const auto at = [&] (double seconds) { return held[(size_t) (seconds * rate / block) - 1]; };
    CHECK_THAT (at (0.05), WithinAbs (0.5, 0.02));    // halfway up
    CHECK_THAT (at (0.15), WithinAbs (0.75, 0.02));   // halfway down to sustain
    CHECK_THAT (at (0.4), WithinAbs (0.5, 1.0e-6));

    c.heldNotes = 0;
    c.noteStarted = false;
    const auto released = run (m, p, 0.3, c);
    CHECK_THAT (released[(size_t) (0.05 * rate / block) - 1], WithinAbs (0.25, 0.02));
    CHECK (released.back() == 0.0f);

    m.reset();
    p.repeat = true;
    c.heldNotes = 1;
    c.noteStarted = true;
    const auto looping = run (m, p, 1.0, c);
    int peaks = 0;
    for (size_t i = 1; i < looping.size(); ++i)
        peaks += looping[i] == 1.0f && looping[i - 1] < 1.0f;
    CHECK (peaks >= 4);   // one every ~150 ms
}

TEST_CASE ("Follow: tracks the input's level on a 60 dB scale")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.type = ModType::follow;
    auto c = context();
    c.inputPeak = 1.0f;
    CHECK_THAT (run (m, p, 0.5, c).back(), WithinAbs (1.0, 0.01));
    c.inputPeak = 0.001f;   // −60 dB
    CHECK (run (m, p, 2.0, c).back() < 0.02f);
    c.inputPeak = std::pow (10.0f, -30.0f / 20.0f);
    CHECK_THAT (run (m, p, 2.0, c).back(), WithinAbs (0.5, 0.02));
}

TEST_CASE ("MIDI sources pass through; note and bend are bipolar")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.type = ModType::midi;
    auto c = context();
    c.velocity = 0.8f;
    c.note = 84;
    c.modWheel = 0.3f;
    c.aftertouch = 0.6f;
    c.pitchBend = -0.5f;
    p.source = MidiSource::velocity;   CHECK (m.advance (p, c) == 0.8f);
    p.source = MidiSource::note;       CHECK (m.advance (p, c) == 0.5f);
    p.source = MidiSource::modWheel;   CHECK (m.advance (p, c) == 0.3f);
    p.source = MidiSource::aftertouch; CHECK (m.advance (p, c) == 0.6f);
    p.source = MidiSource::pitchBend;  CHECK (m.advance (p, c) == -0.5f);
    CHECK (Modulator::isBipolar (p));
    p.source = MidiSource::velocity;
    CHECK_FALSE (Modulator::isBipolar (p));
}

TEST_CASE ("MACRO is its knob, 0 to 1")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.type = ModType::macro;
    p.macro = 0.37f;
    CHECK (m.advance (p, context()) == 0.37f);
    p.macro = 2.0f;
    CHECK (m.advance (p, context()) == 1.0f);
    CHECK_FALSE (Modulator::isBipolar (p));
}

TEST_CASE ("Wave: SYMMETRY 50% and CURVE 0% are exactly today's shapes")
{
    for (const float shape : { 0.0f, 1.0f, 1.5f, 2.0f, 3.0f })
    {
        Modulator a, b;
        a.reset();
        b.reset();
        ModulatorParams plain, explicitDefaults;
        plain.shape = explicitDefaults.shape = shape;
        plain.rateHz = explicitDefaults.rateHz = 3.0f;
        explicitDefaults.symmetry = 0.5f;
        explicitDefaults.curve = 0.0f;
        CHECK (run (a, plain, 1.0) == run (b, explicitDefaults, 1.0));
    }
}

TEST_CASE ("Wave: SYMMETRY moves where the cycle turns; a square's duty follows it")
{
    Modulator m;
    m.reset();
    ModulatorParams p;
    p.shape = 3.0f;        // square: low for the first half of the (warped) cycle
    p.symmetry = 0.25f;
    p.rateHz = 1.0f;
    const auto out = run (m, p, 4.0);
    int low = 0;
    for (const float v : out)
        low += v < 0.0f ? 1 : 0;
    CHECK (std::abs ((double) low / (double) out.size() - 0.25) < 0.02);

    // A triangle at 25% peaks a quarter of the way through its first half.
    Modulator t;
    t.reset();
    p.shape = 1.0f;
    const auto tri = run (t, p, 1.0);
    const auto peak = std::max_element (tri.begin(), tri.end()) - tri.begin();
    CHECK (std::abs ((double) peak / (double) tri.size() - 0.125) < 0.02);
}

TEST_CASE ("Wave: CURVE bends but keeps the peaks and the sign, everywhere in range")
{
    for (const float curve : { -1.0f, -0.5f, 0.5f, 1.0f })
        for (const float sym : { 0.1f, 0.5f, 0.9f })
        {
            Modulator plain, bent;
            plain.reset();
            bent.reset();
            ModulatorParams p;
            p.shape = 1.0f;
            p.symmetry = sym;
            p.rateHz = 2.0f;
            const auto a = run (plain, p, 1.0);
            p.curve = curve;
            const auto b = run (bent, p, 1.0);
            for (size_t i = 0; i < a.size(); ++i)
            {
                REQUIRE (std::abs (b[i]) <= 1.0f);
                REQUIRE ((a[i] < 0.0f) == (b[i] < 0.0f));
                // Spikes (+) sit nearer zero than the original; bulges (−) further out.
                if (curve > 0.0f)
                    REQUIRE (std::abs (b[i]) <= std::abs (a[i]) + 1.0e-6f);
                else
                    REQUIRE (std::abs (b[i]) >= std::abs (a[i]) - 1.0e-6f);
            }
        }
}
