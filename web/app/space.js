// SPACE's picture (plugin/SpaceScene.cpp): the sample's waveform adrift in a night sky filling the
// whole screen, each control part of the scene. DECAY fades from a close space to a vast one,
// SIZE brings in the Milky Way, REVERB the nebula (TONE warm to cool), SHIMMER sparkles off the
// waveform, DELAY light echoes round each note, FREEZE a hush, DUCK dims it while a note sounds.
// The sky is made once per screen size, twice as wide so it drifts round without a seam.
import { JuceRandom, rgba } from './draw.js';

// Value noise repeating every `period` cells across.
function hash(x, y, salt) {
  let h = (Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(salt, 1442695041)) >>> 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
  return ((h ^ (h >>> 16)) & 0xffffff) / 0xffffff;
}
const smooth = (t) => t * t * (3 - 2 * t);
function noise(x, y, period, salt) {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = Math.fround(smooth(x - xi)), yf = Math.fround(smooth(y - yi));
  const wrap = (v) => ((v % period) + period) % period;
  const a = hash(wrap(xi), yi, salt), b = hash(wrap(xi + 1), yi, salt);
  const c = hash(wrap(xi), yi + 1, salt), d = hash(wrap(xi + 1), yi + 1, salt);
  return a + (b - a) * xf + (c - a) * yf + (a - b - c + d) * xf * yf;
}
function fbm(x, y, width, cells, octaves, salt) {
  let v = 0, amp = 0.5;
  for (let o = 0; o < octaves; o++) {
    const n = cells << o;
    v += amp * noise(x / width * n, y / width * n, n, o + salt * 16);
    amp *= 0.5;
  }
  return v;
}

// Star colours, orange through white to blue-white.
const STAR_COLOURS = [0xffb078, 0xffd6aa, 0xfff0e1, 0xecf0ff, 0xc4d6ff, 0xa8c4ff];
function starColour(r) {
  const u = r.nextFloat();
  return STAR_COLOURS[u < 0.12 ? 0 : u < 0.32 ? 1 : u < 0.62 ? 2 : u < 0.82 ? 3 : u < 0.94 ? 4 : 5];
}
const canvas = (w, h) => { const c = document.createElement('canvas'); c.width = Math.max(1, w); c.height = Math.max(1, h); return c; };

// The scene's settings from SPACE's controls (SiltEditor::updateSpaceSettings), before what it's
// doing now (wet, sinceNote, notePlace, duck), which the screen adds as it draws.
export function spaceSettings(v, tempo, quarters) {
  const s = {};
  s.decay = Math.max(0, Math.min(1, Math.log(Math.max(0.3, v('spaceDecay')) / 0.3) / Math.log(100)));
  s.size = v('spaceSize') * 0.01;
  s.reverb = v('spaceReverb') * 0.01;
  s.tone = v('spaceTone') * 0.01;
  s.shimmer = v('spaceShimmer') * 0.01;
  s.delay = v('spaceDelay') * 0.01;
  s.feedback = v('spaceFeedback') * 0.01;
  s.echoSeconds = v('spaceSync') > 0.5 ? quarters(Math.round(v('spaceDiv'))) * 60 / Math.max(20, Math.min(400, tempo)) : v('spaceTime') * 0.001;
  s.pingPong = Math.round(v('spaceMode')) === 0;
  s.frozen = v('spaceFreeze') > 0.5;
  // The reverb's controls only show while there's reverb: they come in over the first 20%.
  const reverbIn = Math.max(0, Math.min(1, s.reverb * 5));
  s.decay = 0.5 + (s.decay - 0.5) * reverbIn;
  s.size = 0.5 + (s.size - 0.5) * reverbIn;
  s.tone = 0.5 + (s.tone - 0.5) * reverbIn;
  s.shimmer *= reverbIn;
  s.frozen = s.frozen && s.reverb > 0;
  s.duckAmount = [0, 0.35, 0.65, 1][Math.max(0, Math.min(3, Math.round(v('spaceDuck'))))];
  return s;
}
export const sceneMix = (s) => Math.min(1, 1.25 * Math.max(s.delay, s.reverb));
export const sceneVisible = (s) => sceneMix(s) > 0.001;

export class SpaceScene {
  constructor() {
    this.skyW = 0; this.skyH = 0; this.builtFor = null;
    this.drift = [0, 0];
    this.now = 0; this.lastSinceNote = 99;
    this.sparkles = []; this.flares = [];
    this.random = new JuceRandom(11);
  }

  // A bright star as a camera sees it: a hot core, a soft glow, and (the brightest) thin spikes.
  static brightStar(g, cx, cy, size, colour, alpha, spikes) {
    if (alpha <= 0.01) return;
    const glow = 2.5 + 7 * size;
    const halo = g.createRadialGradient(cx, cy, 0, cx, cy, glow);
    halo.addColorStop(0, rgba(colour, 0.45 * alpha));
    halo.addColorStop(0.12, rgba(colour, 0.22 * alpha));
    halo.addColorStop(0.35, rgba(colour, 0.05 * alpha));
    halo.addColorStop(1, rgba(colour, 0));
    g.fillStyle = halo;
    g.beginPath(); g.arc(cx, cy, glow, 0, Math.PI * 2); g.fill();
    if (spikes) {
      const len = glow * (1.6 + 1.4 * size), w = 0.55 + 0.5 * size;
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const sx = -dy * w, sy = dx * w;
        const grad = g.createLinearGradient(cx, cy, cx + dx * len, cy + dy * len);
        grad.addColorStop(0, rgba(colour, 0.7 * alpha));
        grad.addColorStop(1, rgba(colour, 0));
        g.fillStyle = grad;
        g.beginPath(); g.moveTo(cx + sx, cy + sy); g.lineTo(cx + dx * len, cy + dy * len); g.lineTo(cx - sx, cy - sy); g.closePath(); g.fill();
      }
    }
    const core = 0.7 + size;
    g.fillStyle = `rgba(255,255,255,${Math.min(1, alpha)})`;
    g.beginPath(); g.arc(cx, cy, core, 0, Math.PI * 2); g.fill();
  }

  build([, , aw, ah]) {
    this.builtFor = [aw, ah];
    const skyW = this.skyW = aw * 2, skyH = this.skyH = ah;
    const qw = Math.max(1, Math.trunc(skyW / 4)), qh = Math.max(1, Math.trunc(skyH / 4));
    const r = new JuceRandom(7);

    // The clouds, worked out a quarter size, then drawn up: a curving Milky Way band with dust
    // lanes and a warmer core, and nebula wisps off it.
    const bandQ = new ImageData(qw, qh), wispQ = new ImageData(qw, qh);
    const density = new Float32Array(qw * qh);
    const w = skyW;
    for (let y = 0; y < qh; y++)
      for (let x = 0; x < qw; x++) {
        const X = x * 4, Y = y * 4;
        const across = ((Y / skyH - 0.5) - 0.2 * Math.sin(6.2831853 * X / w) - 0.07 * Math.sin(12.566 * X / w + 1.3)) * 2.2;
        const core = Math.exp(-across * across * 3.2);
        const mottle = fbm(X, Y, w, 12, 5, 1);
        const dust = Math.max(0, fbm(X, Y, w, 18, 5, 2) - 0.47) * 3.2 * core;
        const glow = Math.max(0, core * (0.35 + 0.9 * mottle) - dust * 0.9);
        density[y * qw + x] = Math.min(1, core * (0.4 + mottle));
        const warm = Math.exp(-across * across * 9);
        const i = (y * qw + x) * 4;
        bandQ.data[i] = 200 + 40 * warm; bandQ.data[i + 1] = 186 + 10 * warm; bandQ.data[i + 2] = 176 - 50 * warm;
        bandQ.data[i + 3] = Math.min(1, glow * 0.47) * 255;
        const wisp = Math.max(0, fbm(X, Y, w, 8, 6, 3) - 0.52) * 4.2 * (0.5 + fbm(X, Y, w, 36, 3, 4));
        wispQ.data[i] = 255; wispQ.data[i + 1] = 255; wispQ.data[i + 2] = 255;
        wispQ.data[i + 3] = Math.min(1, wisp * 0.59) * 255;
      }
    const fullSize = (small, tint) => {
      const s = canvas(qw, qh); s.getContext('2d').putImageData(small, 0, 0);
      const out = canvas(skyW, skyH), g = out.getContext('2d');
      g.imageSmoothingQuality = 'high';
      g.drawImage(s, 0, 0, skyW, skyH);
      if (tint !== undefined) { g.globalCompositeOperation = 'source-in'; g.fillStyle = rgba(tint, 1); g.fillRect(0, 0, skyW, skyH); }
      return out;
    };
    this.band = fullSize(bandQ);
    this.nebulaWarm = fullSize(wispQ, 0xaa6040);
    this.nebulaCool = fullSize(wispQ, 0x5c70be);

    // Stars, thicker along the band and mostly faint: the vast sky's many, the close sky's few.
    const perPixel = (skyW * skyH) / (2600 * 980);
    const scatter = (g, count, minSize, maxSize, brightness, bandBias) => {
      for (let k = 0; k < count; k++) {
        let x = r.nextFloat() * skyW, y = r.nextFloat() * skyH;
        const q = Math.max(0, Math.min(qh - 1, Math.trunc(y / 4))) * qw + Math.max(0, Math.min(qw - 1, Math.trunc(x / 4)));
        if (r.nextFloat() > 0.25 + bandBias * density[q]) { x = r.nextFloat() * skyW; y = r.nextFloat() * skyH; }
        const m = Math.pow(r.nextFloat(), 3.2);
        const sz = minSize + maxSize * m;
        g.fillStyle = rgba(starColour(r), Math.min(1, brightness * (0.25 + 0.75 * m)));
        if (sz < 1.1) g.fillRect(x, y, sz, sz);
        else { g.beginPath(); g.ellipse(x, y, sz * 0.5, sz * 0.5, 0, 0, Math.PI * 2); g.fill(); }
      }
    };
    this.vastStars = canvas(skyW, skyH);
    const vg = this.vastStars.getContext('2d', { willReadFrequently: true });
    scatter(vg, Math.round(16000 * perPixel), 0.3, 0.7, 0.75, 1.6);
    scatter(vg, Math.round(4000 * perPixel), 0.35, 1.1, 0.9, 1.0);
    {
      // Film grain in the vast sky's gaps.
      const px = vg.getImageData(0, 0, skyW, skyH), d = px.data;
      for (let y = 0; y < skyH; y += 2)
        for (let x = 0; x < skyW; x += 2)
          if (r.nextFloat() < 0.5 && d[(y * skyW + x) * 4 + 3] === 0) {
            const i = (y * skyW + x) * 4;
            d[i] = d[i + 1] = d[i + 2] = 255; d[i + 3] = Math.round((0.02 + 0.04 * r.nextFloat()) * 255);
          }
      vg.putImageData(px, 0, 0);
    }
    this.closeStars = canvas(skyW, skyH);
    const cg = this.closeStars.getContext('2d');
    scatter(cg, Math.round(1500 * perPixel), 0.5, 1.6, 0.95, 0.3);
    scatter(cg, Math.round(500 * perPixel), 0.8, 2.4, 1.0, 0.2);

    // The few bright ones, drawn live so they twinkle.
    this.bright = [];
    const brightCount = Math.max(8, Math.round(60 * perPixel));
    for (let k = 0; k < brightCount; k++) {
      const x = r.nextFloat() * skyW, y = r.nextFloat() * skyH, size = 0.3 + Math.pow(r.nextFloat(), 2) * 0.7;
      const colour = starColour(r), phase = r.nextFloat() * 6.28, rate = 0.4 + r.nextFloat() * 1.4, spikes = r.nextFloat() < 0.4;
      this.bright.push({ x, y, size, colour, phase, rate, spikes });
    }

    // Darker corners, fixed to the screen.
    this.vignette = canvas(aw, ah);
    const g = this.vignette.getContext('2d');
    const grad = g.createRadialGradient(aw / 2, ah / 2, 0, aw / 2, ah / 2, aw * 0.6);
    grad.addColorStop(0, 'rgba(0,0,0,0)'); grad.addColorStop(1, 'rgba(0,0,0,0.5)');
    g.fillStyle = grad; g.fillRect(0, 0, aw, ah);
  }

  drawWrapped(g, img, [ax, ay, aw], offset, alpha) {
    if (alpha <= 0.005 || !img) return;
    const w = img.width;
    let x = ax - Math.trunc(offset % w);
    if (x > ax) x -= w;
    g.globalAlpha = Math.min(1, alpha);
    for (; x < ax + aw; x += w) g.drawImage(img, x, ay);
    g.globalAlpha = 1;
  }

  // Moves it on: the drift, the sparkles, the light echoes (nothing moves while frozen).
  advance(s, seconds, envelope) {
    if (s.sinceNote + 0.05 < this.lastSinceNote && s.delay > 0.01) {
      this.flares.push({ u: Math.max(0.08, Math.min(0.92, s.notePlace + (this.random.nextFloat() - 0.5) * 0.1)),
                         v: 0.3 + this.random.nextFloat() * 0.4, born: this.now, seed: this.random.nextInt(100000) });
      if (this.flares.length > 3) this.flares.shift();
    }
    this.lastSinceNote = s.sinceNote;
    if (s.frozen) return;
    this.now += seconds;
    const speed = 8 * (1.4 - s.size), skyW = Math.max(1, this.skyW);
    this.drift[0] = (this.drift[0] + seconds * speed * 0.35) % skyW;
    this.drift[1] = (this.drift[1] + seconds * speed) % skyW;
    const lifetime = s.echoSeconds * 12 + 8;
    this.flares = this.flares.filter((f) => this.now - f.born <= lifetime);
    if (s.shimmer > 0.01 && envelope?.length) {
      for (let n = s.shimmer * 70 * (0.15 + s.wet) * seconds; n > 0; n -= 1)
        if (this.random.nextFloat() < n) {
          const u = this.random.nextFloat();
          const height = envelope[Math.max(0, Math.min(envelope.length - 1, Math.trunc(u * envelope.length)))];
          this.sparkles.push({ u, y: -height, vy: -(0.15 + 0.5 * s.shimmer) * (0.6 + this.random.nextFloat() * 0.8), vx: (this.random.nextFloat() - 0.5) * 0.04,
                               life: 0, span: 1.2 + this.random.nextFloat() * 1.6 * (0.5 + s.shimmer) });
        }
    }
    for (const p of this.sparkles) { p.life += seconds; p.y += p.vy * seconds; p.u += p.vx * seconds; }
    this.sparkles = this.sparkles.filter((p) => p.life < p.span);
    if (this.sparkles.length > 400) this.sparkles.splice(0, this.sparkles.length - 400);
  }

  // Behind the waveform and grains: sky, nebula, light echoes.
  drawBehind(g, area, s) {
    const [ax, ay, aw, ah] = area;
    if (aw <= 0 || ah <= 0) return;
    if (!this.band || Math.abs(aw - this.builtFor[0]) > 24 || Math.abs(ah - this.builtFor[1]) > 2) this.build(area);
    g.save();
    g.beginPath(); g.rect(ax, ay, aw, ah); g.clip();
    const mix = sceneMix(s), dim = s.duck * mix;
    g.fillStyle = `rgba(5,6,10,${mix})`;
    g.fillRect(ax, ay, aw, ah);
    const vast = s.decay;
    this.drawWrapped(g, this.band, area, this.drift[0], (0.15 + 0.85 * s.size) * (0.3 + 0.7 * vast) * (0.6 + 0.4 * s.tone) * dim);
    this.drawWrapped(g, this.vastStars, area, this.drift[0], (0.2 + 0.8 * vast) * (0.6 + 0.4 * s.size) * dim);
    this.drawWrapped(g, this.closeStars, area, this.drift[1], (1 - 0.75 * vast) * dim);
    const neb = s.reverb * (0.35 + 0.65 * s.wet) * dim * 1.3;
    this.drawWrapped(g, this.nebulaWarm, area, this.drift[0], neb * (1 - s.tone));
    this.drawWrapped(g, this.nebulaCool, area, this.drift[0], neb * s.tone);

    for (const star of this.bright) {
      const x = ax + ((star.x - this.drift[1] + this.skyW) % this.skyW), y = ay + star.y;
      if (x < ax - 40 || x > ax + aw + 40) continue;
      const twinkle = 1 - (0.1 + 0.4 * s.tone) * 0.5 * (1 + Math.sin(star.phase + this.now * star.rate));
      SpaceScene.brightStar(g, x, y, star.size * (1.15 - 0.45 * vast), star.colour, star.size * twinkle * dim, star.spikes);
    }

    // DELAY: light echoes. Each note flares; every echo sends a ring of lit dust out from it.
    if (s.delay > 0.01) {
      const speed = 90 * ah / 300, reach = aw * 0.42;
      const levels = [];
      for (let level = 1; level > 0.04 && levels.length < 12; level *= s.feedback) { levels.push(level); if (s.feedback < 0.02) break; }
      for (const f of this.flares) {
        const age = this.now - f.born;
        const cx = ax + f.u * aw, cy = ay + f.v * ah;
        const apart = 14 * ah / 300;
        const source = (n) => (s.pingPong ? [cx + (n % 2 ? apart : -apart), cy] : [cx, cy]);
        const lastRing = levels.length * s.echoSeconds;
        const fadeOut = Math.max(0, Math.min(1, 1 - (age - lastRing) / (reach / speed)));
        for (let star = 0; star < (s.pingPong ? 2 : 1); star++) {
          let flash = Math.exp(-age / 0.3);
          for (let i = 0; i < levels.length; i++) {
            const since = age - (i + 1) * s.echoSeconds;
            if (since >= 0 && (!s.pingPong || (i + 1) % 2 === star)) flash = Math.max(flash, levels[i] * Math.exp(-since / 0.25));
          }
          const [sx, sy] = source(star);
          SpaceScene.brightStar(g, sx, sy, 0.6, star ? 0xffd6aa : 0xd8e4ff, Math.min(1, s.delay * dim * fadeOut * (0.35 + 0.9 * flash)), true);
        }
        for (let i = 0; i < levels.length; i++) {
          const n = i + 1, ringAge = age - n * s.echoSeconds;
          if (ringAge <= 0) break;
          const radius = ringAge * speed;
          if (radius > reach) continue;
          const spread = 1 - radius / reach;
          const a = Math.min(1, s.delay * levels[i] * spread * (0.55 + 0.8 * Math.exp(-ringAge / 0.25)) * dim);
          if (a < 0.01) continue;
          const [rx, ry] = source(n);
          const colour = s.pingPong && n % 2 ? 0xffdcbc : 0xd6e2ff;
          g.strokeStyle = rgba(colour, 0.18 * a); g.lineWidth = 1;
          g.beginPath(); g.ellipse(rx, ry, radius, radius * 0.82, 0, 0, Math.PI * 2); g.stroke();
          const dust = new JuceRandom(f.seed + n * 131);
          const clump = dust.nextFloat() * 6.28;
          const count = Math.max(16, Math.min(240, Math.trunc(6.2831853 * radius / 3)));
          for (let k = 0; k < count; k++) {
            const angle = 6.2831853 * (k + dust.nextFloat()) / count;
            const thick = 0.5 + 0.5 * Math.sin(angle * 3 + clump) * Math.sin(angle * 5 + clump * 1.7);
            if (dust.nextFloat() > 0.25 + 0.75 * thick) continue;
            const rr = radius * (1 + 0.05 * (dust.nextFloat() - 0.5)) + (dust.nextFloat() - 0.5) * 3;
            g.fillStyle = rgba(colour, Math.min(1, a * (0.4 + 0.8 * thick)));
            const sz = dust.nextFloat() < 0.1 ? 1.7 : 1.1;
            g.fillRect(rx + Math.cos(angle) * rr, ry + Math.sin(angle) * rr * 0.82, sz, sz);
          }
        }
      }
    }
    g.globalAlpha = mix;
    g.drawImage(this.vignette, ax, ay);
    g.globalAlpha = 1;
    g.restore();
  }

  // Over the waveform and grains: SHIMMER's sparkles, the freeze's hush.
  drawOver(g, area, [wx, wy, ww, wh], s) {
    g.save();
    g.beginPath(); g.rect(...area); g.clip();
    const dim = s.duck * sceneMix(s), half = wh * 0.48, mid = wy + wh / 2;
    for (const p of this.sparkles) {
      const life = p.life / p.span, twinkle = Math.sin(Math.PI * life);
      const x = wx + p.u * ww, y = mid + p.y * half;
      const a = twinkle * (0.45 + 0.55 * s.shimmer) * dim, len = 1.2 + 3.5 * s.shimmer * twinkle;
      g.strokeStyle = rgba(0xe1ebff, a * 0.8); g.lineWidth = 0.6; g.lineCap = 'butt';
      g.beginPath(); g.moveTo(x - len, y); g.lineTo(x + len, y); g.moveTo(x, y - len); g.lineTo(x, y + len); g.stroke();
      g.fillStyle = `rgba(255,255,255,${Math.max(0, Math.min(1, a))})`;
      g.fillRect(x - 0.6, y - 0.6, 1.2, 1.2);
    }
    if (s.frozen) { g.fillStyle = rgba(0x96b4dc, 0.06 * sceneMix(s)); g.fillRect(...area); }
    g.restore();
  }
}
