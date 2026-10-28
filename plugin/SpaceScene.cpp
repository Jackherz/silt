#include "SpaceScene.h"

#include <cmath>

using namespace silt::ui;

namespace
{
// Value noise that repeats exactly every `period` cells across, so the sky
// tiles with no seam as it drifts.
float hash (int x, int y, int salt)
{
    auto h = (juce::uint32) (x * 374761393 + y * 668265263 + salt * 1442695041);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / (float) 0xffffff;
}

float smooth (float t) { return t * t * (3.0f - 2.0f * t); }

float noise (float x, float y, int period, int salt)
{
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    const float xf = smooth (x - (float) xi), yf = smooth (y - (float) yi);
    const auto wrap = [period] (int v) { return ((v % period) + period) % period; };
    const float a = hash (wrap (xi), yi, salt), b = hash (wrap (xi + 1), yi, salt);
    const float c = hash (wrap (xi), yi + 1, salt), d = hash (wrap (xi + 1), yi + 1, salt);
    return a + (b - a) * xf + (c - a) * yf + (a - b - c + d) * xf * yf;
}

// Octaves of it; `cells` across the sky's width at the first, doubling each time.
float fbm (float x, float y, float width, int cells, int octaves, int salt)
{
    float v = 0.0f, amp = 0.5f;
    for (int o = 0; o < octaves; ++o)
    {
        const int n = cells << o;
        v += amp * noise (x / width * (float) n, y / width * (float) n, n, o + salt * 16);
        amp *= 0.5f;
    }
    return v;
}

// Star colours, orange through white to blue-white, as a photograph has them.
juce::Colour starColour (juce::Random& r)
{
    static const juce::uint32 colours[] { 0xffffb078, 0xffffd6aa, 0xfffff0e1, 0xffecf0ff, 0xffc4d6ff, 0xffa8c4ff };
    const float u = r.nextFloat();
    return juce::Colour (colours[u < 0.12f ? 0 : u < 0.32f ? 1 : u < 0.62f ? 2 : u < 0.82f ? 3 : u < 0.94f ? 4 : 5]);
}
} // namespace

void SpaceScene::buildSky (juce::Rectangle<int> area)
{
    // Twice and a bit the screen each way, so DECAY can pull well back.
    skyW = juce::roundToInt ((float) area.getWidth() * 2.2f) / 4 * 4;
    skyH = juce::roundToInt ((float) area.getHeight() * 2.2f) / 4 * 4;
    const int qw = skyW / 4, qh = skyH / 4;
    juce::Random r (7);

    // The clouds, a quarter size (drawn up soft): a curving Milky Way band with
    // dark dust lanes and a warmer core, and nebula wisps off it.
    band = juce::Image (juce::Image::ARGB, qw, qh, true);
    juce::Image wisps (juce::Image::ARGB, qw, qh, true);
    std::vector<float> density ((size_t) (qw * qh));
    {
        juce::Image::BitmapData bandPixels (band, juce::Image::BitmapData::writeOnly);
        juce::Image::BitmapData wispPixels (wisps, juce::Image::BitmapData::writeOnly);
        const float w = (float) skyW;
        for (int y = 0; y < qh; ++y)
            for (int x = 0; x < qw; ++x)
            {
                const float X = (float) x * 4.0f, Y = (float) y * 4.0f;
                const float across = ((Y / (float) skyH - 0.5f) - 0.22f * std::sin (6.2831853f * X / w)
                                      - 0.08f * std::sin (12.566f * X / w + 1.3f)) * 2.4f;
                const float core = std::exp (-across * across * 3.2f);
                const float mottle = fbm (X, Y, w, 15, 5, 1);
                const float dust = juce::jmax (0.0f, fbm (X, Y, w, 22, 5, 2) - 0.47f) * 3.2f * core;
                const float glow = juce::jmax (0.0f, core * (0.35f + 0.9f * mottle) - dust * 0.9f);
                density[(size_t) (y * qw + x)] = juce::jmin (1.0f, core * (0.4f + mottle));
                const float warm = std::exp (-across * across * 9.0f);
                bandPixels.setPixelColour (x, y, juce::Colour::fromFloatRGBA ((200.0f + 40.0f * warm) / 255.0f, (186.0f + 10.0f * warm) / 255.0f,
                                                                              (176.0f - 50.0f * warm) / 255.0f, juce::jmin (1.0f, glow * 0.47f)));
                const float wisp = juce::jmax (0.0f, fbm (X, Y, w, 10, 6, 3) - 0.52f) * 4.2f * (0.5f + fbm (X, Y, w, 43, 3, 4));
                wispPixels.setPixelColour (x, y, juce::Colours::white.withAlpha (juce::jmin (1.0f, wisp * 0.59f)));
            }
    }
    // The nebula in two colours, for TONE: warm dust (dark) and cool blue (bright).
    const auto tinted = [&] (juce::Colour colour)
    {
        juce::Image out (juce::Image::ARGB, qw, qh, true);
        juce::Image::BitmapData src (wisps, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < qh; ++y)
            for (int x = 0; x < qw; ++x)
                dst.setPixelColour (x, y, colour.withAlpha (src.getPixelColour (x, y).getFloatAlpha()));
        return out;
    };
    nebulaWarm = tinted (juce::Colour (0xffaa6040));
    nebulaCool = tinted (juce::Colour (0xff5c70be));

    // Stars: thicker along the band, mostly faint.
    const auto stars = [&] (int count, float maxSize, float brightness, float bandBias)
    {
        juce::Image img (juce::Image::ARGB, skyW, skyH, true);
        juce::Graphics g (img);
        for (int k = 0; k < count; ++k)
        {
            float x = r.nextFloat() * (float) skyW, y = r.nextFloat() * (float) skyH;
            const auto q = (size_t) (juce::jlimit (0, qh - 1, (int) (y / 4.0f)) * qw + juce::jlimit (0, qw - 1, (int) (x / 4.0f)));
            if (r.nextFloat() > 0.25f + bandBias * density[q])
            {
                x = r.nextFloat() * (float) skyW;
                y = r.nextFloat() * (float) skyH;
            }
            const float m = std::pow (r.nextFloat(), 3.2f);
            const float sz = 0.35f + maxSize * m;
            g.setColour (starColour (r).withAlpha (juce::jmin (1.0f, brightness * (0.25f + 0.75f * m))));
            if (sz < 1.1f)
                g.fillRect (x, y, sz, sz);
            else
                g.fillEllipse (x - sz * 0.6f, y - sz * 0.6f, sz * 1.2f, sz * 1.2f);
        }
        return img;
    };
    const float areaScale = (float) (skyW * skyH) / (2600.0f * 980.0f);   // as many per pixel whatever the size
    farStars = stars (juce::roundToInt (16000.0f * areaScale), 0.9f, 0.75f, 1.6f);
    midStars = stars (juce::roundToInt (4200.0f * areaScale), 1.4f, 0.9f, 1.0f);
    nearStars = stars (juce::roundToInt (900.0f * areaScale), 2.0f, 1.0f, 0.4f);

    // The few bright ones, drawn live so they twinkle.
    bright.clear();
    for (int k = 0; k < 26; ++k)
        bright.push_back ({ r.nextFloat() * (float) skyW, r.nextFloat() * (float) skyH, 0.35f + std::pow (r.nextFloat(), 2.0f) * 0.65f,
                            starColour (r), r.nextFloat() * 6.28f, 0.4f + r.nextFloat() * 1.4f, r.nextFloat() < 0.55f });

    // Film grain, tiled into the back of the sky.
    grain = juce::Image (juce::Image::ARGB, 128, 128, true);
    {
        juce::Image::BitmapData px (grain, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                px.setPixelColour (x, y, (r.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + 0.05f * r.nextFloat()));
    }
    compositeKey.clear();
}

void SpaceScene::buildComposites (juce::Rectangle<int> area, const Settings& s)
{
    // Rebuilt only when what they show changes (steps fine enough not to see).
    const float v = std::round (view (s) * 64.0f) / 64.0f;
    const auto key = juce::String (area.getWidth()) + "x" + juce::String (area.getHeight()) + "|" + juce::String (v, 4) + "|"
                   + juce::String (juce::roundToInt (s.size * 40.0f)) + "|" + juce::String (juce::roundToInt (s.tone * 40.0f));
    if (key == compositeKey && backComposite.isValid())
        return;
    compositeKey = key;

    scale = (float) area.getWidth() / ((float) skyW * v);
    const int cw = juce::jmax (1, juce::roundToInt ((float) skyW * scale)), ch = juce::jmax (1, juce::roundToInt ((float) skyH * scale));
    const auto into = [&] (juce::Image& target, std::initializer_list<std::pair<const juce::Image*, float>> layers)
    {
        target = juce::Image (juce::Image::ARGB, cw, ch, true);
        juce::Graphics g (target);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        for (const auto& [img, alpha] : layers)
            if (alpha > 0.005f)
            {
                g.setOpacity (juce::jmin (1.0f, alpha));
                g.drawImage (*img, 0, 0, cw, ch, 0, 0, img->getWidth(), img->getHeight());
            }
    };
    // SIZE: depth. The band and the far stars come in as it grows; TONE warms or cools.
    into (backComposite, { { &band, (0.25f + 0.75f * s.size) * (0.55f + 0.45f * s.tone) },
                           { &farStars, 0.15f + 0.85f * s.size },
                           { &midStars, 0.45f + 0.55f * s.size } });
    {
        juce::Graphics g (backComposite);
        g.setTiledImageFill (grain, 0, 0, 1.0f);
        g.fillAll();
    }
    into (nebulaComposite, { { &nebulaWarm, (1.0f - s.tone) * 1.3f }, { &nebulaCool, s.tone * 1.3f } });
    into (nearComposite, { { &nearStars, 1.0f } });

    // Darker corners, fixed to the screen.
    vignette = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics vg (vignette);
    const auto c = vignette.getBounds().toFloat().getCentre();
    vg.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c.x, c.y, juce::Colours::black.withAlpha (0.5f),
                                              c.x + (float) area.getWidth() * 0.62f, c.y, true));
    vg.fillAll();
}

void SpaceScene::drawWrapped (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> area, float offset, float alpha) const
{
    // The sky's vertical middle in the middle of the area; wrapped across.
    if (alpha <= 0.005f || ! img.isValid())
        return;
    const int w = img.getWidth();
    const int y = area.getY() - (img.getHeight() - area.getHeight()) / 2;
    int x = area.getX() - (int) std::fmod (offset * scale, (float) w);
    if (x > area.getX())
        x -= w;
    g.setOpacity (juce::jmin (1.0f, alpha));
    for (; x < area.getRight(); x += w)
        g.drawImageAt (img, x, y);
    g.setOpacity (1.0f);
}

void SpaceScene::advance (const Settings& s, float seconds, const std::vector<float>& envelope)
{
    if (s.frozen)
        return;   // time stops
    now += seconds;
    const float speed = 9.0f * (1.4f - s.size) * (float) skyW / 2600.0f;
    const float depths[] { 0.25f, 0.55f, 1.0f };
    for (int i = 0; i < 3; ++i)
        drift[i] = std::fmod (drift[i] + seconds * speed * depths[i], (float) juce::jmax (1, skyW));

    // SHIMMER: sparkles off the top of the waveform, more while SPACE is sounding.
    if (s.shimmer > 0.01f && ! envelope.empty())
    {
        float n = s.shimmer * 70.0f * (0.15f + s.wet) * seconds;
        for (; n > 0.0f; n -= 1.0f)
            if (random.nextFloat() < n)
            {
                const float u = random.nextFloat();
                const float height = envelope[(size_t) juce::jlimit (0, (int) envelope.size() - 1, (int) (u * (float) envelope.size()))];
                sparkles.push_back ({ u, -height, -(0.15f + 0.5f * s.shimmer) * (0.6f + random.nextFloat() * 0.8f), (random.nextFloat() - 0.5f) * 0.04f,
                                      0.0f, 1.2f + random.nextFloat() * 1.6f * (0.5f + s.shimmer) });
            }
    }
    for (auto& p : sparkles)
    {
        p.life += seconds;
        p.y += p.vy * seconds;
        p.u += p.vx * seconds;
    }
    sparkles.erase (std::remove_if (sparkles.begin(), sparkles.end(), [] (const Sparkle& p) { return p.life >= p.span; }), sparkles.end());
    if (sparkles.size() > 400)
        sparkles.erase (sparkles.begin(), sparkles.begin() + (long) (sparkles.size() - 400));
}

void SpaceScene::drawBehind (juce::Graphics& g, juce::Rectangle<int> area, juce::Rectangle<int> wave, const std::vector<float>& envelope, const Settings& s)
{
    if (area.isEmpty())
        return;
    if (skyW == 0 || (float) area.getWidth() * 2.2f > (float) skyW * 1.15f || (float) area.getWidth() * 2.2f < (float) skyW * 0.7f
        || (float) area.getHeight() * 2.2f > (float) skyH * 1.15f || (float) area.getHeight() * 2.2f < (float) skyH * 0.7f)
        buildSky (area);   // made again only when the screen's size has changed a lot
    buildComposites (area, s);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    const float mix = s.mix(), dim = s.duck * mix;
    g.setColour (juce::Colour (0xff05060a).withAlpha (mix));
    g.fillRect (area);

    drawWrapped (g, backComposite, area, drift[0], dim);
    drawWrapped (g, nebulaComposite, area, drift[1], s.reverb * (0.35f + 0.65f * s.wet) * dim);
    drawWrapped (g, nearComposite, area, drift[2], dim);

    // The bright stars: a soft halo, thin four-point spikes, twinkling more with TONE bright.
    const float yOffset = (float) area.getY() - ((float) skyH * scale - (float) area.getHeight()) * 0.5f;
    for (const auto& star : bright)
    {
        const float x = (float) area.getX() + std::fmod (star.x - drift[2] + (float) skyW, (float) skyW) * scale;
        const float y = yOffset + star.y * scale;
        if (x < (float) area.getX() - 30.0f || x > (float) area.getRight() + 30.0f || y < (float) area.getY() - 30.0f || y > (float) area.getBottom() + 30.0f)
            continue;
        const float twinkle = 1.0f - (0.1f + 0.4f * s.tone) * 0.5f * (1.0f + std::sin (star.phase + now * star.rate));
        const float a = star.size * twinkle * dim;
        const float r = (3.0f + 9.0f * star.size) * std::sqrt (scale * 2.2f);
        juce::ColourGradient halo (star.colour.withAlpha (0.55f * a), x, y, star.colour.withAlpha (0.0f), x + r, y, true);
        halo.addColour (0.25, star.colour.withAlpha (0.12f * a));
        g.setGradientFill (halo);
        g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
        if (star.spikes)
        {
            const float len = r * (1.4f + 1.2f * star.size);
            g.setColour (star.colour.withAlpha (0.5f * a));
            g.drawLine (x - len, y, x + len, y, 0.7f);
            g.drawLine (x, y - len, x, y + len, 0.7f);
        }
        g.setColour (juce::Colours::white.withAlpha (juce::jmin (1.0f, a * 1.2f)));
        const float core = 0.8f + 1.2f * star.size;
        g.fillEllipse (x - core, y - core, 2.0f * core, 2.0f * core);
    }

    // DELAY: ghosts of the waveform made of star-dust, further back and fainter
    // each repeat, each flashing as it fires after the latest note.
    if (s.delay > 0.01f && ! envelope.empty())
    {
        const float timeShare = juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (0.01f, s.echoSeconds) / 0.01f) / std::log (200.0f));
        const float k = 0.88f - 0.32f * timeShare;   // longer TIME: further apart
        struct Echo { int n; float level; };
        std::vector<Echo> echoes;
        float level = 1.0f;
        for (int n = 1; n <= 12 && level > 0.04f; ++n)
        {
            echoes.push_back ({ n, level });
            level *= s.feedback;
            if (s.feedback < 0.02f)
                break;
        }
        const float w = (float) wave.getWidth(), h = (float) wave.getHeight() * 0.48f;
        const auto centre = wave.getCentre().toFloat();
        const float below = (float) area.getBottom() - centre.y;   // room to sink into
        for (auto it = echoes.rbegin(); it != echoes.rend(); ++it)
        {
            const float sc = std::pow (k, (float) it->n);
            const float since = s.sinceNote - (float) it->n * s.echoSeconds;
            const float flash = since >= 0.0f ? std::exp (-since / 0.4f) : 0.0f;
            const float side = s.pingPong ? (it->n % 2 ? -1.0f : 1.0f) : 0.0f;
            // Down and away, into the grains' part of the sky.
            const float ox = side * (1.0f - sc) * w * 0.42f, oy = (1.0f - sc) * below * (s.pingPong ? 0.8f : 0.95f);
            const float a = juce::jmin (1.0f, s.delay * it->level * (0.6f + 0.4f * flash) * dim);
            if (a < 0.01f)
                continue;
            g.setColour ((s.pingPong ? (it->n % 2 ? juce::Colour (0xffc8d6ff) : juce::Colour (0xffffceac)) : juce::Colour (0xffffe2c4)).withAlpha (a));
            const float ew = w * sc, eh = h * sc, x0 = centre.x + ox - ew * 0.5f, y0 = centre.y + oy;
            juce::Random dust (it->n * 977);
            const int dots = (int) (520.0f * sc + 100.0f);
            for (int d = 0; d < dots; ++d)
            {
                const float u = dust.nextFloat();
                const float height = envelope[(size_t) juce::jlimit (0, (int) envelope.size() - 1, (int) (u * (float) envelope.size()))] * eh;
                const float v = dust.nextFloat() < 0.55f ? (dust.nextBool() ? -1.0f : 1.0f) * (0.85f + 0.15f * dust.nextFloat()) : dust.nextFloat() * 2.0f - 1.0f;
                const float sz = dust.nextFloat() < 0.1f ? 2.0f : 1.3f;
                g.fillRect (x0 + u * ew, y0 + v * height, sz, sz);
            }
        }
    }
    if (vignette.isValid())
    {
        g.setOpacity (mix);
        g.drawImageAt (vignette, area.getX(), area.getY());
        g.setOpacity (1.0f);
    }
}

void SpaceScene::drawOver (juce::Graphics& g, juce::Rectangle<int> area, juce::Rectangle<int> wave, const Settings& s)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    const float dim = s.duck * s.mix();
    const float w = (float) wave.getWidth(), half = (float) wave.getHeight() * 0.48f, mid = (float) wave.getCentreY();
    // SHIMMER: tiny four-point sparkles, twinkling as they rise.
    for (const auto& p : sparkles)
    {
        const float life = p.life / p.span, twinkle = std::sin (juce::MathConstants<float>::pi * life);
        const float x = (float) wave.getX() + p.u * w, y = mid + p.y * half;
        const float a = twinkle * (0.45f + 0.55f * s.shimmer) * dim;
        const float len = 1.2f + 3.5f * s.shimmer * twinkle;
        g.setColour (juce::Colour (0xffe1ebff).withAlpha (a * 0.8f));
        g.drawLine (x - len, y, x + len, y, 0.6f);
        g.drawLine (x, y - len, x, y + len, 0.6f);
        g.setColour (juce::Colours::white.withAlpha (a));
        g.fillRect (x - 0.6f, y - 0.6f, 1.2f, 1.2f);
    }
    if (s.frozen)
    {
        g.setColour (juce::Colour (0xff96b4dc).withAlpha (0.06f * s.mix()));
        g.fillRect (area);
    }
}
