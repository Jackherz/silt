// The browser's way into Silt: silt::Instrument (the plug-ins' own sound)
// behind plain C functions, for WebAssembly. One instance per module.
//
// Parameters are set by silt::params id, in their own units, and land as a
// host's would (snapped to the parameter's steps). MIDI is queued as raw bytes
// for the next block. Built by web/engine/build.sh; also builds natively, so
// tests can drive the same functions.

#include <silt/Dsp.h>
#include <silt/Instrument.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
  #define SILT_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
  #define SILT_EXPORT extern "C"
#endif

namespace
{
namespace sp = silt::params;
constexpr int maxBlock = 128;   // an AudioWorklet block

struct Web
{
    explicit Web (bool isInstrument) : instrument (isInstrument) {}

    silt::Instrument instrument;
    std::unique_ptr<silt::SampleBuffer> loading;
    std::array<silt::MidiEvent, 512> midi {};
    int midiCount = 0;
    std::array<float, maxBlock * 2> input {}, output {};   // left, then right
    silt::Transport transport;
    std::array<bool, sp::count> moved {};   // set at least once (JUCE always takes the first change)
};

Web* web = nullptr;
} // namespace

SILT_EXPORT void silt_create (int isInstrument)
{
    delete web;
    web = new Web (isInstrument != 0);
}

// maxBlockSize: the most a host block ever holds (128 in a worklet). It also sets
// how far behind the live write head LIVE's grains read, so the parity check
// passes the plug-in's 512.
SILT_EXPORT void silt_prepare (double sampleRate, int maxBlockSize)
{
    web->instrument.prepare (sampleRate, std::max (maxBlock, maxBlockSize));
}

SILT_EXPORT int silt_param_count() { return sp::count; }

// In the parameter's own units, held as a JUCE host holds it (silt::params::hostSet),
// so the browser and the plug-in play the same numbers.
SILT_EXPORT void silt_set_param (int id, float value)
{
    if (id < 0 || id >= sp::count)
        return;
    const float held = sp::hostSet (id, value);
    if (web->moved[(size_t) id] && sp::approximatelyEqual (web->instrument.parameter (id), held))
        return;
    web->moved[(size_t) id] = true;
    web->instrument.setParameter (id, held);
}

// A value exactly as given: what the audio's instance takes from the page's, which already
// holds it as a host would.
SILT_EXPORT void silt_set_param_raw (int id, float value)
{
    if (id >= 0 && id < sp::count)
        web->instrument.setParameter (id, value);
}

SILT_EXPORT float silt_get_param (int id) { return web->instrument.parameter (id); }

// A knob's travel, as the plug-in's slider has it: 0–1 from a value (unsnapped), and the
// value at a point along it (snapped to the parameter's steps).
SILT_EXPORT float silt_to01 (int id, float value)
{
    const auto& p = sp::info (id);
    if (p.mapping() == sp::Mapping::linear || p.kind != sp::Kind::real)
        return std::clamp ((value - p.low) / (p.high - p.low), 0.0f, 1.0f);
    return sp::to01 (id, value);
}
SILT_EXPORT float silt_from01 (int id, float proportion) { return sp::from01 (id, proportion); }
SILT_EXPORT float silt_get_value (int id) { return web->instrument.value (id); }   // as the sound reads it, modulated

// Modulation routes, by parameter id; amount −1…1, polarity 0 natural, 1 unipolar, 2 bipolar.
SILT_EXPORT int silt_set_route (int modulator, int parameterId, float amount, int polarity)
{
    const int t = web->instrument.targetFor (parameterId);
    if (t < 0)
        return 0;
    web->instrument.setRouteAmount (modulator, t, amount);
    web->instrument.setRoutePolarity (modulator, t, polarity);
    return 1;
}

SILT_EXPORT void silt_clear_routes() { web->instrument.clearAllRoutes(); }

// Routes by parameter id, for the panel (rings, the route list): its depth, its polarity
// as set (0 natural), whether it swings both ways, whether a route could work at all.
SILT_EXPORT int silt_is_target (int parameterId) { return web->instrument.targetFor (parameterId) >= 0 ? 1 : 0; }
SILT_EXPORT float silt_route_amount (int modulator, int parameterId)
{
    const int t = web->instrument.targetFor (parameterId);
    return t < 0 ? 0.0f : web->instrument.routeAmount (modulator, t);
}
SILT_EXPORT int silt_route_polarity (int modulator, int parameterId)
{
    const int t = web->instrument.targetFor (parameterId);
    return t < 0 ? 0 : web->instrument.routePolarityOf (modulator, t);
}
SILT_EXPORT int silt_route_bipolar (int modulator, int parameterId)
{
    const int t = web->instrument.targetFor (parameterId);
    return t >= 0 && web->instrument.routeIsBipolar (modulator, t) ? 1 : 0;
}
SILT_EXPORT int silt_route_can_work (int modulator, int parameterId)
{
    const int t = web->instrument.targetFor (parameterId);
    return t >= 0 && web->instrument.routeCanWork (modulator, t) ? 1 : 0;
}
SILT_EXPORT void silt_natural_where_unrouted() { web->instrument.naturalWhereUnrouted(); }
SILT_EXPORT int silt_modulator_per_voice (int modulator) { return web->instrument.isModulatorPerVoice (modulator) ? 1 : 0; }

// A sample: begin, fill both channels, end (which analyses it and hands it over).
SILT_EXPORT float* silt_sample_begin (int numFrames, double sampleRate)
{
    web->loading = std::make_unique<silt::SampleBuffer> (numFrames, sampleRate);
    return web->loading->getWritePointer (0);
}

SILT_EXPORT float* silt_sample_channel (int channel) { return web->loading->getWritePointer (channel); }

// Analysing a sample without playing it (the page does this, so the audio thread doesn't
// stall): begin and fill as above, then silt_analyse; the results are at silt_analysis()
// (counts: zero crossings, transients, modes), then the ints, then the modes as hz, level.
namespace { std::vector<float> analysis; }
SILT_EXPORT int silt_analyse()
{
    auto& b = *web->loading;
    b.analyse();
    const auto& z = b.getZeroCrossings();
    const auto& t = b.getTransients();
    const auto& m = b.getModes();
    analysis.assign (3 + z.size() + t.size() + 2 * m.size(), 0.0f);
    analysis[0] = (float) z.size();
    analysis[1] = (float) t.size();
    analysis[2] = (float) m.size();
    size_t at = 3;
    for (const int i : z) analysis[at++] = (float) i;   // exact up to 2^24 frames (5.8 minutes at 48 kHz)
    for (const int i : t) analysis[at++] = (float) i;
    for (const auto& mode : m) { analysis[at++] = (float) mode.hz; analysis[at++] = mode.level; }
    web->loading.reset();
    return (int) analysis.size();
}
SILT_EXPORT float* silt_analysis() { return analysis.data(); }

// Hands a filled sample over with an analysis made by silt_analyse (laid out the same):
// nothing slow happens here, so the audio thread can call it.
SILT_EXPORT float* silt_analysis_in (int size)
{
    analysis.assign ((size_t) std::max (0, size), 0.0f);
    return analysis.data();
}
SILT_EXPORT void silt_sample_end_analysed()
{
    const size_t nz = (size_t) analysis[0], nt = (size_t) analysis[1], nm = (size_t) analysis[2];
    std::vector<int> z (nz), t (nt);
    std::vector<silt::SampleMode> m (nm);
    size_t at = 3;
    for (auto& i : z) i = (int) analysis[at++];
    for (auto& i : t) i = (int) analysis[at++];
    for (auto& mode : m) { mode.hz = analysis[at++]; mode.level = analysis[at++]; }
    web->loading->setAnalysis (std::move (z), std::move (t), std::move (m));
    auto& in = web->instrument;
    const auto& modes = web->loading->getModes();
    const int count = std::min ((int) modes.size(), (int) in.samplePitchHz.size());
    for (int i = 0; i < count; ++i)
    {
        in.samplePitchHz[(size_t) i].store ((float) modes[(size_t) i].hz, std::memory_order_relaxed);
        in.samplePitchLevel[(size_t) i].store (modes[(size_t) i].level, std::memory_order_relaxed);
    }
    in.samplePitchCount.store (count, std::memory_order_release);
    in.getEngine().setBuffer (std::move (web->loading));
}

SILT_EXPORT void silt_sample_end()
{
    auto& in = web->instrument;
    web->loading->analyse();
    const auto& modes = web->loading->getModes();
    const int count = std::min ((int) modes.size(), (int) in.samplePitchHz.size());
    for (int i = 0; i < count; ++i)
    {
        in.samplePitchHz[(size_t) i].store ((float) modes[(size_t) i].hz, std::memory_order_relaxed);
        in.samplePitchLevel[(size_t) i].store (modes[(size_t) i].level, std::memory_order_relaxed);
    }
    in.samplePitchCount.store (count, std::memory_order_release);
    in.getEngine().setBuffer (std::move (web->loading));
}

SILT_EXPORT void silt_midi (int offset, int b0, int b1, int b2)
{
    if (web->midiCount < (int) web->midi.size())
        web->midi[(size_t) web->midiCount++] = { offset, { (std::uint8_t) b0, (std::uint8_t) b1, (std::uint8_t) b2 } };
}

// The transport for the next blocks. bpm ≤ 0: none given (the last stays).
SILT_EXPORT void silt_transport (int playing, double ppq, double bpm, int numerator, int denominator)
{
    auto& t = web->transport;
    t.known = true;
    t.playing = playing != 0;
    t.ppq = ppq;
    t.hasBpm = bpm > 0.0;
    t.bpm = bpm;
    t.hasSignature = numerator > 0 && denominator > 0;
    t.numerator = numerator;
    t.denominator = denominator;
}

// Where to put the live input (left, then right, maxBlock floats each).
SILT_EXPORT float* silt_input() { return web->input.data(); }

// Renders numSamples (≤ 128) with inputChannels of input (0–2); returns the
// output, left then right, 128 floats apart.
SILT_EXPORT float* silt_process (int numSamples, int inputChannels)
{
    numSamples = std::clamp (numSamples, 0, maxBlock);
    const float* in[2] { web->input.data(), web->input.data() + maxBlock };
    // In time order, ties as they came (an insertion sort: nothing allocates here).
    for (int i = 1; i < web->midiCount; ++i)
        for (int j = i; j > 0 && web->midi[(size_t) j - 1].offset > web->midi[(size_t) j].offset; --j)
            std::swap (web->midi[(size_t) j - 1], web->midi[(size_t) j]);
    web->instrument.process (web->output.data(), web->output.data() + maxBlock, numSamples,
                             inputChannels > 0 ? in : nullptr, std::clamp (inputChannels, 0, 2),
                             web->midi.data(), web->midiCount, web->transport);
    web->midiCount = 0;
    web->instrument.getEngine().collectGarbage();
    return web->output.data();
}

// How a value reads on the panel, exactly as in the plug-in (UTF-8, until the next call).
SILT_EXPORT const char* silt_param_text (int id, float value)
{
    static std::string text;
    text = sp::text (id, value);
    return text.c_str();
}

// One grain's envelope for SHAPE's preview, as the plug-in draws it (SiltEditor::paintPreview):
// points (t, level), both 0–1, sampled along the rise and the fall. Returns how many; the
// points are at silt_curve_points(), x then y.
namespace { std::array<float, 2 * 120> curve {}; }
SILT_EXPORT float* silt_curve_points() { return curve.data(); }
SILT_EXPORT int silt_grain_curve (float fade, float slant, float ease)
{
    static const silt::HannWindow hann;
    silt::GrainEnvelope env;
    env.set (fade * 0.01, slant * 0.01, ease * 0.01, 1000, 0.0);
    int n = 0;
    const auto add = [&] (double t, float level) { curve[(size_t) n * 2] = (float) t; curve[(size_t) n * 2 + 1] = level; ++n; };
    const double rise = env.getAttackEnd(), fall = env.getReleaseStart();
    constexpr int steps = 48;
    add (0.0, env (hann, 0.0));
    for (int i = 1; i <= steps; ++i)
    {
        const double t = rise * i / steps * (1.0 - 1.0e-9);
        add (t, env (hann, t));
    }
    add (rise, 1.0f);
    add (fall, 1.0f);
    for (int i = 1; i <= steps; ++i)
    {
        const double t = fall + (1.0 - fall) * i / steps;
        add (t, env (hann, t));
    }
    return n;
}

// For the panel: where modulation has each target now (0–1, -1 for a parameter that isn't
// one), every target's at once into silt_display(); and the modulators' outputs.
namespace { std::array<float, sp::count + 4> display {}; }
SILT_EXPORT float* silt_display()
{
    auto& in = web->instrument;
    for (int id = 0; id < sp::count; ++id)
    {
        const int t = in.targetFor (id);
        display[(size_t) id] = t < 0 ? -1.0f : in.modulatedValue01 (t);
    }
    for (int m = 0; m < 4; ++m)
        display[(size_t) sp::count + (size_t) m] = in.modOutputs[(size_t) m].load (std::memory_order_relaxed);
    return display.data();
}

SILT_EXPORT int silt_max_block() { return maxBlock; }
SILT_EXPORT int silt_active_grains() { return web->instrument.activeGrains.load(); }
