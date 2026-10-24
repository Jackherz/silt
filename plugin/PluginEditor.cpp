#include "PluginEditor.h"

namespace
{
// Palette tokens from plan §5.
const juce::Colour chassis { 0xffd8ccb2 };
const juce::Colour ink { 0xff2a2420 };
const juce::Colour bone { 0xffefe6cf };
const juce::Colour boneLip { 0xffa99e85 };
const juce::Colour rust { 0xffa64826 };
const juce::Colour rustLip { 0xff6e2f17 };
const juce::Colour signalRed { 0xffc8342b };
const juce::Colour lcdGreen { 0xffb6c46b };
const juce::Colour lcdText { 0xff26301a };
const juce::Colour screenBlue { 0xff283070 };
const juce::Colour screenLight { 0xff6a78c8 };
const juce::Colour screenLime { 0xffc9cf6a };
const juce::Colour knobCap { 0xff1d1a17 };
const juce::Colour knobSkirt { 0xff3a332d };

constexpr int editorWidth = 1040, editorHeight = 660;

juce::Font panelFont (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
juce::Font lcdFont (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }

void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (ink);
    g.drawRect (area, 2);
    auto strip = area.removeFromTop (18);
    g.fillRect (strip);
    g.setColour (bone);
    g.setFont (panelFont (10.0f));
    g.drawText (title, strip.reduced (8, 0), juce::Justification::centredLeft);
}
} // namespace

// Knobs with a printed scale and keys with a lip, per plan §5 "Parts".
class SiltEditor::PanelLook final : public juce::LookAndFeel_V4
{
public:
    PanelLook()
    {
        setColour (juce::Slider::textBoxTextColourId, lcdText);
        setColour (juce::Slider::textBoxBackgroundColourId, lcdGreen);
        setColour (juce::Slider::textBoxOutlineColourId, ink);
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

        g.setColour (ink);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const float outer = i % 5 == 0 ? radius : radius - 2.5f;
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius - 6.0f, a),
                                           centre.getPointOnCircumference (outer, a)), 1.5f);
        }

        const float skirt = radius - 8.0f;
        g.setColour (knobSkirt);
        g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre));
        g.setColour (knobCap);
        g.fillEllipse (juce::Rectangle<float> (skirt * 1.55f, skirt * 1.55f).withCentre (centre));

        const float angle = startAngle + pos * (endAngle - startAngle);
        g.setColour (bone);
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (skirt * 0.25f, angle),
                                       centre.getPointOnCircumference (skirt * 0.72f, angle)), 3.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const auto cap = b.getProperties().getWithDefault ("red", false) ? signalRed : (on ? rust : bone);
        const auto lip = b.getProperties().getWithDefault ("red", false) ? rustLip : (on ? rustLip : boneLip);

        g.setColour (lip);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (hover ? cap.brighter (0.06f) : cap);
        g.fillRoundedRectangle (r.withTrimmedBottom (down ? 1.0f : 4.0f), 2.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return panelFont (12.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return lcdFont (13.0f); }
    juce::Font getPopupMenuFont() override { return lcdFont (13.0f); }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (lcdFont (13.0f));
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::backgroundColourId, lcdGreen);
        l->setColour (juce::Label::textColourId, lcdText);
        l->setColour (juce::Label::outlineColourId, ink);
        l->setColour (juce::Label::backgroundWhenEditingColourId, bone);
        l->setColour (juce::Label::textWhenEditingColourId, ink);
        l->setColour (juce::Label::outlineWhenEditingColourId, ink);
        l->setColour (juce::TextEditor::highlightColourId, rust.withAlpha (0.4f));
        return l;
    }
};

// The sample: min/max columns, the region, and a playhead per sounding voice.
class SiltEditor::SampleView final : public juce::Component
{
public:
    void setOverview (SiltProcessor::Overview o) { overview = std::move (o); repaint(); }
    void setRegion (float s, float len) { start = s; length = len; }
    void setPlayheads (const std::array<float, silt::Engine::numVoices>& p) { playheads = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (ink);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        r.reduce (10, 10);
        g.setColour (screenLight);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        r.reduce (14, 10);
        g.setColour (screenBlue);
        g.fillRect (r);

        auto text = r.reduced (8, 4);
        g.setFont (lcdFont (12.0f));
        g.setColour (juce::Colours::white);

        if (overview.minimum.empty())
        {
            g.drawText ("**** NO SAMPLE ****", text, juce::Justification::centredTop);
            g.setColour (screenLight.brighter (0.4f));
            g.drawText ("DROP AN AUDIO FILE HERE, PRESS LOAD, OR ARM AND RECORD", text, juce::Justification::centred);
            g.setColour (juce::Colours::white);
            g.drawText ("READY.", text, juce::Justification::bottomLeft);
            return;
        }

        g.drawText (overview.name.toUpperCase(), text, juce::Justification::topLeft);
        g.drawText (juce::String (overview.seconds, 2) + "S", text, juce::Justification::topRight);

        auto wave = r.reduced (8, 24).toFloat();
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        const float regionStart = wave.getX() + start * wave.getWidth();
        const float regionEnd = juce::jmin (wave.getRight(), regionStart + length * wave.getWidth());

        const int cols = (int) overview.minimum.size();
        const float colW = wave.getWidth() / (float) cols;
        for (int c = 0; c < cols; ++c)
        {
            const float x = wave.getX() + c * colW;
            const bool inRegion = x >= regionStart && x <= regionEnd;
            g.setColour (inRegion ? juce::Colours::white : screenLight);
            const float top = mid - overview.maximum[(size_t) c] * half;
            const float bottom = mid - overview.minimum[(size_t) c] * half;
            g.fillRect (x, top, juce::jmax (1.0f, colW), juce::jmax (1.0f, bottom - top));
        }

        g.setColour (screenLime);
        g.drawVerticalLine ((int) regionStart, wave.getY() - 6.0f, wave.getBottom() + 6.0f);
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb6START"), juce::Rectangle<float> (regionStart + 3.0f, wave.getY() - 14.0f, 80.0f, 12.0f),
                    juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff70e0f0));
        int number = 1;
        for (float p : playheads)
        {
            if (p < 0.0f)
                continue;
            const float x = wave.getX() + p * wave.getWidth();
            g.drawVerticalLine ((int) x, wave.getY(), wave.getBottom());
            g.drawText (juce::String (number++), juce::Rectangle<float> (x - 8.0f, wave.getBottom() + 2.0f, 16.0f, 12.0f),
                        juce::Justification::centred);
        }

        g.setColour (juce::Colours::white);
        g.drawText ("READY.", text, juce::Justification::bottomLeft);
    }

private:
    SiltProcessor::Overview overview;
    float start = 0.0f, length = 1.0f;
    std::array<float, silt::Engine::numVoices> playheads {};
};

SiltEditor::SiltEditor (SiltProcessor& p)
    : AudioProcessorEditor (p), silt (p),
      look (std::make_unique<PanelLook>()), sampleView (std::make_unique<SampleView>())
{
    setLookAndFeel (look.get());
    addAndMakeVisible (*sampleView);

    addKnob (grainKnobs, "pitch", "PITCH");
    addKnob (grainKnobs, "size", "SIZE");
    addKnob (grainKnobs, "rate", "RATE");
    addKnob (grainKnobs, "spray", "SPRAY");
    addKnob (grainKnobs, "reverse", "REVERSE");
    addKnob (grainKnobs, "randAmp", "RND AMP");
    addKnob (grainKnobs, "fade", "FADE");
    addKnob (grainKnobs, "slant", "SLANT");
    addKnob (grainKnobs, "ease", "EASE");

    addKnob (syncedKnobs, "sizeDiv", "SIZE");
    addKnob (syncedKnobs, "rateDiv", "RATE");
    for (auto* b : { &sizeSyncButton, &rateSyncButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    sizeSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "sizeSync", sizeSyncButton);
    rateSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "rateSync", rateSyncButton);

    addKnob (regionKnobs, "start", "START");
    addKnob (regionKnobs, "length", "LENGTH");
    addKnob (regionKnobs, "scan", "SCAN");

    addKnob (voiceKnobs, "attack", "ATTACK");
    addKnob (voiceKnobs, "decay", "DECAY");
    addKnob (voiceKnobs, "sustain", "SUSTAIN");
    addKnob (voiceKnobs, "release", "RELEASE");
    addKnob (voiceKnobs, "root", "ROOT");

    addKnob (outKnobs, "gain", "OUTPUT");
    if constexpr (! SiltProcessor::isInstrument)
        addKnob (outKnobs, "mix", "MIX");

    for (auto* box : { &captureBars, &endMode })
        addAndMakeVisible (*box);
    captureBars.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    endMode.addItemList ({ "LOOP", "HOLD", "STOP" }, 1);
    captureBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "captureBars", captureBars);
    endModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (silt.params, "endMode", endMode);

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
        monitorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (silt.params, "monitor", monitorButton);
    }

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

SiltEditor::~SiltEditor()
{
    setLookAndFeel (nullptr);
}

void SiltEditor::addKnob (std::vector<std::unique_ptr<Knob>>& row, const char* paramId, const juce::String& label)
{
    auto knob = std::make_unique<Knob>();
    knob->label = label;
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    addAndMakeVisible (knob->slider);
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (silt.params, paramId, knob->slider);
    row.push_back (std::move (knob));
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
        case SiltProcessor::CaptureSource::sidechain: source = "SIDECHAIN"; break;
        case SiltProcessor::CaptureSource::mainInput: source = "INPUT"; break;
        case SiltProcessor::CaptureSource::none: source = "NO INPUT"; break;
    }
    const auto level = levelDb <= -60.0f ? juce::String ("-INF") : juce::String (levelDb, 0);
    return source + " " + level + "DB  " + juce::String (silt.hostBpm.load(), 1) + " BPM  "
         + juce::String (silt.activeGrains.load()) + " GRAINS";
}

void SiltEditor::timerCallback()
{
    const float db = juce::Decibels::gainToDecibels (silt.captureMeter.takePeak(), -100.0f);
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);

    if (const int generation = silt.getSampleGeneration(); generation != seenGeneration)
    {
        seenGeneration = generation;
        sampleView->setOverview (silt.getOverview());
    }

    sampleView->setRegion (silt.params.getRawParameterValue ("start")->load() * 0.01f,
                           silt.params.getRawParameterValue ("length")->load() * 0.01f);

    std::array<float, silt::Engine::numVoices> heads {};
    for (size_t i = 0; i < heads.size(); ++i)
        heads[i] = silt.voicePlayheads[i].load();
    sampleView->setPlayheads (heads);

    const bool sizeSynced = sizeSyncButton.getToggleState(), rateSynced = rateSyncButton.getToggleState();
    grainKnobs[1]->slider.setVisible (! sizeSynced);
    syncedKnobs[0]->slider.setVisible (sizeSynced);
    grainKnobs[2]->slider.setVisible (! rateSynced);
    syncedKnobs[1]->slider.setVisible (rateSynced);

    const bool capturing = silt.captureState.load() != SiltProcessor::CaptureState::idle;
    armButton.setButtonText (capturing ? "CANCEL" : juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ARM")));
    repaint (header);
    repaint (captureRow);
}

void SiltEditor::layoutRow (std::vector<std::unique_ptr<Knob>>& row, juce::Rectangle<int> area)
{
    if (row.empty())
        return;
    const int w = area.getWidth() / (int) row.size();
    for (auto& k : row)
        k->slider.setBounds (area.removeFromLeft (w).withTrimmedTop (16).reduced (4, 0));
}

void SiltEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    header = r.removeFromTop (52);
    r.removeFromTop (10);
    sampleView->setBounds (r.removeFromTop (210));
    r.removeFromTop (10);

    captureRow = r.removeFromTop (40);
    auto row = captureRow;
    armButton.setBounds (row.removeFromLeft (110).reduced (0, 2));
    row.removeFromLeft (8);
    captureBars.setBounds (row.removeFromLeft (110).reduced (0, 6));
    row.removeFromLeft (8);
    loadButton.setBounds (row.removeFromLeft (90).reduced (0, 2));
    row.removeFromLeft (8);
    if constexpr (SiltProcessor::isInstrument)
        monitorButton.setBounds (row.removeFromRight (110).reduced (0, 6));
    r.removeFromTop (10);

    grainSection = r.removeFromTop (150);
    auto grainArea = grainSection.withTrimmedTop (22).reduced (6, 4);
    const auto syncRow = grainArea.removeFromBottom (18);
    layoutRow (grainKnobs, grainArea);

    // The synced knobs sit exactly over Size and Rate, with SYNC under each.
    for (int i = 0; i < 2; ++i)
    {
        const auto& knob = grainKnobs[(size_t) i + 1]->slider;
        syncedKnobs[(size_t) i]->slider.setBounds (knob.getBounds());
        (i == 0 ? sizeSyncButton : rateSyncButton)
            .setBounds (knob.getBounds().withY (syncRow.getY()).withHeight (syncRow.getHeight()).withSizeKeepingCentre (56, 18));
    }
    r.removeFromTop (10);

    auto lower = r;
    regionSection = lower.removeFromLeft (370);
    lower.removeFromLeft (10);
    outSection = lower.removeFromRight (SiltProcessor::isInstrument ? 110 : 200);
    lower.removeFromRight (10);
    voiceSection = lower;

    auto regionArea = regionSection.withTrimmedTop (22).reduced (6, 4);
    endMode.setBounds (regionArea.removeFromRight (84).withSizeKeepingCentre (80, 24));
    layoutRow (regionKnobs, regionArea);
    layoutRow (voiceKnobs, voiceSection.withTrimmedTop (22).reduced (6, 4));
    layoutRow (outKnobs, outSection.withTrimmedTop (22).reduced (6, 4));
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

    auto stripes = header.withTrimmedLeft (150).withWidth (220).withSizeKeepingCentre (220, 19);
    for (auto c : { rust, juce::Colour (0xffd9a03a), juce::Colour (0xff8fa27a), bone })
    {
        g.setColour (c);
        g.fillRect (stripes.removeFromTop (4));
        stripes.removeFromTop (1);
    }

    auto lcd = header.withTrimmedLeft (400).reduced (10, 9);
    g.setColour (juce::Colour (0xff14110f));
    g.fillRect (lcd);
    g.setColour (lcdGreen);
    g.fillRect (lcd.reduced (3));
    g.setColour (lcdText);
    g.setFont (lcdFont (15.0f));
    g.drawText (statusText(), lcd.reduced (12, 0), juce::Justification::centredLeft);

    drawSection (g, grainSection, "GRAIN");
    drawSection (g, regionSection, "REGION");
    drawSection (g, voiceSection, "VOICE");
    drawSection (g, outSection, "OUT");

    g.setColour (ink);
    g.setFont (panelFont (10.0f));
    for (auto* row : { &grainKnobs, &regionKnobs, &voiceKnobs, &outKnobs })
        for (auto& k : *row)
            g.drawText (k->label, k->slider.getBounds().withY (k->slider.getY() - 15).withHeight (14),
                        juce::Justification::centred);

    if (dragHover)
    {
        g.setColour (rust);
        g.drawRect (getLocalBounds(), 4);
    }
}
