#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Silt's settings on this computer (not in any session): whether the tour has
// been seen, the colour theme.
//   macOS    ~/Library/Application Support/Silt Audio/Silt.settings
namespace silt::ui
{
inline std::unique_ptr<juce::PropertiesFile> settingsFile()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Silt";
    options.folderName = "Silt Audio";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}
} // namespace silt::ui
