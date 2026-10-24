#include "PluginEditor.h"

namespace
{
// Palette tokens from plan §5.
const juce::Colour chassis { 0xffd8ccb2 };
const juce::Colour ink { 0xff2a2420 };
const juce::Colour inkSoft { 0xff6b6157 };
const juce::Colour bone { 0xffefe6cf };
const juce::Colour boneLip { 0xffa99e85 };
const juce::Colour rust { 0xffa64826 };
const juce::Colour rustLip { 0xff6e2f17 };
const juce::Colour signalRed { 0xffc8342b };
const juce::Colour lcdGreen { 0xffb6c46b };
const juce::Colour lcdText { 0xff26301a };
const juce::Colour screenBlue { 0xff283070 };
const juce::Colour screenLight { 0xff6a78c8 };
const juce::Colour screenPale { 0xffa7b0ec };
const juce::Colour screenLime { 0xffc9cf6a };
const juce::Colour playheadCyan { 0xff70e0f0 };
const juce::Colour knobCap { 0xff1d1a17 };
const juce::Colour knobSkirt { 0xff3a332d };

constexpr int editorWidth = 1040, editorHeight = 600;
constexpr int slotsPerRow = 8;

juce::Font panelFont (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
juce::Font monoFont (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }
} // namespace

// Knobs with a printed scale, keys with a lip, LCD value boxes (plan §5 "Parts").
class SiltEditor::PanelLook final : public juce::LookAndFeel_V4
{
public:
    PanelLook()
    {
        setColour (juce::ComboBox::backgroundColourId, lcdGreen);
        setColour (juce::ComboBox::textColourId, lcdText);
        setColour (juce::ComboBox::outlineColourId, ink);
        setColour (juce::ComboBox::arrowColourId, lcdText);
        setColour (juce::PopupMenu::backgroundColourId, bone);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour (juce::PopupMenu::highlightedTextColourId, bone);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, ink);
        setColour (juce::ToggleButton::tickDisabledColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, bone);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;

        g.setColour (inkSoft);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const float inner = i % 5 == 0 ? radius - 5.0f : radius - 3.0f;
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (inner, a),
                                           centre.getPointOnCircumference (radius, a)), 1.0f);
        }

        // The travelled arc, thin, so the value reads at a glance.
        const float arcRadius = radius - 7.0f;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, startAngle + pos * (endAngle - startAngle), true);
        g.setColour (rust);
        g.strokePath (arc, juce::PathStrokeType (2.0f));

        const float skirt = radius - 10.0f;
        g.setColour (knobSkirt);
        g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre));
        g.setColour (knobCap);
        g.fillEllipse (juce::Rectangle<float> (skirt * 1.6f, skirt * 1.6f).withCentre (centre));

        const float angle = startAngle + pos * (endAngle - startAngle);
        g.setColour (bone);
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (skirt * 0.2f, angle),
                                       centre.getPointOnCircumference (skirt * 0.75f, angle)), 2.5f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool red = b.getProperties().getWithDefault ("red", false);
        const bool on = b.getToggleState();
        const auto cap = red ? signalRed : (on ? ink : bone);
        const auto lip = red ? rustLip : (on ? knobCap : boneLip);

        g.setColour (lip);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (hover && ! on ? cap.brighter (0.06f) : cap);
        g.fillRoundedRectangle (r.withTrimmedBottom (down ? 1.0f : 3.0f), 2.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return panelFont (juce::jmin (12.0f, (float) h * 0.55f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return monoFont (12.0f); }
    juce::Font getPopupMenuFont() override { return monoFont (13.0f); }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (monoFont (12.0f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }
};

// The screen: the sample (cached, redrawn only when it or the size changes),
// the region, a playhead per voice, and every sounding grain twice: as a line
// on the waveform where it's reading, and as a bar in the grain roll below.
// The roll's two axes keep Spray and Random pan apart: left to right is where
// in the sample a grain plays (spray scatters it sideways), top to bottom is
// where it sits in the stereo field (random pan spreads it up and down).
class SiltEditor::GrainScreen final : public juce::Component
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
    bool live = false, frozen = false;
    float writeShare = 0.0f;
    std::array<float, SiltProcessor::liveColumns> livePeaks {};
    float start = 0.0f, length = 1.0f;
    std::array<silt::GrainView, SiltProcessor::maxGrainViews> views {};
    int numViews = 0;
    std::array<float, silt::Engine::numVoices> heads {};
    juce::String hud;
};

// --- editor -----------------------------------------------------------------------

SiltEditor::SiltEditor (SiltProcessor& p)
    : AudioProcessorEditor (p), silt (p),
      look (std::make_unique<PanelLook>()), screen (std::make_unique<GrainScreen>())
{
    setLookAndFeel (look.get());
    addAndMakeVisible (*screen);

    const auto rateModeIs = [this] (int m) { return std::function<bool()> ([this, m] { return rateMode.getSelectedItemIndex() == m; }); };
    const auto sizeSynced = std::function<bool()> ([this] { return sizeSyncButton.getToggleState(); });
    const auto grainsMode = rateModeIs (2);

    // GRAIN: what the grains are.
    addKnob (Page::grain, 0, "pitch", "PITCH");
    addKnob (Page::grain, 1, "size", "SIZE").shown = [=] { return ! sizeSynced() && ! grainsMode(); };
    addKnob (Page::grain, 1, "sizeDiv", "SIZE").shown = sizeSynced;
    addKnob (Page::grain, 1, "grainSize", "SIZE").shown = [=] { return ! sizeSynced() && grainsMode(); };
    addKnob (Page::grain, 2, "rate", "RATE").shown = rateModeIs (0);
    addKnob (Page::grain, 2, "rateDiv", "RATE").shown = rateModeIs (1);
    addKnob (Page::grain, 2, "grains", "GRAINS").shown = grainsMode;
    addKnob (Page::grain, 3, "spray", "SPRAY");
    addKnob (Page::grain, 4, "jitter", "JITTER");
    addKnob (Page::grain, 5, "reverse", "REVERSE");
    addKnob (Page::grain, 6, "randAmp", "RND AMP");
    addKnob (Page::grain, 7, "randPan", "RND PAN");

    // SHAPE: the grain's envelope, then the note's (both previewed in slot 7).
    addKnob (Page::shape, 0, "fade", "FADE");
    addKnob (Page::shape, 1, "slant", "SLANT");
    addKnob (Page::shape, 2, "ease", "EASE");
    addKnob (Page::shape, 3, "attack", "ATTACK");
    addKnob (Page::shape, 4, "decay", "DECAY");
    addKnob (Page::shape, 5, "sustain", "SUSTAIN");
    addKnob (Page::shape, 6, "release", "RELEASE");

    // PLAY: the mode, then what it plays from. PITCH: a region of the sample and
    // how the playhead moves through it. LIVE: how far behind the input the
    // grains read, feedback, freeze, and what plays grains without notes
    // (FOLLOW: the input's level plays the SHAPE page's note envelope).
    keyMode.addItemList ({ "PITCH", "LIVE" }, 1);
    addMenu (Page::play, 0, keyMode, "MODE");
    const auto pitchMode = std::function<bool()> ([this] { return keyMode.getSelectedItemIndex() == 0; });
    const auto liveMode = std::function<bool()> ([this] { return keyMode.getSelectedItemIndex() == 1; });
    const auto delaySynced = std::function<bool()> ([this] { return delaySyncButton.getToggleState(); });

    addKnob (Page::play, 1, "start", "START").shown = pitchMode;
    addKnob (Page::play, 2, "length", "LENGTH").shown = pitchMode;
    addKnob (Page::play, 3, "scan", "SCAN").shown = pitchMode;
    endMode.addItemList ({ "LOOP", "HOLD", "STOP" }, 1);
    addMenu (Page::play, 4, endMode, "AT THE END").shown = pitchMode;

    addKnob (Page::play, 1, "liveDelay", "DELAY").shown = [=] { return liveMode() && ! delaySynced(); };
    addKnob (Page::play, 1, "delayDiv", "DELAY").shown = [=] { return liveMode() && delaySynced(); };
    addKnob (Page::play, 2, "feedback", "FEEDBACK").shown = liveMode;
    addButton (Page::play, 3, freezeButton, "freeze", "FREEZE").shown = liveMode;
    liveTrigger.addItemList ({ "OFF", "ALWAYS", "FOLLOW" }, 1);
    addMenu (Page::play, 4, liveTrigger, "NO NOTES").shown = liveMode;

    addKnob (Page::play, 5, "root", "ROOT");
    addKnob (Page::play, 6, "gain", "OUTPUT");
    if constexpr (! SiltProcessor::isInstrument)
        addKnob (Page::play, 7, "mix", "MIX");

    menuAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "endMode", endMode));
    menuAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "keyMode", keyMode));
    menuAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "liveRun", liveTrigger));
    keyMode.onChange = [this] { updateVisibility(); };
    delaySyncButton.setClickingTogglesState (true);
    addChildComponent (delaySyncButton);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "delaySync", delaySyncButton));
    delaySyncButton.onStateChange = [this] { updateVisibility(); };

    // Rate mode and Size SYNC sit under their knobs on the GRAIN page.
    rateMode.addItemList ({ "FREE", "SYNC", "GRAINS" }, 1);
    addChildComponent (rateMode);
    menuAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "rateMode", rateMode));
    rateMode.onChange = [this] { updateVisibility(); };
    sizeSyncButton.setClickingTogglesState (true);
    addChildComponent (sizeSyncButton);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "sizeSync", sizeSyncButton));
    sizeSyncButton.onStateChange = [this] { updateVisibility(); };

    // Page tabs.
    const char* names[] { "GRAIN", "SHAPE", "PLAY" };
    for (size_t i = 0; i < pageTabs.size(); ++i)
    {
        auto& tab = pageTabs[i];
        tab.setButtonText (names[i]);
        tab.setRadioGroupId (1);
        tab.setClickingTogglesState (true);
        tab.onClick = [this, i] { if (pageTabs[i].getToggleState()) showPage ((Page) i); };
        addAndMakeVisible (tab);
    }

    // Capture and loading.
    captureBars.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    addAndMakeVisible (captureBars);
    menuAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "captureBars", captureBars));

    armButton.getProperties().set ("red", true);
    armButton.setColour (juce::TextButton::textColourOffId, bone);
    armButton.onClick = [this]
    {
        if (silt.captureState.load() == SiltProcessor::CaptureState::idle)
            silt.armCapture();
        else
            silt.cancelCapture();
    };
    addAndMakeVisible (armButton);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      silt.loadFile (fc.getResult());
                              });
    };
    addAndMakeVisible (loadButton);

    if constexpr (SiltProcessor::isInstrument)
    {
        addAndMakeVisible (monitorButton);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "monitor", monitorButton));
    }

    setSize (editorWidth, editorHeight);
    pageTabs[0].setToggleState (true, juce::dontSendNotification);
    showPage (Page::grain);
    startTimerHz (30);
}

SiltEditor::~SiltEditor()
{
    setLookAndFeel (nullptr);
}

SiltEditor::Control& SiltEditor::addKnob (Page p, int slot, const char* paramId, const juce::String& label)
{
    auto c = std::make_unique<Control>();
    c->label = label;
    c->page = p;
    c->slot = slot;
    c->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
    // Set on the slider itself: its text box is made before it joins the
    // editor, so it never sees the panel look's colours.
    c->slider.setColour (juce::Slider::textBoxTextColourId, lcdText);
    c->slider.setColour (juce::Slider::textBoxBackgroundColourId, lcdGreen);
    c->slider.setColour (juce::Slider::textBoxOutlineColourId, ink);
    c->slider.setColour (juce::Slider::textBoxHighlightColourId, rust.withAlpha (0.4f));
    addChildComponent (c->slider);
    c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (silt.params, paramId, c->slider);
    controls.push_back (std::move (c));
    return *controls.back();
}

SiltEditor::Control& SiltEditor::addMenu (Page p, int slot, juce::ComboBox& box, const juce::String& label)
{
    auto c = std::make_unique<Control>();
    c->menu = &box;
    c->label = label;
    c->page = p;
    c->slot = slot;
    addChildComponent (box);
    controls.push_back (std::move (c));
    return *controls.back();
}

SiltEditor::Control& SiltEditor::addButton (Page p, int slot, juce::Button& button, const char* paramId, const juce::String& label)
{
    auto c = std::make_unique<Control>();
    c->button = &button;
    c->label = label;
    c->page = p;
    c->slot = slot;
    button.setClickingTogglesState (true);
    addChildComponent (button);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, paramId, button));
    controls.push_back (std::move (c));
    return *controls.back();
}

void SiltEditor::showPage (Page p)
{
    page = p;
    updateVisibility();
}

void SiltEditor::updateVisibility()
{
    for (auto& c : controls)
        c->component().setVisible (c->page == page && (c->shown == nullptr || c->shown()));

    rateMode.setVisible (page == Page::grain);
    sizeSyncButton.setVisible (page == Page::grain);
    delaySyncButton.setVisible (page == Page::play && keyMode.getSelectedItemIndex() == 1);
    repaint (pagePanel);
}

juce::Rectangle<int> SiltEditor::slotBounds (int slot) const
{
    const int w = controlRow.getWidth() / slotsPerRow;
    return { controlRow.getX() + slot * w, controlRow.getY(), w, controlRow.getHeight() };
}

bool SiltEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && silt.isAudioFile (juce::File (files[0]));
}

void SiltEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (! files.isEmpty())
        silt.loadFile (juce::File (files[0]));
    repaint();
}

juce::String SiltEditor::statusText() const
{
    using S = SiltProcessor::CaptureState;
    switch (silt.captureState.load())
    {
        case S::armed: return silt.hostPlaying.load() ? "ARMED: WAITING FOR NEXT BAR" : "ARMED: STARTING";
        case S::recording: return "REC " + juce::String (juce::roundToInt (silt.captureProgress.load() * 100.0f)) + "%";
        case S::done:
        case S::cancelRequested:
        case S::cancelled:
        case S::idle: break;
    }

    if (silt.lastLoadError.isNotEmpty())
        return silt.lastLoadError;

    juce::String source;
    switch (silt.captureSource.load())
    {
        case SiltProcessor::CaptureSource::sidechain: source = "SC"; break;
        case SiltProcessor::CaptureSource::mainInput: source = "IN"; break;
        case SiltProcessor::CaptureSource::none: source = "NO INPUT"; break;
    }
    const auto level = levelDb <= -60.0f ? juce::String ("-INF") : juce::String (juce::roundToInt (levelDb));
    return source + " " + level + " DB   " + juce::String (silt.hostBpm.load(), 1) + " BPM";
}

void SiltEditor::timerCallback()
{
    const auto oldStatus = statusText();
    const float db = juce::Decibels::gainToDecibels (silt.captureMeter.takePeak(), -100.0f);
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);

    if (const int generation = silt.getSampleGeneration(); generation != seenGeneration)
    {
        seenGeneration = generation;
        screen->setOverview (silt.getOverview());
    }
    screen->setRegion (param ("start") * 0.01f, param ("length") * 0.01f);

    // The grains: the screen only repaints while something is moving.
    static std::array<silt::GrainView, SiltProcessor::maxGrainViews> views;
    const int n = silt.readGrainViews (views);
    std::array<float, silt::Engine::numVoices> heads {};
    int voices = 0;
    for (size_t i = 0; i < heads.size(); ++i)
    {
        heads[i] = silt.voicePlayheads[i].load();
        voices += heads[i] >= 0.0f ? 1 : 0;
    }

    juce::String hud;
    if (n > 0)
        hud = juce::String (n) + (n == 1 ? " GRAIN   " : " GRAINS   ") + juce::String (voices) + (voices == 1 ? " VOICE" : " VOICES");
    const bool live = silt.liveMode.load();
    const bool frozen = param ("freeze") > 0.5f;
    if (live)
        screen->setLive (true, frozen, silt.liveWriteShare.load(), silt.getLivePeaks());
    else
        screen->setLive (false, false, 0.0f, {});
    const bool moved = screen->setActivity (views, n, heads, hud);
    if (moved || (live && ! frozen) || live != screenWasLive)
        screen->repaint();
    screenWasLive = live;

    const bool capturing = silt.captureState.load() != SiltProcessor::CaptureState::idle;
    armButton.setButtonText (capturing ? "CANCEL" : juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ARM")));

    if (statusText() != oldStatus)
        repaint (header);

    // Redraw the SHAPE preview only when its values change.
    if (page == Page::shape)
    {
        const std::array<float, 7> now { param ("fade"), param ("slant"), param ("ease"), param ("attack"),
                                         param ("decay"), param ("sustain"), param ("release") };
        if (now != previewSeen)
        {
            previewSeen = now;
            repaint (pagePanel);
        }
    }
}

void SiltEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    header = r.removeFromTop (44);
    r.removeFromTop (10);
    screen->setBounds (r.removeFromTop (272));
    r.removeFromTop (10);

    strip = r.removeFromTop (34);
    auto row = strip;
    armButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (8);
    captureBars.setBounds (row.removeFromLeft (104).reduced (0, 4));
    row.removeFromLeft (8);
    loadButton.setBounds (row.removeFromLeft (90));
    if constexpr (SiltProcessor::isInstrument)
        monitorButton.setBounds (row.removeFromRight (104).reduced (0, 4));
    r.removeFromTop (10);

    pagePanel = r;
    auto panel = r.reduced (2);
    auto tabs = panel.removeFromTop (28);
    for (auto& tab : pageTabs)
        tab.setBounds (tabs.removeFromLeft (110).reduced (3, 3));
    controlRow = panel.withTrimmedTop (2);

    for (auto& c : controls)
    {
        auto cell = slotBounds (c->slot).withTrimmedTop (18).withTrimmedBottom (22).reduced (10, 0);
        if (c->menu != nullptr)
            c->menu->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24));
        else if (c->button != nullptr)
            c->button->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 12, 34));
        else
            c->slider.setBounds (cell);
    }

    const auto under = [this] (int slot, int width)
    {
        return slotBounds (slot).removeFromBottom (20).withSizeKeepingCentre (width, 18);
    };
    sizeSyncButton.setBounds (under (1, 60));
    delaySyncButton.setBounds (under (1, 60));
    rateMode.setBounds (under (2, 92));
}

void SiltEditor::paintPreview (juce::Graphics& g)
{
    // A rule between the grain's envelope (slots 0–2) and the note's (3–6).
    g.setColour (ink);
    g.fillRect (slotBounds (3).getX() - 1, controlRow.getY() + 6, 2, controlRow.getHeight() - 12);

    // Slot 7: one grain's envelope above, one note's ADSR below.
    auto cell = slotBounds (slotsPerRow - 1).reduced (10, 8).toFloat();
    const auto drawCurve = [&g] (juce::Rectangle<float> box, const juce::Path& curve, const char* caption)
    {
        g.setColour (ink);
        g.fillRect (box);
        g.setColour (lcdGreen);
        g.strokePath (curve, juce::PathStrokeType (1.5f));
        g.setFont (monoFont (9.0f));
        g.drawText (caption, box.reduced (4, 2), juce::Justification::topLeft);
    };
    const auto plot = [] (juce::Rectangle<float> box) { return box.reduced (5.0f, 6.0f).withTrimmedTop (8.0f); };

    const auto grainBox = cell.removeFromTop (cell.getHeight() * 0.5f - 3.0f);
    cell.removeFromTop (6.0f);
    const auto noteBox = cell;

    juce::Path grainCurve;
    {
        const auto area = plot (grainBox);
        silt::HannWindow hann;
        silt::GrainEnvelope env;
        env.set (param ("fade") * 0.01, param ("slant") * 0.01, param ("ease") * 0.01, 1000, 0.0);
        for (int i = 0; i <= 100; ++i)
        {
            const float t = (float) i / 100.0f;
            const juce::Point<float> pt (area.getX() + t * area.getWidth(), area.getBottom() - env (hann, t) * area.getHeight());
            if (i == 0)
                grainCurve.startNewSubPath (pt);
            else
                grainCurve.lineTo (pt);
        }
    }

    juce::Path noteCurve;
    {
        // Attack, decay and release on a shared time scale, with a fixed sustain hold.
        const auto area = plot (noteBox);
        const float a = param ("attack"), d = param ("decay"), s = param ("sustain") * 0.01f, rel = param ("release");
        const float hold = 0.25f * (a + d + rel) + 1.0f;
        const float total = a + d + hold + rel;
        const auto at = [&] (float ms, float level)
        {
            return juce::Point<float> (area.getX() + ms / total * area.getWidth(), area.getBottom() - level * area.getHeight());
        };
        noteCurve.startNewSubPath (at (0.0f, 0.0f));
        noteCurve.lineTo (at (a, 1.0f));
        noteCurve.lineTo (at (a + d, s));
        noteCurve.lineTo (at (a + d + hold, s));
        noteCurve.lineTo (at (total, 0.0f));
    }

    drawCurve (grainBox, grainCurve, "GRAIN");
    drawCurve (noteBox, noteCurve, "NOTE");
}

void SiltEditor::paint (juce::Graphics& g)
{
    g.fillAll (chassis);

    // Header band: name, stripes, LCD.
    g.setColour (ink);
    g.fillRect (header);
    g.setColour (bone);
    g.setFont (panelFont (20.0f));
    g.drawText (juce::String (JucePlugin_Name).toUpperCase(), header.withTrimmedLeft (16), juce::Justification::centredLeft);

    auto stripes = header.withTrimmedLeft (130).withWidth (180).withSizeKeepingCentre (180, 19);
    for (auto c : { rust, juce::Colour (0xffd9a03a), juce::Colour (0xff8fa27a), bone })
    {
        g.setColour (c);
        g.fillRect (stripes.removeFromTop (4));
        stripes.removeFromTop (1);
    }

    auto lcd = header.withTrimmedLeft (header.getWidth() - 420).reduced (8, 7);
    g.setColour (juce::Colour (0xff14110f));
    g.fillRect (lcd);
    g.setColour (lcdGreen);
    g.fillRect (lcd.reduced (3));
    g.setColour (lcdText);
    g.setFont (monoFont (14.0f));
    g.drawText (statusText(), lcd.reduced (12, 0), juce::Justification::centredLeft);

    // Page panel: a ruled grid of cells (UI inspiration: data sheets, diagnostics).
    g.setColour (ink);
    g.drawRect (pagePanel, 2);
    g.drawHorizontalLine (pagePanel.getY() + 30, (float) pagePanel.getX(), (float) pagePanel.getRight());
    g.setColour (ink.withAlpha (0.25f));
    for (int s = 1; s < slotsPerRow; ++s)
        g.drawVerticalLine (slotBounds (s).getX(), (float) controlRow.getY() + 6.0f, (float) controlRow.getBottom() - 6.0f);

    g.setColour (ink);
    g.setFont (panelFont (10.5f));
    for (auto& c : controls)
        if (c->component().isVisible())
            g.drawText (c->label, slotBounds (c->slot).withHeight (18).translated (0, 2), juce::Justification::centred);

    if (page == Page::shape)
        paintPreview (g);

    if (dragHover)
    {
        g.setColour (rust);
        g.drawRect (getLocalBounds(), 4);
    }
}
