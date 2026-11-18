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
  const inner = [sourcePanel[0] + spineWidth + 6, sourcePanel[1] + 4, 290 - spineWidth - 12, stripHeight - 8];
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

  return { W, H, header, presets, tour, bench, strip, lcd, sourcePanel, chainPanel, chainView, arm, bars, resample, load, monitor,
           screen, keyRow, tray, pageKeys, redo, undo, routes, amount, random, learn, well, rail, controlRow, slots, instrument };
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
