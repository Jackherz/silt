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

    // Page 3: reshaping the series. All at 0 leaves the notes as NOTES set them.
    float stretch = 0.0f;           // −1…+1: squeezes the top of the bank down (metallic) or pulls it up (sparse), up to 3 octaves
    float stretchCurve = 0.0f;      // −1…+1: STRETCH acts more on the low modes, or only the high ones
    float snap = 0.0f;              // pulls every mode towards a whole-number ratio of the root
    float offset = 0.0f;            // semitones, ±12: every other mode, on top of it all
    float ripple = 2.0f;            // how many peaks a highlight across the bank has
    float rippleDepth = 0.0f;       // 0 no highlight … 1 only the very peaks ring
    float sweep = 0.0f;             // 0…1: where the peaks sit; moved, they scan through the modes

    // Page 4: what sets it ringing, beyond the sound coming in.
    float strike = 0.0f;            // how hard each new grain hits the bank directly
    float hardness = 0.6f;          // 0: a hit rings only the low modes … 1: all of them
    float particles = 0.0f;         // noise crackle following the input's level
    float particleDensity = 200.0f; // crackles a second
    float top = 0.0f;               // lifts the top of what goes in, so a dull sound rings the high modes
    float saturate = 0.0f;          // loud modes hold back and spill into the others; quiet ones ring as they were

    // SAMPLE: the sample's modes (Hz, low to high), already moved by TUNE or the note
    // FOLLOW plays, and how strong each is (0: not known, rings like the rest). RANGE
    // keeps those within its octaves of the lowest.
    std::array<float, 128> sampleHz {};
    std::array<float, 128> sampleLevel {};
    int sampleCount = 0;

    // FOLLOW with a chord held: each held note above the lowest (rootNote), in
    // semitones over it. BANDS is shared out evenly, so every note keeps its own
    // fundamental; RANGE and page 3 act on each note from its own fundamental.
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
    // strikes: grains starting in this block, in order of offset.
    void process (float* left, float* right, int numSamples, const ResonatorParams&,
                  const GrainStrike* strikes = nullptr, int numStrikes = 0) noexcept;

    int getNumBands() const noexcept { return numBands; }
    double getBandFrequency (int band) const noexcept { return bands[(size_t) band].frequency; }

    // One mode of the bank, as ratios to the root (where it sits) and to its own note
    // (how loud and how long it rings, so a chord's upper notes aren't quieter for being higher).
    struct BankMode
    {
        double ratio = 1.0, own = 1.0;
        int note = 0;      // which of the chord's notes, lowest first
        int partial = 0;   // its place in that note's series
        double weight = 1.0;   // SAMPLE: how strong it is in the sample
    };

    // The bank's modes, ascending. Returns how many.
    static int bankModes (const ResonatorParams&, std::array<BankMode, maxBands>& modes) noexcept;

    // The bands' frequencies as ratios to the root, ascending. Returns how many.
    static int bandRatios (const ResonatorParams&, std::array<double, maxBands>& ratios) noexcept;

    // A mode at this ratio to the root: how loud, and how much of DECAY it rings for.
    static double levelAt (double ratio, double bright) noexcept;
    static double ringAt (double ratio, double bright, double damp) noexcept;
    // RIPPLE's share of that level, for a mode this far up a bank reaching `top`.
    static double rippleAt (double ratio, double top, const ResonatorParams&) noexcept;

private:
    struct Band
    {
        double frequency = 0.0;                // 0: not sounding
        double nominal = 0.0;                  // where it's meant to be, before SPREAD and DRIFT
        double driftFrom = 1.0, driftTo = 1.0, driftPhase = 1.0;   // DRIFT: wandering between random targets
    };

    double nextRandom() noexcept;   // 0…1

    void updateCoefficients (const ResonatorParams&, bool snap) noexcept;
    void clearBand (int band) noexcept;
    void keepSharedNotes (const std::array<BankMode, maxBands>&, int count, double rootHz) noexcept;
    void strikeBank (const GrainStrike&, float amount) noexcept;

    static constexpr int controlInterval = 32;
    double sampleRate = 44100.0;
    std::array<Band, maxBands> bands {};

    // Per mode, the audio loop's own: state, the turn each sample, gains now and
    // at the end of this block. Four at a time, so the compiler can vectorise.
    alignas (32) std::array<float, maxBands> re {}, im {}, turnCos {}, turnSin {};
    alignas (32) std::array<float, maxBands> gainLeft {}, gainRight {}, nextLeft {}, nextRight {};
    std::array<float, maxBands> strikeShare {}, modeRatio {};   // what a hit gives each mode, before where it lands

    // Which slot each of the bank's modes rings in. Across a chord change a mode the
    // new chord still has stays where it is, undisturbed.
    std::array<short, maxBands> slotOf {};
    float slottedRoot = 0.0f;
    std::array<float, 8> slottedChord {};
    int slottedChordSize = 0;

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
    double previousIn = 0.0;            // TOP: the last sample in
    double previousWet = 0.0;           // SATURATE: the last sample out
    bool active = false;   // false: modes are silent and the input passes through
    unsigned randomState = 0x5117u;
};

} // namespace silt
