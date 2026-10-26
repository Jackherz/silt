#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SiltSamples.h"

namespace
{
// The factory samples (resources/samples), with the note each plays at as
// recorded: ROOT is set to it when one loads.
struct FactorySample
{
    const char* name;
    const char* data;
    int size;
    int root;
};

const std::array<FactorySample, 4>& factorySamples()
{
    static const std::array<FactorySample, 4> list { {
        { "Ahhh Progression", SiltSamples::ahhhprogression_flac, SiltSamples::ahhhprogression_flacSize, 64 },   // E minor
        { "Keys Drone", SiltSamples::keysdrone_flac, SiltSamples::keysdrone_flacSize, 55 },                     // G
        { "Rain Bells", SiltSamples::rainbells_flac, SiltSamples::rainbells_flacSize, 63 },                     // D sharp
        { "Guitar Harmonics", SiltSamples::guitarharmonics_flac, SiltSamples::guitarharmonics_flacSize, 59 },   // B minor
    } };
    return list;
}

// The 24 chain orders, in permutation order: 0 is RES, FILTER, COLOR, SPACE.
using ChainOrder = std::array<int, SiltProcessor::numModules>;
const std::array<ChainOrder, SiltProcessor::numChainOrders>& chainOrders()
{
    static const auto list = []
    {
        std::array<ChainOrder, SiltProcessor::numChainOrders> orders {};
        ChainOrder order { 0, 1, 2, 3 };
        for (auto& o : orders)
        {
            o = order;
            std::next_permutation (order.begin(), order.end());
        }
        return orders;
    }();
    return list;
}
} // namespace

namespace
{
using Set = juce::AudioChannelSet;
using Float = juce::AudioParameterFloat;
using Range = juce::NormalisableRange<float>;

constexpr int stateMagic = 0x544c4953;   // "SILT"

// Synced Size and Rate: note values, in quarter notes (T = triplet, D = dotted).
const juce::StringArray divisionNames { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T",
                                        "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 bar", "2 bars", "4 bars" };
constexpr double divisionQuarters[] { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2, 2.0 / 3,
                                      3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 3.0, 4.0, 8.0, 16.0 };
constexpr int sixteenth = 3, eighth = 6, dottedEighth = 8, quarter = 9;
constexpr double maxDelayMs = 4000.0;

// In silt::Scale order.
const juce::StringArray scaleNames { "Off", "Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian",
                                     "Mixolydian", "Major pent.", "Minor pent.", "Whole tone", "Octaves", "Fifths" };
const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr int rateModeSync = 1, rateModeGrains = 2;   // "rateMode" choices; 0 is Free
constexpr double maxSyncedSizeMs = 4000.0;

double quartersFor (const std::atomic<float>* choice) noexcept
{
    return divisionQuarters[juce::jlimit (0, (int) std::size (divisionQuarters) - 1, (int) choice->load (std::memory_order_relaxed))];
}
const juce::StringArray captureBarChoices { "1 bar", "2 bars", "4 bars", "8 bars" };
constexpr int captureBars[] { 1, 2, 4, 8 };

Range skewed (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

// How values read on the panel and in the host: a few significant digits and the unit.
using Format = std::function<juce::String (float, int)>;

juce::String formatMs (float v, int)
{
    // juce::String (v, 0) means "no limit", not "no decimals": round instead.
    if (v >= 1000.0f)
        return juce::String (v / 1000.0f, 2) + " s";
    return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms";
}

juce::String formatHz (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; }
juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
juce::String formatSemitones (float v, int) { return (v >= 0.005f ? "+" : v <= -0.005f ? "" : " ") + juce::String (std::abs (v) < 0.005f ? 0.0f : v, 2) + " st"; }
juce::String formatTimes (float v, int) { return juce::String (v, 2) + "x"; }
juce::String formatDb (float v, int) { return v <= -47.95f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; }

std::unique_ptr<Float> percent (const juce::String& id, const juce::String& name, float def)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, Range (0.0f, 100.0f, 0.1f), def,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPercent));
}

std::unique_ptr<Float> withUnit (const juce::String& id, const juce::String& name, Range range, float def, Format format)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, range, def,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (format)));
}

juce::AudioProcessorValueTreeState::ParameterLayout makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Grain
    layout.add (withUnit ("pitch", "Pitch", Range (-24.0f, 24.0f, 0.01f), 0.0f, formatSemitones),
                withUnit ("size", "Size", skewed (10.0f, 2000.0f, 150.0f), 100.0f, formatMs),
                withUnit ("rate", "Rate", skewed (0.1f, 100.0f, 10.0f), 20.0f, formatHz),
                percent ("spray", "Spray", 0.0f),
                percent ("jitter", "Jitter", 50.0f),
                percent ("reverse", "Reverse", 0.0f),
                percent ("randAmp", "Random amp", 0.0f),
                percent ("randPan", "Random pan", 0.0f),
                percent ("fade", "Fade", 100.0f),
                percent ("slant", "Slant", 50.0f),
                percent ("ease", "Ease", 50.0f));

    // Tempo sync for Size and Rate
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sizeSync", 1 }, "Size sync", false),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sizeDiv", 1 }, "Size (synced)",
                                                              divisionNames, sixteenth),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rateMode", 1 }, "Rate mode",
                                                              juce::StringArray { "Free", "Sync", "Grains" }, 0),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rateDiv", 1 }, "Rate (synced)",
                                                              divisionNames, sixteenth),
                // Size in Grains mode: its own range, with half the knob below 60 ms
                // where the texture changes most.
                withUnit ("grainSize", "Size (grains)", skewed (10.0f, 500.0f, 60.0f), 80.0f, formatMs),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "grains", 1 }, "Grains", 1, 32, 8,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int n, int)
                    {
                        return juce::String (n) + (n == 1 ? " grain" : " grains");
                    })));

    // Region and playhead
    // Start and Length in fine steps (0.001 %: a third of a millisecond on 30 s).
    const auto finePercent = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, Range (0.0f, 100.0f, 0.001f), def,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                            [] (float v, int) { return juce::String (v, 2) + "%"; }));
    };
    layout.add (finePercent ("start", "Start", 0.0f),
                finePercent ("length", "Length", 100.0f),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "snap", 1 }, "Snap",
                                                              juce::StringArray { "Off", "Zero crossing", "Transient" }, 0),
                withUnit ("scan", "Scan", Range (-4.0f, 4.0f, 0.01f), 1.0f, formatTimes),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "endMode", 1 }, "End",
                                                              juce::StringArray { "Loop", "Hold", "Stop" }, 0));

    // Voice
    layout.add (withUnit ("attack", "Attack", skewed (0.0f, 5000.0f, 100.0f), 5.0f, formatMs),
                withUnit ("decay", "Decay", skewed (1.0f, 5000.0f, 200.0f), 200.0f, formatMs),
                percent ("sustain", "Sustain", 100.0f),
                withUnit ("release", "Release", skewed (1.0f, 10000.0f, 400.0f), 300.0f, formatMs),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "root", 1 }, "Root note", 0, 127, 60,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int note, int)
                    {
                        return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
                    })));

    // Pitch page: random pitch, scale snapping, pattern, heads.
    const auto semitonesSpread = [] (float v, int) { return (v < 0.05f ? juce::String ("0") : juce::String::fromUTF8 ("\xc2\xb1") + juce::String (v, 1)) + " st"; };
    const auto semitonesDepth = [] (float v, int) { return juce::String (v, 1) + " st"; };
    layout.add (withUnit ("randPitch", "Random pitch", Range (0.0f, 24.0f, 0.01f), 0.0f, semitonesSpread),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scale", 1 }, "Scale", scaleNames, 0),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "key", 1 }, "Key", keyNames, 0),
                withUnit ("patternDepth", "Pattern", Range (0.0f, 24.0f, 0.01f), 0.0f, semitonesDepth),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "patternSteps", 1 }, "Pattern steps", 2, 16, 8,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int n, int) { return juce::String (n) + " steps"; })),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "patternSeed", 1 }, "Pattern seed", 1, 9999, 1),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "heads", 1 }, "Heads", 1, 8, 1,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int n, int) { return juce::String (n) + (n == 1 ? " head" : " heads"); })),
                percent ("headSpread", "Head spread", 50.0f));

    // Mode: the sample (PITCH) or the live input (LIVE: a granular delay).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "keyMode", 1 }, "Mode",
                                                              juce::StringArray { "Pitch", "Live" },
                                                              SiltProcessor::isInstrument ? 0 : 1),
                withUnit ("liveDelay", "Delay", skewed (10.0f, 4000.0f, 300.0f), 250.0f, formatMs),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delaySync", 1 }, "Delay sync", false),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "delayDiv", 1 }, "Delay (synced)",
                                                              divisionNames, eighth),
                percent ("feedback", "Feedback", 0.0f),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, "Freeze", false),
                // With no keys held: nothing, a constant voice, or the input playing the envelope.
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "liveRun", 1 }, "No notes",
                                                              juce::StringArray { "Off", "Always", "Follow" },
                                                              SiltProcessor::isInstrument ? 1 : 2));

    // Resonator: a bank of ringing bands after the grains.
    layout.add (withUnit ("resTune", "Resonator tune", Range (24.0f, 96.0f, 0.01f), 48.0f, [] (float v, int)
                {
                    const int note = juce::roundToInt (v);
                    const int cents = juce::roundToInt ((v - (float) note) * 100.0f);
                    return juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                         + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents) + "c");
                }),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "resNotes", 1 }, "Resonator notes",
                                                              juce::StringArray { "Scale", "Harmonics", "Bell", "Sample" }, 0),
                withUnit ("resRange", "Resonator range", Range (1.0f, 4.0f, 0.01f), 2.0f,
                          [] (float v, int) { return juce::String (v, 1) + " oct"; }),
                withUnit ("resDecay", "Resonator decay", skewed (0.05f, 20.0f, 1.5f), 1.5f,
                          [] (float v, int) { return formatMs (v * 1000.0f, 0); }),
                percent ("resBright", "Resonator bright", 50.0f),
                percent ("resWidth", "Resonator width", 50.0f),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "resFollow", 1 }, "Resonator follows notes", false),
                percent ("resMix", "Resonator mix", 0.0f));

    // Resonator, page 2: the bank's character.
    layout.add (percent ("resReso", "Resonator resonance", 50.0f),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "resBands", 1 }, "Resonator bands", 1, silt::Resonator::maxBands,
                                                           silt::Resonator::maxBands),
                percent ("resDrift", "Resonator drift", 0.0f),
                withUnit ("resDriftRate", "Resonator drift rate", skewed (0.05f, 10.0f, 0.7f), 0.5f,
                          [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }),
                percent ("resSpread", "Resonator spread", 0.0f),
                withUnit ("resOddEven", "Resonator odd/even", Range (-100.0f, 100.0f, 0.1f), 0.0f, [] (float v, int)
                {
                    const int n = juce::roundToInt (v);
                    return n < 0 ? "ODD " + juce::String (-n) + "%" : n > 0 ? "EVEN " + juce::String (n) + "%" : juce::String ("ALL");
                }),
                percent ("resDrive", "Resonator drive", 0.0f),
                percent ("resDamp", "Resonator damp", 0.0f));

    // Filter: after the resonator.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "filterMode", 1 }, "Filter",
                                                              juce::StringArray { "Off", "12 dB", "24 dB" }, 0),
                withUnit ("filterCutoff", "Filter cutoff", skewed (20.0f, 20000.0f, 1000.0f), 1000.0f, [] (float v, int)
                {
                    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                        : juce::String (juce::roundToInt (v)) + " Hz";
                }),
                percent ("filterReso", "Filter resonance", 0.0f),
                withUnit ("filterMorph", "Filter morph", Range (0.0f, 2.0f, 0.001f), 0.0f, [] (float v, int)
                {
                    const char* names[] { "LP", "BP", "HP" };
                    const int nearest = juce::roundToInt (v);
                    if (std::abs (v - (float) nearest) < 0.02f)
                        return juce::String (names[nearest]);
                    const int lower = juce::jmin (1, (int) v);
                    return juce::String (names[lower]) + "-" + names[lower + 1] + " " + juce::String (juce::roundToInt ((v - (float) lower) * 100.0f)) + "%";
                }),
                percent ("filterDrive", "Filter drive", 0.0f),
                withUnit ("filterEnv", "Filter envelope", Range (-100.0f, 100.0f, 0.1f), 0.0f, [] (float v, int)
                {
                    const int n = juce::roundToInt (v);
                    return (n > 0 ? "+" : "") + juce::String (n) + "%";
                }),
                percent ("filterKey", "Filter key tracking", 0.0f),
                percent ("filterMix", "Filter mix", 100.0f));

    // Color: after the filter.
    layout.add (percent ("colorDrive", "Color drive", 0.0f),
                withUnit ("colorTone", "Color tone", Range (-100.0f, 100.0f, 0.1f), 0.0f, [] (float v, int)
                {
                    const int n = juce::roundToInt (v);
                    return n == 0 ? juce::String ("FLAT") : (n < 0 ? "DARK " : "BRIGHT ") + juce::String (std::abs (n)) + "%";
                }),
                withUnit ("colorBits", "Color bits", Range (2.0f, 16.0f, 0.01f), 16.0f, [] (float v, int)
                {
                    return v >= 15.995f ? juce::String ("OFF") : juce::String (v, 1) + " bits";
                }),
                withUnit ("colorRate", "Color rate", skewed (200.0f, silt::Color::rateOff, 4000.0f), silt::Color::rateOff, [] (float v, int)
                {
                    if (v >= silt::Color::rateOff - 1.0f)
                        return juce::String ("OFF");
                    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                        : juce::String (juce::roundToInt (v)) + " Hz";
                }),
                percent ("colorComp", "Color comp", 0.0f),
                percent ("colorNoise", "Color noise", 0.0f),
                percent ("colorWow", "Color wow", 0.0f),
                percent ("colorMix", "Color mix", 100.0f));

    // Space: delay and reverb sends, last in the chain.
    layout.add (withUnit ("spaceTime", "Delay time", skewed (10.0f, 4000.0f, 400.0f), 375.0f, formatMs),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "spaceSync", 1 }, "Delay sync", false),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "spaceDiv", 1 }, "Delay time (synced)",
                                                              divisionNames, dottedEighth),
                percent ("spaceFeedback", "Delay feedback", 35.0f),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "spaceMode", 1 }, "Delay mode",
                                                              juce::StringArray { "Ping-pong", "Stereo" }, 0),
                percent ("spaceDelay", "Delay level", 0.0f),
                percent ("spaceSize", "Reverb size", 50.0f),
                withUnit ("spaceDecay", "Reverb decay", skewed (0.3f, 30.0f, 3.0f), 3.0f,
                          [] (float v, int) { return formatMs (v * 1000.0f, 0); }),
                withUnit ("spaceTone", "Space tone", Range (0.0f, 100.0f, 0.1f), 50.0f, [] (float v, int)
                {
                    const int n = juce::roundToInt (v);
                    return n < 50 ? "DARK " + juce::String ((50 - n) * 2) + "%"
                         : n > 50 ? "BRIGHT " + juce::String ((n - 50) * 2) + "%" : juce::String ("NATURAL");
                }),
                percent ("spaceShimmer", "Reverb shimmer", 0.0f),
                percent ("spaceReverb", "Reverb level", 0.0f),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "spaceFreeze", 1 }, "Space freeze", false),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "spaceDuck", 1 }, "Space duck",
                                                              juce::StringArray { "Off", "Light", "Medium", "Heavy" }, 0));

    // CHAIN: the order the four modules run in, one of the 24.
    {
        juce::StringArray orders;
        for (int i = 0; i < SiltProcessor::numChainOrders; ++i)
            orders.add (SiltProcessor::chainOrderName (i));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chainOrder", 1 }, "Chain order", orders, 0));
    }

    // Modulators 1–4 (MOD page). Their own settings aren't modulation targets.
    for (int n = 1; n <= SiltProcessor::numModulators; ++n)
    {
        const auto id = [n] (const char* suffix) { return "m" + juce::String (n) + suffix; };
        const auto name = [n] (const char* what) { return "Mod " + juce::String (n) + " " + what; };
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Type"), 1 }, name ("type"),
                                                                  juce::StringArray { "Wave", "Random", "Envelope", "Follow", "MIDI", "Macro" }, 0),
                    // Through zero: −50…+50 Hz on a cubic (most of the travel at slow rates,
                    // smooth through 0, so modulation can sweep across it). Below zero
                    // a WAVE runs backwards; near 0 it holds (STOP).
                    withUnit (id ("Rate"), name ("rate"),
                              Range (-50.0f, 50.0f,
                                     [] (float, float, float x) { const float u = 2.0f * x - 1.0f; return 50.0f * u * u * u; },
                                     [] (float, float, float v) { return 0.5f * (std::cbrt (juce::jlimit (-50.0f, 50.0f, v) / 50.0f) + 1.0f); },
                                     [] (float, float, float v) { return std::abs (v) < 0.01f ? 0.0f : v; }),
                              1.0f, [] (float v, int) { return std::abs (v) < 0.005f ? juce::String ("STOP") : (v < 0.0f ? "-" : "") + formatHz (std::abs (v), 0); }),
                    std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Sync"), 1 }, name ("sync"), false),
                    std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Div"), 1 }, name ("rate (synced)"), divisionNames, quarter),
                    withUnit (id ("Shape"), name ("shape"), Range (0.0f, 3.0f, 0.001f), 0.0f, [] (float v, int)
                    {
                        const char* names[] { "SINE", "TRI", "SAW", "SQUARE" };
                        const int nearest = juce::roundToInt (v);
                        if (std::abs (v - (float) nearest) < 0.02f)
                            return juce::String (names[nearest]);
                        const int lower = juce::jmin (2, (int) v);
                        return juce::String (names[lower]) + "-" + names[lower + 1] + " " + juce::String (juce::roundToInt ((v - (float) lower) * 100.0f)) + "%";
                    }),
                    withUnit (id ("Phase"), name ("phase"), Range (0.0f, 360.0f, 0.1f), 0.0f,
                              [] (float v, int) { return juce::String (juce::roundToInt (v)) + juce::String::charToString ((juce::juce_wchar) 0x00b0); }),
                    std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Retrig"), 1 }, name ("retrigger"), false),
                    std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Rev"), 1 }, name ("reverse (synced)"), false),
                    percent (id ("Smooth"), name ("smooth"), 100.0f),
                    percent (id ("Jitter"), name ("jitter"), 0.0f),
                    withUnit (id ("Attack"), name ("attack"), skewed (0.0f, 5000.0f, 100.0f), 10.0f, formatMs),
                    withUnit (id ("Decay"), name ("decay"), skewed (1.0f, 5000.0f, 200.0f), 300.0f, formatMs),
                    percent (id ("Sustain"), name ("sustain"), 50.0f),
                    withUnit (id ("Release"), name ("release"), skewed (1.0f, 10000.0f, 400.0f), 500.0f, formatMs),
                    std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Repeat"), 1 }, name ("repeat"), false),
                    withUnit (id ("FAttack"), name ("follow attack"), skewed (0.1f, 1000.0f, 30.0f), 10.0f, formatMs),
                    withUnit (id ("FRelease"), name ("follow release"), skewed (1.0f, 3000.0f, 200.0f), 200.0f, formatMs),
                    withUnit (id ("Gain"), name ("follow gain"), Range (-24.0f, 24.0f, 0.1f), 0.0f, formatDb),
                    std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Source"), 1 }, name ("MIDI source"),
                                                                  juce::StringArray { "Velocity", "Note", "Mod wheel", "Aftertouch", "Pitch bend" }, 0),
                    // Muted: contributes nothing; its routes are kept.
                    std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Enable"), 1 }, name ("on"), true),
                    // Per voice: each voice runs its own copy from its own note (grain
                    // parameters only; the effects follow the shared one).
                    std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Voice"), 1 }, name ("per voice"), false),
                    percent (id ("Macro"), name ("macro"), 0.0f),
                    // WAVE: where the cycle turns round (50%: as drawn) and how it bends (0%: straight).
                    percent (id ("Sym"), name ("symmetry"), 50.0f),
                    withUnit (id ("Curve"), name ("curve"), Range (-100.0f, 100.0f, 0.1f), 0.0f, [] (float v, int)
                    {
                        const int whole = juce::roundToInt (v);
                        return (whole > 0 ? "+" : "") + juce::String (whole) + "%";
                    }));
    }

    // Capture and output
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "captureBars", 1 }, "Capture length",
                                                              captureBarChoices, 1),
                withUnit ("gain", "Output", Range (-48.0f, 12.0f, 0.1f), 0.0f, formatDb),
                // The sample's own level, before the grains go anywhere: for samples
                // that come in too loud or too soft (the screen's GAIN).
                withUnit ("sampleGain", "Sample gain", Range (-24.0f, 24.0f, 0.1f), 0.0f, [] (float v, int)
                {
                    return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
                }));

    if constexpr (SiltProcessor::isInstrument)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "monitor", 1 }, "Monitor input", false));
    else
        layout.add (percent ("mix", "Mix", 100.0f));

    return layout;
}

bool isMonoOrStereo (const Set& set) { return set == Set::mono() || set == Set::stereo(); }

// Linear to −1.9 dBFS, then a soft knee to full scale (plan gap 7: a safety
// net for dense patches, not a sound).
float safetyClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    return std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}
} // namespace

struct SiltProcessor::RawParams
{
    // lookup: where each parameter's value is read from (its modulated value,
    // for modulation targets).
    explicit RawParams (const std::function<std::atomic<float>* (const char*)>& p)
    {
        pitch = p ("pitch"); size = p ("size"); rate = p ("rate"); spray = p ("spray"); jitter = p ("jitter");
        reverse = p ("reverse"); randAmp = p ("randAmp"); randPan = p ("randPan"); fade = p ("fade"); slant = p ("slant"); ease = p ("ease");
        sizeSync = p ("sizeSync"); sizeDiv = p ("sizeDiv"); rateMode = p ("rateMode"); rateDiv = p ("rateDiv"); grains = p ("grains"); grainSize = p ("grainSize");
        start = p ("start"); length = p ("length"); scan = p ("scan"); endMode = p ("endMode"); snap = p ("snap");
        attack = p ("attack"); decay = p ("decay"); sustain = p ("sustain"); release = p ("release"); root = p ("root");
        captureBars = p ("captureBars"); gain = p ("gain");
        keyMode = p ("keyMode"); liveDelay = p ("liveDelay"); delaySync = p ("delaySync"); delayDiv = p ("delayDiv");
        feedback = p ("feedback"); freeze = p ("freeze"); liveRun = p ("liveRun");
        randPitch = p ("randPitch"); scale = p ("scale"); key = p ("key"); patternDepth = p ("patternDepth");
        patternSteps = p ("patternSteps"); patternSeed = p ("patternSeed"); heads = p ("heads"); headSpread = p ("headSpread");
        resTune = p ("resTune"); resNotes = p ("resNotes"); resRange = p ("resRange"); resDecay = p ("resDecay");
        resBright = p ("resBright"); resWidth = p ("resWidth"); resFollow = p ("resFollow"); resMix = p ("resMix");
        resReso = p ("resReso"); resBands = p ("resBands"); resDrift = p ("resDrift"); resDriftRate = p ("resDriftRate");
        resSpread = p ("resSpread"); resOddEven = p ("resOddEven"); resDrive = p ("resDrive"); resDamp = p ("resDamp");
        filterMode = p ("filterMode"); filterCutoff = p ("filterCutoff"); filterReso = p ("filterReso"); filterMorph = p ("filterMorph");
        filterDrive = p ("filterDrive"); filterEnv = p ("filterEnv"); filterKey = p ("filterKey"); filterMix = p ("filterMix");
        colorDrive = p ("colorDrive"); colorTone = p ("colorTone"); colorBits = p ("colorBits"); colorRate = p ("colorRate");
        colorComp = p ("colorComp"); colorNoise = p ("colorNoise"); colorWow = p ("colorWow"); colorMix = p ("colorMix");
        spaceTime = p ("spaceTime"); spaceSync = p ("spaceSync"); spaceDiv = p ("spaceDiv"); spaceFeedback = p ("spaceFeedback");
        spaceMode = p ("spaceMode"); spaceDelay = p ("spaceDelay"); spaceSize = p ("spaceSize"); spaceDecay = p ("spaceDecay");
        spaceTone = p ("spaceTone"); spaceShimmer = p ("spaceShimmer"); spaceReverb = p ("spaceReverb"); spaceFreeze = p ("spaceFreeze"); spaceDuck = p ("spaceDuck");
        monitor = p ("monitor"); mix = p ("mix");   // one of these is null, per build
        chainOrder = p ("chainOrder");
    }

    std::atomic<float>* pitch, *size, *rate, *spray, *jitter, *reverse, *randAmp, *randPan, *fade, *slant, *ease;
    std::atomic<float>* sizeSync, *sizeDiv, *rateMode, *rateDiv, *grains, *grainSize;
    std::atomic<float>* start, *length, *scan, *endMode, *snap;
    std::atomic<float>* attack, *decay, *sustain, *release, *root;
    std::atomic<float>* captureBars, *gain, *monitor, *mix, *chainOrder;
    std::atomic<float>* keyMode, *liveDelay, *delaySync, *delayDiv, *feedback, *freeze, *liveRun;
    std::atomic<float>* randPitch, *scale, *key, *patternDepth, *patternSteps, *patternSeed, *heads, *headSpread;
    std::atomic<float>* resTune, *resNotes, *resRange, *resDecay, *resBright, *resWidth, *resFollow, *resMix;
    std::atomic<float>* resReso, *resBands, *resDrift, *resDriftRate, *resSpread, *resOddEven, *resDrive, *resDamp;
    std::atomic<float>* filterMode, *filterCutoff, *filterReso, *filterMorph, *filterDrive, *filterEnv, *filterKey, *filterMix;
    std::atomic<float>* colorDrive, *colorTone, *colorBits, *colorRate, *colorComp, *colorNoise, *colorWow, *colorMix;
    std::atomic<float>* spaceTime, *spaceSync, *spaceDiv, *spaceFeedback, *spaceMode, *spaceDelay, *spaceSize, *spaceDecay;
    std::atomic<float>* spaceTone, *spaceShimmer, *spaceReverb, *spaceFreeze, *spaceDuck;
};

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
      params (*this, nullptr, "SILT", makeParameters()),
      voiceModulation (*this)
{
    // Modulation targets: continuous and stepped parameters (see the header).
    // Each gets a modulated copy, which is what the audio code reads.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            const auto id = ranged->getParameterID();
            // Modulators' own continuous settings are targets too (cross-modulation),
            // except MACRO, which stays a plain knob.
            const bool macro = id.length() > 1 && id[0] == 'm' && juce::CharacterFunctions::isDigit (id[1]) && id.endsWith ("Macro");
            const bool switchLike = dynamic_cast<juce::AudioParameterChoice*> (ranged) != nullptr
                                 || dynamic_cast<juce::AudioParameterBool*> (ranged) != nullptr;
            if (switchLike || macro || id == "patternSeed" || id == "sampleGain")
                continue;
            if (targets.size() >= (size_t) maxTargets)
            {
                jassertfalse;   // raise maxTargets; past it, parameters simply aren't targets
                break;
            }
            targetIndex[id] = (int) targets.size();
            targets.push_back ({ ranged, params.getRawParameterValue (id) });
        }
    for (size_t t = 0; t < targets.size(); ++t)
        modulated[t].store (targets[t].raw->load());

    // Grain parameters: the ones voices read, so per-voice modulation reaches them.
    for (const auto* id : { "pitch", "size", "rate", "spray", "jitter", "reverse", "randAmp", "randPan", "fade", "slant", "ease",
                            "grains", "grainSize", "start", "length", "scan", "attack", "decay", "sustain", "release", "root",
                            "randPitch", "patternDepth", "patternSteps", "heads", "headSpread", "liveDelay", "feedback" })
        if (const int t = modulationTarget (id); t >= 0)
            voiceTargets.push_back (t);

    for (int m = 0; m < numModulators; ++m)
    {
        // Modulated values where a setting is a target: modulators read each
        // other (and themselves) as of the previous chunk, so order never matters.
        const auto p = [this, m] (const char* suffix)
        {
            const auto id = "m" + juce::String (m + 1) + suffix;
            if (const auto it = targetIndex.find (id); it != targetIndex.end())
                return &modulated[(size_t) it->second];
            return params.getRawParameterValue (id);
        };
        modulatorRaw[(size_t) m] = { p ("Type"), p ("Rate"), p ("Sync"), p ("Div"), p ("Shape"), p ("Phase"), p ("Retrig"), p ("Smooth"), p ("Jitter"),
                                     p ("Attack"), p ("Decay"), p ("Sustain"), p ("Release"), p ("Repeat"), p ("FAttack"), p ("FRelease"), p ("Gain"), p ("Source"),
                                     p ("Enable"), p ("Voice"), p ("Macro"), p ("Sym"), p ("Curve"), p ("Rev") };
    }

    for (auto* parameter : getParameters())
        parameter->addListener (this);
    sampleGain = params.getRawParameterValue ("sampleGain");

    raw = std::make_unique<RawParams> ([this] (const char* id) -> std::atomic<float>*
    {
        if (const auto it = targetIndex.find (id); it != targetIndex.end())
            return &modulated[(size_t) it->second];
        return params.getRawParameterValue (id);
    });

    formats.registerBasicFormats();
    for (auto& v : voicePlayheads)
        v.store (-1.0f);
    for (size_t i = 0; i < knobCCs.size(); ++i)
        knobCCs[i].store (70 + (int) i);   // a common default for 8-knob controllers
    lastKnobValue.fill (-1);
    startTimerHz (30);

    // A new instance opens with the first factory sample, unless the host
    // restores a state (or a sample arrives) before it's ready.
    loadInBackground ([] { return openFactorySample (0); }, factorySampleName (0), 0, factorySamples()[0].root, true);
}

SiltProcessor::~SiltProcessor()
{
    for (auto* parameter : getParameters())
        parameter->removeListener (this);
    stopTimer();
    loader.removeAllJobs (true, 5000);
}

void SiltProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, juce::jmax (1, maximumExpectedSamplesPerBlock));
    voiceModulation.reset();
    engine.setVoiceParams (&voiceModulation);
    chainNow = juce::jlimit (0, numChainOrders - 1, (int) raw->chainOrder->load());
    chainFade = 1.0f;
    resonator.prepare (sampleRate);
    filter.prepare (sampleRate);
    color.prepare (sampleRate);
    space.prepare (sampleRate);
    for (size_t m = 0; m < modulators.size(); ++m)
        modulators[m].reset (0x5117 + m);
    modContext = {};
    lastNote = -1;
    followRoot = -1;
    followChordSize = 0;
    liveColumn = -1;
    liveColumnPeak = 0.0f;
    for (auto& c : livePeaks)
        c.store (0.0f);
    wet.setSize (2, juce::jmax (modulationInterval, maximumExpectedSamplesPerBlock));
    noteEvents.reserve (1024);
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

// voiceValues (one voice's modulated values, by target) stands in for the
// shared modulated values when given.
silt::GrainParams SiltProcessor::readParams (const float* voiceValues) const noexcept
{
    const auto* first = modulated.data();
    const auto v = [first, voiceValues] (const std::atomic<float>* a)
    {
        if (voiceValues != nullptr && std::less_equal<>() (first, a) && std::less<>() (a, first + maxTargets))
            return (double) voiceValues[a - first];
        return (double) a->load (std::memory_order_relaxed);
    };

    silt::GrainParams p;
    p.pitchSemitones = v (raw->pitch);
    p.sizeMs = v (raw->size);
    p.rateHz = v (raw->rate);
    p.spray = v (raw->spray) * 0.01;
    p.jitter = v (raw->jitter) * 0.01;
    p.reverseChance = v (raw->reverse) * 0.01;
    p.randomAmp = v (raw->randAmp) * 0.01;
    p.randomPan = v (raw->randPan) * 0.01;
    p.fade = v (raw->fade) * 0.01;
    p.slant = v (raw->slant) * 0.01;
    p.ease = v (raw->ease) * 0.01;
    p.start = v (raw->start) * 0.01;
    p.length = v (raw->length) * 0.01;
    p.scan = v (raw->scan);
    p.endMode = (silt::EndMode) juce::jlimit (0, 2, (int) v (raw->endMode));
    p.snap = (silt::Snap) juce::jlimit (0, 2, (int) v (raw->snap));
    p.attackMs = v (raw->attack);
    p.decayMs = v (raw->decay);
    p.sustain = v (raw->sustain) * 0.01;
    p.releaseMs = v (raw->release);
    p.rootNote = (int) v (raw->root);

    p.randomPitch = v (raw->randPitch);
    p.scale = (silt::Scale) juce::jlimit (0, silt::scaleCount - 1, (int) v (raw->scale));
    p.key = juce::jlimit (0, 11, (int) v (raw->key));
    p.patternDepth = v (raw->patternDepth);
    p.patternSteps = (int) v (raw->patternSteps);
    p.patternSeed = (std::uint32_t) v (raw->patternSeed);
    p.heads = juce::jlimit (1, 8, (int) v (raw->heads));
    p.headSpread = v (raw->headSpread) * 0.01;

    p.live = (int) v (raw->keyMode) == 1;
    p.feedback = v (raw->feedback) * 0.01;
    p.freeze = v (raw->freeze) > 0.5;
    p.liveTrigger = (silt::LiveTrigger) juce::jlimit (0, 2, (int) v (raw->liveRun));

    // Synced values follow the host tempo (the last one seen while stopped).
    const double msPerQuarter = 60000.0 / juce::jlimit (20.0, 400.0, hostBpm.load (std::memory_order_relaxed));
    p.liveDelayMs = v (raw->delaySync) > 0.5 ? juce::jlimit (10.0, maxDelayMs, quartersFor (raw->delayDiv) * msPerQuarter)
                                              : v (raw->liveDelay);
    if (v (raw->sizeSync) > 0.5)
        p.sizeMs = juce::jlimit (10.0, maxSyncedSizeMs, quartersFor (raw->sizeDiv) * msPerQuarter);
    switch ((int) v (raw->rateMode))
    {
        case rateModeSync:
            p.gridDivision = quartersFor (raw->rateDiv);
            p.rateHz = 1000.0 / (p.gridDivision * msPerQuarter);
            break;
        case rateModeGrains:
            p.grainCount = (int) v (raw->grains);
            if (v (raw->sizeSync) < 0.5)
                p.sizeMs = v (raw->grainSize);
            break;
        default:
            break;
    }
    return p;
}

// Audio thread. The bands use the PITCH page's scale and key; FOLLOW tunes the
// lowest band to the last note played (until one is, TUNE).
silt::ResonatorParams SiltProcessor::readResonatorParams (const silt::GrainParams& grain) const noexcept
{
    const auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    silt::ResonatorParams p;
    const bool follow = v (raw->resFollow) > 0.5f && followRoot >= 0;
    p.rootNote = follow ? (float) followRoot : v (raw->resTune);
    p.notes = (silt::ResonatorNotes) juce::jlimit (0, 3, (int) v (raw->resNotes));
    if (follow)
    {
        p.chord = followChord;
        p.chordSize = followChordSize;
    }

    // SAMPLE: the sample's pitches, moved by TUNE from its default (C2), or
    // with FOLLOW by the note played against ROOT (as the grains are).
    const float shift = follow ? (float) followRoot - v (raw->root) : v (raw->resTune) - 48.0f;
    const float ratio = std::pow (2.0f, shift / 12.0f);
    p.sampleCount = juce::jlimit (0, (int) p.sampleHz.size(), samplePitchCount.load (std::memory_order_relaxed));
    for (int i = 0; i < p.sampleCount; ++i)
        p.sampleHz[(size_t) i] = samplePitchHz[(size_t) i].load (std::memory_order_relaxed) * ratio;
    p.scale = grain.scale;
    p.key = grain.key;
    p.rangeOctaves = v (raw->resRange);
    p.decaySeconds = v (raw->resDecay);
    p.bright = v (raw->resBright) * 0.01f;
    p.width = v (raw->resWidth) * 0.01f;
    p.mix = v (raw->resMix) * 0.01f;
    p.resonance = v (raw->resReso) * 0.01f;
    p.bandCount = juce::roundToInt (v (raw->resBands));
    p.drift = v (raw->resDrift) * 0.01f;
    p.driftRate = v (raw->resDriftRate);
    p.spread = v (raw->resSpread) * 0.01f;
    p.oddEven = v (raw->resOddEven) * 0.01f;
    p.drive = v (raw->resDrive) * 0.01f;
    p.damp = v (raw->resDamp) * 0.01f;
    return p;
}

// Audio thread. KEY tracks the last note played around middle C.
silt::FilterParams SiltProcessor::readFilterParams() const noexcept
{
    const auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    silt::FilterParams p;
    p.slope = (silt::FilterSlope) juce::jlimit (0, 2, (int) v (raw->filterMode));
    p.cutoffHz = v (raw->filterCutoff);
    p.resonance = v (raw->filterReso) * 0.01f;
    p.morph = v (raw->filterMorph);
    p.drive = v (raw->filterDrive) * 0.01f;
    p.envAmount = v (raw->filterEnv) * 0.01f;
    p.keyOctaves = lastNote >= 0 ? v (raw->filterKey) * 0.01f * (float) (lastNote - 60) / 12.0f : 0.0f;
    p.mix = v (raw->filterMix) * 0.01f;
    return p;
}

// Audio thread.
silt::ColorParams SiltProcessor::readColorParams() const noexcept
{
    const auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    silt::ColorParams p;
    p.drive = v (raw->colorDrive) * 0.01f;
    p.tone = v (raw->colorTone) * 0.01f;
    p.bits = v (raw->colorBits);
    p.rateHz = v (raw->colorRate);
    p.comp = v (raw->colorComp) * 0.01f;
    p.noise = v (raw->colorNoise) * 0.01f;
    p.wow = v (raw->colorWow) * 0.01f;
    p.mix = v (raw->colorMix) * 0.01f;
    return p;
}

// Audio thread. A synced time follows the host tempo (the last one seen while stopped).
silt::SpaceParams SiltProcessor::readSpaceParams() const noexcept
{
    const auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    const double msPerQuarter = 60000.0 / juce::jlimit (20.0, 400.0, hostBpm.load (std::memory_order_relaxed));
    silt::SpaceParams p;
    p.delayMs = v (raw->spaceSync) > 0.5f ? (float) juce::jlimit (10.0, silt::Space::maxDelaySeconds * 1000.0, quartersFor (raw->spaceDiv) * msPerQuarter)
                                          : v (raw->spaceTime);
    p.feedback = v (raw->spaceFeedback) * 0.01f;
    p.delayLevel = v (raw->spaceDelay) * 0.01f;
    p.pingPong = (int) v (raw->spaceMode) == 0;
    p.size = v (raw->spaceSize) * 0.01f;
    p.decaySeconds = v (raw->spaceDecay);
    p.tone = v (raw->spaceTone) * 0.01f;
    p.shimmer = v (raw->spaceShimmer) * 0.01f;
    p.reverbLevel = v (raw->spaceReverb) * 0.01f;
    p.freeze = v (raw->spaceFreeze) > 0.5f;
    static constexpr float duckAmounts[] { 0.0f, 0.35f, 0.65f, 1.0f };
    p.duck = duckAmounts[juce::jlimit (0, 3, juce::roundToInt (v (raw->spaceDuck)))];
    return p;
}

// Audio thread. Armed: wait for the next bar line (or start now when the host
// is stopped), then record until the target is full.
void SiltProcessor::runCapture (const juce::AudioBuffer<float>& capture, const juce::AudioPlayHead::PositionInfo* pos)
{
    auto state = captureState.load (std::memory_order_acquire);

    if (state == CaptureState::cancelRequested)
    {
        captureState.store (CaptureState::cancelled, std::memory_order_release);
        return;
    }

    if (state != CaptureState::armed && state != CaptureState::recording)
        return;

    auto* target = captureTarget.load (std::memory_order_acquire);
    if (target == nullptr)
        return;

    const int numSamples = capture.getNumSamples();
    int offset = 0;

    if (state == CaptureState::armed)
    {
        if (pos != nullptr && pos->getIsPlaying() && pos->getPpqPosition() && pos->getBpm())
        {
            const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature {});
            const double barPpq = sig.numerator * 4.0 / juce::jmax (1, sig.denominator);
            const double ppq = *pos->getPpqPosition();
            const double nextBar = std::ceil (ppq / barPpq - 1.0e-9) * barPpq;
            const double samplesToBar = (nextBar - ppq) * 60.0 / *pos->getBpm() * currentSampleRate;

            if (samplesToBar >= numSamples)
                return;
            offset = juce::jmax (0, (int) samplesToBar);
        }

        captureWritePos = 0;
        captureState.store (CaptureState::recording, std::memory_order_release);
    }

    const int count = juce::jmin (numSamples - offset, target->getNumFrames() - captureWritePos);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* dest = target->getWritePointer (ch) + captureWritePos;
        if (capture.getNumChannels() > 0)
            juce::FloatVectorOperations::copy (dest, capture.getReadPointer (juce::jmin (ch, capture.getNumChannels() - 1), offset), count);
        else
            juce::FloatVectorOperations::clear (dest, count);
    }

    captureWritePos += count;
    captureProgress.store ((float) captureWritePos / (float) target->getNumFrames(), std::memory_order_relaxed);

    if (captureWritePos >= target->getNumFrames())
        captureState.store (CaptureState::done, std::memory_order_release);
}

void SiltProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (auto* playHead = getPlayHead())
        position = playHead->getPosition();

    if (position)
    {
        if (const auto bpm = position->getBpm())
            hostBpm.store (*bpm, std::memory_order_relaxed);
        if (const auto sig = position->getTimeSignature())
        {
            timeSigNumerator.store (sig->numerator, std::memory_order_relaxed);
            timeSigDenominator.store (sig->denominator, std::memory_order_relaxed);
        }
        hostPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
    }

    noteEvents.clear();
    bool allOff = false;
    for (const auto event : midi)
    {
        const auto m = event.getMessage();
        if ((m.isNoteOnOrOff()) && noteEvents.size() < noteEvents.capacity())
            noteEvents.push_back ({ event.samplePosition, m.getNoteNumber(), m.getFloatVelocity(), m.isNoteOn() });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allOff = true;
        else if (m.isController())
        {
            if (m.getControllerNumber() == 1)
                modContext.modWheel = (float) m.getControllerValue() / 127.0f;
            handleController (m.getControllerNumber(), m.getControllerValue());
        }
        else if (m.isChannelPressure())
            modContext.aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
            modContext.aftertouch = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isPitchWheel())
            modContext.pitchBend = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
    }
    if (allOff)
    {
        engine.allNotesOff();
        heldKeys.fill (false);
        modContext.heldNotes = 0;
    }

    auto source = CaptureSource::none;
    auto capture = findCaptureBus (buffer, source);
    captureSource.store (source, std::memory_order_relaxed);
    if (capture.getNumChannels() > 0)
        captureMeter.process (capture.getArrayOfReadPointers(), capture.getNumChannels(), numSamples);
    runCapture (capture, position ? &*position : nullptr);

    // Rendered in chunks of up to 64 samples: modulation updates per chunk and
    // every parameter is read after it.
    auto output = getBusBuffer (buffer, false, 0);
    const bool playing = position && position->getIsPlaying();
    const double blockPpq = position ? position->getPpqPosition().orFallback (0.0) : 0.0;
    const double bpm = position ? position->getBpm().orFallback (hostBpm.load (std::memory_order_relaxed))
                                : hostBpm.load (std::memory_order_relaxed);
    const double ppqPerSample = bpm / 60.0 / currentSampleRate;

    for (int start = 0; start < numSamples; start += modulationInterval)
    {
        const int len = juce::jmin (modulationInterval, numSamples - start);

        // Events for this chunk, shifted to its start; they also feed the modulators.
        const auto first = std::find_if (noteEvents.begin(), noteEvents.end(), [start] (const auto& e) { return e.sampleOffset >= start; });
        const auto last = std::find_if (first, noteEvents.end(), [start, len] (const auto& e) { return e.sampleOffset >= start + len; });
        modContext.noteStarted = false;
        for (auto it = first; it != last; ++it)
        {
            it->sampleOffset -= start;
            // Held keys as a set, so a repeated note-on for a key already down
            // (arps, clip retriggers) can't leave the count stuck above zero.
            auto& held = heldKeys[(size_t) juce::jlimit (0, 127, it->note)];
            if (it->isNoteOn)
            {
                lastNote = it->note;
                modContext.noteStarted = true;
                spaceNoteStarted = true;
                modContext.note = it->note;
                modContext.velocity = it->velocity;
                if (! held)
                    ++modContext.heldNotes;
                held = true;
            }
            else if (held)
            {
                held = false;
                modContext.heldNotes = juce::jmax (0, modContext.heldNotes - 1);
            }
        }
        if (first != last && modContext.heldNotes > 0)
        {
            // FOLLOW's chord: the held notes, lowest first (kept after release, so the bands ring out).
            followRoot = -1;
            followChordSize = 0;
            for (int note = 0; note < 128; ++note)
                if (heldKeys[(size_t) note])
                {
                    if (followRoot < 0)
                        followRoot = note;
                    else if (followChordSize < (int) followChord.size())
                        followChord[(size_t) followChordSize++] = (float) (note - followRoot);
                }
        }
        float peak = 0.0f;
        for (int ch = 0; ch < capture.getNumChannels(); ++ch)
            peak = juce::jmax (peak, capture.getMagnitude (ch, start, len));
        modContext.inputPeak = peak;
        modContext.numSamples = len;
        modContext.sampleRate = currentSampleRate;
        modContext.hostPlaying = playing;
        modContext.bpm = bpm;
        modContext.ppqEnd = blockPpq + (start + len) * ppqPerSample;
        applyModulation();
        voiceModulation.prepareChunk();

        const float gain = juce::Decibels::decibelsToGain (raw->gain->load (std::memory_order_relaxed), -48.0f);
        const bool monitoring = isInstrument && raw->monitor->load (std::memory_order_relaxed) > 0.5f;
        const float mix = isInstrument ? 1.0f : raw->mix->load (std::memory_order_relaxed) * 0.01f;
        auto grainParams = readParams();
        const auto resonatorParams = readResonatorParams (grainParams);
        const auto filterParams = readFilterParams();
        const auto colorParams = readColorParams();
        const auto spaceParams = readSpaceParams();

        // Synced Rate locks grains to the beat grid while the host plays; stopped,
        // they free-run at the synced rate.
        if ((int) raw->rateMode->load (std::memory_order_relaxed) == rateModeSync && playing && bpm > 0.0)
        {
            grainParams.gridSync = true;
            grainParams.ppqPerSample = ppqPerSample;
            grainParams.gridPpq = blockPpq + start * ppqPerSample;
        }
        lastLive = grainParams.live;

        const int liveWriteBefore = engine.getLiveWritePosition();
        const float* inLeft = capture.getNumChannels() > 0 ? capture.getReadPointer (0, start) : nullptr;
        const float* inRight = capture.getNumChannels() > 1 ? capture.getReadPointer (1, start) : inLeft;
        engine.process (wet.getWritePointer (0), wet.getWritePointer (1), len, inLeft, inRight,
                        noteEvents.data() + (first - noteEvents.begin()), (int) (last - first), grainParams);
        if (grainParams.live && ! grainParams.freeze)
            trackLivePeaks (liveWriteBefore, len);

        // Sample gain, ramped across the chunk so moving it doesn't click.
        {
            const float target = juce::Decibels::decibelsToGain (sampleGain->load (std::memory_order_relaxed));
            if (! juce::exactlyEqual (target, sampleGainNow) || ! juce::exactlyEqual (target, 1.0f))
                for (int ch = 0; ch < 2; ++ch)
                    wet.applyGainRamp (ch, 0, len, sampleGainNow, target);
            sampleGainNow = target;
        }

        // The modules, in the CHAIN order. A new order fades the sound down,
        // switches and fades back up (about 5 ms each way), so it doesn't click.
        const int wantedChain = juce::jlimit (0, numChainOrders - 1, (int) raw->chainOrder->load (std::memory_order_relaxed));
        const float fadeStep = (float) len / (float) (0.005 * currentSampleRate);
        float fadeTo = chainFade;
        if (wantedChain != chainNow)
            fadeTo = juce::jmax (0.0f, chainFade - fadeStep);
        else if (chainFade < 1.0f)
            fadeTo = juce::jmin (1.0f, chainFade + fadeStep);

        const int tapped = scopeModule.load (std::memory_order_relaxed);
        for (const int module : chainOrders()[(size_t) chainNow])
        {
            float* l = wet.getWritePointer (0);
            float* r = wet.getWritePointer (1);
            if (module == tapped)
                writeScope (scopeIn, l, r, len);
            switch (module)
            {
                case moduleResonator: resonator.process (l, r, len, resonatorParams); break;
                case moduleFilter:    filter.process (l, r, len, filterParams); break;
                case moduleColor:     color.process (l, r, len, colorParams); break;
                default:
                {
                    // Measure what SPACE adds: its output less what went in.
                    std::array<float, modulationInterval> inL {}, inR {};
                    std::copy (l, l + len, inL.begin());
                    std::copy (r, r + len, inR.begin());
                    space.process (l, r, len, spaceParams);
                    if (spaceNoteStarted)
                    {
                        spaceTraceCount.store (0, std::memory_order_relaxed);
                        spaceTraceFill = 0;
                        spaceTracePeak = 0.0f;
                        spaceNoteStarted = false;
                    }
                    for (int i = 0; i < len; ++i)
                    {
                        spaceTracePeak = std::max ({ spaceTracePeak, std::abs (l[i] - inL[(size_t) i]), std::abs (r[i] - inR[(size_t) i]) });
                        if (++spaceTraceFill == spaceTraceStep)
                        {
                            const unsigned at = spaceTraceCount.load (std::memory_order_relaxed);
                            spaceTrace[at & (spaceTraceSize - 1)].store (spaceTracePeak, std::memory_order_relaxed);
                            spaceTraceCount.store (at + 1, std::memory_order_release);
                            spaceTraceFill = 0;
                            spaceTracePeak = 0.0f;
                        }
                    }
                    break;
                }
            }
            if (module == tapped)
            {
                writeScope (scopeOut, l, r, len);
                scopeWrite.store (scopeWrite.load (std::memory_order_relaxed) + (unsigned) len, std::memory_order_release);
            }
        }

        if (! juce::exactlyEqual (fadeTo, chainFade) || chainFade < 1.0f)
            for (int ch = 0; ch < 2; ++ch)
                wet.applyGainRamp (ch, 0, len, chainFade, fadeTo);
        chainFade = fadeTo;
        if (wantedChain != chainNow && chainFade <= 0.0f)
            chainNow = wantedChain;

        // Sample by sample, reading the input before writing the output: the
        // two can share memory.
        const int inCh = capture.getNumChannels();
        for (int i = 0; i < len; ++i)
        {
            const float inL = inCh > 0 ? capture.getSample (0, start + i) : 0.0f;
            const float inR = inCh > 1 ? capture.getSample (1, start + i) : inL;
            const float dryL = isInstrument ? (monitoring ? inL : 0.0f) : inL * (1.0f - mix);
            const float dryR = isInstrument ? (monitoring ? inR : 0.0f) : inR * (1.0f - mix);

            output.setSample (0, start + i, safetyClip ((dryL + wet.getSample (0, i) * mix) * gain));
            if (output.getNumChannels() > 1)
                output.setSample (1, start + i, safetyClip ((dryR + wet.getSample (1, i) * mix) * gain));
        }
    }

    activeGrains.store (engine.getActiveGrainCount(), std::memory_order_relaxed);
    liveMode.store (lastLive, std::memory_order_relaxed);
    if (const auto* ring = engine.getLiveBuffer())
        liveWriteShare.store ((float) engine.getLiveWritePosition() / (float) ring->getNumFrames(), std::memory_order_relaxed);

    samplesSinceGrainViews += numSamples;
    if (samplesSinceGrainViews >= (int) (currentSampleRate / 60.0))
    {
        samplesSinceGrainViews = 0;
        grainViewSequence.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        grainViewCount.store (engine.collectGrainViews (grainViews.data(), maxGrainViews), std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        grainViewSequence.fetch_add (1, std::memory_order_release);
    }
    for (int v = 0; v < silt::Engine::numVoices; ++v)
    {
        const auto& voice = engine.getVoice (v);
        voicePlayheads[(size_t) v].store (voice.isActive() ? voice.getPlayheadPosition() : -1.0f, std::memory_order_relaxed);
    }
}

// Audio thread: steps the modulators over this chunk, then writes every
// target's modulated value: its own value plus each route's amount times its
// modulator, in the parameter's 0–1 range, clamped.
void SiltProcessor::applyModulation() noexcept
{
    std::array<float, numModulators> value {};
    for (int m = 0; m < numModulators; ++m)
    {
        const auto mp = readModulatorParams (m);
        value[(size_t) m] = modulators[(size_t) m].advance (mp, modContext);   // runs muted too, so unmuting picks up in step
        nativeBipolar[(size_t) m] = silt::Modulator::isBipolar (mp);
        modEnabled[(size_t) m] = isModulatorEnabled (m);
        if (! modEnabled[(size_t) m])
            value[(size_t) m] = 0.0f;
        globalModValues[(size_t) m] = value[(size_t) m];
        modOutputs[(size_t) m].store (value[(size_t) m], std::memory_order_relaxed);
        modPhases[(size_t) m].store ((float) modulators[(size_t) m].getPhase(), std::memory_order_relaxed);
        modStages[(size_t) m].store (modulators[(size_t) m].getStage(), std::memory_order_relaxed);
    }

    for (size_t t = 0; t < targets.size(); ++t)
    {
        const float base = targets[t].raw->load (std::memory_order_relaxed);
        float offset = 0.0f;
        for (int m = 0; m < numModulators; ++m)
            if (const float amount = routeAmounts[(size_t) m * maxTargets + t].load (std::memory_order_relaxed); amount != 0.0f && modEnabled[(size_t) m])
                offset += amount * shapedFor (m, (int) t, value[(size_t) m]);
        if (offset == 0.0f)
        {
            modulated[t].store (base, std::memory_order_relaxed);
            continue;
        }
        auto* p = targets[t].parameter;
        const float moved = juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (base) + offset);
        modulated[t].store (p->convertFrom0to1 (moved), std::memory_order_relaxed);
    }
}

silt::ModulatorParams SiltProcessor::readModulatorParams (int m) const noexcept
{
    const auto& r = modulatorRaw[(size_t) m];
    const auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    silt::ModulatorParams p;
    p.type = (silt::ModType) juce::jlimit (0, 5, (int) v (r.type));
    p.rateHz = v (r.rate);
    p.sync = v (r.sync) > 0.5f;
    p.reverse = v (r.rev) > 0.5f;
    p.syncQuarters = quartersFor (r.div);
    p.shape = v (r.shape);
    p.phase = v (r.phase) / 360.0f;
    p.retrigger = v (r.retrig) > 0.5f;
    p.smooth = v (r.smooth) * 0.01f;
    p.jitter = v (r.jitter) * 0.01f;
    p.attackMs = v (r.attack);
    p.decayMs = v (r.decay);
    p.sustain = v (r.sustain) * 0.01f;
    p.releaseMs = v (r.release);
    p.repeat = v (r.repeat) > 0.5f;
    p.followAttackMs = v (r.followAttack);
    p.followReleaseMs = v (r.followRelease);
    p.gainDb = v (r.gain);
    p.source = (silt::MidiSource) juce::jlimit (0, 4, (int) v (r.source));
    p.macro = v (r.macro) * 0.01f;
    p.symmetry = v (r.sym) * 0.01f;
    p.curve = v (r.curve) * 0.01f;
    return p;
}

int SiltProcessor::modulationTarget (const juce::String& id) const
{
    const auto it = targetIndex.find (id);
    return it == targetIndex.end() ? -1 : it->second;
}

float SiltProcessor::routeAmount (int modulator, int target) const noexcept
{
    return routeAmounts[(size_t) modulator * maxTargets + (size_t) target].load (std::memory_order_relaxed);
}

void SiltProcessor::setRouteAmount (int modulator, int target, float amount) noexcept
{
    if (modulator >= 0 && modulator < numModulators && target >= 0 && target < (int) targets.size())
    {
        routeAmounts[(size_t) modulator * maxTargets + (size_t) target].store (juce::jlimit (-1.0f, 1.0f, amount), std::memory_order_relaxed);
    }
}

void SiltProcessor::setRoutePolarity (int modulator, int target, int polarity) noexcept
{
    if (modulator >= 0 && modulator < numModulators && target >= 0 && target < (int) targets.size())
        routePolarity[(size_t) modulator * maxTargets + (size_t) target].store (juce::jlimit (0, 2, polarity), std::memory_order_relaxed);
}

int SiltProcessor::routePolarityOf (int modulator, int target) const noexcept
{
    return routePolarity[(size_t) modulator * maxTargets + (size_t) target].load (std::memory_order_relaxed);
}

// Bipolar: swings both ways round the knob; unipolar: one way. A route follows
// its source (WAVE and RANDOM bipolar, ENVELOPE and the rest unipolar) unless
// it's been set.
bool SiltProcessor::routeIsBipolar (int modulator, int target) const noexcept
{
    switch (routePolarityOf (modulator, target))
    {
        case unipolar: return false;
        case bipolar:  return true;
        default:       return silt::Modulator::isBipolar (readModulatorParams (modulator));
    }
}

// Audio thread: a modulator's value as this route takes it (the source's
// native range rescaled when the route's polarity differs).
float SiltProcessor::shapedFor (int m, int t, float v) const noexcept
{
    const int polarity = routePolarity[(size_t) m * maxTargets + (size_t) t].load (std::memory_order_relaxed);
    const bool native = nativeBipolar[(size_t) m];
    if (polarity == unipolar && native)
        return 0.5f * (v + 1.0f);
    if (polarity == bipolar && ! native)
        return 2.0f * v - 1.0f;
    return v;
}

// "modulator:parameter:amount[:b|u]" separated by ';' (modulator 0–3; the polarity
// only when it's been set).
juce::String SiltProcessor::routesToString() const
{
    juce::StringArray routes;
    for (const auto& r : allRoutes())
    {
        const int polarity = routePolarityOf (r.modulator, r.target);
        routes.add (juce::String (r.modulator) + ":" + targets[(size_t) r.target].parameter->getParameterID() + ":" + juce::String (r.amount, 4)
                    + (polarity == bipolar ? ":b" : polarity == unipolar ? ":u" : ""));
    }
    return routes.joinIntoString (";");
}

void SiltProcessor::routesFromString (const juce::String& text)
{
    for (auto& amount : routeAmounts)
        amount.store (0.0f);
    for (auto& polarity : routePolarity)
        polarity.store (natural);
    for (const auto& route : juce::StringArray::fromTokens (text, ";", ""))
    {
        const auto parts = juce::StringArray::fromTokens (route, ":", "");
        if (parts.size() < 3)
            continue;
        const int m = parts[0].getIntValue(), t = modulationTarget (parts[1]);
        setRouteAmount (m, t, parts[2].getFloatValue());
        if (parts.size() > 3)
            setRoutePolarity (m, t, parts[3] == "b" ? bipolar : parts[3] == "u" ? unipolar : natural);
    }
}

void SiltProcessor::clearRoutesOnTarget (int target) noexcept
{
    for (int m = 0; m < numModulators; ++m)
        setRouteAmount (m, target, 0.0f);
}

void SiltProcessor::clearRoutesForModulator (int modulator) noexcept
{
    for (int t = 0; t < numTargets(); ++t)
        setRouteAmount (modulator, t, 0.0f);
}

int SiltProcessor::routeCount (int modulator) const noexcept
{
    int n = 0;
    for (int t = 0; t < numTargets(); ++t)
        n += routeAmount (modulator, t) != 0.0f ? 1 : 0;
    return n;
}

std::vector<SiltProcessor::Route> SiltProcessor::allRoutes() const
{
    std::vector<Route> routes;
    for (int t = 0; t < numTargets(); ++t)
        for (int m = 0; m < numModulators; ++m)
            if (const float amount = routeAmount (m, t); amount != 0.0f)
                routes.push_back ({ m, t, amount });
    return routes;
}

bool SiltProcessor::isSteppedTarget (int target) const
{
    return dynamic_cast<juce::AudioParameterInt*> (targets[(size_t) target].parameter) != nullptr;
}

bool SiltProcessor::isModulatorEnabled (int modulator) const noexcept
{
    return modulatorRaw[(size_t) modulator].enable->load (std::memory_order_relaxed) > 0.5f;
}

// What a route moves its parameter over, in the parameter's own units (both
// ways from the knob for bipolar sources): the readout that means the same for
// cutoff and for mix, which a percentage doesn't.
juce::String SiltProcessor::depthSpan (int modulator, int target) const
{
    const float amount = routeAmount (modulator, target);
    auto& p = *targets[(size_t) target].parameter;
    const float base = p.getValue();
    const bool bipolar = routeIsBipolar (modulator, target);
    const float lo = juce::jlimit (0.0f, 1.0f, base + (bipolar ? -std::abs (amount) : juce::jmin (0.0f, amount)));
    const float hi = juce::jlimit (0.0f, 1.0f, base + (bipolar ? std::abs (amount) : juce::jmax (0.0f, amount)));
    return p.getText (lo, 0) + " to " + p.getText (hi, 0);
}

// The span, then the depth, signed like an Elektron LFO's: + moves the value
// up first, − down first (bipolar routes then swing back past the knob).
juce::String SiltProcessor::depthText (int modulator, int target) const
{
    const int percent = juce::roundToInt (routeAmount (modulator, target) * 100.0f);
    return depthSpan (modulator, target) + "  (" + (percent > 0 ? "+" : "") + juce::String (percent) + "%)";
}

// --- route undo ---------------------------------------------------------------------

// One undo step: every route before and after. Steps in one gesture merge, so
// the first "before" and the last "after" are kept.
class SiltProcessor::RoutesChange final : public juce::UndoableAction
{
public:
    RoutesChange (SiltProcessor& p, std::vector<float> b, std::vector<float> a)
        : processor (p), before (std::move (b)), after (std::move (a)) {}

    bool perform() override { processor.restoreRoutes (after); return true; }
    bool undo() override { processor.restoreRoutes (before); return true; }
    int getSizeInUnits() override { return 1; }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* change = dynamic_cast<RoutesChange*> (next))
            return new RoutesChange (processor, before, change->after);
        return nullptr;
    }

private:
    SiltProcessor& processor;
    std::vector<float> before, after;
};

std::vector<float> SiltProcessor::routeSnapshot() const
{
    // Amount and polarity per route.
    std::vector<float> s ((size_t) (2 * numModulators * numTargets()));
    for (int m = 0; m < numModulators; ++m)
        for (int t = 0; t < numTargets(); ++t)
        {
            s[(size_t) (2 * (m * numTargets() + t))] = routeAmount (m, t);
            s[(size_t) (2 * (m * numTargets() + t) + 1)] = (float) routePolarityOf (m, t);
        }
    return s;
}

void SiltProcessor::restoreRoutes (const std::vector<float>& s) noexcept
{
    for (int m = 0; m < numModulators; ++m)
        for (int t = 0; t < numTargets(); ++t)
        {
            setRouteAmount (m, t, s[(size_t) (2 * (m * numTargets() + t))]);
            setRoutePolarity (m, t, (int) s[(size_t) (2 * (m * numTargets() + t) + 1)]);
        }
}

// One parameter's value before and after a gesture. Gestures on the same
// parameter close together (a controller knob sends one per message) merge.
class SiltProcessor::ParameterChange final : public juce::UndoableAction
{
public:
    ParameterChange (SiltProcessor& p, int i, float b, float a) : processor (p), index (i), before (b), after (a) {}

    bool perform() override { set (after); return true; }
    bool undo() override { set (before); return true; }
    int getSizeInUnits() override { return 1; }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* change = dynamic_cast<ParameterChange*> (next); change != nullptr && change->index == index)
            return new ParameterChange (processor, index, before, change->after);
        return nullptr;
    }

private:
    void set (float value)
    {
        auto* parameter = processor.getParameters()[index];
        const juce::ScopedValueSetter<bool> quiet (processor.replaying, true);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (value);
        parameter->endChangeGesture();
    }

    SiltProcessor& processor;
    int index;
    float before, after;
};

// Gestures from the panel, the controller and the mod rail: a value at the
// start, a step at the end if it changed. Only on the message thread; never
// while undo, redo or a preset is setting values.
void SiltProcessor::parameterGestureChanged (int index, bool starting)
{
    if (replaying || ! juce::MessageManager::existsAndIsCurrentThread())
        return;
    auto* parameter = getParameters()[index];
    if (starting)
    {
        gestureStart[index] = parameter->getValue();
        return;
    }
    const auto it = gestureStart.find (index);
    if (it == gestureStart.end())
        return;
    const float before = it->second, after = parameter->getValue();
    gestureStart.erase (it);
    if (juce::exactlyEqual (before, after))
        return;

    const auto now = juce::Time::getMillisecondCounter();
    if (index != lastGestureParameter || now - lastGestureTime > 800)
        routeUndo.beginNewTransaction (parameter->getName (24).toUpperCase());
    lastGestureParameter = index;
    lastGestureTime = now;
    const juce::ScopedValueSetter<bool> quiet (replaying, true);   // perform() sets the value it already has
    routeUndo.perform (new ParameterChange (*this, index, before, after));
}

std::vector<const silt::layout::Entry*> SiltProcessor::randomisable (int page) const
{
    using silt::layout::Kind;
    const auto context = layoutContext();
    std::vector<const silt::layout::Entry*> out;
    for (const auto& e : silt::layout::entries (isInstrument))
        if (e.page == page && (e.kind == Kind::knob || e.kind == Kind::menu || e.kind == Kind::button)
            && ! e.id.startsWith ("!") && e.isShown (context) && params.getParameter (e.id) != nullptr)
            out.push_back (&e);
    return out;
}

int SiltProcessor::randomisePage (int page)
{
    const float amount = juce::jlimit (0.0f, 1.0f, randomAmount.load());
    routeUndo.beginNewTransaction ("RANDOM " + juce::String (silt::layout::pageName (page)));
    lastGestureParameter = -1;
    int changed = 0;
    for (const auto* e : randomisable (page))
    {
        if (isRandomExcluded (e->id))
            continue;
        auto* parameter = params.getParameter (e->id);
        const float before = parameter->getValue();
        float after = before;
        if (e->kind == silt::layout::Kind::knob)
            after = before + (randomiser.nextFloat() - before) * amount;
        else if (randomiser.nextFloat() < amount)
        {
            const int steps = juce::jmax (2, parameter->getNumSteps());
            after = (float) randomiser.nextInt (steps) / (float) (steps - 1);
        }
        after = parameter->convertTo0to1 (parameter->convertFrom0to1 (after));   // onto the parameter's steps
        if (juce::exactlyEqual (before, after))
            continue;
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        routeUndo.perform (new ParameterChange (*this, parameter->getParameterIndex(), before, after));
        ++changed;
    }
    lastGestureParameter = -1;   // the next change starts its own step
    return changed;
}

void SiltProcessor::setRandomExcluded (const juce::String& id, bool excluded)
{
    if (excluded)
        randomExcluded.addIfNotAlreadyThere (id);
    else
        randomExcluded.removeString (id);
}

bool SiltProcessor::undoing (const std::function<bool()>& step)
{
    lastGestureParameter = -1;   // the next change starts its own step
    return step();
}

// A route removed earlier starts natural when it's made again. Only here, at a
// gesture's start: a depth dragged through 0 keeps its polarity.
void SiltProcessor::beginRouteGesture()
{
    for (size_t i = 0; i < routeAmounts.size(); ++i)
        if (juce::exactlyEqual (routeAmounts[i].load (std::memory_order_relaxed), 0.0f))
            routePolarity[i].store (natural, std::memory_order_relaxed);
    routeUndo.beginNewTransaction ("MODULATION");
}

void SiltProcessor::editRoutes (const std::function<void()>& edit)
{
    auto before = routeSnapshot();
    edit();
    auto after = routeSnapshot();
    if (before != after)
        routeUndo.perform (new RoutesChange (*this, std::move (before), std::move (after)));
}

float SiltProcessor::modulatedValue01 (int target) const noexcept
{
    return targets[(size_t) target].parameter->convertTo0to1 (modulated[(size_t) target].load (std::memory_order_relaxed));
}

// --- per-voice modulation -----------------------------------------------------------

SiltProcessor::VoiceModulation::VoiceModulation (SiltProcessor& p) : owner (p) {}

void SiltProcessor::VoiceModulation::reset() noexcept
{
    for (int v = 0; v < silt::Engine::numVoices; ++v)
    {
        for (int m = 0; m < numModulators; ++m)
            mods[(size_t) v][(size_t) m].reset (0x7019e + (std::uint64_t) (v * numModulators + m));
        context[(size_t) v] = {};
        held[(size_t) v] = false;
        started[(size_t) v] = false;
    }
    active = false;
}

// Per chunk: which modulators run per voice and whether any reaches a grain
// parameter; then each sounding voice's modulators step and its parameters
// are worked out once (the engine copies them per stretch).
void SiltProcessor::VoiceModulation::prepareChunk() noexcept
{
    active = false;
    for (int m = 0; m < numModulators; ++m)
    {
        perVoice[(size_t) m] = owner.isModulatorEnabled (m) && owner.modulatorRaw[(size_t) m].voice->load (std::memory_order_relaxed) > 0.5f;
        if (perVoice[(size_t) m])
            for (const int t : owner.voiceTargets)
                active = active || owner.routeAmount (m, t) != 0.0f;
    }
    for (auto& shown : owner.voiceShown)
        shown.store (false, std::memory_order_relaxed);
    if (! active)
        return;

    for (const int t : owner.voiceTargets)
        base01[(size_t) t] = owner.targets[(size_t) t].parameter->convertTo0to1 (owner.targets[(size_t) t].raw->load (std::memory_order_relaxed));

    for (int v = 0; v < silt::Engine::numVoices; ++v)
    {
        if (! owner.engine.getVoice (v).isActive() && ! held[(size_t) v])
            continue;
        step (v, owner.modContext.numSamples);
        compute (v);
    }
}

// Steps one voice's per-voice modulators: the shared context (clock, CCs,
// input level) with this voice's own note.
void SiltProcessor::VoiceModulation::step (int v, int numSamples) noexcept
{
    auto& c = context[(size_t) v];
    const auto& shared = owner.modContext;
    c.numSamples = numSamples;
    c.sampleRate = shared.sampleRate;
    c.hostPlaying = shared.hostPlaying;
    c.ppqEnd = shared.ppqEnd;
    c.bpm = shared.bpm;
    c.modWheel = shared.modWheel;
    c.aftertouch = shared.aftertouch;
    c.pitchBend = shared.pitchBend;
    c.inputPeak = shared.inputPeak;
    c.heldNotes = held[(size_t) v] ? 1 : 0;
    c.noteStarted = started[(size_t) v];
    started[(size_t) v] = false;
    for (int m = 0; m < numModulators; ++m)
        if (perVoice[(size_t) m])
            values[(size_t) v][(size_t) m] = mods[(size_t) v][(size_t) m].advance (owner.readModulatorParams (m), c);
}

// One voice's grain parameters: grain targets from this voice's modulators
// (shared ones for modulators that aren't per voice), the rest as shared.
void SiltProcessor::VoiceModulation::compute (int v) noexcept
{
    for (int t = 0; t < owner.numTargets(); ++t)
        scratch[(size_t) t] = owner.modulated[(size_t) t].load (std::memory_order_relaxed);
    for (const int t : owner.voiceTargets)
    {
        float offset = 0.0f;
        for (int m = 0; m < numModulators; ++m)
            if (const float amount = owner.routeAmount (m, t); amount != 0.0f && owner.modEnabled[(size_t) m])
                offset += amount * owner.shapedFor (m, t, perVoice[(size_t) m] ? values[(size_t) v][(size_t) m] : owner.globalModValues[(size_t) m]);
        const float moved = juce::jlimit (0.0f, 1.0f, base01[(size_t) t] + offset);
        scratch[(size_t) t] = owner.targets[(size_t) t].parameter->convertFrom0to1 (moved);
        owner.voiceModulated01[(size_t) v][(size_t) t].store (moved, std::memory_order_relaxed);
    }
    owner.voiceShown[(size_t) v].store (true, std::memory_order_relaxed);
    params[(size_t) v] = owner.readParams (scratch.data());
}

void SiltProcessor::VoiceModulation::noteStarted (int v, int note, float velocity) noexcept
{
    // A new note (or a stolen voice) starts its modulators afresh.
    for (int m = 0; m < numModulators; ++m)
        mods[(size_t) v][(size_t) m].reset (0x7019e + (std::uint64_t) (v * numModulators + m) + (std::uint64_t) note * 977u);
    auto& c = context[(size_t) v];
    c.note = note;
    c.velocity = velocity;
    held[(size_t) v] = true;
    started[(size_t) v] = true;
    if (active)
    {
        step (v, 0);
        compute (v);
    }
}

void SiltProcessor::VoiceModulation::noteReleased (int v) noexcept
{
    held[(size_t) v] = false;
}

// The engine's stretch settings (grid, pattern, live position) stay; the
// parameters are this voice's.
void SiltProcessor::VoiceModulation::apply (int v, silt::GrainParams& out) noexcept
{
    auto own = params[(size_t) v];
    own.gridSync = out.gridSync;
    own.ppqPerSample = out.ppqPerSample;
    own.gridPpq = out.gridPpq;
    own.liveMargin = out.liveMargin;
    own.liveNow = out.liveNow;
    own.pattern = out.pattern;
    own.patternSteps = juce::jlimit (1, 16, own.patternSteps);
    out = own;
}

bool SiltProcessor::isVoiceTarget (int target) const noexcept
{
    return std::find (voiceTargets.begin(), voiceTargets.end(), target) != voiceTargets.end();
}

bool SiltProcessor::isModulatorPerVoice (int m) const noexcept
{
    return modulatorRaw[(size_t) m].voice->load (std::memory_order_relaxed) > 0.5f;
}

// --- presets ---------------------------------------------------------------------------

namespace
{
// Session and sample settings (ROOT is the sample's key), not part of a sound.
bool inPresets (const juce::String& id) { return id != "monitor" && id != "captureBars" && id != "sampleGain" && id != "root"; }
} // namespace

std::unique_ptr<juce::XmlElement> SiltProcessor::capturePreset (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> ("SiltPreset");
    xml->setAttribute ("name", name);
    xml->setAttribute ("version", 1);
    auto* values = xml->createNewChildElement ("PARAMS");
    for (auto* parameter : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (parameter); p != nullptr && inPresets (p->getParameterID()))
            values->setAttribute (p->getParameterID(), p->convertFrom0to1 (p->getValue()));
    xml->setAttribute ("routes", routesToString());
    xml->setAttribute ("modShown", modShown.load());
    return xml;
}

// Every parameter takes the preset's value, or its default if the preset
// doesn't list it (so presets are complete), as host-visible gestures. Routes
// are replaced; the route undo history starts over (it can't undo the values).
void SiltProcessor::applyPreset (const juce::XmlElement& xml, const juce::String& name)
{
    const juce::ScopedValueSetter<bool> quiet (replaying, true);   // one load, not 150 undo steps
    const auto* values = xml.getChildByName ("PARAMS");
    for (auto* parameter : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (parameter); p != nullptr && inPresets (p->getParameterID()))
        {
            const auto id = p->getParameterID();
            const float normalised = values != nullptr && values->hasAttribute (id)
                                   ? p->convertTo0to1 ((float) values->getDoubleAttribute (id))
                                   : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }

    routesFromString (xml.getStringAttribute ("routes"));
    modShown.store (juce::jlimit (0, numModulators - 1, xml.getIntAttribute ("modShown", 0)));
    routeUndo.clearUndoHistory();
    presetName = name;
}

juce::AudioProcessorEditor* SiltProcessor::createEditor()
{
    return new SiltEditor (*this);
}

// --- message thread -----------------------------------------------------------

// Audio thread: learn, page buttons, or queue a knob move.
void SiltProcessor::handleController (int number, int value) noexcept
{
    if (const int step = learnStep.load(); step >= 0)
    {
        // A knob sends many messages and a button sends press and release: only a
        // number not yet taken in this learn moves it on.
        for (int i = 0; i < juce::jmin (step, slotsPerPage); ++i)
            if (learnedThisRun[(size_t) i] == number)
                return;
        if (step == 9 && number == previousPageCC.load())
            return;

        if (step < slotsPerPage)
        {
            knobCCs[(size_t) step].store (number);
            learnedThisRun[(size_t) step] = number;
        }
        else if (step == 8)
            previousPageCC.store (number);
        else
            nextPageCC.store (number);
        learnStep.store (step < 9 ? step + 1 : -1);
        return;
    }

    if (number == previousPageCC.load() || number == nextPageCC.load())
    {
        const int which = number == nextPageCC.load() ? 1 : 0;
        if (value > 63 && pageButtonLast[(size_t) which] <= 63)
        {
            // RESONATOR's two halves are two stops.
            const int pageNow = currentPage.load(), half = resonatorHalf.load();
            if (pageNow == silt::layout::resonator && half == 0 && which == 1)
                resonatorHalf.store (1);
            else if (pageNow == silt::layout::resonator && half == 1 && which == 0)
                resonatorHalf.store (0);
            else
            {
                const int to = (pageNow + (which == 1 ? 1 : numPages - 1)) % numPages;
                if (to == silt::layout::resonator)
                    resonatorHalf.store (which == 1 ? 0 : 1);
                currentPage.store (to);
            }
        }
        pageButtonLast[(size_t) which] = value;
        return;
    }

    for (int slot = 0; slot < slotsPerPage; ++slot)
        if (knobCCs[(size_t) slot].load() == number)
        {
            int start1, size1, start2, size2;
            controllerFifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 > 0)
                controllerMoves[(size_t) start1] = { slot, value };
            controllerFifo.finishedWrite (size1);
            return;
        }
}

// Which parameter a page slot moves right now: the editor's page table, read
// with the current parameter values.
juce::String SiltProcessor::slotParameter (int page, int slot) const
{
    return silt::layout::slotParameter (isInstrument, page, slot, layoutContext());
}

silt::layout::Context SiltProcessor::layoutContext() const
{
    silt::layout::Context c;
    c.value = [this] (const juce::String& id)
    {
        const auto* value = params.getRawParameterValue (id);
        jassert (value != nullptr);   // a typo in an id
        return value != nullptr ? value->load() : 0.0f;
    };
    c.modShown = modShown.load();
    c.resonatorHalf = resonatorHalf.load();
    return c;
}

double SiltProcessor::divisionQuarters (int index)
{
    return ::divisionQuarters[juce::jlimit (0, (int) std::size (::divisionQuarters) - 1, index)];
}

void SiltProcessor::newPattern()
{
    if (auto* seed = dynamic_cast<juce::RangedAudioParameter*> (params.getParameter ("patternSeed")))
        seed->setValueNotifyingHost (seed->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 10000 })));
}

// Message thread: applies queued knob moves with pickup.
void SiltProcessor::applyControllerMoves()
{
    const int page = currentPage.load();
    if (page != pickupPage)
    {
        pickupPage = page;
        pickedUp.fill (false);
        lastKnobValue.fill (-1);
    }

    int start1, size1, start2, size2;
    controllerFifo.prepareToRead (controllerFifo.getNumReady(), start1, size1, start2, size2);

    const auto apply = [this, page] (std::pair<int, int> move)
    {
        const auto [slot, value] = move;
        const auto id = slotParameter (page, slot);
        auto& last = lastKnobValue[(size_t) slot];
        if (id != slotTarget[(size_t) slot])
        {
            slotTarget[(size_t) slot] = id;   // a mode switched what this slot moves
            pickedUp[(size_t) slot] = false;
            last = -1;
        }
        if (id.isEmpty())
            return;

        // ASSIGN on and on another page: a knob sets this modulator's amount on
        // its slot's parameter, centre = none (no pickup needed: it's relative to nothing).
        if (const int m = armedMod.load(); m >= 0 && page != modPage && ! id.startsWith ("!"))
        {
            if (const int t = modulationTarget (id); t >= 0)
            {
                // One undo step per knob turn: a new step on another route or after a pause.
                const auto now = juce::Time::getMillisecondCounter();
                if (m * maxTargets + t != lastControllerRoute || now - lastControllerRouteTime > 1000)
                    beginRouteGesture();
                lastControllerRoute = m * maxTargets + t;
                lastControllerRouteTime = now;
                editRoutes ([&] { setRouteAmount (m, t, juce::jlimit (-1.0f, 1.0f, (float) (value - 64) / 63.0f)); });
            }
            last = value;
            return;
        }

        if (id == "!modSelect")   // a knob picks the modulator shown
        {
            modShown.store (juce::jlimit (0, numModulators - 1, value * numModulators / 128));
            last = value;
            return;
        }

        if (id == "!assign")   // a button: ASSIGN on or off for the shown modulator
        {
            if (value > 63 && last <= 63)
                armedMod.store (armedMod.load() >= 0 ? -1 : modShown.load());
            last = value;
            return;
        }

        if (id == "!newPattern")   // a button: new pattern on each press
        {
            if (value > 63 && last <= 63)
                newPattern();
            last = value;
            return;
        }

        auto* parameter = params.getParameter (id);
        if (parameter == nullptr)
            return;

        const float target = (float) value / 127.0f;
        if (! pickedUp[(size_t) slot])
        {
            // Take over once the knob reaches the value: close to it, or moved across it.
            const float current = parameter->getValue();
            const float previous = last >= 0 ? (float) last / 127.0f : target;
            pickedUp[(size_t) slot] = std::abs (target - current) < 0.04f || (previous - current) * (target - current) <= 0.0f;
            last = value;
            if (! pickedUp[(size_t) slot])
                return;
        }
        last = value;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (target);
        parameter->endChangeGesture();
    };

    for (int i = 0; i < size1; ++i)
        apply (controllerMoves[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        apply (controllerMoves[(size_t) (start2 + i)]);
    controllerFifo.finishedRead (size1 + size2);
}

void SiltProcessor::timerCallback()
{
    engine.collectGarbage();
    applyControllerMoves();

    const auto state = captureState.load (std::memory_order_acquire);

    if (state == CaptureState::done)
    {
        captureTarget.store (nullptr, std::memory_order_release);
        analyseThenInstall (std::move (captureOwned), "CAPTURE " + juce::Time::getCurrentTime().formatted ("%H:%M:%S"));
        captureState.store (CaptureState::idle, std::memory_order_release);
    }
    else if (state == CaptureState::cancelled)
    {
        captureTarget.store (nullptr, std::memory_order_release);
        captureOwned.reset();
        captureState.store (CaptureState::idle, std::memory_order_release);
    }
}

void SiltProcessor::armCapture()
{
    if (captureState.load() != CaptureState::idle)
        return;

    const int bars = captureBars[juce::jlimit (0, 3, (int) raw->captureBars->load())];
    const double barQuarters = timeSigNumerator.load() * 4.0 / juce::jmax (1, timeSigDenominator.load());
    const double bpm = juce::jlimit (20.0, 400.0, hostBpm.load());
    const double seconds = juce::jmin (maxCaptureSeconds, bars * barQuarters * 60.0 / bpm);

    captureOwned = std::make_unique<silt::SampleBuffer> ((int) (seconds * currentSampleRate), currentSampleRate);
    captureProgress.store (0.0f);
    captureTarget.store (captureOwned.get(), std::memory_order_release);
    captureState.store (CaptureState::armed, std::memory_order_release);
}

void SiltProcessor::cancelCapture()
{
    const auto state = captureState.load();
    if (state == CaptureState::armed || state == CaptureState::recording)
        captureState.store (CaptureState::cancelRequested, std::memory_order_release);
}

bool SiltProcessor::isAudioFile (const juce::File& file) const
{
    return formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

std::unique_ptr<juce::AudioBuffer<float>> SiltProcessor::decode (juce::AudioFormatReader& reader) const
{
    // D9: samples hold up to 30 s.
    const int frames = (int) juce::jmin (reader.lengthInSamples, (juce::int64) (maxCaptureSeconds * reader.sampleRate));
    auto audio = std::make_unique<juce::AudioBuffer<float>> ((int) juce::jmax (1u, reader.numChannels), juce::jmax (1, frames));
    reader.read (audio.get(), 0, frames, 0, true, true);
    return audio;
}

std::array<int, SiltProcessor::numModules> SiltProcessor::chainOrder (int index)
{
    return chainOrders()[(size_t) juce::jlimit (0, numChainOrders - 1, index)];
}

int SiltProcessor::chainOrderIndex (const std::array<int, numModules>& order)
{
    const auto& all = chainOrders();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i] == order)
            return (int) i;
    return 0;
}

juce::String SiltProcessor::moduleName (int module)
{
    static const char* names[] { "RESONATOR", "FILTER", "COLOR", "SPACE" };
    return names[juce::jlimit (0, numModules - 1, module)];
}

juce::String SiltProcessor::chainOrderName (int index)
{
    juce::StringArray letters;
    for (const int m : chainOrder (index))
        letters.add (moduleName (m).substring (0, 1));
    return letters.joinIntoString (">");
}

// Audio thread: mono, into the scope's ring at the current write count.
void SiltProcessor::writeScope (std::array<std::atomic<float>, scopeSize>& ring, const float* l, const float* r, int count) noexcept
{
    const unsigned at = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        ring[(at + (unsigned) i) & (scopeSize - 1)].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
}

unsigned SiltProcessor::readScope (float* in, float* out, int count) const
{
    count = juce::jmin (count, scopeSize / 2);
    const unsigned end = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
    {
        const auto at = (end - (unsigned) (count - i)) & (scopeSize - 1);
        in[i] = scopeIn[at].load (std::memory_order_relaxed);
        out[i] = scopeOut[at].load (std::memory_order_relaxed);
    }
    return end;
}

void SiltProcessor::loadFile (const juce::File& file)
{
    loadInBackground ([this, file] { return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)); },
                      file.getFileNameWithoutExtension(), -1, 60, false);
}

int SiltProcessor::numFactorySamples() { return (int) factorySamples().size(); }

juce::String SiltProcessor::factorySampleName (int index)
{
    return factorySamples()[(size_t) juce::jlimit (0, numFactorySamples() - 1, index)].name;
}

void SiltProcessor::loadFactorySample (int index)
{
    index = juce::jlimit (0, numFactorySamples() - 1, index);
    loadInBackground ([index] { return openFactorySample (index); }, factorySampleName (index), index, factorySamples()[(size_t) index].root, false);
}

int SiltProcessor::getFactorySample() const
{
    const juce::ScopedLock sl (sampleLock);
    return factorySample;
}

std::unique_ptr<juce::AudioFormatReader> SiltProcessor::openFactorySample (int index)
{
    const auto& f = factorySamples()[(size_t) index];
    return std::unique_ptr<juce::AudioFormatReader> (
        juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (f.data, (size_t) f.size, false), true));
}

void SiltProcessor::loadInBackground (std::function<std::unique_ptr<juce::AudioFormatReader>()> open, const juce::String& name,
                                      int factory, int rootNote, bool onlyIfUnchanged)
{
    juce::WeakReference<SiltProcessor> weak (this);
    const int generation = sampleGeneration.load();

    loader.addJob ([this, weak, open, name, factory, rootNote, onlyIfUnchanged, generation]
    {
        const auto reader = open();
        if (reader == nullptr)
        {
            juce::MessageManager::callAsync ([weak, name]
            {
                if (weak != nullptr)
                    weak->lastLoadError = "CAN'T READ " + name.toUpperCase();
            });
            return;
        }

        // Passed on by moving the buffer out of a shared holder: no copy.
        auto buffer = std::make_shared<std::unique_ptr<silt::SampleBuffer>> (makeBuffer (*decode (*reader), reader->sampleRate));
        (*buffer)->analyse();

        juce::MessageManager::callAsync ([weak, buffer, name, factory, rootNote, onlyIfUnchanged, generation]
        {
            if (weak == nullptr || (onlyIfUnchanged && (weak->sampleGeneration.load() != generation || weak->stateRestored.load())))
                return;
            weak->lastLoadError.clear();
            weak->installBuffer (std::move (*buffer), name, factory);
            if (auto* root = weak->params.getParameter ("root"))
                root->setValueNotifyingHost (root->convertTo0to1 ((float) rootNote));
        });
    });
}

std::unique_ptr<silt::SampleBuffer> SiltProcessor::makeBuffer (const juce::AudioBuffer<float>& audio, double rate)
{
    const int frames = audio.getNumSamples();
    auto buffer = std::make_unique<silt::SampleBuffer> (frames, rate);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::copy (buffer->getWritePointer (ch),
                                           audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1)), frames);
    return buffer;
}

// Analysis (zero crossings and transients, for SNAP) takes a moment on long
// samples, so it runs on the loader thread; the sample is installed after.
void SiltProcessor::analyseThenInstall (std::unique_ptr<silt::SampleBuffer> buffer, const juce::String& name)
{
    juce::WeakReference<SiltProcessor> weak (this);
    auto holder = std::make_shared<std::unique_ptr<silt::SampleBuffer>> (std::move (buffer));   // moved on, not copied
    loader.addJob ([weak, holder, name]
    {
        (*holder)->analyse();
        juce::MessageManager::callAsync ([weak, holder, name]
        {
            if (weak != nullptr)
                weak->installBuffer (std::move (*holder), name);
        });
    });
}

// Message thread. The buffer is already analysed.
void SiltProcessor::installBuffer (std::unique_ptr<silt::SampleBuffer> buffer, const juce::String& name, int factory)
{
    const int frames = buffer->getNumFrames();
    const double rate = buffer->getSampleRate();
    {
        const juce::ScopedLock sl (sampleLock);
        sampleZeroCrossings = buffer->getZeroCrossings();
        const auto& pitches = buffer->getPitches();
        const int count = juce::jmin ((int) pitches.size(), (int) samplePitchHz.size());
        for (int i = 0; i < count; ++i)
            samplePitchHz[(size_t) i].store ((float) pitches[(size_t) i], std::memory_order_relaxed);
        samplePitchCount.store (count, std::memory_order_release);
        sampleTransients = buffer->getTransients();
        sampleCopy.setSize (2, frames);
        for (int ch = 0; ch < 2; ++ch)
            sampleCopy.copyFrom (ch, 0, buffer->getReadPointer (ch), frames);
        sampleCopyRate = rate;
        sampleName = name;
        factorySample = factory;
        encodedSample.reset();
    }

    engine.setBuffer (std::move (buffer));
    ++sampleGeneration;
}

// Audio thread: the peak of each 1/liveColumns of the ring, as it's written,
// for the screen's scrolling view of the live input.
void SiltProcessor::trackLivePeaks (int writeStart, int count) noexcept
{
    const auto* ring = engine.getLiveBuffer();
    if (ring == nullptr)
        return;
    const int frames = ring->getNumFrames();
    const float* l = ring->getReadPointer (0);
    const float* r = ring->getReadPointer (1);
    for (int i = 0; i < count; ++i)
    {
        const int pos = (writeStart + i) % frames;
        const int column = (int) ((juce::int64) pos * liveColumns / frames);
        if (column != liveColumn)
        {
            if (liveColumn >= 0)
                livePeaks[(size_t) liveColumn].store (liveColumnPeak, std::memory_order_relaxed);
            liveColumn = column;
            liveColumnPeak = 0.0f;
        }
        liveColumnPeak = juce::jmax (liveColumnPeak, std::abs (l[pos]), std::abs (r[pos]));
    }
}

std::array<float, SiltProcessor::liveColumns> SiltProcessor::getLivePeaks() const
{
    std::array<float, liveColumns> out {};
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = livePeaks[i].load (std::memory_order_relaxed);
    return out;
}

int SiltProcessor::readGrainViews (std::array<silt::GrainView, maxGrainViews>& out) const
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = grainViewSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        const int n = juce::jlimit (0, maxGrainViews, grainViewCount.load (std::memory_order_relaxed));
        std::copy (grainViews.begin(), grainViews.begin() + n, out.begin());
        std::atomic_thread_fence (std::memory_order_acquire);
        if (grainViewSequence.load (std::memory_order_relaxed) == before)
            return n;
    }
    return 0;   // the audio thread kept updating; the next frame will catch it
}

// The region as it will play (snapped), as shares of the sample, for the screen.
std::pair<float, float> SiltProcessor::shownRegion() const
{
    const float start = params.getRawParameterValue ("start")->load() * 0.01f;
    const float length = params.getRawParameterValue ("length")->load() * 0.01f;
    const juce::ScopedLock sl (sampleLock);
    const int frames = sampleCopy.getNumSamples();
    if (frames == 0)
        return { start, length };

    const double begin = start * (frames - 1.0);
    const double end = std::min ((double) frames, begin + std::max (1.0, (double) length * frames));
    const auto snapped = silt::snapRegion (begin, end, frames,
                                           (silt::Snap) juce::jlimit (0, 2, (int) params.getRawParameterValue ("snap")->load()),
                                           sampleZeroCrossings, sampleTransients);
    return { (float) (snapped.begin / frames), (float) ((snapped.end - snapped.begin) / frames) };
}

// The gain that puts the sample's peak at −1 dBFS (0 with no sample).
float SiltProcessor::normalisingGainDb() const
{
    const juce::ScopedLock sl (sampleLock);
    const float peak = sampleCopy.getNumSamples() > 0 ? sampleCopy.getMagnitude (0, sampleCopy.getNumSamples()) : 0.0f;
    return peak > 1.0e-6f ? juce::jlimit (-24.0f, 24.0f, -1.0f - juce::Decibels::gainToDecibels (peak)) : 0.0f;
}

SiltProcessor::Overview SiltProcessor::getOverview() const
{
    const juce::ScopedLock sl (sampleLock);
    Overview o;
    const int frames = sampleCopy.getNumSamples();
    if (frames == 0)
        return o;

    o.seconds = frames / sampleCopyRate;
    o.name = sampleName;
    for (int t : sampleTransients)
        o.transients.push_back ((float) t / (float) frames);
    o.minimum.resize (overviewColumns);
    o.maximum.resize (overviewColumns);

    for (int c = 0; c < overviewColumns; ++c)
    {
        const int from = (int) ((juce::int64) c * frames / overviewColumns);
        const int to = juce::jmax (from + 1, (int) ((juce::int64) (c + 1) * frames / overviewColumns));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (sampleCopy.getReadPointer (ch, from), to - from);
            lo = juce::jmin (lo, range.getStart());
            hi = juce::jmax (hi, range.getEnd());
        }
        o.minimum[(size_t) c] = lo;
        o.maximum[(size_t) c] = hi;
    }
    return o;
}

// State: magic, version, the parameter XML, then the sample as FLAC (plan D10).
void SiltProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    juce::StringArray ccs;
    for (const auto& cc : knobCCs)
        ccs.add (juce::String (cc.load()));
    state.setProperty ("knobCCs", ccs.joinIntoString (","), nullptr);
    state.setProperty ("pageCCs", juce::String (previousPageCC.load()) + "," + juce::String (nextPageCC.load()), nullptr);
    state.setProperty ("page", currentPage.load(), nullptr);
    state.setProperty ("resonatorHalf", resonatorHalf.load(), nullptr);
    state.setProperty ("modRoutes", routesToString(), nullptr);
    state.setProperty ("modShown", modShown.load(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("randomAmount", randomAmount.load(), nullptr);
    state.setProperty ("randomExcluded", randomExcluded.joinIntoString (","), nullptr);
    if (editorWidth.load() > 0)
    {
        state.setProperty ("editorWidth", editorWidth.load(), nullptr);
        state.setProperty ("editorHeight", editorHeight.load(), nullptr);
    }

    juce::MemoryBlock flac;
    state.removeProperty ("factorySample", nullptr);
    {
        const juce::ScopedLock sl (sampleLock);
        if (factorySample >= 0)
        {
            state.setProperty ("factorySample", factorySample, nullptr);   // built in: no need to carry the audio
            state.setProperty ("sampleName", sampleName, nullptr);
        }
        else if (sampleCopy.getNumSamples() > 0)
        {
            if (encodedSample.isEmpty())
            {
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (encodedSample, false);
                if (auto writer = juce::FlacAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                        .withSampleRate (sampleCopyRate)
                                                                                        .withNumChannels (2)
                                                                                        .withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (sampleCopy, 0, sampleCopy.getNumSamples());
            }
            flac = encodedSample;
            state.setProperty ("sampleName", sampleName, nullptr);
        }
    }

    juce::MemoryBlock xmlBlock;
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, xmlBlock);

    juce::MemoryOutputStream out (destData, false);
    out.writeInt (stateMagic);
    out.writeInt (stateVersion);
    out.writeInt64 ((juce::int64) xmlBlock.getSize());
    out.write (xmlBlock.getData(), xmlBlock.getSize());
    out.writeInt64 ((juce::int64) flac.getSize());
    out.write (flac.getData(), flac.getSize());
}

void SiltProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    std::unique_ptr<juce::XmlElement> xml;
    juce::MemoryBlock flac;

    if (sizeInBytes > 8 && in.readInt() == stateMagic)
    {
        in.readInt();   // version
        juce::MemoryBlock xmlBlock;
        in.readIntoMemoryBlock (xmlBlock, (juce::pointer_sized_int) in.readInt64());
        xml = getXmlFromBinary (xmlBlock.getData(), (int) xmlBlock.getSize());
        in.readIntoMemoryBlock (flac, (juce::pointer_sized_int) in.readInt64());
    }
    else
    {
        xml = getXmlFromBinary (data, sizeInBytes);   // M0 states: parameters only
    }

    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;

    stateRestored.store (true);
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto name = tree.getProperty ("sampleName").toString();
    const int factory = tree.getProperty ("factorySample", -1);
    params.replaceState (tree);

    if (const auto ccs = juce::StringArray::fromTokens (tree.getProperty ("knobCCs").toString(), ",", ""); ccs.size() == slotsPerPage)
        for (int i = 0; i < slotsPerPage; ++i)
            knobCCs[(size_t) i].store (juce::jlimit (0, 127, ccs[i].getIntValue()));
    if (const auto pages = juce::StringArray::fromTokens (tree.getProperty ("pageCCs").toString(), ",", ""); pages.size() == 2)
    {
        previousPageCC.store (juce::jlimit (0, 127, pages[0].getIntValue()));
        nextPageCC.store (juce::jlimit (0, 127, pages[1].getIntValue()));
    }
    currentPage.store (juce::jlimit (0, numPages - 1, (int) tree.getProperty ("page", 0)));
    resonatorHalf.store (juce::jlimit (0, 1, (int) tree.getProperty ("resonatorHalf", 0)));
    routesFromString (tree.getProperty ("modRoutes").toString());
    modShown.store (juce::jlimit (0, numModulators - 1, (int) tree.getProperty ("modShown", 0)));
    routeUndo.clearUndoHistory();   // a loaded set starts its own history
    presetName = tree.getProperty ("presetName", "Init").toString();
    randomAmount.store (juce::jlimit (0.0f, 1.0f, (float) tree.getProperty ("randomAmount", 0.5f)));
    if (tree.hasProperty ("randomExcluded"))
        randomExcluded = juce::StringArray::fromTokens (tree.getProperty ("randomExcluded").toString(), ",", "");
    editorWidth.store ((int) tree.getProperty ("editorWidth", 0));
    editorHeight.store ((int) tree.getProperty ("editorHeight", 0));

    std::unique_ptr<juce::AudioFormatReader> reader;
    if (! flac.isEmpty())
        reader.reset (juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (flac, true), true));
    else if (factory >= 0 && factory < numFactorySamples())
        reader = openFactorySample (factory);
    if (reader == nullptr)
        return;

    const auto audio = decode (*reader);
    auto buffer = makeBuffer (*audio, reader->sampleRate);
    buffer->analyse();   // on the host's thread: hosts restore state off the audio thread
    installBuffer (std::move (buffer), name, flac.isEmpty() ? factory : -1);

    const juce::ScopedLock sl (sampleLock);
    encodedSample = flac;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SiltProcessor();
}
