// The screen (plugin/GrainScreen.h), drawn on a canvas: the sample's waveform with the region,
// SLICE's lines and the voices' playheads; under it the grain field, where every grain leaves
// a short trail (Graphite's look: dots); LIVE's scrolling ring; and the lines of text. It reads
// the audio's latest report itself, 60 times a second, without redrawing the rest of the panel.
import React, { useEffect, useMemo, useRef } from 'react';
import { C } from './theme.js';

const COLUMNS = 32768;   // the overview's columns (SiltProcessor's overviewColumns)
const hex = (rgb) => `#${rgb.toString(16).padStart(6, '0')}`;
const rgba = (rgb, a) => `rgba(${rgb >> 16},${(rgb >> 8) & 255},${rgb & 255},${a})`;
const WHITE = 0xffffff;

// The sample as the screen shows it: per column the lowest and highest of either channel.
export function makeOverview(sample) {
  if (!sample) return null;
  const [l, r] = sample.channels;
  const frames = l.length, cols = Math.min(COLUMNS, frames);
  const min = new Float32Array(cols), max = new Float32Array(cols);
  let mono = true;
  for (let c = 0; c < cols; c++) {
    const a = Math.floor((c * frames) / cols), b = Math.max(a + 1, Math.floor(((c + 1) * frames) / cols));
    let lo = 0, hi = 0;
    for (let i = a; i < b; i++) {
      const x = l[i], y = r[i];
      if (x < lo) lo = x; if (x > hi) hi = x;
      if (y < lo) lo = y; if (y > hi) hi = y;
      if (mono && x !== y) mono = false;
    }
    min[c] = lo; max[c] = hi;
  }
  return { min, max, mono, seconds: frames / sample.sampleRate, sampleRate: sample.sampleRate, name: sample.name };
}

// The screen's parts (GrainScreen's paintScreen): the glass inside a 6 px bezel, a line of text
// at the top and the bottom, the grain field taking `fieldShare` of the rest, the waveform above.
function layout(w, h, fieldShare) {
  const glass = [6, 6, w - 12, h - 12];
  let inner = [glass[0] + 10, glass[1] + 6, glass[2] - 20, glass[3] - 12];
  const top = [inner[0], inner[1], inner[2], 16];
  const bottom = [inner[0], inner[1] + inner[3] - 16, inner[2], 16];
  inner = [inner[0], inner[1] + 16, inner[2], inner[3] - 16 - 16 - 4];
  const rollH = Math.round(inner[3] * fieldShare);
  const roll = [inner[0], inner[1] + inner[3] - rollH, inner[2], rollH];
  const wave = [inner[0], inner[1], inner[2], inner[3] - rollH - 6];
  return { glass, top, bottom, roll, wave };
}

export function Screen({ r, scale, audio, overview, region, slices, gain, live, frozen, feed, fontPx }) {
  const canvas = useRef(null);
  const field = useRef(null);   // the grain field with its trails, at 2× the roll's size
  const state = useRef({});
  state.current = { overview, region, slices, gain, live, frozen, feed };
  const [x0, y0, w, h] = r;
  const L = useMemo(() => layout(w, h, 0.36), [w, h]);

  // The waveform at this zoom and gain: drawn once per sample, gain and size.
  const waveCache = useRef(null);
  useEffect(() => { waveCache.current = null; }, [overview, gain, scale]);

  useEffect(() => {
    let raf = 0, tick = 0, overSeen = -1, fullUntil = 0, lastReport = null;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      const c = canvas.current;
      if (!c) return;
      const px = (window.devicePixelRatio || 1) * scale;
      const cw = Math.round(w * px), ch = Math.round(h * px);
      if (c.width !== cw || c.height !== ch) { c.width = cw; c.height = ch; waveCache.current = null; }
      const g = c.getContext('2d');
      g.setTransform(px, 0, 0, px, 0, 0);
      const s = state.current, report = audio?.lastDisplay?.screen;
      const views = [], heads = [];
      let n = 0, isLive = s.live, writeShare = 0, over = 0;
      if (report) {
        n = report[0]; isLive = report[1] > 0.5 || s.live; writeShare = report[2]; over = report[3];
        for (let v = 0; v < 16; v++) heads.push(report[4 + v]);
        for (let i = 0; i < n; i++) {
          const a = 20 + i * 6;
          views.push({ position: report[a], pan: report[a + 1], level: report[a + 2], reversed: report[a + 3] > 0.5, voice: report[a + 4], slot: report[a + 5] });
        }
      }
      const ov = s.overview;
      const { glass, top, bottom, roll, wave } = L;

      // Flat, like the rest of the panel: the screen's colour as one rounded surface.
      g.clearRect(0, 0, w, h);
      g.fillStyle = hex(C.screenBlue);
      roundRect(g, 0, 0, w, h, 6);
      g.fill();

      g.font = `${fontPx(12)}px "B612 Mono"`;
      g.textBaseline = 'middle';
      const text = (t, box, align = 'left', colour = '#ffffff') => {
        g.fillStyle = colour;
        g.textAlign = align;
        g.fillText(t, align === 'left' ? box[0] : align === 'right' ? box[0] + box[2] : box[0] + box[2] / 2, box[1] + box[3] / 2 + 1);
      };
      const toX = (share) => (isLive ? xOf(share, writeShare) : share) * wave[2] + wave[0];

      if (isLive) {
        text(`${s.feed ? "FEED: SILT'S OWN SOUND" : 'LIVE INPUT'}${s.frozen ? '  FROZEN' : ''}`, top);
        text('8 S  NOW >', top, 'right');
        const peaks = audio?.livePeaks;
        const empty = !peaks || !peaks.some((p) => p > 0.001);
        if (empty) drawEmptyLive(g, wave, s.frozen ? 'NOTHING HELD' : s.feed ? 'PLAY A NOTE' : 'NO SIGNAL', fontPx);
        else drawLiveWave(g, wave, peaks, writeShare, s.frozen);
      } else if (!ov) {
        text('**** NO SAMPLE ****', top, 'center');
        text('DROP AN AUDIO FILE HERE, PRESS LOAD, OR ARM AND RECORD', wave, 'center', hex(C.screenPale));
        text('READY.', bottom);
        return;
      } else {
        text(ov.name.toUpperCase(), top);
        text(`${ov.seconds.toFixed(2)} S  ${(ov.sampleRate / 1000).toFixed(ov.sampleRate >= 100000 ? 0 : 1)}K ${ov.mono ? 'MONO' : 'ST'}`, top, 'right');
        if (!waveCache.current) waveCache.current = drawWaveCache(ov, wave, px, s.gain);
        g.drawImage(waveCache.current, wave[0], wave[1], wave[2], wave[3]);
        // The region: what's outside it dimmed on the waveform and the field, its edges in lime.
        const [start, length] = s.region;
        const a = Math.max(wave[0], toX(start)), b = Math.min(wave[0] + wave[2], Math.max(a, toX(start + length)));
        const full = [wave[0], wave[1], wave[2], roll[1] + roll[3] - wave[1]];
        g.fillStyle = rgba(C.screenBlue, 0.72);
        g.fillRect(full[0], full[1], a - full[0], full[3]);
        g.fillRect(b, full[1], full[0] + full[2] - b, full[3]);
        g.fillStyle = hex(C.screenLime);
        g.fillRect(Math.trunc(a), full[1], 1, full[3]);
        g.fillRect(Math.trunc(b) - 1, full[1], 1, full[3]);
        // SLICE: a line at each start, numbered while there's room.
        if (s.slices.length) {
          const spacing = wave[2] * (s.slices.length > 1 ? s.slices[1] - s.slices[0] : 1);
          g.font = `${fontPx(9)}px "B612 Mono"`;
          s.slices.forEach((share, i) => {
            const x = toX(share);
            g.fillStyle = rgba(C.screenLime, 0.55);
            g.fillRect(x, wave[1], 1, wave[3]);
            if (spacing > 16) { g.fillStyle = hex(C.screenLime); g.textAlign = 'left'; g.fillText(String(i + 1), x + 2, wave[1] + 7); }
          });
          g.font = `${fontPx(12)}px "B612 Mono"`;
        }
      }
      g.fillStyle = rgba(C.screenLight, 0.5);
      g.fillRect(roll[0], roll[1] - 3, roll[2], 1);

      // The grain field: fade what's there, then each grain's dot and glow, once per report
      // (the plug-in's 60 a second), however often the browser draws.
      const fresh = report && report !== lastReport;
      lastReport = report;
      tick++;
      const fw = Math.round(roll[2] * 2), fh = Math.round(roll[3] * 2);
      if (!field.current || field.current.width !== fw || field.current.height !== fh) {
        field.current = document.createElement('canvas');
        field.current.width = fw; field.current.height = fh;
      }
      const f = field.current.getContext('2d');
      if (fresh) {
      f.globalCompositeOperation = 'destination-out';
      f.fillStyle = `rgba(0,0,0,${1 - (170 + 256) / 2 / 256})`;   // dots keep 170 of 256 a frame; long trails: halfway to all
      f.fillRect(0, 0, fw, fh);
      f.globalCompositeOperation = 'source-over';
      const laneTop = fh * 0.12, laneBottom = fh * 0.88;
      for (const v of views) {
        const level = Math.max(0, Math.min(1, v.level));
        const x = (isLive ? xOf(v.position, writeShare) : v.position) * fw;
        const hash = (v.voice * 131 + v.slot * 2654435761) >>> 0;
        const own = ((hash % 1000) / 1000 - 0.5) * 0.5;
        const y = laneTop + (Math.max(-1, Math.min(1, v.pan * 0.85 + own)) + 1) / 2 * (laneBottom - laneTop);
        const rr = (1.5 + 4 * level) * 2;
        const colour = v.reversed ? C.screenLime : WHITE;
        f.fillStyle = rgba(colour, 0.15 + 0.25 * level);
        ellipse(f, x, y, rr * 1.3);
        f.fillStyle = rgba(colour, 0.45 + 0.55 * level);
        ellipse(f, x, y, rr * 0.5);
      }
      }

      // Stereo guides: L at the top, centre, R at the bottom.
      const laneH = roll[3] / 16;
      const panToY = (p) => roll[1] + laneH * 1.5 + (p + 1) / 2 * (roll[3] - laneH * 3);
      g.fillStyle = rgba(C.screenLight, 0.45);
      g.fillRect(roll[0] + 12, Math.trunc(panToY(0)), roll[2] - 12, 1);
      g.font = `${fontPx(10)}px "B612 Mono"`;
      for (const [t, p] of [['L', -1], ['C', 0], ['R', 1]]) text(t, [roll[0], panToY(p) - 6, 10, 12], 'left', rgba(C.screenPale, 0.8));
      g.drawImage(field.current, roll[0], roll[1], roll[2], roll[3]);

      // On the waveform: a line at each grain's read position, as tall as it's loud.
      const mid = wave[1] + wave[3] / 2, half = wave[3] / 2;
      for (const v of views) {
        const level = Math.max(0, Math.min(1, v.level));
        g.fillStyle = rgba(v.reversed ? C.screenLime : WHITE, (0.25 + 0.75 * level) * 0.8);
        g.fillRect(toX(v.position), mid - half * level, 1.5, Math.max(1, 2 * half * level));
      }
      // Voice playheads: a marker over the waveform.
      g.fillStyle = hex(C.playhead);
      let voices = 0;
      for (const p of heads) {
        if (p < 0) continue;
        voices++;
        const x = toX(p);
        if (x < wave[0] || x > wave[0] + wave[2]) continue;
        g.beginPath(); g.moveTo(x - 4, wave[1] - 1); g.lineTo(x + 4, wave[1] - 1); g.lineTo(x, wave[1] + 6); g.closePath(); g.fill();
        g.fillRect(x - 0.5, wave[1], 1, wave[3]);
      }

      // The line at the bottom: what's sounding (FULL a moment after grains had to make way), in the
      // guides' face, as the plug-in draws it.
      if (over !== overSeen) { if (overSeen >= 0) fullUntil = performance.now() + 1000; overSeen = over; }
      const hud = n > 0 ? `${n}${n === 1 ? ' GRAIN   ' : ' GRAINS   '}${voices}${voices === 1 ? ' VOICE' : ' VOICES'}${performance.now() < fullUntil ? '   FULL' : ''}`
                        : isLive && !(audio?.livePeaks?.some((p) => p > 0.001)) ? '' : 'READY.';
      text(hud, bottom);
      void glass;
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [audio, scale, w, h, L, fontPx]);

  // The sample's name as the screen shows it, for screen readers (the plug-in's title) and tests.
  const shown = overview ? overview.name.toUpperCase().replace(/ /g, '-') : '';
  return <canvas ref={canvas} className="at" data-help="!screen" data-sample={shown} role="img" aria-label={shown ? `Screen: ${shown}` : 'Screen: no sample'}
                 style={{ left: x0, top: y0, width: w, height: h }} />;
}

function roundRect(g, x, y, w, h, r) {
  g.beginPath();
  g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r);
  g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath();
}
function ellipse(g, x, y, radius) { g.beginPath(); g.arc(x, y, Math.max(0.1, radius), 0, Math.PI * 2); g.fill(); }

// LIVE: x (0 oldest … 1 newest) to a share of the ring, and back.
function shareAt(x, writeShare) { const s = writeShare - (1 - x); return s - Math.floor(s); }
function xOf(share, writeShare) { const age = writeShare - share; return 1 - (age - Math.floor(age)); }

function drawWaveCache(ov, wave, px, gain) {
  const width = Math.max(1, Math.round(wave[2] * px)), height = Math.max(1, Math.round(wave[3] * px));
  const c = document.createElement('canvas');
  c.width = width; c.height = height;
  const g = c.getContext('2d');
  const mid = height * 0.5, half = height * 0.48, cols = ov.min.length;
  const perPixel = cols / width;
  for (let x = 0; x < width; x++) {
    const c0 = Math.min(cols - 1, Math.floor(x * perPixel)), c1 = Math.max(c0 + 1, Math.min(cols, Math.floor((x + 1) * perPixel)));
    let hi = 0, lo = 0;
    for (let k = c0; k < c1; k++) { if (ov.max[k] > hi) hi = ov.max[k]; if (ov.min[k] < lo) lo = ov.min[k]; }
    hi *= gain; lo *= gain;
    const clipped = hi > 1 || lo < -1;
    const top = mid - Math.min(1, hi) * half, bottom = mid - Math.max(-1, lo) * half;
    g.fillStyle = clipped ? '#c8342b' : hex(C.screenPale);
    g.fillRect(x, top, 1, Math.max(1, bottom - top));
  }
  return c;
}

function drawLiveWave(g, wave, peaks, writeShare, frozen) {
  const mid = wave[1] + wave[3] / 2, half = wave[3] * 0.48;
  g.fillStyle = frozen ? rgba(C.playhead, 0.8) : hex(C.screenPale);
  for (let x = 0; x < wave[2]; x++) {
    const share = shareAt(x / wave[2], writeShare);
    const hgt = Math.min(1, peaks[Math.max(0, Math.min(1023, Math.floor(share * 1024)))]) * half;
    g.fillRect(wave[0] + x, mid - hgt, 1, Math.max(1, 2 * hgt));
  }
  g.fillStyle = hex(C.screenLime);
  g.fillRect(wave[0] + wave[2] - 1, wave[1], 1, wave[3]);
}

// The live view with nothing in it: the time scale, a tick a second, with the word set into it.
function drawEmptyLive(g, wave, word, fontPx) {
  const mid = Math.trunc(wave[1] + wave[3] / 2);
  g.font = `${fontPx(12)}px "B612 Mono"`;
  const gw = g.measureText(word).width + 28, gx = wave[0] + (wave[2] - gw) / 2;
  g.fillStyle = rgba(C.screenPale, 0.3);
  g.fillRect(wave[0], mid, gx - wave[0], 1);
  g.fillRect(gx + gw, mid, wave[0] + wave[2] - gx - gw, 1);
  for (let s = 1; s < 8; s++) { const x = wave[0] + Math.trunc(wave[2] * s / 8); if (x < gx || x >= gx + gw) g.fillRect(x, mid - 2, 1, 5); }
  g.fillStyle = rgba(C.screenPale, 0.8);
  g.textAlign = 'center';
  g.fillText(word, wave[0] + wave[2] / 2, mid + 1);
  g.fillStyle = hex(C.screenLime);
  g.fillRect(wave[0] + wave[2] - 1, wave[1], 1, wave[3]);
}
