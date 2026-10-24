// silt-render: runs the engine offline over an audio file (plan §3).
//
//   silt-render --in drums.wav --out out.wav [--midi notes.mid | --note 60 --hold 4]
//               [--tail 1] [--seed 1] [--rate 48000] [--set key=value ...]
//
// --set keys: pitch (st), size (ms), rate (Hz), grains (count; overrides rate),
// spray, jitter, reverse, randamp, randpan,
// fade, slant, ease, start, length (all 0–1), scan (×), attack, decay,
// release (ms), sustain (0–1), root (note), end (loop|hold|stop),
// interp (linear|cubic)

#include <juce_audio_formats/juce_audio_formats.h>
#include <silt/Engine.h>

#include <iostream>

namespace
{
struct Options
{
    juce::File input, output, midi;
    int note = 60;
    double holdSeconds = 4.0, tailSeconds = 1.0, sampleRate = 48000.0;
    std::uint64_t seed = 1;
    silt::GrainParams params;
};

[[noreturn]] void fail (const juce::String& message)
{
    std::cerr << "silt-render: " << message << "\n";
    std::exit (1);
}

void applySetting (silt::GrainParams& p, const juce::String& setting)
{
    const auto key = setting.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto value = setting.fromFirstOccurrenceOf ("=", false, false).trim();
    const double v = value.getDoubleValue();

    if      (key == "pitch")   p.pitchSemitones = v;
    else if (key == "size")    p.sizeMs = v;
    else if (key == "rate")    p.rateHz = v;
    else if (key == "spray")   p.spray = v;
    else if (key == "start")   p.start = v;
    else if (key == "scan")    p.scan = v;
    else if (key == "reverse") p.reverseChance = v;
    else if (key == "grains")  p.grainCount = value.getIntValue();
    else if (key == "jitter")  p.jitter = v;
    else if (key == "randamp") p.randomAmp = v;
    else if (key == "randpan") p.randomPan = v;
    else if (key == "fade")    p.fade = v;
    else if (key == "slant")   p.slant = v;
    else if (key == "ease")    p.ease = v;
    else if (key == "length")  p.length = v;
    else if (key == "attack")  p.attackMs = v;
    else if (key == "decay")   p.decayMs = v;
    else if (key == "sustain") p.sustain = v;
    else if (key == "release") p.releaseMs = v;
    else if (key == "root")    p.rootNote = value.getIntValue();
    else if (key == "end")     p.endMode = value == "stop" ? silt::EndMode::stop
                                         : value == "hold" ? silt::EndMode::hold : silt::EndMode::loop;
    else if (key == "interp")  p.interpolation = value == "linear" ? silt::Interpolation::linear
                                                                   : silt::Interpolation::cubic;
    else fail ("unknown setting '" + key + "'");
}

Options parse (const juce::StringArray& args)
{
    Options o;
    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&]
        {
            if (++i >= args.size())
                fail (a + " needs a value");
            return args[i];
        };
        const auto file = [&] { return juce::File::getCurrentWorkingDirectory().getChildFile (next()); };

        if      (a == "--in")    o.input = file();
        else if (a == "--out")   o.output = file();
        else if (a == "--midi")  o.midi = file();
        else if (a == "--note")  o.note = next().getIntValue();
        else if (a == "--hold")  o.holdSeconds = next().getDoubleValue();
        else if (a == "--tail")  o.tailSeconds = next().getDoubleValue();
        else if (a == "--seed")  o.seed = (std::uint64_t) next().getLargeIntValue();
        else if (a == "--rate")  o.sampleRate = next().getDoubleValue();
        else if (a == "--set")   applySetting (o.params, next());
        else fail ("unknown option '" + a + "'");
    }
    if (o.input == juce::File() || o.output == juce::File())
        fail ("--in and --out are required");
    return o;
}

std::unique_ptr<silt::SampleBuffer> load (juce::AudioFormatManager& formats, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        fail ("can't read " + file.getFullPathName());

    const int frames = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio ((int) reader->numChannels, frames);
    reader->read (&audio, 0, frames, 0, true, true);

    auto buffer = std::make_unique<silt::SampleBuffer> (frames, reader->sampleRate);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::copy (buffer->getWritePointer (ch),
                                           audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1)), frames);
    return buffer;
}

std::vector<silt::NoteEvent> loadNotes (const Options& o, int& totalSamples)
{
    std::vector<silt::NoteEvent> notes;   // sampleOffset = absolute sample here
    const auto toSamples = [&] (double seconds) { return (int) std::lround (seconds * o.sampleRate); };

    if (o.midi == juce::File())
    {
        notes.push_back ({ 0, o.note, 1.0f, true });
        notes.push_back ({ toSamples (o.holdSeconds), o.note, 1.0f, false });
    }
    else
    {
        juce::FileInputStream stream (o.midi);
        juce::MidiFile file;
        if (! stream.openedOk() || ! file.readFrom (stream))
            fail ("can't read " + o.midi.getFullPathName());
        file.convertTimestampTicksToSeconds();

        for (int t = 0; t < file.getNumTracks(); ++t)
            for (const auto* e : *file.getTrack (t))
                if (e->message.isNoteOnOrOff())
                    notes.push_back ({ toSamples (e->message.getTimeStamp()), e->message.getNoteNumber(),
                                       e->message.getFloatVelocity(), e->message.isNoteOn() });

        std::stable_sort (notes.begin(), notes.end(),
                          [] (const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });
    }

    totalSamples = (notes.empty() ? 0 : notes.back().sampleOffset) + toSamples (o.tailSeconds);
    return notes;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);
    const auto o = parse (args);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    silt::Engine engine;
    engine.prepare (o.sampleRate);
    engine.setSeed (o.seed);
    engine.setBuffer (load (formats, o.input));

    int total = 0;
    const auto notes = loadNotes (o, total);

    juce::AudioBuffer<float> out (2, total);
    std::vector<silt::NoteEvent> blockEvents;
    constexpr int blockSize = 128;
    size_t next = 0;
    int maxGrains = 0;

    for (int start = 0; start < total; start += blockSize)
    {
        const int len = juce::jmin (blockSize, total - start);
        blockEvents.clear();
        for (; next < notes.size() && notes[next].sampleOffset < start + len; ++next)
        {
            auto e = notes[next];
            e.sampleOffset -= start;
            blockEvents.push_back (e);
        }

        engine.process (out.getWritePointer (0, start), out.getWritePointer (1, start), len,
                        blockEvents.data(), (int) blockEvents.size(), o.params);
        maxGrains = juce::jmax (maxGrains, engine.getActiveGrainCount());
    }

    o.output.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (o.output);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                      .withSampleRate (o.sampleRate)
                                                                      .withNumChannels (2)
                                                                      .withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (out, 0, total))
        fail ("can't write " + o.output.getFullPathName());

    const float peak = out.getMagnitude (0, total);
    std::cout << o.output.getFileName() << ": " << juce::String (total / o.sampleRate, 2) << " s, peak "
              << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dBFS, up to "
              << maxGrains << " grains\n";
    return 0;
}
