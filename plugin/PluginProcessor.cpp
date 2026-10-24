#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Set = juce::AudioChannelSet;

juce::AudioProcessorValueTreeState::ParameterLayout makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    if constexpr (SiltProcessor::isInstrument)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "monitor", 1 }, "Monitor input", false));

    return layout;
}

bool isMonoOrStereo (const Set& set) { return set == Set::mono() || set == Set::stereo(); }
} // namespace

juce::AudioProcessor::BusesProperties SiltProcessor::makeBuses()
{
    if constexpr (isInstrument)
    {
        // Spike A (Live 12, macOS): Live feeds a VST3 instrument's second (aux)
        // bus, but an AU instrument only gets sidechain audio on a single input
        // bus. Logic is still untested.
        if (juce::PluginHostType::getPluginLoadedAs() == wrapperType_AudioUnit)
            return BusesProperties()
                .withInput ("Sidechain", Set::stereo(), true)
                .withOutput ("Output", Set::stereo(), true);

        // The main input stays off by default; the host's sidechain feeds bus 1.
        return BusesProperties()
            .withInput ("Input", Set::stereo(), false)
            .withInput ("Sidechain", Set::stereo(), true)
            .withOutput ("Output", Set::stereo(), true);
    }
    else
        return BusesProperties()
            .withInput ("Input", Set::stereo(), true)
            .withOutput ("Output", Set::stereo(), true);
}

SiltProcessor::SiltProcessor()
    : AudioProcessor (makeBuses()),
      params (*this, nullptr, "SILT", makeParameters())
{
    monitor = params.getRawParameterValue ("monitor");
}

void SiltProcessor::prepareToPlay (double, int maximumExpectedSamplesPerBlock)
{
    scratch.setSize (2, juce::jmax (1, maximumExpectedSamplesPerBlock));
    captureMeter.reset();
}

bool SiltProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != Set::stereo())
        return false;

    for (const auto& input : layouts.inputBuses)
        if (! input.isDisabled() && ! isMonoOrStereo (input))
            return false;

    // Silt FX always needs its own input; the instrument can run with none.
    return isInstrument || ! layouts.getMainInputChannelSet().isDisabled();
}

juce::AudioBuffer<float> SiltProcessor::findCaptureBus (juce::AudioBuffer<float>& buffer, CaptureSource& source)
{
    const auto usable = [this] (int bus)
    {
        const auto* b = getBus (true, bus);
        return b != nullptr && b->isEnabled() && b->getNumberOfChannels() > 0;
    };

    // The sidechain is the last input bus: bus 1 normally, the only bus on AU.
    const int sidechainBus = getBusCount (true) - 1;

    if (isInstrument && usable (sidechainBus))
    {
        source = CaptureSource::sidechain;
        return getBusBuffer (buffer, true, sidechainBus);
    }

    if (usable (0))
    {
        source = CaptureSource::mainInput;
        return getBusBuffer (buffer, true, 0);
    }

    source = CaptureSource::none;
    return {};
}

void SiltProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        if (message.isNoteOn())
        {
            lastNote.store (message.getNoteNumber(), std::memory_order_relaxed);
            noteOnCount.fetch_add (1, std::memory_order_relaxed);
        }
    }

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                hostBpm.store (*bpm, std::memory_order_relaxed);
            hostPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
        }

    auto source = CaptureSource::none;
    auto capture = findCaptureBus (buffer, source);
    captureSource.store (source, std::memory_order_relaxed);
    captureChannels.store (capture.getNumChannels(), std::memory_order_relaxed);

    if (capture.getNumChannels() > 0)
        captureMeter.process (capture.getArrayOfReadPointers(), capture.getNumChannels(), numSamples);

    auto output = getBusBuffer (buffer, false, 0);

    if constexpr (! isInstrument)
    {
        // Silt FX passes its input through until the engine exists.
        // A mono input arrives in channel 0 only.
        if (getTotalNumInputChannels() == 1 && output.getNumChannels() > 1)
            output.copyFrom (1, 0, output, 0, 0, numSamples);
        return;
    }

    // The instrument is silent unless Monitor is on. The capture bus can share
    // memory with the output bus, so copy it aside before clearing the output.
    // Work in chunks so a block bigger than announced never allocates.
    const bool monitoring = monitor != nullptr && monitor->load (std::memory_order_relaxed) > 0.5f;

    for (int start = 0; start < numSamples; start += scratch.getNumSamples())
    {
        const int len = juce::jmin (scratch.getNumSamples(), numSamples - start);
        const int captureCh = juce::jmin (capture.getNumChannels(), scratch.getNumChannels());

        for (int ch = 0; ch < captureCh; ++ch)
            scratch.copyFrom (ch, 0, capture, ch, start, len);

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
        {
            output.clear (ch, start, len);
            if (monitoring && captureCh > 0)
                output.copyFrom (ch, start, scratch, juce::jmin (ch, captureCh - 1), 0, len);
        }
    }
}

juce::AudioProcessorEditor* SiltProcessor::createEditor()
{
    return new SiltEditor (*this);
}

void SiltProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SiltProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName (params.state.getType()))
        params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SiltProcessor();
}
