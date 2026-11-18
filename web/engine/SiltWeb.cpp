// The browser's way into Silt: silt::Instrument (the plug-ins' own sound)
// behind plain C functions, for WebAssembly. One instance per module.
//
// Parameters are set by silt::params id, in their own units, and land as a
// host's would (snapped to the parameter's steps). MIDI is queued as raw bytes
// for the next block. Built by web/engine/build.sh; also builds natively, so
// tests can drive the same functions.

#include <silt/Dsp.h>
#include <silt/Filter.h>
#include <silt/Instrument.h>
#include <silt/Modulator.h>
#include <silt/Resonator.h>
#include <silt/Snap.h>

#include <algorithm>
#include <cmath>
#include <complex>
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

// For the screen: [grains, LIVE?, the ring's write head (share), grains ever over the limit,
// the 16 voices' playheads (< 0: silent), then per grain: position, pan, level, reversed,
// voice, slot]. Returns how many floats.
namespace { std::array<float, 4 + silt::Engine::numVoices + 6 * silt::Instrument::maxGrainViews> screen {};
            std::array<silt::GrainView, silt::Instrument::maxGrainViews> views {}; }
SILT_EXPORT float* silt_screen_data() { return screen.data(); }
SILT_EXPORT int silt_screen()
{
    auto& in = web->instrument;
    const int n = in.getEngine().collectGrainViews (views.data(), (int) views.size());
    screen[0] = (float) n;
    screen[1] = in.liveMode.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    screen[2] = in.liveWriteShare.load (std::memory_order_relaxed);
    screen[3] = (float) in.grainsOverLimit.load (std::memory_order_relaxed);
    for (int v = 0; v < silt::Engine::numVoices; ++v)
        screen[(size_t) (4 + v)] = in.voicePlayheads[(size_t) v].load (std::memory_order_relaxed);
    size_t at = 4 + silt::Engine::numVoices;
    for (int i = 0; i < n; ++i)
    {
        const auto& g = views[(size_t) i];
        screen[at++] = g.position;
        screen[at++] = g.pan;
        screen[at++] = g.level;
        screen[at++] = g.reversed ? 1.0f : 0.0f;
        screen[at++] = (float) g.voice;
        screen[at++] = (float) g.slot;
    }
    return (int) at;
}

// LIVE's ring, as the screen shows it: its peak by position, 1024 columns.
namespace { std::array<float, silt::Instrument::liveColumns> peaks {}; }
SILT_EXPORT float* silt_live_peaks()
{
    peaks = web->instrument.getLivePeaks();
    return peaks.data();
}

// The region as it will play (silt::snapRegion with the sample's analysis, put in with
// silt_analysis_in): writes begin and end (frames) to silt_analysis()[0..1].
namespace { std::array<float, 2> region {}; }
SILT_EXPORT float* silt_snap_region (double begin, double end, int frames, int snap)
{
    const size_t nz = (size_t) analysis[0], nt = (size_t) analysis[1];
    std::vector<int> z (nz), t (nt);
    size_t at = 3;
    for (auto& i : z) i = (int) analysis[at++];
    for (auto& i : t) i = (int) analysis[at++];
    const auto r = silt::snapRegion (begin, end, frames, (silt::Snap) std::clamp (snap, 0, 2), z, t);
    region = { (float) r.begin, (float) r.end };
    return region.data();
}

SILT_EXPORT int silt_max_block() { return maxBlock; }
SILT_EXPORT int silt_active_grains() { return web->instrument.activeGrains.load(); }

// SLICE BY MANUAL: your own slice starts (shares), already sorted and spaced by the page
// (SiltProcessor::setManualSlices); written into silt_slices_in() first.
namespace { std::array<float, 128> sliceIn {}; }
SILT_EXPORT float* silt_slices_in() { return sliceIn.data(); }
SILT_EXPORT void silt_set_slices (int count)
{
    auto& in = web->instrument;
    count = std::clamp (count, 0, (int) in.sliceTable.size());
    in.sliceCountManual.store (0, std::memory_order_release);   // none read while they change
    for (int i = 0; i < count; ++i)
        in.sliceTable[(size_t) i].store (sliceIn[(size_t) i], std::memory_order_relaxed);
    in.sliceCountManual.store (count, std::memory_order_release);
}

// The scope (RESONATOR, FILTER, COLOR): which module it taps (−1: none), and the latest
// `count` samples in and out, oldest first; returns the write counter, as readScope does.
namespace { std::array<float, silt::Instrument::scopeSize / 2> scopeIn {}, scopeOut {}; }
SILT_EXPORT void silt_scope_module (int module) { web->instrument.scopeModule.store (module); }
SILT_EXPORT float* silt_scope_in() { return scopeIn.data(); }
SILT_EXPORT float* silt_scope_out() { return scopeOut.data(); }
SILT_EXPORT unsigned silt_read_scope (int count)
{
    count = std::clamp (count, 0, (int) scopeIn.size());
    return web->instrument.readScope (scopeIn.data(), scopeOut.data(), count);
}

// The screen's corner pictures (plugin/ScreenInsets.cpp): what each needs from the engine as it
// runs, read in the worklet with the modulated values, packed for the page to draw.
//   page 2 PITCH:     [scale mask, 16 pattern steps]
//   page 4 RESONATOR: [bands, root note, then per band: ratio, level, ring seconds, pan side]
//   page 5 FILTER:    [open, 121 levels in dB from 20 Hz to 20 kHz]
//   page 7 SPACE:     [trace count, SPACE's latest peak]
//   page 8 MOD:       [type, bipolar, retrig, backwards, phase, sym, curve, attack, decay, sustain,
//                      release, now phase, now output, stage, stage ms, released from,
//                      release level, 161 points of one cycle]
namespace { std::vector<float> inset (8 + 128 * 4); }
SILT_EXPORT float* silt_inset_data() { return inset.data(); }
SILT_EXPORT int silt_inset (int page, int modulator)
{
    auto& in = web->instrument;
    const auto v = [&] (int id) { return in.value (id); };
    inset.clear();
    switch (page)
    {
        case 2:
        {
            inset.push_back ((float) silt::scaleMask ((silt::Scale) std::clamp ((int) std::lround (v (sp::scale)), 0, silt::scaleCount - 1)));
            silt::Random r (0x9e3779b97f4a7c15ull ^ (std::uint64_t) (int) std::lround (v (sp::patternSeed)));
            for (int i = 0; i < 16; ++i)
                inset.push_back (r.nextBipolar());
            break;
        }
        case 4:
        {
            silt::ResonatorParams p;
            p.rootNote = in.resonatorRootNow.load (std::memory_order_relaxed);
            p.chordSize = std::clamp (in.resonatorChordSizeNow.load (std::memory_order_relaxed), 0, (int) p.chord.size());
            for (int c = 0; c < p.chordSize; ++c)
                p.chord[(size_t) c] = in.resonatorChordNow[(size_t) c].load (std::memory_order_relaxed);
            p.notes = (silt::ResonatorNotes) std::clamp ((int) std::lround (v (sp::resNotes)), 0, 3);
            p.sampleCount = std::clamp (in.samplePitchCount.load(), 0, (int) p.sampleHz.size());
            for (int i = 0; i < p.sampleCount; ++i)
            {
                p.sampleHz[(size_t) i] = in.samplePitchHz[(size_t) i].load() * std::pow (2.0f, (p.rootNote - 48.0f) / 12.0f);
                p.sampleLevel[(size_t) i] = in.samplePitchLevel[(size_t) i].load();
            }
            p.scale = (silt::Scale) std::clamp ((int) std::lround (v (sp::scale)), 0, silt::scaleCount - 1);
            p.key = (int) std::lround (v (sp::key));
            p.rangeOctaves = v (sp::resRange);
            p.decaySeconds = v (sp::resDecay);
            p.bright = v (sp::resBright) * 0.01f;
            p.bandCount = (int) std::lround (v (sp::resBands));
            p.stretch = v (sp::resStretch) * 0.01f;
            p.stretchCurve = v (sp::resCurve) * 0.01f;
            p.snap = v (sp::resSnap) * 0.01f;
            p.offset = v (sp::resOffset);
            p.ripple = v (sp::resRipple);
            p.rippleDepth = v (sp::resDepth) * 0.01f;
            p.sweep = v (sp::resSweep) * 0.01f;
            const float oddEven = v (sp::resOddEven) * 0.01f, damp = v (sp::resDamp) * 0.01f;
            static std::array<silt::Resonator::BankMode, silt::Resonator::maxBands> modes {};
            const int n = silt::Resonator::bankModes (p, modes);
            inset.push_back ((float) n);
            inset.push_back (p.rootNote);
            for (int i = 0; i < n; ++i)
            {
                const auto& mode = modes[(size_t) i];
                const float against = mode.partial % 2 == 0 ? std::max (0.0f, oddEven) : std::max (0.0f, -oddEven);
                const float level = std::clamp ((float) (mode.weight * silt::Resonator::levelAt (mode.own, p.bright)
                                                         * silt::Resonator::rippleAt (mode.ratio, modes[(size_t) n - 1].ratio, p)) * (1.0f - against), 0.0f, 1.0f);
                inset.push_back ((float) mode.ratio);
                inset.push_back (level);
                inset.push_back (std::max (0.02f, p.decaySeconds * (float) silt::Resonator::ringAt (mode.own, p.bright, damp)));
                inset.push_back ((mode.partial + mode.note) % 2 == 0 ? -1.0f : 1.0f);
            }
            break;
        }
        case 5:
        {
            const float cutoffF = v (sp::filterCutoff), resoF = v (sp::filterReso) * 0.01f, morphF = v (sp::filterMorph), mixF = v (sp::filterMix) * 0.01f;
            // As drawFilter passes them: the values in double, the percentages scaled in double.
            const double cutoff = cutoffF, reso = (double) v (sp::filterReso) * 0.01, morph = morphF, mix = (double) v (sp::filterMix) * 0.01;
            const bool steep = std::lround (v (sp::filterMode)) == 1;
            const bool open = silt::Instrument::filterIsOpen (cutoffF, resoF, morphF, v (sp::filterDrive) * 0.01f, v (sp::filterEnv) * 0.01f, v (sp::filterKey) * 0.01f, mixF);
            inset.push_back (open ? 1.0f : 0.0f);
            for (int i = 0; i <= 120; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, i / 120.0);
                const double slope = steep ? 1.0 : 0.0;
                const std::complex<double> pole = 1.0 / (1.0 + std::complex<double> (0.0, hz / cutoff));
                const auto fourPoles = pole * pole * pole * pole;
                std::complex<double> tap = 1.0 / (1.0 + silt::Filter::feedbackFor (reso, slope) * fourPoles), h = 0.0;
                for (const double amount : silt::Filter::tapMixFor (morph, slope, reso, cutoff))
                {
                    h += amount * tap;
                    tap *= pole;
                }
                const double magnitude = std::abs ((1.0 - mix) + mix * h);
                inset.push_back (open ? 0.0f : (float) (20.0 * std::log10 (std::max (1.0e-6, magnitude))));
            }
            break;
        }
        case 7:
        {
            const unsigned count = in.spaceTraceCount.load (std::memory_order_acquire);
            float peak = 0.0f;
            for (unsigned i = count > 8 ? count - 8 : 0; i < count; ++i)
                peak = std::max (peak, in.spaceTraceAt (i));
            inset.push_back ((float) count);
            inset.push_back (peak);
            break;
        }
        case 8:
        {
            const int m = std::clamp (modulator, 0, silt::Instrument::numModulators - 1);
            auto p = in.readModulatorParams (m);
            const bool backwards = p.sync ? p.reverse : p.rateHz < 0.0f;
            inset.insert (inset.end(), { (float) p.type, silt::Modulator::isBipolar (p) ? 1.0f : 0.0f, p.retrigger ? 1.0f : 0.0f,
                                         backwards ? 1.0f : (! p.sync && p.rateHz == 0.0f ? 2.0f : 0.0f),
                                         p.phase, p.symmetry, p.curve, p.attackMs, p.decayMs, p.sustain, p.releaseMs,
                                         in.modPhases[(size_t) m].load(), in.modOutputs[(size_t) m].load(),
                                         (float) in.modStages[(size_t) m].load(), in.modStageMs[(size_t) m].load(),
                                         (float) in.modReleasedFrom[(size_t) m].load(), in.modReleaseLevel[(size_t) m].load() });
            if (p.type == silt::ModType::wave)
            {
                // One cycle from PHASE, as ScreenInsets draws it: a copy restarted there, stepped 1/160 at a time.
                constexpr int n = 160;
                silt::Modulator copy;
                copy.reset();
                p.rateHz = backwards ? -1.0f : 1.0f;
                p.sync = false;
                p.retrigger = true;
                silt::ModContext c;
                c.sampleRate = n;
                c.numSamples = 0;
                c.noteStarted = true;
                inset.push_back (copy.advance (p, c));
                c.numSamples = 1;
                c.noteStarted = false;
                for (int i = 1; i <= n; ++i)
                    inset.push_back (copy.advance (p, c));
            }
            break;
        }
        default: break;
    }
    return (int) inset.size();
}
