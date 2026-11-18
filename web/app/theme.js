// Graphite, the plug-in's default theme (plugin/Palette.h, kit::graphite in
// kit/include/kit/Look.h), and its page and modulator colours.

const hex = (argb) => `#${(argb & 0xffffff).toString(16).padStart(6, '0')}`;
export const rgba = (argb, alpha) => {
  const c = argb & 0xffffff;
  return `rgba(${c >> 16},${(c >> 8) & 255},${c & 255},${alpha})`;
};

// Graphite's roles (Palette.h's theme row), as 0xRRGGBB.
export const C = {
  chassis: 0x262a27, ink: 0xe9e2d0, inkSoft: 0xa9a291, bone: 0x3a3f3b, frame: 0x151716, onFrame: 0xe9e2d0,
  rust: 0xe86a2c, lcdGreen: 0x141615, lcdText: 0xe9e2d0, screenBlue: 0x1c2729, screenLight: 0x4c6468, screenPale: 0xb8ccc6,
  screenLime: 0xe9c46a, playhead: 0xe86a2c, insetBack: 0x152022,
  knobFace: 0xefe8d6, knobCollar: 0x151716, knobTick: 0xe9e2d0, knobDot: 0x232624,
};

// The kit's sheet on Graphite's paper (applyTheme): page, panel, ink, rule, sunk…
// JUCE's Colour::darker: each channel × 1/(1 + amount), truncated.
const darker = (rgb, amount) => {
  const k = 1 / (1 + amount);
  const ch = (shift) => Math.trunc(((rgb >> shift) & 255) * k);
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
};
export const S = {
  page: C.chassis,
  panel: darker(C.chassis, 0.14),
  head: darker(C.chassis, 0.3),
  ink: C.ink,
  soft: C.inkSoft,
  sunk: C.frame,
  onSunk: C.onFrame,
  shadow: darker(C.chassis, 0.75),
  accent: C.rust,
  lit: C.rust,
};

// Page colours (PageColours.h): GRAIN SHAPE PITCH PLAY RES FILTER COLOR SPACE; MOD and CHAIN use ink.
const pages = [0xdb94a0, 0xe39a70, 0xdcc98a, 0xa9c27a, 0x52c2b0, 0x6fb3dc, 0x93a3e6, 0xc497cf];
export const pageColour = (page) => (page >= 0 && page < pages.length ? pages[page] : C.ink);

// The modulators (ModColours.h): base, dim, ink, name.
export const MOD = [
  { base: 0xa64826, dim: 0x5c2714, ink: 0xefe6cf },
  { base: 0xd29a2c, dim: 0x7a5513, ink: 0x2a2420 },
  { base: 0x6f8f4e, dim: 0x3a4d28, ink: 0xefe6cf },
  { base: 0x4f62a8, dim: 0x2a3560, ink: 0xefe6cf },
];

export const css = (rgb) => hex(rgb);

// Type. JUCE sizes a font by ascent + descent; CSS by the em. Each face's ratio
// (from its hhea table) turns one into the other.
const ratio = { screen: 1.215, name: 1.266, value: 1.19, wordmark: 1.0 };
export const font = {
  // kit::label: B612 Mono, the screen face.
  screen: (h) => ({ fontFamily: 'B612 Mono', fontSize: h / ratio.screen }),
  // panelFont: Overpass Medium at 1.08× the size, tracked.
  name: (size, tracking = 0.12) => ({ fontFamily: 'Overpass', fontWeight: 500, fontSize: size * 1.08 / ratio.name, letterSpacing: tracking * 0.5 * size * 1.08 }),
  // valueFont: Chivo Mono at 1.2× + 2, less the 2 the plug-in takes off for its height.
  value: (size) => ({ fontFamily: 'Chivo Mono', fontSize: (size * 1.2 + 2 - 2) / ratio.value }),
  // monoFont: the screen face at 1.1×.
  mono: (size) => ({ fontFamily: 'B612 Mono', fontSize: size * 1.1 / ratio.screen }),
  wordmark: (h) => ({ fontFamily: 'Glyph Grotesque', fontSize: h / ratio.wordmark }),
};
