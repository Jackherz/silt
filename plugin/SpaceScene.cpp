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

// A bright star as a camera sees it: a small hot core, a soft glow falling
// away fast, and (the brightest) thin spikes that taper to nothing.
void SpaceScene::drawBrightStar (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour, float alpha, bool spikes)
{
    if (alpha <= 0.01f)
        return;
    const float glow = 2.5f + 7.0f * size;
    juce::ColourGradient halo (colour.withAlpha (0.45f * alpha), c.x, c.y, colour.withAlpha (0.0f), c.x + glow, c.y, true);
    halo.addColour (0.12, colour.withAlpha (0.22f * alpha));
    halo.addColour (0.35, colour.withAlpha (0.05f * alpha));
    g.setGradientFill (halo);
    g.fillEllipse (c.x - glow, c.y - glow, 2.0f * glow, 2.0f * glow);
    if (spikes)
    {
        // Each spike a sliver, widest at the core, fading along its length.
        const float len = glow * (1.6f + 1.4f * size), w = 0.55f + 0.5f * size;
        const juce::Point<float> dirs[] { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
        for (const auto dir : dirs)
        {
            const auto side = juce::Point<float> (-dir.y, dir.x) * w;
            juce::Path sliver;
            sliver.startNewSubPath (c + side);
            sliver.lineTo (c + dir * len);
            sliver.lineTo (c - side);
            sliver.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.7f * alpha), c.x, c.y, colour.withAlpha (0.0f),
                                                     c.x + dir.x * len, c.y + dir.y * len, false));
            g.fillPath (sliver);
        }
    }
    const float core = 0.7f + 1.0f * size;
    g.setColour (juce::Colours::white.withAlpha (juce::jmin (1.0f, alpha)));
    g.fillEllipse (c.x - core, c.y - core, 2.0f * core, 2.0f * core);
}

void SpaceScene::buildSky (juce::Rectangle<int> area)
{
    builtFor = area;
    skyW = area.getWidth() * 2;
    skyH = area.getHeight();
    const int qw = juce::jmax (1, skyW / 4), qh = juce::jmax (1, skyH / 4);
    juce::Random r (7);

    // The clouds, worked out a quarter size, then drawn up to full size once: a
    // curving Milky Way band with dark dust lanes and a warmer core, and
    // nebula wisps off it.
    juce::Image bandQ (juce::Image::ARGB, qw, qh, true), wispQ (juce::Image::ARGB, qw, qh, true);
    std::vector<float> density ((size_t) (qw * qh));
    {
        juce::Image::BitmapData bandPixels (bandQ, juce::Image::BitmapData::writeOnly);
        juce::Image::BitmapData wispPixels (wispQ, juce::Image::BitmapData::writeOnly);
        const float w = (float) skyW;
        for (int y = 0; y < qh; ++y)
            for (int x = 0; x < qw; ++x)
            {
                const float X = (float) x * 4.0f, Y = (float) y * 4.0f;
                const float across = ((Y / (float) skyH - 0.5f) - 0.2f * std::sin (6.2831853f * X / w)
                                      - 0.07f * std::sin (12.566f * X / w + 1.3f)) * 2.2f;
                const float core = std::exp (-across * across * 3.2f);
                const float mottle = fbm (X, Y, w, 12, 5, 1);
                const float dust = juce::jmax (0.0f, fbm (X, Y, w, 18, 5, 2) - 0.47f) * 3.2f * core;
                const float glow = juce::jmax (0.0f, core * (0.35f + 0.9f * mottle) - dust * 0.9f);
                density[(size_t) (y * qw + x)] = juce::jmin (1.0f, core * (0.4f + mottle));
                const float warm = std::exp (-across * across * 9.0f);
                bandPixels.setPixelColour (x, y, juce::Colour::fromFloatRGBA ((200.0f + 40.0f * warm) / 255.0f, (186.0f + 10.0f * warm) / 255.0f,
                                                                              (176.0f - 50.0f * warm) / 255.0f, juce::jmin (1.0f, glow * 0.47f)));
                const float wisp = juce::jmax (0.0f, fbm (X, Y, w, 8, 6, 3) - 0.52f) * 4.2f * (0.5f + fbm (X, Y, w, 36, 3, 4));
                wispPixels.setPixelColour (x, y, juce::Colours::white.withAlpha (juce::jmin (1.0f, wisp * 0.59f)));
            }
    }
    const auto fullSize = [&] (const juce::Image& small, juce::Colour tint, bool tinted)
    {
        juce::Image out (juce::Image::ARGB, skyW, skyH, true);
        {
            juce::Graphics g (out);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (small, 0, 0, skyW, skyH, 0, 0, qw, qh);
        }
        if (tinted)
        {
            // Keep the wisps' shape, in this colour.
            juce::Image::BitmapData px (out, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < skyH; ++y)
                for (int x = 0; x < skyW; ++x)
                    px.setPixelColour (x, y, tint.withAlpha (px.getPixelColour (x, y).getFloatAlpha()));
        }
        return out;
    };
    band = fullSize (bandQ, {}, false);
    nebulaWarm = fullSize (wispQ, juce::Colour (0xffaa6040), true);
    nebulaCool = fullSize (wispQ, juce::Colour (0xff5c70be), true);

    // Stars, thicker along the band and mostly faint: the vast sky's many tiny
    // ones, and the close sky's fewer, larger ones.
    const float perPixel = (float) (skyW * skyH) / (2600.0f * 980.0f);
    const auto scatter = [&] (juce::Graphics& g, int count, float minSize, float maxSize, float brightness, float bandBias)
    {
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
            const float sz = minSize + maxSize * m;
            g.setColour (starColour (r).withAlpha (juce::jmin (1.0f, brightness * (0.25f + 0.75f * m))));
            if (sz < 1.1f)
                g.fillRect (x, y, sz, sz);
            else
                g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        }
    };
    vastStars = juce::Image (juce::Image::ARGB, skyW, skyH, true);
    {
        juce::Graphics g (vastStars);
        scatter (g, juce::roundToInt (16000.0f * perPixel), 0.3f, 0.7f, 0.75f, 1.6f);
        scatter (g, juce::roundToInt (4000.0f * perPixel), 0.35f, 1.1f, 0.9f, 1.0f);
    }
    {
        // Film grain in the vast sky's gaps.
        juce::Image::BitmapData px (vastStars, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < skyH; y += 2)
            for (int x = 0; x < skyW; x += 2)
                if (r.nextFloat() < 0.5f && px.getPixelColour (x, y).getAlpha() == 0)
                    px.setPixelColour (x, y, juce::Colours::white.withAlpha (0.02f + 0.04f * r.nextFloat()));
    }
    closeStars = juce::Image (juce::Image::ARGB, skyW, skyH, true);
    {
        juce::Graphics g (closeStars);
        scatter (g, juce::roundToInt (1500.0f * perPixel), 0.5f, 1.6f, 0.95f, 0.3f);
        scatter (g, juce::roundToInt (500.0f * perPixel), 0.8f, 2.4f, 1.0f, 0.2f);
    }

    // The few bright ones, drawn live so they twinkle.
    bright.clear();
    const int brightCount = juce::jmax (8, juce::roundToInt (60.0f * perPixel));
    for (int k = 0; k < brightCount; ++k)
        bright.push_back ({ r.nextFloat() * (float) skyW, r.nextFloat() * (float) skyH, 0.3f + std::pow (r.nextFloat(), 2.0f) * 0.7f,
                            starColour (r), r.nextFloat() * 6.28f, 0.4f + r.nextFloat() * 1.4f, r.nextFloat() < 0.4f });

    // Darker corners, fixed to the screen.
    vignette = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics vg (vignette);
    const auto c = vignette.getBounds().toFloat().getCentre();
    vg.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c.x, c.y, juce::Colours::black.withAlpha (0.5f),
                                              c.x + (float) area.getWidth() * 0.6f, c.y, true));
    vg.fillAll();
}

void SpaceScene::drawWrapped (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> area, float offset, float alpha) const
{
    if (alpha <= 0.005f || ! img.isValid())
        return;
    const int w = img.getWidth();
    int x = area.getX() - (int) std::fmod (offset, (float) w);
    if (x > area.getX())
        x -= w;
    g.setOpacity (juce::jmin (1.0f, alpha));
    for (; x < area.getRight(); x += w)
        g.drawImageAt (img, x, area.getY());
    g.setOpacity (1.0f);
}

void SpaceScene::advance (const Settings& s, float seconds, const std::vector<float>& envelope)
{
    // A new note (the time since one starts again): a flare there, near where it plays.
    if (s.sinceNote + 0.05f < lastSinceNote && s.delay > 0.01f)
    {
        flares.push_back ({ juce::jlimit (0.08f, 0.92f, s.notePlace + (random.nextFloat() - 0.5f) * 0.1f),
                            0.3f + random.nextFloat() * 0.4f, now, random.nextInt (100000) });
        if (flares.size() > 3)
            flares.erase (flares.begin());
    }
    lastSinceNote = s.sinceNote;
    if (s.frozen)
        return;   // time stops
    now += seconds;

    const float speed = 8.0f * (1.4f - s.size);
    drift[0] = std::fmod (drift[0] + seconds * speed * 0.35f, (float) juce::jmax (1, skyW));
    drift[1] = std::fmod (drift[1] + seconds * speed, (float) juce::jmax (1, skyW));

    // A flare goes once its last ring has spread and faded.
    const float lifetime = s.echoSeconds * 12.0f + 8.0f;
    flares.erase (std::remove_if (flares.begin(), flares.end(), [&] (const Flare& f) { return now - f.born > lifetime; }), flares.end());

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
    juce::ignoreUnused (wave, envelope);
    if (area.isEmpty())
        return;
    // Made again only when the screen's size changes by more than a little
    // (it's drawn wrapped and cropped either way).
    if (! band.isValid() || std::abs (area.getWidth() - builtFor.getWidth()) > 24 || std::abs (area.getHeight() - builtFor.getHeight()) > 2)
        buildSky (area);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    const float mix = s.mix(), dim = s.duck * mix;
    g.setColour (juce::Colour (0xff05060a).withAlpha (mix));
    g.fillRect (area);

    // DECAY fades from the close space to the vast one; SIZE brings the band in.
    const float vast = s.decay;
    drawWrapped (g, band, area, drift[0], (0.15f + 0.85f * s.size) * (0.3f + 0.7f * vast) * (0.6f + 0.4f * s.tone) * dim);
    drawWrapped (g, vastStars, area, drift[0], (0.2f + 0.8f * vast) * (0.6f + 0.4f * s.size) * dim);
    drawWrapped (g, closeStars, area, drift[1], (1.0f - 0.75f * vast) * dim);
    // REVERB: the nebula, breathing with the wet sound; TONE moves it from warm dust to cool blue.
    const float neb = s.reverb * (0.35f + 0.65f * s.wet) * dim * 1.3f;
    drawWrapped (g, nebulaWarm, area, drift[0], neb * (1.0f - s.tone));
    drawWrapped (g, nebulaCool, area, drift[0], neb * s.tone);

    // The bright stars: larger in the close space, twinkling more with TONE bright.
    for (const auto& star : bright)
    {
        const float x = (float) area.getX() + std::fmod (star.x - drift[1] + (float) skyW, (float) skyW);
        const float y = (float) area.getY() + star.y;
        if (x < (float) area.getX() - 40.0f || x > (float) area.getRight() + 40.0f)
            continue;
        const float twinkle = 1.0f - (0.1f + 0.4f * s.tone) * 0.5f * (1.0f + std::sin (star.phase + now * star.rate));
        drawBrightStar (g, { x, y }, star.size * (1.15f - 0.45f * vast), star.colour, star.size * twinkle * dim, star.spikes);
    }

    // DELAY: light echoes. Each note flares; every echo sends a ring of lit
    // dust out from it, so the rings (expanding at a steady speed) are spaced
    // by TIME; FEEDBACK says how many, each fainter; they fade as they spread.
    // Ping-pong: two stars side by side, the rings alternating between them.
    if (s.delay > 0.01f)
    {
        const float speed = 90.0f * (float) area.getHeight() / 300.0f;   // pixels a second
        const float reach = (float) area.getWidth() * 0.42f;
        std::vector<float> levels;
        for (float level = 1.0f; level > 0.04f && levels.size() < 12; level *= s.feedback)
        {
            levels.push_back (level);
            if (s.feedback < 0.02f)
                break;
        }
        for (const auto& f : flares)
        {
            const float age = now - f.born;
            const auto centre = juce::Point<float> ((float) area.getX() + f.u * (float) area.getWidth(), (float) area.getY() + f.v * (float) area.getHeight());
            const float apart = 14.0f * (float) area.getHeight() / 300.0f;
            const auto source = [&] (int n) { return s.pingPong ? centre + juce::Point<float> (n % 2 ? apart : -apart, 0.0f) : centre; };
            const float lastRing = (float) levels.size() * s.echoSeconds;
            const float fadeOut = juce::jlimit (0.0f, 1.0f, 1.0f - (age - lastRing) / (reach / speed));

            // The stars themselves: a flash on the note, and on each echo they send.
            for (int star = 0; star < (s.pingPong ? 2 : 1); ++star)
            {
                float flash = std::exp (-age / 0.3f);
                for (size_t i = 0; i < levels.size(); ++i)
                    if (const float since = age - (float) (i + 1) * s.echoSeconds; since >= 0.0f && (! s.pingPong || (int) (i + 1) % 2 == star))
                        flash = juce::jmax (flash, levels[i] * std::exp (-since / 0.25f));
                drawBrightStar (g, source (star), 0.6f, star ? juce::Colour (0xffffd6aa) : juce::Colour (0xffd8e4ff),
                                juce::jmin (1.0f, s.delay * dim * fadeOut * (0.35f + 0.9f * flash)), true);
            }

            for (size_t i = 0; i < levels.size(); ++i)
            {
                const int n = (int) i + 1;
                const float ringAge = age - (float) n * s.echoSeconds;
                if (ringAge <= 0.0f)
                    break;
                const float radius = ringAge * speed;
                if (radius > reach)
                    continue;
                const float spread = 1.0f - radius / reach;
                const float a = juce::jmin (1.0f, s.delay * levels[i] * spread * (0.55f + 0.8f * std::exp (-ringAge / 0.25f)) * dim);
                if (a < 0.01f)
                    continue;
                const auto c = source (n);
                const auto colour = s.pingPong && n % 2 ? juce::Colour (0xffffdcbc) : juce::Colour (0xffd6e2ff);
                // A faint sheet of light, then the dust lit along it, clumpy and fixed to the ring.
                g.setColour (colour.withAlpha (0.18f * a));
                g.drawEllipse (c.x - radius, c.y - radius * 0.82f, 2.0f * radius, 1.64f * radius, 1.0f);
                juce::Random dust (f.seed + n * 131);
                const float clump = dust.nextFloat() * 6.28f;
                const int count = juce::jlimit (16, 240, (int) (6.2831853f * radius / 3.0f));
                for (int k = 0; k < count; ++k)
                {
                    const float angle = 6.2831853f * ((float) k + dust.nextFloat()) / (float) count;
                    const float thick = 0.5f + 0.5f * std::sin (angle * 3.0f + clump) * std::sin (angle * 5.0f + clump * 1.7f);
                    if (dust.nextFloat() > 0.25f + 0.75f * thick)
                        continue;
                    const float rr = radius * (1.0f + 0.05f * (dust.nextFloat() - 0.5f)) + (dust.nextFloat() - 0.5f) * 3.0f;
                    g.setColour (colour.withAlpha (juce::jmin (1.0f, a * (0.4f + 0.8f * thick))));
                    const float sz = dust.nextFloat() < 0.1f ? 1.7f : 1.1f;
                    g.fillRect (c.x + std::cos (angle) * rr, c.y + std::sin (angle) * rr * 0.82f, sz, sz);
                }
            }
        }
    }

    g.setOpacity (mix);
    g.drawImageAt (vignette, area.getX(), area.getY());
    g.setOpacity (1.0f);
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
