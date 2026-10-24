#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <silt/Engine.h>
#include <silt/LevelMeter.h>

#include <atomic>

// One processor class, compiled into both builds. JucePlugin_IsSynth tells
// them apart: 1 for Silt (instrument), 0 for Silt FX.
//
// M2: the engine plays from MIDI. The sample comes from a dropped/loaded file
// or from Capture: arm, wait for the next bar, record N bars of the capture
// source (sidechain on the instrument, input on Silt FX), then swap it in.
class SiltProcessor final : public juce::AudioProcessor,
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
    static constexpr int numPages = 4, slotsPerPage = 8;
    std::atomic<int> currentPage { 0 };
    std::array<std::atomic<int>, slotsPerPage> knobCCs {};
    std::atomic<int> previousPageCC { 78 }, nextPageCC { 79 };
    // Learn: −1 idle; 0–7 waiting for knob n; 8 the previous-page button; 9 next.
    std::atomic<int> learnStep { -1 };
    void startLearn() noexcept { learnStep.store (0); }
    void cancelLearn() noexcept { learnStep.store (-1); }
    juce::String slotParameter (int page, int slot) const;   // "" for an empty slot
    void applyControllerMoves();                             // message thread (also the timer)
    void newPattern();

    // Message thread.
    void loadFile (const juce::File&);
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
    silt::GrainParams readParams() const noexcept;
    void runCapture (const juce::AudioBuffer<float>& capture, const juce::AudioPlayHead::PositionInfo*);
    void timerCallback() override;

    // Gives the engine a new sample and keeps a message-side copy for the UI and state.
    static std::unique_ptr<silt::SampleBuffer> makeBuffer (const juce::AudioBuffer<float>& audio, double sampleRate);
    void analyseThenInstall (std::unique_ptr<silt::SampleBuffer>, const juce::String& name);
    void installBuffer (std::unique_ptr<silt::SampleBuffer> analysed, const juce::String& name);
    std::unique_ptr<juce::AudioBuffer<float>> decode (juce::AudioFormatReader&) const;

    silt::Engine engine;
    double currentSampleRate = 48000.0;

    juce::AudioFormatManager formats;
    juce::ThreadPool loader { 1 };

    // Message-side copy of the sample (never touched by the audio thread).
    mutable juce::CriticalSection sampleLock;
    juce::AudioBuffer<float> sampleCopy;
    double sampleCopyRate = 0.0;
    juce::String sampleName;
    juce::MemoryBlock encodedSample;   // FLAC, cached for state saves
    std::vector<int> sampleZeroCrossings, sampleTransients;   // the sample's analysis, for the screen
    std::atomic<int> sampleGeneration { 0 };

    // Capture: the message thread allocates the target, the audio thread fills it.
    std::unique_ptr<silt::SampleBuffer> captureOwned;            // message thread only
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
