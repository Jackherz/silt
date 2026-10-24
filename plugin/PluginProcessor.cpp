#include "PluginProcessor.h"
#include "PluginEditor.h"

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
constexpr int sixteenth = 3, eighth = 6, dottedEighth = 8;
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

std::unique_ptr<Float> percent (const char* id, const char* name, float def)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, Range (0.0f, 100.0f, 0.1f), def,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPercent));
}

std::unique_ptr<Float> withUnit (const char* id, const char* name, Range range, float def, Format format)
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
                                                              juce::StringArray { "Scale", "Harmonics", "Bell" }, 0),
                withUnit ("resRange", "Resonator range", Range (1.0f, 4.0f, 0.01f), 2.0f,
                          [] (float v, int) { return juce::String (v, 1) + " oct"; }),
                withUnit ("resDecay", "Resonator decay", skewed (0.05f, 20.0f, 1.5f), 1.5f,
                          [] (float v, int) { return formatMs (v * 1000.0f, 0); }),
                percent ("resBright", "Resonator bright", 50.0f),
                percent ("resWidth", "Resonator width", 50.0f),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "resFollow", 1 }, "Resonator follows notes", false),
                percent ("resMix", "Resonator mix", 0.0f));

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
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "spaceFreeze", 1 }, "Space freeze", false));

    // Capture and output
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "captureBars", 1 }, "Capture length",
                                                              captureBarChoices, 1),
                withUnit ("gain", "Output", Range (-48.0f, 12.0f, 0.1f), 0.0f, formatDb));

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
    explicit RawParams (juce::AudioProcessorValueTreeState& s)
    {
        const auto p = [&s] (const char* id) { return s.getRawParameterValue (id); };
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
        filterMode = p ("filterMode"); filterCutoff = p ("filterCutoff"); filterReso = p ("filterReso"); filterMorph = p ("filterMorph");
        filterDrive = p ("filterDrive"); filterEnv = p ("filterEnv"); filterKey = p ("filterKey"); filterMix = p ("filterMix");
        colorDrive = p ("colorDrive"); colorTone = p ("colorTone"); colorBits = p ("colorBits"); colorRate = p ("colorRate");
        colorComp = p ("colorComp"); colorNoise = p ("colorNoise"); colorWow = p ("colorWow"); colorMix = p ("colorMix");
        spaceTime = p ("spaceTime"); spaceSync = p ("spaceSync"); spaceDiv = p ("spaceDiv"); spaceFeedback = p ("spaceFeedback");
        spaceMode = p ("spaceMode"); spaceDelay = p ("spaceDelay"); spaceSize = p ("spaceSize"); spaceDecay = p ("spaceDecay");
        spaceTone = p ("spaceTone"); spaceShimmer = p ("spaceShimmer"); spaceReverb = p ("spaceReverb"); spaceFreeze = p ("spaceFreeze");
        monitor = p ("monitor"); mix = p ("mix");   // one of these is null, per build
    }

    std::atomic<float>* pitch, *size, *rate, *spray, *jitter, *reverse, *randAmp, *randPan, *fade, *slant, *ease;
    std::atomic<float>* sizeSync, *sizeDiv, *rateMode, *rateDiv, *grains, *grainSize;
    std::atomic<float>* start, *length, *scan, *endMode, *snap;
    std::atomic<float>* attack, *decay, *sustain, *release, *root;
    std::atomic<float>* captureBars, *gain, *monitor, *mix;
    std::atomic<float>* keyMode, *liveDelay, *delaySync, *delayDiv, *feedback, *freeze, *liveRun;
    std::atomic<float>* randPitch, *scale, *key, *patternDepth, *patternSteps, *patternSeed, *heads, *headSpread;
    std::atomic<float>* resTune, *resNotes, *resRange, *resDecay, *resBright, *resWidth, *resFollow, *resMix;
    std::atomic<float>* filterMode, *filterCutoff, *filterReso, *filterMorph, *filterDrive, *filterEnv, *filterKey, *filterMix;
    std::atomic<float>* colorDrive, *colorTone, *colorBits, *colorRate, *colorComp, *colorNoise, *colorWow, *colorMix;
    std::atomic<float>* spaceTime, *spaceSync, *spaceDiv, *spaceFeedback, *spaceMode, *spaceDelay, *spaceSize, *spaceDecay;
    std::atomic<float>* spaceTone, *spaceShimmer, *spaceReverb, *spaceFreeze;
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
      raw (std::make_unique<RawParams> (params))
{
    formats.registerBasicFormats();
    for (auto& v : voicePlayheads)
        v.store (-1.0f);
    for (size_t i = 0; i < knobCCs.size(); ++i)
        knobCCs[i].store (70 + (int) i);   // a common default for 8-knob controllers
    lastKnobValue.fill (-1);
    startTimerHz (30);
}

SiltProcessor::~SiltProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 5000);
}

void SiltProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, juce::jmax (1, maximumExpectedSamplesPerBlock));
    resonator.prepare (sampleRate);
    filter.prepare (sampleRate);
    color.prepare (sampleRate);
    space.prepare (sampleRate);
    lastNote = -1;
    liveColumn = -1;
    liveColumnPeak = 0.0f;
    for (auto& c : livePeaks)
        c.store (0.0f);
    wet.setSize (2, juce::jmax (1, maximumExpectedSamplesPerBlock));
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

silt::GrainParams SiltProcessor::readParams() const noexcept
{
    const auto v = [] (const std::atomic<float>* a) { return (double) a->load (std::memory_order_relaxed); };

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
    p.rootNote = v (raw->resFollow) > 0.5f && lastNote >= 0 ? (float) lastNote : v (raw->resTune);
    p.notes = (silt::ResonatorNotes) juce::jlimit (0, 2, (int) v (raw->resNotes));
    p.scale = grain.scale;
    p.key = grain.key;
    p.rangeOctaves = v (raw->resRange);
    p.decaySeconds = v (raw->resDecay);
    p.bright = v (raw->resBright) * 0.01f;
    p.width = v (raw->resWidth) * 0.01f;
    p.mix = v (raw->resMix) * 0.01f;
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
        {
            noteEvents.push_back ({ event.samplePosition, m.getNoteNumber(), m.getFloatVelocity(), m.isNoteOn() });
            if (m.isNoteOn())
                lastNote = m.getNoteNumber();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allOff = true;
        else if (m.isController())
            handleController (m.getControllerNumber(), m.getControllerValue());
    }
    if (allOff)
        engine.allNotesOff();

    auto source = CaptureSource::none;
    auto capture = findCaptureBus (buffer, source);
    captureSource.store (source, std::memory_order_relaxed);
    if (capture.getNumChannels() > 0)
        captureMeter.process (capture.getArrayOfReadPointers(), capture.getNumChannels(), numSamples);
    runCapture (capture, position ? &*position : nullptr);

    // Engine output. Hosts can send a block bigger than announced; render in chunks.
    auto output = getBusBuffer (buffer, false, 0);
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
    double blockPpq = 0.0;
    if ((int) raw->rateMode->load (std::memory_order_relaxed) == rateModeSync && position && position->getIsPlaying())
        if (const auto ppq = position->getPpqPosition())
            if (const auto bpm = position->getBpm(); bpm && *bpm > 0.0)
            {
                grainParams.gridSync = true;
                grainParams.ppqPerSample = *bpm / 60.0 / currentSampleRate;
                blockPpq = *ppq;
            }

    for (int start = 0; start < numSamples; start += wet.getNumSamples())
    {
        const int len = juce::jmin (wet.getNumSamples(), numSamples - start);
        grainParams.gridPpq = blockPpq + start * grainParams.ppqPerSample;

        // Events for this chunk, shifted to its start.
        const auto first = std::find_if (noteEvents.begin(), noteEvents.end(), [start] (const auto& e) { return e.sampleOffset >= start; });
        const auto last = std::find_if (first, noteEvents.end(), [start, len] (const auto& e) { return e.sampleOffset >= start + len; });
        for (auto it = first; it != last; ++it)
            it->sampleOffset -= start;

        const int liveWriteBefore = engine.getLiveWritePosition();
        const float* inLeft = capture.getNumChannels() > 0 ? capture.getReadPointer (0, start) : nullptr;
        const float* inRight = capture.getNumChannels() > 1 ? capture.getReadPointer (1, start) : inLeft;
        engine.process (wet.getWritePointer (0), wet.getWritePointer (1), len, inLeft, inRight,
                        noteEvents.data() + (first - noteEvents.begin()), (int) (last - first), grainParams);
        if (grainParams.live && ! grainParams.freeze)
            trackLivePeaks (liveWriteBefore, len);
        resonator.process (wet.getWritePointer (0), wet.getWritePointer (1), len, resonatorParams);
        filter.process (wet.getWritePointer (0), wet.getWritePointer (1), len, filterParams);
        color.process (wet.getWritePointer (0), wet.getWritePointer (1), len, colorParams);
        space.process (wet.getWritePointer (0), wet.getWritePointer (1), len, spaceParams);

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
    liveMode.store (grainParams.live, std::memory_order_relaxed);
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
            currentPage.store ((currentPage.load() + (which == 1 ? 1 : numPages - 1)) % numPages);
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

// Which parameter a page slot moves right now, matching the editor's layout.
juce::String SiltProcessor::slotParameter (int page, int slot) const
{
    const auto v = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };

    switch (page)
    {
        case 0:   // GRAIN
        {
            const int rateMode = (int) v ("rateMode");
            const char* ids[] { "pitch", nullptr, nullptr, "spray", "jitter", "reverse", "randAmp", "randPan" };
            if (slot == 1)
                return v ("sizeSync") > 0.5f ? "sizeDiv" : rateMode == 2 ? "grainSize" : "size";
            if (slot == 2)
                return rateMode == 1 ? "rateDiv" : rateMode == 2 ? "grains" : "rate";
            return ids[slot];
        }
        case 1:   // SHAPE
        {
            const char* ids[] { "fade", "slant", "ease", "attack", "decay", "sustain", "release", "" };
            return ids[slot];
        }
        case 2:   // PITCH
        {
            const char* ids[] { "randPitch", "scale", "key", "patternDepth", "patternSteps", "!newPattern", "heads", "headSpread" };
            return ids[slot];
        }
        case 4:   // RESONATOR
        {
            const char* ids[] { "resTune", "resNotes", "resRange", "resDecay", "resBright", "resWidth", "resFollow", "resMix" };
            return ids[slot];
        }
        case 5:   // FILTER
        {
            const char* ids[] { "filterMode", "filterCutoff", "filterReso", "filterMorph", "filterDrive", "filterEnv", "filterKey", "filterMix" };
            return ids[slot];
        }
        case 6:   // COLOR
        {
            const char* ids[] { "colorDrive", "colorTone", "colorBits", "colorRate", "colorComp", "colorNoise", "colorWow", "colorMix" };
            return ids[slot];
        }
        case 7:   // SPACE
        {
            const char* ids[] { nullptr, "spaceFeedback", "spaceDelay", "spaceSize", "spaceDecay", "spaceTone", "spaceShimmer", "spaceReverb" };
            if (slot == 0)
                return v ("spaceSync") > 0.5f ? "spaceDiv" : "spaceTime";
            return ids[slot];
        }
        case 3:   // PLAY
        {
            const bool live = (int) v ("keyMode") == 1;
            const char* pitchIds[] { "keyMode", "start", "length", "scan", "endMode", "root", "gain", "mix" };
            const char* liveIds[] { "keyMode", nullptr, "feedback", "freeze", "liveRun", "root", "gain", "mix" };
            if (slot == 7 && isInstrument)
                return {};
            if (live && slot == 1)
                return v ("delaySync") > 0.5f ? "delayDiv" : "liveDelay";
            return live ? liveIds[slot] : pitchIds[slot];
        }
        default: break;
    }
    return {};
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

void SiltProcessor::loadFile (const juce::File& file)
{
    juce::WeakReference<SiltProcessor> weak (this);

    loader.addJob ([this, weak, file]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            juce::MessageManager::callAsync ([weak, file]
            {
                if (weak != nullptr)
                    weak->lastLoadError = "CAN'T READ " + file.getFileName().toUpperCase();
            });
            return;
        }

        // Passed on by moving the buffer out of a shared holder: no copy.
        auto buffer = std::make_shared<std::unique_ptr<silt::SampleBuffer>> (makeBuffer (*decode (*reader), reader->sampleRate));
        (*buffer)->analyse();

        juce::MessageManager::callAsync ([weak, buffer, file]
        {
            if (weak != nullptr)
            {
                weak->lastLoadError.clear();
                weak->installBuffer (std::move (*buffer), file.getFileNameWithoutExtension());
            }
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
void SiltProcessor::installBuffer (std::unique_ptr<silt::SampleBuffer> buffer, const juce::String& name)
{
    const int frames = buffer->getNumFrames();
    const double rate = buffer->getSampleRate();
    {
        const juce::ScopedLock sl (sampleLock);
        sampleZeroCrossings = buffer->getZeroCrossings();
        sampleTransients = buffer->getTransients();
        sampleCopy.setSize (2, frames);
        for (int ch = 0; ch < 2; ++ch)
            sampleCopy.copyFrom (ch, 0, buffer->getReadPointer (ch), frames);
        sampleCopyRate = rate;
        sampleName = name;
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

    juce::MemoryBlock flac;
    {
        const juce::ScopedLock sl (sampleLock);
        if (sampleCopy.getNumSamples() > 0)
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

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto name = tree.getProperty ("sampleName").toString();
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

    if (flac.isEmpty())
        return;

    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (flac, true), true));
    if (reader == nullptr)
        return;

    const auto audio = decode (*reader);
    auto buffer = makeBuffer (*audio, reader->sampleRate);
    buffer->analyse();   // on the host's thread: hosts restore state off the audio thread
    installBuffer (std::move (buffer), name);

    const juce::ScopedLock sl (sampleLock);
    encodedSample = flac;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SiltProcessor();
}
