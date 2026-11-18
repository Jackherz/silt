#include <silt/Instrument.h>
#include <silt/Pitches.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace silt
{
namespace
{
namespace sp = params;

// JUCE's helpers, to the bit, so the plug-in sounds as it did before this moved here.
template <typename T> T limit (T low, T high, T v) noexcept { return v < low ? low : (high < v ? high : v); }

int roundToInt (double value) noexcept
{
    double n = value + 6755399441055744.0;
    std::int32_t parts[2];
    std::memcpy (parts, &n, sizeof (n));
    return parts[0];
}

float decibelsToGain (float db, float minusInfinityDb = -100.0f) noexcept
{
    return db > minusInfinityDb ? std::pow (10.0f, db * 0.05f) : 0.0f;
}

float gainToDecibels (float gain, float minusInfinityDb = -100.0f) noexcept
{
    return gain > 0.0f ? std::max (minusInfinityDb, static_cast<float> (std::log10 (gain)) * 20.0f) : minusInfinityDb;
}

void applyGainRamp (float* d, int numSamples, float startGain, float endGain) noexcept
{
    const auto increment = (endGain - startGain) / (float) numSamples;
    while (--numSamples >= 0)
    {
        *d++ *= startGain;
        startGain += increment;
    }
}

float magnitude (const float* d, int n) noexcept
{
    if (n <= 0)
        return 0.0f;
    float lo = d[0], hi = d[0];
    for (int i = 1; i < n; ++i)
    {
        lo = std::min (lo, d[i]);
        hi = std::max (hi, d[i]);
    }
    return std::max ({ lo, -lo, hi, -hi });
}

// Linear to −1.9 dBFS, then a soft knee to full scale (plan gap 7: a safety
// net for dense patches, not a sound).
float safetyClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    return std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}

constexpr int rateModeSync = 1, rateModeGrains = 2;   // "rateMode" choices; 0 is Free
constexpr double maxDelayMs = 4000.0, maxSyncedSizeMs = 4000.0;

// The 24 chain orders, in permutation order: 0 is RES, FILTER, COLOR, SPACE.
using ChainOrder = std::array<int, Instrument::numModules>;
const std::array<ChainOrder, Instrument::numChainOrders>& chainOrders()
{
    static const auto list = []
    {
        std::array<ChainOrder, Instrument::numChainOrders> orders {};
        ChainOrder order { 0, 1, 2, 3 };
        for (auto& o : orders)
        {
            o = order;
            std::next_permutation (order.begin(), order.end());
        }
        return orders;
    }();
    return list;
}

constexpr int moduleOnIds[] { sp::resOn, sp::filterOn, sp::colorOn, sp::spaceOn };
} // namespace

std::array<int, Instrument::numModules> Instrument::chainOrder (int index) noexcept
{
    return chainOrders()[(size_t) limit (0, numChainOrders - 1, index)];
}

// --- per-voice modulation (docs/per-voice-modulation.md) -----------------------------

// Each voice's own copies of the per-voice modulators, its own note context,
// and its grain parameters for the chunk. The engine asks for them.
class Instrument::VoiceModulation final : public VoiceParams
{
public:
    explicit VoiceModulation (Instrument& o) : owner (o) {}

    void reset() noexcept
    {
        for (int v = 0; v < Engine::numVoices; ++v)
        {
            for (int m = 0; m < numModulators; ++m)
                mods[(size_t) v][(size_t) m].reset (0x7019e + (std::uint64_t) (v * numModulators + m));
            context[(size_t) v] = {};
            held[(size_t) v] = false;
            started[(size_t) v] = false;
        }
        active = false;
    }

    // Per chunk: which modulators run per voice and whether any reaches a grain
    // parameter; then each sounding voice's modulators step and its parameters
    // are worked out once (the engine copies them per stretch).
    void prepareChunk() noexcept
    {
        active = false;
        for (int m = 0; m < numModulators; ++m)
        {
            perVoice[(size_t) m] = owner.isModulatorEnabled (m) && owner.v (owner.modulatorIds[(size_t) m].voice) > 0.5f && ! owner.grainSource[(size_t) m];
            if (perVoice[(size_t) m])
                for (const int t : owner.voiceTargets)
                    active = active || owner.routeAmount (m, t) != 0.0f;
        }
        for (auto& shown : owner.voiceShown)
            shown.store (false, std::memory_order_relaxed);
        if (! active)
            return;

        for (const int t : owner.voiceTargets)
            base01[(size_t) t] = sp::to01 (owner.targets[(size_t) t], owner.source[(size_t) owner.targets[(size_t) t]]->load (std::memory_order_relaxed));

        for (int v = 0; v < Engine::numVoices; ++v)
        {
            if (! owner.engine.getVoice (v).isActive() && ! held[(size_t) v])
                continue;
            step (v, owner.modContext.numSamples);
            compute (v);
        }
    }

    bool isActive() const noexcept override { return active; }

    void noteStarted (int v, int note, float velocity) noexcept override
    {
        // SPREAD: the notes of a phrase (from the first after all were let go)
        // step round the cycle by the golden ratio, so any number stay apart.
        if (std::none_of (held.begin(), held.end(), [] (bool h) { return h; }))
            notesInPhrase = 0;
        spreadOffset[(size_t) v] = (float) std::fmod (0.6180339887 * notesInPhrase++, 1.0);

        // A new note (or a stolen voice) starts its modulators afresh.
        for (int m = 0; m < numModulators; ++m)
            mods[(size_t) v][(size_t) m].reset (0x7019e + (std::uint64_t) (v * numModulators + m) + (std::uint64_t) note * 977u);
        auto& c = context[(size_t) v];
        c.note = note;
        c.velocity = velocity;
        held[(size_t) v] = true;
        started[(size_t) v] = true;
        if (active)
        {
            step (v, 0);
            compute (v);
        }
    }

    void noteReleased (int v) noexcept override { held[(size_t) v] = false; }

    // The engine's stretch settings (grid, pattern, live position) stay; the
    // parameters are this voice's.
    void apply (int v, GrainParams& out) noexcept override
    {
        auto mine = params[(size_t) v];
        mine.gridSync = out.gridSync;
        mine.ppqPerSample = out.ppqPerSample;
        mine.gridPpq = out.gridPpq;
        mine.liveMargin = out.liveMargin;
        mine.liveNow = out.liveNow;
        mine.pattern = out.pattern;
        mine.scatter = out.scatter;
        mine.patternSteps = limit (1, 16, mine.patternSteps);
        out = mine;
    }

private:
    // Steps one voice's per-voice modulators: the shared context (clock, CCs,
    // input level) with this voice's own note.
    void step (int v, int numSamples) noexcept
    {
        auto& c = context[(size_t) v];
        const auto& shared = owner.modContext;
        c.numSamples = numSamples;
        c.sampleRate = shared.sampleRate;
        c.hostPlaying = shared.hostPlaying;
        c.ppqEnd = shared.ppqEnd;
        c.bpm = shared.bpm;
        c.modWheel = shared.modWheel;
        c.aftertouch = shared.aftertouch;
        c.pitchBend = shared.pitchBend;
        c.inputPeak = shared.inputPeak;
        c.heldNotes = held[(size_t) v] ? 1 : 0;
        c.noteStarted = started[(size_t) v];
        started[(size_t) v] = false;
        for (int m = 0; m < numModulators; ++m)
            if (perVoice[(size_t) m])
            {
                auto mine = owner.readModulatorParams (m);
                mine.voiceOffset = owner.v (owner.modulatorIds[(size_t) m].spread) * 0.01f * spreadOffset[(size_t) v];
                values[(size_t) v][(size_t) m] = mods[(size_t) v][(size_t) m].advance (mine, c);
            }
    }

    // One voice's grain parameters: grain targets from this voice's modulators
    // (shared ones for modulators that aren't per voice), the rest as shared.
    void compute (int v) noexcept
    {
        for (int t = 0; t < owner.numTargets(); ++t)
            scratch[(size_t) t] = owner.modulated[(size_t) t].load (std::memory_order_relaxed);
        for (const int t : owner.voiceTargets)
        {
            float offset = 0.0f;
            for (int m = 0; m < numModulators; ++m)
                if (const float amount = owner.routeAmount (m, t); amount != 0.0f && owner.modEnabled[(size_t) m] && ! owner.grainSource[(size_t) m])
                    offset += amount * owner.shapedFor (m, t, perVoice[(size_t) m] ? values[(size_t) v][(size_t) m] : owner.globalModValues[(size_t) m]);
            const float moved = limit (0.0f, 1.0f, base01[(size_t) t] + offset);
            scratch[(size_t) t] = sp::from01 (owner.targets[(size_t) t], moved);
            owner.voiceModulated01[(size_t) v][(size_t) t].store (moved, std::memory_order_relaxed);
        }
        owner.voiceShown[(size_t) v].store (true, std::memory_order_relaxed);
        params[(size_t) v] = owner.readParams (scratch.data());
    }

    Instrument& owner;
    static constexpr int voicesN = Engine::numVoices;
    std::array<std::array<Modulator, numModulators>, voicesN> mods;
    std::array<ModContext, voicesN> context {};
    std::array<std::array<float, numModulators>, voicesN> values {};
    std::array<GrainParams, voicesN> params {};
    std::array<bool, voicesN> held {}, started {};
    std::array<float, voicesN> spreadOffset {};   // SPREAD: where this voice's note sits round the cycle
    int notesInPhrase = 0;
    std::array<bool, numModulators> perVoice {};
    std::array<float, maxTargets> base01 {}, scratch {};
    bool active = false;
};

// --- set-up ---------------------------------------------------------------------------

Instrument::Instrument (bool isInstrument)
    : instrument (isInstrument),
      voiceModulation (std::make_unique<VoiceModulation> (*this))
{
    targetOf.fill (-1);
    for (int id = 0; id < sp::count; ++id)
    {
        // As a host holds it (Pitch's 0 is −5.4e-7 st in the plug-in too).
        own[(size_t) id].store (sp::hostDefault (id, instrument));
        source[(size_t) id] = &own[(size_t) id];
    }

    // Modulation targets: continuous and stepped parameters, in parameter order.
    // Switch-like ones (choices and switches) aren't, except the divisions (a
    // stepped choice drawn as a knob), nor the seed, MACRO knobs or sample gain.
    for (int id = 0; id < sp::count; ++id)
    {
        if (! sp::inBuild (id, instrument))
            continue;
        const auto& p = sp::info (id);
        const std::string_view name = p.id;
        const bool macro = name.size() > 1 && name[0] == 'm' && name[1] >= '0' && name[1] <= '9' && name.ends_with ("Macro");
        const bool switchLike = p.kind == sp::Kind::choice || p.kind == sp::Kind::boolean;
        const bool division = id == sp::sizeDiv || id == sp::rateDiv || id == sp::delayDiv || id == sp::spaceDiv || id == sp::scanDiv;
        if ((switchLike && ! division) || macro || id == sp::patternSeed || id == sp::sampleGain)
            continue;
        if (targets.size() >= (size_t) maxTargets)
            break;   // raise maxTargets; past it, parameters simply aren't targets
        targetOf[(size_t) id] = (int) targets.size();
        targets.push_back (id);
    }

    // Grain parameters: the ones voices read, so per-voice modulation reaches them.
    for (const int id : { sp::pitch, sp::size, sp::rate, sp::spray, sp::jitter, sp::reverse, sp::randAmp, sp::randPan, sp::fade, sp::slant, sp::ease,
                          sp::grains, sp::grainSize, sp::start, sp::length, sp::scan, sp::attack, sp::decay, sp::sustain, sp::release, sp::root,
                          sp::randPitch, sp::patternDepth, sp::patternSteps, sp::heads, sp::headSpread, sp::liveDelay, sp::feedback })
        if (const int t = targetFor (id); t >= 0)
            voiceTargets.push_back (t);

    // What a PER GRAIN modulator reaches: the settings a grain takes up as it starts.
    {
        using GP = GrainParams;
        const std::tuple<int, double GP::*, double> scattered[] {
            { sp::pitch, &GP::pitchSemitones, 1.0 }, { sp::size, &GP::sizeMs, 1.0 }, { sp::grainSize, &GP::sizeMs, 1.0 }, { sp::rate, &GP::rateHz, 1.0 },
            { sp::spray, &GP::spray, 0.01 }, { sp::reverse, &GP::reverseChance, 0.01 }, { sp::randAmp, &GP::randomAmp, 0.01 }, { sp::randPan, &GP::randomPan, 0.01 },
            { sp::fade, &GP::fade, 0.01 }, { sp::slant, &GP::slant, 0.01 }, { sp::ease, &GP::ease, 0.01 }, { sp::randPitch, &GP::randomPitch, 1.0 },
            { sp::patternDepth, &GP::patternDepth, 1.0 }, { sp::headSpread, &GP::headSpread, 0.01 }, { sp::liveDelay, &GP::liveDelayMs, 1.0 },
            { sp::resPosition, &GP::strikePosition, 0.01 } };
        for (const auto& [id, field, scale] : scattered)
            if (const int t = targetFor (id); t >= 0)
                scatterTargets.push_back ({ t, field, scale });
    }

    for (int m = 0; m < numModulators; ++m)
    {
        const auto id = [m] (const char* suffix) { return sp::find ("m" + std::to_string (m + 1) + suffix); };
        modulatorIds[(size_t) m] = { id ("Type"), id ("Rate"), id ("Sync"), id ("Div"), id ("Shape"), id ("Phase"), id ("Retrig"), id ("Smooth"), id ("Jitter"),
                                     id ("Attack"), id ("Decay"), id ("Sustain"), id ("Release"), id ("Repeat"), id ("FAttack"), id ("FRelease"), id ("Gain"), id ("Source"),
                                     id ("Enable"), id ("Voice"), id ("Macro"), id ("Sym"), id ("Curve"), id ("Rev"), id ("Spread"), id ("GrainSpread") };
    }

    rebuildReads();
    syncModulated();
    for (auto& v : voicePlayheads)
        v.store (-1.0f);
}

Instrument::~Instrument() = default;

void Instrument::rebuildReads() noexcept
{
    for (int id = 0; id < sp::count; ++id)
    {
        const int t = targetOf[(size_t) id];
        read[(size_t) id] = t >= 0 ? &modulated[(size_t) t] : source[(size_t) id];
    }
}

void Instrument::bindParameter (int id, std::atomic<float>* where) noexcept
{
    if (id < 0 || id >= sp::count)
        return;
    source[(size_t) id] = where != nullptr ? where : &own[(size_t) id];
    rebuildReads();
}

void Instrument::setParameter (int id, float value) noexcept
{
    if (id >= 0 && id < sp::count)
        own[(size_t) id].store (value, std::memory_order_relaxed);
}

float Instrument::parameter (int id) const noexcept
{
    return source[(size_t) limit (0, sp::count - 1, id)]->load (std::memory_order_relaxed);
}

void Instrument::syncModulated() noexcept
{
    for (size_t t = 0; t < targets.size(); ++t)
        modulated[t].store (source[(size_t) targets[t]]->load());
}

void Instrument::prepare (double rate, int maxBlockSize)
{
    sampleRate = rate;
    engine.prepare (rate, std::max (1, maxBlockSize));
    voiceModulation->reset();
    engine.setVoiceParams (voiceModulation.get());
    chainNow = limit (0, numChainOrders - 1, (int) v (sp::chainOrder));
    chainFade = 1.0f;
    heldKeys.fill (false);
    sustainedKeys.fill (false);
    sustainDown = false;
    for (int m = 0; m < numModules; ++m)
        moduleLevel[(size_t) m] = v (moduleOnIds[m]) > 0.5f ? 1.0f : 0.0f;
    resonator.prepare (rate);
    filter.prepare (rate);
    tapeInput.prepare (rate);
    color.prepare (rate);
    space.prepare (rate);
    for (size_t m = 0; m < modulators.size(); ++m)
        modulators[m].reset (0x5117 + m);
    modContext = {};
    lastNote = -1;
    followRoot = -1;
    followChordSize = 0;
    followLetGoIn = 0;
    liveColumn = -1;
    liveColumnPeak = 0.0f;
    for (auto& c : livePeaks)
        c.store (0.0f);
}

// --- parameters ----------------------------------------------------------------------

double Instrument::quartersFor (int id) const noexcept
{
    return sp::quartersFor ((int) v (id));
}

// voiceValues (one voice's modulated values, by target) stands in for the
// shared modulated values when given.
GrainParams Instrument::readParams (const float* voiceValues) const noexcept
{
    const auto val = [this, voiceValues] (int id)
    {
        if (voiceValues != nullptr)
            if (const int t = targetOf[(size_t) id]; t >= 0)
                return (double) voiceValues[t];
        return (double) read[(size_t) id]->load (std::memory_order_relaxed);
    };

    GrainParams p;
    p.pitchSemitones = val (sp::pitch);
    p.sizeMs = val (sp::size);
    p.rateHz = val (sp::rate);
    p.spray = val (sp::spray) * 0.01;
    p.jitter = val (sp::jitter) * 0.01;
    p.reverseChance = val (sp::reverse) * 0.01;
    p.randomAmp = val (sp::randAmp) * 0.01;
    p.randomPan = val (sp::randPan) * 0.01;
    p.fade = val (sp::fade) * 0.01;
    p.slant = val (sp::slant) * 0.01;
    p.ease = val (sp::ease) * 0.01;
    p.strikePosition = val (sp::resPosition) * 0.01;
    p.start = val (sp::start) * 0.01;
    p.length = val (sp::length) * 0.01;
    p.scan = val (sp::scan);
    p.endMode = (EndMode) limit (0, 3, (int) val (sp::endMode));
    p.snap = (Snap) limit (0, 2, (int) val (sp::snap));
    p.attackMs = val (sp::attack);
    p.decayMs = val (sp::decay);
    p.sustain = val (sp::sustain) * 0.01;
    p.releaseMs = val (sp::release);
    p.rootNote = (int) val (sp::root);

    p.randomPitch = val (sp::randPitch);
    p.scale = (Scale) limit (0, scaleCount - 1, (int) val (sp::scale));
    p.key = limit (0, 11, (int) val (sp::key));
    p.patternDepth = val (sp::patternDepth);
    p.patternSteps = (int) val (sp::patternSteps);
    p.patternSeed = (std::uint32_t) val (sp::patternSeed);
    p.heads = limit (1, 8, (int) val (sp::heads));
    p.headSpread = val (sp::headSpread) * 0.01;

    // PITCH, LIVE, FEED, SLICE (Silt FX: LIVE).
    const int mode = instrument ? limit (0, 3, roundToInt ((float) val (sp::keyMode))) : (int) modeLive;
    p.live = mode == 1 || mode == 2;
    p.selfFeed = mode == 2;
    p.slice = mode == 3;
    p.sliceTransients = (int) val (sp::sliceMode) == 1;
    p.sliceCount = (int) val (sp::sliceCount);
    if (p.slice && (int) val (sp::sliceMode) == 2)
        if (const int n = sliceCountManual.load (std::memory_order_acquire); n > 0)
        {
            for (int i = 0; i < n; ++i)
                sliceScratch[(size_t) i] = sliceTable[(size_t) i].load (std::memory_order_relaxed);
            p.sliceTable = sliceScratch.data();
            p.sliceTableSize = n;
        }
    p.feedback = val (sp::feedback) * 0.01;
    p.freeze = val (sp::freeze) > 0.5;
    p.liveTrigger = (LiveTrigger) limit (0, 2, (int) val (sp::liveRun));

    // Synced values follow the host tempo (the last one seen while stopped).
    const double msPerQuarter = 60000.0 / limit (20.0, 400.0, hostBpm.load (std::memory_order_relaxed));
    p.liveDelayMs = val (sp::delaySync) > 0.5 ? limit (10.0, maxDelayMs, quartersFor (sp::delayDiv) * msPerQuarter)
                                               : val (sp::liveDelay);
    if (val (sp::sizeSync) > 0.5)
        p.sizeMs = limit (10.0, maxSyncedSizeMs, quartersFor (sp::sizeDiv) * msPerQuarter);
    switch ((int) val (sp::rateMode))
    {
        case rateModeSync:
            p.gridDivision = quartersFor (sp::rateDiv);
            p.rateHz = 1000.0 / (p.gridDivision * msPerQuarter);
            break;
        case rateModeGrains:
            p.grainCount = (int) val (sp::grains);
            if (val (sp::sizeSync) < 0.5)
                p.sizeMs = val (sp::grainSize);
            break;
        default:
            break;
    }
    if (val (sp::scanSync) > 0.5 && ! p.slice && ! p.live)
    {
        const double quartersPerBar = timeSigNumerator.load (std::memory_order_relaxed) * 4.0 / std::max (1, timeSigDenominator.load (std::memory_order_relaxed));
        const double perQuarter = 60.0 / limit (20.0, 400.0, hostBpm.load (std::memory_order_relaxed));
        p.scanPassSeconds = sp::scanBars[(size_t) limit (0, (int) std::size (sp::scanBars) - 1, roundToInt (val (sp::scanDiv)))] * quartersPerBar * perQuarter;
    }
    p.scatter = grainScatter.count > 0 ? &grainScatter : nullptr;
    return p;
}

// The bands use the PITCH page's scale and key; FOLLOW tunes the lowest band to
// the last note played (until one is, TUNE).
ResonatorParams Instrument::readResonatorParams (const GrainParams& grain) noexcept
{
    ResonatorParams p;
    const bool follow = v (sp::resFollow) > 0.5f && followRoot >= 0;
    // SHIFT: every note FOLLOW is ringing, moved together without leaving the key.
    const int shiftBy = roundToInt (v (sp::resShift));
    const auto inKey = [&] (int note) { return shiftInKey (note, shiftBy, grain.scale, grain.key); };
    const int shiftedRoot = follow ? inKey (followRoot) : 0;
    p.rootNote = follow ? (float) shiftedRoot : v (sp::resTune);
    resonatorRootNow.store (p.rootNote, std::memory_order_relaxed);
    p.notes = (ResonatorNotes) limit (0, 3, (int) v (sp::resNotes));
    if (follow)
    {
        for (int c = 0; c < followChordSize; ++c)
            p.chord[(size_t) c] = (float) (inKey (followRoot + roundToInt (followChord[(size_t) c])) - shiftedRoot);
        p.chordSize = followChordSize;
    }
    for (int c = 0; c < p.chordSize; ++c)
        resonatorChordNow[(size_t) c].store (p.chord[(size_t) c], std::memory_order_relaxed);
    resonatorChordSizeNow.store (p.chordSize, std::memory_order_relaxed);

    // SAMPLE: the sample's pitches, moved by TUNE from its default (C2), or
    // with FOLLOW by the note played against ROOT (as the grains are).
    const float shift = follow ? (float) shiftedRoot - v (sp::root) : v (sp::resTune) - 48.0f;
    const float ratio = std::pow (2.0f, shift / 12.0f);
    p.sampleCount = limit (0, (int) p.sampleHz.size(), samplePitchCount.load (std::memory_order_relaxed));
    for (int i = 0; i < p.sampleCount; ++i)
    {
        p.sampleHz[(size_t) i] = samplePitchHz[(size_t) i].load (std::memory_order_relaxed) * ratio;
        p.sampleLevel[(size_t) i] = samplePitchLevel[(size_t) i].load (std::memory_order_relaxed);
    }
    p.scale = grain.scale;
    p.key = grain.key;
    p.rangeOctaves = v (sp::resRange);
    p.decaySeconds = v (sp::resDecay);
    p.bright = v (sp::resBright) * 0.01f;
    p.width = v (sp::resWidth) * 0.01f;
    p.mix = v (sp::resMix) * 0.01f;
    p.resonance = v (sp::resReso) * 0.01f;
    p.bandCount = roundToInt (v (sp::resBands));
    p.drift = v (sp::resDrift) * 0.01f;
    // One knob for both: more DRIFT wanders further and a little faster (0.15 to 3 Hz).
    p.driftRate = 0.15f * std::pow (20.0f, p.drift);
    p.spread = v (sp::resSpread) * 0.01f;
    p.oddEven = v (sp::resOddEven) * 0.01f;
    p.drive = v (sp::resDrive) * 0.01f;
    p.damp = v (sp::resDamp) * 0.01f;
    p.stretch = v (sp::resStretch) * 0.01f;
    p.stretchCurve = v (sp::resCurve) * 0.01f;
    p.snap = v (sp::resSnap) * 0.01f;
    p.offset = v (sp::resOffset);
    p.ripple = v (sp::resRipple);
    p.rippleDepth = v (sp::resDepth) * 0.01f;
    p.sweep = v (sp::resSweep) * 0.01f;
    p.strike = v (sp::resStrike) * 0.01f;
    p.hardness = v (sp::resHardness) * 0.01f;
    p.particles = v (sp::resParticles) * 0.01f;
    p.particleDensity = v (sp::resDensity);
    p.top = v (sp::resTop) * 0.01f;
    p.saturate = v (sp::resSaturate) * 0.01f;
    return p;
}

bool Instrument::filterIsOpen (float cutoffHz, float resonance, float morph, float drive, float env, float key, float mix) noexcept
{
    if (mix <= 0.0f)
        return true;
    const bool still = resonance <= 0.0f && drive <= 0.0f && std::abs (env) < 0.001f && std::abs (key) < 0.001f;
    return still && ((morph <= 0.001f && cutoffHz >= 19900.0f) || (morph >= 1.999f && cutoffHz <= 21.0f));
}

// KEY tracks the last note played around middle C.
FilterParams Instrument::readFilterParams() const noexcept
{
    FilterParams p;
    p.slope = (int) v (sp::filterMode) == 1 ? FilterSlope::db24 : FilterSlope::db12;
    p.cutoffHz = v (sp::filterCutoff);
    p.resonance = v (sp::filterReso) * 0.01f;
    p.morph = v (sp::filterMorph);
    p.drive = v (sp::filterDrive) * 0.01f;
    p.envAmount = v (sp::filterEnv) * 0.01f;
    p.keyOctaves = lastNote >= 0 ? v (sp::filterKey) * 0.01f * (float) (lastNote - 60) / 12.0f : 0.0f;
    p.mix = v (sp::filterMix) * 0.01f;
    // Wide open with nothing to add, it passes the sound through untouched.
    if (filterIsOpen (p.cutoffHz, p.resonance, p.morph, p.drive, p.envAmount, v (sp::filterKey) * 0.01f, p.mix))
        p.slope = FilterSlope::off;
    return p;
}

ColorParams Instrument::readColorParams() const noexcept
{
    ColorParams p;
    p.drive = v (sp::colorDrive) * 0.01f;
    p.tone = v (sp::colorTone) * 0.01f;
    p.bits = v (sp::colorBits);
    p.rateHz = v (sp::colorRate);
    p.comp = v (sp::colorComp) * 0.01f;
    p.noise = v (sp::colorNoise) * 0.01f;
    p.wow = v (sp::colorWow) * 0.01f;
    p.mix = v (sp::colorMix) * 0.01f;
    return p;
}

// A synced time follows the host tempo (the last one seen while stopped).
SpaceParams Instrument::readSpaceParams() const noexcept
{
    const double msPerQuarter = 60000.0 / limit (20.0, 400.0, hostBpm.load (std::memory_order_relaxed));
    SpaceParams p;
    p.delayMs = v (sp::spaceSync) > 0.5f ? (float) limit (10.0, Space::maxDelaySeconds * 1000.0, quartersFor (sp::spaceDiv) * msPerQuarter)
                                          : v (sp::spaceTime);
    p.feedback = v (sp::spaceFeedback) * 0.01f;
    p.delayLevel = v (sp::spaceDelay) * 0.01f;
    p.pingPong = (int) v (sp::spaceMode) == 0;
    p.size = v (sp::spaceSize) * 0.01f;
    p.decaySeconds = v (sp::spaceDecay);
    p.tone = v (sp::spaceTone) * 0.01f;
    p.shimmer = v (sp::spaceShimmer) * 0.01f;
    p.reverbLevel = v (sp::spaceReverb) * 0.01f;
    p.freeze = v (sp::spaceFreeze) > 0.5f;
    static constexpr float duckAmounts[] { 0.0f, 0.35f, 0.65f, 1.0f };
    p.duck = duckAmounts[limit (0, 3, roundToInt (v (sp::spaceDuck)))];
    return p;
}

// --- modulation ----------------------------------------------------------------------

ModulatorParams Instrument::readModulatorParams (int m) const noexcept
{
    const auto& r = modulatorIds[(size_t) m];
    ModulatorParams p;
    p.type = (ModType) limit (0, 6, (int) v (r.type));
    p.rateHz = v (r.rate);
    p.sync = v (r.sync) > 0.5f;
    p.reverse = v (r.rev) > 0.5f;
    p.syncQuarters = quartersFor (r.div);
    p.shape = v (r.shape);
    p.phase = v (r.phase) / 360.0f;
    p.retrigger = v (r.retrig) > 0.5f;
    p.smooth = v (r.smooth) * 0.01f;
    p.jitter = v (r.jitter) * 0.01f;
    p.attackMs = v (r.attack);
    p.decayMs = v (r.decay);
    p.sustain = v (r.sustain) * 0.01f;
    p.releaseMs = v (r.release);
    p.repeat = v (r.repeat) > 0.5f;
    p.followAttackMs = v (r.followAttack);
    p.followReleaseMs = v (r.followRelease);
    p.gainDb = v (r.gain);
    p.source = (MidiSource) limit (0, 4, (int) v (r.source));
    p.macro = v (r.macro) * 0.01f;
    p.symmetry = v (r.sym) * 0.01f;
    p.curve = v (r.curve) * 0.01f;
    return p;
}

// Steps the modulators over this chunk, then writes every target's modulated
// value: its own value plus each route's amount times its modulator, in the
// parameter's 0–1 range, clamped.
void Instrument::applyModulation() noexcept
{
    std::array<float, numModulators> value {};
    for (int m = 0; m < numModulators; ++m)
    {
        const auto mp = readModulatorParams (m);
        value[(size_t) m] = modulators[(size_t) m].advance (mp, modContext);   // runs muted too, so unmuting picks up in step
        nativeBipolar[(size_t) m] = Modulator::isBipolar (mp);
        modEnabled[(size_t) m] = isModulatorEnabled (m);
        if (! modEnabled[(size_t) m])
            value[(size_t) m] = 0.0f;
        globalModValues[(size_t) m] = value[(size_t) m];
        grainSource[(size_t) m] = mp.type == ModType::grain;
        if (! grainSource[(size_t) m] || ! modEnabled[(size_t) m])   // otherwise it shows the grains' rolls, below
            modOutputs[(size_t) m].store (value[(size_t) m], std::memory_order_relaxed);
        modPhases[(size_t) m].store ((float) modulators[(size_t) m].getPhase(), std::memory_order_relaxed);
        modStages[(size_t) m].store (modulators[(size_t) m].getStage(), std::memory_order_relaxed);
        modStageMs[(size_t) m].store (modulators[(size_t) m].getStageMs(), std::memory_order_relaxed);
        modReleaseLevel[(size_t) m].store (modulators[(size_t) m].getReleaseLevel(), std::memory_order_relaxed);
        modReleasedFrom[(size_t) m].store (modulators[(size_t) m].getReleasedFrom(), std::memory_order_relaxed);
    }

    for (size_t t = 0; t < targets.size(); ++t)
    {
        const float base = source[(size_t) targets[t]]->load (std::memory_order_relaxed);
        float offset = 0.0f;
        for (int m = 0; m < numModulators; ++m)
            if (const float amount = routeAmounts[(size_t) m * maxTargets + t].load (std::memory_order_relaxed); amount != 0.0f && modEnabled[(size_t) m] && ! grainSource[(size_t) m])
                offset += amount * shapedFor (m, (int) t, value[(size_t) m]);
        if (offset == 0.0f)
        {
            modulated[t].store (base, std::memory_order_relaxed);
            continue;
        }
        const float moved = limit (0.0f, 1.0f, sp::to01 (targets[t], base) + offset);
        modulated[t].store (sp::from01 (targets[t], moved), std::memory_order_relaxed);
    }
    buildGrainScatter();
}

// This chunk's PER GRAIN routes, each as how far a whole roll moves its setting,
// in the setting's own units, from where it now sits. It's a route like any other,
// so the same depth covers the same part of the knob's travel.
void Instrument::buildGrainScatter() noexcept
{
    grainScatter.count = 0;
    for (int m = 0; m < numModulators; ++m)
    {
        if (! grainSource[(size_t) m] || ! modEnabled[(size_t) m])
            continue;
        const auto spread = (GrainScatter::Spread) limit (0, 2, (int) v (modulatorIds[(size_t) m].grainSpread));
        for (const auto& target : scatterTargets)
        {
            const float amount = routeAmounts[(size_t) m * maxTargets + (size_t) target.target].load (std::memory_order_relaxed);
            if (amount == 0.0f || grainScatter.count >= GrainScatter::maxItems)
                continue;
            const int id = targets[(size_t) target.target];
            const float now = modulated[(size_t) target.target].load (std::memory_order_relaxed);
            const float now01 = sp::to01 (id, now);
            const float depth = std::abs (amount);
            const double above = (double) sp::from01 (id, std::min (1.0f, now01 + depth)) - now;
            const double below = (double) now - sp::from01 (id, std::max (0.0f, now01 - depth));
            auto& item = grainScatter.items[(size_t) grainScatter.count++];
            item.field = target.field;
            item.up = (amount >= 0.0f ? above : -below) * target.scale;
            item.down = (amount >= 0.0f ? below : -above) * target.scale;
            item.low = sp::from01 (id, 0.0f) * target.scale;
            item.high = sp::from01 (id, 1.0f) * target.scale;
            item.spread = spread;
            item.oneWay = ! routeIsBipolar (m, target.target);
            item.source = m;
        }
    }
}

int Instrument::targetFor (int parameterId) const noexcept
{
    return parameterId >= 0 && parameterId < sp::count ? targetOf[(size_t) parameterId] : -1;
}

float Instrument::routeAmount (int modulator, int target) const noexcept
{
    return routeAmounts[(size_t) modulator * maxTargets + (size_t) target].load (std::memory_order_relaxed);
}

void Instrument::setRouteAmount (int modulator, int target, float amount) noexcept
{
    if (modulator >= 0 && modulator < numModulators && target >= 0 && target < numTargets())
        routeAmounts[(size_t) modulator * maxTargets + (size_t) target].store (limit (-1.0f, 1.0f, amount), std::memory_order_relaxed);
}

void Instrument::setRoutePolarity (int modulator, int target, int polarity) noexcept
{
    if (modulator >= 0 && modulator < numModulators && target >= 0 && target < numTargets())
        routePolarity[(size_t) modulator * maxTargets + (size_t) target].store (limit (0, 2, polarity), std::memory_order_relaxed);
}

int Instrument::routePolarityOf (int modulator, int target) const noexcept
{
    return routePolarity[(size_t) modulator * maxTargets + (size_t) target].load (std::memory_order_relaxed);
}

void Instrument::clearAllRoutes() noexcept
{
    for (auto& amount : routeAmounts)
        amount.store (0.0f);
    for (auto& polarity : routePolarity)
        polarity.store (natural);
}

void Instrument::naturalWhereUnrouted() noexcept
{
    for (size_t i = 0; i < routeAmounts.size(); ++i)
        if (routeAmounts[i].load (std::memory_order_relaxed) == 0.0f)
            routePolarity[i].store (natural, std::memory_order_relaxed);
}

// Bipolar: swings both ways round the knob; unipolar: one way. A route follows
// its source (WAVE and RANDOM bipolar, ENVELOPE and the rest unipolar) unless
// it's been set.
bool Instrument::routeIsBipolar (int modulator, int target) const noexcept
{
    switch (routePolarityOf (modulator, target))
    {
        case unipolar: return false;
        case bipolar:  return true;
        default:       return Modulator::isBipolar (readModulatorParams (modulator));
    }
}

// A modulator's value as this route takes it (the source's native range
// rescaled when the route's polarity differs).
float Instrument::shapedFor (int m, int t, float value) const noexcept
{
    const int polarity = routePolarity[(size_t) m * maxTargets + (size_t) t].load (std::memory_order_relaxed);
    const bool native = nativeBipolar[(size_t) m];
    if (polarity == unipolar && native)
        return 0.5f * (value + 1.0f);
    if (polarity == bipolar && ! native)
        return 2.0f * value - 1.0f;
    return value;
}

// Whether a route from this modulator would do anything: PER GRAIN only reaches
// what a grain takes up as it starts.
bool Instrument::routeCanWork (int modulator, int target) const noexcept
{
    return readModulatorParams (modulator).type != ModType::grain
        || std::any_of (scatterTargets.begin(), scatterTargets.end(), [target] (const ScatterTarget& s) { return s.target == target; });
}

bool Instrument::isModulatorEnabled (int modulator) const noexcept
{
    return v (modulatorIds[(size_t) modulator].enable) > 0.5f;
}

bool Instrument::isModulatorPerVoice (int m) const noexcept
{
    return v (modulatorIds[(size_t) m].voice) > 0.5f && readModulatorParams (m).type != ModType::grain;
}

bool Instrument::isVoiceTarget (int target) const noexcept
{
    return std::find (voiceTargets.begin(), voiceTargets.end(), target) != voiceTargets.end();
}

float Instrument::modulatedValue01 (int target) const noexcept
{
    return sp::to01 (targets[(size_t) target], modulated[(size_t) target].load (std::memory_order_relaxed));
}

// --- notes ---------------------------------------------------------------------------

// MIDI on any channel. The sustain pedal (CC 64) holds back note-offs until it's
// let up; all-notes-off drops what came before it in the block. Other controllers
// are the host's (LEARN), except the mod wheel, which the modulators hear.
void Instrument::handleMidi (const MidiEvent& e) noexcept
{
    const auto addNote = [this] (int at, int note, float velocity, bool on)
    {
        if (noteCount < (int) noteEvents.size())
            noteEvents[(size_t) noteCount++] = { at, note, velocity, on };
    };
    const std::uint8_t status = e.bytes[0] & 0xf0;
    const int note = e.bytes[1] & 127;
    const float velocity = (float) e.bytes[2] * (1.0f / 127.0f);

    if (status == 0x90 && e.bytes[2] != 0)
    {
        sustainedKeys[(size_t) note] = false;   // down again: no longer only held by the pedal
        addNote (e.offset, note, velocity, true);
    }
    else if (status == 0x80 || status == 0x90)
    {
        if (sustainDown)
            sustainedKeys[(size_t) note] = true;
        else
            addNote (e.offset, note, velocity, false);
    }
    else if (status == 0xb0 && (e.bytes[1] == 123 || e.bytes[1] == 120))
    {
        allOff = true;
        noteCount = 0;   // the notes before it in this block end with it
        sustainedKeys.fill (false);
    }
    else if (status == 0xb0 && e.bytes[1] == 64)
    {
        sustainDown = e.bytes[2] >= 64;
        if (! sustainDown)
            for (int n = 0; n < 128; ++n)
                if (std::exchange (sustainedKeys[(size_t) n], false))
                    addNote (e.offset, n, 0.0f, false);
    }
    else if (status == 0xb0)
    {
        if (e.bytes[1] == 1)
            modContext.modWheel = (float) e.bytes[2] / 127.0f;
    }
    else if (status == 0xd0)
        modContext.aftertouch = (float) e.bytes[1] / 127.0f;
    else if (status == 0xa0)
        modContext.aftertouch = (float) e.bytes[2] / 127.0f;
    else if (status == 0xe0)
        modContext.pitchBend = limit (-1.0f, 1.0f, (float) ((e.bytes[1] | (e.bytes[2] << 7)) - 8192) / 8191.0f);
}

// --- the block -----------------------------------------------------------------------

void Instrument::process (float* outLeft, float* outRight, int numSamples,
                          const float* const* input, int numInputChannels,
                          const MidiEvent* events, int numEvents, const Transport& transport) noexcept
{
    if (transport.known)
    {
        if (transport.hasBpm)
            hostBpm.store (transport.bpm, std::memory_order_relaxed);
        if (transport.hasSignature)
        {
            timeSigNumerator.store (transport.numerator, std::memory_order_relaxed);
            timeSigDenominator.store (transport.denominator, std::memory_order_relaxed);
        }
        hostPlaying.store (transport.playing, std::memory_order_relaxed);
    }

    noteCount = 0;
    allOff = false;
    for (int i = 0; i < numEvents; ++i)
        handleMidi (events[i]);
    if (allOff)
    {
        engine.allNotesOff();
        heldKeys.fill (false);
        modContext.heldNotes = 0;
    }

    const int inChannels = input != nullptr ? numInputChannels : 0;
    const auto in = [input] (int ch, int at) { return input[ch] + at; };

    // Rendered in chunks of up to 64 samples: modulation updates per chunk and
    // every parameter is read after it.
    const bool playing = transport.known && transport.playing;
    const double blockPpq = transport.known ? transport.ppq : 0.0;
    const double bpm = transport.known && transport.hasBpm ? transport.bpm : hostBpm.load (std::memory_order_relaxed);
    const double ppqPerSample = bpm / 60.0 / sampleRate;

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int len = std::min (chunkSize, numSamples - start);

        // Events for this chunk, shifted to its start; they also feed the modulators.
        auto* first = std::find_if (noteEvents.data(), noteEvents.data() + noteCount, [start] (const auto& e) { return e.sampleOffset >= start; });
        auto* last = std::find_if (first, noteEvents.data() + noteCount, [start, len] (const auto& e) { return e.sampleOffset >= start + len; });
        modContext.noteStarted = false;
        for (auto* it = first; it != last; ++it)
        {
            it->sampleOffset -= start;
            // Held keys as a set, so a repeated note-on for a key already down
            // (arps, clip retriggers) can't leave the count stuck above zero.
            auto& held = heldKeys[(size_t) limit (0, 127, it->note)];
            if (it->isNoteOn)
            {
                lastNote = it->note;
                modContext.noteStarted = true;
                spaceNoteStarted = true;
                modContext.note = it->note;
                modContext.velocity = it->velocity;
                if (! held)
                    ++modContext.heldNotes;
                held = true;
            }
            else if (held)
            {
                held = false;
                modContext.heldNotes = std::max (0, modContext.heldNotes - 1);
            }
        }
        // FOLLOW's chord: the held notes, lowest first. A new note changes it at once, and it's
        // kept after release so the bands ring out. Fingers never lift quite together, so a
        // note let go only leaves the chord if others are still down a moment later.
        const auto followHeldNotes = [this]
        {
            followRoot = -1;
            followChordSize = 0;
            for (int note = 0; note < 128; ++note)
                if (heldKeys[(size_t) note])
                {
                    if (followRoot < 0)
                        followRoot = note;
                    else if (followChordSize < (int) followChord.size())
                        followChord[(size_t) followChordSize++] = (float) (note - followRoot);
                }
        };
        if (first != last)
        {
            if (modContext.heldNotes == 0)
                followLetGoIn = 0;
            else if (modContext.noteStarted)
            {
                followLetGoIn = 0;
                followHeldNotes();
            }
            else
                followLetGoIn = (int) (followLetGoSeconds * sampleRate);
        }
        else if (followLetGoIn > 0)
        {
            followLetGoIn -= len;
            if (followLetGoIn <= 0)
                followHeldNotes();
        }
        float peak = 0.0f;
        for (int ch = 0; ch < inChannels; ++ch)
            peak = std::max (peak, magnitude (in (ch, start), len));

        // Silt FX's INPUT: the grains hear the track through it, ramped across the
        // chunk so moving it doesn't click. The dry signal doesn't go through it.
        const float* inLeft = inChannels > 0 ? in (0, start) : nullptr;
        const float* inRight = inChannels > 1 ? in (1, start) : inLeft;
        if (! instrument)
        {
            const float target = decibelsToGain (v (sp::inputGain));
            if (inLeft != nullptr && ! (target == inputGainNow && target == 1.0f))
            {
                const float step = (target - inputGainNow) / (float) len;
                for (int i = 0; i < len; ++i)
                {
                    const float g = inputGainNow + step * (float) i;
                    inputScaled[0][(size_t) i] = inLeft[i] * g;
                    inputScaled[1][(size_t) i] = inRight[i] * g;
                }
                inLeft = inputScaled[0].data();
                inRight = inputScaled[1].data();
            }
            peak *= std::max (target, inputGainNow);
            // Past 0 dB the track meets the tape rather than peaking.
            if (inLeft == inputScaled[0].data())
                tapeInput.process (inputScaled[0].data(), inputScaled[1].data(), len, gainToDecibels (inputGainNow, -100.0f), gainToDecibels (target, -100.0f));
            inputGainNow = target;
        }
        modContext.inputPeak = peak;
        modContext.numSamples = len;
        modContext.sampleRate = sampleRate;
        modContext.hostPlaying = playing;
        modContext.bpm = bpm;
        modContext.ppqEnd = blockPpq + (start + len) * ppqPerSample;
        applyModulation();
        voiceModulation->prepareChunk();

        const float gain = decibelsToGain (v (sp::gain), -48.0f);
        const bool monitoring = instrument && v (sp::monitor) > 0.5f;
        auto grainParams = readParams();
        const auto resonatorParams = readResonatorParams (grainParams);
        const auto filterParams = readFilterParams();
        const auto colorParams = readColorParams();
        const auto spaceParams = readSpaceParams();

        // Synced Rate locks grains to the beat grid while the host plays; stopped,
        // they free-run at the synced rate.
        if ((int) v (sp::rateMode) == rateModeSync && playing && bpm > 0.0)
        {
            grainParams.gridSync = true;
            grainParams.ppqPerSample = ppqPerSample;
            grainParams.gridPpq = blockPpq + start * ppqPerSample;
        }
        lastLive = grainParams.live;
        delayForced.store (grainParams.live && grainParams.liveDelayMs < grainParams.sizeMs * std::max (1.0, std::pow (2.0, grainParams.pitchSemitones / 12.0)), std::memory_order_relaxed);

        float* wl = wet[0].data();
        float* wr = wet[1].data();
        const int liveWriteBefore = engine.getLiveWritePosition();
        engine.process (wl, wr, len, inLeft, inRight, first, (int) (last - first), grainParams);
        if (grainParams.live && grainParams.selfFeed && ! lastSelfFeed)
            refreshAllLivePeaks();   // FEED just seeded the whole ring: the screen's picture of it too
        else if (grainParams.live && ! grainParams.freeze)
            trackLivePeaks (liveWriteBefore, len);
        lastSelfFeed = grainParams.live && grainParams.selfFeed;

        // Sample gain, ramped across the chunk so moving it doesn't click.
        {
            const float target = instrument ? decibelsToGain (v (sp::sampleGain)) : 1.0f;
            if (target != sampleGainNow || target != 1.0f)
            {
                applyGainRamp (wl, len, sampleGainNow, target);
                applyGainRamp (wr, len, sampleGainNow, target);
            }
            sampleGainNow = target;
        }

        // MIX: the dry input joins the grains here, so every module works on both. The
        // instrument only has a dry side in LIVE and FEED.
        {
            const float target = grainParams.live ? v (sp::mix) * 0.01f : 1.0f;
            if (target != mixNow || target != 1.0f)
            {
                const float step = (target - mixNow) / (float) len;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* dry = inChannels > 0 ? in (std::min (ch, inChannels - 1), start) : nullptr;
                    float* out = wet[(size_t) ch].data();
                    for (int i = 0; i < len; ++i)
                    {
                        const float g = mixNow + step * (float) (i + 1);
                        out[i] = out[i] * g + (dry != nullptr ? dry[i] * (1.0f - g) : 0.0f);
                    }
                }
            }
            mixNow = target;
        }

        // The modules, in the CHAIN order. A new order fades the sound down,
        // switches and fades back up (about 5 ms each way), so it doesn't click.
        const int wantedChain = limit (0, numChainOrders - 1, (int) v (sp::chainOrder));
        const float fadeStep = (float) len / (float) (0.005 * sampleRate);
        float fadeTo = chainFade;
        if (wantedChain != chainNow)
            fadeTo = std::max (0.0f, chainFade - fadeStep);
        else if (chainFade < 1.0f)
            fadeTo = std::min (1.0f, chainFade + fadeStep);

        const int tapped = scopeModule.load (std::memory_order_relaxed);
        for (const int module : chainOrders()[(size_t) chainNow])
        {
            float* l = wl;
            float* r = wr;
            if (module == tapped)
                writeScope (scopeIn, l, r, len);

            // Its ON switch: fade towards on or off; fully off, skip it.
            auto& level = moduleLevel[(size_t) module];
            const float wantLevel = v (moduleOnIds[module]) > 0.5f ? 1.0f : 0.0f;
            const float levelFrom = level;
            level = wantLevel > level ? std::min (wantLevel, level + fadeStep) : std::max (wantLevel, level - fadeStep);
            const bool off = levelFrom <= 0.0f && level <= 0.0f;
            const bool fading = ! off && (levelFrom < 1.0f || level < 1.0f);
            std::array<float, chunkSize> dryL, dryR;
            std::copy (l, l + len, dryL.begin());
            std::copy (r, r + len, dryR.begin());
            if (! off)
            switch (module)
            {
                case moduleResonator: resonator.process (l, r, len, resonatorParams, engine.getStrikes(), engine.getStrikeCount()); break;
                case moduleFilter:    filter.process (l, r, len, filterParams); break;
                case moduleColor:     color.process (l, r, len, colorParams); break;
                default:
                {
                    // Measure what SPACE adds: its output less what went in.
                    space.process (l, r, len, spaceParams);
                    if (spaceNoteStarted)
                    {
                        spaceTraceCount.store (0, std::memory_order_relaxed);
                        spaceTraceFill = 0;
                        spaceTracePeak = 0.0f;
                        spaceNoteStarted = false;
                    }
                    for (int i = 0; i < len; ++i)
                    {
                        spaceTracePeak = std::max ({ spaceTracePeak, std::abs (l[i] - dryL[(size_t) i]), std::abs (r[i] - dryR[(size_t) i]) });
                        if (++spaceTraceFill == spaceTraceStep)
                        {
                            const unsigned at = spaceTraceCount.load (std::memory_order_relaxed);
                            spaceTrace[at & (spaceTraceSize - 1)].store (spaceTracePeak, std::memory_order_relaxed);
                            spaceTraceCount.store (at + 1, std::memory_order_release);
                            spaceTraceFill = 0;
                            spaceTracePeak = 0.0f;
                        }
                    }
                    break;
                }
            }
            if (fading)
                for (int i = 0; i < len; ++i)
                {
                    const float g = levelFrom + (level - levelFrom) * (float) (i + 1) / (float) len;
                    l[i] = dryL[(size_t) i] + g * (l[i] - dryL[(size_t) i]);
                    r[i] = dryR[(size_t) i] + g * (r[i] - dryR[(size_t) i]);
                }
            if (levelFrom > 0.0f && level <= 0.0f)
            {
                // Just switched off: clear it, so it comes back without the old tail.
                switch (module)
                {
                    case moduleResonator: resonator.reset(); break;
                    case moduleFilter:    filter.reset(); break;
                    case moduleColor:     color.reset(); break;
                    default:              space.reset(); break;
                }
            }
            // For the chain's tile and arrow: how much it changed, and what's leaving it.
            {
                float changed = 0.0f, leaving = 0.0f;
                for (int i = 0; i < len; ++i)
                {
                    changed = std::max ({ changed, std::abs (l[i] - dryL[(size_t) i]), std::abs (r[i] - dryR[(size_t) i]) });
                    leaving = std::max ({ leaving, std::abs (l[i]), std::abs (r[i]) });
                }
                auto& change = moduleChangePeak[(size_t) module];
                auto& out = moduleOutputPeak[(size_t) module];
                if (changed > change.load (std::memory_order_relaxed))
                    change.store (changed, std::memory_order_relaxed);
                if (leaving > out.load (std::memory_order_relaxed))
                    out.store (leaving, std::memory_order_relaxed);
            }
            if (module == tapped)
            {
                writeScope (scopeOut, l, r, len);
                scopeWrite.store (scopeWrite.load (std::memory_order_relaxed) + (unsigned) len, std::memory_order_release);
            }
        }

        if (fadeTo != chainFade || chainFade < 1.0f)
        {
            applyGainRamp (wl, len, chainFade, fadeTo);
            applyGainRamp (wr, len, chainFade, fadeTo);
        }
        chainFade = fadeTo;
        if (wantedChain != chainNow && chainFade <= 0.0f)
            chainNow = wantedChain;

        // Sample by sample, reading the input before writing the output: the
        // two can share memory.
        for (int i = 0; i < len; ++i)
        {
            const float inL = inChannels > 0 ? input[0][start + i] : 0.0f;
            const float inR = inChannels > 1 ? input[1][start + i] : inL;
            const float monitorL = monitoring ? inL : 0.0f, monitorR = monitoring ? inR : 0.0f;

            outLeft[start + i] = safetyClip ((monitorL + wl[i]) * gain);
            if (outRight != nullptr)
                outRight[start + i] = safetyClip ((monitorR + wr[i]) * gain);
        }
    }

    activeGrains.store (engine.getActiveGrainCount(), std::memory_order_relaxed);
    liveMode.store (lastLive, std::memory_order_relaxed);
    if (const auto* ring = engine.getLiveBuffer())
        liveWriteShare.store ((float) engine.getLiveWritePosition() / (float) ring->getNumFrames(), std::memory_order_relaxed);

    samplesSinceGrainViews += numSamples;
    if (samplesSinceGrainViews >= (int) (sampleRate / 60.0))
    {
        samplesSinceGrainViews = 0;
        grainViewSequence.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        grainViewCount.store (engine.collectGrainViews (grainViews.data(), maxGrainViews), std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        grainViewSequence.fetch_add (1, std::memory_order_release);
    }
    for (int i = 0; i < Engine::numVoices; ++i)
    {
        const auto& voice = engine.getVoice (i);
        voicePlayheads[(size_t) i].store (voice.isActive() ? voice.getPlayheadPosition() : -1.0f, std::memory_order_relaxed);
    }
    grainsOverLimit.store (engine.getGrainsOverLimit(), std::memory_order_relaxed);
    // A PER GRAIN modulator shows what the grains are really rolling.
    for (int m = 0; m < numModulators; ++m)
        if (grainSource[(size_t) m] && modEnabled[(size_t) m])
            for (int i = 0; i < Engine::numVoices; ++i)
                if (engine.getVoice (i).isActive())
                {
                    modOutputs[(size_t) m].store (engine.getVoice (i).getLastRoll (m), std::memory_order_relaxed);
                    break;
                }
}

// --- displays ------------------------------------------------------------------------

// Mono, into the scope's ring at the current write count.
void Instrument::writeScope (std::array<std::atomic<float>, scopeSize>& ring, const float* l, const float* r, int count) noexcept
{
    const unsigned at = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        ring[(at + (unsigned) i) & (scopeSize - 1)].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
}

unsigned Instrument::readScope (float* inOut, float* out, int count) const noexcept
{
    count = std::min (count, scopeSize / 2);
    const unsigned end = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
    {
        const auto at = (end - (unsigned) (count - i)) & (scopeSize - 1);
        inOut[i] = scopeIn[at].load (std::memory_order_relaxed);
        out[i] = scopeOut[at].load (std::memory_order_relaxed);
    }
    return end;
}

// Once when FEED seeds the ring: every column's peak, a column at a time.
void Instrument::refreshAllLivePeaks() noexcept
{
    const auto* ring = engine.getLiveBuffer();
    if (ring == nullptr)
        return;
    const int frames = ring->getNumFrames();
    for (int c = 0; c < liveColumns; ++c)
    {
        const int from = (int) ((std::int64_t) c * frames / liveColumns);
        const int to = std::max (from + 1, (int) ((std::int64_t) (c + 1) * frames / liveColumns));
        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = ring->getReadPointer (ch) + from;
            float lo = d[0], hi = d[0];
            for (int i = 1; i < to - from; ++i)
            {
                lo = std::min (lo, d[i]);
                hi = std::max (hi, d[i]);
            }
            peak = std::max ({ peak, -lo, hi });
        }
        livePeaks[(size_t) c].store (peak, std::memory_order_relaxed);
    }
    liveColumn = -1;
}

// The peak of each 1/liveColumns of the ring, as it's written, for the screen's
// scrolling view of the live input.
void Instrument::trackLivePeaks (int writeStart, int count) noexcept
{
    const auto* ring = engine.getLiveBuffer();
    if (ring == nullptr)
        return;
    const int frames = ring->getNumFrames();
    const float* l = ring->getReadPointer (0);
    const float* r = ring->getReadPointer (1);
    for (int i = 0; i < count; ++i)
    {
        const int pos = (writeStart + i) % frames;
        const int column = (int) ((std::int64_t) pos * liveColumns / frames);
        if (column != liveColumn)
        {
            if (liveColumn >= 0)
                livePeaks[(size_t) liveColumn].store (liveColumnPeak, std::memory_order_relaxed);
            liveColumn = column;
            liveColumnPeak = 0.0f;
        }
        liveColumnPeak = std::max ({ liveColumnPeak, std::abs (l[pos]), std::abs (r[pos]) });
    }
}

std::array<float, Instrument::liveColumns> Instrument::getLivePeaks() const noexcept
{
    std::array<float, liveColumns> out {};
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = livePeaks[i].load (std::memory_order_relaxed);
    return out;
}

int Instrument::readGrainViews (std::array<GrainView, maxGrainViews>& out) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = grainViewSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        const int n = limit (0, maxGrainViews, grainViewCount.load (std::memory_order_relaxed));
        std::copy (grainViews.begin(), grainViews.begin() + n, out.begin());
        std::atomic_thread_fence (std::memory_order_acquire);
        if (grainViewSequence.load (std::memory_order_relaxed) == before)
            return n;
    }
    return 0;   // the audio thread kept updating; the next frame will catch it
}

} // namespace silt
