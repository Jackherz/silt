// Drawing helpers the screen's pictures share: JUCE's colour arithmetic and Random, so the
// pictures come out as the plug-in draws them, and a few of juce::Graphics' calls on a canvas.

// --- colours (0xRRGGBB) ---
export const hex = (rgb) => `#${(rgb & 0xffffff).toString(16).padStart(6, '0')}`;
export const rgba = (rgb, a) => `rgba(${(rgb >> 16) & 255},${(rgb >> 8) & 255},${rgb & 255},${Math.max(0, Math.min(1, a))})`;

// Colour::interpolatedWith: PixelARGB's tween by round(p × 255) / 256.
export function interpolate(a, b, p) {
  if (p <= 0) return a;
  if (p >= 1) return b;
  const amount = Math.round(p * 255);
  const ch = (s) => { const x = (a >> s) & 255, y = (b >> s) & 255; return (x + (((y - x) * amount) >> 8)) & 255; };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Colour::brighter.
export function brighter(c, amount) {
  const k = 1 / (1 + amount);
  const ch = (s) => Math.trunc(255 - k * (255 - ((c >> s) & 255)));
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// --- juce::Random: the 48-bit generator, in two 24-bit halves so plain numbers stay exact ---
const M_LO = 0xece66d, M_HI = 0x5de, TWO24 = 16777216;
export class JuceRandom {
  constructor(seed) { this.setSeed(seed); }
  setSeed(seed) {
    // As JUCE keeps it: the raw 64-bit seed, masked to 48 bits by the first step.
    const s = BigInt.asUintN(64, BigInt(Math.trunc(seed)));
    this.hi = Number((s >> 24n) & 0xffffffn);
    this.lo = Number(s & 0xffffffn);
  }
  nextInt(max) {
    const full = this.lo * M_LO + 11;
    const carry = Math.floor(full / TWO24);
    const lo = full - carry * TWO24;
    const hi = (this.hi * M_LO + this.lo * M_HI + carry) % TWO24;
    this.lo = lo; this.hi = hi;
    const value = (hi * 256 + Math.floor(lo / 65536)) | 0;   // (int) (seed >> 16)
    if (max === undefined) return value;
    return Math.floor(((value >>> 0) * max) / 4294967296);
  }
  // In floats, as JUCE divides: (float) (uint32) nextInt() / 2^32.
  nextFloat() { return Math.min(Math.fround(Math.fround(this.nextInt() >>> 0) / 4294967296), 1 - 5.960464477539063e-8); }
}

// --- the canvas, as juce::Graphics ---
export function fillRoundRect(g, x, y, w, h, r) {
  r = Math.min(r, w / 2, h / 2);
  g.beginPath();
  g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r);
  g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath();
  g.fill();
}
export const vline = (g, x, top, bottom) => g.fillRect(Math.trunc(x), top, 1, bottom - top);
export const hline = (g, y, left, right) => g.fillRect(left, Math.trunc(y), right - left, 1);
export function line(g, x1, y1, x2, y2, width) {
  g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2);
  g.lineWidth = width; g.lineCap = 'butt'; g.stroke();
}
export function ellipse(g, x, y, w, h) { g.beginPath(); g.ellipse(x + w / 2, y + h / 2, Math.max(0, w / 2), Math.max(0, h / 2), 0, 0, Math.PI * 2); g.fill(); }

// Text in a box, as drawText's justifications: 'left', 'right' or 'center', centred down.
export function text(g, str, [x, y, w, h], align = 'left') {
  g.textAlign = align;
  g.textBaseline = 'middle';
  g.fillText(str, align === 'left' ? x : align === 'right' ? x + w : x + w / 2, y + h / 2 + 0.5);
}

// drawFittedText on one line: squeezed across down to minScale, then cut with "...".
export function fittedText(g, str, [x, y, w, h], minScale = 0.75) {
  let width = g.measureText(str).width;
  if (width <= w) { text(g, str, [x, y, w, h]); return; }
  let squeeze = Math.max(minScale, w / width);
  if (width * squeeze > w) {
    let cut = str;
    while (cut.length > 1 && g.measureText(`${cut}...`).width * minScale > w) cut = cut.slice(0, -1);
    str = `${cut.trimEnd()}...`;
    width = g.measureText(str).width;
    squeeze = Math.max(minScale, Math.min(1, w / width));
  }
  g.save();
  g.translate(x, 0);
  g.scale(squeeze, 1);
  text(g, str, [0, y, w / squeeze, h]);
  g.restore();
}

// A path from points.
export function path(points, closed = false) {
  const p = new Path2D();
  points.forEach(([x, y], i) => (i === 0 ? p.moveTo(x, y) : p.lineTo(x, y)));
  if (closed) p.closePath();
  return p;
}

// A line that glows: a wide soft stroke under a crisp one (ScreenInsets' glowStroke).
export function glowStroke(g, p, colour, alpha, width) {
  g.lineJoin = 'round'; g.lineCap = 'round';
  g.strokeStyle = rgba(colour, alpha * 0.12); g.lineWidth = width * 5; g.stroke(p);
  g.strokeStyle = rgba(colour, alpha * 0.25); g.lineWidth = width * 2.5; g.stroke(p);
  g.strokeStyle = rgba(colour, alpha); g.lineWidth = width; g.stroke(p);
}

export function glowRect(g, [x, y, w, h], colour) {
  g.fillStyle = rgba(colour, 0.18);
  g.fillRect(x - 2.5, y, w + 5, h);
  g.fillStyle = hex(colour);
  g.fillRect(x, y, w, h);
}

// --- the spectrum: a radix-2 FFT ---
export function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const angle = -2 * Math.PI / len, wr = Math.cos(angle), wi = Math.sin(angle);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}
