#pragma once

#include "Palette.h"
#include "PluginProcessor.h"

namespace silt::ui
{

// The screen: the sample (cached, redrawn only when it or the size changes),
// the region, a playhead per voice, and every sounding grain twice: as a line
// on the waveform where it's reading, and as a glowing dot with a fading
// trail in the grain field below. The field's two axes keep Spray and Random
// pan apart: left to right is where in the sample a grain plays (spray
// scatters it sideways), top to bottom is where it sits in the stereo field
// (random pan spreads it up and down).
// The screen's ZOOM: how far in the waveform is zoomed; click to fit it all.
class ZoomReadout final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ZoomReadout()
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("Scroll on the waveform to zoom (Shift: move along), or drag here up and down; double-click to see it all");
        setSize (78, 16);
    }

    std::function<void()> onReset;
    std::function<void()> onDragStart;
    std::function<void (float upwards)> onDrag;   // pixels dragged up since the press
    float zoom = 1.0f;

    void paint (juce::Graphics& g) override
    {
        if (isMouseOver (true))
        {
            g.setColour (screenLight.withAlpha (0.35f));
            g.fillRect (getLocalBounds());
        }
        const bool zoomed = zoom > 1.01f;
        g.setColour (zoomed ? screenLime : screenPale.withAlpha (0.6f));
        g.setFont (monoFont (12.0f));
        g.drawText ("ZOOM " + juce::String (zoom, zoom < 10.0f ? 1 : 0) + "X", getLocalBounds(), juce::Justification::centred);
    }
    // Press and drag up to zoom in, down to zoom out; double-click: all of it.
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onDragStart)
            onDragStart();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onDrag)
            onDrag ((float) -e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.25f : 1.0f));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onReset)
            onReset();
    }
};

class GrainScreen final : public juce::Component
{
public:
    GrainScreen()
    {
        zoomBox.onReset = [this] { setView (0.0f, 1.0f); };
        zoomBox.onDragStart = [this] { dragViewStart = viewStart; dragViewEnd = viewEnd; };
        zoomBox.onDrag = [this] (float upwards)
        {
            if (live || overview.minimum.empty())
                return;
            // Every 100 px up doubles the zoom, around the middle of the view.
            const float centre = 0.5f * (dragViewStart + dragViewEnd);
            const float span = juce::jlimit (1.0f / maxZoom, 1.0f, (dragViewEnd - dragViewStart) * std::pow (0.5f, upwards / 100.0f));
            setView (centre - 0.5f * span, centre + 0.5f * span);
        };
        zoomBox.getProperties().set ("help", "!zoom");
        addAndMakeVisible (zoomBox);
    }

    // Zoom: the part of the sample in view, as shares (the UI's own: not saved).
    static constexpr float maxZoom = 32.0f;
    void setView (float from, float to)
    {
        const float span = juce::jlimit (1.0f / maxZoom, 1.0f, to - from);
        from = juce::jlimit (0.0f, 1.0f - span, from);
        if (juce::approximatelyEqual (from, viewStart) && juce::approximatelyEqual (from + span, viewEnd))
            return;
        viewStart = from;
        viewEnd = from + span;
        waveCache = {};
        field = {};
        zoomBox.zoom = 1.0f / span;
        zoomBox.repaint();
        repaint();
    }
    float getZoom() const noexcept { return 1.0f / (viewEnd - viewStart); }
    float getViewStart() const noexcept { return viewStart; }

    // Scroll on the waveform: zoom around the mouse; Shift (or a sideways
    // swipe): move along. Not in Live mode, which always shows the whole ring.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (live || overview.minimum.empty())
            return;
        const float span = viewEnd - viewStart;
        if (! std::isfinite (wheel.deltaX) || ! std::isfinite (wheel.deltaY))
            return;
        // A trackpad's up/down swipe carries a little sideways movement too:
        // only a mostly sideways swipe moves along, or every zoom would be lost.
        const bool across = e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
        const float sideways = e.mods.isShiftDown() ? (std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX) : wheel.deltaX;
        if (across)
        {
            const float move = -sideways * span * 0.5f;
            setView (viewStart + move, viewEnd + move);
            return;
        }
        const float anchor = waveShareAt (e.position.x);
        const float wider = std::pow (0.5f, wheel.deltaY * (wheel.isReversed ? -4.0f : 4.0f));
        const float newSpan = juce::jlimit (1.0f / maxZoom, 1.0f, span * wider);
        const float along = (anchor - viewStart) / span;
        setView (anchor - along * newSpan, anchor - along * newSpan + newSpan);
    }

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
        if (n > 0)
            std::copy (v.begin(), v.begin() + n, views.begin());
        numViews = n;
        fadeFrames = n > 0 ? 45 : juce::jmax (0, fadeFrames - 1);   // trails fade for 1.5 s after the last grain
        const bool changed = n > 0 || fadeFrames > 0 || h != heads || info != hud;
        heads = h;
        hud = info;
        if ((n > 0 || fadeFrames > 0) && ! scopeShown)
            updateField();
        return changed;
    }

    // Live mode: the ring's peaks by position and the write head, drawn as a
    // scrolling view with the newest audio at the right edge.
    void setLive (bool isLive, bool isFrozen, float writeHead, const std::array<float, SiltProcessor::liveColumns>& peaks, bool feeding = false)
    {
        if (isLive && ! live)
            setView (0.0f, 1.0f);   // the ring is always shown whole
        live = isLive;
        selfFeed = feeding;
        frozen = isFrozen;
        writeShare = writeHead;
        livePeaks = peaks;
    }

    // SLICE: where each slice starts (shares), drawn and numbered on the waveform.
    void setSlices (std::vector<float> marks)
    {
        if (marks != slices)
        {
            slices = std::move (marks);
            repaint();
        }
    }

    void setTransientsShown (bool shown)
    {
        if (shown != transientsShown)
        {
            transientsShown = shown;
            repaint();
        }
    }

    void resized() override
    {
        waveCache = {};
        // The top bar's controls: right-aligned, left of the length readout.
        auto top = getLocalBounds().reduced (8 + 12 + 10, 8 + 8 + 6).removeFromTop (16);
        top.removeFromRight (96);
        for (auto* child : getChildren())
            child->setBounds (top.removeFromRight (child->getWidth() > 0 ? child->getWidth() : 130).withSizeKeepingCentre (child->getWidth() > 0 ? child->getWidth() : 130, 16));
    }

    // The waveform is drawn at the sample's gain.
    void setDisplayGain (float gain)
    {
        if (! juce::approximatelyEqual (gain, displayGain))
        {
            displayGain = gain;
            waveCache = {};
            repaint();
        }
    }

    // The open page's picture, in the bottom-right corner over the grain roll
    // (drawInset draws it and returns false when the page has none).
    std::function<bool (juce::Graphics&, juce::Rectangle<float>)> drawInset;
    void repaintInset() { repaint (insetArea()); }

    // The live scope, over the grain roll left of the inset: the sound going
    // into a module (faint) and coming out (bright), steadied on the input's
    // rising zero crossings and scaled to fit. in/out: the latest samples,
    // oldest first; window: how many to show.
    void setScope (const std::vector<float>& in, const std::vector<float>& out, int window, const juce::String& title)
    {
        const bool wasShown = scopeShown;
        scopeShown = true;
        scopeTitle = title;
        const int n = (int) juce::jmin (in.size(), out.size());
        window = juce::jlimit (16, juce::jmax (16, n / 2), window);
        int from = n - window;
        // The latest rising zero crossing that still leaves a full window.
        for (int i = from; i > juce::jmax (1, n - 2 * window); --i)
            if (in[(size_t) i - 1] <= 0.0f && in[(size_t) i] > 0.0f)
            {
                from = i;
                break;
            }
        scopeIn.assign (in.begin() + from, in.begin() + from + window);
        scopeOut.assign (out.begin() + from, out.begin() + from + window);

        float peak = 0.0f;
        for (int i = 0; i < window; ++i)
            peak = juce::jmax (peak, std::abs (scopeIn[(size_t) i]), std::abs (scopeOut[(size_t) i]));
        const bool wasQuiet = scopeQuiet;
        scopeQuiet = peak < 1.0e-4f;
        if (scopeQuiet && wasQuiet && wasShown)
            return;   // silence: already showing PLAY A NOTE
        const float target = 0.9f / juce::jmax (peak, 0.02f);
        scopeScale = target < scopeScale ? target : scopeScale + (target - scopeScale) * 0.15f;   // shrink at once, grow slowly
        repaint (scopeArea());
    }

    void hideScope()
    {
        if (scopeShown)
        {
            scopeShown = false;
            repaint (scopeArea());
        }
    }

    // Right-click on the screen: the sample's menu (the editor builds it).
    // SLICE: drag a slice line to move it; double-click a line to remove it,
    // or anywhere else on the waveform to add one.
    std::function<void()> onMenu;
    std::function<void (int index, float share)> onSliceMove;
    std::function<void (bool starting)> onSliceDrag;   // a drag's start and end: one undo step
    std::function<void (float share)> onSliceAdd;
    std::function<void (int index)> onSliceRemove;

    void mouseDown (const juce::MouseEvent& e) override
    {
        draggingSlice = -1;
        draggingBar = false;
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (onScrollBar (e.position))
        {
            // On the thumb: hold it where it was taken; elsewhere on the bar:
            // centre the view there, then drag from the middle.
            const auto track = scrollTrack();
            const auto thumb = thumbIn (track);
            if (! thumb.contains (e.position.x, thumb.getCentreY()))
                scrollTo (e.position.x - thumb.getWidth() * 0.5f);
            barGrab = e.position.x - thumbIn (track).getX();
            draggingBar = true;
            repaint();
            return;
        }
        draggingSlice = sliceAt (e.position);
        if (draggingSlice >= 0 && onSliceDrag)
            onSliceDrag (true);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingBar)
        {
            scrollTo (e.position.x - barGrab);
            return;
        }
        if (draggingSlice >= 0 && onSliceMove)
            onSliceMove (draggingSlice, waveShareAt (e.position.x));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingSlice >= 0 && onSliceDrag)
            onSliceDrag (false);
        draggingSlice = -1;
        if (draggingBar)
        {
            draggingBar = false;
            repaint();
        }
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (slices.empty() || live || ! waveRect().toFloat().contains (e.position))
            return;
        if (const int i = sliceAt (e.position); i >= 0)
        {
            if (onSliceRemove)
                onSliceRemove (i);
        }
        else if (onSliceAdd)
            onSliceAdd (waveShareAt (e.position.x));
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (onScrollBar (e.position) ? juce::MouseCursor::DraggingHandCursor
                        : sliceAt (e.position) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    // How the grains are drawn below the waveform; each theme has its own.
    enum class FieldLook { dots, stars, shore, film, clouds };
    void setFieldLook (FieldLook l)
    {
        if (l != look)
        {
            look = l;
            field = {};
            fieldBackdrop = {};
            repaint();
        }
    }

    // How the screen is shared: the grain field's share of the height, the
    // corner picture's share of the width (the screen's tiled sections).
    void setSplit (float fieldPart, float insetPart)
    {
        if (! juce::approximatelyEqual (fieldPart, fieldShare) || ! juce::approximatelyEqual (insetPart, insetShare))
        {
            fieldShare = fieldPart;
            insetShare = insetPart;
            waveCache = {};
            field = {};
            repaint();
        }
    }

    // The zoom's scroll bar, along the waveform's bottom edge (only when zoomed).
    static constexpr float scrollBarHeight = 6.0f;
    juce::Rectangle<float> scrollTrack() const { return waveRect().toFloat().removeFromBottom (scrollBarHeight); }
    juce::Rectangle<float> thumbIn (juce::Rectangle<float> track) const
    {
        return track.withX (track.getX() + track.getWidth() * viewStart).withWidth (juce::jmax (8.0f, track.getWidth() * (viewEnd - viewStart)));
    }
    bool onScrollBar (juce::Point<float> p) const
    {
        return ! live && viewEnd - viewStart < 0.999f && scrollTrack().expanded (0.0f, 4.0f).contains (p);
    }
    void scrollTo (float thumbX)   // the thumb's left edge, in pixels
    {
        const auto track = scrollTrack();
        const float span = viewEnd - viewStart;
        const float from = (thumbX - track.getX()) / juce::jmax (1.0f, track.getWidth());
        setView (from, from + span);
    }
    bool draggingBar = false;
    float barGrab = 0.0f;   // where on the thumb it was taken

    // Where the waveform is (as paintScreen lays it out).
    juce::Rectangle<int> waveRect() const
    {
        auto inner = getLocalBounds().reduced (8 + 12 + 10, 8 + 8 + 6);
        inner.removeFromTop (16);
        inner.removeFromBottom (20);
        inner.removeFromBottom (juce::roundToInt ((float) inner.getHeight() * fieldShare));
        inner.removeFromBottom (6);
        return inner;
    }

    // Where the grain roll is: the lower part of the screen.
    juce::Rectangle<int> rollArea() const
    {
        auto inner = getLocalBounds().reduced (8 + 12 + 10, 8 + 8 + 6);
        inner.removeFromTop (16);
        inner.removeFromBottom (20);
        return inner.removeFromBottom (juce::roundToInt ((float) inner.getHeight() * fieldShare));
    }

    juce::Point<int> getInsetTopLeft() const { return insetArea().getTopLeft(); }
    void restyle() { bezel = {}; field = {}; fieldBackdrop = {}; repaint(); }   // after a theme change

    juce::Rectangle<int> scopeArea() const
    {
        auto area = rollArea();
        return area.withRight (juce::jmin (area.getRight(), insetArea().getX() - 10));
    }

    // A page's own picture in the strip where the scope goes (SPACE's
    // timeline); returns false when the page has none.
    std::function<bool (juce::Graphics&, juce::Rectangle<float>)> drawStrip;
    void repaintStrip() { repaint (scopeArea().expanded (0, 2)); }

    void paint (juce::Graphics& g) override
    {
        paintScreen (g);
        if (scopeShown)
            drawScope (g, scopeArea());
        else if (drawStrip != nullptr)
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (scopeArea().expanded (0, 2));
            drawStrip (g, scopeArea().toFloat());
        }
        if (drawInset != nullptr)
        {
            const auto area = insetArea().toFloat();
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (area.toNearestInt());
            drawInset (g, area);
        }

        // Under glass: a shadow cast by the bezel along the top, and a faint sheen.
        if (! glass.isEmpty())
        {
            const auto face = glass.toFloat();
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, face.getY(),
                                                     juce::Colours::transparentBlack, 0.0f, face.getY() + 9.0f, false));
            g.fillRect (face.withHeight (9.0f));
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, face.getY(),
                                                     juce::Colours::transparentWhite, 0.0f, face.getY() + face.getHeight() * 0.45f, false));
            g.fillRect (face.withHeight (face.getHeight() * 0.45f));
        }
    }

    // Right-aligned at the bottom: the grain roll's height plus the status line.
    juce::Rectangle<int> insetArea() const
    {
        auto content = getLocalBounds().reduced (8 + 12 + 10, 8 + 8 + 6);
        const int rollAndStatus = juce::roundToInt ((float) (content.getHeight() - 36) * fieldShare) + 20;
        return content.removeFromRight (juce::roundToInt ((float) content.getWidth() * insetShare)).removeFromBottom (rollAndStatus);
    }

private:
    void drawScope (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (insetBack.interpolatedWith (juce::Colours::black, 0.55f));   // as the corner pictures' ground
        g.fillRect (area.expanded (0, 2));
        const auto box = area.toFloat();
        g.setColour (screenLight.withAlpha (0.6f));
        g.drawRect (box, 1.0f);
        g.setColour (screenLight.withAlpha (0.35f));
        g.drawHorizontalLine (area.getCentreY(), box.getX(), box.getRight());

        auto label = area.reduced (6, 3).removeFromTop (12);
        g.setFont (monoFont (10.0f));
        g.setColour (screenLime);
        g.drawText (scopeTitle, label, juce::Justification::centredLeft);
        g.setColour (signalTeal);
        g.drawText ("OUT", label.removeFromRight (26), juce::Justification::centredRight);
        g.setColour (screenPale.withAlpha (0.7f));
        g.drawText ("IN", label.removeFromRight (30), juce::Justification::centredLeft);

        if (scopeQuiet || scopeIn.empty())
        {
            g.setColour (screenPale.withAlpha (0.8f));
            g.drawText ("PLAY A NOTE", area, juce::Justification::centred);
            return;
        }

        const auto trace = [&] (const std::vector<float>& v, juce::Colour colour, float thickness, bool glow)
        {
            const float half = box.getHeight() * 0.5f - 3.0f, mid = box.getCentreY();
            const float dx = box.getWidth() / (float) juce::jmax (1, (int) v.size() - 1);
            juce::Path path;
            for (size_t i = 0; i < v.size(); ++i)
            {
                const float y = mid - juce::jlimit (-1.0f, 1.0f, v[i] * scopeScale) * half;
                if (i == 0)
                    path.startNewSubPath (box.getX(), y);
                else
                    path.lineTo (box.getX() + dx * (float) i, y);
            }
            if (glow)   // a wide soft stroke under the crisp one, as the corner pictures draw the sound
            {
                g.setColour (colour.withMultipliedAlpha (0.12f));
                g.strokePath (path, juce::PathStrokeType (thickness * 5.0f));
                g.setColour (colour.withMultipliedAlpha (0.25f));
                g.strokePath (path, juce::PathStrokeType (thickness * 2.5f));
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (thickness));
        };
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.reduced (1));
        trace (scopeIn, screenPale.withAlpha (0.4f), 1.1f, false);
        trace (scopeOut, signalTeal, 1.4f, true);
    }

    void paintScreen (juce::Graphics& g)
    {
        // The bezel: a dark frame lit from above, a lighter ring, then the
        // glass; drawn once per size and theme.
        auto r = getLocalBounds();
        if (bezel.getWidth() != getWidth() || bezel.getHeight() != getHeight())
        {
            bezel = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
            juce::Graphics b (bezel);
            auto ring = r;
            b.setGradientFill (juce::ColourGradient (frame.brighter (0.25f), 0.0f, (float) ring.getY(), frame.darker (0.35f), 0.0f, (float) ring.getBottom(), false));
            b.fillRoundedRectangle (ring.toFloat(), 9.0f);
            b.setColour (juce::Colours::white.withAlpha (0.12f));
            b.drawRoundedRectangle (ring.toFloat().reduced (0.5f), 9.0f, 1.0f);
            ring.reduce (8, 8);
            b.setGradientFill (juce::ColourGradient (screenLight.brighter (0.15f), 0.0f, (float) ring.getY(), screenLight.darker (0.15f), 0.0f, (float) ring.getBottom(), false));
            b.fillRoundedRectangle (ring.toFloat(), 8.0f);
            ring.reduce (12, 8);
            b.setColour (screenBlue);
            b.fillRect (ring);
        }
        g.drawImageAt (bezel, 0, 0);
        r.reduce (20, 16);
        glass = r;

        auto inner = r.reduced (10, 6);
        auto top = inner.removeFromTop (16);
        auto bottom = inner.removeFromBottom (16);
        inner.removeFromBottom (4);
        const auto rollArea = inner.removeFromBottom (juce::roundToInt ((float) inner.getHeight() * fieldShare));
        inner.removeFromBottom (6);
        const auto waveArea = inner;

        g.setFont (monoFont (12.0f));
        g.setColour (juce::Colours::white);

        if (live)
        {
            g.drawText (juce::String (selfFeed ? "FEED: SILT'S OWN SOUND" : "LIVE INPUT") + (frozen ? "  FROZEN" : ""), top, juce::Justification::centredLeft);
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
        if (viewEnd - viewStart < 0.999f)
        {
            // Zoomed: a bar along the bottom shows which part is in view; drag
            // it (or click along it) to move through the sample.
            const auto track = waveArea.toFloat().removeFromBottom (scrollBarHeight);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (track, scrollBarHeight * 0.5f);
            g.setColour (screenLime.withAlpha (draggingBar ? 1.0f : 0.8f));
            g.fillRoundedRectangle (thumbIn (track), scrollBarHeight * 0.5f);
        }
        if (transientsShown)
        {
            // The transients SNAP can land on: ticks along the top of the waveform.
            g.setColour (screenLime.withAlpha (0.8f));
            for (float t : overview.transients)
            {
                const float x = toX (t, waveArea);
                if (x >= (float) waveArea.getX() && x <= (float) waveArea.getRight())
                    g.fillRect (x, (float) waveArea.getY(), 1.0f, 7.0f);
            }
        }
        drawRegion (g, waveArea, rollArea);
        if (! slices.empty())
        {
            // The slices: a line at each start, numbered while there's room.
            const float spacing = (float) waveArea.getWidth() * (slices.size() > 1 ? slices[1] - slices[0] : 1.0f) / (viewEnd - viewStart);
            g.setFont (monoFont (9.0f));
            for (size_t i = 0; i < slices.size(); ++i)
            {
                const float x = toX (slices[i], waveArea);
                if (x < (float) waveArea.getX() || x > (float) waveArea.getRight())
                    continue;
                g.setColour (screenLime.withAlpha (0.55f));
                g.fillRect (x, (float) waveArea.getY(), 1.0f, (float) waveArea.getHeight());
                if (spacing > 16.0f)
                {
                    g.setColour (screenLime);
                    g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x + 2.0f, (float) waveArea.getY() + 2.0f, 20.0f, 10.0f),
                                juce::Justification::centredLeft);
                }
            }
        }
        drawGrains (g, waveArea, rollArea);

        g.setColour (juce::Colours::white);
        g.drawText (numViews > 0 ? hud : juce::String ("READY."), bottom, juce::Justification::centredLeft);
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (! waveCache.isValid() || waveCache.getWidth() != area.getWidth() || waveCache.getHeight() != area.getHeight())
        {
            waveCache = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
            juce::Graphics c (waveCache);
            const float mid = (float) area.getHeight() * 0.5f, half = (float) area.getHeight() * 0.48f;
            const int cols = (int) overview.minimum.size();
            // The columns in view, each pixel taking the loudest of those under it.
            // At the sample gain: columns pushed past full scale are drawn red.
            const double first = viewStart * cols, perPixel = (viewEnd - viewStart) * cols / juce::jmax (1, area.getWidth());
            for (int x = 0; x < area.getWidth(); ++x)
            {
                const int c0 = juce::jlimit (0, cols - 1, (int) (first + x * perPixel));
                const int c1 = juce::jlimit (c0 + 1, cols, (int) (first + (x + 1) * perPixel));
                float hi = 0.0f, lo = 0.0f;
                for (int col = c0; col < c1; ++col)
                {
                    hi = juce::jmax (hi, overview.maximum[(size_t) col]);
                    lo = juce::jmin (lo, overview.minimum[(size_t) col]);
                }
                hi *= displayGain;
                lo *= displayGain;
                const bool clipped = hi > 1.0f || lo < -1.0f;
                const float top = mid - juce::jmin (1.0f, hi) * half;
                const float bottom = mid - juce::jmax (-1.0f, lo) * half;
                c.setColour (clipped ? signalRed : screenPale);
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

    // Where a share of the sample sits across an area: through the zoom, or
    // in Live mode through the scrolling ring.
    float toX (float share, juce::Rectangle<int> area) const
    {
        const float across = live ? xOf (share) : (share - viewStart) / (viewEnd - viewStart);
        return (float) area.getX() + across * (float) area.getWidth();
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
        const auto x0 = juce::jmax ((float) wave.getX(), toX (start, wave));
        const auto x1 = juce::jlimit (x0, (float) wave.getRight(), toX (start + length, wave));
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

    // The grain field, drawn into an image that fades a little each frame, so
    // every grain leaves a short trail. Across: where it's reading (as on the
    // waveform). Down: where it sits in the stereo field, plus an offset of its
    // own (up to a quarter of the height) so grains at one pan still form a
    // cloud. Size: level.
    void updateField()
    {
        const auto roll = rollArea();
        const float scale = 2.0f;
        const int w = juce::roundToInt ((float) roll.getWidth() * scale), h = juce::roundToInt ((float) roll.getHeight() * scale);
        if (w <= 0 || h <= 0)
            return;
        if (! field.isValid() || field.getWidth() != w || field.getHeight() != h)
            field = juce::Image (juce::Image::ARGB, w, h, true);

        juce::Graphics g (field);
        // Fade what's there, each look at its own pace: clouds linger, film flickers off.
        static constexpr int keep[] { 170, 205, 150, 120, 236 };   // of 256 per frame, by FieldLook
        const int kept = keep[(int) look];
        ++tick;
        {
            juce::Image::BitmapData pixels (field, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto* p = pixels.getPixelPointer (x, y);
                    for (int c = 0; c < 4; ++c)
                        p[c] = (juce::uint8) ((p[c] * kept) >> 8);
                }
        }

        const float laneTop = (float) h * 0.12f, laneBottom = (float) h * 0.88f;
        for (int i = 0; i < numViews; ++i)
        {
            const auto& v = views[(size_t) i];
            const float level = juce::jlimit (0.0f, 1.0f, v.level);
            const float x = (live ? xOf (v.position) : (v.position - viewStart) / (viewEnd - viewStart)) * (float) w;
            const auto hash = (std::uint32_t) (v.voice * 131u + v.slot * 2654435761u);
            const float own = ((float) (hash % 1000u) / 1000.0f - 0.5f) * 0.5f;   // ±25% of the height
            const float y = juce::jmap (juce::jlimit (-1.0f, 1.0f, v.pan * 0.85f + own), -1.0f, 1.0f, laneTop, laneBottom);
            const float r = (1.5f + 4.0f * level) * scale;
            const auto colour = v.reversed ? screenLime : juce::Colours::white;
            switch (look)
            {
                case FieldLook::dots:
                    g.setColour (colour.withAlpha (0.15f + 0.25f * level));
                    g.fillEllipse (juce::Rectangle<float> (r * 2.6f, r * 2.6f).withCentre ({ x, y }));   // glow
                    g.setColour (colour.withAlpha (0.45f + 0.55f * level));
                    g.fillEllipse (juce::Rectangle<float> (r, r).withCentre ({ x, y }));
                    break;

                case FieldLook::stars:
                {
                    // A four-pointed star that twinkles: its rays breathe with the frame.
                    const float twinkle = 0.6f + 0.4f * std::sin ((float) tick * 0.7f + (float) (hash % 628u) * 0.01f);
                    const float ray = r * (1.6f + 1.4f * twinkle);
                    g.setColour (colour.withAlpha (0.12f + 0.2f * level));
                    g.fillEllipse (juce::Rectangle<float> (r * 3.0f, r * 3.0f).withCentre ({ x, y }));
                    g.setColour (colour.withAlpha ((0.5f + 0.5f * level) * twinkle));
                    g.drawLine (x - ray, y, x + ray, y, 0.9f * scale);
                    g.drawLine (x, y - ray, x, y + ray, 0.9f * scale);
                    g.fillEllipse (juce::Rectangle<float> (r * 0.8f, r * 0.8f).withCentre ({ x, y }));
                    break;
                }

                case FieldLook::shore:
                {
                    // Foam: a cluster of small bubbles riding a wave whose height the grain lifts.
                    const float swell = std::sin ((float) tick * 0.12f + x * 0.012f) * (float) h * 0.06f;
                    juce::Random foam (hash + (std::uint32_t) tick);
                    for (int b = 0; b < 4; ++b)
                    {
                        const float bx = x + (foam.nextFloat() - 0.5f) * r * 3.0f, by = y + swell + (foam.nextFloat() - 0.5f) * r * 1.5f;
                        const float br = r * (0.3f + 0.4f * foam.nextFloat());
                        g.setColour (colour.withAlpha (0.35f + 0.5f * level));
                        g.drawEllipse (juce::Rectangle<float> (br * 2.0f, br * 2.0f).withCentre ({ bx, by }), 0.8f * scale);
                    }
                    break;
                }

                case FieldLook::film:
                {
                    // A rough speck, sometimes a scratch down the frame.
                    juce::Random speck (hash + (std::uint32_t) tick * 7u);
                    juce::Path blot;
                    for (int k = 0; k < 6; ++k)
                    {
                        const auto pt = juce::Point<float> (x, y).getPointOnCircumference (r * (0.4f + 0.6f * speck.nextFloat()), (float) k * 1.047f);
                        k == 0 ? blot.startNewSubPath (pt) : blot.lineTo (pt);
                    }
                    blot.closeSubPath();
                    g.setColour (colour.withAlpha (0.5f + 0.5f * level));
                    g.fillPath (blot);
                    if (speck.nextFloat() < 0.04f)
                    {
                        g.setColour (colour.withAlpha (0.35f));
                        g.drawLine (x, 0.0f, x + (speck.nextFloat() - 0.5f) * 6.0f, (float) h, 0.7f * scale);
                    }
                    break;
                }

                case FieldLook::clouds:
                    // Soft puffs, several layered, that drift and linger.
                    for (int k = 0; k < 3; ++k)
                    {
                        const float pr = r * (2.0f + (float) k * 1.2f);
                        g.setColour (colour.withAlpha ((0.05f + 0.07f * level) * (3.0f - (float) k) / 3.0f));
                        g.fillEllipse (juce::Rectangle<float> (pr * 1.6f, pr).withCentre ({ x + (float) k * r, y - (float) k * r * 0.3f }));
                    }
                    break;
            }
        }
    }

    // What's behind the grains, by look: a starfield, a few still wave lines,
    // the film's own grain, a lighter sky. Dots have none.
    void drawFieldBackdrop (juce::Graphics& g, juce::Rectangle<int> roll)
    {
        if (look == FieldLook::dots || roll.isEmpty())
            return;
        // Drawn once per size, look and theme (a starfield is hundreds of dots).
        if (fieldBackdrop.getWidth() != roll.getWidth() * 2 || fieldBackdrop.getHeight() != roll.getHeight() * 2)
        {
            fieldBackdrop = juce::Image (juce::Image::ARGB, roll.getWidth() * 2, roll.getHeight() * 2, true);
            juce::Graphics b (fieldBackdrop);
            b.addTransform (juce::AffineTransform::scale (2.0f));
            paintFieldBackdrop (b, roll.withZeroOrigin());
        }
        g.drawImage (fieldBackdrop, roll.toFloat());
    }

    void paintFieldBackdrop (juce::Graphics& g, juce::Rectangle<int> roll)
    {
        const auto area = roll.toFloat();
        switch (look)
        {
            case FieldLook::dots:
                break;
            case FieldLook::stars:
            {
                g.setGradientFill (juce::ColourGradient (screenBlue.darker (0.8f), 0.0f, area.getY(), screenBlue.darker (0.4f), 0.0f, area.getBottom(), false));
                g.fillRect (area);
                juce::Random sky (42);
                for (int i = 0; i < (int) (area.getWidth() * area.getHeight() / 900.0f); ++i)
                {
                    const float size = 0.6f + sky.nextFloat() * 1.2f;
                    g.setColour (screenPale.withAlpha (0.08f + 0.35f * sky.nextFloat()));
                    g.fillEllipse (area.getX() + sky.nextFloat() * area.getWidth(), area.getY() + sky.nextFloat() * area.getHeight(), size, size);
                }
                break;
            }
            case FieldLook::shore:
                for (int line = 0; line < 3; ++line)
                {
                    juce::Path wave;
                    const float base = area.getY() + area.getHeight() * (0.3f + 0.22f * (float) line);
                    for (float x = 0.0f; x <= area.getWidth(); x += 6.0f)
                    {
                        const float y = base + std::sin (x * 0.03f + (float) line * 1.7f) * 4.0f;
                        x == 0.0f ? wave.startNewSubPath (area.getX() + x, y) : wave.lineTo (area.getX() + x, y);
                    }
                    g.setColour (screenLight.withAlpha (0.18f));
                    g.strokePath (wave, juce::PathStrokeType (1.2f));
                }
                break;
            case FieldLook::film:
            {
                juce::Random grain (7);
                for (int i = 0; i < (int) (area.getWidth() * area.getHeight() / 60.0f); ++i)
                {
                    g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f + 0.06f * grain.nextFloat()));
                    g.fillRect (area.getX() + grain.nextFloat() * area.getWidth(), area.getY() + grain.nextFloat() * area.getHeight(), 1.0f, 1.0f);
                }
                break;
            }
            case FieldLook::clouds:
                g.setGradientFill (juce::ColourGradient (screenLight.withAlpha (0.25f), 0.0f, area.getY(), juce::Colours::transparentBlack, 0.0f, area.getBottom(), false));
                g.fillRect (area);
                break;
        }
    }

    void drawGrains (juce::Graphics& g, juce::Rectangle<int> wave, juce::Rectangle<int> roll)
    {
        const float mid = (float) wave.getCentreY(), half = (float) wave.getHeight() * 0.5f;
        constexpr int lanes = 16;
        const float laneH = (float) roll.getHeight() / lanes;
        const float rollTop = (float) roll.getY() + laneH * 1.5f, rollBottom = (float) roll.getBottom() - laneH * 1.5f;
        const auto panToY = [=] (float pan) { return juce::jmap (pan, -1.0f, 1.0f, rollTop, rollBottom); };

        drawFieldBackdrop (g, roll);

        // Stereo guides: L at the top, centre, R at the bottom.
        g.setFont (monoFont (10.0f));
        g.setColour (screenLight.withAlpha (0.45f));
        g.drawHorizontalLine ((int) panToY (0.0f), (float) roll.getX() + 12.0f, (float) roll.getRight());
        g.setColour (screenPale.withAlpha (0.8f));
        g.drawText ("L", juce::Rectangle<float> ((float) roll.getX(), panToY (-1.0f) - 6.0f, 10.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("C", juce::Rectangle<float> ((float) roll.getX(), panToY (0.0f) - 6.0f, 10.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("R", juce::Rectangle<float> ((float) roll.getX(), panToY (1.0f) - 6.0f, 10.0f, 12.0f), juce::Justification::centredLeft);
        if (field.isValid() && fadeFrames > 0)
            g.drawImage (field, roll.toFloat());

        for (int i = 0; i < numViews; ++i)
        {
            const auto& v = views[(size_t) i];
            const float level = juce::jlimit (0.0f, 1.0f, v.level);
            const float alpha = 0.25f + 0.75f * level;
            const float x = toX (v.position, wave);

            // On the waveform: a line at the read position, as tall as the grain is loud.
            g.setColour ((v.reversed ? screenLime : juce::Colours::white).withAlpha (alpha * 0.8f));
            g.fillRect (x, mid - half * level, 1.5f, juce::jmax (1.0f, 2.0f * half * level));
        }

        // Voice playheads: a marker over the waveform.
        g.setColour (playheadCyan);
        for (float p : heads)
        {
            if (p < 0.0f)
                continue;
            const float x = toX (p, wave);
            if (x < (float) wave.getX() || x > (float) wave.getRight())
                continue;
            juce::Path marker;
            marker.addTriangle (x - 4.0f, (float) wave.getY() - 1.0f, x + 4.0f, (float) wave.getY() - 1.0f, x, (float) wave.getY() + 6.0f);
            g.fillPath (marker);
            g.fillRect (x - 0.5f, (float) wave.getY(), 1.0f, (float) wave.getHeight());
        }
    }

    SiltProcessor::Overview overview;
    juce::Image waveCache;
    float displayGain = 1.0f;
    bool live = false, frozen = false, transientsShown = false, selfFeed = false;
    std::vector<float> slices;
    int draggingSlice = -1;

    // The slice line within a few pixels of a point on the waveform, or −1.
    int sliceAt (juce::Point<float> p) const
    {
        const auto wave = waveRect().toFloat();
        if (slices.empty() || live || ! wave.contains (p))
            return -1;
        int best = -1;
        float nearest = 6.0f;
        for (size_t i = 0; i < slices.size(); ++i)
            if (const float d = std::abs (toX (slices[i], waveRect()) - p.x); d < nearest)
            {
                nearest = d;
                best = (int) i;
            }
        return best;
    }
    float waveShareAt (float x) const   // a share of the sample, from a point on the waveform
    {
        const auto wave = waveRect().toFloat();
        return juce::jlimit (0.0f, 1.0f, viewStart + (viewEnd - viewStart) * (x - wave.getX()) / juce::jmax (1.0f, wave.getWidth()));
    }
    float writeShare = 0.0f;
    std::array<float, SiltProcessor::liveColumns> livePeaks {};
    float start = 0.0f, length = 1.0f;
    std::array<silt::GrainView, SiltProcessor::maxGrainViews> views {};
    int numViews = 0;
    std::array<float, silt::Engine::numVoices> heads {};
    juce::String hud;
    juce::Image field;     // the grain field with its trails
    float viewStart = 0.0f, viewEnd = 1.0f;
    float dragViewStart = 0.0f, dragViewEnd = 1.0f;   // the view when the ZOOM drag began
    ZoomReadout zoomBox;
    FieldLook look = FieldLook::dots;
    juce::Image fieldBackdrop;   // what's behind the grains, by look
    unsigned tick = 0;     // the field's own clock, for twinkles and waves
    juce::Rectangle<int> glass;   // the screen's face, inside the bezel
    juce::Image bezel;            // the bezel and the empty glass, drawn once per size and theme
    float fieldShare = 0.36f, insetShare = 0.33f;
    int fadeFrames = 0;    // frames left to fade after the last grain
    bool scopeShown = false, scopeQuiet = true;
    std::vector<float> scopeIn, scopeOut;
    float scopeScale = 1.0f;
    juce::String scopeTitle;
};

// The screen's DRAG: drag from it into a DAW to drop the sound in as audio
// (the editor writes the file as the drag starts).
class DragOutHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragOutHandle()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("Drag into your DAW to drop the sample in as audio (in LIVE and FEED: the last 8 seconds)");
        setSize (60, 16);
    }

    std::function<void()> onDragOut;

    void paint (juce::Graphics& g) override
    {
        if (isMouseOver (true))
        {
            g.setColour (screenLight.withAlpha (0.35f));
            g.fillRect (getLocalBounds());
        }
        g.setColour (screenPale);
        g.setFont (monoFont (12.0f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) 0x21e9) + " DRAG", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override { started = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! started && e.getDistanceFromDragStart() > 4 && onDragOut)
        {
            started = true;
            onDragOut();
        }
    }

private:
    bool started = false;
};

// The screen's GAIN: the sample's level, as text on the screen. Drag up or
// down (Shift: finer), scroll, double-click for 0 dB; right-click to
// normalise (peak at −1 dB) or reset.
class SampleGainBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SampleGainBox (juce::RangedAudioParameter& p, std::function<float()> normalising)
        : parameter (p), normalise (std::move (normalising)), attachment (p, [this] (float) { repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("The sample's level: drag, scroll, double-click for 0 dB, right-click to normalise");
        setSize (130, 16);
    }

    void paint (juce::Graphics& g) override
    {
        const float db = parameter.convertFrom0to1 (parameter.getValue());
        if (isMouseOver (true))
        {
            g.setColour (screenLight.withAlpha (0.35f));
            g.fillRect (getLocalBounds());
        }
        g.setColour (std::abs (db) < 0.05f ? juce::Colours::white : screenLime);
        g.setFont (monoFont (12.0f));
        g.drawText ("GAIN " + parameter.getText (parameter.getValue(), 0).toUpperCase(), getLocalBounds(), juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Normalise (peak at -1 dB)");
            menu.addItem (2, "Reset to 0 dB");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int chosen)
            {
                if (chosen == 1)
                    attachment.setValueAsCompleteGesture (normalise());
                else if (chosen == 2)
                    attachment.setValueAsCompleteGesture (0.0f);
            });
            return;
        }
        dbAtStart = parameter.convertFrom0to1 (parameter.getValue());
        attachment.beginGesture();
        dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            attachment.setValueAsPartOfGesture (juce::jlimit (-24.0f, 24.0f, dbAtStart - (float) e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.02f : 0.15f)));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            attachment.endGesture();
        dragging = false;
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (0.0f); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float db = parameter.convertFrom0to1 (parameter.getValue());
        attachment.setValueAsCompleteGesture (juce::jlimit (-24.0f, 24.0f, db + (wheel.deltaY > 0.0f ? 0.5f : -0.5f)));
    }

private:
    juce::RangedAudioParameter& parameter;
    std::function<float()> normalise;
    juce::ParameterAttachment attachment;
    float dbAtStart = 0.0f;
    bool dragging = false;
};

} // namespace silt::ui
