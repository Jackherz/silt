// The screen's corner picture for the open page (plugin/ScreenInsets.cpp): FILTER's response,
// RESONATOR's bands, COLOR's curve and spectra, SPACE's numbers, PITCH's scale and pattern,
// MOD's shape. They read the modulated values, so they move with them. The worklet sends what
// needs the running engine (silt_inset); the scope's sound comes with its own message.
import { C } from './theme.js';
import { JuceRandom, brighter, ellipse, fft, fittedText, glowRect, glowStroke, hex, hline, interpolate, line, path, rgba, text, vline, fillRoundRect } from './draw.js';

export const TEAL = 0x46d8c4, MAGENTA = 0xe0579c, RED = 0xc8342b, FILTER_CURVE = 0xd8693f, WHITE = 0xffffff;
export const deepBack = () => interpolate(C.insetBack, 0x000000, 0.55);   // the pictures' ground
export const INSET_PAGES = new Set([2, 4, 5, 6, 7, 8]);                   // PITCH, RESONATOR, FILTER, COLOR, SPACE, MOD
export const seconds = () => (performance.now() % 3600000) * 0.001;      // for pictures that move by themselves

// --- the live scope with each side's spectrum (LiveScope): worked out when new audio arrives ---
const SIZE = 4096, FFT = 2048;
const live = { seen: null, in: new Float32Array(SIZE), out: new Float32Array(SIZE), inDb: new Float32Array(FFT / 2), outDb: new Float32Array(FFT / 2),
               inPeak: 0, outPeak: 0, inRms: 0, outRms: 0, sampleRate: 48000, sounding: false, count: 0 };
const re = new Float64Array(FFT), im = new Float64Array(FFT);
export function liveScope(report, sampleRate) {
  if (!report || report === live.seen) return live;
  live.seen = report;
  live.count = report.written;
  live.in = report.input; live.out = report.output;
  live.sampleRate = sampleRate || 48000;
  const measure = (x) => { let peak = 0, sum = 0; for (const v of x) { peak = Math.max(peak, Math.abs(v)); sum += v * v; } return [peak, Math.sqrt(sum / x.length)]; };
  [live.inPeak, live.inRms] = measure(live.in);
  [live.outPeak, live.outRms] = measure(live.out);
  live.sounding = Math.max(live.inPeak, live.outPeak) > 1e-4;
  if (!live.sounding) return live;
  const spectrum = (x, db) => {
    for (let i = 0; i < FFT; i++) { re[i] = x[SIZE - FFT + i] * (0.5 - 0.5 * Math.cos(6.2831853 * i / (FFT - 1))); im[i] = 0; }
    fft(re, im);
    for (let k = 0; k < FFT / 2; k++) db[k] = 20 * Math.log10(Math.max(1e-7, Math.hypot(re[k], im[k]) * 4 / FFT));
  };
  spectrum(live.in, live.inDb);
  spectrum(live.out, live.outDb);
  return live;
}
// The level (dB) round a frequency: the loudest bin within ±3%.
function dbAt(s, db, hz) {
  const perBin = s.sampleRate / FFT;
  const lo = Math.max(1, Math.min(FFT / 2 - 1, Math.trunc(hz * 0.97 / perBin))), hi = Math.max(lo, Math.min(FFT / 2 - 1, Math.ceil(hz * 1.03 / perBin)));
  let best = -120;
  for (let k = lo; k <= hi; k++) best = Math.max(best, db[k]);
  return best;
}
// A spectrum across a log-frequency plot (20 Hz–20 kHz, −72…0 dB).
function spectrumPath(s, db, [px, py, pw, ph], closed) {
  const perBin = s.sampleRate / FFT, columns = 140, pts = [];
  const yFor = (level) => py + ph - ph * Math.max(0, Math.min(1, (level + 72) / 72));
  for (let c = 0; c <= columns; c++) {
    const hzLo = 20 * Math.pow(1000, c / columns), hzHi = 20 * Math.pow(1000, (c + 1) / columns);
    let level = -120;
    for (let k = Math.max(1, Math.trunc(hzLo / perBin)); k <= Math.min(FFT / 2 - 1, Math.trunc(hzHi / perBin)); k++) level = Math.max(level, db[k]);
    if (level < -119) level = db[Math.max(1, Math.min(FFT / 2 - 1, Math.round(hzLo / perBin)))];
    pts.push([px + pw * c / columns, yFor(level)]);
  }
  if (closed) return path([[px, py + ph], ...pts, [px + pw, py + ph]], true);
  return path(pts);
}

// The frame: a dark panel, a title, and the drawing area under it.
function frame(g, [x, y, w, h], title, fontPx) {
  const r = [x + 1, y + 1, w - 2, h - 2];
  g.fillStyle = hex(deepBack());
  fillRoundRect(g, ...r, 4);
  const inside = [r[0] + 8, r[1] + 4, r[2] - 16, r[3] - 8];
  g.fillStyle = hex(C.screenLime);
  g.font = `${fontPx(10)}px "B612 Mono"`;
  fittedText(g, title, [inside[0], inside[1], inside[2], 13], 0.75);
  return [inside[0], inside[1] + 16, inside[2], inside[3] - 16];
}
const faintGrid = (g, [x, y, w, h], xs, ys) => {
  g.fillStyle = rgba(C.screenLight, 0.2);
  for (const v of xs) vline(g, v, y, y + h);
  for (const v of ys) hline(g, v, x, x + w);
};
const logX = ([x, , w]) => (hz) => x + w * (Math.log10(Math.max(20, Math.min(20000, hz)) / 20) / 3);
const pct = (v) => `${Math.round(v * 100)}%`;

// --- FILTER ---
function drawFilter(g, area, c) {
  const { v, text: t, inset, fontPx } = c;
  if (!inset) return;
  const open = inset[0] > 0.5, mode = Math.round(v('filterMode')), cutoff = v('filterCutoff');
  const title = open ? 'FILTER - OPEN' : `FILTER - ${t('filterMorph', v('filterMorph'))} ${mode === 1 ? '24 DB' : '12 DB'} ${t('filterCutoff', cutoff)}`;
  const plot = frame(g, area, title, fontPx);
  const [px, py, pw, ph] = plot;
  const xFor = (hz) => px + pw * (Math.log10(hz / 20) / 3);
  const yFor = (db) => py + ph * ((24 - Math.max(-48, Math.min(24, db))) / 72);
  g.fillStyle = rgba(C.screenLight, 0.35);
  for (const hz of [100, 1000, 10000]) vline(g, xFor(hz), py, py + ph);
  hline(g, yFor(0), px, px + pw);
  const pts = [];
  for (let i = 0; i <= 120; i++) pts.push([xFor(20 * Math.pow(1000, i / 120)), yFor(inset[1 + i])]);
  g.fillStyle = rgba(C.rust, 0.35);
  g.fill(path([[pts[0][0], py + ph], ...pts, [px + pw, py + ph]], true));
  glowStroke(g, path(pts), FILTER_CURVE, 1, 1.5);
  if (!open) {
    g.fillStyle = rgba(C.screenPale, 0.7);
    vline(g, xFor(cutoff), py, py + ph);
    // ENV: how far the input's level can take the cutoff (up to ±4 octaves).
    const env = v('filterEnv') * 0.01;
    if (Math.abs(env) > 0.005) {
      const to = xFor(Math.max(20, Math.min(20000, cutoff * Math.pow(2, 4 * env)))), y = py + 4;
      g.strokeStyle = rgba(C.screenLime, 0.85); g.fillStyle = rgba(C.screenLime, 0.85);
      line(g, xFor(cutoff), y, to, y, 1.4);
      ellipse(g, to - 2.5, y - 2.5, 5, 5);
      g.font = `${fontPx(8.5)}px "B612 Mono"`;
      text(g, 'ENV', [(xFor(cutoff) + to) * 0.5 - 15, y + 3, 30, 10], 'center');
    }
  }
}

// --- RESONATOR ---
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const res = { energy: new Float32Array(128), lastTime: 0, ringing: false };
function drawResonator(g, area, c) {
  const { v, text: t, inset, fontPx, half, scope } = c;
  const notes = ['SCALE', 'HARMONICS', 'BELL', 'SAMPLE'][Math.max(0, Math.min(3, Math.round(v('resNotes'))))];
  const stretch = v('resStretch') * 0.01, snap = v('resSnap') * 0.01, rippleDepth = v('resDepth') * 0.01;
  const drift = v('resDrift') * 0.01, mix = v('resMix') * 0.01, resonance = v('resReso') * 0.01;
  const title = half === 3 ? `RESONATOR 4/4 - STRIKE ${pct(v('resStrike') * 0.01)}  PARTICLES ${pct(v('resParticles') * 0.01)}  TOP ${pct(v('resTop') * 0.01)}${v('resSaturate') > 0 ? `  SAT ${pct(v('resSaturate') * 0.01)}` : ''}`
              : half === 2 ? `RESONATOR 3/4 - STRETCH ${pct(stretch)}  SNAP ${pct(snap)}${rippleDepth > 0 ? `  RIPPLE ${v('resRipple').toFixed(1)}` : ''}`
              : half === 1 ? `RESONATOR 2/4 - ${Math.round(v('resBands'))} BANDS  Q ${Math.round(8 * Math.pow(15, resonance))}${drift > 0 ? `  DRIFT ${Math.round(drift * 100)}%` : ''}`
              : `RESONATOR 1/4 - ${notes} ${v('resRange').toFixed(1)} OCT ${t('resDecay', v('resDecay'))}  MIX ${Math.round(mix * 100)}%`;
  const plot = frame(g, area, title, fontPx);
  const n = inset ? inset[0] : 0;
  if (n === 0) return;
  const [px, py, pw, ph] = plot;
  const rootNote = inset[1];
  const spread = v('resSpread') * 0.01, width = v('resWidth') * 0.01, drive = v('resDrive') * 0.01;
  const rootHz = 440 * Math.pow(2, (rootNote - 69) / 12);
  const xFor = logX(plot);
  const base = py + ph - 2, tall = ph - 14;
  const now = seconds();
  const driftHz = 0.15 * Math.pow(20, drift), fade = 0.3 + 0.7 * mix;
  const shownQ = Math.min(8 * Math.pow(15, resonance), 60);
  faintGrid(g, plot, [xFor(100), xFor(1000), xFor(10000)], [base]);
  const dt = Math.max(0, Math.min(0.2, now - res.lastTime));
  res.lastTime = now;

  // DRIVE: the input along the bottom, pushed up and flattened as the drive squashes it.
  if (drive > 0) {
    const flicker = new JuceRandom(Math.trunc(now * 24));
    const pts = [[px, base]];
    for (let k = 0; k <= 80; k++) {
      const hz = 20 * Math.pow(1000, k / 80);
      const level = scope.sounding ? Math.max(0, Math.min(1, (dbAt(scope, scope.inDb, hz) + 72) / 72)) : 0.08 * flicker.nextFloat();
      const pushed = Math.tanh(level * (1 + 4 * drive) * 1.4) / 1.4;
      pts.push([xFor(hz), base - pushed * ph * 0.3 * drive]);
    }
    pts.push([px + pw, base]);
    g.fillStyle = rgba(C.screenLime, 0.18 * fade);
    g.fill(path(pts, true));
  }

  let ringing = false;
  for (let i = 0; i < n; i++) {
    const [modeRatio, level, ring, side] = [inset[2 + i * 4], inset[3 + i * 4], inset[4 + i * 4], inset[5 + i * 4]];
    const cents = 35 * spread * Math.sin(i * 2.4 + 0.7);
    let ratio = modeRatio * Math.pow(2, cents / 1200);
    if (drift > 0) ratio *= Math.pow(2, 48 * drift * Math.sin(6.2831853 * driftHz * now * (0.7 + 0.13 * (i % 5)) + i * 1.7) / 1200);
    const hz = rootHz * ratio;
    if (hz > 20000) break;
    const pan = side * width * (n > 1 ? 1 : 0);
    const colour = pan < 0 ? interpolate(C.screenPale, TEAL, -pan) : interpolate(C.screenPale, MAGENTA, pan);
    const lit = scope.sounding ? Math.max(0, Math.min(1, (dbAt(scope, scope.outDb, hz) + 54) / 42)) : 0;
    const e = res.energy[i] = Math.max(lit, res.energy[i] * Math.pow(10, -3 * dt / ring));
    ringing = ringing || e > 0.01;
    const peak = (centre, height, closed) => {
      const reach = Math.max(0.08, 3 / shownQ), pts = [];
      for (let k = 0; k <= 48; k++) {
        const f = centre * Math.pow(2, reach * (2 * k / 48 - 1)), detune = f / centre - centre / f;
        pts.push([xFor(f), base - height / Math.sqrt(1 + shownQ * shownQ * detune * detune)]);
      }
      if (closed) return path([[pts[0][0], base], ...pts, [xFor(centre * Math.pow(2, reach)), base]], true);
      return path(pts);
    };
    const height = tall * (0.25 + 0.75 * level) * (0.55 + 0.45 * e);
    if (spread > 0.02)
      for (const s of [-1, 1]) {
        g.strokeStyle = rgba(colour, fade * 0.3); g.lineWidth = 0.8; g.lineJoin = 'miter'; g.lineCap = 'butt';
        g.stroke(peak(hz * Math.pow(2, s * (20 + 60 * spread) / 1200), height * 0.6, false));
      }
    const grad = g.createLinearGradient(0, base - height, 0, base);
    grad.addColorStop(0, rgba(colour, fade * (0.15 + 0.45 * e)));
    grad.addColorStop(1, rgba(colour, 0));
    g.fillStyle = grad;
    g.fill(peak(hz, height, true));
    glowStroke(g, peak(hz, height, false), colour, fade * (0.45 + 0.55 * e), 0.9 + 0.8 * e);
    // DRIVE: sparks rising from the bottom into the band.
    if (drive > 0 && e > 0.05) {
      const r = new JuceRandom(i * 977);
      for (let k = 0; k < Math.round(4 * drive * e) + 1; k++) {
        const phase = (now * (0.8 + r.nextFloat()) + r.nextFloat()) % 1;
        const x = xFor(hz * Math.pow(2, (r.nextFloat() - 0.5) * 0.15));
        g.fillStyle = rgba(C.screenLime, fade * (1 - phase) * 0.9);
        g.fillRect(x - 0.8, base - phase * height - 0.8, 1.6, 1.6);
      }
    }
  }
  res.ringing = ringing;

  // TUNE: the root, marked and named.
  const rootX = xFor(rootHz), note = Math.round(rootNote);
  g.fillStyle = rgba(C.screenLime, 0.75);
  vline(g, rootX, py, base);
  g.font = `${fontPx(8.5)}px "B612 Mono"`;
  text(g, `${NOTE_NAMES[((note % 12) + 12) % 12]}${Math.floor(note / 12) - 2}`, [rootX + 3, py, 60, 10]);
  g.fillStyle = rgba(C.screenPale, 0.5);
  text(g, `${Math.round(rootHz)} HZ`, [rootX + 3, py + 10, 60, 10]);
}

// --- COLOR ---
function drawColor(g, area, c) {
  const { v, text: t, fontPx, scope } = c;
  const drive = v('colorDrive') * 0.01, bits = v('colorBits'), tone = v('colorTone') * 0.01;
  const rate = v('colorRate'), noise = v('colorNoise') * 0.01, comp = v('colorComp') * 0.01, wow = v('colorWow') * 0.01;
  const rateOff = 40000;
  const parts = [];
  if (drive > 0) parts.push(`DRIVE ${Math.round(drive * 100)}%`);
  if (bits < 15.995) parts.push(t('colorBits', bits));
  if (rate < rateOff - 1) parts.push(t('colorRate', rate));
  if (Math.abs(tone) > 0.005) parts.push(t('colorTone', tone * 100));
  if (noise > 0) parts.push('NOISE');
  if (comp > 0) parts.push('COMP');
  if (wow > 0) parts.push('WOW');
  let [px, py, pw, ph] = frame(g, area, `COLOR - ${parts.length ? parts.join('  ') : 'CLEAN'}`, fontPx);
  const now = seconds();

  // The level curve, in a square on the left.
  const side = Math.min(ph, pw * 0.32), box = [px, py, side, ph];
  px += side + 8; pw -= side + 8;
  g.strokeStyle = rgba(C.screenLight, 0.35); g.lineWidth = 1;
  g.strokeRect(box[0] + 0.5, box[1] + 0.5, box[2] - 1, box[3] - 1);
  g.strokeStyle = rgba(C.screenLight, 0.2);
  line(g, box[0], box[1] + box[3], box[0] + box[2], box[1], 0.8);
  const gain = Math.pow(10, 30 * drive / 20), bias = 0.2 * drive;
  const makeup = 1 / Math.pow(gain, 0.8), blend = Math.min(1, drive * 10);
  const steps = bits < 15.995 ? Math.pow(2, bits - 1) : 0;
  const shape = (x) => {
    let y = x + blend * ((Math.tanh(gain * x + bias) - Math.tanh(bias)) * makeup - x);
    if (steps > 0) y = Math.round(y * steps) / steps;
    return Math.max(-1, Math.min(1, y));
  };
  const boxPoint = (x, y) => [box[0] + box[2] / 2 + Math.max(-1, Math.min(1, x)) * box[2] * 0.5, box[1] + box[3] / 2 - Math.max(-1, Math.min(1, y)) * box[3] * 0.5];
  const curve = [];
  for (let i = 0; i <= 120; i++) { const x = -1 + 2 * i / 120; curve.push(boxPoint(x, shape(x))); }
  glowStroke(g, path(curve), C.screenLime, scope.sounding ? 0.55 : 0.9, 1.3);
  if (scope.sounding && scope.inPeak > 1e-4) {
    // The sound through it: each sample's input against its output, scaled by the input's peak.
    g.fillStyle = rgba(TEAL, 0.55);
    const k = 1 / scope.inPeak;
    for (let i = SIZE - 1200; i < SIZE; i += 2) { const [x, y] = boxPoint(scope.in[i] * k, scope.out[i] * k); g.fillRect(x - 0.8, y - 0.8, 1.6, 1.6); }
  } else {
    // Quiet: a sine riding the curve, so the squash and the steps still show.
    const x = Math.sin(6.2831853 * 0.5 * now), [dx, dy] = boxPoint(x, shape(x));
    g.fillStyle = rgba(TEAL, 0.25); ellipse(g, dx - 5, dy - 5, 10, 10);
    g.fillStyle = hex(TEAL); ellipse(g, dx - 2, dy - 2, 4, 4);
  }

  // Across frequency. The live spectra use −72…0 dB; TONE's tilt ±9 dB round a line 40% down.
  const plot = [px, py, pw, ph];
  const xFor = logX(plot);
  const mid = py + ph * 0.4;
  const yFor = (db) => mid - db / 9 * ph * 0.4;
  faintGrid(g, plot, [xFor(100), xFor(1000), xFor(10000)], [mid]);
  let spectra = plot;
  if (comp > 0) {
    // COMP: a gain-reduction meter on the right edge, falling from the top.
    const meter = [px + pw - 6, py, 6, ph];
    spectra = [px, py, pw - 10, ph];
    const threshold = -30 * comp, ratio = 1 + 7 * comp;
    const peakDb = 20 * Math.log10(Math.max(scope.inPeak, 1e-6));
    const reduction = scope.sounding ? Math.max(0, peakDb - threshold) * (1 - 1 / ratio) : 0;
    g.fillStyle = 'rgba(0,0,0,0.4)'; g.fillRect(...meter);
    glowRect(g, [meter[0], meter[1], meter[2], meter[3] * Math.max(0, Math.min(1, reduction / 24))], C.screenLime);
  }
  const [sx, sy, sw, sh] = spectra;
  if (scope.sounding) {
    g.fillStyle = rgba(C.screenPale, 0.12); g.fill(spectrumPath(scope, scope.inDb, spectra, true));
    g.strokeStyle = rgba(C.screenPale, 0.35); g.lineWidth = 0.8; g.lineJoin = 'miter'; g.lineCap = 'butt'; g.stroke(spectrumPath(scope, scope.inDb, spectra, false));
    glowStroke(g, spectrumPath(scope, scope.outDb, spectra, false), TEAL, 1, 1.1);
  }
  if (noise > 0) {
    // NOISE: a hiss floor along the bottom that rides the sound's level.
    const level = scope.sounding ? Math.max(0, Math.min(1, scope.outRms * 4)) : 0;
    const r = new JuceRandom(Math.trunc(now * 20));
    const h = sh * (0.04 + 0.3 * noise * noise * level);
    g.fillStyle = rgba(C.screenPale, 0.2 + 0.4 * noise);
    for (let x = sx; x < sx + sw; x += 2) g.fillRect(x, sy + sh - h * (0.3 + 0.7 * r.nextFloat()), 1.2, h);
  }
  if (rate < rateOff - 1) {
    // RATE: above half the hold rate folds back below it; the fold, and the sound above it
    // mirrored down as the alias it turns into.
    const foldHz = Math.max(20, Math.min(20000, rate * 0.5)), fold = xFor(foldHz);
    const grad = g.createLinearGradient(fold, 0, sx + sw, 0);
    grad.addColorStop(0, rgba(RED, 0.18)); grad.addColorStop(1, rgba(RED, 0));
    g.fillStyle = grad; g.fillRect(fold, sy, sx + sw - fold, sh);
    const levelAt = (hz) => (scope.sounding ? dbAt(scope, scope.inDb, hz) : -24 - 18 * Math.log2(hz / foldHz));
    const alias = [];
    for (let i = 0; i <= 60; i++) {
      const above = foldHz * Math.pow(2, 2 * i / 60), mirrored = 2 * foldHz - above;
      if (mirrored < 20 || above > 24000) break;
      alias.push([xFor(mirrored), sy + sh - sh * Math.max(0, Math.min(1, (levelAt(above) + 72) / 72))]);
    }
    if (alias.length) glowStroke(g, path(alias), MAGENTA, 0.85, 1);
    g.fillStyle = hex(brighter(RED, 0.3));
    vline(g, fold, sy, sy + sh);
  }
  // TONE: the tilt round 800 Hz (silt::Color's one-pole split); WOW sends a wobble along it.
  const high = Math.pow(10, 6 * tone / 20), low = 1 / high, tilt = [];
  for (let i = 0; i <= 100; i++) {
    const hz = 20 * Math.pow(1000, i / 100), w = hz / 800;
    // |(low + high·jw) / (1 + jw)|
    const db = 20 * Math.log10(Math.hypot(low, high * w) / Math.hypot(1, w));
    tilt.push([xFor(hz), yFor(db) + wow * 3.5 * Math.sin(i * 0.55 - now * 5)]);
  }
  glowStroke(g, path(tilt), C.screenLime, 1, 1.4);
}

// --- SPACE: the numbers behind the sky, each with a small bar for how far along its range ---
export function spaceView(v, tempo, quarters) {
  const synced = v('spaceSync') > 0.5;
  const ms = synced ? quarters(Math.round(v('spaceDiv'))) * 60000 / Math.max(20, Math.min(400, tempo)) : v('spaceTime');
  // In 32-bit floats, as the plug-in works them out (so 1.575 shows as 1.57, as there).
  const f = Math.fround;
  const feedback = f(v('spaceFeedback') * 0.01), size = f(v('spaceSize') * 0.01), tone = f(v('spaceTone') * 0.01);
  const decay = Math.max(0.1, f(v('spaceDecay')));
  const taps = [];
  for (let level = 1; level > 0.03 && taps.length < 24; level *= feedback) taps.push(level);
  return { synced, delay: ms / 1000, feedback, delayLevel: v('spaceDelay') * 0.01, reverbLevel: v('spaceReverb') * 0.01, decay, size, tone,
           shimmer: v('spaceShimmer') * 0.01, frozen: v('spaceFreeze') > 0.5, pingPong: Math.round(v('spaceMode')) === 0, duck: Math.round(v('spaceDuck')),
           preDelay: f(0.035 * size), early: f(0.025 + f(0.125 * size)), highDecay: Math.max(0.05, f(f(0.15 + f(0.75 * tone)) * decay)), taps };
}
function drawSpace(g, area, c) {
  const { v, text: t, fontPx, tempo, quarters } = c;
  const s = spaceView(v, tempo, quarters);
  const plot = frame(g, area, `SPACE${s.frozen ? '  - FROZEN' : ''}`, fontPx);
  const delayOn = s.delayLevel > 0, reverbOn = s.reverbLevel > 0;
  const ms = (sec) => (sec < 1 ? `${Math.round(Math.fround(sec * 1000))} MS` : `${sec.toFixed(2)} S`);   // roundToInt (seconds * 1000.0f)
  const logShare = (x, lo, hi) => Math.log(Math.max(x, lo) / lo) / Math.log(hi / lo);
  const repeats = s.feedback <= 0 ? 1 : s.taps.length;
  const rows = [
    ['DELAY', delayOn ? (s.synced ? t('spaceDiv', v('spaceDiv')) : ms(s.delay)) : 'OFF', logShare(s.delay, 0.01, 4), delayOn, MAGENTA],
    ['REPEATS', `${repeats}${s.pingPong ? ' L/R' : ' ST'}`, repeats / 24, delayOn, MAGENTA],
    ['LEVEL', `${Math.round(s.delayLevel * 100)}%`, s.delayLevel, delayOn, MAGENTA],
    ['PRE', ms(s.preDelay), s.preDelay / 0.035, reverbOn, TEAL],
    ['EARLY', ms(s.early), s.early / 0.15, reverbOn, TEAL],
    ['DECAY', s.frozen ? 'HOLD' : t('spaceDecay', s.decay), s.frozen ? 1 : logShare(s.decay, 0.3, 30), reverbOn, TEAL],
    ['HIGHS', s.frozen ? 'HOLD' : `${s.highDecay.toFixed(2)} S`, s.frozen ? 1 : s.highDecay / s.decay, reverbOn, TEAL],
    ['SHIMMER', `${Math.round(s.shimmer * 100)}%`, s.shimmer, reverbOn && s.shimmer > 0, TEAL],
    ['REVERB', `${Math.round(s.reverbLevel * 100)}%`, s.reverbLevel, reverbOn, TEAL],
    ['DUCK', s.duck > 0 ? t('spaceDuck', s.duck) : 'OFF', s.duck / 3, (delayOn || reverbOn) && s.duck > 0, C.screenLime],
  ];
  const [px, py, pw, ph] = plot;
  const columns = pw > 260 ? 2 : 1, perColumn = Math.ceil(rows.length / columns);
  const rowH = Math.min(13, ph / perColumn), colW = pw / columns;
  g.font = `${fontPx(9)}px "B612 Mono"`;
  rows.forEach(([label, value, share, on, colour], i) => {
    const column = Math.floor(i / perColumn), row = i % perColumn;
    const cell = [px + colW * column, py + rowH * row, colW - 8, rowH];
    g.fillStyle = rgba(C.screenPale, on ? 0.75 : 0.3);
    text(g, label, [cell[0], cell[1], 56, rowH]);
    g.fillStyle = on ? '#ffffff' : rgba(C.screenPale, 0.3);
    text(g, value, [cell[0] + cell[2] - 52, cell[1], 52, rowH], 'right');
    // The bar: a faint track, filled (and glowing) while it's in use.
    // Between the label (56) and the value (52), 4 in from each, 3 tall.
    const track = [cell[0] + 56 + 4, cell[1] + rowH / 2 - 1.5, Math.max(0, cell[2] - 56 - 52 - 8), 3];
    g.fillStyle = rgba(C.screenLight, 0.2);
    g.fillRect(...track);
    if (on) glowRect(g, [track[0], track[1], track[2] * Math.max(0, Math.min(1, share)), 3], colour);
  });
}

// --- PITCH ---
function drawPitch(g, area, c) {
  const { v, text: t, inset, fontPx } = c;
  const scale = Math.round(v('scale')), key = Math.round(v('key'));
  const random = v('randPitch'), depth = v('patternDepth'), steps = Math.round(v('patternSteps'));
  let [px, py, pw, ph] = frame(g, area, `PITCH - ${t('scale', scale)}  KEY ${t('key', key)}${random > 0.05 ? `  RND ${t('randPitch', random)}` : ''}`, fontPx);
  // A keyboard: in-scale notes lit, the key brightest.
  const keysH = ph * 0.45, mask = inset ? inset[0] : 0xfff, w = pw / 12;
  g.font = `${fontPx(8.5)}px "B612 Mono"`;
  for (let note = 0; note < 12; note++) {
    const cell = [px + w * note, py, w - 2, keysH];
    const inScale = scale === 0 || ((mask >> ((note - key + 12) % 12)) & 1) !== 0;
    g.fillStyle = note === key && scale !== 0 ? hex(C.screenLime) : inScale ? rgba(C.screenPale, 0.75) : rgba(C.screenLight, 0.18);
    g.fillRect(...cell);
    g.fillStyle = hex(C.insetBack);
    text(g, NOTE_NAMES[note], cell, 'center');
  }
  // The pattern: one bar per step, up or down by DEPTH.
  py += keysH + 4; ph -= keysH + 4;
  const mid = py + ph / 2;
  g.fillStyle = rgba(C.screenLight, 0.4);
  hline(g, mid, px, px + pw);
  if (depth < 0.05) {
    g.fillStyle = rgba(C.screenPale, 0.6);
    text(g, 'NO PATTERN', [px, py, pw, ph], 'center');
    return;
  }
  if (!inset) return;
  const n = Math.max(1, Math.min(16, steps)), bw = pw / n;
  g.fillStyle = hex(FILTER_CURVE);
  for (let i = 0; i < n; i++) {
    const h = inset[1 + i] * ph * 0.5 * Math.min(1, depth / 12);
    const top = Math.min(mid, mid - h), bottom = Math.max(mid, mid - h);
    g.fillRect(px + bw * i + 1, top, bw - 2, bottom - top);
  }
}

// --- MOD ---
const TYPES = ['WAVE', 'RANDOM', 'ENVELOPE', 'FOLLOW', 'MIDI', 'MACRO', 'PER GRAIN'];
const bend = (y, curve) => (curve === 0 ? y : Math.sign(y) * Math.pow(Math.abs(y), Math.pow(4, Math.max(-1, Math.min(1, curve)))));
function drawMod(g, area, c) {
  const { inset, fontPx, mod, modColour, trace } = c;
  if (!inset) return;
  const [type, bipolarF, retrigF, backwardsF, phase, symmetry, curveP, a, d, s, rel, nowPhase, nowOutput, stage, stageMs, releasedFrom, releaseLevel] = inset;
  const bipolar = bipolarF > 0.5;
  let title = `MOD ${mod + 1} - ${TYPES[type] ?? ''}`;
  if (type === 0)
    title += `  PHASE ${Math.round(phase * 360)}°  SYM ${Math.round(symmetry * 100)}%  CURVE ${Math.round(curveP * 100)}%${retrigF > 0.5 ? '  RETRIG' : ''}${backwardsF === 1 ? '  BACKWARDS' : backwardsF === 2 ? '  STOP' : ''}`;
  const plot = frame(g, area, title, fontPx);
  const [px, py, pw, ph] = plot;
  const colour = brighter(modColour, 0.25);
  const baseline = bipolar ? py + ph / 2 : py + ph;
  g.fillStyle = rgba(C.screenLight, 0.4);
  hline(g, baseline, px, px + pw);
  const yOf = (val) => (bipolar ? baseline - Math.max(-1, Math.min(1, val)) * ph * 0.5 : baseline - Math.max(0, Math.min(1, val)) * ph);
  const pts = [];
  let playhead = null;
  if (type === 0) {
    const n = 160, cycle = inset.subarray(17);
    for (let i = 0; i <= n; i++) pts.push([px + pw * i / n, yOf(cycle[i])]);
    // The playhead: where the wave is now, riding the drawn cycle.
    const along = backwardsF === 1 ? phase - nowPhase : nowPhase - phase;
    const x = px + pw * (along - Math.floor(along));
    g.fillStyle = rgba(colour, 0.35);
    vline(g, x, py, py + ph);
    playhead = [x, yOf(nowOutput)];
    if (retrigF > 0.5) {
      const y = bipolar ? baseline - cycle[0] * ph * 0.5 : baseline - cycle[0] * ph;
      g.fillStyle = hex(colour); ellipse(g, px + 1 - 4, y - 4, 8, 8);
      g.fillStyle = hex(C.insetBack); ellipse(g, px + 1 - 1.5, y - 1.5, 3, 3);
    }
  } else if (type === 2) {
    // Attack, decay, a held stretch at sustain, release, on one time scale.
    const hold = 0.25 * (a + d + rel) + 1, total = a + d + hold + rel;
    const at = (ms, level) => [px + pw * ms / total, py + ph - level * ph];
    pts.push(at(0, 0));
    const stageLine = (fromMs, toMs, fromLevel, toLevel) => {
      for (let i = 1; i <= 24; i++) { const t = i / 24; pts.push(at(fromMs + (toMs - fromMs) * t, bend(fromLevel + (toLevel - fromLevel) * t, curveP))); }
    };
    stageLine(0, a, 0, 1); stageLine(a, a + d, 1, s); stageLine(a + d, a + d + hold, s, s); stageLine(a + d + hold, total, s, 0);
    const onLine = (ms) => {
      const level = ms < a ? ms / Math.max(a, 1e-3) : ms < a + d ? 1 - (1 - s) * (ms - a) / Math.max(d, 1e-3) : ms < a + d + hold ? s : s * (1 - (ms - a - d - hold) / Math.max(rel, 1e-3));
      return at(ms, bend(Math.max(0, Math.min(1, level)), curveP));
    };
    if (stage === 1) playhead = onLine(Math.min(stageMs, a));
    else if (stage === 2) playhead = onLine(a + Math.min(stageMs, d));
    else if (stage === 3) playhead = onLine(a + d + hold * 0.5 * Math.min(1, stageMs / 300));
    else if (stage === 4) {
      const progress = Math.min(1, stageMs / Math.max(rel, 1));
      if (releasedFrom === 1 || releasedFrom === 2) {
        // Let go before sustain: it falls from where it had got to, along a line of its own.
        const level = Math.max(0, Math.min(1, releaseLevel));
        const atMs = releasedFrom === 1 ? a * level : a + d * (1 - level) / Math.max(1 - s, 1e-3);
        const start = onLine(Math.min(atMs, a + d)), end = [start[0] + pw * rel / total, py + ph];
        g.strokeStyle = rgba(colour, 0.6); g.setLineDash([3, 3]);
        line(g, start[0], start[1], end[0], end[1], 1.2);
        g.setLineDash([]);
        playhead = [start[0] + (end[0] - start[0]) * progress, start[1] + (end[1] - start[1]) * progress];
      } else playhead = onLine(a + d + hold + rel * progress);
    }
  } else {
    // The rest: what it's been doing, from the rail chip's trace.
    const n = trace.length;
    for (let i = 0; i < n; i++) pts.push([px + pw * i / Math.max(1, n - 1), yOf(trace[i])]);
  }
  if (pts.length > 1) {
    g.strokeStyle = hex(colour); g.lineWidth = 1.8; g.lineJoin = 'round'; g.lineCap = 'round';
    g.stroke(path(pts));
  }
  if (playhead) {
    g.fillStyle = '#ffffff'; ellipse(g, playhead[0] - 3.5, playhead[1] - 3.5, 7, 7);
    g.strokeStyle = hex(colour); g.lineWidth = 1.5;
    g.beginPath(); g.ellipse(playhead[0], playhead[1], 3.5, 3.5, 0, 0, Math.PI * 2); g.stroke();
  }
}

// The open page's picture; false for pages without one.
export function drawInset(g, area, page, c) {
  g.save();
  g.beginPath(); g.rect(...area); g.clip();
  switch (page) {
    case 5: drawFilter(g, area, c); break;
    case 4: drawResonator(g, area, c); break;
    case 6: drawColor(g, area, c); break;
    case 7: drawSpace(g, area, c); break;
    case 2: drawPitch(g, area, c); break;
    case 8: drawMod(g, area, c); break;
    default: g.restore(); return false;
  }
  g.restore();
  return true;
}
