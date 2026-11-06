#include "Presets.h"
#include "Palette.h"

#include <map>

using namespace silt::ui;

namespace
{
const juce::String siltPresetExtension (".siltpreset");

// A factory preset: values in each parameter's own units (choices by index,
// switches 0/1), anything unlisted at its default; routes as
// "modulator(1–4):parameter:amount".
struct Factory
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;   // owned: an initializer_list here would dangle
    std::vector<const char*> routes;
    bool instrumentOnly = false;   // played from notes (SLICE, FEED): not in Silt FX, which usually gets none
};

// Division indexes: 1/32 = 1, 1/16 = 3, 1/8 = 6, 1/8D = 8, 1/4 = 9, 1/2 = 12, 1 bar = 14, 2 bars = 15.
// MODE: 0 PITCH, 1 LIVE, 2 FEED, 3 SLICE. Chain orders: 0 R>F>C>S, 12 C>R>F>S.
const std::vector<Factory>& allFactories()
{
    static const std::vector<Factory> list {
        { "Init", {}, {} },

        { "Glass Cloud",
          { { "size", 180.0f }, { "rate", 30.0f }, { "spray", 30.0f }, { "jitter", 60.0f }, { "randPan", 70.0f },
            { "randPitch", 0.1f }, { "fade", 80.0f }, { "attack", 120.0f }, { "release", 1500.0f },
            { "spaceReverb", 45.0f }, { "spaceDecay", 8.0f }, { "spaceSize", 85.0f }, { "spaceShimmer", 35.0f }, { "spaceTone", 60.0f },
            { "m1Rate", 0.15f }, { "m2Type", 1.0f }, { "m2Rate", 2.0f } },
          { "1:spray:0.2", "2:pitch:0.02" } },

        { "Tape Choir",
          { { "rateMode", 2.0f }, { "grains", 12.0f }, { "grainSize", 120.0f }, { "randPitch", 0.15f }, { "randPan", 50.0f },
            { "colorWow", 45.0f }, { "colorDrive", 30.0f }, { "colorTone", -25.0f }, { "colorNoise", 20.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 3.0f } },
          {} },

        { "Rust Rhythm",
          { { "rateMode", 1.0f }, { "rateDiv", 3.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 3.0f }, { "spray", 10.0f },
            { "filterMode", 1.0f }, { "filterCutoff", 1800.0f }, { "filterReso", 40.0f },
            { "colorBits", 7.0f }, { "colorRate", 9000.0f }, { "colorComp", 50.0f },
            { "spaceDelay", 30.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 45.0f },
            { "m1Sync", 1.0f }, { "m1Div", 14.0f } },
          { "1:filterCutoff:0.35" } },

        { "Bell Garden",
          { { "scale", 8.0f }, { "spray", 20.0f }, { "randPitch", 7.0f }, { "release", 900.0f },
            { "resMix", 70.0f }, { "resNotes", 2.0f }, { "resDecay", 3.5f }, { "resBright", 70.0f }, { "resWidth", 80.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 5.0f } },
          {} },

        // Per-voice: every note wanders through the sample on its own.
        { "Wander",
          { { "length", 40.0f }, { "scan", 0.5f }, { "size", 220.0f }, { "rate", 14.0f }, { "spray", 10.0f },
            { "m1Type", 1.0f }, { "m1Rate", 0.7f }, { "m1Voice", 1.0f },
            { "m2Type", 2.0f }, { "m2Attack", 900.0f }, { "m2Decay", 1500.0f }, { "m2Sustain", 30.0f }, { "m2Voice", 1.0f },
            { "spaceReverb", 25.0f } },
          { "1:start:0.25", "1:spray:0.2", "2:size:0.4" } },

        { "Dark Drone",
          { { "pitch", -12.0f }, { "size", 900.0f }, { "rate", 8.0f }, { "spray", 25.0f }, { "attack", 1500.0f }, { "release", 4000.0f },
            { "filterMode", 1.0f }, { "filterCutoff", 700.0f }, { "filterReso", 25.0f }, { "colorWow", 20.0f },
            { "spaceReverb", 60.0f }, { "spaceDecay", 20.0f }, { "spaceSize", 100.0f }, { "spaceTone", 30.0f },
            { "m1Rate", 0.05f } },
          { "1:filterCutoff:0.3" } },

        // MOD 4 is a macro: one knob opens the filter and the space together.
        { "Shimmer Pad",
          { { "attack", 800.0f }, { "release", 3000.0f }, { "fade", 100.0f }, { "size", 300.0f }, { "spray", 15.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 1200.0f },
            { "spaceShimmer", 60.0f }, { "spaceReverb", 55.0f }, { "spaceDecay", 12.0f },
            { "m4Type", 5.0f }, { "m4Macro", 30.0f } },
          { "4:filterCutoff:0.5", "4:spaceReverb:0.3", "4:spaceShimmer:0.3" } },

        // SLICE at the sample's hits: C1 up plays each hit, cut short at the next.
        { "Hit Chop",
          { { "keyMode", 3.0f }, { "sliceMode", 1.0f }, { "endMode", 2.0f },
            { "rateMode", 2.0f }, { "grains", 4.0f }, { "grainSize", 70.0f }, { "attack", 2.0f }, { "release", 120.0f },
            { "colorDrive", 25.0f }, { "colorComp", 40.0f },
            { "spaceDelay", 25.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 30.0f } },
          {}, true },

        // SLICE, sixteen equal parts, as synced stutters through crunch.
        { "Stutter Grid",
          { { "keyMode", 3.0f }, { "sliceCount", 16.0f }, { "endMode", 0.0f },
            { "rateMode", 1.0f }, { "rateDiv", 1.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 1.0f }, { "fade", 40.0f },
            { "attack", 1.0f }, { "release", 60.0f },
            { "colorBits", 9.0f }, { "colorRate", 12000.0f }, { "filterMode", 0.0f }, { "filterCutoff", 5000.0f }, { "filterReso", 30.0f },
            { "m1Type", 1.0f }, { "m1Sync", 1.0f }, { "m1Div", 6.0f }, { "m1Smooth", 0.0f } },
          { "1:filterCutoff:0.25", "1:reverse:0.5" }, true },

        // FEED: the grains keep re-granulating their own output, blooming as you hold.
        { "Feed Bloom",
          { { "keyMode", 2.0f }, { "liveRun", 0.0f }, { "feedback", 92.0f }, { "size", 260.0f }, { "rate", 12.0f },
            { "spray", 30.0f }, { "randPitch", 0.1f }, { "randPan", 60.0f }, { "attack", 400.0f }, { "release", 2500.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 3500.0f },
            { "spaceReverb", 40.0f }, { "spaceDecay", 6.0f }, { "spaceSize", 75.0f },
            { "m1Rate", 0.1f } },
          { "1:filterCutoff:0.2" }, true },

        // The resonator rung as the sample's own modes, drifting and damped.
        { "Sample Strings",
          { { "size", 220.0f }, { "spray", 20.0f }, { "release", 1800.0f },
            { "resMix", 65.0f }, { "resNotes", 3.0f }, { "resFollow", 1.0f }, { "resDecay", 4.0f }, { "resBright", 55.0f }, { "resWidth", 70.0f },
            { "resReso", 70.0f }, { "resDrift", 35.0f }, { "resSpread", 15.0f }, { "resOddEven", -40.0f },
            { "resDrive", 25.0f }, { "resDamp", 35.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 4.0f } },
          {} },

        // Grit first: COLOR runs before the resonator, so the distortion rings.
        { "Grit Bells",
          { { "chainOrder", 12.0f }, { "size", 90.0f }, { "rate", 25.0f }, { "spray", 15.0f }, { "release", 1200.0f },
            { "colorDrive", 55.0f }, { "colorBits", 10.0f }, { "colorTone", 20.0f },
            { "resMix", 55.0f }, { "resNotes", 2.0f }, { "resDecay", 2.5f }, { "resDamp", 20.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 5.0f } },
          {} },

        // Sparse grains striking a bell for each note held, every hit landing somewhere else on it.
        { "Struck Glass",
          { { "size", 60.0f }, { "rate", 6.0f }, { "spray", 15.0f }, { "randPan", 40.0f }, { "release", 1800.0f },
            { "resMix", 80.0f }, { "resNotes", 2.0f }, { "resFollow", 1.0f }, { "resDecay", 4.0f }, { "resBright", 70.0f }, { "resWidth", 75.0f },
            { "resStrike", 70.0f }, { "resHardness", 80.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 5.0f },
            { "m1Type", 6.0f }, { "m1GrainSpread", 1.0f } },
          { "1:resPosition:0.4" } },

        // A chord on the harmonic series: each note its own strings, the top lifted so they all sing.
        { "Wire Choir",
          { { "size", 240.0f }, { "rate", 18.0f }, { "spray", 20.0f }, { "attack", 400.0f }, { "release", 2500.0f },
            { "resMix", 75.0f }, { "resNotes", 1.0f }, { "resFollow", 1.0f }, { "resDecay", 6.0f }, { "resBright", 60.0f }, { "resWidth", 80.0f },
            { "resReso", 75.0f }, { "resDrift", 15.0f }, { "resTop", 30.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 6.0f } },
          {} },

        // A highlight turning slowly through the series, so the partials take turns.
        { "Slow Prism",
          { { "size", 320.0f }, { "rate", 16.0f }, { "spray", 25.0f }, { "randPan", 50.0f }, { "attack", 600.0f }, { "release", 3000.0f },
            { "resMix", 85.0f }, { "resNotes", 1.0f }, { "resFollow", 1.0f }, { "resDecay", 5.0f }, { "resBright", 75.0f }, { "resWidth", 70.0f },
            { "resRipple", 3.0f }, { "resDepth", 70.0f }, { "resSweep", 50.0f }, { "resTop", 20.0f },
            { "spaceReverb", 35.0f }, { "spaceDecay", 8.0f },
            { "m1Rate", 0.07f } },
          { "1:resSweep:0.5" } },

        // The series squeezed out of true and hit: warped metal, still on the note you play.
        { "Bent Plate",
          { { "size", 45.0f }, { "rate", 9.0f }, { "spray", 10.0f }, { "jitter", 50.0f }, { "release", 1200.0f },
            { "resMix", 80.0f }, { "resNotes", 1.0f }, { "resFollow", 1.0f }, { "resDecay", 2.5f }, { "resBright", 65.0f }, { "resDamp", 25.0f },
            { "resStretch", -25.0f }, { "resCurve", 30.0f }, { "resStrike", 55.0f }, { "resHardness", 55.0f }, { "resSaturate", 40.0f },
            { "colorDrive", 20.0f }, { "spaceReverb", 20.0f }, { "spaceDecay", 3.0f } },
          {} },

        // Long grains with noise crackling along their level, each crackle ringing the strings.
        { "Ember Reed",
          { { "size", 500.0f }, { "rate", 12.0f }, { "spray", 15.0f }, { "attack", 300.0f }, { "release", 2000.0f },
            { "resMix", 70.0f }, { "resNotes", 1.0f }, { "resFollow", 1.0f }, { "resDecay", 3.0f }, { "resBright", 55.0f }, { "resWidth", 60.0f },
            { "resParticles", 50.0f }, { "resDensity", 60.0f }, { "resTop", 20.0f }, { "resOddEven", -50.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 4.0f } },
          {} },

        // Echoes on the dotted eighth that dip while you play and swell after.
        { "Ducked Echoes",
          { { "rateMode", 1.0f }, { "rateDiv", 6.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 3.0f }, { "spray", 20.0f }, { "release", 600.0f },
            { "spaceDelay", 55.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 60.0f }, { "spaceDuck", 2.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 4.0f }, { "spaceTone", 40.0f } },
          {} },

        // Per-voice SPREAD: each note of a chord rides its own place on the wave.
        { "Spread Chorus",
          { { "size", 160.0f }, { "rate", 22.0f }, { "spray", 10.0f }, { "randPan", 40.0f }, { "attack", 300.0f }, { "release", 1500.0f },
            { "m1Rate", 0.4f }, { "m1Voice", 1.0f }, { "m1Spread", 80.0f },
            { "colorWow", 15.0f }, { "spaceReverb", 35.0f }, { "spaceDecay", 5.0f } },
          { "1:pitch:0.004", "1:start:0.15", "1:randPan:0.3" } },

        // Four playheads stepping a minor-pentatonic pattern.
        { "Head Patterns",
          { { "heads", 4.0f }, { "headSpread", 60.0f }, { "patternDepth", 12.0f }, { "patternSteps", 8.0f },
            { "scale", 9.0f }, { "key", 4.0f }, { "rateMode", 1.0f }, { "rateDiv", 3.0f }, { "size", 140.0f }, { "release", 900.0f },
            { "spaceDelay", 30.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 6.0f }, { "spaceReverb", 25.0f } },
          {} },

        { "Moth Light",
          { { "size", 420.0f }, { "rate", 18.0f }, { "spray", 45.0f }, { "randPan", 80.0f }, { "randPitch", 0.08f }, { "attack", 600.0f }, { "release", 3000.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 2600.0f }, { "filterReso", 15.0f }, { "spaceReverb", 50.0f }, { "spaceDecay", 9.0f }, { "spaceSize", 90.0f }, { "spaceTone", 35.0f },
            { "m1Rate", 0.08f } },
          { "1:filterCutoff:0.18" } },

        { "Salt Flats",
          { { "size", 1400.0f }, { "rate", 40.0f }, { "spray", 60.0f }, { "randPan", 60.0f }, { "attack", 1500.0f }, { "release", 5000.0f },
            { "filterMode", 1.0f }, { "filterCutoff", 900.0f }, { "colorWow", 12.0f }, { "spaceReverb", 45.0f }, { "spaceDecay", 14.0f }, { "spaceSize", 100.0f },
            { "m1Rate", 0.03f } },
          { "1:filterCutoff:0.25" } },

        { "Old Cassette",
          { { "size", 240.0f }, { "rate", 14.0f }, { "spray", 8.0f }, { "colorWow", 60.0f }, { "colorDrive", 35.0f }, { "colorTone", -45.0f }, { "colorNoise", 30.0f }, { "colorBits", 12.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 5200.0f }, { "spaceReverb", 15.0f }, { "spaceDecay", 1.5f } },
          {} },

        { "Slow Tide",
          { { "size", 800.0f }, { "rate", 9.0f }, { "spray", 25.0f }, { "randPan", 45.0f }, { "attack", 1200.0f }, { "release", 4000.0f }, { "spaceDelay", 25.0f }, { "spaceTime", 900.0f }, { "spaceFeedback", 55.0f },
            { "spaceReverb", 40.0f }, { "spaceDecay", 8.0f }, { "filterMode", 0.0f }, { "filterCutoff", 3000.0f }, { "m1Rate", 0.06f } },
          { "1:filterCutoff:0.3", "1:spray:0.15" } },

        { "Hollow Bell",
          { { "size", 70.0f }, { "rate", 12.0f }, { "spray", 6.0f }, { "release", 1600.0f }, { "resMix", 70.0f }, { "resNotes", 2.0f }, { "resDecay", 3.5f }, { "resOddEven", -70.0f }, { "resReso", 80.0f }, { "resDamp", 25.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 5.0f } },
          {} },

        { "Rain on Tin",
          { { "size", 22.0f }, { "rate", 80.0f }, { "spray", 70.0f }, { "jitter", 100.0f }, { "randAmp", 80.0f }, { "randPan", 100.0f }, { "fade", 40.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 7000.0f }, { "filterMorph", 0.5f }, { "colorBits", 11.0f }, { "spaceReverb", 20.0f }, { "spaceDecay", 2.0f } },
          {} },

        { "Night Bus",
          { { "rateMode", 1.0f }, { "rateDiv", 6.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 6.0f }, { "spray", 12.0f }, { "fade", 55.0f }, { "colorDrive", 25.0f }, { "colorTone", -20.0f },
            { "filterMode", 1.0f }, { "filterCutoff", 1400.0f }, { "filterReso", 35.0f }, { "spaceDelay", 35.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 45.0f },
            { "m1Sync", 1.0f }, { "m1Div", 14.0f } },
          { "1:filterCutoff:0.3" } },

        { "Back to Front",
          { { "size", 340.0f }, { "rate", 12.0f }, { "reverse", 85.0f }, { "spray", 18.0f }, { "slant", 80.0f }, { "randPan", 35.0f }, { "spaceReverb", 35.0f }, { "spaceDecay", 4.0f }, { "spaceTone", 45.0f } },
          {} },

        { "Warm Ladder",
          { { "size", 120.0f }, { "rate", 26.0f }, { "spray", 6.0f }, { "filterMode", 1.0f }, { "filterCutoff", 650.0f }, { "filterReso", 62.0f }, { "filterDrive", 40.0f }, { "filterEnv", 45.0f }, { "filterKey", 50.0f },
            { "release", 700.0f }, { "spaceReverb", 15.0f }, { "spaceDecay", 2.0f }, { "m1Rate", 0.2f } },
          { "1:filterCutoff:0.12" } },

        { "Breath Held",
          { { "size", 2000.0f }, { "rate", 30.0f }, { "spray", 15.0f }, { "fade", 100.0f }, { "attack", 2500.0f }, { "release", 6000.0f }, { "filterMode", 0.0f }, { "filterCutoff", 1800.0f }, { "spaceReverb", 35.0f }, { "spaceDecay", 10.0f } },
          {} },

        { "Paper Choir",
          { { "rateMode", 2.0f }, { "grains", 18.0f }, { "grainSize", 200.0f }, { "randPitch", 0.2f }, { "randPan", 75.0f }, { "spray", 12.0f }, { "attack", 500.0f }, { "release", 2200.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 4200.0f }, { "filterMorph", 0.35f }, { "spaceReverb", 40.0f }, { "spaceDecay", 6.0f }, { "spaceShimmer", 15.0f } },
          {} },

        { "Dust Engine",
          { { "rateMode", 1.0f }, { "rateDiv", 3.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 1.0f }, { "randAmp", 60.0f }, { "reverse", 20.0f }, { "colorBits", 8.0f }, { "colorRate", 9000.0f }, { "colorDrive", 45.0f }, { "colorComp", 40.0f },
            { "filterMode", 0.0f }, { "filterCutoff", 6000.0f } },
          {} },

        // MIX in the room: the track stays present, and the reverb takes both.
        { "Far Room",
          { { "mix", 55.0f }, { "size", 300.0f }, { "rate", 14.0f }, { "spray", 35.0f }, { "randPan", 60.0f }, { "spaceReverb", 55.0f }, { "spaceDecay", 7.0f }, { "spaceSize", 90.0f }, { "spaceTone", 40.0f } },
          {} },

        { "Broken Clock",
          { { "size", 95.0f }, { "rate", 7.0f }, { "jitter", 100.0f }, { "reverse", 30.0f }, { "randAmp", 50.0f }, { "randPitch", 0.0f }, { "spaceDelay", 40.0f }, { "spaceTime", 310.0f }, { "spaceFeedback", 60.0f }, { "colorWow", 20.0f } },
          {} },

        { "Under Ice",
          { { "size", 700.0f }, { "rate", 22.0f }, { "spray", 40.0f }, { "pitch", -12.0f }, { "filterMode", 1.0f }, { "filterCutoff", 420.0f }, { "filterReso", 25.0f }, { "spaceReverb", 45.0f }, { "spaceDecay", 11.0f }, { "spaceTone", 15.0f }, { "m1Rate", 0.05f } },
          { "1:filterCutoff:0.2" } },

        // Every grain its own length and pitch, mostly close to what's set.
        { "Murmur",
          { { "size", 240.0f }, { "rate", 28.0f }, { "spray", 8.0f }, { "randPan", 60.0f }, { "fade", 85.0f },
            { "scale", 3.0f }, { "attack", 200.0f }, { "release", 1200.0f },
            { "spaceReverb", 35.0f }, { "spaceDecay", 5.0f },
            { "m1Type", 6.0f }, { "m1GrainSpread", 1.0f } },
          { "1:size:0.35", "1:pitch:0.1" } },

        // Only the extremes: tiny ticks and long smears, forwards and back.
        { "Hard Edges",
          { { "size", 150.0f }, { "rate", 14.0f }, { "randPan", 80.0f }, { "fade", 40.0f }, { "colorDrive", 20.0f },
            { "m1Type", 6.0f }, { "m1GrainSpread", 2.0f } },
          { "1:size:0.5", "1:reverse:0.5", "1:rate:0.2" } },

        // There and back through the sample, never meeting the seam.
        { "Tide Line",
          { { "endMode", 3.0f }, { "scan", 0.25f }, { "size", 400.0f }, { "rate", 22.0f }, { "spray", 5.0f }, { "randPan", 50.0f },
            { "attack", 300.0f }, { "release", 1800.0f }, { "spaceReverb", 40.0f }, { "spaceDecay", 7.0f }, { "spaceSize", 80.0f } },
          {}, true },

        // The whole sample walked across four bars, whatever the tempo.
        { "Four Bar Walk",
          { { "scanSync", 1.0f }, { "scanDiv", 4.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 6.0f }, { "rateMode", 1.0f }, { "rateDiv", 3.0f },
            { "fade", 60.0f }, { "spaceDelay", 25.0f }, { "spaceSync", 1.0f }, { "spaceFeedback", 35.0f } },
          {}, true },

        // One moment held still, the grains wandering round it.
        { "Held Still",
          { { "freeze", 1.0f }, { "start", 35.0f }, { "size", 500.0f }, { "rate", 18.0f }, { "spray", 6.0f }, { "randPan", 70.0f }, { "fade", 90.0f },
            { "attack", 800.0f }, { "release", 2500.0f }, { "spaceReverb", 45.0f }, { "spaceDecay", 9.0f },
            { "m1Type", 6.0f }, { "m1GrainSpread", 1.0f } },
          { "1:spray:0.15", "1:slant:0.3" }, true },

        // A filter that opens late, on a bent envelope.
        { "Late Bloom",
          { { "size", 300.0f }, { "rate", 20.0f }, { "spray", 10.0f }, { "filterMode", 1.0f }, { "filterCutoff", 500.0f }, { "filterReso", 25.0f },
            { "attack", 40.0f }, { "release", 1000.0f },
            { "m1Type", 2.0f }, { "m1Attack", 2500.0f }, { "m1Decay", 1500.0f }, { "m1Sustain", 60.0f }, { "m1Release", 1500.0f }, { "m1Curve", 60.0f } },
          { "1:filterCutoff:0.45" } },
    };
    return list;
}

// Each factory preset's category in the browser.
juce::String categoryOfFactory (const juce::String& name)
{
    static const std::map<juce::String, const char*> categoryOf {
        { "Murmur", "TONAL" }, { "Hard Edges", "RHYTHMIC" }, { "Tide Line", "PADS" }, { "Four Bar Walk", "RHYTHMIC" },
        { "Held Still", "DRONES" }, { "Late Bloom", "PADS" },
        { "Init", "TONAL" },          { "Glass Cloud", "PADS" },     { "Tape Choir", "TEXTURES" },  { "Rust Rhythm", "RHYTHMIC" },
        { "Bell Garden", "TONAL" },   { "Wander", "TEXTURES" },      { "Dark Drone", "DRONES" },    { "Shimmer Pad", "PADS" },
        { "Hit Chop", "SLICES" },     { "Stutter Grid", "SLICES" },  { "Feed Bloom", "TEXTURES" },  { "Sample Strings", "TONAL" },
        { "Moth Light", "PADS" },     { "Salt Flats", "DRONES" },    { "Old Cassette", "TEXTURES" }, { "Slow Tide", "PADS" },
        { "Hollow Bell", "TONAL" },   { "Rain on Tin", "TEXTURES" }, { "Night Bus", "RHYTHMIC" },   { "Back to Front", "TEXTURES" },
        { "Warm Ladder", "TONAL" },   { "Breath Held", "DRONES" },   { "Paper Choir", "PADS" },     { "Dust Engine", "RHYTHMIC" },
        { "Far Room", "PADS" },       { "Broken Clock", "RHYTHMIC" }, { "Under Ice", "DRONES" },
        { "Grit Bells", "TONAL" },    { "Ducked Echoes", "RHYTHMIC" }, { "Spread Chorus", "PADS" }, { "Head Patterns", "RHYTHMIC" },
    };
    const auto it = categoryOf.find (name);
    return it != categoryOf.end() ? it->second : "TONAL";
}

// This build's presets: Silt FX leaves out the ones played from notes.
const std::vector<Factory>& factories()
{
    static const auto list = []
    {
        std::vector<Factory> mine;
        for (const auto& f : allFactories())
            if (SiltProcessor::isInstrument || ! f.instrumentOnly)
                mine.push_back (f);
        return mine;
    }();
    return list;
}
} // namespace

PresetManager::PresetManager (SiltProcessor& p, juce::File f) : kit::PresetLibrary (std::move (f), "SiltPreset", siltPresetExtension), silt (p) {}

const juce::StringArray& PresetManager::categories()
{
    static const juce::StringArray list { "PADS", "TEXTURES", "DRONES", "TONAL", "RHYTHMIC", "SLICES", "USER" };
    return list;
}

juce::File PresetManager::defaultFolder()
{
    const auto data = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    const auto folder = data.getChildFile ("Audio/Presets/Glyph Audio").getChildFile (JucePlugin_Name);
    // Presets from before the range had a name.
    if (const auto old = data.getChildFile ("Audio/Presets/Silt Audio").getChildFile (JucePlugin_Name); old.isDirectory() && ! folder.exists())
    {
        folder.getParentDirectory().createDirectory();
        old.moveFileTo (folder);
    }
    return folder;
#else
    return data.getChildFile ("Glyph Audio").getChildFile (JucePlugin_Name).getChildFile ("Presets");
#endif
}

int PresetManager::numFactoryPresets() { return (int) factories().size(); }

juce::String PresetManager::factoryName (int index) const { return factories()[(size_t) index].name; }
juce::String PresetManager::factoryCategory (int index) const { return categoryOfFactory (factories()[(size_t) index].name); }

std::unique_ptr<juce::XmlElement> PresetManager::factoryPreset (int index)
{
    const auto& f = factories()[(size_t) juce::jlimit (0, numFactoryPresets() - 1, index)];
    auto xml = std::make_unique<juce::XmlElement> ("SiltPreset");
    xml->setAttribute ("name", f.name);
    xml->setAttribute ("category", categoryOfFactory (f.name));
    auto* values = xml->createNewChildElement ("PARAMS");
    for (const auto& [id, value] : f.values)
        values->setAttribute (id, value);
    juce::StringArray routes;
    for (const auto* route : f.routes)
    {
        // Factory routes number modulators 1–4; the state format uses 0–3.
        const auto parts = juce::StringArray::fromTokens (route, ":", "");
        routes.add (juce::String (parts[0].getIntValue() - 1) + ":" + parts[1] + ":" + parts[2]);
    }
    xml->setAttribute ("routes", routes.joinIntoString (";"));
    return xml;
}
