#pragma once

#include <silt/Dsp.h>

#include <array>
#include <cstddef>

namespace silt
{

// Where the bands sit: on the PITCH page's scale and key, on the harmonic
// series, on inharmonic bell partials, or on the sample's own strongest pitches.
enum class ResonatorNotes { scale, harmonics, bell, sample };

struct ResonatorParams
{
    float rootNote = 48.0f;         // MIDI note of the lowest band; fractions detune (not in SCALE)
    ResonatorNotes notes = ResonatorNotes::scale;
    Scale scale = Scale::off;       // SCALE notes; off rings the root and fifths
    int key = 0;
    float rangeOctaves = 8.0f;      // how far above the root the bands reach, 1…8
    float decaySeconds = 1.5f;      // ring time of the lowest band (to −60 dB)
    float bright = 0.5f;            // 0: high bands quieter and shorter; 1: all equal
    float width = 0.5f;             // bands alternate left and right by this much
    float mix = 0.0f;               // 0 passes the input through untouched

    // Page 2.
    float resonance = 0.5f;         // how narrow the bands are while sound comes in (Q 8…120; 0.5 ≈ 31)
    int bandCount = 128;            // how many bands, 1…maxBands (an even pick across the range)
    float drift = 0.0f;             // each band's level (and a little of its tuning) wanders on its own
    float driftRate = 0.5f;         // Hz: how often each band picks a new place to wander to
    float spread = 0.0f;            // detunes the bands from their notes, each its own way (up to ±35 cents)
    float oddEven = 0.0f;           // −1: only the 1st, 3rd, 5th… bands; +1: only the 2nd, 4th…
    float drive = 0.0f;             // saturates the sound going in, so it excites more of the bank
    float damp = 0.0f;              // high bands ring shorter (on top of BRIGHT)

    // SAMPLE: the sample's pitches (Hz, low to high), already moved by TUNE
    // or the note FOLLOW plays; RANGE keeps those within its octaves of the lowest.
    std::array<float, 12> sampleHz {};
    int sampleCount = 0;

    // FOLLOW with a chord held: each held note above the lowest (rootNote), in
    // semitones over it. Every note gets its own set of bands, shared out.
    std::array<float, 8> chord {};
    int chordSize = 0;
};

// A modal bank: up to 128 ringing modes, each a complex one-pole resonator
// (a point turning and shrinking a little every sample). A hit rings a mode just
// as hard at any decay, and retuning one mid-ring can't click or blow up. The
// input's mono sum excites every mode. While sound comes in short decays stay
// narrow (pitched); when it stops they ring out for the decay time. The rings are
// kept in line with the input's peaks: a tone on a mode can't run away, and a
// short hit isn't crushed for being quiet on average.
//
// Audio thread only, after prepare(). Coefficients follow the parameters every
// 32 samples and aren't recalculated while nothing moves.
class Resonator
{
public:
    static constexpr int maxBands = 128;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // In place; left and right may be the same pointer (mono).
    void process (float* left, float* right, int numSamples, const ResonatorParams&) noexcept;

    int getNumBands() const noexcept { return numBands; }
    double getBandFrequency (int band) const noexcept { return bands[(size_t) band].frequency; }

    // The bands' frequencies as ratios to the root, ascending. Returns how many.
    static int bandRatios (const ResonatorParams&, std::array<double, maxBands>& ratios) noexcept;

    // A mode at this ratio to the root: how loud, and how much of DECAY it rings for.
    static double levelAt (double ratio, double bright) noexcept;
    static double ringAt (double ratio, double bright, double damp) noexcept;

private:
    struct Band
    {
        double frequency = 0.0;                // 0: not sounding
        double driftFrom = 1.0, driftTo = 1.0, driftPhase = 1.0;   // DRIFT: wandering between random targets
    };

    double nextRandom() noexcept;   // 0…1

    void updateCoefficients (const ResonatorParams&, bool snap) noexcept;
    void clearBand (int band) noexcept;

    static constexpr int controlInterval = 32;
    double sampleRate = 44100.0;
    std::array<Band, maxBands> bands {};

    // Per mode, the audio loop's own: state, the turn each sample, gains now and
    // at the end of this block. Four at a time, so the compiler can vectorise.
    alignas (32) std::array<float, maxBands> re {}, im {}, turnCos {}, turnSin {};
    alignas (32) std::array<float, maxBands> gainLeft {}, gainRight {}, nextLeft {}, nextRight {};

    int numBands = 0;
    float mixNow = 0.0f;
    ResonatorParams smoothed;   // the knobs, gliding
    ResonatorParams lastTarget; // what they were last asked to be
    bool bankSilent = false;    // every mode has rung out
    bool settled = false;       // nothing's moving: the coefficients can stay as they are
    bool chokeMatters = true;   // some mode's width depends on the choke
    double chokeUsed = -1.0;
    double inMean = 0.0, inPeak = 0.0, inPower = 0.0, wetPower = 0.0, ringGain = 1.0;   // level match
    double inSlow = 0.0, choke = 0.0;   // choke: 1 once the input has dropped away
    bool active = false;   // false: modes are silent and the input passes through
    unsigned randomState = 0x5117u;
};

} // namespace silt
