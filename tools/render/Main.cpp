// silt-render: runs the engine offline over an audio file (plan §3).
//
//   silt-render --in drums.wav --out out.wav [--midi notes.mid | --note 60 --hold 4]
//               [--tail 1] [--seed 1] [--rate 48000] [--set key=value ...]
//
// --set keys: pitch (st), size (ms), rate (Hz), grains (count; overrides rate),
// spray, jitter, reverse, randamp, randpan, randpitch (st), scale (0-12),
// key (0-11), pattern (st), steps, heads, headspread,
// fade, slant, ease, start, length (all 0–1), scan (×), attack, decay,
// release (ms), sustain (0–1), root (note), end (loop|hold|stop),
// interp (linear|cubic); resonator: restune (note), resnotes (scale|harmonics|bell),
// resrange (octaves), resdecay (s), resbright, reswidth, resmix (0–1); filter: filter
// (12|24), cutoff (Hz), reso, drive, filtermix (0–1), morph (0–2), env (−1…1); color:
// cdrive, ctone (−1…1), bits, crate (Hz), comp, noise, wow, cmix (0–1); space: time (ms),
// feedback, delay, rsize, rdecay (s), stone, shimmer, reverb (0–1), pingpong (0|1)

#include <juce_audio_formats/juce_audio_formats.h>
#include <silt/Color.h>
#include <silt/Engine.h>
#include <silt/Filter.h>
#include <silt/Resonator.h>
#include <silt/Space.h>

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
    silt::ResonatorParams resonator;
    silt::FilterParams filter;
    silt::ColorParams color;
    silt::SpaceParams space;
};

[[noreturn]] void fail (const juce::String& message)
{
    std::cerr << "silt-render: " << message << "\n";
    std::exit (1);
}

void applySetting (silt::GrainParams& p, silt::ResonatorParams& r, silt::FilterParams& f, silt::ColorParams& c, silt::SpaceParams& s, const juce::String& setting)
{
    const auto key = setting.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto value = setting.fromFirstOccurrenceOf ("=", false, false).trim();
    const double v = value.getDoubleValue();

    if      (key == "time")      s.delayMs = (float) v;
    else if (key == "feedback")  s.feedback = (float) v;
    else if (key == "delay")     s.delayLevel = (float) v;
    else if (key == "rsize")     s.size = (float) v;
    else if (key == "rdecay")    s.decaySeconds = (float) v;
    else if (key == "stone")     s.tone = (float) v;
    else if (key == "shimmer")   s.shimmer = (float) v;
    else if (key == "reverb")    s.reverbLevel = (float) v;
    else if (key == "pingpong")  s.pingPong = v > 0.5;
    else if (key == "cdrive")    c.drive = (float) v;
    else if (key == "ctone")     c.tone = (float) v;
    else if (key == "bits")      c.bits = (float) v;
    else if (key == "crate")     c.rateHz = (float) v;
    else if (key == "comp")      c.comp = (float) v;
    else if (key == "noise")     c.noise = (float) v;
    else if (key == "wow")       c.wow = (float) v;
    else if (key == "cmix")      c.mix = (float) v;
    else if (key == "filter")    f.slope = value == "24" ? silt::FilterSlope::db24 : silt::FilterSlope::db12;
    else if (key == "cutoff")    f.cutoffHz = (float) v;
    else if (key == "reso")      f.resonance = (float) v;
    else if (key == "morph")     f.morph = (float) v;
    else if (key == "drive")     f.drive = (float) v;
    else if (key == "env")       f.envAmount = (float) v;
    else if (key == "filtermix") f.mix = (float) v;
    else if (key == "restune")   r.rootNote = (float) v;
    else if (key == "resnotes")  r.notes = value == "harmonics" ? silt::ResonatorNotes::harmonics
                                         : value == "bell" ? silt::ResonatorNotes::bell : silt::ResonatorNotes::scale;
    else if (key == "resrange")  r.rangeOctaves = (float) v;
    else if (key == "resbands")  r.bandCount = (int) v;
    else if (key == "resdamp")   r.damp = (float) v;
    else if (key == "resdecay")  r.decaySeconds = (float) v;
    else if (key == "resbright") r.bright = (float) v;
    else if (key == "reswidth")  r.width = (float) v;
    else if (key == "resmix")    r.mix = (float) v;
    else if (key == "pitch")   p.pitchSemitones = v;
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
    else if (key == "randpitch") p.randomPitch = v;
    else if (key == "scale")   p.scale = (silt::Scale) value.getIntValue();
    else if (key == "key")     p.key = value.getIntValue();
    else if (key == "pattern") p.patternDepth = v;
    else if (key == "steps")   p.patternSteps = value.getIntValue();
    else if (key == "heads")   p.heads = value.getIntValue();
    else if (key == "headspread") p.headSpread = v;
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
        else if (a == "--set")   applySetting (o.params, o.resonator, o.filter, o.color, o.space, next());
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
    silt::Resonator resonator;
    resonator.prepare (o.sampleRate);
    auto resonatorParams = o.resonator;
    resonatorParams.scale = o.params.scale;   // as in the plug-in: the PITCH page's scale and key
    resonatorParams.key = o.params.key;
    silt::Filter filter;
    filter.prepare (o.sampleRate);
    silt::Color color;
    color.prepare (o.sampleRate);
    silt::Space space;
    space.prepare (o.sampleRate);

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
        resonator.process (out.getWritePointer (0, start), out.getWritePointer (1, start), len, resonatorParams);
        filter.process (out.getWritePointer (0, start), out.getWritePointer (1, start), len, o.filter);
        color.process (out.getWritePointer (0, start), out.getWritePointer (1, start), len, o.color);
        space.process (out.getWritePointer (0, start), out.getWritePointer (1, start), len, o.space);
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
