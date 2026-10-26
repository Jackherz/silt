#pragma once

#include "PluginProcessor.h"

// Presets: a sound (every parameter, the modulation routes and the shown
// modulator), not the sample, the controller mapping or the window. A small
// factory set is built in; user presets are .siltpreset files (XML) in the
// standard preset folder:
//   macOS    ~/Library/Audio/Presets/Silt Audio/<plug-in>/
//   Windows  %APPDATA%/Silt Audio/<plug-in>/Presets/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
    };

    // folder: where user presets live (tests pass their own).
    explicit PresetManager (SiltProcessor&, juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    const juce::File& getFolder() const noexcept { return folder; }

    std::vector<Entry> list() const;           // factory first, then user, by name
    bool load (const Entry&);
    bool save (const juce::String& name);      // as a user preset (replaces one of that name)
    bool step (int direction);                 // the next or previous in list()
    juce::String currentName() const { return silt.presetName; }

    static int numFactoryPresets();
    static std::unique_ptr<juce::XmlElement> factoryPreset (int index);

private:
    SiltProcessor& silt;
    juce::File folder;
};

// The header's preset bar: previous, the name (click for the list, SAVE AS…,
// the folder), next, and SAVE.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (SiltProcessor&);
    void resized() override;
    void refresh();   // shows the current preset's name

private:
    void showMenu();
    void saveAs();

    PresetManager presets;
    juce::TextButton previous, next, name, save { "SAVE" };
    std::unique_ptr<juce::AlertWindow> asking;
};
