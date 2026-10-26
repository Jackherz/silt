#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The panel's palette and type (plan §5), shared by every part of the UI.
namespace silt::ui
{
inline const juce::Colour chassis { 0xffd8ccb2 };
inline const juce::Colour ink { 0xff2a2420 };
inline const juce::Colour inkSoft { 0xff6b6157 };
inline const juce::Colour bone { 0xffefe6cf };
inline const juce::Colour boneLip { 0xffa99e85 };
inline const juce::Colour rust { 0xffa64826 };
inline const juce::Colour rustLip { 0xff6e2f17 };
inline const juce::Colour signalRed { 0xffc8342b };
inline const juce::Colour lcdGreen { 0xffb6c46b };
inline const juce::Colour lcdText { 0xff26301a };
inline const juce::Colour screenBlue { 0xff283070 };
inline const juce::Colour screenLight { 0xff6a78c8 };
inline const juce::Colour screenPale { 0xffa7b0ec };
inline const juce::Colour screenLime { 0xffc9cf6a };
inline const juce::Colour playheadCyan { 0xff70e0f0 };
inline const juce::Colour knobCap { 0xff1d1a17 };
inline const juce::Colour knobSkirt { 0xff3a332d };

inline juce::Font panelFont (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
inline juce::Font monoFont (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }
} // namespace silt::ui
