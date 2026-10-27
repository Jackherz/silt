#include "ScreenInsets.h"
#include "ModColours.h"
#include "PageLayout.h"
#include "Palette.h"

#include <silt/Color.h>
#include <silt/Resonator.h>
#include "../core/src/Fft.h"

#include <complex>
#include <optional>

namespace silt::ui
{

namespace
{
namespace layout = silt::layout;
using Complex = std::complex<double>;

// A parameter's value as the sound has it now: modulated where it's a target.
float live (const SiltProcessor& silt, const char* id)
{
    if (const int t = silt.modulationTarget (id); t >= 0)
        return silt.targetParameter (t).convertFrom0to1 (silt.modulatedValue01 (t));
    const auto* raw = silt.params.getRawParameterValue (id);
    return raw != nullptr ? raw->load() : 0.0f;
}

juce::String text (const SiltProcessor& silt, const char* id, float value)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter (id)))
        return p->getText (p->convertTo0to1 (value), 0).toUpperCase();
    return {};
}

// The pictures' ground: the theme's inset colour taken most of the way to black.
juce::Colour deepBack() { return insetBack.interpolatedWith (juce::Colours::black, 0.55f); }

// A line that glows: a wide soft stroke under a crisp one.
void glowStroke (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width)
{
    g.setColour (colour.withMultipliedAlpha (0.12f));
    g.strokePath (path, juce::PathStrokeType (width * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour.withMultipliedAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (width * 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void glowRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour (colour.withMultipliedAlpha (0.18f));
    g.fillRect (r.expanded (2.5f, 0.0f));
    g.setColour (colour);
    g.fillRect (r);
}

// A faint grid, as FILTER draws it: the same in every picture.
void faintGrid (juce::Graphics& g, juce::Rectangle<float> plot, std::initializer_list<float> xs, std::initializer_list<float> ys)
{
    g.setColour (screenLight.withAlpha (0.2f));
    for (const float x : xs)
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    for (const float y : ys)
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
}

// The live scope (what goes into the open page's module and what comes out),
// with each side's spectrum. Read on the message thread; the FFTs run only
// when new audio has arrived.
struct LiveScope
{
    static constexpr int size = 4096, fftSize = 2048;
    std::vector<float> in = std::vector<float> (size), out = std::vector<float> (size);
    std::vector<float> inDb = std::vector<float> (fftSize / 2), outDb = std::vector<float> (fftSize / 2);
    unsigned count = ~0u;
    float inPeak = 0.0f, outPeak = 0.0f, inRms = 0.0f, outRms = 0.0f;
    double sampleRate = 48000.0;
    bool sounding = false;

    // The level (dB) of a spectrum around a frequency: the loudest bin within ±3%.
    float dbAt (const std::vector<float>& db, double hz) const
    {
        const double perBin = sampleRate / fftSize;
        const int lo = juce::jlimit (1, fftSize / 2 - 1, (int) (hz * 0.97 / perBin)), hi = juce::jlimit (lo, fftSize / 2 - 1, (int) std::ceil (hz * 1.03 / perBin));
        float best = -120.0f;
        for (int k = lo; k <= hi; ++k)
            best = std::max (best, db[(size_t) k]);
        return best;
    }
};

const LiveScope& liveScope (const SiltProcessor& silt)
{
    static LiveScope s;
    const unsigned count = silt.readScope (s.in.data(), s.out.data(), LiveScope::size);
    if (count == s.count)
        return s;
    s.count = count;
    s.sampleRate = silt.getSampleRate() > 0.0 ? silt.getSampleRate() : 48000.0;
    const auto measure = [] (const std::vector<float>& x, float& peak, float& rms)
    {
        double sum = 0.0;
        peak = 0.0f;
        for (const float v : x)
        {
            peak = std::max (peak, std::abs (v));
            sum += (double) v * v;
        }
        rms = (float) std::sqrt (sum / (double) x.size());
    };
    measure (s.in, s.inPeak, s.inRms);
    measure (s.out, s.outPeak, s.outRms);
    s.sounding = std::max (s.inPeak, s.outPeak) > 1.0e-4f;
    if (! s.sounding)
        return s;
    static std::vector<std::complex<float>> bins (LiveScope::fftSize);
    const auto spectrum = [&] (const std::vector<float>& x, std::vector<float>& db)
    {
        constexpr int n = LiveScope::fftSize;
        for (int i = 0; i < n; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (n - 1));
            bins[(size_t) i] = { x[(size_t) (LiveScope::size - n + i)] * w, 0.0f };
        }
        silt::fft (bins);
        for (int k = 0; k < n / 2; ++k)
            db[(size_t) k] = 20.0f * std::log10 (std::max (1.0e-7f, std::abs (bins[(size_t) k]) * 4.0f / (float) n));
    };
    spectrum (s.in, s.inDb);
    spectrum (s.out, s.outDb);
    return s;
}

// A spectrum as a path across a log-frequency plot (20 Hz–20 kHz, −72…0 dB).
juce::Path spectrumPath (const LiveScope& s, const std::vector<float>& db, juce::Rectangle<float> plot, bool closed)
{
    juce::Path p;
    const double perBin = s.sampleRate / LiveScope::fftSize;
    const auto yFor = [&] (float level) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (level + 72.0f) / 72.0f); };
    constexpr int columns = 140;
    for (int c = 0; c <= columns; ++c)
    {
        // The loudest bin in each column's share, so narrow peaks survive.
        const double hzLo = 20.0 * std::pow (1000.0, c / (double) columns), hzHi = 20.0 * std::pow (1000.0, (c + 1) / (double) columns);
        float level = -120.0f;
        for (int k = std::max (1, (int) (hzLo / perBin)); k <= std::min (LiveScope::fftSize / 2 - 1, (int) (hzHi / perBin)); ++k)
            level = std::max (level, db[(size_t) k]);
        if (level < -119.0f)
            level = db[(size_t) juce::jlimit (1, LiveScope::fftSize / 2 - 1, juce::roundToInt (hzLo / perBin))];
        const juce::Point<float> pt (plot.getX() + plot.getWidth() * (float) c / columns, yFor (level));
        if (c == 0)
        {
            if (closed)
            {
                p.startNewSubPath (plot.getX(), plot.getBottom());
                p.lineTo (pt);
            }
            else
                p.startNewSubPath (pt);
        }
        else
            p.lineTo (pt);
    }
    if (closed)
    {
        p.lineTo (plot.getRight(), plot.getBottom());
        p.closeSubPath();
    }
    return p;
}

// RESONATOR: whether a band is still ringing down, so the picture keeps moving.
bool resonatorRinging = false;

// Seconds on the message thread, for pictures that move by themselves (DRIFT, WOW).
float animationSeconds() { return (float) (juce::Time::getMillisecondCounter() % 3600000u) * 0.001f; }

// The frame: a dark panel, a title, and the drawing area under it.
juce::Rectangle<float> frame (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
{
    auto r = area.reduced (1.0f);
    g.setColour (deepBack());
    g.fillRect (r);
    g.setColour (screenLight);
    g.drawRect (r, 1.0f);
    auto inside = r.reduced (8.0f, 4.0f);
    g.setColour (screenLime);
    g.setFont (monoFont (10.0f));
    g.drawFittedText (title, inside.removeFromTop (13.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    inside.removeFromTop (3.0f);
    return inside;
}

// --- FILTER ------------------------------------------------------------------------------

// The SVF's analogue response (what the TPT design follows) with the same
// morph, resonance trims and 12/24 dB structure as silt::Filter, mixed with the
// dry signal by MIX.
double filterMagnitude (double hz, double cutoff, double resonance, double morph, bool steep, double mix)
{
    const Complex s (0.0, hz / cutoff);
    const double lowMix = std::max (0.0, 1.0 - morph), highMix = std::max (0.0, morph - 1.0), bandMix = 1.0 - lowMix - highMix;
    const auto stage = [&] (double q, bool normaliseBand)
    {
        const double k = 1.0 / q;
        const Complex d = s * s + k * s + 1.0;
        return (lowMix + bandMix * s * (normaliseBand ? k : 1.0) + highMix * s * s) / d;
    };
    const double butterworth = 0.7071067811865476, second = 1.3065629648763766, first = 0.5411961001461970;
    Complex h;
    if (steep)
    {
        const double q = second * std::pow (30.0 / second, resonance);
        h = stage (first, true) * stage (q, false) / std::sqrt (q / second);
    }
    else
    {
        const double q = butterworth * std::pow (30.0 / butterworth, resonance);
        h = stage (q, false) / std::sqrt (q / butterworth);
    }
    return std::abs ((1.0 - mix) + mix * h);
}

void drawFilter (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const int mode = juce::roundToInt (live (silt, "filterMode"));
    const float cutoff = live (silt, "filterCutoff");
    const auto title = mode == 0 ? juce::String ("FILTER - OFF")
                                 : "FILTER - " + text (silt, "filterMorph", live (silt, "filterMorph")) + " " + juce::String (mode == 2 ? "24 DB" : "12 DB")
                                       + " " + text (silt, "filterCutoff", cutoff);
    auto plot = frame (g, area, title);

    const auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log10 (hz / 20.0) / 3.0); };
    const auto yFor = [&] (double db) { return plot.getY() + plot.getHeight() * (float) ((24.0 - juce::jlimit (-48.0, 24.0, db)) / 72.0); };

    // Grid: 100 Hz, 1 kHz, 10 kHz, and the 0 dB line.
    g.setColour (screenLight.withAlpha (0.35f));
    for (const double hz : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) xFor (hz), plot.getY(), plot.getBottom());
    g.drawHorizontalLine ((int) yFor (0.0), plot.getX(), plot.getRight());

    juce::Path curve, fill;
    for (int i = 0; i <= 120; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 120.0);
        const double db = mode == 0 ? 0.0
                                    : 20.0 * std::log10 (std::max (1.0e-6, filterMagnitude (hz, cutoff, live (silt, "filterReso") * 0.01,
                                                                                            live (silt, "filterMorph"), mode == 2, live (silt, "filterMix") * 0.01)));
        const juce::Point<float> p (xFor (hz), yFor (db));
        if (i == 0)
        {
            curve.startNewSubPath (p);
            fill.startNewSubPath (p.x, plot.getBottom());
        }
        else
            curve.lineTo (p);
        fill.lineTo (p);
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (rust.withAlpha (0.35f));
    g.fillPath (fill);
    glowStroke (g, curve, juce::Colour (0xffd8693f), 1.5f);
    if (mode != 0)
    {
        g.setColour (screenPale.withAlpha (0.7f));
        g.drawVerticalLine ((int) xFor (cutoff), plot.getY(), plot.getBottom());

        // ENV: how far the input's level can take the cutoff (up to ±4 octaves).
        if (const float env = live (silt, "filterEnv") * 0.01f; std::abs (env) > 0.005f)
        {
            const float to = xFor (juce::jlimit (20.0, 20000.0, cutoff * std::pow (2.0, 4.0 * env)));
            const float y = plot.getY() + 4.0f;
            g.setColour (screenLime.withAlpha (0.85f));
            g.drawLine (xFor (cutoff), y, to, y, 1.4f);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ to, y }));
            g.setFont (monoFont (8.5f));
            g.drawText ("ENV", juce::Rectangle<float> (30.0f, 10.0f).withCentre ({ (xFor (cutoff) + to) * 0.5f, y + 8.0f }), juce::Justification::centred);
        }
    }
}

// --- RESONATOR -------------------------------------------------------------------------

void drawResonator (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    silt::ResonatorParams p;
    p.rootNote = live (silt, "resTune");
    p.notes = (silt::ResonatorNotes) juce::jlimit (0, 3, juce::roundToInt (live (silt, "resNotes")));
    p.sampleCount = juce::jlimit (0, (int) p.sampleHz.size(), silt.samplePitchCount.load());
    for (int i = 0; i < p.sampleCount; ++i)
        p.sampleHz[(size_t) i] = silt.samplePitchHz[(size_t) i].load() * std::pow (2.0f, (p.rootNote - 48.0f) / 12.0f);
    p.scale = (silt::Scale) juce::jlimit (0, silt::scaleCount - 1, juce::roundToInt (live (silt, "scale")));
    p.key = juce::roundToInt (live (silt, "key"));
    p.rangeOctaves = live (silt, "resRange");
    p.decaySeconds = live (silt, "resDecay");
    p.bright = live (silt, "resBright") * 0.01f;
    p.bandCount = juce::roundToInt (live (silt, "resBands"));
    const float mix = live (silt, "resMix") * 0.01f;
    const float spread = live (silt, "resSpread") * 0.01f, oddEven = live (silt, "resOddEven") * 0.01f;
    const float drift = live (silt, "resDrift") * 0.01f, damp = live (silt, "resDamp") * 0.01f;
    const float resonance = live (silt, "resReso") * 0.01f, width = live (silt, "resWidth") * 0.01f;
    const float drive = live (silt, "resDrive") * 0.01f;
    const char* notes[] { "SCALE", "HARMONICS", "BELL", "SAMPLE" };
    const bool secondHalf = silt.resonatorHalf.load() == 1;
    auto plot = frame (g, area, secondHalf ? "RESONATOR 2/2 - " + juce::String (p.bandCount) + " BANDS  Q " + juce::String (juce::roundToInt (8.0 * std::pow (15.0, (double) resonance)))
                                                 + (drift > 0.0f ? "  DRIFT " + juce::String (juce::roundToInt (drift * 100.0f)) + "%" : juce::String())
                                           : "RESONATOR - " + juce::String (notes[(int) p.notes]) + " " + juce::String (p.rangeOctaves, 1) + " OCT "
                                                 + text (silt, "resDecay", p.decaySeconds) + "  MIX " + juce::String (juce::roundToInt (mix * 100.0f)) + "%");

    std::array<double, silt::Resonator::maxBands> ratios {};
    const int n = silt::Resonator::bandRatios (p, ratios);
    if (n == 0)
        return;

    // A resonant filter bank, as its response: across, frequency (log, 20 Hz
    // to 20 kHz), so TUNE slides the whole comb of peaks; each band a peak as
    // sharp as RESONANCE makes it, as tall as BRIGHT and ODD/EVEN leave it,
    // teal panned left and magenta right (WIDTH), with SPREAD's detuned
    // neighbours and DRIFT's wander. Bands the sound hits light up and ring
    // down over their own ring time (DECAY, shorter up high with DAMP), so the
    // picture rings as the sound does. DRIVE: the input pushed up and
    // flattened along the bottom, sparks rising into the bands it excites.
    // MIX dims it all, never to nothing.
    const double tilt = 1.0 - p.bright;
    const double rootHz = 440.0 * std::pow (2.0, (p.rootNote - 69.0) / 12.0);
    const auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log10 (juce::jlimit (20.0, 20000.0, hz) / 20.0) / 3.0); };
    const float base = plot.getBottom() - 2.0f, tall = plot.getHeight() - 14.0f;
    const auto& scope = liveScope (silt);
    const float now = animationSeconds();
    const float driftHz = 0.15f * std::pow (20.0f, drift);
    const float fade = 0.3f + 0.7f * mix;
    const double q = 8.0 * std::pow (15.0, (double) resonance);
    const double shownQ = std::min (q, 60.0);   // the sharpest a peak can be drawn and still be seen

    faintGrid (g, plot, { xFor (100.0), xFor (1000.0), xFor (10000.0) }, { base });

    // Each band's ringing, carried from one picture to the next.
    static std::array<float, silt::Resonator::maxBands> energy {};
    static float lastTime = 0.0f;
    const float dt = juce::jlimit (0.0f, 0.2f, now - lastTime);
    lastTime = now;

    // DRIVE: the input along the bottom, pushed up and flattened as the drive
    // squashes it (or, quiet, a low flicker).
    if (drive > 0.0f)
    {
        juce::Path driven;
        driven.startNewSubPath (plot.getX(), base);
        juce::Random flicker ((juce::int64) (now * 24.0f));
        for (int c = 0; c <= 80; ++c)
        {
            const double hz = 20.0 * std::pow (1000.0, c / 80.0);
            const float level = scope.sounding ? juce::jlimit (0.0f, 1.0f, (scope.dbAt (scope.inDb, hz) + 72.0f) / 72.0f)
                                               : 0.08f * flicker.nextFloat();
            const float pushed = std::tanh (level * (1.0f + 4.0f * drive) * 1.4f) / 1.4f;
            driven.lineTo (xFor (hz), base - pushed * plot.getHeight() * 0.3f * drive);
        }
        driven.lineTo (plot.getRight(), base);
        driven.closeSubPath();
        g.setColour (screenLime.withAlpha (0.18f * fade));
        g.fillPath (driven);
    }

    juce::Path response;
    std::vector<float> responseLevel (161, 0.0f);
    bool ringing = false;
    for (int i = 0; i < n; ++i)
    {
        const double cents = 35.0 * spread * std::sin ((double) i * 2.4 + 0.7);
        double ratio = ratios[(size_t) i] * std::pow (2.0, cents / 1200.0);
        if (drift > 0.0f)   // up to ±48 cents of wander, each band at its own pace
            ratio *= std::pow (2.0, 48.0 * drift * std::sin (6.2831853 * driftHz * now * (0.7 + 0.13 * (i % 5)) + i * 1.7) / 1200.0);
        const double hz = rootHz * ratio;
        if (hz > 20000.0)
            break;
        const float against = i % 2 == 0 ? std::max (0.0f, oddEven) : std::max (0.0f, -oddEven);
        const float level = juce::jlimit (0.0f, 1.0f, (float) std::pow (ratio, -0.75 * tilt) * (1.0f - against));
        const float ring = std::max (0.02f, p.decaySeconds * (float) std::pow (ratio, -0.7 * tilt - 1.5 * damp));
        const float pan = (i % 2 == 0 ? -1.0f : 1.0f) * width * (n > 1 ? 1.0f : 0.0f);
        const auto colour = pan < 0.0f ? screenPale.interpolatedWith (signalTeal, -pan) : screenPale.interpolatedWith (signalMagenta, pan);

        // Struck by the sound now, it rings, falling 60 dB over its ring time.
        const float lit = scope.sounding ? juce::jlimit (0.0f, 1.0f, (scope.dbAt (scope.outDb, hz) + 54.0f) / 42.0f) : 0.0f;
        auto& e = energy[(size_t) i];
        e = std::max (lit, e * std::pow (10.0f, -3.0f * dt / ring));
        ringing = ringing || e > 0.01f;

        const auto peakPath = [&] (double centre, float height, bool closed)
        {
            juce::Path path;
            const double reach = std::max (0.08, 3.0 / shownQ);   // octaves either side worth drawing
            for (int k = 0; k <= 48; ++k)
            {
                const double octaves = reach * (2.0 * k / 48.0 - 1.0);
                const double f = centre * std::pow (2.0, octaves);
                const double detune = f / centre - centre / f;
                const float y = base - height / (float) std::sqrt (1.0 + shownQ * shownQ * detune * detune);
                if (k == 0)
                {
                    if (closed)
                    {
                        path.startNewSubPath (xFor (f), base);
                        path.lineTo (xFor (f), y);
                    }
                    else
                        path.startNewSubPath (xFor (f), y);
                }
                else
                    path.lineTo (xFor (f), y);
            }
            if (closed)
            {
                path.lineTo (xFor (centre * std::pow (2.0, reach)), base);
                path.closeSubPath();
            }
            return path;
        };
        const float height = tall * (0.25f + 0.75f * level) * (0.55f + 0.45f * e);
        // SPREAD: two faint detuned neighbours.
        if (spread > 0.02f)
            for (const double side : { -1.0, 1.0 })
            {
                g.setColour (colour.withAlpha (fade * 0.3f));
                g.strokePath (peakPath (hz * std::pow (2.0, side * (20.0 + 60.0 * spread) / 1200.0), height * 0.6f, false), juce::PathStrokeType (0.8f));
            }
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (fade * (0.15f + 0.45f * e)), 0.0f, base - height, colour.withAlpha (0.0f), 0.0f, base, false));
        g.fillPath (peakPath (hz, height, true));
        glowStroke (g, peakPath (hz, height, false), colour.withAlpha (fade * (0.45f + 0.55f * e)), 0.9f + 0.8f * e);

        // DRIVE: sparks rising from the bottom into the band, more with more drive.
        if (drive > 0.0f && e > 0.05f)
        {
            juce::Random r ((juce::int64) i * 977);
            for (int k = 0; k < juce::roundToInt (4.0f * drive * e) + 1; ++k)
            {
                const float phase = std::fmod (now * (0.8f + r.nextFloat()) + r.nextFloat(), 1.0f);
                const float x = xFor (hz * std::pow (2.0, (r.nextFloat() - 0.5) * 0.15));
                g.setColour (screenLime.withAlpha (fade * (1.0f - phase) * 0.9f));
                g.fillRect (juce::Rectangle<float> (1.6f, 1.6f).withCentre ({ x, base - phase * height }));
            }
        }
    }
    resonatorRinging = ringing;

    // TUNE: the root, marked and named.
    const float rootX = xFor (rootHz);
    g.setColour (screenLime.withAlpha (0.75f));
    g.drawVerticalLine ((int) rootX, plot.getY(), base);
    g.setFont (monoFont (8.5f));
    g.drawText (text (silt, "resTune", p.rootNote), juce::Rectangle<float> (rootX + 3.0f, plot.getY(), 60.0f, 10.0f), juce::Justification::centredLeft);
    g.setColour (screenPale.withAlpha (0.5f));
    g.drawText (juce::String (juce::roundToInt (rootHz)) + " HZ", juce::Rectangle<float> (rootX + 3.0f, plot.getY() + 10.0f, 60.0f, 10.0f), juce::Justification::centredLeft);
}

// --- COLOR -------------------------------------------------------------------------------

// Left: the level curve (DRIVE's saturation, BITS' steps) with the live sound
// plotted through it, input across and output up (or, quiet, a sine riding
// it). Right: across frequency, the input faint and the output bright; RATE's
// fold with the sound above it mirrored back down as the alias it becomes;
// TONE's tilt, wobbling with WOW; NOISE riding the level; COMP's gain
// reduction as it happens.
void drawColor (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const float drive = live (silt, "colorDrive") * 0.01f, bits = live (silt, "colorBits"), tone = live (silt, "colorTone") * 0.01f;
    const float rate = live (silt, "colorRate"), noise = live (silt, "colorNoise") * 0.01f, comp = live (silt, "colorComp") * 0.01f;
    const float wow = live (silt, "colorWow") * 0.01f;
    juce::StringArray parts;
    if (drive > 0.0f) parts.add ("DRIVE " + juce::String (juce::roundToInt (drive * 100.0f)) + "%");
    if (bits < 15.995f) parts.add (text (silt, "colorBits", bits));
    if (rate < silt::Color::rateOff - 1.0f) parts.add (text (silt, "colorRate", rate));
    if (std::abs (tone) > 0.005f) parts.add (text (silt, "colorTone", tone * 100.0f));
    if (noise > 0.0f) parts.add ("NOISE");
    if (comp > 0.0f) parts.add ("COMP");
    if (wow > 0.0f) parts.add ("WOW");
    auto plot = frame (g, area, "COLOR - " + (parts.isEmpty() ? juce::String ("CLEAN") : parts.joinIntoString ("  ")));
    const auto& scope = liveScope (silt);
    const float now = animationSeconds();

    // The level curve, in a square on the left.
    auto curveBox = plot.removeFromLeft (juce::jmin (plot.getHeight(), plot.getWidth() * 0.32f));
    plot.removeFromLeft (8.0f);
    g.setColour (screenLight.withAlpha (0.35f));
    g.drawRect (curveBox, 1.0f);
    g.setColour (screenLight.withAlpha (0.2f));
    g.drawLine (juce::Line<float> (curveBox.getBottomLeft(), curveBox.getTopRight()), 0.8f);
    const float gain = std::pow (10.0f, 30.0f * drive / 20.0f), bias = 0.2f * drive;
    const float makeup = 1.0f / std::pow (gain, 0.8f), blend = std::min (1.0f, drive * 10.0f);
    const float steps = bits < 15.995f ? std::pow (2.0f, bits - 1.0f) : 0.0f;
    const auto shape = [&] (float x)
    {
        float y = x + blend * ((std::tanh (gain * x + bias) - std::tanh (bias)) * makeup - x);
        if (steps > 0.0f)
            y = std::round (y * steps) / steps;
        return juce::jlimit (-1.0f, 1.0f, y);
    };
    const auto boxPoint = [&] (float x, float y)
    {
        return juce::Point<float> (curveBox.getCentreX() + juce::jlimit (-1.0f, 1.0f, x) * curveBox.getWidth() * 0.5f,
                                   curveBox.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * curveBox.getHeight() * 0.5f);
    };
    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 120.0f;
        if (i == 0)
            curve.startNewSubPath (boxPoint (x, shape (x)));
        else
            curve.lineTo (boxPoint (x, shape (x)));
    }
    glowStroke (g, curve, screenLime.withAlpha (scope.sounding ? 0.55f : 0.9f), 1.3f);
    if (scope.sounding && scope.inPeak > 1.0e-4f)
    {
        // The sound through it: each sample's input against its output, both
        // scaled by the input's peak, so a quiet sound fills the box too.
        g.setColour (signalTeal.withAlpha (0.55f));
        const float scale = 1.0f / scope.inPeak;
        for (int i = LiveScope::size - 1200; i < LiveScope::size; i += 2)
            g.fillRect (juce::Rectangle<float> (1.6f, 1.6f).withCentre (boxPoint (scope.in[(size_t) i] * scale, scope.out[(size_t) i] * scale)));
    }
    else
    {
        // Quiet: a sine riding the curve, so the squash and the steps still show.
        const float x = std::sin (6.2831853f * 0.5f * now);
        const auto dot = boxPoint (x, shape (x));
        g.setColour (signalTeal.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (dot));
        g.setColour (signalTeal);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (dot));
    }

    // Across frequency (log, 20 Hz–20 kHz). The live spectra use −72…0 dB;
    // TONE's tilt ±9 dB round a line 40% down.
    const auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log10 (juce::jlimit (20.0, 20000.0, hz) / 20.0) / 3.0); };
    const float mid = plot.getY() + plot.getHeight() * 0.4f;
    const auto yFor = [&] (double db) { return mid - (float) db / 9.0f * plot.getHeight() * 0.4f; };
    faintGrid (g, plot, { xFor (100.0), xFor (1000.0), xFor (10000.0) }, { mid });

    // COMP: a gain-reduction meter on the right edge, falling from the top.
    auto spectra = plot;
    if (comp > 0.0f)
    {
        auto meter = spectra.removeFromRight (6.0f);
        spectra.removeFromRight (4.0f);
        const float threshold = -30.0f * comp, ratio = 1.0f + 7.0f * comp;
        const float peakDb = 20.0f * std::log10 (std::max (scope.inPeak, 1.0e-6f));
        const float reduction = scope.sounding ? std::max (0.0f, peakDb - threshold) * (1.0f - 1.0f / ratio) : 0.0f;
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (meter);
        glowRect (g, meter.withHeight (meter.getHeight() * juce::jlimit (0.0f, 1.0f, reduction / 24.0f)), screenLime);
    }

    if (scope.sounding)
    {
        g.setColour (screenPale.withAlpha (0.12f));
        g.fillPath (spectrumPath (scope, scope.inDb, spectra, true));
        g.setColour (screenPale.withAlpha (0.35f));
        g.strokePath (spectrumPath (scope, scope.inDb, spectra, false), juce::PathStrokeType (0.8f));
        glowStroke (g, spectrumPath (scope, scope.outDb, spectra, false), signalTeal, 1.1f);
    }

    // NOISE: a hiss floor along the bottom that rides the sound's level.
    if (noise > 0.0f)
    {
        const float level = scope.sounding ? juce::jlimit (0.0f, 1.0f, scope.outRms * 4.0f) : 0.0f;
        juce::Random r ((juce::int64) (now * 20.0f));
        const float h = spectra.getHeight() * (0.04f + 0.3f * noise * noise * level);
        g.setColour (screenPale.withAlpha (0.2f + 0.4f * noise));
        for (float x = spectra.getX(); x < spectra.getRight(); x += 2.0f)
            g.fillRect (x, spectra.getBottom() - h * (0.3f + 0.7f * r.nextFloat()), 1.2f, h);
    }

    // RATE: everything above half the hold rate folds back below it. The
    // fold, and the sound above it mirrored down as the alias it turns into.
    if (rate < silt::Color::rateOff - 1.0f)
    {
        const double foldHz = juce::jlimit (20.0, 20000.0, (double) rate * 0.5);
        const float fold = xFor (foldHz);
        g.setGradientFill (juce::ColourGradient (signalRed.withAlpha (0.18f), fold, 0.0f, signalRed.withAlpha (0.0f), spectra.getRight(), 0.0f, false));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fold, spectra.getY(), spectra.getRight(), spectra.getBottom()));
        juce::Path alias;
        const auto levelAt = [&] (double hz)
        {
            // The input's level above the fold, or (quiet) a falling slope.
            return scope.sounding ? scope.dbAt (scope.inDb, hz) : (float) (-24.0 - 18.0 * std::log2 (hz / foldHz));
        };
        bool started = false;
        for (int i = 0; i <= 60; ++i)
        {
            const double above = foldHz * std::pow (2.0, 2.0 * i / 60.0);   // up to two octaves over
            const double mirrored = 2.0 * foldHz - above;
            if (mirrored < 20.0 || above > 24000.0)
                break;
            const float y = spectra.getBottom() - spectra.getHeight() * juce::jlimit (0.0f, 1.0f, (levelAt (above) + 72.0f) / 72.0f);
            if (! started)
                alias.startNewSubPath (xFor (mirrored), y);
            else
                alias.lineTo (xFor (mirrored), y);
            started = true;
        }
        if (started)
            glowStroke (g, alias, signalMagenta.withAlpha (0.85f), 1.0f);
        g.setColour (signalRed.brighter (0.3f));
        g.drawVerticalLine ((int) fold, spectra.getY(), spectra.getBottom());
    }

    // TONE: the tilt round 800 Hz, as silt::Color's one-pole split does it;
    // WOW sends a wobble along it.
    const double high = std::pow (10.0, 6.0 * tone / 20.0), low = 1.0 / high;
    juce::Path tilt;
    for (int i = 0; i <= 100; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 100.0);
        const Complex jw (0.0, hz / 800.0);
        const double db = 20.0 * std::log10 (std::abs ((low + high * jw) / (1.0 + jw)));
        const float y = yFor (db) + wow * 3.5f * (float) std::sin (i * 0.55 - now * 5.0);
        if (i == 0)
            tilt.startNewSubPath (xFor (hz), y);
        else
            tilt.lineTo (xFor (hz), y);
    }
    glowStroke (g, tilt, screenLime, 1.4f);
}

// --- SPACE -------------------------------------------------------------------------------

// SPACE as the engine runs it (silt::Space): the delay's repeats, and the
// reverb after the dry sound and after each repeat (the delay feeds it). The
// reverb waits PRE-DELAY (35 ms at full SIZE), builds over its early
// reflections (25–150 ms with SIZE), then falls 60 dB over DECAY; its highs
// fall over a TONE share of that.
struct SpaceView
{
    bool synced, frozen, pingPong;
    int duck;   // 0 off … 3 heavy
    float delay, feedback, delayLevel, reverbLevel, decay, size, tone, shimmer;
    float preDelay, early, highDecay;
    std::vector<float> taps;   // each repeat's level, before DELAY

    explicit SpaceView (const SiltProcessor& silt)
    {
        synced = live (silt, "spaceSync") > 0.5f;
        const float ms = synced ? (float) (SiltProcessor::divisionQuarters (juce::roundToInt (live (silt, "spaceDiv")))
                                           * 60000.0 / juce::jlimit (20.0, 400.0, silt.hostBpm.load()))
                                : live (silt, "spaceTime");
        delay = ms / 1000.0f;
        feedback = live (silt, "spaceFeedback") * 0.01f;
        delayLevel = live (silt, "spaceDelay") * 0.01f;
        reverbLevel = live (silt, "spaceReverb") * 0.01f;
        decay = std::max (0.1f, live (silt, "spaceDecay"));
        size = live (silt, "spaceSize") * 0.01f;
        tone = live (silt, "spaceTone") * 0.01f;
        shimmer = live (silt, "spaceShimmer") * 0.01f;
        frozen = live (silt, "spaceFreeze") > 0.5f;
        pingPong = juce::roundToInt (live (silt, "spaceMode")) == 0;
        duck = juce::roundToInt (live (silt, "spaceDuck"));
        preDelay = 0.035f * size;
        early = 0.025f + 0.125f * size;
        highDecay = std::max (0.05f, (0.15f + 0.75f * tone) * decay);
        for (float level = 1.0f; level > 0.03f && taps.size() < 24; level *= feedback)
            taps.push_back (level);
    }

    // How much time the picture spans: the repeats, and the tail to −60 dB.
    float window() const
    {
        float w = delayLevel > 0.0f ? delay * ((float) std::min<size_t> (taps.size(), 8) + 0.6f) : 0.0f;
        if (reverbLevel > 0.0f)
            w = std::max (w, frozen ? 3.0f : preDelay + early * 0.5f + decay);
        return juce::jlimit (0.4f, 10.0f, std::max (w, 0.5f));
    }

    // The reverb's level t seconds after a hit: silent through the pre-delay,
    // building over the early reflections, then falling 60 dB over `fall`.
    float reverbAfter (float t, float fall) const
    {
        const float start = preDelay, peak = preDelay + early * 0.5f;
        if (t <= start)
            return 0.0f;
        if (t < peak)
            return (t - start) / (peak - start);
        return frozen ? 1.0f : std::pow (10.0f, -3.0f * (t - peak) / fall);
    }

    // What SPACE adds t seconds after a note, as a level: the repeats' and
    // the tails' energies added.
    float tailLevel (float t, float fall) const
    {
        if (reverbLevel <= 0.0f)
            return 0.0f;
        float energy = juce::square (reverbAfter (t, fall));
        for (size_t n = 0; n < taps.size() && delayLevel > 0.0f; ++n)
        {
            const float at = delay * (float) (n + 1);
            if (at < t)
                energy += juce::square (delayLevel * taps[n] * reverbAfter (t - at, fall));
        }
        return reverbLevel * std::sqrt (energy) * duckAt (t);
    }

    // DUCK, as the picture assumes it: the note sounds for its first `held`
    // seconds, dipping the sends (up to 30 dB with HEAVY), then they come back
    // over about half a second.
    float held() const { return std::min (0.4f, window() * 0.25f); }
    float duckAt (float t) const
    {
        static constexpr float amounts[] { 0.0f, 0.35f, 0.65f, 1.0f };
        const float depth = amounts[juce::jlimit (0, 3, duck)];
        if (depth <= 0.0f)
            return 1.0f;
        const float dip = std::pow (10.0f, -1.5f * depth);
        return t < held() ? dip : 1.0f - (1.0f - dip) * std::exp (-(t - held()) / 0.12f);
    }
};

// The readout beside the timeline: the numbers behind the picture, each with
// a small bar for how far along its range it is.
void drawSpace (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const SpaceView s (silt);
    auto plot = frame (g, area, juce::String ("SPACE") + (s.frozen ? "  - FROZEN" : ""));
    const bool delayOn = s.delayLevel > 0.0f, reverbOn = s.reverbLevel > 0.0f;
    const auto ms = [] (float seconds) { return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " MS"
                                                               : juce::String (seconds, 2) + " S"; };
    const auto logShare = [] (float v, float lo, float hi) { return std::log (std::max (v, lo) / lo) / std::log (hi / lo); };
    const int repeats = s.feedback <= 0.0f ? 1 : (int) s.taps.size();
    const struct Row { const char* label; juce::String value; float share; bool on; juce::Colour colour; } rows[] {
        { "DELAY", delayOn ? (s.synced ? text (silt, "spaceDiv", live (silt, "spaceDiv")) : ms (s.delay)) : juce::String ("OFF"), logShare (s.delay, 0.01f, 4.0f), delayOn, signalMagenta },
        { "REPEATS", juce::String (repeats) + (s.pingPong ? " L/R" : " ST"), (float) repeats / 24.0f, delayOn, signalMagenta },
        { "LEVEL", juce::String (juce::roundToInt (s.delayLevel * 100.0f)) + "%", s.delayLevel, delayOn, signalMagenta },
        { "PRE", ms (s.preDelay), s.preDelay / 0.035f, reverbOn, signalTeal },
        { "EARLY", ms (s.early), s.early / 0.15f, reverbOn, signalTeal },
        { "DECAY", s.frozen ? juce::String ("HOLD") : text (silt, "spaceDecay", s.decay), s.frozen ? 1.0f : logShare (s.decay, 0.3f, 30.0f), reverbOn, signalTeal },
        { "HIGHS", s.frozen ? juce::String ("HOLD") : juce::String (s.highDecay, 2) + " S", s.frozen ? 1.0f : s.highDecay / s.decay, reverbOn, signalTeal },
        { "SHIMMER", juce::String (juce::roundToInt (s.shimmer * 100.0f)) + "%", s.shimmer, reverbOn && s.shimmer > 0.0f, signalTeal },
        { "REVERB", juce::String (juce::roundToInt (s.reverbLevel * 100.0f)) + "%", s.reverbLevel, reverbOn, signalTeal },
        { "DUCK", s.duck > 0 ? text (silt, "spaceDuck", (float) s.duck) : juce::String ("OFF"), (float) s.duck / 3.0f, (delayOn || reverbOn) && s.duck > 0, screenLime },
    };
    const int columns = plot.getWidth() > 260.0f ? 2 : 1;
    const int perColumn = (int) std::ceil ((float) std::size (rows) / (float) columns);
    const float rowH = std::min (13.0f, plot.getHeight() / (float) perColumn), colW = plot.getWidth() / (float) columns;
    g.setFont (monoFont (9.0f));
    for (size_t i = 0; i < std::size (rows); ++i)
    {
        const int column = (int) i / perColumn, row = (int) i % perColumn;
        auto cell = juce::Rectangle<float> (plot.getX() + colW * (float) column, plot.getY() + rowH * (float) row, colW - 8.0f, rowH);
        const auto& r = rows[i];
        g.setColour (screenPale.withAlpha (r.on ? 0.75f : 0.3f));
        g.drawText (r.label, cell.removeFromLeft (56.0f), juce::Justification::centredLeft);
        g.setColour (r.on ? juce::Colours::white : screenPale.withAlpha (0.3f));
        g.drawText (r.value, cell.removeFromRight (52.0f), juce::Justification::centredRight);
        // The bar: a faint track, filled (and glowing) while it's in use.
        const auto track = cell.reduced (4.0f, 0.0f).withSizeKeepingCentre (cell.getWidth() - 8.0f, 3.0f);
        g.setColour (screenLight.withAlpha (0.2f));
        g.fillRect (track);
        if (r.on)
            glowRect (g, track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, r.share)), r.colour);
    }
}

// The timeline across the screen's lower strip: from a note (at the left) on,
// what SPACE adds, on a decibel scale. The repeats are bars, the reverb a
// filled shape (its highs, fading sooner, a brighter one inside), SHIMMER a
// glitter above. Over it, the level SPACE is really adding since the last note.
void drawSpaceTimelineImpl (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const SpaceView s (silt);
    const float now = animationSeconds();
    g.setColour (deepBack());
    g.fillRect (area.expanded (0.0f, 2.0f));
    g.setColour (screenLight.withAlpha (0.6f));
    g.drawRect (area, 1.0f);

    auto inner = area.reduced (6.0f, 3.0f);
    auto title = inner.removeFromTop (12.0f);
    g.setFont (monoFont (10.0f));
    g.setColour (screenLime);
    g.drawText ("SPACE", title, juce::Justification::centredLeft);
    g.setColour (screenPale.withAlpha (0.7f));
    g.drawText ("SINCE THE LAST NOTE", title, juce::Justification::centredRight);
    inner.removeFromTop (2.0f);
    auto plot = inner.withTrimmedBottom (1.0f);

    const float window = s.window();
    const auto xFor = [&] (float seconds) { return plot.getX() + plot.getWidth() * seconds / window; };
    const auto yFor = [&] (float level)   // −60…0 dB
    {
        const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (level, -60.0f));
        return plot.getBottom() - plot.getHeight() * (db + 60.0f) / 60.0f;
    };

    // Grid: −20 and −40 dB; beats when synced, else even steps of time.
    g.setColour (screenLight.withAlpha (0.18f));
    for (const float db : { -20.0f, -40.0f })
        g.drawHorizontalLine ((int) yFor (juce::Decibels::decibelsToGain (db)), plot.getX(), plot.getRight());
    float step = s.synced ? (float) (60.0 / juce::jlimit (20.0, 400.0, silt.hostBpm.load())) : 0.05f;
    if (! s.synced)
        for (const float candidate : { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f })
            if (window / candidate <= 12.0f) { step = candidate; break; }
    while (window / step > 16.0f)
        step *= 4.0f;   // a bar of beats
    g.setColour (screenLight.withAlpha (0.25f));
    for (float t = step; t < window; t += step)
        g.drawVerticalLine ((int) xFor (t), plot.getY(), plot.getBottom());
    g.setFont (monoFont (8.5f));
    g.setColour (screenPale.withAlpha (0.6f));
    g.drawText ((window < 1.0f ? juce::String (juce::roundToInt (window * 1000.0f)) + " MS" : juce::String (window, 1) + " S"),
                plot.withTrimmedTop (plot.getHeight() - 10.0f).removeFromRight (44.0f), juce::Justification::centredRight);

    // The reverb: all of it, then its highs.
    float predictedPeak = 0.0f;
    const auto layer = [&] (float fall, juce::Colour colour)
    {
        juce::Path shape;
        shape.startNewSubPath (plot.getX(), plot.getBottom());
        constexpr int points = 220;
        for (int i = 0; i <= points; ++i)
        {
            const float t = window * (float) i / points;
            const float level = s.tailLevel (t, fall);
            predictedPeak = std::max (predictedPeak, level);
            shape.lineTo (xFor (t), yFor (level));
        }
        shape.lineTo (plot.getRight(), plot.getBottom());
        shape.closeSubPath();
        g.setColour (colour);
        g.fillPath (shape);
    };
    const auto tailColour = screenLight.interpolatedWith (screenPale, 0.4f);
    if (s.reverbLevel > 0.0f)
    {
        layer (s.decay, tailColour.withAlpha (0.55f));
        layer (s.highDecay, juce::Colours::white.withAlpha (0.12f + 0.25f * s.tone));

        // The tail's density: a sparkle that shifts inside it, thicker with SIZE.
        {
            juce::Random r ((juce::int64) (now * 12.0f));
            g.setColour (signalTeal.withAlpha (0.35f));
            for (int i = 0; i < 20 + juce::roundToInt (60.0f * s.size * s.reverbLevel); ++i)
            {
                const float t = s.preDelay + r.nextFloat() * (window - s.preDelay);
                const float top = yFor (s.tailLevel (t, s.decay));
                const float y = top + r.nextFloat() * (plot.getBottom() - top);
                if (top < plot.getBottom() - 2.0f)
                    g.fillRect (xFor (t), y, 1.2f, 1.2f);
            }
        }

        // SHIMMER: glitter rising off the tail (it's an octave up, fed back),
        // drifting on through time and twinkling; more, faster and higher as it
        // turns up, gathering as the feedback builds.
        if (s.shimmer > 0.0f)
        {
            juce::Random r (11);
            const int count = 20 + juce::roundToInt (130.0f * s.shimmer);
            for (int i = 0; i < count; ++i)
            {
                const float seed = r.nextFloat(), speed = 0.25f + 0.5f * r.nextFloat() + 0.6f * s.shimmer;
                const float age = std::fmod (now * speed + seed * 7.0f, 1.0f);
                const float t = s.preDelay + std::fmod (seed + age * (0.08f + 0.12f * s.shimmer), 1.0f) * (window - s.preDelay);
                const float build = 1.0f - std::exp (-(t - s.preDelay) / (0.25f + 0.15f * s.decay));
                const float level = s.tailLevel (t, s.decay) * build;
                if (level <= 0.001f)
                    continue;
                const float rise = age * plot.getHeight() * (0.15f + 0.45f * s.shimmer);
                const float y = yFor (level) - 2.0f - rise;
                if (y < plot.getY())
                    continue;
                const float twinkle = std::sin (juce::MathConstants<float>::pi * age);
                const float size = 1.2f + 1.6f * s.shimmer * twinkle;
                g.setColour (juce::Colours::white.interpolatedWith (signalTeal, age).withAlpha (twinkle * (0.4f + 0.6f * s.shimmer)));
                g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ xFor (t), y }));
            }
        }
    }

    // The pre-delay as the gap it is, and the early reflections building the tail.
    if (s.reverbLevel > 0.0f && ! s.frozen)
    {
        const float x0 = xFor (0.0f), x1 = xFor (s.preDelay);
        if (x1 - x0 > 3.0f)
        {
            const float y = plot.getBottom() - 4.0f;
            g.setColour (screenLime.withAlpha (0.7f));
            g.drawLine (x0 + 2.0f, y, x1, y, 1.0f);
            g.drawVerticalLine ((int) x1, y - 3.0f, y + 2.0f);
            if (x1 - x0 > 24.0f)
            {
                g.setFont (monoFont (8.0f));
                g.drawText ("PRE", juce::Rectangle<float> (x0 + 2.0f, y - 11.0f, x1 - x0, 9.0f), juce::Justification::centred);
            }
        }
        juce::Random r (5);
        const int reflections = 6 + juce::roundToInt (10.0f * s.size);
        for (int i = 0; i < reflections; ++i)
        {
            // Closer together as they build, as a room's reflections thicken.
            const float along = std::sqrt (((float) i + r.nextFloat()) / (float) reflections);
            const float t = s.preDelay + along * s.early;
            const float level = s.tailLevel (t, s.decay) * (0.7f + 0.5f * r.nextFloat());
            if (level > 0.001f)
                glowRect (g, juce::Rectangle<float>::leftTopRightBottom (xFor (t) - 0.5f, yFor (level), xFor (t) + 0.5f, plot.getBottom()), signalTeal.withAlpha (0.8f));
        }
    }

    // The repeats: each FEEDBACK quieter and, with TONE dark, duller.
    if (s.delayLevel > 0.0f)
    {
        const juce::Colour bright (0xffe0784a), dull (0xff7a4a52);
        g.setFont (monoFont (8.0f));
        // Each repeat flashes as the playhead reaches it: the latest note's,
        // while it's in view; otherwise a slow sweep, so the timing shows.
        const double rate = silt.getSampleRate() > 0.0 ? silt.getSampleRate() : 48000.0;
        const float sinceNote = (float) silt.spaceTraceCount.load (std::memory_order_relaxed) * (float) (SiltProcessor::spaceTraceStep / rate);
        const float sweep = sinceNote <= window ? sinceNote : std::fmod (now, window * 1.2f);
        for (size_t n = 0; n < s.taps.size(); ++n)
        {
            const float t = s.delay * (float) (n + 1);
            if (t > window)
                break;
            predictedPeak = std::max (predictedPeak, s.delayLevel * s.taps[n] * s.duckAt (t));
            const float level = s.delayLevel * s.taps[n] * s.duckAt (t);
            const float x = xFor (t), top = yFor (level);
            const auto base = s.pingPong ? (n % 2 == 0 ? signalTeal : signalMagenta) : bright;
            const auto colour = base.interpolatedWith (dull, std::min (1.0f, (1.0f - s.tone) * (float) n / 6.0f));
            const float flash = sweep >= t ? std::exp (-(sweep - t) / 0.2f) : 0.0f;
            if (flash > 0.02f)
            {
                g.setColour (colour.withAlpha (0.35f * flash));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x - 6.0f, top - 4.0f * flash, x + 6.0f, plot.getBottom()));
            }
            glowRect (g, juce::Rectangle<float>::leftTopRightBottom (x - 1.5f, top, x + 1.5f, plot.getBottom()), colour.brighter (0.6f * flash));
            if (s.pingPong && s.delay / window * plot.getWidth() > 14.0f && top - 10.0f > plot.getY())
            {
                g.setColour (colour.withAlpha (0.9f));
                g.drawText (n % 2 == 0 ? "L" : "R", juce::Rectangle<float> (x - 5.0f, top - 10.0f, 10.0f, 9.0f), juce::Justification::centred);
            }
        }
    }

    // The note, at the left; with DUCK, how long the picture has it sounding.
    g.setColour (screenLime.withAlpha (0.7f));
    g.fillRect (plot.getX(), plot.getY(), 1.5f, plot.getHeight());
    if (s.duck > 0 && (s.reverbLevel > 0.0f || s.delayLevel > 0.0f))
    {
        g.setColour (screenLime.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), plot.getY(), xFor (s.held()), plot.getBottom()));
        g.setColour (screenLime.withAlpha (0.7f));
        g.setFont (monoFont (8.0f));
        g.drawText ("DUCK", juce::Rectangle<float> (plot.getX() + 3.0f, plot.getY() + 1.0f, 40.0f, 9.0f), juce::Justification::centredLeft);
    }

    if (predictedPeak <= 0.0f)
    {
        g.setColour (screenPale.withAlpha (0.8f));
        g.drawText ("TURN UP DELAY OR REVERB", plot, juce::Justification::centred);
        return;
    }

    // What SPACE is really adding: aligned to the latest note while it's in
    // view, scrolling once the note is older than the picture. Scaled so its
    // loudest moment meets the picture's.
    const double rate = silt.getSampleRate() > 0.0 ? silt.getSampleRate() : 48000.0;
    const float stepSeconds = (float) (SiltProcessor::spaceTraceStep / rate);
    const unsigned count = silt.spaceTraceCount.load (std::memory_order_acquire);
    const unsigned shown = (unsigned) std::min<double> (window / stepSeconds, SiltProcessor::spaceTraceSize - 1);
    const bool aligned = count <= shown;
    const unsigned first = aligned ? 0u : count - shown, last = count;
    float loudest = 0.0f;
    for (unsigned i = first; i < last; ++i)
        loudest = std::max (loudest, silt.spaceTraceAt (i));
    if (loudest < 1.0e-5f)
        return;
    const float scale = predictedPeak / loudest;
    juce::Path trace;
    const float perPixel = window / stepSeconds / plot.getWidth();   // trace values per pixel
    const int columns = juce::roundToInt ((float) (last - first) / std::max (perPixel, 1.0e-3f));
    for (int c = 0; c < std::max (columns, 1); ++c)
    {
        // The loudest value in each pixel's share, so peaks survive.
        const unsigned from = first + (unsigned) ((float) c * perPixel);
        const unsigned to = std::min (last, std::max (from + 1, first + (unsigned) ((float) (c + 1) * perPixel)));
        float level = 0.0f;
        for (unsigned i = from; i < to; ++i)
            level = std::max (level, silt.spaceTraceAt (i));
        const float x = plot.getX() + (float) (from - first) * stepSeconds / window * plot.getWidth();
        const float y = yFor (level * scale);
        if (c == 0)
            trace.startNewSubPath (x, y);
        else
            trace.lineTo (x, y);
    }
    glowStroke (g, trace, signalTeal, 1.3f);
    if (aligned)
    {
        const float x = plot.getX() + (float) count * stepSeconds / window * plot.getWidth();
        g.setColour (playheadCyan);
        g.fillRect (x - 0.5f, plot.getY(), 1.5f, plot.getHeight());
    }
}

// --- PITCH -------------------------------------------------------------------------------

void drawPitch (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const int scale = juce::roundToInt (live (silt, "scale")), key = juce::roundToInt (live (silt, "key"));
    const float random = live (silt, "randPitch"), depth = live (silt, "patternDepth");
    const int steps = juce::roundToInt (live (silt, "patternSteps"));
    auto plot = frame (g, area, "PITCH - " + text (silt, "scale", (float) scale) + "  KEY " + text (silt, "key", (float) key)
                                    + (random > 0.05f ? "  RND " + text (silt, "randPitch", random) : juce::String()));

    // A keyboard: in-scale notes lit, the key brightest.
    auto keys = plot.removeFromTop (plot.getHeight() * 0.45f);
    const auto mask = silt::scaleMask ((silt::Scale) juce::jlimit (0, silt::scaleCount - 1, scale));
    const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float w = keys.getWidth() / 12.0f;
    g.setFont (monoFont (8.5f));
    for (int note = 0; note < 12; ++note)
    {
        const auto cell = juce::Rectangle<float> (keys.getX() + w * (float) note, keys.getY(), w - 2.0f, keys.getHeight());
        const bool in = scale == 0 || (mask >> ((note - key + 12) % 12) & 1u) != 0;
        g.setColour (note == key && scale != 0 ? screenLime : in ? screenPale.withAlpha (0.75f) : screenLight.withAlpha (0.18f));
        g.fillRect (cell);
        g.setColour (insetBack);
        g.drawText (names[note], cell, juce::Justification::centred);
    }

    // The pattern: one bar per step, up or down by DEPTH.
    plot.removeFromTop (4.0f);
    const float mid = plot.getCentreY();
    g.setColour (screenLight.withAlpha (0.4f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    if (depth < 0.05f)
    {
        g.setColour (screenPale.withAlpha (0.6f));
        g.drawText ("NO PATTERN", plot, juce::Justification::centred);
        return;
    }
    // The engine's pattern for this seed (Engine::process).
    silt::Random r (0x9e3779b97f4a7c15ull ^ (std::uint64_t) juce::roundToInt (live (silt, "patternSeed")));
    const int n = juce::jlimit (1, 16, steps);
    const float bw = plot.getWidth() / (float) n;
    for (int i = 0; i < 16; ++i)
    {
        const float v = r.nextBipolar();
        if (i >= n)
            continue;
        const float h = v * plot.getHeight() * 0.5f * juce::jmin (1.0f, depth / 12.0f);
        g.setColour (juce::Colour (0xffd8693f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX() + bw * (float) i + 1.0f, std::min (mid, mid - h),
                                                                plot.getX() + bw * (float) (i + 1) - 1.0f, std::max (mid, mid - h)));
    }
}

// --- MOD ---------------------------------------------------------------------------------

// Each modulator's envelope stage as the screen last saw it, and when it began.
struct EnvelopeClock { int stage = 0; double since = 0.0; };
std::array<EnvelopeClock, SiltProcessor::numModulators> envelopeClocks {};

void drawMod (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt, const ModRail& rail)
{
    const int m = silt.modShown.load();
    auto p = silt.readModulatorParams (m);
    const char* types[] { "WAVE", "RANDOM", "ENVELOPE", "FOLLOW", "MIDI", "MACRO" };
    juce::String title = "MOD " + juce::String (m + 1) + " - " + types[(int) p.type];
    if (p.type == silt::ModType::wave)
        title << "  PHASE " << juce::roundToInt (p.phase * 360.0f) << juce::String::charToString ((juce::juce_wchar) 0x00b0)
              << "  SYM " << juce::roundToInt (p.symmetry * 100.0f) << "%  CURVE " << juce::roundToInt (p.curve * 100.0f) << "%"
              << (p.retrigger ? "  RETRIG" : "")
              << ((p.sync ? p.reverse : p.rateHz < 0.0f) ? "  BACKWARDS" : (! p.sync && p.rateHz == 0.0f) ? "  STOP" : "");
    auto plot = frame (g, area, title);
    const auto colour = modColour (m).base.brighter (0.25f);
    const bool bipolar = silt::Modulator::isBipolar (p);
    const float baseline = bipolar ? plot.getCentreY() : plot.getBottom();
    g.setColour (screenLight.withAlpha (0.4f));
    g.drawHorizontalLine ((int) baseline, plot.getX(), plot.getRight());

    juce::Path line;
    std::optional<juce::Point<float>> playhead;   // a moving dot for WAVE and ENVELOPE
    const auto add = [&] (int i, int n, float v)
    {
        const float y = bipolar ? baseline - juce::jlimit (-1.0f, 1.0f, v) * plot.getHeight() * 0.5f
                                : baseline - juce::jlimit (0.0f, 1.0f, v) * plot.getHeight();
        const juce::Point<float> pt (plot.getX() + plot.getWidth() * (float) i / (float) n, y);
        if (i == 0)
            line.startNewSubPath (pt);
        else
            line.lineTo (pt);
    };

    if (p.type == silt::ModType::wave)
    {
        // One cycle from PHASE: a copy of the modulator restarted there, then
        // stepped 1/N of a cycle at a time.
        constexpr int n = 160;
        const bool retrigger = p.retrigger;
        silt::Modulator copy;
        copy.reset();
        // Drawn the way it runs: backwards for a negative rate or REV.
        p.rateHz = (p.sync ? p.reverse : p.rateHz < 0.0f) ? -1.0f : 1.0f;
        const double cycleStart = p.phase;
        p.sync = false;
        p.retrigger = true;
        silt::ModContext c;
        c.sampleRate = n;
        c.numSamples = 0;
        c.noteStarted = true;
        const float first = copy.advance (p, c);
        add (0, n, first);
        c.numSamples = 1;
        c.noteStarted = false;
        for (int i = 1; i <= n; ++i)
            add (i, n, copy.advance (p, c));
        // The playhead: where the wave is now, riding the drawn cycle (which
        // starts at PHASE and runs the way the wave does).
        {
            const bool backwards = p.rateHz < 0.0f;
            const double now = silt.modPhases[(size_t) m].load();
            const double along = backwards ? cycleStart - now : now - cycleStart;
            const float x = plot.getX() + plot.getWidth() * (float) (along - std::floor (along));
            const float v = silt.modOutputs[(size_t) m].load();
            const float y = bipolar ? baseline - juce::jlimit (-1.0f, 1.0f, v) * plot.getHeight() * 0.5f
                                    : baseline - juce::jlimit (0.0f, 1.0f, v) * plot.getHeight();
            g.setColour (colour.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            playhead = { x, y };
        }
        // RETRIG: a dot where each note starts the wave.
        if (retrigger)
        {
            const float y = bipolar ? baseline - first * plot.getHeight() * 0.5f : baseline - first * plot.getHeight();
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ plot.getX() + 1.0f, y }));
            g.setColour (insetBack);
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ plot.getX() + 1.0f, y }));
        }
    }
    else if (p.type == silt::ModType::envelope)
    {
        // Attack, decay, a held stretch at sustain, release, on one time scale.
        const float a = p.attackMs, d = p.decayMs, s = p.sustain, rel = p.releaseMs;
        const float hold = 0.25f * (a + d + rel) + 1.0f, total = a + d + hold + rel;
        const auto at = [&] (float ms, float level)
        {
            return juce::Point<float> (plot.getX() + plot.getWidth() * ms / total, plot.getBottom() - level * plot.getHeight());
        };
        line.startNewSubPath (at (0.0f, 0.0f));
        line.lineTo (at (a, 1.0f));
        line.lineTo (at (a + d, s));
        line.lineTo (at (a + d + hold, s));
        line.lineTo (at (total, 0.0f));

        // Where it is now: along its stage by the time since the stage began
        // (the level can't say, as a release can start anywhere and the curves
        // aren't straight), at the height it really has.
        const float level = juce::jlimit (0.0f, 1.0f, silt.modOutputs[(size_t) m].load());
        const int stage = silt.modStages[(size_t) m].load();
        auto& clock = envelopeClocks[(size_t) m];
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (stage != clock.stage)
        {
            clock.stage = stage;
            clock.since = nowMs;
        }
        const float elapsed = (float) (nowMs - clock.since);
        switch (stage)
        {
            case 1: playhead = at (std::min (elapsed, a), level); break;                                           // attack
            case 2: playhead = at (a + std::min (elapsed, d), level); break;                                       // decay
            case 3: playhead = at (a + d + hold * 0.5f * std::min (1.0f, elapsed / 300.0f), level); break;         // sustain: settles mid-hold
            case 4: playhead = at (a + d + hold + std::min (elapsed, rel), level); break;                          // release
            default: break;
        }
    }
    else
    {
        // The rest: what it's been doing, from the rail chip's trace.
        const auto& chip = rail.chip (m);
        const auto& history = chip.getHistory();
        const int n = (int) history.size();
        for (int i = 0; i < n; ++i)
            add (i, n - 1, history[(size_t) ((chip.getHistoryStart() + i) % n)]);
    }
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (playhead.has_value())
    {
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (*playhead));
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (*playhead), 1.5f);
    }
}
} // namespace

bool spaceTimelineMoves (const SiltProcessor& silt)
{
    return live (silt, "spaceReverb") > 0.0f || live (silt, "spaceDelay") > 0.0f;
}

bool drawSpaceTimeline (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    drawSpaceTimelineImpl (g, area, silt);
    return true;
}

bool pageHasInset (int page)
{
    return page == layout::filter || page == layout::resonator || page == layout::color || page == layout::space
        || page == layout::pitch || page == layout::mod;
}

bool drawPageInset (juce::Graphics& g, juce::Rectangle<float> area, int page, const SiltProcessor& silt, const ModRail& rail)
{
    switch (page)
    {
        case layout::filter:    drawFilter (g, area, silt); return true;
        case layout::resonator: drawResonator (g, area, silt); return true;
        case layout::color:     drawColor (g, area, silt); return true;
        case layout::space:     drawSpace (g, area, silt); return true;
        case layout::pitch:     drawPitch (g, area, silt); return true;
        case layout::mod:       drawMod (g, area, silt, rail); return true;
        default:                return false;
    }
}

std::vector<float> insetKey (int page, const SiltProcessor& silt, const ModRail& rail)
{
    std::vector<float> key { (float) page };
    const auto add = [&] (std::initializer_list<const char*> ids)
    {
        for (const auto* id : ids)
            key.push_back (live (silt, id));
    };
    switch (page)
    {
        case layout::filter:    add ({ "filterMode", "filterCutoff", "filterReso", "filterMorph", "filterMix", "filterEnv" }); break;
        case layout::resonator:
            add ({ "resTune", "resNotes", "resRange", "resDecay", "resBright", "resMix", "scale", "key",
                   "resReso", "resBands", "resDrift", "resSpread", "resOddEven", "resDamp" });
            key.push_back ((float) silt.resonatorHalf.load());
            add ({ "resWidth", "resDrive" });
            if (const auto& scope = liveScope (silt); scope.sounding)
                key.push_back ((float) scope.count);   // the lit bands follow the sound
            if (live (silt, "resDrift") > 0.0f || live (silt, "resDrive") > 0.0f || resonatorRinging)
                key.push_back (animationSeconds());    // DRIFT and DRIVE move, struck bands ring down
            break;
        case layout::color:
            add ({ "colorDrive", "colorBits", "colorTone", "colorRate", "colorNoise", "colorComp", "colorWow" });
            // The live plot follows the sound; quiet, the sine rides the curve (and WOW wobbles).
            key.push_back (liveScope (silt).sounding ? (float) liveScope (silt).count : animationSeconds());
            break;
        case layout::space:     add ({ "spaceTime", "spaceSync", "spaceDiv", "spaceFeedback", "spaceDelay", "spaceReverb", "spaceDecay", "spaceSize", "spaceMode", "spaceTone", "spaceShimmer", "spaceFreeze", "spaceDuck" }); break;
        case layout::pitch:     add ({ "scale", "key", "randPitch", "patternDepth", "patternSteps", "patternSeed" }); break;
        case layout::mod:
        {
            const int m = silt.modShown.load();
            const auto p = silt.readModulatorParams (m);
            key.insert (key.end(), { (float) m, (float) p.type, p.shape, p.symmetry, p.curve, p.phase, p.retrigger ? 1.0f : 0.0f,
                                     p.rateHz < 0.0f ? -1.0f : p.rateHz > 0.0f ? 1.0f : 0.0f, p.reverse ? 1.0f : 0.0f, p.sync ? 1.0f : 0.0f,
                                     p.attackMs, p.decayMs, p.sustain, p.releaseMs });
            if (p.type != silt::ModType::wave && p.type != silt::ModType::envelope)
                key.push_back ((float) rail.chip (m).getHistoryStart());   // the trace moves on
            else
                key.insert (key.end(), { silt.modOutputs[(size_t) m].load(), silt.modPhases[(size_t) m].load() });   // the dot moves on
            if (p.type == silt::ModType::envelope && silt.modStages[(size_t) m].load() != 0)
                key.push_back (animationSeconds());   // the dot travels through each stage in time
            break;
        }
        default: break;
    }
    return key;
}

} // namespace silt::ui
