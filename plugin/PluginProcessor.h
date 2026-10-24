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
    };
    Overview getOverview() const;
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
    void installSample (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name);
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
    std::atomic<int> sampleGeneration { 0 };

    // Capture: the message thread allocates the target, the audio thread fills it.
    std::unique_ptr<silt::SampleBuffer> captureOwned;            // message thread only
    std::atomic<silt::SampleBuffer*> captureTarget { nullptr };  // shared view for the audio thread
    int captureWritePos = 0;                                     // audio thread only

    std::vector<silt::NoteEvent> noteEvents;   // preallocated in prepareToPlay
    juce::AudioBuffer<float> wet;              // engine output, preallocated

    struct RawParams;                          // cached parameter pointers
    std::unique_ptr<RawParams> raw;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SiltProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltProcessor)
};
