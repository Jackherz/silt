#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

// The panel's palette and type (plan §5), shared by every part of the UI.
namespace silt::ui
{
// The colours by role. A theme sets them all (applyTheme), so they're
// variables: read them when drawing, not once at start-up.
inline juce::Colour chassis { 0xffd8ccb2 };       // the panel
inline juce::Colour ink { 0xff2a2420 };           // text and lines on the panel and keys
inline juce::Colour inkSoft { 0xff6b6157 };
inline juce::Colour bone { 0xffefe6cf };          // key caps, cards, menus
inline juce::Colour boneLip { 0xffa99e85 };
inline juce::Colour frame { 0xff2a2420 };         // always-dark surfaces: the header band, the screen's bezel
inline juce::Colour onFrame { 0xffefe6cf };       // text on them, and on red
inline juce::Colour rust { 0xffa64826 };          // the accent
inline juce::Colour rustLip { 0xff6e2f17 };
inline juce::Colour signalRed { 0xffc8342b };
inline juce::Colour lcdGreen { 0xffb6c46b };      // the LCD and value boxes
inline juce::Colour lcdText { 0xff26301a };
inline juce::Colour screenBlue { 0xff283070 };    // the screen
inline juce::Colour screenLight { 0xff6a78c8 };
inline juce::Colour screenPale { 0xffa7b0ec };
inline juce::Colour screenLime { 0xffc9cf6a };
inline juce::Colour playheadCyan { 0xff70e0f0 };
inline juce::Colour insetBack { 0xff1b2152 };     // the screen's corner pictures
inline juce::Colour knobFace { 0xfff3ecdc };      // the knobs: domed cap, recessed collar, ticks, value dot
inline juce::Colour knobCollar { 0xff2e2925 };
inline juce::Colour knobTick { 0xffefe6cf };
inline juce::Colour knobDot { 0xff2a2420 };
inline juce::Colour knobCap { 0xff1d1a17 };
inline juce::Colour knobSkirt { 0xff3a332d };

struct Theme
{
    const char* name;
    juce::uint32 chassis, ink, inkSoft, bone, boneLip, frame, onFrame, rust, rustLip, lcdGreen, lcdText,
                 screenBlue, screenLight, screenPale, screenLime, playhead, insetBack;
    juce::uint32 knobFace, knobCollar, knobTick, knobDot;
    int grainLook;   // how the screen draws grains: 0 dots, 1 stars, 2 shore, 3 film, 4 clouds
};

inline const std::array<Theme, 5>& themes()
{
    static const std::array<Theme, 5> list { {
        { "Cream", 0xffd8ccb2, 0xff2a2420, 0xff6b6157, 0xffefe6cf, 0xffa99e85, 0xff2a2420, 0xffefe6cf, 0xffa64826, 0xff6e2f17,
          0xffb6c46b, 0xff26301a, 0xff283070, 0xff6a78c8, 0xffa7b0ec, 0xffc9cf6a, 0xff70e0f0, 0xff1b2152,
          0xfff3ecdc, 0xff2e2925, 0xffefe6cf, 0xff2a2420, 4 },
        { "Night", 0xff2a2724, 0xffe9e0cc, 0xffa39888, 0xff3b3631, 0xff1a1715, 0xff131110, 0xffe9e0cc, 0xffd0683c, 0xff7a3418,
          0xff9fb05a, 0xff1d2412, 0xff1c2250, 0xff5562b0, 0xff97a2e6, 0xffc9cf6a, 0xff70e0f0, 0xff121738,
          0xffe9e2d0, 0xff141210, 0xffe9e2d0, 0xff232624, 1 },
        { "Slate", 0xffc5ccd0, 0xff1f262b, 0xff5a6670, 0xffeef1f2, 0xff9aa5ac, 0xff1f262b, 0xffeef1f2, 0xff2f7f8a, 0xff1b4c53,
          0xffe2b65a, 0xff3a2a08, 0xff16352f, 0xff3f8f7c, 0xff8fd8c2, 0xffe2d36a, 0xfff0f0a0, 0xff0f2622,
          0xfff2f4f5, 0xff1f262b, 0xffe6ebee, 0xff1f262b, 2 },
        { "Moss", 0xffc3c7a8, 0xff23261b, 0xff5d6250, 0xffecedd9, 0xff9da087, 0xff23261b, 0xffecedd9, 0xffb0602a, 0xff6a3412,
          0xffd3d9a0, 0xff2a2f12, 0xff2b1d0e, 0xff8a5a24, 0xffe0a860, 0xfff0d070, 0xfffff0b0, 0xff1e140a,
          0xfff1f0df, 0xff23261b, 0xffecedd9, 0xff23261b, 3 },
        // Graphite: a matt dark panel, cream knobs and keys, values in dark sunk windows.
        { "Graphite", 0xff262a27, 0xffe9e2d0, 0xffa9a291, 0xff3a3f3b, 0xff101211, 0xff151716, 0xffe9e2d0, 0xffe86a2c, 0xff8a3a14,
          0xff141615, 0xffe9e2d0, 0xff1c2729, 0xff4c6468, 0xffb8ccc6, 0xffe9c46a, 0xffe86a2c, 0xff152022,
          0xffefe8d6, 0xff151716, 0xffe9e2d0, 0xff232624, 0 },
    } };
    return list;
}

constexpr int defaultTheme = 4;   // Graphite
inline int& currentThemeIndex() { static int index = 0; return index; }

inline void applyTheme (int index)
{
    index = juce::jlimit (0, (int) themes().size() - 1, index);
    currentThemeIndex() = index;
    const auto& t = themes()[(size_t) index];
    chassis = juce::Colour (t.chassis); ink = juce::Colour (t.ink); inkSoft = juce::Colour (t.inkSoft);
    bone = juce::Colour (t.bone); boneLip = juce::Colour (t.boneLip); frame = juce::Colour (t.frame); onFrame = juce::Colour (t.onFrame);
    rust = juce::Colour (t.rust); rustLip = juce::Colour (t.rustLip); lcdGreen = juce::Colour (t.lcdGreen); lcdText = juce::Colour (t.lcdText);
    screenBlue = juce::Colour (t.screenBlue); screenLight = juce::Colour (t.screenLight); screenPale = juce::Colour (t.screenPale);
    screenLime = juce::Colour (t.screenLime); playheadCyan = juce::Colour (t.playhead); insetBack = juce::Colour (t.insetBack);
    knobFace = juce::Colour (t.knobFace); knobCollar = juce::Colour (t.knobCollar); knobTick = juce::Colour (t.knobTick); knobDot = juce::Colour (t.knobDot);
}

inline juce::Font panelFont (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
inline juce::Font monoFont (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }
} // namespace silt::ui
