#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// SPACE's picture: the sample's waveform adrift in a night sky filling the
// whole screen. Each control is part of the scene:
//   DECAY     fades from a close, small space (fewer, larger stars) to a vast
//             one (dense faint stars, the Milky Way)
//   SIZE      depth: the Milky Way band comes in
//   REVERB    nebula wisps, breathing with what SPACE is adding now
//   TONE      warm dust (dark) to blue nebula and twinkling (bright)
//   SHIMMER   four-point sparkles rising off the waveform
//   DELAY     light echoes: each note flares, and every echo sends a ring of
//             lit dust out from it, so TIME is the spacing between rings,
//             FEEDBACK how many (each fainter), DELAY how bright; ping-pong
//             alternates the rings between two stars side by side
//   FREEZE    the sky stops and a cold hush falls
//   DUCK      the sky dims while a note sounds
// The whole scene fades in with DELAY and REVERB; with both down it's not there.
//
// The sky is made once per screen size, at the screen's own scale and seamless
// across (so its drift wraps round): every control is then a fade between
// layers already drawn, never a redraw.
namespace silt::ui
{
class SpaceScene
{
public:
    struct Settings
    {
        float decay = 0.5f;          // 0…1, DECAY on its log scale
        float size = 0.5f, reverb = 0.0f, tone = 0.5f, shimmer = 0.0f;
        float delay = 0.0f, feedback = 0.0f;
        float echoSeconds = 0.4f;    // TIME
        bool pingPong = true, frozen = false;
        float wet = 0.0f;            // what SPACE is adding now, 0…1
        float sinceNote = 99.0f;     // seconds since the latest note
        float notePlace = 0.5f;      // where across the sample the latest note plays (0…1)
        float duck = 1.0f;           // DUCK's dip now (1: none)

        float mix() const { return juce::jmin (1.0f, 1.25f * juce::jmax (delay, reverb)); }
    };

    static bool visible (const Settings& s) { return s.mix() > 0.001f; }

    // Moves it on: the drift, the sparkles, the light echoes (nothing moves while frozen).
    void advance (const Settings&, float seconds, const std::vector<float>& envelope);

    // Behind the waveform and grains (sky, nebula, light echoes) and over them
    // (sparkles, the freeze's hush). area: the whole scene; wave: the
    // waveform's part of it, with its height (0…1 of half) per pixel across.
    void drawBehind (juce::Graphics&, juce::Rectangle<int> area, juce::Rectangle<int> wave, const std::vector<float>& envelope, const Settings&);
    void drawOver (juce::Graphics&, juce::Rectangle<int> area, juce::Rectangle<int> wave, const Settings&);

private:
    struct Star { float x, y, size; juce::Colour colour; float phase, rate; bool spikes; };
    struct Sparkle { float u, y, vy, vx, life, span; };
    struct Flare { float u, v, born; int seed; };   // where a note flared (shares of the scene) and when

    void buildSky (juce::Rectangle<int> area);
    void drawWrapped (juce::Graphics&, const juce::Image&, juce::Rectangle<int> area, float offset, float alpha) const;
    static void drawBrightStar (juce::Graphics&, juce::Point<float>, float size, juce::Colour, float alpha, bool spikes);

    // The sky at the screen's scale, twice its width so it can drift.
    int skyW = 0, skyH = 0;
    juce::Rectangle<int> builtFor;
    juce::Image band, nebulaWarm, nebulaCool, vastStars, closeStars, vignette;
    std::vector<Star> bright;

    float drift[2] {};   // vast, close: how far each has moved, in pixels
    float now = 0.0f, lastSinceNote = 99.0f;
    std::vector<Sparkle> sparkles;
    std::vector<Flare> flares;
    juce::Random random { 11 };
};
} // namespace silt::ui
