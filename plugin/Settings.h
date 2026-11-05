#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Silt's settings on this computer (not in any session): whether the tour has
// been seen, the colour theme.
//   macOS    ~/Library/Application Support/Glyph Audio/Silt.settings
namespace silt::ui
{
inline std::unique_ptr<juce::PropertiesFile> settingsFile()
{
    // From before the range had a name.
    const auto data = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto folder = data.getChildFile ("Application Support/Glyph Audio");
    if (const auto old = data.getChildFile ("Application Support/Silt Audio/Silt.settings"); old.existsAsFile() && ! folder.getChildFile ("Silt.settings").exists())
    {
        folder.createDirectory();
        old.copyFileTo (folder.getChildFile ("Silt.settings"));
    }

    juce::PropertiesFile::Options options;
    options.applicationName = "Silt";
    options.folderName = "Glyph Audio";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}
} // namespace silt::ui
