#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PageLayout.h"

#include <silt/Engine.h>
#include <silt/Color.h>
#include <silt/Filter.h>
#include <silt/LevelMeter.h>
#include <silt/Modulator.h>
#include <silt/Resonator.h>
#include <silt/Space.h>

#include <atomic>
#include <map>

// One processor class, compiled into both builds. JucePlugin_IsSynth tells
// them apart: 1 for Silt (instrument), 0 for Silt FX.
//
// M2: the engine plays from MIDI. The sample comes from a dropped/loaded file
// or from Capture: arm, wait for the next bar, record N bars of the capture
// source (sidechain on the instrument, input on Silt FX), then swap it in.
class SiltProcessor final : public juce::AudioProcessor,
                            private juce::AudioProcessorParameter::Listener,
                            private juce::Timer
{
public:
    static constexpr bool isInstrument = JucePlugin_IsSynth != 0;
    static constexpr double maxCaptureSeconds = 30.0;

    enum class CaptureSource { none, mainInput, sidechain };
    enum class CaptureState : int { idle, armed, recording, done, cancelRequested, cancelled };

    SiltProcessor();
    ~SiltProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 8-knob controller: the controller's 8 knobs (MIDI CCs) always move the 8
    // slots of the current page; two buttons step pages. Page layouts match the
    // editor's (GRAIN / SHAPE / PITCH / PLAY), alternatives following the modes.
    // A knob takes over a parameter only once it reaches the parameter's value
    // (pickup), so switching pages never makes values jump.
    static constexpr int numPages = silt::layout::numPages, slotsPerPage = 8, modPage = silt::layout::mod;
    std::atomic<int> resonatorHalf { 0 };   // which of RESONATOR's two pages of controls is shown
    std::atomic<int> currentPage { 0 };
    std::array<std::atomic<int>, slotsPerPage> knobCCs {};
    std::atomic<int> previousPageCC { 78 }, nextPageCC { 79 };
    // Learn: −1 idle; 0–7 waiting for knob n; 8 the previous-page button; 9 next.
    std::atomic<int> learnStep { -1 };
    void startLearn() noexcept { learnStep.store (0); }
    void cancelLearn() noexcept { learnStep.store (-1); }
    juce::String slotParameter (int page, int slot) const;   // "" for an empty slot
    static double divisionQuarters (int index);              // a synced note value, in quarter notes
    silt::layout::Context layoutContext() const;             // the page table's view of the parameters
    void applyControllerMoves();                             // message thread (also the timer)
    void newPattern();

    // Message thread.
    void loadFile (const juce::File&);   // ROOT goes back to C3

    // RANDOM: moves the controls in a page's slots (not the small mode
    // controls under them) to random values, as one undo step. randomAmount
    // (0–1) is how far: each knob goes that share of the way to a random
    // value, and each menu or switch changes with that chance. Excluded
    // parameters stay put; the modulator types start excluded. Both are
    // saved with the session, not in presets. Message thread.
    int randomisePage (int page);   // how many controls changed
    std::vector<const silt::layout::Entry*> randomisable (int page) const;
    std::atomic<float> randomAmount { 0.5f };
    bool isRandomExcluded (const juce::String& id) const { return randomExcluded.contains (id); }
    void setRandomExcluded (const juce::String& id, bool excluded);

    // CHAIN: the order RESONATOR, FILTER, COLOR and SPACE run in, as one of
    // the 24 orders ("chainOrder"). chainOrder gives the modules by position.
    enum Module { moduleResonator, moduleFilter, moduleColor, moduleSpace, numModules };
    static constexpr int numChainOrders = 24;
    static std::array<int, numModules> chainOrder (int index);
    static int chainOrderIndex (const std::array<int, numModules>&);
    static juce::String moduleName (int module);
    static juce::String chainOrderName (int index);   // "R>F>C>S"

    // SPACE's live trace: the level SPACE adds (its echoes and reverb, not
    // the sound going through), one peak per 64 samples, counted from the
    // latest note-on. spaceTraceCount keeps counting; the ring holds the last
    // spaceTraceSize values.
    static constexpr int spaceTraceSize = 8192, spaceTraceStep = 64;
    std::atomic<unsigned> spaceTraceCount { 0 };

    // The sample's strongest pitches (Hz, low to high), for the resonator's NOTES: SAMPLE.
    std::array<std::atomic<float>, 12> samplePitchHz {};
    std::atomic<int> samplePitchCount { 0 };
    float spaceTraceAt (unsigned index) const noexcept { return spaceTrace[index & (spaceTraceSize - 1)].load (std::memory_order_relaxed); }

    // The live scope: the sound going into one module and coming out of it,
    // mono, as rendered. The editor picks the module (−1: none) and reads the
    // latest samples; the return value changes whenever new ones arrive.
    static constexpr int scopeSize = 8192;
    std::atomic<int> scopeModule { -1 };
    unsigned readScope (float* in, float* out, int count) const;

    // Factory samples, built in: LOAD lists them, and a new instance opens
    // with the first. Loading one sets ROOT to its key. A state saves one by
    // its number rather than as audio.
    static int numFactorySamples();
    static juce::String factorySampleName (int index);
    void loadFactorySample (int index);
    int getFactorySample() const;   // the factory sample loaded, or −1
    void armCapture();
    void cancelCapture();
    bool isAudioFile (const juce::File&) const;

    // The sample as the UI shows it: min/max per column, and a counter that
    // changes whenever the sample does.
    struct Overview
    {
        std::vector<float> minimum, maximum;
        double seconds = 0.0;
        juce::String name;
        std::vector<float> transients;   // shares of the sample
    };
    Overview getOverview() const;
    float normalisingGainDb() const;               // the sample gain that peaks it at −1 dBFS
    std::pair<float, float> shownRegion() const;   // start and length as they'll play (snapped)

    // Sounding grains for the screen. The audio thread publishes a snapshot
    // about 60 times a second; readGrainViews copies the latest (lock-free,
    // retrying if it lands mid-update) and returns how many.
    static constexpr int maxGrainViews = 1024;
    int readGrainViews (std::array<silt::GrainView, maxGrainViews>& out) const;

    // Live mode, for the screen: peaks of the ring by position, and where the
    // write head is (a share of the ring).
    static constexpr int liveColumns = 1024;
    std::array<float, liveColumns> getLivePeaks() const;
    std::atomic<bool> liveMode { false };
    std::atomic<float> liveWriteShare { 0.0f };
    int getSampleGeneration() const noexcept { return sampleGeneration.load(); }

    juce::AudioProcessorValueTreeState params;

    // Modulation (MOD page): four modulators; a route gives one modulator an
    // amount (−1…1 of the parameter's range) on one target parameter. Routes are
    // edited on the message thread and read by the audio thread (atomics).
    //
    // Targets are the continuous and stepped (integer) parameters, including the
    // modulators' own settings (cross-modulation, read a chunk late so the
    // order never matters). Switch-like ones (choices and bools), the seed and
    // MACRO knobs aren't: a route there would jump between modes.
    static constexpr int numModulators = 4, maxTargets = 160, modulationInterval = 64;
    int modulationTarget (const juce::String& parameterId) const;   // −1: not a modulation target
    int numTargets() const noexcept { return (int) targets.size(); }
    juce::RangedAudioParameter& targetParameter (int target) const { return *targets[(size_t) target].parameter; }
    bool isSteppedTarget (int target) const;                         // an integer parameter
    float routeAmount (int modulator, int target) const noexcept;
    void setRouteAmount (int modulator, int target, float amount) noexcept;
    void clearRoutesOnTarget (int target) noexcept;
    // A route's polarity: natural follows its source; or set it bipolar (swings
    // both ways round the knob) or unipolar (one way).
    enum Polarity { natural = 0, unipolar = 1, bipolar = 2 };
    void setRoutePolarity (int modulator, int target, int polarity) noexcept;
    int routePolarityOf (int modulator, int target) const noexcept;
    bool routeIsBipolar (int modulator, int target) const noexcept;
    void clearRoutesForModulator (int modulator) noexcept;
    int routeCount (int modulator) const noexcept;
    struct Route { int modulator, target; float amount; };
    std::vector<Route> allRoutes() const;                            // in panel order, then modulator
    juce::String depthSpan (int modulator, int target) const;       // "480 Hz to 2.64 kHz": what it moves over
    juce::String depthText (int modulator, int target) const;       // "480 Hz to 2.64 kHz  (±35%)"

    // Undo (message thread): one history for everything you change by hand.
    // Knob, menu and button changes are recorded per gesture (a drag, a pick, a
    // controller turn); host automation, which has no gestures, isn't.
    // Route edits go through editRoutes(): the change becomes one undo step,
    // merged with the previous step until the next beginRouteGesture(), so a
    // whole drag undoes at once.
    void editRoutes (const std::function<void()>& edit);
    void beginRouteGesture();
    bool undoRoutes() { return undoing ([this] { return routeUndo.undo(); }); }
    bool redoRoutes() { return undoing ([this] { return routeUndo.redo(); }); }
    bool canUndoRoutes() const { return routeUndo.canUndo(); }
    bool canRedoRoutes() const { return routeUndo.canRedo(); }
    juce::String undoDescription() const { return routeUndo.getUndoDescription(); }
    juce::String redoDescription() const { return routeUndo.getRedoDescription(); }
    void clearUndoHistory() { routeUndo.clearUndoHistory(); }
    float modulatedValue01 (int target) const noexcept;             // where modulation has it now
    silt::ModulatorParams readModulatorParams (int modulator) const noexcept;
    bool isModulatorEnabled (int modulator) const noexcept;         // muted: contributes nothing, routes kept
    bool isModulatorPerVoice (int modulator) const noexcept;        // each voice runs its own copy
    bool isVoiceTarget (int target) const noexcept;                 // a grain parameter: per-voice modulation reaches it
    // Per voice, for the rings: whether a voice is sounding with per-voice
    // modulation, and where its modulation has each grain target (0–1).
    std::array<std::atomic<bool>, silt::Engine::numVoices> voiceShown {};
    std::array<std::array<std::atomic<float>, maxTargets>, silt::Engine::numVoices> voiceModulated01 {};
    std::array<std::atomic<float>, numModulators> modOutputs {};    // each modulator's latest value (0 if muted)
    std::array<std::atomic<float>, numModulators> modPhases {};     // a WAVE's place in its cycle, for display
    std::array<std::atomic<int>, numModulators> modStages {};       // an ENVELOPE's stage, for display
    std::atomic<int> modShown { 0 };                                // the modulator the MOD page shows
    std::atomic<int> armedMod { -1 };                               // ≥ 0: controller knobs set this modulator's depth

    // Presets (see Presets.h): the sound without the sample, controller mapping
    // or window. Message thread.
    std::unique_ptr<juce::XmlElement> capturePreset (const juce::String& name) const;
    void applyPreset (const juce::XmlElement&, const juce::String& name);
    juce::String presetName { "Init" };

    // The editor's size, saved with the set.
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };

    // Read by the editor on the message thread.
    silt::LevelMeter captureMeter;
    std::atomic<CaptureSource> captureSource { CaptureSource::none };
    std::atomic<CaptureState> captureState { CaptureState::idle };
    std::atomic<float> captureProgress { 0.0f };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> timeSigNumerator { 4 }, timeSigDenominator { 4 };
    std::atomic<int> activeGrains { 0 };
    std::array<std::atomic<float>, silt::Engine::numVoices> voicePlayheads {};   // < 0: voice silent
    juce::String lastLoadError;

private:
    static constexpr int stateVersion = 2;
    static constexpr int overviewColumns = 1024;

    static BusesProperties makeBuses();
    juce::AudioBuffer<float> findCaptureBus (juce::AudioBuffer<float>& buffer, CaptureSource& source);
    silt::GrainParams readParams (const float* voiceValues = nullptr) const noexcept;
    silt::ResonatorParams readResonatorParams (const silt::GrainParams&) const noexcept;
    silt::FilterParams readFilterParams() const noexcept;
    silt::ColorParams readColorParams() const noexcept;
    silt::SpaceParams readSpaceParams() const noexcept;
    void applyModulation() noexcept;
    void runCapture (const juce::AudioBuffer<float>& capture, const juce::AudioPlayHead::PositionInfo*);
    void timerCallback() override;

    // Gives the engine a new sample and keeps a message-side copy for the UI and state.
    static std::unique_ptr<silt::SampleBuffer> makeBuffer (const juce::AudioBuffer<float>& audio, double sampleRate);
    // Analyses on the loader thread, then installs. rootNote ≥ 0 sets ROOT;
    // record makes it an undo step (the sample before ↔ this one).
    void analyseThenInstall (std::unique_ptr<silt::SampleBuffer>, const juce::String& name, int factory = -1,
                             float rootNote = -1.0f, bool record = false);

    // Undo for samples: what was loaded (audio, name, factory number, ROOT).
    struct SampleSnapshot
    {
        juce::AudioBuffer<float> audio;
        double rate = 0.0;
        juce::String name;
        int factory = -1;
        float root = 60.0f;
    };
    class SampleChange;
    std::shared_ptr<const SampleSnapshot> snapshotSample() const;
    void restoreSample (const std::shared_ptr<const SampleSnapshot>&);
    void recordSampleChange (std::shared_ptr<const SampleSnapshot> before);
    void installBuffer (std::unique_ptr<silt::SampleBuffer> analysed, const juce::String& name, int factory = -1);
    // Reads, decodes and analyses on the loader thread, then installs and sets
    // ROOT. onlyIfUnchanged: drop it if another sample or a state arrives first.
    void loadInBackground (std::function<std::unique_ptr<juce::AudioFormatReader>()> open, const juce::String& name,
                           int factory, int rootNote, bool onlyIfUnchanged);
    static std::unique_ptr<juce::AudioFormatReader> openFactorySample (int index);
    std::unique_ptr<juce::AudioBuffer<float>> decode (juce::AudioFormatReader&) const;

    silt::Engine engine;
    silt::Resonator resonator;   // after the grains
    silt::Filter filter;         // after the resonator
    silt::Color color;           // after the filter
    silt::Space space;           // last: delay and reverb sends
    int lastNote = -1;           // the last note played, for FOLLOW and KEY (audio thread)
    bool lastLive = false;
    std::atomic<float>* sampleGain = nullptr;
    int chainNow = 0;          // audio thread: the order running, and its fade
    float chainFade = 1.0f;
    std::array<std::atomic<float>, scopeSize> scopeIn {}, scopeOut {};
    std::atomic<unsigned> scopeWrite { 0 };
    std::array<std::atomic<float>, spaceTraceSize> spaceTrace {};
    int spaceTraceFill = 0;        // audio thread: samples into the current step
    float spaceTracePeak = 0.0f;
    int followRoot = -1;   // audio thread: FOLLOW's lowest held note and the chord above it
    std::array<float, 8> followChord {};
    int followChordSize = 0;
    bool spaceNoteStarted = false;   // a note began since SPACE last ran: restart the trace
    void writeScope (std::array<std::atomic<float>, scopeSize>&, const float* l, const float* r, int count) noexcept;
    float sampleGainNow = 1.0f;   // audio thread: the gain ramped to

    // Modulation state.
    struct Target
    {
        juce::RangedAudioParameter* parameter = nullptr;
        std::atomic<float>* raw = nullptr;   // the parameter's own value
    };
    std::vector<Target> targets;
    std::map<juce::String, int> targetIndex;
    std::array<std::atomic<float>, maxTargets> modulated {};                       // what the audio code reads
    std::array<std::atomic<float>, numModulators * maxTargets> routeAmounts {};
    std::array<silt::Modulator, numModulators> modulators;
    silt::ModContext modContext;                                                   // audio thread
    struct ModulatorRaw
    {
        std::atomic<float>* type, *rate, *sync, *div, *shape, *phase, *retrig, *smooth, *jitter;
        std::atomic<float>* attack, *decay, *sustain, *release, *repeat, *followAttack, *followRelease, *gain, *source, *enable, *voice, *macro, *sym, *curve, *rev, *spread;
    };
    std::array<bool, 128> heldKeys {};   // audio thread: which notes are down, so repeats don't miscount
    std::array<float, numModulators> globalModValues {};   // audio thread: this chunk's shared values
    std::array<bool, numModulators> nativeBipolar {}, modEnabled {};   // audio thread, per chunk
    std::array<std::atomic<int>, numModulators * maxTargets> routePolarity {};
    float shapedFor (int modulator, int target, float value) const noexcept;
    juce::String routesToString() const;
    void routesFromString (const juce::String&);
    std::vector<int> voiceTargets;

    // Undo (message thread).
    class RoutesChange;
    class ParameterChange;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    bool undoing (const std::function<bool()>&);
    bool replaying = false;                    // undo/redo or a preset is setting values: don't record
    std::map<int, float> gestureStart;         // parameter index → value when its gesture began
    int lastGestureParameter = -1;
    juce::uint32 lastGestureTime = 0;
    std::vector<float> routeSnapshot() const;
    void restoreRoutes (const std::vector<float>&) noexcept;
    // Units: 1 per knob or route step; a sample step by its size (a unit per
    // 64 KB), so old takes give way once they add up (about 125 MB), keeping
    // at least the last 4 steps.
    juce::UndoManager routeUndo { 2000, 4 };
    juce::StringArray randomExcluded { "m1Type", "m2Type", "m3Type", "m4Type" };
    juce::Random randomiser;
    int lastControllerRoute = -1;
    juce::uint32 lastControllerRouteTime = 0;

    // Per-voice modulation (docs/per-voice-modulation.md): each voice's own
    // copies of the per-voice modulators, its own note context, and its grain
    // parameters for the chunk. The engine asks for them (silt::VoiceParams).
    class VoiceModulation final : public silt::VoiceParams
    {
    public:
        explicit VoiceModulation (SiltProcessor&);
        void reset() noexcept;
        void prepareChunk() noexcept;

        bool isActive() const noexcept override { return active; }
        void noteStarted (int voice, int note, float velocity) noexcept override;
        void noteReleased (int voice) noexcept override;
        void apply (int voice, silt::GrainParams&) noexcept override;

    private:
        void step (int voice, int numSamples) noexcept;
        void compute (int voice) noexcept;

        SiltProcessor& owner;
        static constexpr int voicesN = silt::Engine::numVoices;
        std::array<std::array<silt::Modulator, numModulators>, voicesN> mods;
        std::array<silt::ModContext, voicesN> context {};
        std::array<std::array<float, numModulators>, voicesN> values {};
        std::array<silt::GrainParams, voicesN> params {};
        std::array<bool, voicesN> held {}, started {};
        std::array<float, voicesN> spreadOffset {};   // SPREAD: where this voice's note sits round the cycle
        int notesInPhrase = 0;
        std::array<bool, numModulators> perVoice {};
        std::array<float, maxTargets> base01 {}, scratch {};
        bool active = false;
    };
    VoiceModulation voiceModulation;
    std::array<ModulatorRaw, numModulators> modulatorRaw {};
    double currentSampleRate = 48000.0;

    juce::AudioFormatManager formats;
    juce::ThreadPool loader { 1 };

    // Message-side copy of the sample (never touched by the audio thread).
    mutable juce::CriticalSection sampleLock;
    juce::AudioBuffer<float> sampleCopy;
    double sampleCopyRate = 0.0;
    juce::String sampleName;
    juce::MemoryBlock encodedSample;   // FLAC, cached for state saves
    int factorySample = -1;            // which factory sample this is, or −1
    std::atomic<bool> stateRestored { false };
    std::vector<int> sampleZeroCrossings, sampleTransients;   // the sample's analysis, for the screen
    std::atomic<int> sampleGeneration { 0 };

    // Capture: the message thread allocates the target, the audio thread fills it.
    std::unique_ptr<silt::SampleBuffer> captureOwned;            // message thread only
    bool capturingFeed = false;                                  // message thread: this capture is FEED's
    std::atomic<silt::SampleBuffer*> captureTarget { nullptr };  // shared view for the audio thread
    int captureWritePos = 0;                                     // audio thread only

    std::array<silt::GrainView, maxGrainViews> grainViews {};
    std::atomic<int> grainViewCount { 0 };
    std::atomic<std::uint32_t> grainViewSequence { 0 };   // odd while being written
    int samplesSinceGrainViews = 0;

    void trackLivePeaks (int writeStart, int count) noexcept;
    std::array<std::atomic<float>, liveColumns> livePeaks {};
    int liveColumn = -1;            // audio thread only
    float liveColumnPeak = 0.0f;

    // Controller: the audio thread queues (slot, value) moves for the message thread.
    void handleController (int number, int value) noexcept;
    juce::AbstractFifo controllerFifo { 512 };
    std::array<std::pair<int, int>, 512> controllerMoves {};
    std::array<int, 2> pageButtonLast {};      // audio thread: last value of prev / next
    std::array<int, slotsPerPage> learnedThisRun {};  // audio thread: CCs taken during a learn
    // Pickup, message thread only.
    std::array<bool, slotsPerPage> pickedUp {};
    std::array<int, slotsPerPage> lastKnobValue {};
    std::array<juce::String, slotsPerPage> slotTarget;
    int pickupPage = -1;

    std::vector<silt::NoteEvent> noteEvents;   // preallocated in prepareToPlay
    juce::AudioBuffer<float> wet;              // engine output, preallocated

    struct RawParams;                          // cached parameter pointers
    std::unique_ptr<RawParams> raw;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SiltProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltProcessor)
};
