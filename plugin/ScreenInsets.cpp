#include "ScreenInsets.h"
#include "ModColours.h"
#include "PageLayout.h"
#include "Palette.h"

#include <silt/Color.h>
#include <silt/Resonator.h>

#include <complex>

namespace silt::ui
{

namespace
{
namespace layout = silt::layout;
using Complex = std::complex<double>;

const juce::Colour insetBack { 0xff1b2152 };

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

// The frame: a dark panel, a title, and the drawing area under it.
juce::Rectangle<float> frame (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
{
    auto r = area.reduced (1.0f);
    g.setColour (insetBack);
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
    g.setColour (juce::Colour (0xffd8693f));
    g.strokePath (curve, juce::PathStrokeType (1.6f));
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
    p.notes = (silt::ResonatorNotes) juce::jlimit (0, 2, juce::roundToInt (live (silt, "resNotes")));
    p.scale = (silt::Scale) juce::jlimit (0, silt::scaleCount - 1, juce::roundToInt (live (silt, "scale")));
    p.key = juce::roundToInt (live (silt, "key"));
    p.rangeOctaves = live (silt, "resRange");
    p.decaySeconds = live (silt, "resDecay");
    p.bright = live (silt, "resBright") * 0.01f;
    const float mix = live (silt, "resMix") * 0.01f;
    const char* notes[] { "SCALE", "HARMONICS", "BELL" };
    auto plot = frame (g, area, "RESONATOR - " + juce::String (notes[(int) p.notes]) + " " + juce::String (p.rangeOctaves, 1) + " OCT "
                                    + text (silt, "resDecay", p.decaySeconds) + "  MIX " + juce::String (juce::roundToInt (mix * 100.0f)) + "%");
    g.setColour (screenLight.withAlpha (0.35f));
    g.drawHorizontalLine ((int) plot.getBottom() - 1, plot.getX(), plot.getRight());
    if (mix <= 0.0f)
        return;   // nothing rings: nothing drawn

    std::array<double, silt::Resonator::maxBands> ratios {};
    const int n = silt::Resonator::bandRatios (p, ratios);
    if (n == 0)
        return;
    // Across: pitch, spanning exactly the bands (a snapped root can sit just under
    // TUNE). Height: level. Width: ring time. They fade in with MIX.
    const double lo = std::min (1.0, ratios[0]), hi = std::max ({ std::pow (2.0, (double) p.rangeOctaves), ratios[(size_t) n - 1], lo * 1.01 });
    const double tilt = 1.0 - p.bright;
    const auto bars = plot.reduced (6.0f, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double ratio = ratios[(size_t) i];
        const float x = bars.getX() + bars.getWidth() * (float) (std::log2 (ratio / lo) / std::log2 (hi / lo));
        const float level = (float) std::pow (ratio, -0.75 * tilt);
        const float ring = (float) std::pow (ratio, -0.7 * tilt);
        const float width = 2.0f + 5.0f * juce::jlimit (0.0f, 1.0f, ring);
        const float h = (bars.getHeight() - 2.0f) * juce::jlimit (0.0f, 1.0f, level);
        g.setColour (screenLime.withAlpha (mix));
        g.fillRect (juce::Rectangle<float> (width, h).withCentre ({ juce::jlimit (bars.getX() + width * 0.5f, bars.getRight() - width * 0.5f, x),
                                                                     bars.getBottom() - 1.0f - h * 0.5f }));
    }
}

// --- COLOR -------------------------------------------------------------------------------

// Left: the level curve (DRIVE's saturation, BITS' steps). Right: what it does
// across frequency: TONE's tilt (wobbling with WOW), RATE's fold point with
// the aliased band shaded, NOISE as a hiss floor, COMP as a squeeze bar.
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

    // The level curve, in a square on the left.
    auto curveBox = plot.removeFromLeft (juce::jmin (plot.getHeight(), plot.getWidth() * 0.32f));
    plot.removeFromLeft (8.0f);
    g.setColour (screenLight.withAlpha (0.4f));
    g.drawRect (curveBox, 1.0f);
    g.drawLine (juce::Line<float> (curveBox.getBottomLeft(), curveBox.getTopRight()), 0.8f);
    const float gain = std::pow (10.0f, 30.0f * drive / 20.0f), bias = 0.2f * drive;
    const float makeup = 1.0f / std::pow (gain, 0.8f), blend = std::min (1.0f, drive * 10.0f);
    const float steps = bits < 15.995f ? std::pow (2.0f, bits - 1.0f) : 0.0f;
    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 120.0f;
        float y = x + blend * ((std::tanh (gain * x + bias) - std::tanh (bias)) * makeup - x);
        if (steps > 0.0f)
            y = std::round (y * steps) / steps;
        const juce::Point<float> pt (curveBox.getX() + curveBox.getWidth() * (float) i / 120.0f,
                                     curveBox.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * curveBox.getHeight() * 0.5f);
        if (i == 0)
            curve.startNewSubPath (pt);
        else
            curve.lineTo (pt);
    }
    g.setColour (screenLime);
    g.strokePath (curve, juce::PathStrokeType (1.4f));

    // Across frequency (log, 20 Hz–20 kHz; ±9 dB around the middle line).
    const auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log10 (hz / 20.0) / 3.0); };
    const float mid = plot.getY() + plot.getHeight() * 0.4f;
    const auto yFor = [&] (double db) { return mid - (float) db / 9.0f * plot.getHeight() * 0.4f; };
    g.setColour (screenLight.withAlpha (0.35f));
    for (const double hz : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) xFor (hz), plot.getY(), plot.getBottom());
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

    // NOISE: a hiss floor along the bottom, taller with more noise.
    if (noise > 0.0f)
    {
        juce::Random r (17);
        const float h = plot.getHeight() * 0.3f * noise * noise + 1.0f;
        g.setColour (screenPale.withAlpha (0.25f + 0.4f * noise));
        for (float x = plot.getX(); x < plot.getRight(); x += 2.0f)
            g.fillRect (x, plot.getBottom() - h * (0.4f + 0.6f * r.nextFloat()), 1.2f, h);
    }

    // RATE: everything above half the hold rate folds back; shade it.
    if (rate < silt::Color::rateOff - 1.0f)
    {
        const float fold = xFor (juce::jlimit (20.0, 20000.0, (double) rate * 0.5));
        g.setColour (signalRed.withAlpha (0.28f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fold, plot.getY(), plot.getRight(), plot.getBottom()));
        g.setColour (signalRed.brighter (0.3f));
        g.drawVerticalLine ((int) fold, plot.getY(), plot.getBottom());
    }

    // TONE: the tilt round 800 Hz, as silt::Color's one-pole split does it; WOW
    // wobbles the line.
    const double high = std::pow (10.0, 6.0 * tone / 20.0), low = 1.0 / high;
    juce::Path tilt;
    for (int i = 0; i <= 100; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 100.0);
        const Complex jw (0.0, hz / 800.0);
        const double db = 20.0 * std::log10 (std::abs ((low + high * jw) / (1.0 + jw)));
        const float y = yFor (db) + wow * 3.0f * (float) std::sin (i * 0.55);
        if (i == 0)
            tilt.startNewSubPath (xFor (hz), y);
        else
            tilt.lineTo (xFor (hz), y);
    }
    g.setColour (juce::Colour (0xffd8693f));
    g.strokePath (tilt, juce::PathStrokeType (1.6f));

    // COMP: a squeeze bar on the right edge.
    if (comp > 0.0f)
    {
        const auto bar = juce::Rectangle<float> (plot.getRight() - 5.0f, plot.getY(), 4.0f, plot.getHeight() * 0.85f * comp);
        g.setColour (screenLime.withAlpha (0.8f));
        g.fillRect (bar);
    }
}

// --- SPACE -------------------------------------------------------------------------------

// The delay's taps and, since the repeats feed the reverb, a tail after the dry
// sound and after each repeat. DELAY and REVERB fade them in (gone at 0); TONE
// tints the tails, SHIMMER sparkles above them, FREEZE holds them.
void drawSpace (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const bool synced = live (silt, "spaceSync") > 0.5f;
    const float delayMs = synced ? (float) (SiltProcessor::divisionQuarters (juce::roundToInt (live (silt, "spaceDiv")))
                                            * 60000.0 / juce::jlimit (20.0, 400.0, silt.hostBpm.load()))
                                 : live (silt, "spaceTime");
    const float feedback = live (silt, "spaceFeedback") * 0.01f, delayLevel = live (silt, "spaceDelay") * 0.01f;
    const float reverbLevel = live (silt, "spaceReverb") * 0.01f, decay = std::max (0.1f, live (silt, "spaceDecay")), size = live (silt, "spaceSize") * 0.01f;
    const float tone = live (silt, "spaceTone") * 0.01f, shimmer = live (silt, "spaceShimmer") * 0.01f;
    const bool frozen = live (silt, "spaceFreeze") > 0.5f, pingPong = juce::roundToInt (live (silt, "spaceMode")) == 0;
    auto plot = frame (g, area, "SPACE - DELAY " + (synced ? text (silt, "spaceDiv", live (silt, "spaceDiv")) : text (silt, "spaceTime", delayMs))
                                    + "  FB " + juce::String (juce::roundToInt (feedback * 100.0f)) + "%  DECAY " + text (silt, "spaceDecay", decay)
                                    + (frozen ? "  FROZEN" : ""));

    // The repeats worth showing: until they're 5% of the first, at most eight.
    const float delay = delayMs / 1000.0f;
    std::vector<float> taps;
    for (float level = 1.0f; level > 0.05f && taps.size() < 8; level *= feedback)
        taps.push_back (level);
    const float window = juce::jlimit (0.4f, 8.0f, std::max (delay * ((float) std::min<size_t> (taps.size(), 5) + 0.7f), std::min (decay * 0.8f, 8.0f)));
    const auto xFor = [&] (float seconds) { return plot.getX() + plot.getWidth() * seconds / window; };
    const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f - 1.0f;
    g.setColour (screenLight.withAlpha (0.5f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    g.setFont (monoFont (8.5f));
    g.setColour (screenPale.withAlpha (0.6f));
    g.drawText (juce::String (window, window < 1.0f ? 2 : 1) + " S", plot.removeFromBottom (10.0f).removeFromRight (40.0f), juce::Justification::centredRight);

    // Tails: from the pre-delay, falling 60 dB over DECAY (or held, frozen).
    const auto tailColour = screenLight.interpolatedWith (juce::Colours::white, tone);
    const float preDelay = 0.035f * size;
    const auto drawTail = [&] (float start, float amount)
    {
        if (amount <= 0.0f || start + preDelay >= window)
            return;
        juce::Path tail;
        const float t0 = start + preDelay;
        const auto envelope = [&] (float t) { return frozen ? 1.0f : std::pow (10.0f, -3.0f * (t - t0) / decay); };
        tail.startNewSubPath (xFor (t0), mid);
        for (int i = 0; i <= 60; ++i)
        {
            const float t = t0 + (window - t0) * (float) i / 60.0f;
            tail.lineTo (xFor (t), mid - half * amount * envelope (t));
        }
        for (int i = 60; i >= 0; --i)
        {
            const float t = t0 + (window - t0) * (float) i / 60.0f;
            tail.lineTo (xFor (t), mid + half * amount * envelope (t));
        }
        tail.closeSubPath();
        g.setColour (tailColour.withAlpha (0.18f + 0.5f * reverbLevel));
        g.fillPath (tail);
    };
    if (reverbLevel > 0.0f)
    {
        drawTail (0.0f, reverbLevel);
        for (size_t n = 0; n < taps.size(); ++n)
            drawTail (delay * (float) (n + 1), reverbLevel * delayLevel * taps[n]);

        // SHIMMER: sparks above the first tail, an octave's glitter.
        if (shimmer > 0.0f)
        {
            juce::Random r (5);
            g.setColour (juce::Colours::white.withAlpha (0.3f + 0.6f * shimmer));
            for (int i = 0; i < (int) (40.0f * shimmer); ++i)
            {
                const float t = preDelay + r.nextFloat() * std::min (window, decay);
                const float top = half * reverbLevel * (frozen ? 1.0f : std::pow (10.0f, -3.0f * t / decay));
                g.fillRect (xFor (t), mid - top - 2.0f - r.nextFloat() * half * 0.35f * shimmer, 1.5f, 1.5f);
            }
        }
    }

    // Taps: each repeat FEEDBACK quieter; ping-pong alternates sides.
    if (delayLevel > 0.0f)
    {
        g.setColour (juce::Colour (0xffd8693f).withAlpha (delayLevel));
        for (size_t n = 0; n < taps.size(); ++n)
        {
            const float t = delay * (float) (n + 1);
            if (t > window)
                break;
            const float h = half * taps[n];
            const bool left = ! pingPong || n % 2 == 0, right = ! pingPong || n % 2 == 1;
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (t) - 1.5f, left ? mid - h : mid, xFor (t) + 1.5f, right ? mid + h : mid));
        }
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

void drawMod (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt, const ModRail& rail)
{
    const int m = silt.modShown.load();
    auto p = silt.readModulatorParams (m);
    const char* types[] { "WAVE", "RANDOM", "ENVELOPE", "FOLLOW", "MIDI", "MACRO" };
    juce::String title = "MOD " + juce::String (m + 1) + " - " + types[(int) p.type];
    if (p.type == silt::ModType::wave)
        title << "  PHASE " << juce::roundToInt (p.phase * 360.0f) << juce::String::charToString ((juce::juce_wchar) 0x00b0)
              << "  SYM " << juce::roundToInt (p.symmetry * 100.0f) << "%  CURVE " << juce::roundToInt (p.curve * 100.0f) << "%"
              << (p.retrigger ? "  RETRIG" : "");
    auto plot = frame (g, area, title);
    const auto colour = modColour (m).base.brighter (0.25f);
    const bool bipolar = silt::Modulator::isBipolar (p);
    const float baseline = bipolar ? plot.getCentreY() : plot.getBottom();
    g.setColour (screenLight.withAlpha (0.4f));
    g.drawHorizontalLine ((int) baseline, plot.getX(), plot.getRight());

    juce::Path line;
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
        p.sync = false;
        p.retrigger = true;
        p.rateHz = 1.0f;
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
}
} // namespace

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
        case layout::resonator: add ({ "resTune", "resNotes", "resRange", "resDecay", "resBright", "resMix", "scale", "key" }); break;
        case layout::color:     add ({ "colorDrive", "colorBits", "colorTone", "colorRate", "colorNoise", "colorComp", "colorWow" }); break;
        case layout::space:     add ({ "spaceTime", "spaceSync", "spaceDiv", "spaceFeedback", "spaceDelay", "spaceReverb", "spaceDecay", "spaceSize", "spaceMode", "spaceTone", "spaceShimmer", "spaceFreeze" }); break;
        case layout::pitch:     add ({ "scale", "key", "randPitch", "patternDepth", "patternSteps", "patternSeed" }); break;
        case layout::mod:
        {
            const int m = silt.modShown.load();
            const auto p = silt.readModulatorParams (m);
            key.insert (key.end(), { (float) m, (float) p.type, p.shape, p.symmetry, p.curve, p.phase, p.retrigger ? 1.0f : 0.0f,
                                     p.attackMs, p.decayMs, p.sustain, p.releaseMs });
            if (p.type != silt::ModType::wave && p.type != silt::ModType::envelope)
                key.push_back ((float) rail.chip (m).getHistoryStart());   // the trace moves on
            break;
        }
        default: break;
    }
    return key;
}

} // namespace silt::ui
