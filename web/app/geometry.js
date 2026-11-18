// The panel's rectangles at the plug-in's default size, worked out exactly as
// SiltEditor::resized() (plugin/PluginEditor.cpp) does, so the browser's panel lands
// on the same pixels. The Bench's row holds the browser's source and audio keys.
// Rectangles are [x, y, w, h].

export const W = 1200, H = 820;
const stripHeight = 66, spineWidth = 20, gap = 10, trayHeight = 34, minWell = 170, minScreen = 280;
const layoutWell = 0.36;   // SiltProcessor::defaultLayoutWell
export const SPINE = spineWidth;

const jlimit = (lo, hi, v) => Math.max(lo, Math.min(hi, v));
const trunc = Math.trunc;

export function panelGeometry(instrument = true) {
  let r = [24, 24, W - 48, H - 48];
  const contentWidth = r[2];
  const top = (h) => { const out = [r[0], r[1], r[2], h]; r = [r[0], r[1] + h, r[2], r[3] - h]; return out; };
  const bottom = (h) => { const out = [r[0], r[1] + r[3] - h, r[2], h]; r = [r[0], r[1], r[2], r[3] - h]; return out; };

  const header = top(72);
  top(12);
  const bar = [header[0], header[1] + 6, header[2], 36];
  const tour = [bar[0] + bar[2] - 36, bar[1], 36, 36];
  const barRest = bar[2] - 44;
  const presetW = Math.min(400, trunc(barRest / 2));
  const presets = [bar[0] + barRest - presetW, bar[1], presetW, 36];

  const bench = bottom(50);   // the browser's source row
  bottom(12);

  // The strip: SOURCE (Silt FX: LEVELS), CHAIN, the readout.
  const strip = top(stripHeight);
  top(gap);
  const lcdW = jlimit(220, 400, trunc(contentWidth * 26 / 100));
  const lcd = [strip[0] + strip[2] - lcdW, strip[1], lcdW, stripHeight];
  let sx = strip[0];
  const sourcePanel = [sx, strip[1], 290, stripHeight];
  sx += 290 + gap;
  const chainPanel = [sx, strip[1], lcd[0] - gap - sx, stripHeight];
  const chainView = [chainPanel[0] + spineWidth + 6, chainPanel[1] + 4, chainPanel[2] - spineWidth - 12, stripHeight - 8];

  // SOURCE's keys.
  const inner = [sourcePanel[0] + spineWidth + 6, sourcePanel[1] + 4, sourcePanel[2] - spineWidth - 12, stripHeight - 8];
  const arm = [inner[0], inner[1], 84, 28];
  const bars = [inner[0] + 90, inner[1], inner[2] - 90, 28];
  const third = trunc((inner[2] - 12) / 3);
  const y2 = inner[1] + 30, h2 = inner[3] - 30;
  const resample = [inner[0], y2, third + 16, h2];
  const load = [resample[0] + resample[2] + 6, y2, third - 16, h2];
  const monitor = [load[0] + load[2] + 6, y2, inner[0] + inner[2] - (load[0] + load[2] + 6), h2];

  // The well, the key row and the screen share what's left.
  const flexibleHeight = r[3] - trayHeight - 2 * gap;
  const wellHeight = jlimit(minWell, Math.max(minWell, flexibleHeight - minScreen), Math.round(flexibleHeight * layoutWell));
  const well = bottom(wellHeight);
  bottom(gap);
  const keyRow = bottom(trayHeight);
  bottom(gap);
  const screen = r;

  // The everyday keys, from the right.
  let kr = keyRow[0] + keyRow[2];
  const fromRight = (w, h = trayHeight) => { kr -= w; return [kr, keyRow[1], w, h]; };
  const redo = fromRight(62); kr -= 4;
  const undo = fromRight(62); kr -= 12;
  const routes = fromRight(104); kr -= 4;
  const amount = fromRight(52, trayHeight - 2); kr -= 4;
  const random = fromRight(84); kr -= 4;
  const learn = fromRight(84); kr -= 12;
  const tray = [keyRow[0], keyRow[1], kr - keyRow[0], trayHeight];

  // The page keys: GRAIN SHAPE PITCH PLAY, then MOD after a gap; each the same width.
  const pageKeys = {};
  {
    const groupGap = 10, border = 1, keyed = [0, 1, 2, 3, 8], groups = 2;
    const count = keyed.length;
    const room = Math.min(count * 104, tray[2] - (groups - 1) * groupGap - 2 * groups * border);
    let offset = tray[0] + border, shown = 0;
    for (const t of keyed) {
      if (t === 8) offset += 2 * border + groupGap;
      const left = offset + trunc(room * shown / count), right = offset + trunc(room * (shown + 1) / count);
      pageKeys[t] = [left, tray[1] + border, right - left, trayHeight - 2 * border];
      ++shown;
    }
  }

  // The well: the mod rail, then eight columns.
  const railWidth = jlimit(150, 280, trunc(contentWidth / 6));
  const rail = [well[0] + 8, well[1] + 8, railWidth - 16, well[3] - 16];
  const controlRow = [well[0] + railWidth + 8, well[1] + 2, well[2] - railWidth - 8, well[3] - 4];
  const slotW = trunc(controlRow[2] / 8);
  const slots = Array.from({ length: 8 }, (_, i) => slotParts([controlRow[0] + i * slotW, controlRow[1], slotW, controlRow[3]]));

  return { W, H, phone: false, header, presets, tour, bench, strip, lcd, sourcePanel, chainPanel, chainView, arm, bars, resample, load, monitor,
           screen, keyRow, tray, pageKeys, redo, undo, routes, amount, random, learn, well, rail, controlRow, slots, instrument };
}

// A phone (the browser's own, after Brood's): the same parts stacked on a canvas 390 wide, as
// tall as they need, which the page scrolls. 12 in from each side, 366 across.
export const PW = 390;
export function phoneGeometry() {
  const x = 12, w = 366;
  let y = 12;
  const row = (h, after = 10) => { const r = [x, y, w, h]; y += h + after; return r; };
  const header = row(44, 8);
  const tour = [x + w - 36, header[1] + 4, 36, 36];
  const presets = row(36, 12);
  const lcd = row(stripHeight);
  const sourcePanel = row(stripHeight);
  const chainPanel = row(stripHeight);
  const chainView = [chainPanel[0] + spineWidth + 6, chainPanel[1] + 4, chainPanel[2] - spineWidth - 12, stripHeight - 8];
  const strip = [x, sourcePanel[1], w, chainPanel[1] + stripHeight - sourcePanel[1]];
  const inner = [sourcePanel[0] + spineWidth + 6, sourcePanel[1] + 4, sourcePanel[2] - spineWidth - 12, stripHeight - 8];
  const arm = [inner[0], inner[1], 84, 28];
  const bars = [inner[0] + 90, inner[1], inner[2] - 90, 28];
  const third = trunc((inner[2] - 12) / 3);
  const y2 = inner[1] + 30, h2 = inner[3] - 30;
  const resample = [inner[0], y2, third + 16, h2];
  const load = [resample[0] + resample[2] + 6, y2, third - 16, h2];
  const monitor = [load[0] + load[2] + 6, y2, inner[0] + inner[2] - (load[0] + load[2] + 6), h2];
  const screen = row(360);

  // The everyday keys, then the page keys right over the well, so the open page's tab meets
  // it (LEARN goes down with the browser's keys).
  const actions = row(trayHeight, 8);
  const keyRow = row(trayHeight);
  const tray = keyRow;
  const pageKeys = {};
  {
    const groupGap = 10, border = 1, keyed = [0, 1, 2, 3, 8];
    const room = tray[2] - groupGap - 4 * border;
    let offset = tray[0] + border;
    keyed.forEach((t, shown) => {
      if (t === 8) offset += 2 * border + groupGap;
      const left = offset + trunc(room * shown / keyed.length), right = offset + trunc(room * (shown + 1) / keyed.length);
      pageKeys[t] = [left, tray[1] + border, right - left, trayHeight - 2 * border];
    });
  }
  const random = [x, actions[1], 78, trayHeight];
  const amount = [x + 82, actions[1], 48, trayHeight - 2];
  const routes = [x + 134, actions[1], 104, trayHeight];
  const undo = [x + 242, actions[1], 60, trayHeight];
  const redo = [x + 306, actions[1], 60, trayHeight];

  // The well: the four MOD chips in a row, then the eight controls as two rows of four.
  const rail = [x, y, w, 52];
  const controlRow = [x, y + 60, w, 2 * 150];
  const well = [x, y, w, 60 + 2 * 150];
  y += well[3] + 12;
  const slotW = w / 4;
  const slots = Array.from({ length: 8 }, (_, i) => slotParts([Math.round(x + (i % 4) * slotW), controlRow[1] + Math.trunc(i / 4) * 150, Math.round(slotW), 150]));

  // The browser's own keys, in two rows, then two octaves of keys.
  const bench = [x, y, w, 40 + 8 + 40 + 8 + 84];
  const parts = {
    start: [x, y, 150, 40], mic: [x + 158, y, 72, 40], tempoLabel: [x + 238, y, 52, 40], tempo: [x + 290, y + 5, 76, 30],
    plugin: [x, y + 48, 150, 40], octDown: [x + 250, y + 48, 54, 40], octUp: [x + 312, y + 48, 54, 40],
    keys: [x, y + 96, w, 84],
  };
  const learn = [x + 158, y + 48, 84, 40];
  const H = bench[1] + bench[3] + 16;
  return { W: PW, H, phone: true, header, presets, tour, bench, benchParts: parts, strip, lcd, sourcePanel, chainPanel, chainView, arm, bars, resample, load, monitor,
           screen, keyRow, tray, pageKeys, redo, undo, routes, amount, random, learn, well, rail, controlRow, slots, instrument: true };
}

// SiltEditor::slotParts: a column's label, control and the row under it.
function slotParts(column) {
  const labelHeight = 18, valueBox = 20, underRow = 24;
  const knob = jlimit(40, 92, Math.min(column[2] - 16, column[3] - labelHeight - valueBox - underRow - 8));
  const blockH = Math.min(column[3], labelHeight + knob + valueBox + underRow);
  const blockY = column[1] + trunc((column[3] - blockH) / 2);
  const label = [column[0], blockY + 2, column[2], labelHeight];
  const under = [column[0], blockY + blockH - underRow, column[2], underRow];
  const inset = Math.min(10, trunc(column[2] / 12));
  const control = [column[0] + inset, blockY + labelHeight, column[2] - 2 * inset, blockH - labelHeight - underRow];
  return { column, label, under, control };
}

// Rectangle helpers.
export const centred = (r, w, h) => [r[0] + trunc((r[2] - w) / 2), r[1] + trunc((r[3] - h) / 2), w, h];
export const box = (r) => ({ left: r[0], top: r[1], width: r[2], height: r[3] });
