#pragma once

#include <silt/Dsp.h>

namespace silt
{

enum class ModType { wave, random, envelope, follow, midi };
enum class MidiSource { velocity, note, modWheel, aftertouch, pitchBend };

struct ModulatorParams
{
    ModType type = ModType::wave;

    // WAVE and RANDOM
    float rateHz = 1.0f;
    bool sync = false;
    double syncQuarters = 1.0;   // one cycle (WAVE) or step (RANDOM), in quarter notes
    // WAVE
    float shape = 0.0f;          // 0 sine, 1 triangle, 2 saw, 3 square, blended between
    float phase = 0.0f;          // 0…1: where a retriggered or synced cycle starts
    bool retrigger = false;      // restart on each note
    // RANDOM
    float smooth = 1.0f;         // 0 stepped … 1 gliding
    float jitter = 0.0f;         // 0…1: how unevenly the steps are spaced

    // ENVELOPE (from the notes played)
    float attackMs = 10.0f, decayMs = 300.0f, sustain = 0.5f, releaseMs = 500.0f;
    bool repeat = false;         // while held, loop attack and decay

    // FOLLOW (the input's level)
    float followAttackMs = 10.0f, followReleaseMs = 200.0f;
    float gainDb = 0.0f;         // added to the level before it's read

    // MIDI
    MidiSource source = MidiSource::velocity;
};

// What the modulators can see of a block: the host's clock, the notes and
// controllers, and the input's level.
struct ModContext
{
    int numSamples = 0;
    double sampleRate = 48000.0;
    bool hostPlaying = false;
    double ppqEnd = 0.0;          // the host position at the block's end, when playing
    double bpm = 120.0;

    bool noteStarted = false;     // a note-on in this block
    int heldNotes = 0;
    float velocity = 0.0f;        // the last note-on's, 0…1
    int note = 60;                // the last note-on's
    float modWheel = 0.0f, aftertouch = 0.0f;   // 0…1
    float pitchBend = 0.0f;       // −1…1

    float inputPeak = 0.0f;       // linear, over the block
};

// One modulation source. advance() steps it over a block and returns its value
// at the block's end: −1…1 for bipolar sources (WAVE, RANDOM, note, pitch
// bend), 0…1 for the rest. Plain state, no allocation: audio thread.
class Modulator
{
public:
    static bool isBipolar (const ModulatorParams&) noexcept;

    void reset (std::uint64_t seed = 1) noexcept;
    float advance (const ModulatorParams&, const ModContext&) noexcept;

private:
    float wave (const ModulatorParams&, const ModContext&) noexcept;
    float randomStep (const ModulatorParams&, const ModContext&) noexcept;
    float envelope (const ModulatorParams&, const ModContext&) noexcept;
    float follow (const ModulatorParams&, const ModContext&) noexcept;

    double cycleRate (const ModulatorParams&, const ModContext&) const noexcept;   // cycles per second

    double phase = 0.0;
    // RANDOM
    double stepProgress = 1.0, stepLength = 1.0;
    long long lastStep = -1;      // synced: the host step last seen
    float from = 0.0f, to = 0.0f;
    // ENVELOPE
    enum class Stage { idle, attack, decay, sustain, release } stage = Stage::idle;
    float level = 0.0f;
    // FOLLOW
    float followed = 0.0f;
    Random random;
};

} // namespace silt
