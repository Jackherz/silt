#pragma once

#include <silt/Dsp.h>

#include <array>

namespace silt
{

// Where the bands sit: on the PITCH page's scale and key, on the harmonic
// series, or on inharmonic bell partials.
enum class ResonatorNotes { scale, harmonics, bell };

struct ResonatorParams
{
    float rootNote = 48.0f;         // MIDI note of the lowest band; fractions detune (not in SCALE)
    ResonatorNotes notes = ResonatorNotes::scale;
    Scale scale = Scale::off;       // SCALE notes; off rings the root and fifths
    int key = 0;
    float rangeOctaves = 2.0f;      // how far above the root the bands reach, 1…4
    float decaySeconds = 1.5f;      // ring time of the lowest band (to −60 dB)
    float bright = 0.5f;            // 0: high bands quieter and shorter; 1: all equal
    float width = 0.5f;             // bands alternate left and right by this much
    float mix = 0.0f;               // 0 passes the input through untouched
};

// A bank of up to 12 ringing band-pass filters (TPT state-variable, in double
// so long rings at low notes stay clean). The input's mono sum excites every
// band; each band rings at its note for the decay time. The rings are kept
// no louder than the input, so a tone on a band can't run away.
//
// Audio thread only, after prepare(). Coefficients follow the parameters every
// 32 samples, with the band frequencies gliding, so TUNE can be swept.
class Resonator
{
public:
    static constexpr int maxBands = 12;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // In place; left and right may be the same pointer (mono).
    void process (float* left, float* right, int numSamples, const ResonatorParams&) noexcept;

    int getNumBands() const noexcept { return numBands; }
    double getBandFrequency (int band) const noexcept { return bands[(size_t) band].frequency; }

    // The bands' frequencies as ratios to the root, ascending. Returns how many.
    static int bandRatios (const ResonatorParams&, std::array<double, maxBands>& ratios) noexcept;

private:
    struct Band
    {
        double s1 = 0.0, s2 = 0.0;             // integrator states
        double a1 = 0.0, a2 = 0.0, a3 = 0.0, k = 1.0;
        double frequency = 0.0;
        float gainLeft = 0.0f, gainRight = 0.0f;
    };

    void updateCoefficients (const ResonatorParams&, bool snap) noexcept;

    static constexpr int controlInterval = 32;
    double sampleRate = 44100.0;
    std::array<Band, maxBands> bands {};
    int numBands = 0;
    float mixNow = 0.0f;
    double inMean = 0.0, inPower = 0.0, wetPower = 0.0, ringGain = 1.0;   // level match
    bool active = false;   // false: bands are silent and the input passes through
};

} // namespace silt
