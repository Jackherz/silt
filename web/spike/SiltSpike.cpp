// Spike (handoff-browser.md §9 step 1): Silt's core, unchanged, behind a few C
// functions so a browser AudioWorklet (through WebAssembly) and a native benchmark
// can drive it the same way. Hard-coded scenes stand in for the parameter bridge;
// the real glue (silt::Instrument) replaces this file in step 2.

#include <silt/Color.h>
#include <silt/Engine.h>
#include <silt/Filter.h>
#include <silt/Resonator.h>
#include <silt/Space.h>

#include <algorithm>
#include <array>
#include <memory>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
  #define SPIKE_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
  #define SPIKE_EXPORT extern "C"
#endif

namespace
{
constexpr int maxFrames = 128;   // an AudioWorklet block
constexpr int chunk = 64;        // the plug-in renders in 64-sample chunks

struct Spike
{
    silt::Engine engine;
    silt::Resonator resonator;
    silt::Filter filter;
    silt::Color color;
    silt::Space space;

    silt::GrainParams grain;
    silt::ResonatorParams res;
    silt::FilterParams filt;
    silt::ColorParams col;
    silt::SpaceParams spc;

    std::unique_ptr<silt::SampleBuffer> loading;
    std::array<silt::NoteEvent, 64> events {};
    int eventCount = 0;
    std::array<float, maxFrames * 2> out {};   // left, then right
};

Spike* spike = nullptr;
} // namespace

SPIKE_EXPORT void spike_init (double sampleRate)
{
    delete spike;
    spike = new Spike;
    spike->engine.prepare (sampleRate, maxFrames);
    spike->engine.setSeed (1);
    spike->resonator.prepare (sampleRate);
    spike->filter.prepare (sampleRate);
    spike->color.prepare (sampleRate);
    spike->space.prepare (sampleRate);
}

// The caller fills both channels, then calls spike_sample_end.
SPIKE_EXPORT float* spike_sample_begin (int numFrames, double sampleRate)
{
    spike->loading = std::make_unique<silt::SampleBuffer> (numFrames, sampleRate);
    return spike->loading->getWritePointer (0);
}

SPIKE_EXPORT float* spike_sample_channel (int channel)
{
    return spike->loading->getWritePointer (channel);
}

SPIKE_EXPORT void spike_sample_end()
{
    spike->loading->analyse();
    spike->engine.setBuffer (std::move (spike->loading));
}

// 0: typical (plug-in defaults, effects off). 1: the plug-in benchmark's worst case
// (tests/TestRealtime.cpp), less per-voice modulation, which needs the glue.
SPIKE_EXPORT void spike_scene (int scene)
{
    auto& s = *spike;
    s.engine.allNotesOff();
    s.grain = {};
    s.res = {};
    s.filt = {};
    s.col = {};
    s.spc = {};
    if (scene == 1)
    {
        s.grain.rateHz = 100.0;
        s.grain.sizeMs = 500.0;
        s.grain.heads = 8;
        s.res.mix = 0.6f;
        s.res.drive = 0.5f;
        s.filt.slope = silt::FilterSlope::db24;
        s.col.drive = 0.5f;
        s.col.comp = 0.5f;
        s.col.wow = 0.4f;
        s.spc.delayLevel = 0.5f;
        s.spc.reverbLevel = 0.5f;
        s.spc.shimmer = 0.6f;
    }
    s.res.scale = s.grain.scale;   // as in the plug-in: the PITCH page's scale and key
    s.res.key = s.grain.key;
}

SPIKE_EXPORT void spike_note (int note, float velocity, int isOn)
{
    auto& s = *spike;
    if (s.eventCount < (int) s.events.size())
        s.events[(size_t) s.eventCount++] = { 0, note, velocity, isOn != 0 };
}

SPIKE_EXPORT void spike_all_notes_off()
{
    spike->engine.allNotesOff();
}

// Renders numFrames (≤ 128); returns left, with right maxFrames floats further on.
SPIKE_EXPORT float* spike_process (int numFrames)
{
    auto& s = *spike;
    numFrames = std::min (numFrames, maxFrames);
    float* left = s.out.data();
    float* right = left + maxFrames;

    for (int start = 0; start < numFrames; start += chunk)
    {
        const int len = std::min (chunk, numFrames - start);
        const int events = start == 0 ? s.eventCount : 0;
        float* l = left + start;
        float* r = right + start;
        s.engine.process (l, r, len, s.events.data(), events, s.grain);
        if (s.res.mix > 0.0f)
            s.resonator.process (l, r, len, s.res, s.engine.getStrikes(), s.engine.getStrikeCount());
        if (s.filt.slope != silt::FilterSlope::off)
            s.filter.process (l, r, len, s.filt);
        s.color.process (l, r, len, s.col);
        s.space.process (l, r, len, s.spc);
    }
    s.eventCount = 0;
    s.engine.collectGarbage();
    return left;
}

SPIKE_EXPORT int spike_max_frames() { return maxFrames; }
SPIKE_EXPORT int spike_active_grains() { return spike->engine.getActiveGrainCount(); }
