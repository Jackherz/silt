#pragma once

#include "PluginProcessor.h"
#include "kit/Presets.h"

// Presets: a sound (every parameter, the modulation routes and the shown
// modulator), not the sample, the controller mapping or the window. A factory
// set is built in; user presets are .siltpreset files (XML) in the standard
// preset folder:
//   macOS    ~/Library/Audio/Presets/Glyph Audio/<plug-in>/
//   Windows  %APPDATA%/Glyph Audio/<plug-in>/Presets/
// The browser, the bar and the file handling are the kit's; this is Silt's part.
class PresetManager final : public kit::PresetLibrary
{
public:
    // The browser's categories, in order (ALL and FAVOURITES are views, not categories).
    static const juce::StringArray& categories();   // PADS, TEXTURES, DRONES, TONAL, RHYTHMIC, SLICES, USER

    // folder: where user presets live (tests pass their own).
    explicit PresetManager (SiltProcessor&, juce::File folder = defaultFolder());

    static juce::File defaultFolder();

    static int numFactoryPresets();
    static std::unique_ptr<juce::XmlElement> factoryPreset (int index);

    const juce::StringArray& categoryList() const override { return categories(); }
    int factoryCount() const override { return numFactoryPresets(); }
    juce::String factoryName (int index) const override;
    juce::String factoryCategory (int index) const override;
    std::unique_ptr<juce::XmlElement> factoryXml (int index) const override { return factoryPreset (index); }
    std::unique_ptr<juce::XmlElement> capture (const juce::String& name) const override { return silt.capturePreset (name); }
    void apply (const juce::XmlElement& xml, const juce::String& name) override { silt.applyPreset (xml, name); }
    void applyQuietly (const juce::XmlElement& xml, const juce::String& name) override { silt.applyPresetValues (xml, name); }
    void commit (std::unique_ptr<juce::XmlElement> before, const juce::String& beforeName) override { silt.commitPresetChange (std::move (before), beforeName); }
    juce::String currentName() const override { return silt.presetName; }
    void setCurrentName (const juce::String& name) override { silt.presetName = name; silt.markPresetClean(); }
    bool isEdited() const override { return silt.presetIsEdited(); }
    juce::String partName() const override { return "effects"; }
    void applyPart (const juce::XmlElement& xml) override { silt.applyPresetEffects (xml); }

private:
    SiltProcessor& silt;
};

using PresetBrowser = kit::PresetBrowser;
using PresetBar = kit::PresetBar;
