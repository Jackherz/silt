#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// SPACE's picture: the sample's waveform adrift in a night sky, behind and
// around it on the screen. Each control is part of the scene:
//   DECAY     the camera pulls back: more of the sky, more stars
//   SIZE      depth: the Milky Way and the far stars come in
//   REVERB    nebula wisps, breathing with what SPACE is adding now
//   TONE      warm dust and orange stars (dark) to blue nebula and twinkling (bright)
//   SHIMMER   four-point sparkles rising off the waveform
//   DELAY     each echo a ghost of the waveform made of star-dust, further back
//             and fainter each repeat, sinking into the grains' part of the sky
//             (TIME spaces them, FEEDBACK says how many, ping-pong swings them
//             side to side), flashing as it fires
//   FREEZE    the sky stops and a cold hush falls
//   DUCK      the sky dims while a note sounds
// The whole scene fades in with DELAY and REVERB; with both down it's not there.
//
// The sky is made once per screen size (seamless, so its drift wraps round)
// and drawn into composites when DECAY, SIZE or TONE change; a frame is then a
// few image copies plus the bright stars, echoes and sparkles.
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
        float duck = 1.0f;           // DUCK's dip now (1: none)

        float mix() const { return juce::jmin (1.0f, 1.25f * juce::jmax (delay, reverb)); }
    };

    static bool visible (const Settings& s) { return s.mix() > 0.001f; }

    // Moves it on: the drift and the sparkles (nothing moves while frozen).
    void advance (const Settings&, float seconds, const std::vector<float>& envelope);

    // Behind the waveform and grains (sky, nebula, echoes) and over them
    // (sparkles, the freeze's hush). area: the whole scene; wave: the
    // waveform's part of it, with its height (0…1 of half) per pixel across.
    void drawBehind (juce::Graphics&, juce::Rectangle<int> area, juce::Rectangle<int> wave, const std::vector<float>& envelope, const Settings&);
    void drawOver (juce::Graphics&, juce::Rectangle<int> area, juce::Rectangle<int> wave, const Settings&);

private:
    struct Star { float x, y, size; juce::Colour colour; float phase, rate; bool spikes; };
    struct Sparkle { float u, y, vy, vx, life, span; };

    void buildSky (juce::Rectangle<int> area);
    void buildComposites (juce::Rectangle<int> area, const Settings&);
    float view (const Settings& s) const { return 0.45f + 0.55f * s.decay; }   // the share of the sky in view
    void drawWrapped (juce::Graphics&, const juce::Image&, juce::Rectangle<int> area, float offset, float alpha) const;

    // The sky, at its own size: clouds at a quarter of it, stars at full.
    int skyW = 0, skyH = 0;
    juce::Image band, nebulaWarm, nebulaCool, farStars, midStars, nearStars, grain;
    std::vector<Star> bright;
    // The sky as drawn now, scaled to the view: rebuilt when its key changes.
    juce::Image backComposite, nebulaComposite, nearComposite, vignette;
    juce::String compositeKey;
    float scale = 1.0f;

    float drift[3] {};   // far, middle, near: how far each has moved, in sky pixels
    float now = 0.0f;
    std::vector<Sparkle> sparkles;
    juce::Random random { 11 };
};
} // namespace silt::ui
