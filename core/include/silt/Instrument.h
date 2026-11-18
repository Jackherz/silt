#pragma once

#include <silt/Color.h>
#include <silt/Engine.h>
#include <silt/Filter.h>
#include <silt/Modulator.h>
#include <silt/Params.h>
#include <silt/Resonator.h>
#include <silt/Space.h>
#include <kit/TapeInput.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace silt
{

// One MIDI message (up to three bytes), at a sample within the block.
struct MidiEvent
{
    int offset = 0;
    std::uint8_t bytes[3] {};
};

// What the host says about time this block. Nothing known: the last tempo and
// time signature seen stay, and synced settings follow them.
struct Transport
{
    bool known = false;                  // the host gave a position at all
    bool playing = false;
    double ppq = 0.0;                    // quarter notes at the block's first sample
    bool hasBpm = false;
    double bpm = 120.0;
    bool hasSignature = false;
    int numerator = 4, denominator = 4;
};

// Silt, whole: the engine, the four modules in their CHAIN order, the four
// modulators and their routes, played from MIDI and parameter values. Both
// plug-ins and the browser version run this; it's what processBlock used to do.
// JUCE-free.
//
// Parameters: one value per silt::params id. The host binds each to where it
// keeps its value (the plug-in to its own parameters); anything left unbound
// reads the instrument's own store (setParameter), which is how the browser
// sets them. Modulation targets are read through their modulated copies.
//
// Threads: process() and prepare() on the audio thread; the route setters,
// loading the engine a sample and the displays' atomics from anywhere.
class Instrument
{
public:
    static constexpr int numModulators = 4, maxTargets = 160, chunkSize = 64;
    enum Module { moduleResonator, moduleFilter, moduleColor, moduleSpace, numModules };
    static constexpr int numChainOrders = 24;
    static std::array<int, numModules> chainOrder (int index) noexcept;
    enum PlayMode { modePitch, modeLive, modeFeed, modeSlice };
    enum Polarity { natural = 0, unipolar = 1, bipolar = 2 };

    explicit Instrument (bool isInstrument);   // false: Silt FX (always LIVE, INPUT, no sample gain)
    ~Instrument();

    bool isInstrument() const noexcept { return instrument; }

    // Parameters.
    void bindParameter (int id, std::atomic<float>* source) noexcept;
    void setParameter (int id, float value) noexcept;           // the instrument's own store (unbound ids)
    float parameter (int id) const noexcept;                    // as set, before modulation
    void syncModulated() noexcept;                              // modulated copies to the values as they are now

    void prepare (double sampleRate, int maxBlockSize);
    // Renders numSamples into outLeft/outRight. input (may be null, or have one
    // channel) is the live input: the sidechain, Silt FX's track, the browser's
    // microphone. Output may share memory with input. Events sorted by offset.
    void process (float* outLeft, float* outRight, int numSamples,
                  const float* const* input, int numInputChannels,
                  const MidiEvent* events, int numEvents, const Transport&) noexcept;

    Engine& getEngine() noexcept { return engine; }
    const Engine& getEngine() const noexcept { return engine; }
    double getSampleRate() const noexcept { return sampleRate; }

    // Modulation targets: the continuous and stepped parameters (see the plug-in's
    // header for which and why), in parameter order.
    int numTargets() const noexcept { return (int) targets.size(); }
    int targetParameter (int target) const noexcept { return targets[(size_t) target]; }   // its silt::params id
    int targetFor (int parameterId) const noexcept;             // −1: not a target
    float routeAmount (int modulator, int target) const noexcept;
    void setRouteAmount (int modulator, int target, float amount) noexcept;
    void setRoutePolarity (int modulator, int target, int polarity) noexcept;
    int routePolarityOf (int modulator, int target) const noexcept;
    bool routeIsBipolar (int modulator, int target) const noexcept;
    void clearAllRoutes() noexcept;
    bool routeCanWork (int modulator, int target) const noexcept;
    bool isModulatorEnabled (int modulator) const noexcept;
    bool isModulatorPerVoice (int modulator) const noexcept;
    bool isVoiceTarget (int target) const noexcept;
    float value (int id) const noexcept { return v (id); }   // as the sound reads it (modulated where it's a target)
    void naturalWhereUnrouted() noexcept;                       // a route with no depth goes back to following its source
    float modulatedValue (int target) const noexcept { return modulated[(size_t) target].load (std::memory_order_relaxed); }
    float modulatedValue01 (int target) const noexcept;
    ModulatorParams readModulatorParams (int modulator) const noexcept;

    // True when the filter would change nothing: all the way open (or a high pass all the
    // way down) with nothing moving it, or MIX at 0.
    static bool filterIsOpen (float cutoffHz, float resonance, float morph, float drive, float env, float key, float mix) noexcept;

    // For the displays (written on the audio thread).
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> timeSigNumerator { 4 }, timeSigDenominator { 4 };
    std::array<std::atomic<float>, numModules> moduleChangePeak {}, moduleOutputPeak {};
    std::atomic<int> activeGrains { 0 };
    std::atomic<std::uint64_t> grainsOverLimit { 0 };
    std::array<std::atomic<float>, Engine::numVoices> voicePlayheads {};   // < 0: voice silent
    std::atomic<bool> liveMode { false };
    std::atomic<float> liveWriteShare { 0.0f };
    std::atomic<bool> delayForced { false };
    std::atomic<float> resonatorRootNow { 48.0f };
    std::array<std::atomic<float>, 8> resonatorChordNow {};
    std::atomic<int> resonatorChordSizeNow { 0 };
    std::array<std::atomic<float>, numModulators> modOutputs {}, modPhases {}, modStageMs {}, modReleaseLevel {};
    std::array<std::atomic<int>, numModulators> modStages {}, modReleasedFrom {};
    std::array<std::atomic<bool>, Engine::numVoices> voiceShown {};
    std::array<std::array<std::atomic<float>, maxTargets>, Engine::numVoices> voiceModulated01 {};

    // SPACE's trace: what SPACE adds, one peak per spaceTraceStep samples from the latest note.
    static constexpr int spaceTraceSize = 8192, spaceTraceStep = 64;
    std::atomic<unsigned> spaceTraceCount { 0 };
    float spaceTraceAt (unsigned index) const noexcept { return spaceTrace[index & (spaceTraceSize - 1)].load (std::memory_order_relaxed); }

    // The scope: one module's input and output, mono. scopeModule −1: none.
    static constexpr int scopeSize = 8192;
    std::atomic<int> scopeModule { -1 };
    unsigned readScope (float* in, float* out, int count) const noexcept;

    // Live mode: the ring's peaks by position.
    static constexpr int liveColumns = 1024;
    std::array<float, liveColumns> getLivePeaks() const noexcept;

    // Sounding grains, published about 60 times a second; readGrainViews copies the latest.
    static constexpr int maxGrainViews = 1024;
    int readGrainViews (std::array<GrainView, maxGrainViews>& out) const noexcept;

    // Set by the host when a sample is analysed (the resonator's NOTES: SAMPLE),
    // and the MANUAL slices.
    std::array<std::atomic<float>, 128> samplePitchHz {}, samplePitchLevel {};
    std::atomic<int> samplePitchCount { 0 };
    static constexpr int maxSlices = 128;
    std::array<std::atomic<float>, maxSlices> sliceTable {};   // written before sliceCountManual
    std::atomic<int> sliceCountManual { 0 };

private:
    class VoiceModulation;
    struct ModulatorIds { int type, rate, sync, div, shape, phase, retrig, smooth, jitter, attack, decay, sustain, release, repeat,
                          followAttack, followRelease, gain, source, enable, voice, macro, sym, curve, rev, spread, grainSpread; };

    float v (int id) const noexcept { return read[(size_t) id]->load (std::memory_order_relaxed); }
    double quartersFor (int id) const noexcept;
    GrainParams readParams (const float* voiceValues = nullptr) const noexcept;
    ResonatorParams readResonatorParams (const GrainParams&) noexcept;
    FilterParams readFilterParams() const noexcept;
    ColorParams readColorParams() const noexcept;
    SpaceParams readSpaceParams() const noexcept;
    void applyModulation() noexcept;
    void buildGrainScatter() noexcept;
    float shapedFor (int modulator, int target, float value) const noexcept;
    void writeScope (std::array<std::atomic<float>, scopeSize>&, const float* l, const float* r, int count) noexcept;
    void trackLivePeaks (int writeStart, int count) noexcept;
    void refreshAllLivePeaks() noexcept;
    void handleMidi (const MidiEvent&) noexcept;

    const bool instrument;
    double sampleRate = 48000.0;

    // Parameters: where each is read from (bound, own, or its modulated copy).
    std::array<std::atomic<float>, params::count> own {};
    std::array<std::atomic<float>*, params::count> source {};
    std::array<const std::atomic<float>*, params::count> read {};
    void rebuildReads() noexcept;

    Engine engine;
    Resonator resonator;
    Filter filter;
    Color color;
    Space space;
    kit::TapeInput tapeInput;   // Silt FX's INPUT past 0 dB

    // Modulation.
    std::vector<int> targets;                                // target → parameter id
    std::array<int, params::count> targetOf {};              // parameter id → target, or −1
    std::array<std::atomic<float>, maxTargets> modulated {};
    std::array<std::atomic<float>, numModulators * maxTargets> routeAmounts {};
    std::array<std::atomic<int>, numModulators * maxTargets> routePolarity {};
    std::array<Modulator, numModulators> modulators;
    std::array<ModulatorIds, numModulators> modulatorIds {};
    ModContext modContext;
    std::array<bool, numModulators> grainSource {}, nativeBipolar {}, modEnabled {};
    std::array<float, numModulators> globalModValues {};
    struct ScatterTarget { int target; double GrainParams::* field; double scale; };
    std::vector<ScatterTarget> scatterTargets;
    GrainScatter grainScatter;
    std::vector<int> voiceTargets;
    std::unique_ptr<VoiceModulation> voiceModulation;

    // Notes.
    std::array<NoteEvent, 1024> noteEvents {};
    int noteCount = 0;
    std::array<bool, 128> heldKeys {}, sustainedKeys {};
    bool sustainDown = false;
    bool allOff = false;
    int lastNote = -1;
    int followRoot = -1;
    std::array<float, 8> followChord {};
    int followChordSize = 0;
    static constexpr double followLetGoSeconds = 0.25;
    int followLetGoIn = 0;
    mutable std::array<float, maxSlices> sliceScratch {};

    // The chain.
    int chainNow = 0;
    float chainFade = 1.0f;
    std::array<float, numModules> moduleLevel { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<std::array<float, chunkSize>, 2> wet {};
    std::array<std::array<float, chunkSize>, 2> inputScaled {};
    float sampleGainNow = 1.0f, mixNow = 1.0f, inputGainNow = 1.0f;
    bool lastLive = false, lastSelfFeed = false, spaceNoteStarted = false;

    // Displays.
    std::array<std::atomic<float>, spaceTraceSize> spaceTrace {};
    int spaceTraceFill = 0;
    float spaceTracePeak = 0.0f;
    std::array<std::atomic<float>, scopeSize> scopeIn {}, scopeOut {};
    std::atomic<unsigned> scopeWrite { 0 };
    std::array<std::atomic<float>, liveColumns> livePeaks {};
    int liveColumn = -1;
    float liveColumnPeak = 0.0f;
    std::array<GrainView, maxGrainViews> grainViews {};
    std::atomic<int> grainViewCount { 0 };
    std::atomic<std::uint32_t> grainViewSequence { 0 };   // odd while being written
    int samplesSinceGrainViews = 0;
};

} // namespace silt
