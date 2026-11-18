#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

// Every parameter Silt has: its id, name, range, default and how its value reads.
// The one list both plug-ins build their host parameters from, and that the
// browser version and its JSON export read, so names, ranges and wording never
// drift apart. JUCE-free.
//
// The order is the host's parameter order: never reorder, only append.

namespace silt::params
{
enum class Kind : unsigned char { real, integer, boolean, choice };

// How a real parameter's knob travels over its range.
enum class Mapping : unsigned char { linear, skewed, cubicRate };

// Which builds have it.
enum class Builds : unsigned char { both, instrument, fx };

// How a value reads on the panel and in the host.
enum class Format : unsigned char
{
    plain,          // integers as they are; choices by name; switches On/Off
    percent,        // 42%
    finePercent,    // 42.15%
    signedPercent,  // +42%
    ms,             // 9.5 ms, 120 ms, 1.20 s
    seconds,        // seconds, read as ms
    hz,             // 2.50 Hz, 20.0 Hz
    kHz,            // 800 Hz, 1.20 kHz, 12.0 kHz
    semitones,      // +1.00 st
    semitonesSpread,// ±1.5 st
    semitonesDepth, // 1.5 st
    times,          // 1.00x
    db,             // -3.0 dB, -inf dB
    sampleGain,     // +3.0 dB
    note,           // C3
    grainCount,     // 8 grains
    steps,          // 8 steps
    heads,          // 2 heads
    resTune,        // C2 +15c
    resShift,       // +1 OCT, +7 ST
    octaves,        // 8.0 oct
    oddEven,        // ODD 40%, EVEN 40%, ALL
    stOffset,       // +0.50 st
    peaks,          // 2.0 peaks
    perSecond,      // 200 /s
    morph,          // LP, LP-BP 40%
    tone,           // DARK 20%, FLAT
    bits,           // 8.0 bits, OFF
    colorRate,      // 4.00 kHz, OFF
    spaceTone,      // DARK 20%, NATURAL
    modRate,        // STOP, 1.00 Hz, -2.00 Hz
    waveShape,      // SINE, SINE-TRI 40%
    degrees,        // 90°
};

// Choice lists.
inline constexpr const char* divisions[] { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T",
                                           "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 bar", "2 bars", "4 bars" };
inline constexpr double divisionQuarters[] { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2, 2.0 / 3,
                                             3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 3.0, 4.0, 8.0, 16.0 };
inline constexpr const char* scanBarNames[] { "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS" };
inline constexpr double scanBars[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
inline constexpr const char* scaleNames[] { "Off", "Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian",
                                            "Mixolydian", "Major pent.", "Minor pent.", "Whole tone", "Octaves", "Fifths" };
inline constexpr const char* keyNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline constexpr const char* rateModes[] { "Free", "Sync", "Grains" };
inline constexpr const char* snapModes[] { "Off", "Zero crossing", "Transient" };
inline constexpr const char* endModes[] { "Loop", "Hold", "Stop", "Bounce" };
inline constexpr const char* sliceModes[] { "Equal", "Transients", "Manual" };
inline constexpr const char* keyModes[] { "Pitch", "Live", "Feed", "Slice" };
inline constexpr const char* liveRuns[] { "Off", "Always", "Follow" };
inline constexpr const char* resNoteNames[] { "Scale", "Harmonics", "Bell", "Sample" };
inline constexpr const char* filterModes[] { "12 dB", "24 dB" };
inline constexpr const char* spaceModes[] { "Ping-pong", "Stereo" };
inline constexpr const char* duckModes[] { "Off", "Light", "Medium", "Heavy" };
inline constexpr const char* chainOrders[] { "R>F>C>S", "R>F>S>C", "R>C>F>S", "R>C>S>F", "R>S>F>C", "R>S>C>F",
                                             "F>R>C>S", "F>R>S>C", "F>C>R>S", "F>C>S>R", "F>S>R>C", "F>S>C>R",
                                             "C>R>F>S", "C>R>S>F", "C>F>R>S", "C>F>S>R", "C>S>R>F", "C>S>F>R",
                                             "S>R>F>C", "S>R>C>F", "S>F>R>C", "S>F>C>R", "S>C>R>F", "S>C>F>R" };
inline constexpr const char* modTypes[] { "Wave", "Random", "Envelope", "Follow", "MIDI", "Macro", "Per grain" };
inline constexpr const char* midiSources[] { "Velocity", "Note", "Mod wheel", "Aftertouch", "Pitch bend" };
inline constexpr const char* grainSpreads[] { "Even", "Centre", "Edges" };
inline constexpr const char* captureLengths[] { "1 bar", "2 bars", "4 bars", "8 bars" };
inline constexpr int captureBarCounts[] { 1, 2, 4, 8 };
inline constexpr std::span<const char* const> noChoices {};

inline constexpr float colorRateOff = 40000.0f;   // silt::Color::rateOff
inline constexpr int resonatorBands = 128;        // silt::Resonator::maxBands

// X (ident, "id", "Name", kind, low, high, interval, skew centre (0: linear), default, choices, format, builds)
#define SILT_PERCENT(ident, id, name, def) X (ident, id, name, real, 0.0f, 100.0f, 0.1f, 0.0f, def, noChoices, percent, both)

#define SILT_MODULATOR_PARAMS(X, n) \
    X (m##n##Type, "m" #n "Type", "Mod " #n " type", choice, 0, 6, 1, 0, 0, modTypes, plain, both) \
    X (m##n##Rate, "m" #n "Rate", "Mod " #n " rate", real, -50.0f, 50.0f, 0.0f, -1.0f, 1.0f, noChoices, modRate, both) \
    X (m##n##Sync, "m" #n "Sync", "Mod " #n " sync", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (m##n##Div, "m" #n "Div", "Mod " #n " rate (synced)", choice, 0, 16, 1, 0, 9, divisions, plain, both) \
    X (m##n##Shape, "m" #n "Shape", "Mod " #n " shape", real, 0.0f, 3.0f, 0.001f, 0.0f, 0.0f, noChoices, waveShape, both) \
    X (m##n##Phase, "m" #n "Phase", "Mod " #n " phase", real, 0.0f, 360.0f, 0.1f, 0.0f, 0.0f, noChoices, degrees, both) \
    X (m##n##Retrig, "m" #n "Retrig", "Mod " #n " retrigger", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (m##n##Rev, "m" #n "Rev", "Mod " #n " reverse (synced)", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (m##n##Smooth, "m" #n "Smooth", "Mod " #n " smooth", real, 0.0f, 100.0f, 0.1f, 0.0f, 100.0f, noChoices, percent, both) \
    X (m##n##Jitter, "m" #n "Jitter", "Mod " #n " jitter", real, 0.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, percent, both) \
    X (m##n##Attack, "m" #n "Attack", "Mod " #n " attack", real, 0.0f, 5000.0f, 0.0f, 100.0f, 10.0f, noChoices, ms, both) \
    X (m##n##Decay, "m" #n "Decay", "Mod " #n " decay", real, 1.0f, 5000.0f, 0.0f, 200.0f, 300.0f, noChoices, ms, both) \
    X (m##n##Sustain, "m" #n "Sustain", "Mod " #n " sustain", real, 0.0f, 100.0f, 0.1f, 0.0f, 50.0f, noChoices, percent, both) \
    X (m##n##Release, "m" #n "Release", "Mod " #n " release", real, 1.0f, 10000.0f, 0.0f, 400.0f, 500.0f, noChoices, ms, both) \
    X (m##n##Repeat, "m" #n "Repeat", "Mod " #n " repeat", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (m##n##FAttack, "m" #n "FAttack", "Mod " #n " follow attack", real, 0.1f, 1000.0f, 0.0f, 30.0f, 10.0f, noChoices, ms, both) \
    X (m##n##FRelease, "m" #n "FRelease", "Mod " #n " follow release", real, 1.0f, 3000.0f, 0.0f, 200.0f, 200.0f, noChoices, ms, both) \
    X (m##n##Gain, "m" #n "Gain", "Mod " #n " follow gain", real, -24.0f, 24.0f, 0.1f, 0.0f, 0.0f, noChoices, db, both) \
    X (m##n##Source, "m" #n "Source", "Mod " #n " MIDI source", choice, 0, 4, 1, 0, 0, midiSources, plain, both) \
    X (m##n##Enable, "m" #n "Enable", "Mod " #n " on", boolean, 0, 1, 1, 0, 1, noChoices, plain, both) \
    X (m##n##Voice, "m" #n "Voice", "Mod " #n " per voice", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (m##n##Spread, "m" #n "Spread", "Mod " #n " voice spread", real, 0.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, percent, both) \
    X (m##n##Macro, "m" #n "Macro", "Mod " #n " macro", real, 0.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, percent, both) \
    X (m##n##GrainSpread, "m" #n "GrainSpread", "Mod " #n " grain spread", choice, 0, 2, 1, 0, 0, grainSpreads, plain, both) \
    X (m##n##Sym, "m" #n "Sym", "Mod " #n " symmetry", real, 0.0f, 100.0f, 0.1f, 0.0f, 50.0f, noChoices, percent, both) \
    X (m##n##Curve, "m" #n "Curve", "Mod " #n " curve", real, -100.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, signedPercent, both)

#define SILT_PARAMS(X) \
    X (pitch, "pitch", "Pitch", real, -24.0f, 24.0f, 0.01f, 0.0f, 0.0f, noChoices, semitones, both) \
    X (size, "size", "Size", real, 10.0f, 2000.0f, 0.0f, 150.0f, 100.0f, noChoices, ms, both) \
    X (rate, "rate", "Rate", real, 0.1f, 100.0f, 0.0f, 10.0f, 20.0f, noChoices, hz, both) \
    SILT_PERCENT (spray, "spray", "Spray", 0.0f) \
    SILT_PERCENT (jitter, "jitter", "Jitter", 50.0f) \
    SILT_PERCENT (reverse, "reverse", "Reverse", 0.0f) \
    SILT_PERCENT (randAmp, "randAmp", "Random amp", 0.0f) \
    SILT_PERCENT (randPan, "randPan", "Random pan", 0.0f) \
    SILT_PERCENT (fade, "fade", "Fade", 100.0f) \
    SILT_PERCENT (slant, "slant", "Slant", 50.0f) \
    SILT_PERCENT (ease, "ease", "Ease", 50.0f) \
    X (sizeSync, "sizeSync", "Size sync", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (sizeDiv, "sizeDiv", "Size (synced)", choice, 0, 16, 1, 0, 3, divisions, plain, both) \
    X (rateMode, "rateMode", "Rate mode", choice, 0, 2, 1, 0, 0, rateModes, plain, both) \
    X (rateDiv, "rateDiv", "Rate (synced)", choice, 0, 16, 1, 0, 3, divisions, plain, both) \
    X (grainSize, "grainSize", "Size (grains)", real, 10.0f, 500.0f, 0.0f, 60.0f, 80.0f, noChoices, ms, both) \
    X (grains, "grains", "Grains", integer, 1, 32, 1, 0, 8, noChoices, grainCount, both) \
    X (start, "start", "Start", real, 0.0f, 100.0f, 0.001f, 0.0f, 0.0f, noChoices, finePercent, both) \
    X (length, "length", "Length", real, 0.0f, 100.0f, 0.001f, 0.0f, 100.0f, noChoices, finePercent, both) \
    X (snap, "snap", "Snap", choice, 0, 2, 1, 0, 0, snapModes, plain, both) \
    X (scan, "scan", "Scan", real, -4.0f, 4.0f, 0.01f, 0.0f, 1.0f, noChoices, times, both) \
    X (scanSync, "scanSync", "Scan sync", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (scanDiv, "scanDiv", "Scan (bars a pass)", choice, 0, 7, 1, 0, 4, scanBarNames, plain, both) \
    X (endMode, "endMode", "End", choice, 0, 3, 1, 0, 0, endModes, plain, both) \
    X (attack, "attack", "Attack", real, 0.0f, 5000.0f, 0.0f, 100.0f, 5.0f, noChoices, ms, both) \
    X (decay, "decay", "Decay", real, 1.0f, 5000.0f, 0.0f, 200.0f, 200.0f, noChoices, ms, both) \
    SILT_PERCENT (sustain, "sustain", "Sustain", 100.0f) \
    X (release, "release", "Release", real, 1.0f, 10000.0f, 0.0f, 400.0f, 300.0f, noChoices, ms, both) \
    X (root, "root", "Root note", integer, 0, 127, 1, 0, 60, noChoices, note, both) \
    X (randPitch, "randPitch", "Random pitch", real, 0.0f, 24.0f, 0.01f, 0.0f, 0.0f, noChoices, semitonesSpread, both) \
    X (scale, "scale", "Scale", choice, 0, 12, 1, 0, 0, scaleNames, plain, both) \
    X (key, "key", "Key", choice, 0, 11, 1, 0, 0, keyNames, plain, both) \
    X (patternDepth, "patternDepth", "Pattern", real, 0.0f, 24.0f, 0.01f, 0.0f, 0.0f, noChoices, semitonesDepth, both) \
    X (patternSteps, "patternSteps", "Pattern steps", integer, 2, 16, 1, 0, 8, noChoices, steps, both) \
    X (patternSeed, "patternSeed", "Pattern seed", integer, 1, 9999, 1, 0, 1, noChoices, plain, both) \
    X (heads, "heads", "Heads", integer, 1, 8, 1, 0, 1, noChoices, heads, both) \
    SILT_PERCENT (headSpread, "headSpread", "Head spread", 50.0f) \
    X (sliceMode, "sliceMode", "Slice by", choice, 0, 2, 1, 0, 0, sliceModes, plain, both) \
    X (sliceCount, "sliceCount", "Slices", integer, 2, 64, 1, 0, 16, noChoices, plain, both) \
    X (keyMode, "keyMode", "Mode", choice, 0, 3, 1, 0, 0, keyModes, plain, instrument) \
    X (liveDelay, "liveDelay", "Delay", real, 10.0f, 4000.0f, 0.0f, 300.0f, 250.0f, noChoices, ms, both) \
    X (delaySync, "delaySync", "Delay sync", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (delayDiv, "delayDiv", "Delay (synced)", choice, 0, 16, 1, 0, 6, divisions, plain, both) \
    X (feedback, "feedback", "Feedback", real, 0.0f, 120.0f, 0.1f, 0.0f, 0.0f, noChoices, percent, both) \
    X (freeze, "freeze", "Freeze", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (liveRun, "liveRun", "No notes", choice, 0, 2, 1, 0, 1, liveRuns, plain, both) \
    X (resTune, "resTune", "Resonator tune", real, 24.0f, 96.0f, 0.01f, 0.0f, 48.0f, noChoices, resTune, both) \
    X (resShift, "resShift", "Resonator shift", real, -36.0f, 36.0f, 1.0f, 0.0f, 0.0f, noChoices, resShift, both) \
    X (resNotes, "resNotes", "Resonator notes", choice, 0, 3, 1, 0, 0, resNoteNames, plain, both) \
    X (resRange, "resRange", "Resonator range", real, 1.0f, 8.0f, 0.01f, 0.0f, 8.0f, noChoices, octaves, both) \
    X (resDecay, "resDecay", "Resonator decay", real, 0.05f, 20.0f, 0.0f, 1.5f, 1.5f, noChoices, seconds, both) \
    SILT_PERCENT (resBright, "resBright", "Resonator bright", 50.0f) \
    SILT_PERCENT (resWidth, "resWidth", "Resonator width", 50.0f) \
    X (resFollow, "resFollow", "Resonator follows notes", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    SILT_PERCENT (resMix, "resMix", "Resonator mix", 0.0f) \
    SILT_PERCENT (resReso, "resReso", "Resonator resonance", 50.0f) \
    X (resBands, "resBands", "Resonator bands", integer, 1, resonatorBands, 1, 0, resonatorBands, noChoices, plain, both) \
    SILT_PERCENT (resDrift, "resDrift", "Resonator drift", 0.0f) \
    SILT_PERCENT (resSpread, "resSpread", "Resonator spread", 0.0f) \
    X (resOddEven, "resOddEven", "Resonator odd/even", real, -100.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, oddEven, both) \
    SILT_PERCENT (resDrive, "resDrive", "Resonator drive", 0.0f) \
    SILT_PERCENT (resDamp, "resDamp", "Resonator damp", 0.0f) \
    X (resStretch, "resStretch", "Resonator stretch", real, -100.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, signedPercent, both) \
    X (resCurve, "resCurve", "Resonator stretch curve", real, -100.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, signedPercent, both) \
    SILT_PERCENT (resSnap, "resSnap", "Resonator snap", 0.0f) \
    X (resOffset, "resOffset", "Resonator offset", real, -12.0f, 12.0f, 0.01f, 0.0f, 0.0f, noChoices, stOffset, both) \
    X (resRipple, "resRipple", "Resonator ripple", real, 1.0f, 16.0f, 0.01f, 0.0f, 2.0f, noChoices, peaks, both) \
    SILT_PERCENT (resDepth, "resDepth", "Resonator ripple depth", 0.0f) \
    SILT_PERCENT (resSweep, "resSweep", "Resonator sweep", 0.0f) \
    SILT_PERCENT (resStrike, "resStrike", "Resonator strike", 0.0f) \
    SILT_PERCENT (resPosition, "resPosition", "Resonator strike position", 30.0f) \
    SILT_PERCENT (resHardness, "resHardness", "Resonator strike hardness", 60.0f) \
    SILT_PERCENT (resParticles, "resParticles", "Resonator particles", 0.0f) \
    X (resDensity, "resDensity", "Resonator particle density", real, 2.0f, 5000.0f, 0.0f, 200.0f, 200.0f, noChoices, perSecond, both) \
    SILT_PERCENT (resTop, "resTop", "Resonator top", 0.0f) \
    SILT_PERCENT (resSaturate, "resSaturate", "Resonator saturate", 0.0f) \
    X (filterMode, "filterMode", "Filter", choice, 0, 1, 1, 0, 0, filterModes, plain, both) \
    X (filterCutoff, "filterCutoff", "Filter cutoff", real, 20.0f, 20000.0f, 0.0f, 1000.0f, 20000.0f, noChoices, kHz, both) \
    SILT_PERCENT (filterReso, "filterReso", "Filter resonance", 0.0f) \
    X (filterMorph, "filterMorph", "Filter morph", real, 0.0f, 2.0f, 0.001f, 0.0f, 0.0f, noChoices, morph, both) \
    SILT_PERCENT (filterDrive, "filterDrive", "Filter drive", 0.0f) \
    X (filterEnv, "filterEnv", "Filter envelope", real, -100.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, signedPercent, both) \
    SILT_PERCENT (filterKey, "filterKey", "Filter key tracking", 0.0f) \
    SILT_PERCENT (filterMix, "filterMix", "Filter mix", 100.0f) \
    SILT_PERCENT (colorDrive, "colorDrive", "Color drive", 0.0f) \
    X (colorTone, "colorTone", "Color tone", real, -100.0f, 100.0f, 0.1f, 0.0f, 0.0f, noChoices, tone, both) \
    X (colorBits, "colorBits", "Color bits", real, 2.0f, 16.0f, 0.01f, 0.0f, 16.0f, noChoices, bits, both) \
    X (colorRate, "colorRate", "Color rate", real, 200.0f, colorRateOff, 0.0f, 4000.0f, colorRateOff, noChoices, colorRate, both) \
    SILT_PERCENT (colorComp, "colorComp", "Color comp", 0.0f) \
    SILT_PERCENT (colorNoise, "colorNoise", "Color noise", 0.0f) \
    SILT_PERCENT (colorWow, "colorWow", "Color wow", 0.0f) \
    SILT_PERCENT (colorMix, "colorMix", "Color mix", 100.0f) \
    X (spaceTime, "spaceTime", "Delay time", real, 10.0f, 4000.0f, 0.0f, 400.0f, 375.0f, noChoices, ms, both) \
    X (spaceSync, "spaceSync", "Delay sync", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (spaceDiv, "spaceDiv", "Delay time (synced)", choice, 0, 16, 1, 0, 8, divisions, plain, both) \
    SILT_PERCENT (spaceFeedback, "spaceFeedback", "Delay feedback", 35.0f) \
    X (spaceMode, "spaceMode", "Delay mode", choice, 0, 1, 1, 0, 0, spaceModes, plain, both) \
    SILT_PERCENT (spaceDelay, "spaceDelay", "Delay level", 0.0f) \
    SILT_PERCENT (spaceSize, "spaceSize", "Reverb size", 50.0f) \
    X (spaceDecay, "spaceDecay", "Reverb decay", real, 0.3f, 30.0f, 0.0f, 3.0f, 3.0f, noChoices, seconds, both) \
    X (spaceTone, "spaceTone", "Space tone", real, 0.0f, 100.0f, 0.1f, 0.0f, 50.0f, noChoices, spaceTone, both) \
    SILT_PERCENT (spaceShimmer, "spaceShimmer", "Reverb shimmer", 0.0f) \
    SILT_PERCENT (spaceReverb, "spaceReverb", "Reverb level", 0.0f) \
    X (spaceFreeze, "spaceFreeze", "Space freeze", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (spaceDuck, "spaceDuck", "Space duck", choice, 0, 3, 1, 0, 0, duckModes, plain, both) \
    X (chainOrder, "chainOrder", "Chain order", choice, 0, 23, 1, 0, 0, chainOrders, plain, both) \
    X (resOn, "resOn", "Resonator on", boolean, 0, 1, 1, 0, 1, noChoices, plain, both) \
    X (filterOn, "filterOn", "Filter on", boolean, 0, 1, 1, 0, 1, noChoices, plain, both) \
    X (colorOn, "colorOn", "Color on", boolean, 0, 1, 1, 0, 1, noChoices, plain, both) \
    X (spaceOn, "spaceOn", "Space on", boolean, 0, 1, 1, 0, 1, noChoices, plain, both) \
    SILT_MODULATOR_PARAMS (X, 1) \
    SILT_MODULATOR_PARAMS (X, 2) \
    SILT_MODULATOR_PARAMS (X, 3) \
    SILT_MODULATOR_PARAMS (X, 4) \
    X (captureBars, "captureBars", "Capture length", choice, 0, 3, 1, 0, 1, captureLengths, plain, both) \
    X (captureFeed, "captureFeed", "Resample own output", boolean, 0, 1, 1, 0, 0, noChoices, plain, both) \
    X (gain, "gain", "Output", real, -48.0f, 12.0f, 0.1f, 0.0f, 0.0f, noChoices, db, both) \
    X (sampleGain, "sampleGain", "Sample gain", real, -24.0f, 24.0f, 0.1f, 0.0f, 0.0f, noChoices, sampleGain, both) \
    SILT_PERCENT (mix, "mix", "Mix", 100.0f) \
    X (monitor, "monitor", "Monitor input", boolean, 0, 1, 1, 0, 0, noChoices, plain, instrument) \
    X (inputGain, "inputGain", "Input", real, -24.0f, 24.0f, 0.1f, 0.0f, 0.0f, noChoices, db, fx)

enum Id : int
{
#define X(ident, ...) ident,
    SILT_PARAMS (X)
#undef X
    count
};

struct Info
{
    const char* id;
    const char* name;
    Kind kind;
    float low, high;
    float interval;       // real: 0 continuous; integer, choice, switch: 1
    float centre;         // real, skewed: the value at the knob's middle; 0 linear; −1 the modulators' cubic rate
    float defaultValue;   // in the parameter's own units (liveRun: the instrument's; see defaultFor)
    std::span<const char* const> choices;
    Format format;
    Builds builds;

    Mapping mapping() const noexcept
    {
        return centre < 0.0f ? Mapping::cubicRate : centre > 0.0f ? Mapping::skewed : Mapping::linear;
    }
};

const Info& info (int id) noexcept;
int find (std::string_view id) noexcept;   // −1 if there's none

bool inBuild (int id, bool instrument) noexcept;
float defaultFor (int id, bool instrument) noexcept;

// The knob's travel, as a host sees it (0–1), and back: exactly as JUCE's
// RangedAudioParameter does it (snapped to the parameter's steps), so the
// plug-in and the browser move a modulated value identically.
float to01 (int id, float value) noexcept;
float from01 (int id, float normalised) noexcept;
float snapValue (int id, float value) noexcept;
float rangeFrom01 (int id, float normalised) noexcept;   // from01 before snapping

// A value as a JUCE host holds it. Before anything moves: the default through
// 0–1 and back, not snapped (JUCE's "legacy" start). Set: through 0–1 twice
// (the parameter, then the value tree), and a change approximately equal to the
// value held is dropped, except the first.
float hostDefault (int id, bool instrument) noexcept;
float hostSet (int id, float value) noexcept;
bool approximatelyEqual (float a, float b) noexcept;   // JUCE's

// How a value reads, exactly as in the plug-in.
std::string text (int id, float value);

// Whether presets carry it: session and sample settings (ROOT is the sample's
// key, INPUT the track's level) aren't part of a sound.
bool inPresets (int id) noexcept;

// Synced note values and SCAN's bars.
double quartersFor (int divisionIndex) noexcept;
} // namespace silt::params
