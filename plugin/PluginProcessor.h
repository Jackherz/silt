#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <silt/LevelMeter.h>

#include <atomic>

// One processor class, compiled into both builds. JucePlugin_IsSynth tells
// them apart: 1 for Silt (instrument), 0 for Silt FX.
//
// M0 test build: no engine yet. It reports what the host is sending (capture
// audio, MIDI, tempo) so we can check routing in each DAW (plan M0, Spike A).
class SiltProcessor final : public juce::AudioProcessor
{
public:
    static constexpr bool isInstrument = JucePlugin_IsSynth != 0;

    enum class CaptureSource { none, mainInput, sidechain };

    SiltProcessor();

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState params;

    // Read by the editor on the message thread.
    silt::LevelMeter captureMeter;
    std::atomic<CaptureSource> captureSource { CaptureSource::none };
    std::atomic<int> captureChannels { 0 };
    std::atomic<int> noteOnCount { 0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };

private:
    static constexpr int stateVersion = 1;

    static BusesProperties makeBuses();

    // Instrument: the bus to capture from. Sidechain first, main input as fallback.
    juce::AudioBuffer<float> findCaptureBus (juce::AudioBuffer<float>& buffer, CaptureSource& source);

    juce::AudioBuffer<float> scratch;   // preallocated in prepareToPlay
    std::atomic<float>* monitor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltProcessor)
};
