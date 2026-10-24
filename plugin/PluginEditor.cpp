#include "PluginEditor.h"

namespace
{
// Palette tokens from plan §5.
const juce::Colour chassis { 0xffd8ccb2 };
const juce::Colour ink { 0xff2a2420 };
const juce::Colour bone { 0xffefe6cf };
const juce::Colour lcdGreen { 0xffb6c46b };
const juce::Colour lcdText { 0xff26301a };
const juce::Colour rust { 0xffa64826 };

constexpr float meterFloorDb = -60.0f;

juce::String sourceName (SiltProcessor::CaptureSource source, int channels)
{
    switch (source)
    {
        case SiltProcessor::CaptureSource::sidechain: return "SIDECHAIN (" + juce::String (channels) + " CH)";
        case SiltProcessor::CaptureSource::mainInput: return "MAIN INPUT (" + juce::String (channels) + " CH)";
        case SiltProcessor::CaptureSource::none: break;
    }
    return "NONE";
}
} // namespace

SiltEditor::SiltEditor (SiltProcessor& p)
    : AudioProcessorEditor (p), silt (p)
{
    if constexpr (SiltProcessor::isInstrument)
    {
        monitorButton.setColour (juce::ToggleButton::textColourId, ink);
        monitorButton.setColour (juce::ToggleButton::tickColourId, ink);
        monitorButton.setColour (juce::ToggleButton::tickDisabledColourId, ink);
        addAndMakeVisible (monitorButton);
        monitorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            silt.params, "monitor", monitorButton);
    }

    setSize (640, 320);
    startTimerHz (30);
}

void SiltEditor::timerCallback()
{
    const float peak = silt.captureMeter.takePeak();
    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);

    // Fast attack, slow fall, like a hardware peak meter.
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);
    repaint();
}

void SiltEditor::resized()
{
    monitorButton.setBounds (24, getHeight() - 44, 200, 24);
}

void SiltEditor::paint (juce::Graphics& g)
{
    g.fillAll (chassis);

    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (52);
    g.setColour (ink);
    g.fillRect (header);
    g.setColour (bone);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (juce::String (JucePlugin_Name).toUpperCase(), header.reduced (14, 0), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String ("M0 TEST BUILD  v") + JucePlugin_VersionString, header.reduced (14, 0),
                juce::Justification::centredRight);

    area.removeFromTop (16);
    auto lcd = area.removeFromTop (150);
    g.setColour (ink);
    g.fillRect (lcd);
    lcd.reduce (3, 3);
    g.setColour (lcdGreen);
    g.fillRect (lcd);

    const int note = silt.lastNote.load();
    const double bpm = silt.hostBpm.load();
    const juce::String lines[] {
        "CAPTURE  " + sourceName (silt.captureSource.load(), silt.captureChannels.load()),
        "LEVEL    " + (levelDb <= meterFloorDb ? juce::String ("-INF") : juce::String (levelDb, 1) + " DB"),
        "MIDI     " + juce::String (silt.noteOnCount.load()) + " NOTES"
            + (note >= 0 ? ", LAST " + juce::MidiMessage::getMidiNoteName (note, true, true, 3) : juce::String()),
        "HOST     " + (bpm > 0.0 ? juce::String (bpm, 2) + " BPM" : juce::String ("NO TEMPO"))
            + (silt.hostPlaying.load() ? ", PLAYING" : ", STOPPED"),
    };

    auto text = lcd.reduced (14, 10);
    g.setColour (lcdText);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
    for (const auto& line : lines)
        g.drawText (line, text.removeFromTop (26), juce::Justification::centredLeft);

    // Level bar: segments, lit up to the current level.
    auto bar = text.removeFromTop (16);
    constexpr int segments = 30;
    const float lit = juce::jmap (juce::jlimit (meterFloorDb, 0.0f, levelDb), meterFloorDb, 0.0f, 0.0f, (float) segments);
    const float segW = (float) bar.getWidth() / segments;
    for (int i = 0; i < segments; ++i)
    {
        g.setColour (i < (int) lit ? (i >= segments - 4 ? rust : lcdText) : lcdText.withAlpha (0.15f));
        g.fillRect (juce::Rectangle<float> ((float) bar.getX() + i * segW, (float) bar.getY(), segW - 2.0f, (float) bar.getHeight()));
    }

    if constexpr (! SiltProcessor::isInstrument)
    {
        g.setColour (ink);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("PASSING INPUT THROUGH", getLocalBounds().removeFromBottom (44).reduced (24, 10),
                    juce::Justification::centredLeft);
    }
}
