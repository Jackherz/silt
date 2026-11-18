// silt-golden: plays fixed scenes through the whole processor (processBlock, as a
// host would) and writes the output, so a change to the glue can be checked
// sample for sample against the build before it.
//
//   silt-golden <out-folder>                 write <scene>.f32 (interleaved stereo float)
//   silt-golden <out-folder> <reference>     also compare with a reference folder;
//                                            exits 1 if any scene differs
//
// Built twice, as the instrument (silt_golden) and as Silt FX (silt_golden_fx).

#include "../../plugin/PluginProcessor.h"
#include <silt/Params.h>

#include <cstdio>
#include <functional>

namespace
{
constexpr double rate = 48000.0;

// A host clock: plays from bar 1 at 120 BPM in 4/4 when running.
struct Clock final : juce::AudioPlayHead
{
    bool playing = false;
    double bpm = 120.0;
    std::int64_t samples = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setIsPlaying (playing);
        const double ppq = (double) samples / rate * bpm / 60.0;
        p.setPpqPosition (ppq);
        p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        p.setTimeInSamples (samples);
        return p;
    }
};

struct Scene
{
    const char* name;
    double seconds;
    int blockSize;
    bool input;          // feed a test signal into the input / sidechain
    bool hostPlaying;
    std::function<void (SiltProcessor&)> setUp;
    // Per block: MIDI to send, and anything to change, at this sample position.
    std::function<void (SiltProcessor&, juce::MidiBuffer&, int sampleAt, int blockSize)> during;
};

void set (SiltProcessor& silt, const char* id, float value)   // in the parameter's own units
{
    auto* p = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter (id));
    if (p == nullptr)
        return;   // not in this build
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void route (SiltProcessor& silt, int modulator, const char* id, float amount, int polarity = 0)
{
    const int t = silt.modulationTarget (id);
    if (t < 0)
        return;
    silt.setRouteAmount (modulator, t, amount);
    silt.setRoutePolarity (modulator, t, polarity);
}

// Events at a time in seconds, if it falls in this block.
struct At
{
    juce::MidiBuffer& midi;
    int start, size;
    void operator() (double seconds, const juce::MidiMessage& m) const
    {
        const int s = (int) std::lround (seconds * rate);
        if (s >= start && s < start + size)
            midi.addEvent (m, s - start);
    }
    bool now (double seconds) const
    {
        const int s = (int) std::lround (seconds * rate);
        return s >= start && s < start + size;
    }
};

std::vector<Scene> scenes()
{
    using M = juce::MidiMessage;
    std::vector<Scene> list;

    list.push_back ({ "pitch-chord-sustain", 6.0, 128, false, false, [] (SiltProcessor&) {},
        [] (SiltProcessor&, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (0.0, M::noteOn (1, 60, (juce::uint8) 100));
            at (0.1, M::noteOn (1, 64, (juce::uint8) 80));
            at (0.2, M::noteOn (1, 67, (juce::uint8) 60));
            at (0.5, M::controllerEvent (1, 64, 127));
            at (0.8, M::noteOff (1, 60));
            at (0.9, M::noteOff (1, 64));
            at (1.5, M::controllerEvent (1, 64, 0));
            at (2.0, M::noteOn (1, 72, (juce::uint8) 127));
            at (2.0, M::noteOn (1, 72, (juce::uint8) 90));   // a repeat while held
            at (3.0, M::allNotesOff (1));
            at (3.5, M::noteOn (1, 48, (juce::uint8) 100));
            at (4.5, M::noteOff (1, 48));
            at (4.5, M::noteOff (1, 67));
        } });

    list.push_back ({ "worst-case", 4.0, 128, false, false, [] (SiltProcessor& silt)
        {
            set (silt, "rate", 100.0f); set (silt, "size", 500.0f); set (silt, "heads", 8.0f);
            set (silt, "resMix", 60.0f); set (silt, "resDrive", 50.0f); set (silt, "filterMode", 1.0f);
            set (silt, "filterCutoff", 3000.0f); set (silt, "filterReso", 40.0f);
            set (silt, "colorDrive", 50.0f); set (silt, "colorComp", 50.0f); set (silt, "colorWow", 40.0f);
            set (silt, "spaceDelay", 50.0f); set (silt, "spaceReverb", 50.0f); set (silt, "spaceShimmer", 60.0f);
            set (silt, "m1Voice", 1.0f); set (silt, "m2Voice", 1.0f);
            route (silt, 0, "spray", 0.5f);
            route (silt, 1, "pitch", 0.1f);
        },
        [] (SiltProcessor&, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            for (int k = 0; k < 16; ++k)
                at (0.0, M::noteOn (1, 48 + k * 3, (juce::uint8) 100));
            for (int k = 0; k < 16; k += 2)
                at (2.5, M::noteOff (1, 48 + k * 3));
        } });

    list.push_back ({ "chain-and-switches", 5.0, 256, false, false, [] (SiltProcessor& silt)
        {
            set (silt, "resMix", 50.0f); set (silt, "resDecay", 3.0f);
            set (silt, "filterCutoff", 1200.0f); set (silt, "filterReso", 60.0f); set (silt, "filterEnv", 50.0f); set (silt, "filterKey", 50.0f);
            set (silt, "colorDrive", 70.0f); set (silt, "colorBits", 8.0f); set (silt, "colorRate", 8000.0f);
            set (silt, "spaceDelay", 40.0f); set (silt, "spaceReverb", 30.0f); set (silt, "spaceDuck", 2.0f);
            set (silt, "sampleGain", -6.0f); set (silt, "gain", 3.0f);
        },
        [] (SiltProcessor& silt, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (0.0, M::noteOn (1, 62, (juce::uint8) 110));
            at (2.5, M::noteOn (1, 69, (juce::uint8) 70));
            if (at.now (1.0)) set (silt, "chainOrder", 17.0f);
            if (at.now (1.5)) set (silt, "spaceOn", 0.0f);
            if (at.now (2.0)) set (silt, "resOn", 0.0f);
            if (at.now (2.2)) set (silt, "sampleGain", 4.0f);
            if (at.now (2.7)) set (silt, "spaceOn", 1.0f);
            if (at.now (3.0)) set (silt, "chainOrder", 5.0f);
            if (at.now (3.2)) set (silt, "resOn", 1.0f);
            if (at.now (3.5)) set (silt, "filterOn", 0.0f);
            if (at.now (4.0)) set (silt, "gain", -60.0f);
        } });

    list.push_back ({ "modulation", 6.0, 128, false, false, [] (SiltProcessor& silt)
        {
            set (silt, "m1Type", 0.0f); set (silt, "m1Rate", 3.0f); set (silt, "m1Shape", 1.4f);
            route (silt, 0, "filterCutoff", -0.4f);
            route (silt, 0, "m2Rate", 0.3f);                       // cross-modulation
            set (silt, "m2Type", 1.0f); set (silt, "m2Rate", 6.0f);
            route (silt, 1, "randPan", 0.6f, 1);                   // unipolar from a bipolar source
            set (silt, "m3Type", 2.0f); set (silt, "m3Voice", 1.0f); set (silt, "m3Attack", 300.0f); set (silt, "m3Spread", 50.0f);
            route (silt, 2, "pitch", 0.2f, 2);                     // bipolar from an envelope
            route (silt, 2, "size", -0.3f);
            route (silt, 2, "resMix", 0.5f);                       // not a voice target: shared
            set (silt, "m4Type", 6.0f); set (silt, "m4GrainSpread", 2.0f);
            route (silt, 3, "spray", 0.4f);
            route (silt, 3, "randPitch", 0.2f);
            set (silt, "filterCutoff", 2500.0f); set (silt, "resMix", 20.0f);
        },
        [] (SiltProcessor&, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (0.0, M::noteOn (1, 57, (juce::uint8) 100));
            at (0.7, M::noteOn (1, 64, (juce::uint8) 60));
            at (1.4, M::noteOn (1, 69, (juce::uint8) 120));
            at (2.0, M::noteOff (1, 64));
            at (3.0, M::noteOff (1, 57));
            at (3.0, M::noteOff (1, 69));
            at (3.5, M::noteOn (1, 52, (juce::uint8) 90));
            at (5.0, M::noteOff (1, 52));
        } });

    list.push_back ({ "midi-sources-and-follow", 5.0, 64, false, false, [] (SiltProcessor& silt)
        {
            set (silt, "m1Type", 4.0f); set (silt, "m1Source", 2.0f);   // mod wheel
            route (silt, 0, "filterCutoff", 0.6f);
            set (silt, "m2Type", 4.0f); set (silt, "m2Source", 3.0f);   // aftertouch
            route (silt, 1, "colorDrive", 0.8f);
            set (silt, "m3Type", 4.0f); set (silt, "m3Source", 4.0f);   // pitch bend
            route (silt, 2, "pitch", 0.1f);
            set (silt, "m4Type", 5.0f); set (silt, "m4Macro", 70.0f);
            route (silt, 3, "spaceReverb", 0.5f);
            set (silt, "filterCutoff", 400.0f); set (silt, "resMix", 60.0f); set (silt, "resFollow", 1.0f); set (silt, "resShift", 7.0f);
            set (silt, "scale", 3.0f); set (silt, "key", 2.0f);
        },
        [] (SiltProcessor&, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (0.0, M::noteOn (1, 50, (juce::uint8) 100));
            at (0.0, M::noteOn (1, 57, (juce::uint8) 100));
            for (int k = 0; k <= 20; ++k)
            {
                at (0.2 + k * 0.1, M::controllerEvent (1, 1, k * 6));
                at (0.25 + k * 0.1, M::channelPressureChange (1, 127 - k * 6));
                at (0.3 + k * 0.1, M::pitchWheel (1, 8192 + (k - 10) * 800));
            }
            at (2.5, M::noteOff (1, 57));     // let go: leaves the chord a moment later
            at (2.6, M::noteOn (1, 62, (juce::uint8) 80));
            at (3.5, M::noteOff (1, 50));
            at (3.5, M::noteOff (1, 62));
        } });

    list.push_back ({ "synced-host", 5.0, 128, false, true, [] (SiltProcessor& silt)
        {
            set (silt, "rateMode", 1.0f); set (silt, "rateDiv", 3.0f);
            set (silt, "sizeSync", 1.0f); set (silt, "sizeDiv", 6.0f);
            set (silt, "scanSync", 1.0f); set (silt, "scanDiv", 2.0f);
            set (silt, "spaceSync", 1.0f); set (silt, "spaceDelay", 50.0f);
            set (silt, "m1Sync", 1.0f); set (silt, "m1Div", 6.0f);
            route (silt, 0, "rateDiv", 0.2f);
        },
        [] (SiltProcessor&, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (0.13, M::noteOn (1, 60, (juce::uint8) 100));
            at (3.9, M::noteOff (1, 60));
        } });

    list.push_back ({ "grains-mode-region", 4.0, 128, false, false, [] (SiltProcessor& silt)
        {
            set (silt, "rateMode", 2.0f); set (silt, "grains", 12.0f); set (silt, "grainSize", 120.0f);
            set (silt, "start", 20.0f); set (silt, "length", 30.0f); set (silt, "snap", 1.0f);
            set (silt, "scan", -0.5f); set (silt, "endMode", 3.0f);
            set (silt, "patternDepth", 7.0f); set (silt, "patternSteps", 5.0f); set (silt, "heads", 3.0f);
            set (silt, "reverse", 30.0f); set (silt, "randPitch", 2.0f); set (silt, "scale", 2.0f);
        },
        [] (SiltProcessor&, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (0.0, M::noteOn (1, 60, (juce::uint8) 100));
            at (1.5, M::noteOn (1, 55, (juce::uint8) 100));
            at (3.0, M::noteOff (1, 60));
            at (3.0, M::noteOff (1, 55));
        } });

    list.push_back ({ "slice", 4.0, 128, false, false, [] (SiltProcessor& silt)
        {
            set (silt, "keyMode", 3.0f); set (silt, "sliceCount", 8.0f);
        },
        [] (SiltProcessor& silt, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            for (int k = 0; k < 8; ++k)
            {
                at (k * 0.25, M::noteOn (1, 36 + k, (juce::uint8) 100));
                at (k * 0.25 + 0.2, M::noteOff (1, 36 + k));
            }
            if (at.now (2.0)) set (silt, "sliceMode", 1.0f);
            for (int k = 0; k < 6; ++k)
                at (2.0 + k * 0.3, M::noteOn (1, 36 + k * 2, (juce::uint8) 90));
        } });

    list.push_back ({ "live-input", 6.0, 128, true, false, [] (SiltProcessor& silt)
        {
            set (silt, "keyMode", 1.0f); set (silt, "liveDelay", 300.0f); set (silt, "feedback", 50.0f);
            set (silt, "mix", 60.0f); set (silt, "monitor", 1.0f); set (silt, "liveRun", 2.0f);
            set (silt, "inputGain", 9.0f);   // Silt FX only: past 0 dB the tape takes it
            set (silt, "m1Type", 3.0f); route (silt, 0, "filterCutoff", 0.5f); set (silt, "filterCutoff", 800.0f);
            set (silt, "spaceReverb", 30.0f);
        },
        [] (SiltProcessor& silt, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (1.0, M::noteOn (1, 67, (juce::uint8) 100));
            at (2.5, M::noteOff (1, 67));
            if (at.now (3.0)) set (silt, "freeze", 1.0f);
            if (at.now (3.5)) set (silt, "mix", 20.0f);
            if (at.now (4.0)) set (silt, "inputGain", -6.0f);
            if (at.now (4.5)) set (silt, "freeze", 0.0f);
        } });

    list.push_back ({ "feed", 5.0, 128, true, false, [] (SiltProcessor& silt)
        {
            set (silt, "keyMode", 2.0f); set (silt, "feedback", 105.0f); set (silt, "liveRun", 1.0f);
            set (silt, "resMix", 40.0f);
        },
        [] (SiltProcessor& silt, juce::MidiBuffer& midi, int s, int n)
        {
            At at { midi, s, n };
            at (2.0, M::noteOn (1, 64, (juce::uint8) 100));
            at (3.0, M::noteOff (1, 64));
            if (at.now (3.5)) set (silt, "keyMode", 0.0f);
        } });

    return list;
}

// The test signal for the input: a slow chirp under a pulse, so the follower and
// the live ring have something with shape.
float testInput (std::int64_t i, int channel)
{
    const double t = (double) i / rate;
    const double chirp = std::sin (6.283185307179586 * (110.0 * t + 40.0 * t * t)) * 0.3;
    const double pulse = std::fmod (t, 0.5) < 0.05 ? 0.4 * std::sin (6.283185307179586 * 880.0 * t) : 0.0;
    return (float) (chirp + pulse) * (channel == 0 ? 1.0f : 0.8f);
}

std::vector<float> run (const Scene& scene)
{
    SiltProcessor silt;
    silt.enableAllBuses();
    Clock clock;
    clock.playing = scene.hostPlaying;
    silt.setPlayHead (&clock);
    silt.prepareToPlay (rate, 512);
    if constexpr (SiltProcessor::isInstrument)
    {
        silt.loadFactorySample (2);   // Rain Bells
        for (int i = 0; i < 500 && silt.getFactorySample() != 2; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (silt.getFactorySample() != 2)
        {
            std::fprintf (stderr, "silt-golden: the factory sample didn't load\n");
            std::exit (2);
        }
    }
    scene.setUp (silt);

    const int total = (int) (scene.seconds * rate);
    const int channels = juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, scene.blockSize);
    std::vector<float> out ((size_t) total * 2);
    for (int start = 0; start < total; start += scene.blockSize)
    {
        const int n = juce::jmin (scene.blockSize, total - start);
        buffer.setSize (channels, n, false, false, true);
        buffer.clear();
        if (scene.input || ! SiltProcessor::isInstrument)   // Silt FX only ever grains its input
            for (int ch = 0; ch < silt.getTotalNumInputChannels(); ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, testInput (start + i, ch % 2));
        juce::MidiBuffer midi;
        scene.during (silt, midi, start, n);
        clock.samples = start;
        silt.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            out[(size_t) (start + i) * 2] = buffer.getSample (0, i);
            out[(size_t) (start + i) * 2 + 1] = buffer.getSample (1, i);
        }
    }
    silt.setPlayHead (nullptr);
    return out;
}
// The host's parameters against silt::params, the JUCE-free table: the same ids in
// the same order, names, kinds, choices and defaults, and to the bit the same
// travel (0–1 and back) and text at thousands of points along each.
bool checkParameters()
{
    namespace sp = silt::params;
    SiltProcessor silt;
    const auto& all = silt.getParameters();
    int problems = 0, checked = 0, index = 0;
    const auto report = [&problems] (const juce::String& what)
    {
        if (++problems <= 40)
            std::printf ("params: %s\n", what.toRawUTF8());
    };
    const auto same = [] (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; };

    for (int id = 0; id < sp::count; ++id)
    {
        if (! sp::inBuild (id, SiltProcessor::isInstrument))
            continue;
        const auto& info = sp::info (id);
        if (index >= all.size())
        {
            report (juce::String ("the host has no ") + info.id);
            break;
        }
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (all[index++]);
        const juce::String name = info.id;
        if (p->getParameterID() != name)
        {
            report ("at " + juce::String (index - 1) + " the host has " + p->getParameterID() + ", the table " + name);
            continue;
        }
        if (p->getName (1024) != juce::String::fromUTF8 (info.name))
            report (name + ": named " + p->getName (1024));
        const bool kindOk = info.kind == sp::Kind::real    ? dynamic_cast<juce::AudioParameterFloat*> (p) != nullptr
                          : info.kind == sp::Kind::integer ? dynamic_cast<juce::AudioParameterInt*> (p) != nullptr
                          : info.kind == sp::Kind::boolean ? dynamic_cast<juce::AudioParameterBool*> (p) != nullptr
                                                           : dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr;
        if (! kindOk)
            report (name + ": a different kind");
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            juce::StringArray table;
            for (const auto* c : info.choices)
                table.add (juce::String::fromUTF8 (c));
            if (choice->choices != table)
                report (name + ": choices " + choice->choices.joinIntoString ("|"));
        }
        if (! same (p->getDefaultValue(), sp::to01 (id, sp::defaultFor (id, SiltProcessor::isInstrument))))
            report (name + ": default " + juce::String (p->convertFrom0to1 (p->getDefaultValue())));

        for (int k = 0; k <= 4000; ++k)
        {
            const float x = (float) k / 4000.0f;
            const float v = p->convertFrom0to1 (x);
            if (! same (v, sp::from01 (id, x)))
            {
                report (name + ": from 0-1 at " + juce::String (x, 6) + " " + juce::String (v, 9) + " vs " + juce::String (sp::from01 (id, x), 9));
                break;
            }
            if (p->getText (x, 1024) != juce::String::fromUTF8 (sp::text (id, v).c_str()))
            {
                report (name + ": text at " + juce::String (v, 6) + " '" + p->getText (x, 1024) + "' vs '" + juce::String::fromUTF8 (sp::text (id, v).c_str()) + "'");
                break;
            }
            // Off the steps, and past both ends.
            const auto range = p->getNormalisableRange();
            const float off = range.start + (range.end - range.start) * (-0.01f + 1.02f * x) + 0.000317f;
            if (! same (p->convertTo0to1 (off), sp::to01 (id, off)) || ! same (p->convertTo0to1 (v), sp::to01 (id, v)))
            {
                report (name + ": to 0-1 at " + juce::String (off, 9));
                break;
            }
            ++checked;
        }
    }
    if (index != all.size())
        report ("the host has " + juce::String (all.size() - index) + " more");
    std::printf ("parameters: %d checked at %d points, %d differences\n", index, checked, problems);
    return problems == 0;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: silt-golden <out-folder> [reference-folder]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    outDir.createDirectory();
    const juce::File refDir = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]) : juce::File();

    bool allSame = checkParameters();
    for (const auto& scene : scenes())
    {
        const auto out = run (scene);
        const auto file = outDir.getChildFile (juce::String (scene.name) + ".f32");
        file.replaceWithData (out.data(), out.size() * sizeof (float));

        float peak = 0.0f;
        for (const float x : out)
            peak = juce::jmax (peak, std::abs (x));
        std::printf ("%-26s peak %.3f", scene.name, (double) peak);

        if (refDir != juce::File())
        {
            juce::MemoryBlock ref;
            refDir.getChildFile (file.getFileName()).loadFileAsData (ref);
            if (ref.getSize() != out.size() * sizeof (float))
            {
                std::printf ("  DIFFERENT LENGTH\n");
                allSame = false;
                continue;
            }
            const auto* r = static_cast<const float*> (ref.getData());
            float worst = 0.0f;
            size_t first = out.size();
            for (size_t i = 0; i < out.size(); ++i)
                if (std::memcmp (&r[i], &out[i], sizeof (float)) != 0)
                {
                    first = std::min (first, i);
                    worst = juce::jmax (worst, std::abs (r[i] - out[i]));
                }
            if (first == out.size())
                std::printf ("  identical");
            else
            {
                std::printf ("  DIFFERS from %.4f s, by up to %g", (double) first / 2.0 / rate, (double) worst);
                allSame = false;
            }
        }
        std::printf ("\n");
    }
    return allSame ? 0 : 1;
}
