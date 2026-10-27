#pragma once

#include <juce_core/juce_core.h>

#include <map>

// Hover help: a line for the LCD about whatever the mouse is over. Controls
// are found by their parameter id (a modulator's settings by the part after
// "m1"…"m4"); the panel's own parts by a name of their own ("!load"…).
// Short enough for the LCD: a few words on what it does, not how it works.
namespace silt::ui
{
inline juce::String helpFor (const juce::String& id)
{
    static const std::map<juce::String, const char*> help {
        // GRAIN
        { "pitch", "PITCH: tunes every grain up or down" },
        { "size", "SIZE: how long each grain lasts" },
        { "sizeDiv", "SIZE: each grain a note value long" },
        { "grainSize", "SIZE: how long each grain lasts" },
        { "rate", "RATE: how many grains start each second" },
        { "rateDiv", "RATE: a grain on every note value" },
        { "grains", "GRAINS: how many sound at once" },
        { "spray", "SPRAY: scatters grains round the playhead" },
        { "jitter", "JITTER: loosens when grains start" },
        { "reverse", "REVERSE: the chance a grain plays backwards" },
        { "randAmp", "RND AMP: grains vary in loudness" },
        { "randPan", "RND PAN: grains spread across the stereo" },
        { "sizeSync", "SYNC: SIZE in note values, to the tempo" },
        { "rateMode", "RATE MODE: free, synced, or a grain count" },
        // SHAPE
        { "fade", "FADE: how soft each grain's edges are" },
        { "slant", "SLANT: a grain swells early or late" },
        { "ease", "EASE: the curve of a grain's fades" },
        { "attack", "ATTACK: how a note fades in" },
        { "decay", "DECAY: how fast a note falls to SUSTAIN" },
        { "sustain", "SUSTAIN: the level a held note stays at" },
        { "release", "RELEASE: how a note fades after you let go" },
        // PITCH
        { "randPitch", "RND PITCH: grains drift off pitch" },
        { "scale", "SCALE: grain pitches snap to a scale" },
        { "key", "KEY: the scale's home note" },
        { "patternDepth", "PATTERN: a stepped melody through grains" },
        { "patternSteps", "STEPS: how long the pattern is" },
        { "heads", "HEADS: more playheads through the sample" },
        { "headSpread", "SPREAD: how far apart the playheads sit" },
        { "!newPattern", "NEW: a fresh random pattern" },
        // PLAY
        { "keyMode", "MODE: pitched, live input, FEED or slices" },
        { "sliceCount", "SLICES: the region cut into this many keys" },
        { "sliceMode", "SLICE BY: equal, at hits, or drag your own" },
        { "start", "START: where in the sample notes begin" },
        { "length", "LENGTH: how much of the sample plays" },
        { "scan", "SCAN: how fast the playhead moves through" },
        { "endMode", "AT THE END: loop, hold or stop" },
        { "snap", "SNAP: the region to zero crossings or hits" },
        { "liveDelay", "DELAY: how far back the grains read" },
        { "delayDiv", "DELAY: how far back, in note values" },
        { "delaySync", "SYNC: DELAY in note values" },
        { "feedback", "FEEDBACK: grains fed back in (100% holds)" },
        { "freeze", "FREEZE: stop writing; play what's held" },
        { "liveRun", "NO NOTES: what plays with no keys held" },
        { "root", "ROOT: the note the sample plays as recorded" },
        { "gain", "OUTPUT: Silt's overall level" },
        { "mix", "MIX: dry input against Silt" },
        // RESONATOR
        { "resTune", "TUNE: the lowest band's note" },
        { "resNotes", "NOTES: which pitches the bands ring at" },
        { "resRange", "RANGE: how high the bands reach" },
        { "resDecay", "DECAY: how long the bands ring" },
        { "resBright", "BRIGHT: high bands as loud as low" },
        { "resWidth", "WIDTH: bands spread left and right" },
        { "resFollow", "FOLLOW: bands tune to the notes you play" },
        { "resMix", "MIX: how much of the bands you hear" },
        { "resReso", "RESONANCE: how pitched the bands sound" },
        { "resBands", "BANDS: how many ring" },
        { "resDrift", "DRIFT: each band wanders on its own" },
        { "resDriftRate", "DRIFT RATE: how fast they wander" },
        { "resSpread", "SPREAD: detunes the bands apart" },
        { "resOddEven", "ODD/EVEN: favours alternate bands" },
        { "resDrive", "DRIVE: grit going in, so more bands ring" },
        { "resDamp", "DAMP: high bands die sooner" },
        // FILTER
        { "filterMode", "MODE: off, or a 12 or 24 dB filter" },
        { "filterCutoff", "CUTOFF: where the filter acts" },
        { "filterReso", "RESO: a peak at the cutoff" },
        { "filterMorph", "MORPH: low-pass through band to high-pass" },
        { "filterDrive", "DRIVE: warmth into the filter" },
        { "filterEnv", "ENV: each note opens the filter" },
        { "filterKey", "KEY: higher notes open it further" },
        { "filterMix", "MIX: filtered against unfiltered" },
        // COLOR
        { "colorDrive", "DRIVE: saturation" },
        { "colorTone", "TONE: darker or brighter" },
        { "colorBits", "BITS: fewer bits, crunchier steps" },
        { "colorRate", "RATE: a lower sample rate, aliasing" },
        { "colorComp", "COMP: squeezes the level even" },
        { "colorNoise", "NOISE: hiss under the sound" },
        { "colorWow", "WOW: tape-like wobble in pitch" },
        { "colorMix", "MIX: coloured against clean" },
        // SPACE
        { "spaceTime", "TIME: the delay between echoes" },
        { "spaceDiv", "TIME: echoes on a note value" },
        { "spaceSync", "SYNC: TIME in note values" },
        { "spaceFeedback", "FEEDBACK: how many echoes" },
        { "spaceMode", "MODE: echoes ping-pong or stay put" },
        { "spaceDelay", "DELAY: how loud the echoes are" },
        { "spaceSize", "SIZE: a small room to a hall" },
        { "spaceDecay", "DECAY: how long the reverb lasts" },
        { "spaceFreeze", "FREEZE: hold the reverb tail" },
        { "spaceTone", "TONE: a darker or brighter tail" },
        { "spaceShimmer", "SHIMMER: an octave-up glow in the tail" },
        { "spaceReverb", "REVERB: how loud the reverb is" },
        { "spaceDuck", "DUCK: echoes dip while you play" },
        // CHAIN
        { "chainOrder", "ORDER: the order the effects run in" },
        // MOD (any modulator: the part after m1…m4)
        { "mod:Type", "TYPE: wave, random, envelope, follow, MIDI" },
        { "mod:Shape", "SHAPE: the wave's form" },
        { "mod:Rate", "RATE: how fast it moves (below 0: reverse)" },
        { "mod:Div", "RATE: a cycle per note value" },
        { "mod:Sync", "SYNC: RATE in note values" },
        { "mod:Rev", "REV: synced, running backwards" },
        { "mod:Phase", "PHASE: where the cycle starts" },
        { "mod:Retrig", "RETRIG: each note restarts the cycle" },
        { "mod:Sym", "SYM: where the cycle turns round" },
        { "mod:Curve", "CURVE: bends the wave" },
        { "mod:Smooth", "SMOOTH: glides between random values" },
        { "mod:Jitter", "JITTER: loosens the random timing" },
        { "mod:Attack", "ATTACK: how it rises" },
        { "mod:Decay", "DECAY: how it falls to SUSTAIN" },
        { "mod:Sustain", "SUSTAIN: where it holds" },
        { "mod:Release", "RELEASE: how it falls after the note" },
        { "mod:Repeat", "LOOP: the envelope repeats" },
        { "mod:FAttack", "ATTACK: how fast it follows a rise" },
        { "mod:FRelease", "RELEASE: how fast it follows a fall" },
        { "mod:Gain", "GAIN: how strongly it follows" },
        { "mod:Source", "SOURCE: which MIDI it follows" },
        { "mod:Macro", "MACRO: one knob for every route" },
        { "mod:Enable", "ON: mutes it, routes kept" },
        { "mod:Voice", "VOICE: each note gets its own copy" },
        { "mod:Spread", "SPREAD: each note's wave starts further on" },
        { "!modSelect", "MODULATOR: the one shown (or knob 1)" },
        { "!assign", "ASSIGN: controller knobs set depths" },
        // The panel
        { "!arm", "ARM: record the input from the next bar" },
        { "!bars", "BARS: how long ARM records" },
        { "!resample", "RESAMPLE: ARM records Silt's own sound" },
        { "!load", "LOAD: a file or a built-in sample" },
        { "!learn", "LEARN: teach Silt your controller's knobs" },
        { "!random", "RANDOM: shakes up this page" },
        { "!randomAmount", "AMOUNT: how far RANDOM moves things" },
        { "!undo", "UNDO: a step back (knobs, routes, samples)" },
        { "!redo", "REDO: a step forward again" },
        { "!routes", "MOD ROUTES: every modulation connection" },
        { "!monitor", "MONITOR: hear the input as well" },
        { "!tour", "?: the tour, the theme, the grain view" },
        { "!screen", "SCREEN: drop a file; right-click for tools" },
        { "!zoom", "ZOOM: drag up/down; double-click shows it all" },
        { "!drag", "DRAG: into your DAW as an audio clip" },
        { "!gain", "GAIN: the sample's level (right-click: tools)" },
        { "!tab", "PAGES: eight controls each, for 8 knobs" },
        { "!chip", "MOD: click to arm, drag onto a knob" },
        { "!grip", "GRIP: drag to share space; double-click resets" },
        { "!preset", "PRESETS: step through, or SAVE your own" },
    };

    if (const auto it = help.find (id); it != help.end())
        return it->second;
    // A modulator's setting: m1Rate → mod:Rate.
    if (id.length() > 2 && id[0] == 'm' && juce::CharacterFunctions::isDigit (id[1]))
        if (const auto it = help.find ("mod:" + id.substring (2)); it != help.end())
            return it->second;
    return {};
}
} // namespace silt::ui
