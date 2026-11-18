// The screen (plugin/GrainScreen.h), drawn on a canvas: the sample's waveform with the region,
// SLICE's lines and the voices' playheads; under it the grain field, where every grain leaves
// a short trail (Graphite's look: dots); LIVE's scrolling ring; and the lines of text. It reads
// the audio's latest report itself, 60 times a second, without redrawing the rest of the panel.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { C, font } from './theme.js';
import { INSET_PAGES, deepBack, drawInset, liveScope } from './insets.js';
import { SpaceScene, sceneVisible, spaceSettings } from './space.js';

const COLUMNS = 32768;   // the overview's columns (SiltProcessor's overviewColumns)
const hex = (rgb) => `#${rgb.toString(16).padStart(6, '0')}`;
const rgba = (rgb, a) => `rgba(${rgb >> 16},${(rgb >> 8) & 255},${rgb & 255},${a})`;
const WHITE = 0xffffff;
const TEAL = 0x46d8c4;       // signalTeal: what comes out of a module
const MAX_ZOOM = 32;         // GrainScreen::maxZoom
const SCROLL_BAR = 6;        // the zoom's scroll bar, along the waveform's bottom edge

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
  // The corner picture (insetArea): right-aligned at the bottom, the roll's height plus the
  // status line; the scope takes the roll left of it.
  const content = [16, 12, w - 32, h - 24];
  const insetW = Math.round(content[2] * 0.33), insetH = Math.round((content[3] - 36) * fieldShare) + 20;
  const inset = [content[0] + content[2] - insetW, content[1] + content[3] - insetH, insetW, insetH];
  // GrainScreen::rollArea measures from 30 across and 22 down (not the paint's 16 and 12).
  const area = [30, 22 + 16, w - 60, h - 44 - 16 - 20];
  const areaRollH = Math.round(area[3] * fieldShare);
  const scopeRoll = [area[0], area[1] + area[3] - areaRollH, area[2], areaRollH];
  const scope = [scopeRoll[0], scopeRoll[1], Math.min(scopeRoll[0] + scopeRoll[2], inset[0] - 10) - scopeRoll[0], scopeRoll[3]];
  return { glass, top, bottom, roll, wave, inset, scope };
}

export function Screen({ r, scale, audio, overview, region, slices, gain, live, frozen, feed, fontPx, transients, scope, zoomBox, sliceEdits, page, picture }) {
  const canvas = useRef(null);
  const field = useRef(null);   // the grain field with its trails, at 2× the roll's size
  const state = useRef({});
  state.current = { overview, region, slices, gain, live, frozen, feed, transients, scope, page, picture };
  const scene = useRef(null);       // SPACE's sky, made when first shown
  const sceneState = useRef({ duck: 1, at: 0 });
  const [x0, y0, w, h] = r;
  const L = useMemo(() => layout(w, h, 0.36), [w, h]);

  // The waveform at this zoom and gain: drawn once per sample, gain, size and view.
  const waveCache = useRef(null);
  const waveEnvelope = useRef([]);   // the waveform's height per pixel across (0…1 of half), for SPACE's sparkles
  useEffect(() => { waveCache.current = null; }, [overview, gain, scale]);

  // Zoom: the part of the sample in view, as shares (the screen's own: not saved).
  const view = useRef({ start: 0, end: 1 });
  const [zoom, setZoom] = useState(1);
  const setView = (from, to) => {
    const span = Math.max(1 / MAX_ZOOM, Math.min(1, to - from));
    from = Math.max(0, Math.min(1 - span, from));
    const v = view.current;
    if (Math.abs(from - v.start) < 1e-6 && Math.abs(from + span - v.end) < 1e-6) return;
    view.current = { start: from, end: from + span };
    waveCache.current = null;
    field.current = null;
    setZoom(1 / span);
  };
  useEffect(() => { view.current = { start: 0, end: 1 }; setZoom(1); waveCache.current = null; }, [overview]);
  const zoomable = () => !state.current.live && !!state.current.overview;
  const [over, setOver] = useState(false);   // the mouse on the screen: ZOOM shows

  // The scope's picture, steadied and scaled as GrainScreen::setScope does.
  const scopeState = useRef({ seen: null, in: [], out: [], quiet: true, scale: 1 });

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

      // SPACE: the sky behind everything on the glass, fading in with DELAY and REVERB (not in
      // LIVE, or with no sample), moved on by the time since the last frame.
      const insetData = audio?.lastDisplay?.inset;
      let sky = null;
      {
        const nowMs = performance.now(), st = sceneState.current;
        const seconds = Math.max(0, Math.min(0.1, (nowMs - (st.at || nowMs)) * 0.001));
        st.at = nowMs;
        if (s.page === 7 && !isLive && ov && s.picture) {
          const settings = spaceSettings(s.picture.raw, s.picture.tempo, s.picture.quarters);
          if (sceneVisible(settings)) {
            const rate = audio?.context?.sampleRate ?? 48000;
            const [count = 0, peak = 0] = s.page === 7 && insetData ? insetData : [];
            settings.wet = Math.max(0, Math.min(1, ((peak > 0 ? Math.max(-60, 20 * Math.log10(peak)) : -60) + 54) / 48));
            settings.sinceNote = count * 64 / rate;
            const sounding = heads.find((p) => p >= 0);
            st.notePlace = sounding !== undefined ? sounding : st.notePlace ?? 0.5;
            settings.notePlace = st.notePlace;
            // DUCK: dips while a note sounds, back over about half a second.
            const target = sounding !== undefined ? 1 - 0.8 * settings.duckAmount : 1;
            st.duck += (target - st.duck) * Math.min(1, seconds * (target < st.duck ? 30 : 7));
            settings.duck = st.duck;
            if (!scene.current) scene.current = new SpaceScene();
            scene.current.advance(settings, seconds, waveEnvelope.current);
            scene.current.drawBehind(g, glass, settings);
            sky = settings;
          }
        }
      }

      g.font = `${fontPx(12)}px "B612 Mono"`;
      g.textBaseline = 'middle';
      const text = (t, box, align = 'left', colour = '#ffffff') => {
        g.fillStyle = colour;
        g.textAlign = align;
        g.fillText(t, align === 'left' ? box[0] : align === 'right' ? box[0] + box[2] : box[0] + box[2] / 2, box[1] + box[3] / 2 + 1);
      };
      const v = view.current, span = v.end - v.start;
      const along = (share) => (isLive ? xOf(share, writeShare) : (share - v.start) / span);
      const toX = (share) => along(share) * wave[2] + wave[0];
      stateFor.current = { wave, isLive, toX };

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
        if (!waveCache.current) [waveCache.current, waveEnvelope.current] = drawWaveCache(ov, wave, px, s.gain, v);
        g.drawImage(waveCache.current, wave[0], wave[1], wave[2], wave[3]);
        if (span < 0.999) {
          // Zoomed: a bar along the bottom shows which part is in view; drag it (or click along
          // it) to move through the sample.
          const track = [wave[0], wave[1] + wave[3] - SCROLL_BAR, wave[2], SCROLL_BAR];
          g.fillStyle = 'rgba(0,0,0,0.45)';
          g.fillRect(...track);
          g.fillStyle = rgba(C.screenLime, drag.current?.bar ? 1 : 0.8);
          g.fillRect(...thumbIn(track, v));
        }
        if (s.transients) {
          // The transients SNAP can land on: ticks along the top of the waveform.
          g.fillStyle = rgba(C.screenLime, 0.8);
          for (const t of s.transients) { const x = toX(t); if (x >= wave[0] && x <= wave[0] + wave[2]) g.fillRect(x, wave[1], 1, 7); }
        }
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
          const spacing = wave[2] * (s.slices.length > 1 ? s.slices[1] - s.slices[0] : 1) / span;
          g.font = `${fontPx(9)}px "B612 Mono"`;
          s.slices.forEach((share, i) => {
            const x = toX(share);
            if (x < wave[0] || x > wave[0] + wave[2]) return;
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
      for (const gv of views) {
        const level = Math.max(0, Math.min(1, gv.level));
        const x = along(gv.position) * fw;
        const hash = (gv.voice * 131 + gv.slot * 2654435761) >>> 0;
        const own = ((hash % 1000) / 1000 - 0.5) * 0.5;
        const y = laneTop + (Math.max(-1, Math.min(1, gv.pan * 0.85 + own)) + 1) / 2 * (laneBottom - laneTop);
        const rr = (1.5 + 4 * level) * 2;
        const colour = gv.reversed ? C.screenLime : WHITE;
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
      for (const gv of views) {
        const level = Math.max(0, Math.min(1, gv.level));
        g.fillStyle = rgba(gv.reversed ? C.screenLime : WHITE, (0.25 + 0.75 * level) * 0.8);
        g.fillRect(toX(gv.position), mid - half * level, 1.5, Math.max(1, 2 * half * level));
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

      if (sky) scene.current.drawOver(g, glass, wave, sky);

      // The line at the bottom: what's sounding (FULL a moment after grains had to make way), in the
      // guides' face, as the plug-in draws it.
      if (over !== overSeen) { if (overSeen >= 0) fullUntil = performance.now() + 1000; overSeen = over; }
      const hud = n > 0 ? `${n}${n === 1 ? ' GRAIN   ' : ' GRAINS   '}${voices}${voices === 1 ? ' VOICE' : ' VOICES'}${performance.now() < fullUntil ? '   FULL' : ''}`
                        : isLive && !(audio?.livePeaks?.some((p) => p > 0.001)) ? '' : 'READY.';
      text(hud, bottom);
      void glass;

      // RESONATOR, FILTER, COLOR: the scope over the roll, left of the corner picture.
      if (s.scope) drawScope(g, L.scope, s.scope, audio?.lastScope, scopeState.current, audio?.context?.sampleRate ?? 48000, fontPx);
      else scopeState.current.seen = null;
      // The open page's corner picture, bottom right.
      if (INSET_PAGES.has(s.page) && s.picture)
        drawInset(g, L.inset, s.page, { ...s.picture, fontPx, inset: insetData, scope: liveScope(audio?.lastScope, audio?.context?.sampleRate) });
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [audio, scale, w, h, L, fontPx]);

  // --- the mouse on the screen (GrainScreen's mouse handling) ---
  const stateFor = useRef({});
  const drag = useRef(null);
  const at = (e) => { const b = canvas.current.getBoundingClientRect(); return [(e.clientX - b.left) / b.width * w, (e.clientY - b.top) / b.height * h]; };
  const waveShareAt = (x) => { const { start, end } = view.current, wv = L.wave; return Math.max(0, Math.min(1, start + (end - start) * (x - wv[0]) / Math.max(1, wv[2]))); };
  const inWave = ([x, y]) => x >= L.wave[0] && x < L.wave[0] + L.wave[2] && y >= L.wave[1] && y < L.wave[1] + L.wave[3];
  const sliceAt = (p) => {
    const st = stateFor.current;
    if (!slices.length || st.isLive || !inWave(p)) return -1;
    let best = -1, nearest = 6;
    slices.forEach((share, i) => { const d = Math.abs(st.toX(share) - p[0]); if (d < nearest) { nearest = d; best = i; } });
    return best;
  };
  const track = () => [L.wave[0], L.wave[1] + L.wave[3] - SCROLL_BAR, L.wave[2], SCROLL_BAR];
  const onScrollBar = ([x, y]) => { const t = track(); return zoomable() && view.current.end - view.current.start < 0.999 && x >= t[0] && x < t[0] + t[2] && y >= t[1] - 4 && y < t[1] + t[3] + 4; };
  const scrollTo = (thumbX) => { const t = track(), span = view.current.end - view.current.start, from = (thumbX - t[0]) / Math.max(1, t[2]); setView(from, from + span); };

  const down = (e) => {
    if (e.button !== 0) return;
    const p = at(e);
    if (onScrollBar(p)) {
      // On the thumb: hold it where it was taken; elsewhere on the bar: centre the view there.
      let thumb = thumbIn(track(), view.current);
      if (p[0] < thumb[0] || p[0] >= thumb[0] + thumb[2]) { scrollTo(p[0] - thumb[2] / 2); thumb = thumbIn(track(), view.current); }
      drag.current = { bar: true, grab: p[0] - thumb[0] };
    } else {
      const i = sliceAt(p);
      if (i < 0) return;
      sliceEdits.begin();
      drag.current = { slice: i };
    }
    e.preventDefault();
    const move = (m) => {
      const q = at(m);
      if (drag.current?.bar) scrollTo(q[0] - drag.current.grab);
      else if (drag.current) sliceEdits.move(drag.current.slice, waveShareAt(q[0]));
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      if (drag.current && !drag.current.bar) sliceEdits.end();
      drag.current = null;
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  // SLICE: double-click a line to remove it, or anywhere else on the waveform to add one.
  const doubleClick = (e) => {
    const p = at(e);
    if (!slices.length || stateFor.current.isLive || !inWave(p)) return;
    const i = sliceAt(p);
    if (i >= 0) sliceEdits.remove(i); else sliceEdits.add(waveShareAt(p[0]));
  };
  const hover = (e) => {
    if (drag.current) return;
    const p = at(e);
    canvas.current.style.cursor = onScrollBar(p) ? 'grab' : sliceAt(p) >= 0 ? 'ew-resize' : '';
  };
  // Scroll on the waveform: zoom around the mouse; Shift (or a sideways swipe): move along.
  // In JUCE's wheel units (macOS): a trackpad's pixel is 0.5/256, a mouse wheel's notch 10/256.
  // A notch shows as a multiple of 120 in the browser's older wheelDelta.
  const wheelRef = useRef(null);
  wheelRef.current = (e) => {
    if (!zoomable()) return;
    e.preventDefault();
    const notches = (legacy, delta) => (legacy && legacy % 120 === 0 ? legacy / 120 * 10 / 256
                                        : e.deltaMode === 1 ? -delta * 10 / 256 : -delta * 0.5 / 256);
    const dx = notches(e.wheelDeltaX, e.deltaX), dy = notches(e.wheelDeltaY, e.deltaY);
    const { start, end } = view.current, span = end - start;
    if (e.shiftKey || Math.abs(dx) > Math.abs(dy)) {
      const sideways = e.shiftKey ? (Math.abs(dy) > Math.abs(dx) ? dy : dx) : dx;
      const move = -sideways * span * 0.5;
      setView(start + move, end + move);
      return;
    }
    const anchor = waveShareAt(at(e)[0]);
    const newSpan = Math.max(1 / MAX_ZOOM, Math.min(1, span * Math.pow(0.5, dy * 4)));
    const share = (anchor - start) / span;
    setView(anchor - share * newSpan, anchor - share * newSpan + newSpan);
  };
  useEffect(() => {
    const c = canvas.current, wheel = (e) => wheelRef.current(e);
    c.addEventListener('wheel', wheel, { passive: false });
    return () => c.removeEventListener('wheel', wheel);
  }, []);

  // ZOOM (ZoomReadout): drag up to zoom in, down to zoom out (every 100 px doubles it, round
  // the middle; Shift finer); double-click to see it all. At 1× it shows only while the mouse is on the screen.
  const zoomDown = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const from = { ...view.current }, y0 = e.clientY;
    drag.current = { zoom: true };
    const move = (m) => {
      if (!zoomable()) return;
      const upwards = (y0 - m.clientY) / scale * (m.shiftKey ? 0.25 : 1);
      const centre = 0.5 * (from.start + from.end);
      const span = Math.max(1 / MAX_ZOOM, Math.min(1, (from.end - from.start) * Math.pow(0.5, upwards / 100)));
      setView(centre - 0.5 * span, centre + 0.5 * span);
    };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); drag.current = null; setOver((o) => o); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  const zoomed = zoom > 1.01;

  // The sample's name as the screen shows it, for screen readers (the plug-in's title) and tests.
  const shown = overview ? overview.name.toUpperCase().replace(/ /g, '-') : '';
  return (
    <>
      <canvas ref={canvas} className="at" data-help="!screen" data-sample={shown} data-zoom={zoom.toFixed(2)} data-slices={slices.map((m) => m.toFixed(4)).join(',')} role="img" aria-label={shown ? `Screen: ${shown}` : 'Screen: no sample'}
              style={{ left: x0, top: y0, width: w, height: h }}
              onPointerDown={down} onPointerMove={hover} onDoubleClick={doubleClick}
              onPointerEnter={() => setOver(true)} onPointerLeave={() => setOver(false)} />
      {zoomBox && (
        <div className="at zoom-box" data-help="!zoom" onPointerDown={zoomDown} onDoubleClick={() => setView(0, 1)}
             onPointerEnter={() => setOver(true)}
             style={{ left: zoomBox[0], top: zoomBox[1], width: zoomBox[2], height: zoomBox[3], cursor: 'ns-resize',
                      ...font.screen(12 * 1.1), display: 'flex', alignItems: 'center', justifyContent: 'center', whiteSpace: 'nowrap',
                      color: zoomed ? hex(C.screenLime) : rgba(C.screenPale, 0.6), opacity: zoomed || over ? 1 : 0 }}>
          {`ZOOM ${zoom < 10 ? zoom.toFixed(1) : Math.round(zoom)}X`}
        </div>
      )}
    </>
  );
}

// The scroll bar's thumb: the part in view, at least 8 px wide.
function thumbIn(track, v) { return [track[0] + track[2] * v.start, track[1], Math.max(8, track[2] * (v.end - v.start)), track[3]]; }

// The scope (GrainScreen::drawScope): what goes into the module (faint) against what comes out
// (teal), steadied on the input's latest rising zero crossing, ~21 ms, scaled to fit.
function drawScope(g, area, title, report, st, sampleRate, fontPx) {
  if (report && report !== st.seen) {
    st.seen = report;
    const input = report.input, output = report.output, n = Math.min(input.length, output.length);
    const window = Math.max(16, Math.min(Math.max(16, n >> 1), Math.round(sampleRate * 0.021)));
    let from = n - window;
    for (let i = from; i > Math.max(1, n - 2 * window); i--)
      if (input[i - 1] <= 0 && input[i] > 0) { from = i; break; }
    st.in = input.subarray(from, from + window);
    st.out = output.subarray(from, from + window);
    let peak = 0;
    for (let i = 0; i < window; i++) peak = Math.max(peak, Math.abs(st.in[i]), Math.abs(st.out[i]));
    const wasQuiet = st.quiet;
    st.quiet = peak < 1e-4;
    if (!(st.quiet && wasQuiet)) {
      const target = 0.9 / Math.max(peak, 0.02);
      st.scale = target < st.scale ? target : st.scale + (target - st.scale) * 0.15;   // shrink at once, grow slowly
    }
  }
  const [x, y, w, h] = area;
  // As the corner pictures' ground: insetBack, 55% of the way to black.
  g.fillStyle = hex(deepBack());
  g.fillRect(x, y - 2, w, h + 4);
  g.fillStyle = rgba(C.screenLight, 0.35);
  g.fillRect(x, Math.trunc(y + h / 2), w, 1);
  g.font = `${fontPx(10)}px "B612 Mono"`;
  g.textBaseline = 'middle';
  const ly = y + 3 + 6 + 1;
  g.textAlign = 'left'; g.fillStyle = hex(C.screenLime); g.fillText(title, x + 6, ly);
  g.textAlign = 'right'; g.fillStyle = hex(TEAL); g.fillText('OUT', x + w - 6, ly);
  g.textAlign = 'left'; g.fillStyle = rgba(C.screenPale, 0.7); g.fillText('IN', x + w - 6 - 26 - 30, ly);
  if (st.quiet || !st.in.length) {
    g.textAlign = 'center'; g.fillStyle = rgba(C.screenPale, 0.8); g.fillText('PLAY A NOTE', x + w / 2, y + h / 2 + 1);
    return;
  }
  const trace = (v, colour, alpha, thickness, glow) => {
    const half = h / 2 - 3, mid = y + h / 2, dx = w / Math.max(1, v.length - 1);
    g.beginPath();
    for (let i = 0; i < v.length; i++) {
      const yy = mid - Math.max(-1, Math.min(1, v[i] * st.scale)) * half;
      if (i === 0) g.moveTo(x, yy); else g.lineTo(x + dx * i, yy);
    }
    g.lineJoin = 'round';
    if (glow) {   // a wide soft stroke under the crisp one, as the corner pictures draw the sound
      g.strokeStyle = rgba(colour, alpha * 0.12); g.lineWidth = thickness * 5; g.stroke();
      g.strokeStyle = rgba(colour, alpha * 0.25); g.lineWidth = thickness * 2.5; g.stroke();
    }
    g.strokeStyle = rgba(colour, alpha); g.lineWidth = thickness; g.stroke();
  };
  g.save();
  g.beginPath(); g.rect(x + 1, y + 1, w - 2, h - 2); g.clip();
  trace(st.in, C.screenPale, 0.4, 1.1, false);
  trace(st.out, TEAL, 1, 1.4, true);
  g.restore();
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

function drawWaveCache(ov, wave, px, gain, v) {
  const envelope = new Float32Array(Math.max(1, Math.round(wave[2])));
  const width = Math.max(1, Math.round(wave[2] * px)), height = Math.max(1, Math.round(wave[3] * px));
  const c = document.createElement('canvas');
  c.width = width; c.height = height;
  const g = c.getContext('2d');
  const mid = height * 0.5, half = height * 0.48, cols = ov.min.length;
  // The columns in view, each pixel taking the loudest of those under it.
  const first = v.start * cols, perPixel = (v.end - v.start) * cols / width;
  for (let x = 0; x < width; x++) {
    const c0 = Math.max(0, Math.min(cols - 1, Math.trunc(first + x * perPixel)));
    const c1 = Math.max(c0 + 1, Math.min(cols, Math.trunc(first + (x + 1) * perPixel)));
    let hi = 0, lo = 0;
    for (let k = c0; k < c1; k++) { if (ov.max[k] > hi) hi = ov.max[k]; if (ov.min[k] < lo) lo = ov.min[k]; }
    hi *= gain; lo *= gain;
    const clipped = hi > 1 || lo < -1;
    const e = Math.max(0, Math.min(envelope.length - 1, Math.trunc(x * envelope.length / width)));
    envelope[e] = Math.max(envelope[e], Math.min(1, Math.max(hi, -lo)));
    const top = mid - Math.min(1, hi) * half, bottom = mid - Math.max(-1, lo) * half;
    g.fillStyle = clipped ? '#c8342b' : hex(C.screenPale);
    g.fillRect(x, top, 1, Math.max(1, bottom - top));
  }
  return [c, envelope];
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
