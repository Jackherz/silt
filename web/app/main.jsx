// Silt in the browser: the plug-in's panel at its own 1200 × 820, scaled to the
// window, drawn from the plug-in's exported data (params, layout, help, presets)
// and its own value text (the WebAssembly build).
import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { panelGeometry, centred, box, SPINE } from './geometry.js';
import { C, S, MOD, css, rgba, pageColour, font } from './theme.js';
import { isShown, isDimmed, roundToInt } from './rules.js';
import { loadSilt } from '../engine/silt-wasm.js';

const PAGE_NAMES = ['GRAIN', 'SHAPE', 'PITCH', 'PLAY', 'RESONATOR', 'FILTER', 'COLOR', 'SPACE', 'MOD', 'CHAIN'];

// --- small pieces ----------------------------------------------------------------------

function At({ r, style, children, ...rest }) {
  return <div className="at" style={{ ...box(r), ...style }} {...rest}>{children}</div>;
}

// A key: a filled rounded plate with no frame (PanelLook::drawButtonBackground).
// Its face is the bounds less 2 on the right and bottom, inset 1.
function Key({ r, text, on = false, tint = S.ink, led, accent, disabled, size, flat, style }) {
  const filled = on || accent;
  const plate = flat ? [r[0] + 1, r[1] + 1, r[2] - 2, r[3] - 2] : [r[0] + 1, r[1] + 1, r[2] - 4, r[3] - 4];
  const fontSize = size ?? Math.max(10, Math.min(12.5, r[3] * 0.42));
  return (
    <>
      <At r={plate} style={{ borderRadius: 4, background: filled ? css(accent ? S.accent : tint) : rgba(S.ink, disabled ? 0.04 : 0.08) }} />
      {led !== undefined && (
        <div className="at" style={{ left: plate[0] + 10 - 3.5, top: plate[1] + plate[3] / 2 - 3.5, width: 7, height: 7, borderRadius: 4,
                                      background: led ? css(S.lit) : rgba(S.ink, 0.25) }} />
      )}
      <At r={[r[0], r[1], r[2] - 2, r[3] - 2]} style={{ ...font.name(fontSize), color: filled ? css(S.panel) : rgba(S.ink, disabled ? 0.35 : 1),
                                                        display: 'flex', alignItems: 'center', justifyContent: 'center',
                                                        paddingLeft: led !== undefined ? 14 : 0, paddingTop: 2, ...style }}>
        {text}
      </At>
    </>
  );
}

// A switch: a small toggle and its name in the screen face (the plug-in's MONITOR).
function Switch({ r, text, on }) {
  const toggle = [r[0], r[1] + Math.trunc((r[3] - 13) / 2), 22, 13];
  return (
    <>
      <At r={toggle} style={{ border: `1.5px solid ${css(S.ink)}`, borderRadius: 2, background: on ? css(S.lit) : 'transparent' }} />
      <At r={on ? [toggle[0] + 11, toggle[1], 11, 13] : [toggle[0], toggle[1], 11, 13]} style={{ background: css(S.ink), borderRadius: 1 }} />
      <At r={[r[0] + 26, r[1], r[2] - 26, r[3]]} style={{ ...font.screen(15), color: css(S.soft), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap' }}>
        {text}
      </At>
    </>
  );
}

// A menu: the same plate with a chevron (PanelLook::drawComboBox).
function Menu({ r, text }) {
  const plate = [r[0] + 1, r[1] + 1, r[2] - 4, r[3] - 4];
  const fontSize = Math.max(10, Math.min(12.5, r[3] * 0.42));
  const cx = plate[0] + plate[2] - plate[3] / 2, cy = plate[1] + plate[3] / 2;
  return (
    <>
      <At r={plate} style={{ borderRadius: 4, background: rgba(S.ink, 0.08) }} />
      <At r={[plate[0] + 8, plate[1], plate[2] - plate[3] - 8, plate[3]]}
          style={{ ...font.name(fontSize), color: css(S.ink), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap', overflow: 'hidden', paddingTop: 2 }}>
        {text}
      </At>
      <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
        <path d={`M${cx - 3.5} ${cy - 1.5} L${cx} ${cy + 2} L${cx + 3.5} ${cy - 1.5}`} fill="none" stroke={rgba(S.ink, 0.8)}
              strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    </>
  );
}

// A surface: a filled rounded panel (fillSurface).
const Surface = ({ r, colour, corner = 6 }) => <At r={r} style={{ background: css(colour), borderRadius: corner }} />;

// A strip panel: a surface with its name turned up its left edge.
function StripPanel({ r, name }) {
  return (
    <>
      <Surface r={r} colour={S.panel} />
      <div className="at spine" style={{ left: r[0] + 1, top: r[1] + 1, width: r[3] - 2, height: SPINE - 2,
                                          transform: `translateY(${r[3] - 2}px) rotate(-90deg)`, transformOrigin: '0 0',
                                          ...font.screen(15), color: css(S.soft) }}>
        {name}
      </div>
    </>
  );
}

// --- the knob (PanelLook::drawRotarySlider) ----------------------------------------------

function Knob({ r, pos, bipolarOrigin, text }) {
  // The slider's text box sits below: 76 × 16. The rest is the rotary.
  const boxH = 16, boxW = 76;
  const rot = [r[0], r[1], r[2], r[3] - boxH];
  const side = Math.min(rot[2], rot[3]), u = side / 104;
  const cx = rot[0] + rot[2] / 2, cy = rot[1] + rot[3] / 2;
  const rad = (side - 30 * u) / 2, track = rad + 8 * u;
  const a0 = -135 * Math.PI / 180, a1 = 135 * Math.PI / 180;
  const at = (p) => a0 + p * (a1 - a0);
  const arc = (from, to) => {
    const s = at(from), e = at(to);
    const x0 = cx + track * Math.sin(s), y0 = cy - track * Math.cos(s);
    const x1 = cx + track * Math.sin(e), y1 = cy - track * Math.cos(e);
    return `M${x0} ${y0} A${track} ${track} 0 ${e - s > Math.PI ? 1 : 0} 1 ${x1} ${y1}`;
  };
  const origin = bipolarOrigin ?? 0;
  const lo = Math.min(origin, pos), hi = Math.max(origin, pos);
  const angle = at(pos) * 180 / Math.PI;
  const valueBox = [r[0] + Math.trunc((r[2] - boxW) / 2), r[1] + r[3] - boxH, boxW, boxH];
  return (
    <>
      <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
        <path d={arc(0, 1)} fill="none" stroke={rgba(S.ink, 0.16)} strokeWidth={2.6 * u} strokeLinecap="round" />
        <path d={arc(lo, Math.max(hi, lo + 0.0005))} fill="none" stroke={rgba(S.ink, 0.82)} strokeWidth={2.6 * u} strokeLinecap="round" />
        <circle cx={cx} cy={cy + 2 * u} r={rad} fill={rgba(S.shadow, 0.45)} />
        <circle cx={cx} cy={cy} r={rad} fill="#efe8d6" />
        <rect x={-1.5 * u} y={-rad * 0.86} width={3 * u} height={rad * 0.5} rx={1.5 * u} fill="#181815"
              transform={`translate(${cx} ${cy}) rotate(${angle})`} />
      </svg>
      <At r={valueBox} style={{ background: css(C.lcdGreen), borderRadius: 3, color: css(C.lcdText), ...font.value(Math.min(12, boxH * 0.7)),
                                display: 'flex', alignItems: 'center', justifyContent: 'center', whiteSpace: 'nowrap', overflow: 'hidden' }}>
        {text}
      </At>
    </>
  );
}

// --- the strip ---------------------------------------------------------------------------

function Source({ g }) {
  return (
    <>
      <StripPanel r={g.sourcePanel} name="SOURCE" />
      <Key r={g.arm} text="ARM" led={false} />
      <Menu r={g.bars} text="2 BARS" />
      <Key r={g.resample} text="RESAMPLE" led={false} />
      <Key r={g.load} text="LOAD" />
      <Switch r={g.monitor} text="MONITOR" on={false} />
    </>
  );
}

const MODULE_PAGES = [4, 5, 6, 7];
const SHORT = ['RES', 'FILTER', 'COLOR', 'SPACE'];

function Chain({ g, idle, openModule }) {
  const [x, y, w, h] = g.chainView;
  const gap = 12, bw = (w - gap * 3) / 4;
  return (
    <>
      <StripPanel r={g.chainPanel} name="CHAIN" />
      {[0, 1, 2, 3].map((m) => {
        const r = [x + m * (bw + gap), y, bw, h];
        const face = [r[0], r[1], r[2] - 2, r[3] - 2];
        const open = m === openModule;
        const onTop = open ? S.panel : S.ink;
        const inner = [face[0] + 8, face[1] + 5, face[2] - 16, face[3] - 10];
        const light = [inner[0] + 5.5 - 5.5, inner[1] + inner[3] * 0.25 - 5.5, 11, 11];
        const nameLeft = light[0] + 11 + 6;
        return (
          <React.Fragment key={m}>
            <At r={face} style={{ borderRadius: 5, background: open ? css(pageColour(MODULE_PAGES[m])) : rgba(S.ink, 0.07) }} />
            <At r={light} style={{ borderRadius: '50%', background: css(onTop) }} />
            <At r={[nameLeft, inner[1], inner[0] + inner[2] - nameLeft, inner[3] / 2]}
                style={{ ...font.screen(15), color: css(onTop), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap' }}>
              {SHORT[m]}
            </At>
            <At r={[inner[0], inner[1] + inner[3] / 2, inner[2], inner[3] / 2]}
                style={{ ...font.screen(11), color: open ? rgba(onTop, 0.8) : css(S.soft), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap' }}>
              {idle[m]}
            </At>
            {m < 3 && (
              <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
                <path d={`M${r[0] + r[2] + 2 + 8} ${r[1] + r[3] / 2} L${r[0] + r[2] + 3} ${r[1] + r[3] / 2 - 4} L${r[0] + r[2] + 3} ${r[1] + r[3] / 2 + 4} Z`}
                      fill={rgba(S.soft, 0.35)} />
              </svg>
            )}
          </React.Fragment>
        );
      })}
    </>
  );
}

// The readout at the strip's height: a title with the LIVE chip, and the figure.
function Readout({ r, title, figure }) {
  const inner = [r[0] + 10, r[1] + 6, r[2] - 20, r[3] - 12];
  const chipW = Math.ceil(measure('LIVE', `${font.screen(15).fontSize}px "B612 Mono"`)) + 10;
  return (
    <>
      <Surface r={r} colour={S.sunk} />
      <At r={[inner[0] + inner[2] - chipW, inner[1], chipW, 16]}
          style={{ background: css(S.onSunk), color: css(S.sunk), borderRadius: 3, ...font.screen(15), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        LIVE
      </At>
      <At r={[inner[0], inner[1], inner[2] - chipW - 8, 16]} style={{ ...font.screen(15), color: rgba(S.onSunk, 0.75), display: 'flex', alignItems: 'center' }}>
        {title}
      </At>
      <At r={[inner[0], inner[1] + 16, inner[2], inner[3] - 16]}
          style={{ ...font.screen(28), color: css(S.onSunk), display: 'flex', alignItems: 'center', whiteSpace: 'pre' }}>
        <Fitted width={inner[2]} cssFont={`${font.screen(28).fontSize}px "B612 Mono"`} text={figure} minScale={0.6} />
      </At>
    </>
  );
}

// --- the well ----------------------------------------------------------------------------

function Rail({ r, modTitles }) {
  const gap = 4, h = Math.trunc((r[3] - gap * 3) / 4);
  return [0, 1, 2, 3].map((m) => {
    const chip = [r[0], r[1] + m * (h + gap), r[2], h];
    const plate = [chip[0] + 1.5, chip[1] + 1.5, chip[2] - 3, chip[3] - 3];
    const area = [chip[0] + 5, chip[1] + 3, chip[2] - 10, chip[3] - 6];
    const topH = Math.min(14, Math.trunc(area[3] / 2));
    const swatch = [area[0] + 2, area[1] + topH / 2 - 7, 14, 14];
    const trace = [area[0] + 2 + 21, area[1] + topH + 1, area[2] - 4 - 21, area[3] - topH - 2];
    return (
      <React.Fragment key={m}>
        <At r={plate} style={{ borderRadius: 4, background: rgba(S.ink, m === 0 ? 0.16 : 0.05) }} />
        <At r={swatch} style={{ borderRadius: '50%', background: css(MOD[m].base), color: css(MOD[m].ink), ...font.mono(9.5),
                                display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          {m + 1}
        </At>
        <At r={[area[0] + 23, area[1], area[2] - 23, topH]} style={{ ...font.name(10, 0.24), color: css(S.ink), display: 'flex', alignItems: 'center' }}>
          {modTitles[m]}
        </At>
        <At r={[trace[0], Math.trunc(trace[1] + trace[3] / 2), trace[2], 1]} style={{ background: rgba(S.ink, 0.15) }} />
        <At r={[trace[0], trace[1] + trace[3] / 2 - 0.7, trace[2], 1.4]} style={{ background: css(MOD[m].base), borderRadius: 1 }} />
      </React.Fragment>
    );
  });
}

function Well({ g, page, entries, context, value, text, params }) {
  const colour = pageColour(page);
  const items = entries.filter((e) => (e.alwaysShown || e.page === page) && isShown(e, context));
  return (
    <>
      <Surface r={g.well} colour={S.panel} />
      <At r={[g.well[0], g.well[1], g.well[2], 3]} style={{ background: css(colour), borderRadius: '6px 6px 0 0' }} />
      <Rail r={g.rail} modTitles={['SINE', 'SINE', 'SINE', 'SINE']} />
      <At r={[g.slots[0].column[0], g.controlRow[1] + 8, 1, g.controlRow[3] - 16]} style={{ background: rgba(S.ink, 0.2) }} />
      {items.map((e, i) => {
        const slot = g.slots[e.slot];
        const dim = isDimmed(e, context) ? 0.35 : 1;
        if (e.kind === 'underButton' || e.kind === 'underMenu') {
          const r = centred(slot.under, e.width, 18);
          const rr = [r[0] + (e.dx ?? 0), r[1], r[2], r[3]];
          if (e.kind === 'underMenu') return <Menu key={i} r={rr} text={e.items[Math.round(value(e.id))] ?? ''} />;
          return <Key key={i} r={rr} text={e.text} on={value(e.id) > 0.5} size={10} />;
        }
        const label = (
          <At key={`l${i}`} r={slot.label} style={{ ...font.name(11.5), color: css(colour), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            <span style={{ position: 'relative' }}>
              {e.label}
              {RANDOM_EXCLUDED.has(e.id) && <span style={{ position: 'absolute', left: '100%', top: '50%', marginLeft: 2, transform: 'translateY(-50%)', display: 'flex' }}><Die /></span>}
            </span>
          </At>
        );
        const cell = slot.control;
        let control = null;
        if (e.kind === 'knob') {
          const p = params[e.id];
          const v = value(e.id);
          const pos = p.kind === 'choice' || p.kind === 'integer' ? (v - p.low) / (p.high - p.low) : to01(p, v);
          const bipolar = p.low < 0 && p.high > 0 ? to01(p, 0) : undefined;
          control = <Knob r={cell} pos={pos} bipolarOrigin={bipolar} text={text(e.id, v)} />;
        } else if (e.kind === 'menu') {
          control = <Menu r={centred(cell, cell[2], 26)} text={e.items[Math.round(value(e.id))] ?? ''} />;
        } else if (e.kind === 'button' || e.kind === 'action') {
          control = <Key r={centred(cell, cell[2] - 12, 34)} text={e.text} on={e.kind === 'button' && value(e.id) > 0.5} />;
        } else if (e.kind === 'banner') {
          control = <Banner r={centred(cell, cell[2], 56)} m={context.modShown} type={Math.round(value(`m${context.modShown + 1}Type`))} routes={0}
                            muted={value(`m${context.modShown + 1}Enable`) <= 0.5} />;
        }
        return <div key={i} style={{ opacity: dim }}>{label}{control}</div>;
      })}
    </>
  );
}

// The readout's standing figure, as SiltEditor::statusText: the input and its level, then the
// tempo. The browser's input is the microphone (MIC); none yet, the plug-in's own NO INPUT.
function statusFigure(source, levelDb, bpm) {
  const level = levelDb <= -60 ? '-INF' : String(roundToInt(levelDb));
  return `${source ?? 'NO INPUT'} ${level} DB   ${bpm.toFixed(1)} BPM`;
}

// One line that never gets cut: squeezed sideways to fit, down to minScale (drawFittedText).
function Fitted({ text, width, cssFont, minScale }) {
  const k = Math.max(minScale, Math.min(1, width / Math.max(1, measure(text, cssFont))));
  return <span style={{ display: 'inline-block', transform: `scaleX(${k})`, transformOrigin: '0 50%' }}>{text}</span>;
}

// Text width, as the plug-in measures it for placing the next thing.
const measureCanvas = document.createElement('canvas').getContext('2d');
function measure(text, cssFont) {
  measureCanvas.font = cssFont;
  return measureCanvas.measureText(text).width;
}

// JUCE's interpolatedWith: a share of the way from one colour to another.
function mix(a, b, t) {
  const ch = (shift) => Math.round(((a >> shift) & 255) + (((b >> shift) & 255) - ((a >> shift) & 255)) * t);
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Left out of RANDOM at first (SiltProcessor::randomExcluded): the modulator types.
const RANDOM_EXCLUDED = new Set(['m1Type', 'm2Type', 'm3Type', 'm4Type']);

// A small crossed-out die after a label: left out of RANDOM.
function Die() {
  return (
    <svg width="14" height="14" viewBox="-7 -7 14 14" style={{ overflow: 'visible' }}>
      <rect x="-4" y="-4" width="8" height="8" fill="none" stroke={css(C.inkSoft)} strokeWidth="1" />
      <rect x="-1" y="-1" width="2" height="2" fill={css(C.inkSoft)} />
      <line x1="-5.5" y1="5.5" x2="5.5" y2="-5.5" stroke={css(C.rust)} strokeWidth="1.3" />
    </svg>
  );
}

// MOD's banner: the shown modulator, tinted its colour (ModBanner::paint).
const TYPE_NAMES = ['WAVE', 'RANDOM', 'ENVELOPE', 'FOLLOW', 'MIDI', 'MACRO', 'GRAIN'];
function Banner({ r, m, type, routes, muted }) {
  const plate = [r[0] + 2, r[1] + 2, r[2] - 4, r[3] - 4];
  const text = [plate[0] + 6, plate[1] + 4, plate[2] - 12, plate[3] - 8];
  return (
    <>
      <At r={plate} style={{ borderRadius: 5, background: rgba(muted ? MOD[m].dim : MOD[m].base, 0.22) }} />
      <At r={[text[0], text[1], text[2], text[3] / 2]} style={{ ...font.mono(18), color: css(muted ? C.inkSoft : C.ink), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        MOD {m + 1}
      </At>
      <At r={[text[0], text[1] + text[3] / 2, text[2], text[3] / 2]} style={{ ...font.mono(10), color: css(C.inkSoft), display: 'flex', alignItems: 'center', justifyContent: 'center', whiteSpace: 'pre' }}>
        {`${TYPE_NAMES[type] ?? ''}  ${routes} ${routes === 1 ? 'ROUTE' : 'ROUTES'}${muted ? '  MUTED' : ''}`}
      </At>
    </>
  );
}

// A value's place along its knob (silt::params::to01, without snapping: display only).
function to01(p, v) {
  const proportion = Math.max(0, Math.min(1, (v - p.low) / (p.high - p.low)));
  if (p.mapping === 'cubic') return 0.5 * (Math.cbrt(Math.max(-50, Math.min(50, v)) / 50) + 1);
  if (p.mapping !== 'skewed') return proportion;
  const skew = Math.log(0.5) / Math.log((p.centre - p.low) / (p.high - p.low));
  return Math.pow(proportion, skew);
}

// --- the panel ---------------------------------------------------------------------------

function useScale() {
  const fit = () => Math.max(0.1, Math.min(window.innerWidth / 1200, window.innerHeight / 820));
  const [scale, setScale] = useState(fit);
  useEffect(() => {
    const on = () => setScale(fit());
    window.addEventListener('resize', on);
    return () => window.removeEventListener('resize', on);
  }, []);
  return scale;
}

function App() {
  const scale = useScale();
  const [data, setData] = useState(null);
  // ?page=N opens a page (for checking against the plug-in's snapshots until the keys work).
  const [page] = useState(() => Math.max(0, Math.min(9, Number(new URLSearchParams(location.search).get('page')) || 0)));
  const g = useMemo(() => panelGeometry(true), []);

  useEffect(() => {
    (async () => {
      const get = (name) => fetch(`data/${name}.json`).then((r) => r.json());
      const [params, layout, help, presets] = await Promise.all(['params', 'layout', 'help', 'presets'].map(get));
      // The plug-in's faces, loaded before the first draw so nothing reflows.
      const faces = [['B612 Mono', 'B612Mono-Regular.ttf', {}], ['Overpass', 'Overpass-Medium.ttf', { weight: '500' }],
                     ['Chivo Mono', 'ChivoMono-Regular.ttf', {}], ['Glyph Grotesque', 'GlyphGrotesque-Regular.ttf', {}]];
      await Promise.all(faces.map(async ([family, file, descriptors]) => document.fonts.add(await new FontFace(family, `url(fonts/${file})`, descriptors).load())));
      const silt = await loadSilt(await fetch('silt.wasm').then((r) => r.arrayBuffer()));
      silt.create(true);
      setData({ params, layout, help, presets, silt });
    })().catch((err) => console.error(err));
  }, []);

  if (!data) return <div className="frame" style={{ width: 1200 * scale, height: 820 * scale }} />;

  const byId = Object.fromEntries(data.params.map((p) => [p.id, p]));
  const values = Object.fromEntries(data.params.map((p) => [p.id, data.silt.getParam(p.index)]));
  const value = (id) => values[id] ?? 0;
  const text = (id, v) => data.silt.text(byId[id].index, v);
  const context = { value, modShown: 0, resonatorHalf: 0, delayForced: false };
  const idle = ['MIX 0', 'OPEN', 'NEUTRAL', 'LEVELS 0'];
  const wordWidth = Math.ceil(measure('SILT', `${font.wordmark(48).fontSize}px "Glyph Grotesque"`)) + 4;

  return (
    <div className="frame" style={{ width: 1200 * scale, height: 820 * scale }}>
      <div className="stage" style={{ width: 1200, height: 820, transform: `scale(${scale})`, background: css(S.page) }}>
        {/* Masthead */}
        <At r={[g.header[0], g.header[1] + g.header[3] - 2, g.header[2], 2]} style={{ background: css(S.ink) }} />
        <At r={[g.header[0], g.header[1], 200, g.header[3] - 10]} style={{ ...font.wordmark(48), color: css(S.ink), display: 'flex', alignItems: 'center' }}>
          SILT
        </At>
        <At r={[g.header[0] + wordWidth + 14, g.header[1] + 14, 400, 34]} style={{ ...font.name(15.5, 0.06), color: css(S.soft), lineHeight: '17px' }}>
          GLYPH AUDIO<br />GRANULAR SAMPLER
        </At>
        <Key r={[g.presets[0], g.presets[1], 34, 36]} text="◀" size={9} />
        <At r={[g.presets[0] + 33, g.presets[1] + 1, 266, 32]} style={{ borderRadius: 4, background: rgba(S.ink, 0.08), ...font.name(12.5), color: css(S.ink),
                                                                     display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          INIT
        </At>
        <Key r={[g.presets[0] + 302, g.presets[1], 32, 36]} text="▶" size={9} />
        <Key r={[g.presets[0] + 342, g.presets[1], 58, 36]} text="SAVE" />
        <Key r={g.tour} text="?" />

        {/* Strip */}
        <Source g={g} />
        <Chain g={g} idle={idle} openModule={-1} />
        <Readout r={g.lcd} title="SILT" figure={statusFigure(null, -100, 120)} />

        {/* Screen (step 6 draws what's on it) */}
        <Surface r={g.screen} colour={C.screenBlue} />
        <At r={[g.screen[0] + 26, g.screen[1] + 18, 400, 20]} style={{ ...font.screen(17), color: css(C.screenPale) }}>RAIN-BELLS</At>

        {/* Key row */}
        {[0, 1, 2, 3, 8].map((p) => (
          <React.Fragment key={p}>
            <Key r={g.pageKeys[p]} text={PAGE_NAMES[p]} on={p === page} tint={p === page ? mix(pageColour(p), S.panel, 0.25) : pageColour(p)} flat />
            {p === page && (
              <At r={[g.pageKeys[p][0] + 1, g.pageKeys[p][1] + g.pageKeys[p][3] - 4, g.pageKeys[p][2] - 2, g.well[1] - (g.pageKeys[p][1] + g.pageKeys[p][3]) + 5]}
                  style={{ background: css(pageColour(p)) }} />
            )}
          </React.Fragment>
        ))}
        <Key r={g.learn} text="LEARN" led={false} />
        <Key r={g.random} text="RANDOM" />
        <At r={g.amount} style={{ background: css(C.lcdGreen), borderRadius: 3, color: css(C.lcdText), ...font.value(11.5), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>50%</At>
        <Key r={g.routes} text="MOD ROUTES" />
        <Key r={g.undo} text="UNDO" />
        <Key r={g.redo} text="REDO" disabled />

        {/* Well */}
        <Well g={g} page={page} entries={data.layout.instrument} context={context} value={value} text={text} params={byId} />

        {/* The browser's row, where the plug-in has the Bench */}
        <Surface r={g.bench} colour={S.panel} />
      </div>
    </div>
  );
}

createRoot(document.getElementById('root')).render(<App />);
