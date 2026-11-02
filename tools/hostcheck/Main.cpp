// silt-hostcheck: loads a built Silt VST3 like a DAW would and checks the
// sample survives a state round trip, which pluginval doesn't cover.
//
//   silt_hostcheck path/to/Silt.vst3
//
// 1. Builds a state holding a one-second 220 Hz sine as FLAC (plan D10 format).
// 2. Restores it, plays a note, and checks the plug-in makes sound.
// 3. Saves state again and checks the sample came back out, the same length.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

namespace
{
constexpr int stateMagic = 0x544c4953;   // must match PluginProcessor.cpp
constexpr double rate = 48000.0;
constexpr int blockSize = 512;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    failures += ok ? 0 : 1;
}

struct ParsedState { juce::MemoryBlock xml, flac; };

// A JUCE VST3 host keeps the plug-in's own state bytes base64-encoded in an
// <IComponent> element of its "VST3PluginState" XML.
juce::MemoryBlock unwrapHostState (const juce::MemoryBlock& hostState)
{
    juce::MemoryBlock bytes;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize()))
        if (auto* component = xml->getChildByName ("IComponent"))
            bytes.fromBase64Encoding (component->getAllSubText());
    return bytes;
}

juce::MemoryBlock wrapHostState (const juce::MemoryBlock& pluginState)
{
    juce::XmlElement xml ("VST3PluginState");
    xml.createNewChildElement ("IComponent")->addTextElement (pluginState.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (xml, out);
    return out;
}

// Reads Silt's own layout; anything after it (the VST3 wrapper's private
// data) is ignored, as the plug-in itself does.
bool parse (const juce::MemoryBlock& hostState, ParsedState& out)
{
    const auto state = unwrapHostState (hostState);
    juce::MemoryInputStream in (state, false);
    if (in.readInt() != stateMagic)
        return false;
    in.readInt();
    in.readIntoMemoryBlock (out.xml, (juce::pointer_sized_int) in.readInt64());
    in.readIntoMemoryBlock (out.flac, (juce::pointer_sized_int) in.readInt64());
    return true;
}

juce::MemoryBlock sineAsFlac (int frames)
{
    juce::AudioBuffer<float> sine (2, frames);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < frames; ++i)
            sine.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) rate));

    juce::MemoryBlock block;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
    auto writer = juce::FlacAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                       .withSampleRate (rate)
                                                                       .withNumChannels (2)
                                                                       .withBitsPerSample (24));
    writer->writeFromAudioSampleBuffer (sine, 0, frames);
    writer.reset();
    return block;
}

float playNote (juce::AudioPluginInstance& plugin, double seconds)
{
    const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, blockSize);
    float peak = 0.0f;

    for (int done = 0; done < (int) (seconds * rate); done += blockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (done == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        plugin.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));
    }
    return peak;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: silt_hostcheck path/to/Plugin.vst3\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juce;
    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();
    std::cout << path << "\n";

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, path);
    if (types.isEmpty())
    {
        std::cerr << "no plug-in found\n";
        return 2;
    }

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*types[0], rate, blockSize, error);
    if (plugin == nullptr)
    {
        std::cerr << "can't load: " << error << "\n";
        return 2;
    }

    plugin->enableAllBuses();

    // The sample plays in Pitch mode. Silt FX has no Mode (it's always Live),
    // so there it only round-trips the sample in its state.
    bool playsSamples = false;
    for (auto* parameter : plugin->getParameters())
        if (parameter->getName (32) == "Mode")
        {
            parameter->setValueNotifyingHost (0.0f);
            playsSamples = true;
        }
    plugin->setRateAndBufferSizeDetails (rate, blockSize);
    plugin->prepareToPlay (rate, blockSize);

    check (playNote (*plugin, 0.5) == 0.0f, "silent with no sample");

    juce::MemoryBlock empty;
    plugin->getStateInformation (empty);
    ParsedState fresh;
    check (parse (empty, fresh), "state has the Silt header");
    check (fresh.flac.isEmpty(), "no sample in a fresh state");

    // Same parameters, plus a sample.
    const int frames = (int) rate;
    const auto flac = sineAsFlac (frames);
    juce::MemoryBlock withSample;
    {
        juce::MemoryOutputStream out (withSample, false);
        out.writeInt (stateMagic);
        out.writeInt (2);
        out.writeInt64 ((juce::int64) fresh.xml.getSize());
        out.write (fresh.xml.getData(), fresh.xml.getSize());
        out.writeInt64 ((juce::int64) flac.getSize());
        out.write (flac.getData(), flac.getSize());
    }
    const auto hostState = wrapHostState (withSample);
    plugin->setStateInformation (hostState.getData(), (int) hostState.getSize());

    if (playsSamples)
    {
        const float peak = playNote (*plugin, 1.0);
        check (peak > 0.1f, "plays the restored sample (peak " + juce::String (peak, 3) + ")");
    }

    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    ParsedState again;
    check (parse (saved, again) && ! again.flac.isEmpty(), "saved state carries the sample");

    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (again.flac, false), true));
    check (reader != nullptr && reader->lengthInSamples == frames, "saved sample has the same length");

    plugin->releaseResources();
    std::cout << (failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
