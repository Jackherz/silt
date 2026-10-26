#include "ScreenInsets.h"
#include "ModColours.h"
#include "PageLayout.h"
#include "Palette.h"

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
    const float mix = live (silt, "resMix");
    const char* notes[] { "SCALE", "HARMONICS", "BELL" };
    auto plot = frame (g, area, "RESONATOR - " + juce::String (notes[(int) p.notes]) + " " + juce::String (p.rangeOctaves, 1) + " OCT "
                                    + text (silt, "resDecay", p.decaySeconds) + (mix <= 0.0f ? "  (MIX 0)" : ""));

    std::array<double, silt::Resonator::maxBands> ratios {};
    const int n = silt::Resonator::bandRatios (p, ratios);
    const double tilt = 1.0 - p.bright, top = std::pow (2.0, std::max (0.5, (double) p.rangeOctaves));
    g.setColour (screenLight.withAlpha (0.35f));
    g.drawHorizontalLine ((int) plot.getBottom() - 1, plot.getX(), plot.getRight());
    const float alpha = mix <= 0.0f ? 0.3f : 1.0f;
    for (int i = 0; i < n; ++i)
    {
        // Across: pitch (octaves above the lowest band). Height: level. Width: ring time.
        const double ratio = ratios[(size_t) i];
        const float x = plot.getX() + 4.0f + (plot.getWidth() - 8.0f) * (float) (std::log2 (ratio) / std::log2 (top));
        const float level = (float) std::pow (ratio, -0.75 * tilt);
        const float ring = (float) std::pow (ratio, -0.7 * tilt);
        const float width = 2.0f + 6.0f * ring;
        const float h = (plot.getHeight() - 2.0f) * level;
        g.setColour (screenLime.withAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (width, h).withCentre ({ x, plot.getBottom() - 1.0f - h * 0.5f }));
    }
}

// --- COLOR -------------------------------------------------------------------------------

void drawColor (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const float drive = live (silt, "colorDrive") * 0.01f, bits = live (silt, "colorBits");
    auto plot = frame (g, area, "COLOR - DRIVE " + juce::String (juce::roundToInt (drive * 100.0f)) + "%  " + text (silt, "colorBits", bits)
                                    + "  " + text (silt, "colorTone", live (silt, "colorTone")));
    plot = plot.withSizeKeepingCentre (juce::jmin (plot.getWidth(), plot.getHeight() * 2.4f), plot.getHeight());
    g.setColour (screenLight.withAlpha (0.4f));
    g.drawRect (plot, 1.0f);
    g.drawLine (juce::Line<float> (plot.getBottomLeft(), plot.getTopRight()), 0.8f);

    // The drive curve as silt::Color shapes it, then the bit steps.
    const float gain = std::pow (10.0f, 30.0f * drive / 20.0f), bias = 0.2f * drive;
    const float makeup = 1.0f / std::pow (gain, 0.8f), blend = std::min (1.0f, drive * 10.0f);
    const float steps = bits < 15.995f ? std::pow (2.0f, bits - 1.0f) : 0.0f;
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 200.0f;
        float y = x + blend * ((std::tanh (gain * x + bias) - std::tanh (bias)) * makeup - x);
        if (steps > 0.0f)
            y = std::round (y * steps) / steps;
        const juce::Point<float> p (plot.getX() + plot.getWidth() * (float) i / 200.0f,
                                    plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * plot.getHeight() * 0.5f);
        if (i == 0)
            curve.startNewSubPath (p);
        else
            curve.lineTo (p);
    }
    g.setColour (screenLime);
    g.strokePath (curve, juce::PathStrokeType (1.5f));
}

// --- SPACE -------------------------------------------------------------------------------

void drawSpace (juce::Graphics& g, juce::Rectangle<float> area, const SiltProcessor& silt)
{
    const bool synced = live (silt, "spaceSync") > 0.5f;
    const float delayMs = synced ? (float) (SiltProcessor::divisionQuarters (juce::roundToInt (live (silt, "spaceDiv")))
                                            * 60000.0 / juce::jlimit (20.0, 400.0, silt.hostBpm.load()))
                                 : live (silt, "spaceTime");
    const float feedback = live (silt, "spaceFeedback") * 0.01f, delayLevel = live (silt, "spaceDelay") * 0.01f;
    const float reverbLevel = live (silt, "spaceReverb") * 0.01f, decay = live (silt, "spaceDecay"), size = live (silt, "spaceSize") * 0.01f;
    const bool pingPong = juce::roundToInt (live (silt, "spaceMode")) == 0;
    auto plot = frame (g, area, "SPACE - DELAY " + (synced ? text (silt, "spaceDiv", live (silt, "spaceDiv")) : text (silt, "spaceTime", delayMs))
                                    + "  FB " + juce::String (juce::roundToInt (feedback * 100.0f)) + "%  SIZE " + juce::String (juce::roundToInt (size * 100.0f)) + "%");

    const float window = juce::jlimit (0.4f, 6.0f, std::max (4.5f * delayMs / 1000.0f, std::min (decay, 6.0f) * 0.6f));
    const auto xFor = [&] (float seconds) { return plot.getX() + plot.getWidth() * seconds / window; };
    const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f - 1.0f;
    g.setColour (screenLight.withAlpha (0.5f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

    // The reverb tail: from the pre-delay, falling 60 dB over DECAY.
    const float preDelay = 0.035f * size;
    juce::Path tail;
    tail.startNewSubPath (xFor (preDelay), mid);
    for (int i = 0; i <= 80; ++i)
    {
        const float t = preDelay + (window - preDelay) * (float) i / 80.0f;
        tail.lineTo (xFor (t), mid - half * std::pow (10.0f, -3.0f * (t - preDelay) / std::max (0.1f, decay)));
    }
    for (int i = 80; i >= 0; --i)
    {
        const float t = preDelay + (window - preDelay) * (float) i / 80.0f;
        tail.lineTo (xFor (t), mid + half * std::pow (10.0f, -3.0f * (t - preDelay) / std::max (0.1f, decay)));
    }
    tail.closeSubPath();
    g.setColour (screenPale.withAlpha (reverbLevel > 0.0f ? 0.25f + 0.45f * reverbLevel : 0.08f));
    g.fillPath (tail);

    // The taps: each repeat FEEDBACK quieter; ping-pong alternates sides.
    g.setColour (juce::Colour (0xffd8693f).withAlpha (delayLevel > 0.0f ? 1.0f : 0.3f));
    g.setFont (monoFont (8.5f));
    float level = 1.0f;
    for (int n = 1; level > 0.02f && n < 40; ++n)
    {
        const float t = n * delayMs / 1000.0f;
        if (t > window)
            break;
        const float h = half * level;
        const bool left = ! pingPong || n % 2 == 1, right = ! pingPong || n % 2 == 0;
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (t) - 1.5f, left ? mid - h : mid, xFor (t) + 1.5f, right ? mid + h : mid));
        level *= feedback;
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
        title << "  SYM " << juce::roundToInt (p.symmetry * 100.0f) << "%  CURVE " << juce::roundToInt (p.curve * 100.0f) << "%";
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
        // One cycle: a copy of the modulator stepped 1/N of a cycle at a time.
        constexpr int n = 160;
        silt::Modulator copy;
        copy.reset();
        p.sync = p.retrigger = false;
        p.rateHz = 1.0f;
        silt::ModContext c;
        c.sampleRate = n;
        c.numSamples = 1;
        for (int i = 0; i <= n; ++i)
            add (i, n, copy.advance (p, c));
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
        case layout::filter:    add ({ "filterMode", "filterCutoff", "filterReso", "filterMorph", "filterMix" }); break;
        case layout::resonator: add ({ "resTune", "resNotes", "resRange", "resDecay", "resBright", "resMix", "scale", "key" }); break;
        case layout::color:     add ({ "colorDrive", "colorBits", "colorTone" }); break;
        case layout::space:     add ({ "spaceTime", "spaceSync", "spaceDiv", "spaceFeedback", "spaceDelay", "spaceReverb", "spaceDecay", "spaceSize", "spaceMode" }); break;
        case layout::pitch:     add ({ "scale", "key", "randPitch", "patternDepth", "patternSteps", "patternSeed" }); break;
        case layout::mod:
        {
            const int m = silt.modShown.load();
            const auto p = silt.readModulatorParams (m);
            key.insert (key.end(), { (float) m, (float) p.type, p.shape, p.symmetry, p.curve, p.attackMs, p.decayMs, p.sustain, p.releaseMs });
            if (p.type != silt::ModType::wave && p.type != silt::ModType::envelope)
                key.push_back ((float) rail.chip (m).getHistoryStart());   // the trace moves on
            break;
        }
        default: break;
    }
    return key;
}

} // namespace silt::ui
