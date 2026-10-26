#pragma once

#include "Palette.h"
#include "PluginProcessor.h"

namespace silt::ui
{

// The screen: the sample (cached, redrawn only when it or the size changes),
// the region, a playhead per voice, and every sounding grain twice: as a line
// on the waveform where it's reading, and as a bar in the grain roll below.
// The roll's two axes keep Spray and Random pan apart: left to right is where
// in the sample a grain plays (spray scatters it sideways), top to bottom is
// where it sits in the stereo field (random pan spreads it up and down).
class GrainScreen final : public juce::Component
{
public:
    void setOverview (SiltProcessor::Overview o)
    {
        overview = std::move (o);
        waveCache = {};
        repaint();
    }

    void setRegion (float s, float len)
    {
        if (! juce::approximatelyEqual (s, start) || ! juce::approximatelyEqual (len, length))
        {
            start = s;
            length = len;
            repaint();
        }
    }

    // Returns true if anything moved, so the caller only repaints then.
    bool setActivity (const std::array<silt::GrainView, SiltProcessor::maxGrainViews>& v, int n,
                      const std::array<float, silt::Engine::numVoices>& h, const juce::String& info)
    {
        const bool changed = n > 0 || numViews > 0 || h != heads || info != hud;
        if (n > 0)
            std::copy (v.begin(), v.begin() + n, views.begin());
        numViews = n;
        heads = h;
        hud = info;
        return changed;
    }

    // Live mode: the ring's peaks by position and the write head, drawn as a
    // scrolling view with the newest audio at the right edge.
    void setLive (bool isLive, bool isFrozen, float writeHead, const std::array<float, SiltProcessor::liveColumns>& peaks)
    {
        live = isLive;
        frozen = isFrozen;
        writeShare = writeHead;
        livePeaks = peaks;
    }

    void setTransientsShown (bool shown)
    {
        if (shown != transientsShown)
        {
            transientsShown = shown;
            repaint();
        }
    }

    void resized() override { waveCache = {}; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (ink);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        r.reduce (8, 8);
        g.setColour (screenLight);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        r.reduce (12, 8);
        g.setColour (screenBlue);
        g.fillRect (r);

        auto inner = r.reduced (10, 6);
        auto top = inner.removeFromTop (16);
        auto bottom = inner.removeFromBottom (16);
        inner.removeFromBottom (4);
        const auto rollArea = inner.removeFromBottom (juce::roundToInt ((float) inner.getHeight() * 0.36f));
        inner.removeFromBottom (6);
        const auto waveArea = inner;

        g.setFont (monoFont (12.0f));
        g.setColour (juce::Colours::white);

        if (live)
        {
            g.drawText (frozen ? "LIVE INPUT  FROZEN" : "LIVE INPUT", top, juce::Justification::centredLeft);
            g.drawText (juce::String (silt::Engine::ringSeconds, 0) + " S  NOW >", top, juce::Justification::centredRight);
            drawLiveWave (g, waveArea);
            g.setColour (screenLight.withAlpha (0.5f));
            g.drawHorizontalLine (rollArea.getY() - 3, (float) rollArea.getX(), (float) rollArea.getRight());
            drawGrains (g, waveArea, rollArea);
            g.setColour (juce::Colours::white);
            g.drawText (numViews > 0 ? hud : juce::String ("READY."), bottom, juce::Justification::centredLeft);
            return;
        }

        if (overview.minimum.empty())
        {
            g.drawText ("**** NO SAMPLE ****", top, juce::Justification::centred);
            g.setColour (screenPale);
            g.drawText ("DROP AN AUDIO FILE HERE, PRESS LOAD, OR ARM AND RECORD", waveArea, juce::Justification::centred);
            g.setColour (juce::Colours::white);
            g.drawText ("READY.", bottom, juce::Justification::centredLeft);
            return;
        }

        g.drawText (overview.name.toUpperCase(), top, juce::Justification::centredLeft);
        g.drawText (juce::String (overview.seconds, 2) + " S", top, juce::Justification::centredRight);

        drawWave (g, waveArea);
        if (transientsShown)
        {
            // The transients SNAP can land on: ticks along the top of the waveform.
            g.setColour (screenLime.withAlpha (0.8f));
            for (float t : overview.transients)
            {
                const float x = (float) waveArea.getX() + t * (float) waveArea.getWidth();
                g.fillRect (x, (float) waveArea.getY(), 1.0f, 7.0f);
            }
        }
        drawRegion (g, waveArea, rollArea);
        drawGrains (g, waveArea, rollArea);

        g.setColour (juce::Colours::white);
        g.drawText (numViews > 0 ? hud : juce::String ("READY."), bottom, juce::Justification::centredLeft);
    }

private:
    void drawWave (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (! waveCache.isValid() || waveCache.getWidth() != area.getWidth() || waveCache.getHeight() != area.getHeight())
        {
            waveCache = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
            juce::Graphics c (waveCache);
            const float mid = (float) area.getHeight() * 0.5f, half = (float) area.getHeight() * 0.48f;
            const int cols = (int) overview.minimum.size();
            c.setColour (screenPale);
            for (int x = 0; x < area.getWidth(); ++x)
            {
                const auto i = (size_t) juce::jlimit (0, cols - 1, x * cols / juce::jmax (1, area.getWidth()));
                const float top = mid - overview.maximum[i] * half;
                const float bottom = mid - overview.minimum[i] * half;
                c.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
            }
        }
        g.drawImageAt (waveCache, area.getX(), area.getY());
    }

    void drawLiveWave (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const float mid = (float) area.getCentreY(), half = (float) area.getHeight() * 0.48f;
        g.setColour (frozen ? playheadCyan.withAlpha (0.8f) : screenPale);
        for (int x = 0; x < area.getWidth(); ++x)
        {
            const float share = shareAt ((float) x / (float) area.getWidth());
            const auto column = (size_t) juce::jlimit (0, SiltProcessor::liveColumns - 1, (int) (share * SiltProcessor::liveColumns));
            const float h = juce::jmin (1.0f, livePeaks[column]) * half;
            g.fillRect ((float) (area.getX() + x), mid - h, 1.0f, juce::jmax (1.0f, 2.0f * h));
        }
        g.setColour (screenLime);
        g.drawVerticalLine (area.getRight() - 1, (float) area.getY(), (float) area.getBottom());
    }

    // Live view: x (0 = oldest, 1 = newest) to a share of the ring, and back.
    float shareAt (float x) const
    {
        const float s = writeShare - (1.0f - x);
        return s - std::floor (s);
    }
    float xOf (float share) const
    {
        const float age = writeShare - share;
        return 1.0f - (age - std::floor (age));
    }

    void drawRegion (juce::Graphics& g, juce::Rectangle<int> wave, juce::Rectangle<int> roll)
    {
        // Dim what's outside the region, on both the waveform and the roll.
        const auto x0 = (float) wave.getX() + start * (float) wave.getWidth();
        const auto x1 = juce::jmin ((float) wave.getRight(), x0 + length * (float) wave.getWidth());
        const auto full = wave.getUnion (roll).toFloat();
        g.setColour (screenBlue.withAlpha (0.72f));
        g.fillRect (full.withRight (x0));
        g.fillRect (full.withLeft (x1));

        g.setColour (screenLime);
        g.drawVerticalLine ((int) x0, full.getY(), full.getBottom());
        g.drawVerticalLine ((int) x1 - 1, full.getY(), full.getBottom());

        g.setColour (screenLight.withAlpha (0.5f));
        g.drawHorizontalLine (roll.getY() - 3, full.getX(), full.getRight());
    }

    void drawGrains (juce::Graphics& g, juce::Rectangle<int> wave, juce::Rectangle<int> roll)
    {
        const float w = (float) wave.getWidth();
        const float mid = (float) wave.getCentreY(), half = (float) wave.getHeight() * 0.5f;
        constexpr int lanes = 16;
        const float laneH = (float) roll.getHeight() / lanes;
        const float rollTop = (float) roll.getY() + laneH * 1.5f, rollBottom = (float) roll.getBottom() - laneH * 1.5f;
        const auto panToY = [=] (float pan) { return juce::jmap (pan, -1.0f, 1.0f, rollTop, rollBottom); };

        // Stereo guides: L at the top, centre, R at the bottom.
        g.setFont (monoFont (10.0f));
        g.setColour (screenLight.withAlpha (0.45f));
        g.drawHorizontalLine ((int) panToY (0.0f), (float) roll.getX() + 12.0f, (float) roll.getRight());
        g.setColour (screenPale.withAlpha (0.8f));
        g.drawText ("L", juce::Rectangle<float> ((float) roll.getX(), panToY (-1.0f) - 6.0f, 10.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("C", juce::Rectangle<float> ((float) roll.getX(), panToY (0.0f) - 6.0f, 10.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("R", juce::Rectangle<float> ((float) roll.getX(), panToY (1.0f) - 6.0f, 10.0f, 12.0f), juce::Justification::centredLeft);

        for (int i = 0; i < numViews; ++i)
        {
            const auto& v = views[(size_t) i];
            const float level = juce::jlimit (0.0f, 1.0f, v.level);
            const float alpha = 0.25f + 0.75f * level;
            const float x = (float) wave.getX() + (live ? xOf (v.position) : v.position) * w;

            // On the waveform: a line at the read position, as tall as the grain is loud.
            g.setColour ((v.reversed ? screenLime : juce::Colours::white).withAlpha (alpha * 0.8f));
            g.fillRect (x, mid - half * level, 1.5f, juce::jmax (1.0f, 2.0f * half * level));

            // In the roll: the span it covers and its head, at its stereo
            // position. A small offset per grain keeps centred grains apart.
            const float stagger = ((float) ((v.voice * 5 + v.slot) % 5) - 2.0f) * laneH * 0.5f;
            const float y = panToY (juce::jlimit (-1.0f, 1.0f, v.pan)) + stagger - laneH * 0.5f;
            const float sx = (float) wave.getX() + (live ? xOf (v.start) : v.start) * w;
            const float ex = juce::jmax (sx + 1.0f, sx + (v.end - v.start) * w);
            g.setColour ((v.reversed ? screenLime : screenPale).withAlpha (0.25f + 0.35f * level));
            g.fillRect (sx, y + laneH * 0.3f, ex - sx, juce::jmax (1.0f, laneH * 0.4f));
            g.setColour ((v.reversed ? screenLime : juce::Colours::white).withAlpha (alpha));
            g.fillRect (x - 1.5f, y, 3.0f, juce::jmax (2.0f, laneH - 1.0f));
        }

        // Voice playheads: a marker over the waveform.
        g.setColour (playheadCyan);
        for (float p : heads)
        {
            if (p < 0.0f)
                continue;
            const float x = (float) wave.getX() + (live ? xOf (p) : p) * w;
            juce::Path marker;
            marker.addTriangle (x - 4.0f, (float) wave.getY() - 1.0f, x + 4.0f, (float) wave.getY() - 1.0f, x, (float) wave.getY() + 6.0f);
            g.fillPath (marker);
            g.fillRect (x - 0.5f, (float) wave.getY(), 1.0f, (float) wave.getHeight());
        }
    }

    SiltProcessor::Overview overview;
    juce::Image waveCache;
    bool live = false, frozen = false, transientsShown = false;
    float writeShare = 0.0f;
    std::array<float, SiltProcessor::liveColumns> livePeaks {};
    float start = 0.0f, length = 1.0f;
    std::array<silt::GrainView, SiltProcessor::maxGrainViews> views {};
    int numViews = 0;
    std::array<float, silt::Engine::numVoices> heads {};
    juce::String hud;
};

} // namespace silt::ui
