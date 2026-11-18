// Silt in the browser: the plug-in's panel at its own 1200 × 820, scaled to the
// window, drawn from the plug-in's exported data (params, layout, help, presets)
// and its own value text (the WebAssembly build).
import React, { createContext, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { panelGeometry, centred, box, SPINE } from './geometry.js';
import { C, S, MOD, css, rgba, pageColour, font } from './theme.js';
import { isShown, isDimmed, roundToInt, helpFor } from './rules.js';
import { loadSilt } from '../engine/silt-wasm.js';
import { Audio, SAMPLES } from './audio.js';
import { Screen, makeOverview } from './screen.jsx';

const PAGE_NAMES = ['GRAIN', 'SHAPE', 'PITCH', 'PLAY', 'RESONATOR', 'FILTER', 'COLOR', 'SPACE', 'MOD', 'CHAIN'];

// What the parts can do: set a parameter, open a menu, start typing a value… (App provides it.)
const UI = createContext(null);

// --- small pieces ----------------------------------------------------------------------

function At({ r, style, children, className, ...rest }) {
  return <div className={className ? `at ${className}` : 'at'} style={{ ...box(r), ...style }} {...rest}>{children}</div>;
}

// A key: a filled rounded plate with no frame (PanelLook::drawButtonBackground).
// Its face is the bounds less 2 on the right and bottom, inset 1.
function Key({ r, text, on = false, tint = S.ink, led, accent, disabled, size, flat, style, onClick, help, onPointerDown, onMenu }) {
  const filled = on || accent;
  const plate = flat ? [r[0] + 1, r[1] + 1, r[2] - 2, r[3] - 2] : [r[0] + 1, r[1] + 1, r[2] - 4, r[3] - 4];
  const fontSize = size ?? Math.max(10, Math.min(12.5, r[3] * 0.42));
  return (
    <div className={`key${filled ? ' filled' : ''}${disabled ? ' disabled' : ''}`} data-help={help} onClick={disabled ? undefined : onClick} onPointerDown={onPointerDown}
         onContextMenu={onMenu ? (e) => { e.preventDefault(); onMenu(); } : undefined}>
      <At r={plate} className="plate" style={{ borderRadius: 4, background: filled ? css(accent ? S.accent : tint) : rgba(S.ink, disabled ? 0.04 : 0.08) }} />
      {led !== undefined && (
        <div className="at" style={{ left: plate[0] + 10 - 3.5, top: plate[1] + plate[3] / 2 - 3.5, width: 7, height: 7, borderRadius: 4,
                                      background: led ? css(S.lit) : rgba(S.ink, 0.25) }} />
      )}
      <At r={[r[0], r[1], r[2] - 2, r[3] - 2]} style={{ ...font.name(fontSize), color: filled ? css(S.panel) : rgba(S.ink, disabled ? 0.35 : 1),
                                                        display: 'flex', alignItems: 'center', justifyContent: 'center',
                                                        paddingLeft: led !== undefined ? 14 : 0, paddingTop: 2, whiteSpace: 'pre', ...style }}>
        <Fitted text={text} style={font.name(fontSize)} width={r[2] - 2 - 6 - (led !== undefined ? 14 : 0)} />
      </At>
    </div>
  );
}

// The preset bar (kit::PresetBar): previous, the name (click: the browser), next, SAVE.
function PresetBar({ r, name, onStep, onBrowse, onSave }) {
  const save = [r[0] + r[2] - 58, r[1], 58, r[3]];
  const rest = [r[0], r[1], r[2] - 66, r[3]];
  const previous = [rest[0], r[1], 32, r[3]], next = [rest[0] + rest[2] - 32, r[1], 32, r[3]];
  const field = [previous[0] + 32, r[1], rest[2] - 64, r[3]];
  const arrow = (k, dir) => {
    const cx = k[0] + (k[2] - 2) / 2, cy = k[1] + (k[3] - 2) / 2;
    return (
      <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
        <path d={`M${cx + 3.5 * dir} ${cy} L${cx - 3 * dir} ${cy - 5} L${cx - 3 * dir} ${cy + 5} Z`} fill={css(S.ink)} />
      </svg>
    );
  };
  return (
    <>
      <Key r={previous} text="" onClick={() => onStep(-1)} />{arrow(previous, -1)}
      <Key r={field} text={name} onClick={onBrowse} />
      <Key r={next} text="" onClick={() => onStep(1)} />{arrow(next, 1)}
      <Key r={save} text="SAVE" onClick={onSave} />
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
function Menu({ r, text, items, selected, onPick, help }) {
  const ui = useContext(UI);
  const plate = [r[0] + 1, r[1] + 1, r[2] - 4, r[3] - 4];
  const fontSize = Math.max(10, Math.min(12.5, r[3] * 0.42));
  const cx = plate[0] + plate[2] - plate[3] / 2, cy = plate[1] + plate[3] / 2;
  const open = () => items && ui.openMenu({ r, items, selected, onPick });
  return (
    <div className="key" data-help={help} onClick={open}>
      <At r={plate} className="plate" style={{ borderRadius: 4, background: rgba(S.ink, 0.08) }} />
      <At r={[plate[0] + 8, plate[1], plate[2] - plate[3] - 8, plate[3]]}
          style={{ ...font.name(fontSize), color: css(S.ink), display: 'flex', alignItems: 'center', whiteSpace: 'pre', paddingTop: 2 }}>
        <Fitted text={text} style={font.name(fontSize)} width={plate[2] - plate[3] - 8} />
      </At>
      <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
        <path d={`M${cx - 3.5} ${cy - 1.5} L${cx} ${cy + 2} L${cx + 3.5} ${cy - 1.5}`} fill="none" stroke={rgba(S.ink, 0.8)}
              strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    </div>
  );
}

// The pop-up a menu opens (JUCE's PopupMenu in the kit's colours): the panel, the
// items in the name face, the one under the mouse in ink, a tick on what's chosen.
// Items are names (a menu's choices: `selected` ticked, `onPick` with the index), or
// { text, ticked, header, separator, disabled, pick, items } for a fuller menu.
function Popup({ menu, close }) {
  return (
    <div className="popup-shield" onPointerDown={close} onContextMenu={(e) => e.preventDefault()}>
      <PopupList menu={menu} close={close} />
    </div>
  );
}

function PopupList({ menu, close }) {
  const itemH = 24, sepH = 9;
  const items = menu.items.map((it, i) => (typeof it === 'string' ? { text: it, ticked: i === menu.selected, pick: () => menu.onPick(i) } : it));
  const width = Math.max(menu.r[2], 140, ...items.map((it) => (it.text ? measure(it.text, `500 ${font.name(13).fontSize}px "Overpass"`) + 44 : 0)));
  const height = items.reduce((h, it) => h + (it.separator ? sepH : itemH), 8);
  const top = menu.r[1] + menu.r[3];
  const left = Math.max(4, Math.min(menu.r[0], 1200 - width - 4));
  const y = top + height <= 816 ? top : Math.max(4, Math.min(menu.r[1], 816) - height);
  const [sub, setSub] = useState(null);   // { index, y }
  let at = y + 4;
  return (
    <>
      <div className="popup" style={{ left, top: y, width, background: css(S.panel), color: css(S.ink), ...font.name(13) }}
           onPointerDown={(e) => e.stopPropagation()}>
        {items.map((it, i) => {
          const rowY = at; at += it.separator ? sepH : itemH;
          if (it.separator) return <div key={i} className="popup-sep" />;
          if (it.header) return <div key={i} className="popup-header" style={{ height: itemH, color: css(S.soft) }}>{it.text}</div>;
          return (
            <div key={i} className={`popup-item${it.disabled ? ' disabled' : ''}`} style={{ height: itemH }}
                 onPointerEnter={() => setSub(it.items && !it.disabled ? { index: i, y: rowY } : null)}
                 onClick={() => { if (it.disabled || it.items) return; it.pick?.(); close(); }}>
              <span className="tick">{it.ticked ? '✓' : ''}</span>{it.text}{it.items && <span className="arrow">{'▸'}</span>}
            </div>
          );
        })}
      </div>
      {sub && <PopupList key={sub.index} menu={{ r: [left + width - 2, sub.y - 4, 0, 0], items: items[sub.index].items }} close={close} />}
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

function Knob({ r, pos, bipolarOrigin, text, id, help }) {
  const ui = useContext(UI);
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
  const editing = ui.editing === id;
  // The rings (SiltEditor::drawModulation): one per live route, 3 px apart outside the arc.
  const ringBase = side / 2 - 4 * u + 2;
  const target = ui.isTarget(id);
  const live = target ? [0, 1, 2, 3].filter((m) => ui.routeAmount(m, id) !== 0 && ui.modulatorOn(m)) : [];
  const hint = target && ui.armed >= 0 && ui.routeAmount(ui.armed, id) === 0 ? ui.armed : -1;
  const dropHover = target && ui.dropHover?.id === id && ui.routeAmount(ui.dropHover.m, id) === 0 ? ui.dropHover.m : -1;
  const ringAt = (dx, dy) => {
    const rings = live.length + (hint >= 0 ? 1 : 0);
    if (!target || rings === 0) return -1;
    const d = Math.hypot(dx, dy);
    if (d < ringBase - 4 || d > ringBase + 3 * (rings - 1) + 5) return -1;
    const ring = Math.max(0, Math.min(rings - 1, roundToInt((d - ringBase) / 3)));
    return ring < live.length ? live[ring] : hint;
  };
  const localPoint = (e) => {
    const box = e.currentTarget.getBoundingClientRect();
    return [(e.clientX - box.left) / ui.scale - rot[2] / 2, (e.clientY - box.top) / ui.scale - rot[3] / 2];
  };
  // A depth drag: 150 px from no depth to full, ten times finer with Shift (held now).
  const depthDrag = (e, m) => {
    ui.beginRouteGesture();
    ui.beginStep('MODULATION');
    let running = ui.routeAmount(m, id), last = 0;
    const startY = e.clientY;
    ui.depthDragged(m, id, true);
    const move = (mv) => {
      const pixelsUp = (startY - mv.clientY) / ui.scale;
      running = Math.max(-1, Math.min(1, running + (pixelsUp - last) / (mv.shiftKey ? 1000 : 150)));
      last = pixelsUp;
      ui.setAmount(m, id, Math.abs(running) < 0.02 ? 0 : running);
      ui.depthDragged(m, id, true);
    };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); ui.depthDragged(m, id, false); ui.endStep(); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  // Up or right raises it: 250 px end to end, ten times finer with Shift (held now).
  const down = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const [dx, dy] = localPoint(e);
    const onRing = ringAt(dx, dy);
    if (onRing >= 0) {
      if (e.shiftKey && e.altKey) ui.togglePolarity(onRing, id);
      else depthDrag(e, onRing);
      return;
    }
    if (e.altKey && target) { depthDrag(e, ui.depthModulator(id)); return; }
    let last = [e.clientX, e.clientY], running = ui.to01(id);
    const move = (m) => {
      const pixels = ((last[1] - m.clientY) + (m.clientX - last[0])) / ui.scale;
      last = [m.clientX, m.clientY];
      running = Math.max(0, Math.min(1, running + pixels / (m.shiftKey ? 2500 : 250)));
      ui.setFrom01(id, running);
    };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); ui.endGesture(id); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    ui.beginGesture(id);
  };
  return (
    <div data-help={help}>
      <At r={rot} className="hit knob-hit" data-knob={id} tabIndex={0} onPointerDown={down}
          onKeyDown={(e) => { if (target && (e.key === 'Delete' || e.key === 'Backspace')) ui.clearTarget(id); }}
          onDoubleClick={(e) => {
            const [dx, dy] = localPoint(e);
            const m = ringAt(dx, dy);
            if (m >= 0 && m !== hint) { ui.beginRouteGesture(); ui.setAmount(m, id, 0); return; }   // off this knob
            if (e.altKey && target) { ui.beginRouteGesture(); ui.setAmount(ui.depthModulator(id), id, 0); return; }
            ui.resetToDefault(id);
          }} />
      <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
        <path d={arc(0, 1)} fill="none" stroke={rgba(S.ink, 0.16)} strokeWidth={2.6 * u} strokeLinecap="round" />
        <path d={arc(lo, Math.max(hi, lo + 0.0005))} fill="none" stroke={rgba(S.ink, 0.82)} strokeWidth={2.6 * u} strokeLinecap="round" />
        <circle cx={cx} cy={cy + 2 * u} r={rad} fill={rgba(S.shadow, 0.45)} />
        <circle cx={cx} cy={cy} r={rad} fill="#efe8d6" />
        <rect x={-1.5 * u} y={-rad * 0.86} width={3 * u} height={rad * 0.5} rx={1.5 * u} fill="#181815"
              transform={`translate(${cx} ${cy}) rotate(${angle})`} />
        {target && <Rings cx={cx} cy={cy} base={ringBase} pos={pos} id={id} live={live} hint={dropHover >= 0 ? -1 : hint} dropHover={dropHover} />}
      </svg>
      <At r={valueBox} onClick={() => ui.setEditing(id)}
          style={{ background: css(C.lcdGreen), borderRadius: 3, color: css(C.lcdText), ...font.value(Math.min(12, boxH * 0.7)),
                   display: 'flex', alignItems: 'center', justifyContent: 'center', whiteSpace: 'pre', cursor: 'text' }}>
        {editing ? <TypedValue id={id} text={text} style={font.value(Math.min(12, boxH * 0.7))} />
                 : <Fitted text={text} style={font.value(Math.min(12, boxH * 0.7))} width={boxW - 4} />}
      </At>
    </div>
  );
}

// A knob's modulation (SiltEditor::drawModulation): per live route a ring, solid the way the
// value moves first with an arrow and the modulator's number at its end, faint the other way
// for a bipolar route, a stop at a clamped end; an armed modulator's hint ring; a held chip's
// preview; and the dot where modulation has the value now.
function Rings({ cx, cy, base, pos, id, live, hint, dropHover }) {
  const ui = useContext(UI);
  const angle = (v) => (-135 + v * 270) * Math.PI / 180;
  const pt = (r, a) => [cx + r * Math.sin(a), cy - r * Math.cos(a)];
  const stepped = ui.isStepped(id);
  const quantise = (v) => (stepped ? ui.to01Of(id, Math.round(ui.from01Of(id, v))) : v);
  const clamp = (v) => Math.max(0, Math.min(1, v));
  const arcPath = (r, lo, hi) => {
    if (hi - lo < 1e-4) hi = lo + 1e-4;
    const [x0, y0] = pt(r, angle(lo)), [x1, y1] = pt(r, angle(hi));
    return `M${x0} ${y0} A${r} ${r} 0 ${(hi - lo) * 270 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };
  const dash = (m) => ({ 1: '7 3', 2: '3 2.5', 3: '1.2 2.2' }[m]);
  const out = [];
  live.forEach((m, ring) => {
    const r = base + 3 * ring, colour = MOD[m].base;
    const amount = ui.routeAmount(m, id), bipolar = ui.routeBipolar(m, id);
    const rawFirst = pos + amount, rawOther = bipolar ? pos - amount : pos;
    const rawLo = Math.min(rawFirst, rawOther), rawHi = Math.max(rawFirst, rawOther);
    const lo = quantise(clamp(rawLo)), hi = quantise(clamp(rawHi));
    const first = quantise(clamp(rawFirst)), other = quantise(clamp(rawOther));
    const alpha = ui.armed === m ? 1 : 0.85;
    if (bipolar) out.push(<path key={`o${m}`} d={arcPath(r, Math.min(pos, other), Math.max(pos, other))} fill="none" stroke={rgba(colour, alpha * 0.35)} strokeWidth="2.2" strokeDasharray={dash(m)} />);
    out.push(<path key={`f${m}`} d={arcPath(r, Math.min(pos, first), Math.max(pos, first))} fill="none" stroke={rgba(colour, alpha)} strokeWidth="2.2" strokeDasharray={dash(m)} />);
    const at = angle(first), onward = (amount >= 0 ? -1 : 1) * 0.13;
    const tri = [pt(r - 3.2, at), pt(r + 3.2, at), pt(r, at + onward)];
    out.push(<path key={`a${m}`} d={`M${tri[0]} L${tri[1]} L${tri[2]} Z`} fill={rgba(colour, alpha)} />);
    for (const [raw, end] of [[rawLo, lo], [rawHi, hi]])
      if (raw < 0 || raw > 1) {
        const [a0, a1] = [pt(r - 3, angle(end)), pt(r + 3, angle(end))];
        out.push(<line key={`s${m}${raw}`} x1={a0[0]} y1={a0[1]} x2={a1[0]} y2={a1[1]} stroke={css(colour)} strokeWidth="1.6" />);
      }
    const [nx, ny] = pt(r + 6, angle(first));
    out.push(<text key={`n${m}`} x={nx} y={ny} fill={css(darker(colour, 0.3))} textAnchor="middle" dominantBaseline="central"
                   style={{ ...font.mono(8), fontFamily: 'B612 Mono' }}>{m + 1}</text>);
  });
  const next = base + 3 * live.length;
  if (dropHover >= 0)
    out.push(<path key="drop" d={arcPath(next, pos, clamp(pos + 0.25))} fill="none" stroke={rgba(MOD[dropHover].base, 0.6)} strokeWidth="2.2" strokeDasharray={dash(dropHover)} />);
  else if (hint >= 0)
    out.push(<path key="hint" d={arcPath(next, 0, 1)} fill="none" stroke={rgba(MOD[hint].base, 0.8)} strokeWidth="1.8" strokeDasharray="3 2.5" />);
  if (live.length > 0) {
    const [dx, dy] = pt(base, angle(ui.modulated01(id)));
    out.push(<circle key="dot" cx={dx} cy={dy} r="2.5" fill={css(C.bone)} stroke={css(S.ink)} strokeWidth="1" />);
  }
  return out;
}

// JUCE's Colour::darker for the ring numbers.
function darker(rgb, amount) {
  const k = 1 / (1 + amount);
  const ch = (shift) => Math.trunc(((rgb >> shift) & 255) * k);
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Typing a value into a knob's box: the same window, the same type (PanelLook::ValueBox).
// Enter or leaving the box takes it; Escape doesn't.
function TypedValue({ id, text, style }) {
  const ui = useContext(UI);
  const ref = useRef(null);
  useEffect(() => { ref.current?.focus(); ref.current?.select(); }, []);
  const done = (take) => { if (take) ui.typeValue(id, ref.current.value); ui.setEditing(null); };
  return (
    <input ref={ref} className="typed" defaultValue={text} style={{ ...style, color: css(C.lcdText) }}
           onBlur={() => done(true)} onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') { e.preventDefault(); done(false); } }} />
  );
}

// --- the strip ---------------------------------------------------------------------------

function Source({ g, onLoad, monitor, onMonitor, onArm, capturing, bars, onBars, resample, onResample }) {
  return (
    <>
      <StripPanel r={g.sourcePanel} name="SOURCE" />
      <Key r={g.arm} text="ARM" led={false} accent={capturing} help="!arm" onClick={onArm} />
      <Menu r={g.bars} text={BAR_CHOICES[bars]} items={BAR_CHOICES} selected={bars} onPick={onBars} help="!bars" />
      <Key r={g.resample} text="RESAMPLE" led={resample} help="!resample" onClick={onResample} />
      <Key r={g.load} text="LOAD" help="!load" onClick={onLoad} />
      <div data-help="!monitor" onClick={onMonitor} style={{ cursor: 'pointer' }}><Switch r={g.monitor} text="MONITOR" on={monitor} /></div>
    </>
  );
}

const MODULE_PAGES = [4, 5, 6, 7];
// The scope taps the module of RESONATOR, FILTER and COLOR's pages (SiltEditor::updateScope).
const SCOPE_MODULE = { 4: 0, 5: 1, 6: 2 };
const MODULE_NAMES = ['RESONATOR', 'FILTER', 'COLOR', 'SPACE'];
const BAR_CHOICES = ['1 BAR', '2 BARS', '4 BARS', '8 BARS'];
const SHORT = ['RES', 'FILTER', 'COLOR', 'SPACE'];

// The 24 chain orders, in permutation order (Instrument::chainOrder): 0 is RES, FILTER, COLOR, SPACE.
const CHAIN_ORDERS = (() => {
  const out = [], order = [0, 1, 2, 3];
  for (let i = 0; i < 24; i++) {
    out.push([...order]);
    let k = 2; while (k >= 0 && order[k] >= order[k + 1]) k--;          // std::next_permutation
    if (k < 0) { order.reverse(); continue; }
    let l = 3; while (order[l] <= order[k]) l--;
    [order[k], order[l]] = [order[l], order[k]];
    order.splice(k + 1, 3 - k, ...order.slice(k + 1).reverse());
  }
  return out;
})();
const ON_IDS = ['resOn', 'filterOn', 'colorOn', 'spaceOn'];

// Whether a module would change the sound, and if not why (ChainView::moduleActive, idleReason).
function moduleState(m, v) {
  if (v(ON_IDS[m]) <= 0.5) return { on: false, active: false, reason: 'OFF' };
  let active;
  if (m === 0) active = v('resMix') > 0;
  else if (m === 1) {
    const mix = v('filterMix') * 0.01, reso = v('filterReso') * 0.01, drive = v('filterDrive') * 0.01, env = v('filterEnv') * 0.01, key = v('filterKey') * 0.01;
    const still = reso <= 0 && drive <= 0 && Math.abs(env) < 0.001 && Math.abs(key) < 0.001;
    const open = mix <= 0 || (still && ((v('filterMorph') <= 0.001 && v('filterCutoff') >= 19900) || (v('filterMorph') >= 1.999 && v('filterCutoff') <= 21)));
    active = !open;
  } else if (m === 2) {
    active = v('colorMix') > 0 && (v('colorDrive') > 0 || v('colorBits') < 15.99 || v('colorRate') < 40000 * 0.999 || v('colorComp') > 0
                                   || v('colorNoise') > 0 || v('colorWow') > 0 || Math.abs(v('colorTone')) > 0.05);
  } else active = v('spaceDelay') > 0 || v('spaceReverb') > 0 || v('spaceFreeze') > 0.5;
  const reason = active ? '' : m === 0 ? 'MIX 0' : m === 1 ? (v('filterMix') <= 0 ? 'MIX 0' : 'OPEN') : m === 2 ? (v('colorMix') <= 0 ? 'MIX 0' : 'NEUTRAL') : 'LEVELS 0';
  return { on: true, active, reason };
}

function Chain({ g, value, openModule, half, chainOpen }) {
  const ui = useContext(UI);
  const [x, y, w, h] = g.chainView;
  const gap = 12, bw = (w - gap * 3) / 4;
  const order = CHAIN_ORDERS[Math.max(0, Math.min(23, Math.round(value('chainOrder'))))];
  const [drag, setDrag] = useState(null);   // { from, x }
  const blockAt = (p) => [x + p * (bw + gap), y, bw, h];
  const positionAt = (px) => {
    let best = 0;
    for (let p = 1; p < 4; p++) if (Math.abs(px - (blockAt(p)[0] + bw / 2)) < Math.abs(px - (blockAt(best)[0] + bw / 2))) best = p;
    return best;
  };
  // The order as it would be with the dragged block dropped where it is now.
  let shown = order;
  if (drag?.moved) {
    const rest = order.filter((_, p) => p !== drag.from);
    rest.splice(positionAt(drag.x + bw / 2), 0, order[drag.from]);
    shown = rest;
  }

  const down = (e, p) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const start = e.clientX, offset = (e.clientX - ui.stageLeft()) / ui.scale - blockAt(p)[0];
    let state = { from: p, x: blockAt(p)[0], moved: false };
    const move = (m) => {
      state = { ...state, moved: state.moved || Math.abs(m.clientX - start) / ui.scale > 4,
                x: Math.max(blockAt(0)[0], Math.min(blockAt(3)[0], (m.clientX - ui.stageLeft()) / ui.scale - offset)) };
      setDrag(state);
    };
    const up = () => {
      window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up);
      setDrag(null);
      if (!state.moved) { ui.openModule(order[p]); return; }
      const to = positionAt(state.x + bw / 2);
      if (to === p) return;
      const next = order.filter((_, i) => i !== p);
      next.splice(to, 0, order[p]);
      ui.setParam('chainOrder', CHAIN_ORDERS.findIndex((o) => o.every((v, i) => v === next[i])));
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  const block = (r, m, lifted, p) => {
    const face = [r[0], r[1], r[2] - 2, r[3] - 2];
    const open = m === openModule;
    const st = moduleState(m, value);
    const onTop = open ? S.panel : S.ink;
    const inner = [face[0] + 8, face[1] + 5, face[2] - 16, face[3] - 10];
    const light = [inner[0] + 5.5 - 5.5, inner[1] + inner[3] * 0.25 - 5.5, 11, 11];
    const nameLeft = light[0] + 11 + 6;
    return (
      <div key={`b${m}`} data-help="!chain" className="chain-block" onContextMenu={(e) => { e.preventDefault(); ui.resetMenu(MODULE_PAGES[m], r); }}>
        <At r={face} className="hit" onPointerDown={p === undefined ? undefined : (e) => down(e, p)}
            style={{ borderRadius: 5, cursor: 'grab', background: open ? css(pageColour(MODULE_PAGES[m])) : rgba(S.ink, lifted ? 0.2 : st.active ? 0.12 : 0.07) }} />
        <At r={light} style={st.on ? { borderRadius: '50%', background: css(onTop) } : { borderRadius: '50%', border: `1.2px solid ${rgba(onTop, open ? 0.7 : 0.45)}` }} />
        {p !== undefined && <At r={[light[0] - 7, light[1] - 7, 25, 25]} className="hit" onClick={() => ui.setParam(ON_IDS[m], st.on ? 0 : 1)} style={{ cursor: 'pointer' }} />}
        <At r={[nameLeft, inner[1], inner[0] + inner[2] - nameLeft, inner[3] / 2]}
            style={{ ...font.screen(15), color: st.on || open ? css(onTop) : css(S.soft), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap', pointerEvents: 'none' }}>
          {SHORT[m]}{open && m === 0 ? ` ${half + 1}/4` : ''}
        </At>
        {!st.active && (
          <At r={[inner[0], inner[1] + inner[3] / 2, inner[2], inner[3] / 2]}
              style={{ ...font.screen(11), color: open ? rgba(onTop, 0.8) : css(S.soft), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap', pointerEvents: 'none' }}>
            {st.reason}
          </At>
        )}
      </div>
    );
  };

  return (
    <>
      <StripPanel r={g.chainPanel} name="CHAIN" />
      <At r={[g.chainPanel[0], g.chainPanel[1], SPINE, g.chainPanel[3]]} className="hit" data-help="!chain" onClick={() => ui.setPage(9)} style={{ cursor: 'pointer' }} />
      {chainOpen && (
        // CHAIN has no key: open, the strip's own name shows as the held key.
        <>
          <At r={[g.chainPanel[0] + 1, g.chainPanel[1] + 1, SPINE - 2, g.chainPanel[3] - 2]} style={{ background: css(S.ink), borderRadius: 3, pointerEvents: 'none' }} />
          <div className="at spine" style={{ left: g.chainPanel[0] + 1, top: g.chainPanel[1] + 1, width: g.chainPanel[3] - 2, height: SPINE - 2, pointerEvents: 'none',
                                              transform: `translateY(${g.chainPanel[3] - 2}px) rotate(-90deg)`, transformOrigin: '0 0',
                                              ...font.screen(15), color: css(S.panel) }}>CHAIN</div>
        </>
      )}
      {[0, 1, 2].map((p) => {
        const r = blockAt(p);
        return (
          <svg key={`a${p}`} className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
            <path d={`M${r[0] + r[2] + 2 + 8} ${r[1] + r[3] / 2} L${r[0] + r[2] + 3} ${r[1] + r[3] / 2 - 4} L${r[0] + r[2] + 3} ${r[1] + r[3] / 2 + 4} Z`}
                  fill={rgba(S.soft, 0.35)} />
          </svg>
        );
      })}
      {shown.map((m, p) => (drag?.moved && m === order[drag.from]
        ? <At key={`g${m}`} r={[blockAt(p)[0], y, bw - 2, h - 2]} style={{ borderRadius: 5, background: rgba(S.ink, 0.1) }} />
        : block(blockAt(p), m, false, drag?.moved ? undefined : p)))}
      {drag?.moved && block([drag.x, y, bw, h], order[drag.from], true, undefined)}
    </>
  );
}

// The readout at the strip's height: a title with the LIVE chip, and the figure.
function Readout({ r, title, status }) {
  const inner = [r[0] + 10, r[1] + 6, r[2] - 20, r[3] - 12];
  // Long messages (hover help) take the whole window: up to three lines, squeezed, never cut.
  if (status.length > 26) {
    const box = [r[0] + 10, r[1] + 5, r[2] - 20, r[3] - 10];
    return (
      <>
        <Surface r={r} colour={S.sunk} />
        <At r={box} style={{ color: css(S.onSunk), display: 'flex', alignItems: 'center' }}>
          <WrappedFit text={status} width={box[2]} height={box[3]} style={font.screen(17)} lines={3} minScale={0.8} />
        </At>
      </>
    );
  }
  const figure = status;
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
        <Fitted width={inner[2]} cssFont={`${font.screen(28).fontSize}px "B612 Mono"`} text={figure} minScale={0.6} origin="0 50%" />
      </At>
    </>
  );
}

// --- the well ----------------------------------------------------------------------------

function Rail({ r, modTitles, shown, armed, routeCounts, traces, bipolar }) {
  const ui = useContext(UI);
  const [ghost, setGhost] = useState(null);   // { m, x, y }: a chip in hand
  // Press: a click shows (and arms) it; dragged 5 px, it's in hand, and dropped on a knob
  // it makes a route there (ModChip).
  const down = (e, m) => {
    if (e.button !== 0) return;
    const start = [e.clientX, e.clientY], shift = e.shiftKey;
    let dragging = false, over = null;
    const knobUnder = (x, y) => document.elementsFromPoint(x, y).find((el) => el.dataset?.knob)?.dataset.knob ?? null;
    const move = (mv) => {
      if (!dragging && Math.hypot(mv.clientX - start[0], mv.clientY - start[1]) < 5) return;
      if (!dragging && armed >= 0 && armed !== m) ui.selectModulator(m, true);   // this one takes over
      dragging = true;
      const left = ui.stageLeft(), top = ui.stageTop();
      setGhost({ m, x: (mv.clientX - left) / ui.scale, y: (mv.clientY - top) / ui.scale });
      over = knobUnder(mv.clientX, mv.clientY);
      ui.setDropHover(over ? { m, id: over } : null);
    };
    const up = () => {
      window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up);
      setGhost(null);
      ui.setDropHover(null);
      if (!dragging) ui.selectModulator(m, !shift);
      else if (over) ui.dropChip(m, over);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  const gap = 4, h = Math.trunc((r[3] - gap * 3) / 4);
  const chips = [0, 1, 2, 3].map((m) => {
    const chip = [r[0], r[1] + m * (h + gap), r[2], h];
    const plate = [chip[0] + 1.5, chip[1] + 1.5, chip[2] - 3, chip[3] - 3];
    const area = [chip[0] + 5, chip[1] + 3, chip[2] - 10, chip[3] - 6];
    const topH = Math.min(14, Math.trunc(area[3] / 2));
    const swatch = [area[0] + 2, area[1] + topH / 2 - 7, 14, 14];
    const trace = [area[0] + 2 + 21, area[1] + topH + 1, area[2] - 4 - 21, area[3] - topH - 2];
    return (
      <div key={m} data-help="!chip" className="chip" onPointerDown={(e) => down(e, m)}>
        <At r={plate} className="plate" style={{ borderRadius: 4, background: armed === m ? rgba(MOD[m].base, 0.3) : rgba(S.ink, shown === m ? 0.16 : 0.05) }} />
        <At r={swatch} style={{ borderRadius: '50%', background: css(MOD[m].base), color: css(MOD[m].ink), ...font.mono(9.5),
                                display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          {m + 1}
        </At>
        <At r={[area[0] + 23, area[1], area[2] - 23 - (routeCounts[m] > 0 ? 18 : 0), topH]} style={{ ...font.name(10, 0.24), color: css(S.ink), display: 'flex', alignItems: 'center', whiteSpace: 'pre' }}>
          <Fitted text={modTitles[m]} style={font.name(10, 0.24)} width={area[2] - 23 - (routeCounts[m] > 0 ? 18 : 0)} origin="0 50%" />
        </At>
        {routeCounts[m] > 0 && (
          <At r={[area[0] + area[2] - 16, area[1] + topH / 2 - 6, 16, 12]} style={{ background: css(MOD[m].base), color: css(MOD[m].ink), ...font.mono(9.5),
                                                                        display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            {routeCounts[m]}
          </At>
        )}
        <At r={[trace[0], Math.trunc(trace[1] + trace[3] / 2), trace[2], 1]} style={{ background: rgba(S.ink, 0.15) }} />
        {traces?.[m]?.length > 1 ? (
          // The trace (ModChip): the modulator's recent output, bipolar about the middle, else up from the bottom.
          <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
            <path d={traces[m].map((v, i) => {
              const base = bipolar(m) ? trace[1] + trace[3] / 2 : trace[1] + trace[3];
              const y = bipolar(m) ? base - Math.max(-1, Math.min(1, v)) * trace[3] / 2 : base - Math.max(0, Math.min(1, v)) * trace[3];
              return `${i ? 'L' : 'M'}${trace[0] + trace[2] * i / 95} ${y}`;
            }).join(' ')} fill="none" stroke={css(MOD[m].base)} strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
        ) : <At r={[trace[0], trace[1] + trace[3] / 2 - 0.7, trace[2], 1.4]} style={{ background: css(MOD[m].base), borderRadius: 1 }} />}
      </div>
    );
  });
  return (
    <>
      {chips}
      {ghost && (
        <div className="at chip-ghost" style={{ left: ghost.x - 30, top: ghost.y - 12, width: 60, height: 24, background: rgba(MOD[ghost.m].base, 0.85),
                                                 borderRadius: 4, color: css(MOD[ghost.m].ink), ...font.mono(11), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          MOD {ghost.m + 1}
        </div>
      )}
    </>
  );
}

// MOD ROUTES: every route, filtered by modulator and page (silt::ui::RouteList), over the
// knobs. A row: the modulator, the page, the parameter, the depth (drag it, or double-click to
// type), its polarity, what it spans in the parameter's units, and × to remove it.
function RouteList({ r, routes, pageName, currentPage, close }) {
  const ui = useContext(UI);
  const [filter, setFilter] = useState(-1);
  const [thisPage, setThisPage] = useState(false);
  const [pendingClear, setPendingClear] = useState(false);
  const [typing, setTyping] = useState(null);
  const inner = [r[0] + 6, r[1] + 4, r[2] - 12, r[3] - 8];
  const head = [inner[0], inner[1], inner[2], 22];
  const footer = [inner[0], inner[1] + inner[3] - 24, inner[2], 24];
  const view = [inner[0], inner[1] + 24, inner[2], inner[3] - 24 - 24];
  const right = (from, w) => [from - w, head[1], w, 22];
  const closeKey = right(head[0] + head[2], 64), pageKey = right(closeKey[0] - 6, 84);
  const clearKey = [footer[0] + footer[2] - 110, footer[1] + 2, 110, 20];
  const redoKey = [clearKey[0] - 8 - 64, footer[1] + 2, 64, 20], undoKey = [redoKey[0] - 4 - 64, footer[1] + 2, 64, 20];
  const shown = routes.filter((rt) => (filter < 0 || rt.m === filter) && (!thisPage || pageName(rt.id) === currentPage));
  const muted = [0, 1, 2, 3].filter((m) => !ui.modulatorOn(m)).length;
  const percent = (a) => { const p = roundToInt(a * 100); return `${p > 0 ? '+' : ''}${p}%`; };
  const snapped = (a) => (Math.abs(a) < 0.02 ? 0 : Math.max(-1, Math.min(1, a)));
  const dragDepth = (e, rt) => {
    if (e.button !== 0) return;
    let running = ui.routeAmount(rt.m, rt.id), lastY = e.clientY;
    ui.beginRouteGesture();
    ui.beginStep('MODULATION');
    const move = (mv) => {
      running = Math.max(-1, Math.min(1, running + (lastY - mv.clientY) / ui.scale / (mv.shiftKey ? 1000 : 150)));
      lastY = mv.clientY;
      ui.setAmount(rt.m, rt.id, snapped(running));
    };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); ui.endStep(); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  return (
    <>
      <At r={r} style={{ background: css(C.bone) }} />
      <At r={[r[0] + 10, r[1] + 4, 110, 22]} style={{ ...font.name(12), color: css(S.ink), display: 'flex', alignItems: 'center' }}>MOD ROUTES</At>
      {['ALL', 'M1', 'M2', 'M3', 'M4'].map((t, i) => (
        <Key key={t} r={[head[0] + 112 + i * 40 + 2, head[1], 36, 22]} text={t} on={filter === i - 1} size={10} onClick={() => setFilter(i - 1)} />
      ))}
      <Key r={pageKey} text="THIS PAGE" on={thisPage} size={10} onClick={() => setThisPage(!thisPage)} />
      <Key r={closeKey} text="CLOSE" size={10} onClick={close} />
      {shown.length === 0 && (
        <At r={view} style={{ ...font.mono(12), color: css(S.soft), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          NO ROUTES. ALT-DRAG A KNOB, OR DRAG A MOD CHIP ONTO ONE.
        </At>
      )}
      <div className="at route-rows" style={{ ...box(view), overflowY: 'auto' }}>
        {shown.map((rt, i) => {
          const y = i * 24, on = ui.modulatorOn(rt.m), bipolar = ui.routeBipolar(rt.m, rt.id);
          return (
            <div key={`${rt.m}${rt.id}`} className="route-row" style={{ position: 'absolute', left: 0, top: y, width: view[2], height: 24 }}>
              <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: 1, background: rgba(S.ink, 0.12) }} />
              <div style={{ position: 'absolute', left: 4, top: 4, width: 26, height: 16, background: css(on ? MOD[rt.m].base : MOD[rt.m].dim), color: css(MOD[rt.m].ink),
                            ...font.mono(10), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>M{rt.m + 1}</div>
              <div style={{ position: 'absolute', left: 40, top: 0, width: 84, height: 24, ...font.mono(10), color: css(S.soft), display: 'flex', alignItems: 'center' }}>{pageName(rt.id)}</div>
              <div style={{ position: 'absolute', left: 124, top: 0, width: 170, height: 24, ...font.name(12), color: css(S.ink), display: 'flex', alignItems: 'center', whiteSpace: 'pre' }}>
                <Fitted text={ui.paramName(rt.id).toUpperCase()} style={font.name(12)} width={168} origin="0 50%" />
              </div>
              <div className="depth-box" style={{ position: 'absolute', left: 294, top: 2, width: 72, height: 20, background: css(C.lcdGreen), color: css(C.lcdText), borderRadius: 3,
                                                   ...font.mono(12), display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'ns-resize' }}
                   onPointerDown={(e) => dragDepth(e, rt)} onDoubleClick={() => setTyping(`${rt.m}${rt.id}`)}>
                {typing === `${rt.m}${rt.id}`
                  ? <input autoFocus className="typed" defaultValue={percent(rt.amount)} style={{ ...font.mono(12), color: css(C.lcdText) }}
                           onBlur={(e) => { ui.beginRouteGesture(); ui.setAmount(rt.m, rt.id, snapped((parseFloat(e.target.value.replace(/[^-0-9.]/g, '')) || 0) / 100)); setTyping(null); }}
                           onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') setTyping(null); }} />
                  : percent(rt.amount)}
              </div>
              <Key r={[370, 2, 26, 20]} text={bipolar ? '\u00b1' : '+'} size={11} onClick={() => ui.togglePolarity(rt.m, rt.id)} style={{ position: 'absolute' }} />
              <div style={{ position: 'absolute', left: 402, top: 0, right: 34, height: 24, ...font.mono(11), color: css(S.soft), display: 'flex', alignItems: 'center', whiteSpace: 'pre' }}>
                {ui.depthSpan(rt.m, rt.id)}
              </div>
              <Key r={[view[2] - 26, 2, 26, 20]} text={'\u00d7'} size={12} onClick={() => { ui.beginRouteGesture(); ui.setAmount(rt.m, rt.id, 0); }} />
            </div>
          );
        })}
      </div>
      <At r={[footer[0] + 10, footer[1], 300, 24]} style={{ ...font.mono(11), color: css(S.soft), display: 'flex', alignItems: 'center', whiteSpace: 'pre' }}>
        {`${routes.length} ${routes.length === 1 ? 'ROUTE' : 'ROUTES'}   ${muted} MUTED`}
      </At>
      <Key r={undoKey} text="UNDO" size={10} onClick={ui.undo} disabled={!ui.canUndo} />
      <Key r={redoKey} text="REDO" size={10} onClick={ui.redo} disabled={!ui.canRedo} />
      <Key r={clearKey} text={pendingClear ? 'CLICK AGAIN' : 'CLEAR ALL'} size={10}
           onClick={() => {
             if (!pendingClear) { setPendingClear(true); setTimeout(() => setPendingClear(false), 2500); return; }
             ui.beginRouteGesture();
             ui.step('MODULATION', () => { for (const rt of routes) ui.setAmount(rt.m, rt.id, 0); });
             setPendingClear(false);
           }} />
    </>
  );
}

// SHAPE's slot 7: one grain's envelope above, one note's ADSR below (SiltEditor::paintPreview).
function ShapePreview({ g, value, grainCurve }) {
  const col = g.slots[7].column;
  const cell = [col[0] + 10, col[1] + 8, col[2] - 20, col[3] - 16];
  const grainBox = [cell[0], cell[1], cell[2], cell[3] * 0.5 - 3];
  const noteBox = [cell[0], grainBox[1] + grainBox[3] + 6, cell[2], cell[3] - grainBox[3] - 6];
  const plot = (b) => [b[0] + 5, b[1] + 6 + 8, b[2] - 10, b[3] - 12 - 8];
  const path = (b, pts) => {
    const a = plot(b);
    return pts.map(([t, l], i) => `${i ? 'L' : 'M'}${a[0] + t * a[2]} ${a[1] + a[3] - l * a[3]}`).join(' ');
  };
  const a = value('attack'), d = value('decay'), sus = value('sustain') * 0.01, rel = value('release');
  const hold = 0.25 * (a + d + rel) + 1, total = a + d + hold + rel;
  const note = [[0, 0], [a / total, 1], [(a + d) / total, sus], [(a + d + hold) / total, sus], [1, 0]];
  const box = (b, pts, caption) => (
    <>
      <At r={b} style={{ background: css(darkerOf(C.lcdGreen, 0.8)) }} />
      <At r={[b[0] + 1, b[1] + 1, b[2] - 2, b[3] - 2]} style={{ background: `linear-gradient(rgba(0,0,0,0.35), rgba(0,0,0,0) 4px), linear-gradient(${css(darkerOf(C.lcdGreen, 0.12))}, ${css(C.lcdGreen)})` }} />
      <svg className="at" style={{ left: 0, top: 0, width: 1, height: 1, overflow: 'visible' }}>
        <path d={path(b, pts)} fill="none" stroke={css(C.lcdText)} strokeWidth="1.5" />
      </svg>
      <At r={[b[0] + 4, b[1] + 2, b[2] - 8, 12]} style={{ ...font.mono(9), color: css(C.lcdText) }}>{caption}</At>
    </>
  );
  return (
    <>
      <At r={[g.slots[3].column[0], g.controlRow[1] + 8, 1, g.controlRow[3] - 16]} style={{ background: rgba(S.ink, 0.2) }} />
      {box(grainBox, grainCurve(value('fade'), value('slant'), value('ease')), 'GRAIN')}
      {box(noteBox, note, 'NOTE')}
    </>
  );
}

function Well({ traces, g, page, entries, context, value, text, params, grainCurve, modTitles, armed, routeCounts, routesOpen, routeList }) {
  const ui = useContext(UI);
  const pick = (id) => (index) => ui.setParam(id, index);
  const toggle = (id) => () => ui.setParam(id, value(id) > 0.5 ? 0 : 1);
  const action = (id) => () => {
    if (id === '!newPattern') ui.setParam('patternSeed', 1 + Math.floor(Math.random() * 9999));   // SiltProcessor::newPattern
    if (id === '!assign') ui.selectModulator(context.modShown, true);
  };
  const colour = pageColour(page);
  const items = entries.filter((e) => (e.alwaysShown || e.page === page) && isShown(e, context));
  return (
    <>
      <Surface r={g.well} colour={S.panel} />
      <At r={[g.well[0], g.well[1], g.well[2], 3]} style={{ background: css(routesOpen ? S.ink : colour), borderRadius: '6px 6px 0 0' }} />
      <Rail r={g.rail} modTitles={modTitles} shown={context.modShown} armed={armed} routeCounts={routeCounts} traces={traces} bipolar={(m) => ['WAVE', 'RANDOM'].includes(TYPE_NAMES[Math.round(value(`m${m + 1}Type`))])} />
      <At r={[g.slots[0].column[0], g.controlRow[1] + 8, 1, g.controlRow[3] - 16]} style={{ background: rgba(S.ink, 0.2) }} />
      {routesOpen && routeList}
      {!routesOpen && page === 1 && <ShapePreview g={g} value={value} grainCurve={grainCurve} />}
      {!routesOpen && items.map((e, i) => {
        const slot = g.slots[e.slot];
        const dim = isDimmed(e, context) ? 0.35 : 1;
        if (e.kind === 'underButton' || e.kind === 'underMenu') {
          const r = centred(slot.under, e.width, 18);
          const rr = [r[0] + (e.dx ?? 0), r[1], r[2], r[3]];
          if (e.kind === 'underMenu') return <Menu key={i} r={rr} text={e.items[Math.round(value(e.id))] ?? ''} items={e.items} selected={Math.round(value(e.id))} onPick={pick(e.id)} help={e.id} />;
          return <Key key={i} r={rr} text={e.text} on={value(e.id) > 0.5} size={10} onClick={toggle(e.id)} help={e.id} />;
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
          control = <Knob r={cell} pos={pos} bipolarOrigin={bipolar} text={text(e.id, v)} id={e.id} help={e.id} />;
        } else if (e.kind === 'menu') {
          control = <Menu r={centred(cell, cell[2], 26)} text={e.items[Math.round(value(e.id))] ?? ''} items={e.items} selected={Math.round(value(e.id))} onPick={pick(e.id)} help={e.id} />;
        } else if (e.kind === 'button' || e.kind === 'action') {
          control = <Key r={centred(cell, cell[2] - 12, 34)} text={e.text} on={e.kind === 'button' && value(e.id) > 0.5}
                         onClick={e.kind === 'button' ? toggle(e.id) : action(e.id)} help={e.id} />;
        } else if (e.kind === 'banner') {
          control = <Banner r={centred(cell, cell[2], 56)} onClick={() => ui.selectModulator((context.modShown + 1) % 4, false)} m={context.modShown} type={Math.round(value(`m${context.modShown + 1}Type`))} routes={routeCounts[context.modShown]}
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
// Either a canvas font string, or a style from theme.font (its tracking counted too).
function Fitted({ text, width, cssFont, style, minScale = 0.7, origin = '50% 50%' }) {
  const f = cssFont ?? `${style.fontWeight ?? 400} ${style.fontSize}px "${style.fontFamily}"`;
  const wide = measure(String(text), f) + (style?.letterSpacing ?? 0) * String(text).length;
  const k = Math.max(minScale, Math.min(1, width / Math.max(1, wide)));
  return <span style={{ display: 'inline-block', transform: `scaleX(${k})`, transformOrigin: origin, marginLeft: origin === '0 50%' ? 0 : -(1 - k) * 0, }} data-fit={k < 1 ? k.toFixed(3) : undefined}>{text}</span>;
}

// Text over up to `lines` lines, as JUCE's drawFittedText: words wrap; a line that's still too
// long is squeezed sideways, down to minScale, and below that the type gets smaller. Never cut.
function WrappedFit({ text, width, height, style, lines, minScale }) {
  let size = style.fontSize, out;
  for (let tries = 0; tries < 12; tries++, size *= 0.92) {
    const f = `${size}px "${style.fontFamily}"`;
    const words = text.split(' ');
    out = [];
    let line = '';
    for (const w of words) {
      const next = line ? `${line} ${w}` : w;
      if (measure(next, f) <= width || !line) line = next;
      else { out.push(line); line = w; }
    }
    out.push(line);
    const lineH = size * 1.15;
    const squeeze = Math.min(...out.map((l) => width / Math.max(1, measure(l, f))));
    if (out.length <= lines && out.length * lineH <= height && squeeze >= minScale) break;
  }
  return (
    <div style={{ ...style, fontSize: size, lineHeight: 1.15 }}>
      {out.map((l, i) => {
        const k = Math.min(1, width / Math.max(1, measure(l, `${size}px "${style.fontFamily}"`)));
        return <div key={i} style={{ whiteSpace: 'pre', transform: `scaleX(${k})`, transformOrigin: '0 50%' }} data-fit={k < 1 ? k.toFixed(3) : undefined}>{l}</div>;
      })}
    </div>
  );
}

// Text width, as the plug-in measures it for placing the next thing.
const measureCanvas = document.createElement('canvas').getContext('2d');
function measure(text, cssFont) {
  measureCanvas.font = cssFont;
  return measureCanvas.measureText(text).width;
}

// JUCE's Colour::darker (each channel × 1/(1 + amount), truncated).
function darkerOf(rgb, amount) {
  const k = 1 / (1 + amount);
  const ch = (shift) => Math.trunc(((rgb >> shift) & 255) * k);
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
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
function Banner({ r, m, type, routes, muted, onClick }) {
  const plate = [r[0] + 2, r[1] + 2, r[2] - 4, r[3] - 4];
  const text = [plate[0] + 6, plate[1] + 4, plate[2] - 12, plate[3] - 8];
  return (
    <div data-help="!modSelect" onClick={onClick} style={{ cursor: 'pointer' }}>
      <At r={plate} style={{ borderRadius: 5, background: rgba(muted ? MOD[m].dim : MOD[m].base, 0.22) }} />
      <At r={[text[0], text[1], text[2], text[3] / 2]} style={{ ...font.mono(18), color: css(muted ? C.inkSoft : C.ink), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        MOD {m + 1}
      </At>
      <At r={[text[0], text[1] + text[3] / 2, text[2], text[3] / 2]} style={{ ...font.mono(10), color: css(C.inkSoft), display: 'flex', alignItems: 'center', justifyContent: 'center', whiteSpace: 'pre' }}>
        {`${TYPE_NAMES[type] ?? ''}  ${routes} ${routes === 1 ? 'ROUTE' : 'ROUTES'}${muted ? '  MUTED' : ''}`}
      </At>
    </div>
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

// The browser's row, where the plug-in has the Bench (there's no other plug-in to pass audio to
// here): START AUDIO (Brood's key and wording), MIC, TEMPO, and keys to play from C1 to B3, which
// covers SLICE's slices from C1 up. All of it is the browser's own; IN THE PLUG-IN says so.
function BrowserRow({ r, state, onStart, mic, onMic, tempo, onTempo, held, noteOn, noteOff }) {
  const ui = useContext(UI);
  const y = r[1] + 6, h = r[3] - 12;
  const keysR = [r[0] + 420, y, r[2] - 428, h];
  const first = 36, whites = 21;
  const white = keysR[2] / whites;
  const isBlack = (n) => [1, 3, 6, 8, 10].includes(n % 12);
  const notes = Array.from({ length: 36 }, (_, i) => first + i);
  const whiteIndex = (n) => notes.filter((x) => x < n && !isBlack(x)).length;
  const pressing = useRef(null);
  const press = (note) => { if (pressing.current === note) return; if (pressing.current != null) noteOff(pressing.current); pressing.current = note; if (note != null) noteOn(note); };
  useEffect(() => {
    const up = () => press(null);
    window.addEventListener('pointerup', up);
    return () => window.removeEventListener('pointerup', up);
  }, []);
  const key = (n) => {
    const b = isBlack(n);
    const x = b ? keysR[0] + whiteIndex(n) * white - white * 0.3 : keysR[0] + whiteIndex(n) * white;
    const kr = b ? [x, keysR[1], white * 0.6, keysR[3] * 0.6] : [x + 0.5, keysR[1], white - 1, keysR[3]];
    const down = held.has(n);
    return (
      <div key={n} className="at piano" data-help="!keys"
           style={{ ...box(kr), zIndex: b ? 2 : 1, borderRadius: '0 0 3px 3px',
                    background: down ? css(S.accent) : b ? css(S.sunk) : css(C.knobFace) }}
           onPointerDown={(e) => { e.preventDefault(); press(n); }} onPointerEnter={(e) => { if (e.buttons & 1) press(n); }}>
        {!b && n % 12 === 0 && (
          <span style={{ position: 'absolute', bottom: 2, left: 0, right: 0, textAlign: 'center', ...font.mono(9), color: down ? css(S.panel) : '#555048' }}>C{n / 12 - 2}</span>
        )}
      </div>
    );
  };
  const tempoDrag = (e) => {
    if (e.button !== 0) return;
    let running = tempo, lastY = e.clientY;
    const move = (m) => { running += (lastY - m.clientY) / ui.scale / (m.shiftKey ? 10 : 2); lastY = m.clientY; onTempo(running); };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  const running = state === 'running';
  return (
    <>
      <Surface r={r} colour={S.panel} />
      <Key r={[r[0] + 8, y, 150, h]} text={state === 'starting' ? 'ONE MOMENT' : running ? 'STOP AUDIO' : 'START AUDIO'} led={running} onClick={onStart} help="!start" accent={!running && state !== 'starting'} />
      <Key r={[r[0] + 166, y, 84, h]} text="MIC" led={mic} onClick={onMic} help="!mic" />
      <At r={[r[0] + 262, y, 60, h]} style={{ ...font.name(12.5), color: css(S.soft), display: 'flex', alignItems: 'center' }} data-help="!tempo">TEMPO</At>
      <At r={[r[0] + 322, y + 5, 86, h - 10]} data-help="!tempo" onPointerDown={tempoDrag} onDoubleClick={() => onTempo(120)}
          style={{ background: css(C.lcdGreen), color: css(C.lcdText), borderRadius: 3, ...font.value(11.5), display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'ns-resize' }}>
        {tempo.toFixed(1)} BPM
      </At>
      {notes.map(key)}
    </>
  );
}

// RANDOM's amount (RandomAmountBox): drag up or down (Shift finer), scroll, double-click for 50%.
function AmountBox({ r, amount, set }) {
  const ui = useContext(UI);
  const clampSet = (a) => set(Math.max(0.01, Math.min(1, Math.round(a * 100) / 100)));
  const down = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    let running = amount, lastY = e.clientY;
    const move = (m) => { running = Math.max(0.01, Math.min(1, running + (lastY - m.clientY) / ui.scale / (m.shiftKey ? 1000 : 200))); lastY = m.clientY; clampSet(running); };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  return (
    <At r={[r[0] + 0.5, r[1] + 2.5, r[2] - 1, r[3] - 5]} data-help="!randomAmount" className="amount" onPointerDown={down}
        onDoubleClick={() => clampSet(0.5)} onWheel={(e) => clampSet(amount + (e.deltaY < 0 ? 0.05 : -0.05))}
        style={{ background: css(C.lcdGreen), color: css(C.lcdText), ...font.mono(12), display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'ns-resize' }}>
      {Math.round(amount * 100)}%
    </At>
  );
}

// The kit's numbered sheet: a hard offset, the panel, a frame, and a head with the number in
// a block, the name, a dotted leader and a figure (kit::drawPanel on a framed stock).
function Sheet({ r, index, name, tail, children }) {
  const head = [r[0] + 1, r[1] + 1, r[2] - 2, 26 - 2];
  return (
    <>
      <At r={[r[0] + 3, r[1] + 3, r[2], r[3]]} style={{ background: css(S.shadow) }} />
      <At r={r} style={{ background: css(S.panel), border: `1px solid ${css(S.ink)}` }} />
      <At r={head} style={{ background: css(S.head), borderBottom: `1px solid ${css(S.ink)}` }} />
      <At r={[head[0] + 8, head[1] + 4, 22, 16]} style={{ background: css(S.ink), color: css(S.panel), borderRadius: 0, ...font.screen(16), display: 'flex', alignItems: 'center', justifyContent: 'center' }}>{index}</At>
      <At r={[head[0] + 36, head[1], head[2] - 44, head[3]]} style={{ ...font.screen(16), color: css(S.ink), display: 'flex', alignItems: 'center', gap: 6 }}>
        <span>{name}</span>
        <span className="leader" style={{ flex: 1, alignSelf: 'center', height: 1, backgroundImage: `linear-gradient(90deg, ${rgba(S.ink, 0.28)} 1px, transparent 1px)`, backgroundSize: '4px 1px' }} />
        <span style={{ fontFamily: 'Chivo Mono', fontSize: (16 - 2) / 1.19, color: css(S.soft) }}>{tail}</span>
      </At>
      {children}
    </>
  );
}

// The preset browser (kit::PresetBrowser), over the screen and the well: SEARCH, the views
// (ALL, FAV, the categories), the list (star, name, category, FACTORY or USER); a click plays
// the preset at once, KEEP (Return, double-click) keeps it as one undo step, CANCEL (Esc) puts
// back the sound there was. FOLDER is the plug-in's alone: the browser keeps your presets itself.
function PresetBrowser({ r, list, current, categories, favourites, setFavourite, begin, audition, keep, cancel, saveAs }) {
  const before = useRef(null);
  if (before.current === null) before.current = begin();
  const [view, setView] = useState('ALL');
  const [search, setSearch] = useState('');
  const [selected, setSelected] = useState(current);
  const searchRef = useRef(null);
  useEffect(() => { searchRef.current?.focus(); }, []);
  const q = search.trim().toLowerCase();
  const shown = list.filter((e) => (view === 'ALL' || (view === 'FAVOURITES' ? favourites.has(e.name) : e.category === view))
                                   && (!q || e.name.toLowerCase().includes(q) || e.category.toLowerCase().includes(q)));
  const views = ['ALL', 'FAVOURITES', ...categories];
  const body = [r[0] + 10, r[1] + 26 + 8, r[2] - 3 - 20, r[3] - 3 - 26 - 16];
  const top = [body[0], body[1], body[2], 30];
  const searchW = Math.min(200, Math.trunc(top[2] / 3));
  const keyW = Math.trunc((top[2] - searchW - 8) / views.length);
  const bottom = [body[0], body[1] + body[3] - 32, body[2], 32];
  const listR = [body[0], body[1] + 38, body[2], body[3] - 38 - 40];
  const choose = (e) => { setSelected(e.name); audition(e); };
  const keyDown = (ev) => {
    if (ev.key === 'Escape') { ev.preventDefault(); cancel(before.current); }
    else if (ev.key === 'Enter') { ev.preventDefault(); keep(before.current); }
    else if (ev.key === 'ArrowDown' || ev.key === 'ArrowUp') {
      ev.preventDefault();
      const at = shown.findIndex((e) => e.name === selected);
      const next = shown[Math.max(0, Math.min(shown.length - 1, at + (ev.key === 'ArrowDown' ? 1 : -1)))];
      if (next) choose(next);
    }
  };
  // Return, Escape and the arrows wherever the focus is while it's open.
  const keyRef = useRef(keyDown);
  keyRef.current = keyDown;
  useEffect(() => {
    const on = (ev) => keyRef.current(ev);
    window.addEventListener('keydown', on);
    return () => window.removeEventListener('keydown', on);
  }, []);
  return (
    <div className="browser">
      <Sheet r={[r[0], r[1], r[2] - 3, r[3] - 3]} index="P" name="PRESETS" tail={`${shown.length} ${shown.length === 1 ? 'SOUND' : 'SOUNDS'}`}>
        <input ref={searchRef} className="search" placeholder="SEARCH" value={search} onChange={(e) => setSearch(e.target.value)}
               style={{ ...box([top[0], top[1], searchW, 30]), background: css(S.sunk), color: css(S.onSunk), ...font.screen(18), border: `1px solid ${css(S.ink)}` }} />
        {views.map((v, i) => (
          <Key key={v} r={[top[0] + searchW + 8 + i * keyW + 1, top[1], keyW - 2, 30]} text={v === 'FAVOURITES' ? 'FAV' : v} on={view === v} onClick={() => setView(v)} />
        ))}
        <div className="at preset-list" style={{ ...box(listR), overflowY: 'auto' }}>
          {shown.map((e) => {
            const sel = e.name === selected, fav = favourites.has(e.name);
            const text = sel ? css(S.panel) : css(S.ink), quiet = sel ? css(S.panel) : css(S.soft);
            return (
              <div key={`${e.factory}${e.name}`} className="preset-row" style={{ height: 26, background: sel ? css(S.ink) : 'transparent', borderBottom: sel ? 'none' : `1px solid ${rgba(S.ink, 0.28)}` }}
                   onClick={() => choose(e)} onDoubleClick={() => { choose(e); keep(before.current); }}>
                <svg width="30" height="26" viewBox="-15 -13 30 26" onClick={(ev) => { ev.stopPropagation(); setFavourite(e.name, !fav); }} style={{ flex: 'none', cursor: 'pointer' }}>
                  <path d={starPath(7.5, 3.4)} fill={fav ? (sel ? text : css(S.accent)) : 'none'} stroke={fav ? 'none' : quiet} strokeWidth="1.2" />
                </svg>
                <span style={{ flex: 1, ...font.screen(19), color: text, whiteSpace: 'pre' }}>{`${e.name === current ? '> ' : ''}${e.name.toUpperCase()}`}</span>
                <span style={{ width: 120, flex: 'none', ...font.screen(17), color: quiet }}>{e.category}</span>
                <span style={{ width: 80, flex: 'none', ...font.screen(17), color: quiet }}>{e.factory ? 'FACTORY' : 'USER'}</span>
              </div>
            );
          })}
        </div>
        <Key r={[bottom[0] + 3, bottom[1], 94, 32]} text="SAVE AS" onClick={saveAs} />
        <At r={[bottom[0] + 100, bottom[1], bottom[2] - 280, 32]} style={{ ...font.screen(15), color: css(S.soft), display: 'flex', alignItems: 'center', justifyContent: 'center', whiteSpace: 'pre' }}>
          CLICK TO HEAR   RETURN KEEPS   ESC GOES BACK
        </At>
        <Key r={[bottom[0] + bottom[2] - 180 + 3, bottom[1], 84, 32]} text="CANCEL" onClick={() => cancel(before.current)} />
        <Key r={[bottom[0] + bottom[2] - 90 + 3, bottom[1], 84, 32]} text="KEEP" accent onClick={() => keep(before.current)} />
      </Sheet>
    </div>
  );
}

// A five-pointed star, a point at the top (juce::Path::addStar).
function starPath(outer, inner) {
  const pts = [];
  for (let i = 0; i < 10; i++) {
    const a = (i * Math.PI) / 5, rr = i % 2 ? inner : outer;
    pts.push(`${(rr * Math.sin(a)).toFixed(2)},${(-rr * Math.cos(a)).toFixed(2)}`);
  }
  return `M${pts.join(' L')} Z`;
}

// Saving a preset (kit::askToSave): a name and a category.
function SaveDialog({ categories, start, save, cancel }) {
  const [name, setName] = useState(start);
  const [category, setCategory] = useState(categories[categories.length - 1]);
  const ui = useContext(UI);
  const r = [400, 300, 400, 170];
  const go = () => { if (name.trim()) save(name.trim(), category); };
  return (
    <div className="dialog-shield" onKeyDown={(e) => { if (e.key === 'Escape') cancel(); if (e.key === 'Enter') go(); }}>
      <Sheet r={r} index="S" name="SAVE PRESET" tail="">
        <At r={[r[0] + 12, r[1] + 36, r[2] - 24, 18]} style={{ ...font.name(12.5), color: css(S.ink), display: 'flex', alignItems: 'center' }}>A name for this sound:</At>
        <input autoFocus className="search" value={name} onChange={(e) => setName(e.target.value)}
               style={{ ...box([r[0] + 12, r[1] + 58, r[2] - 24, 30]), background: css(S.sunk), color: css(S.onSunk), ...font.screen(18), border: `1px solid ${css(S.ink)}` }} />
        <Menu r={[r[0] + 12, r[1] + 96, 180, 30]} text={category} items={categories} selected={categories.indexOf(category)} onPick={(i) => setCategory(categories[i])} />
        <Key r={[r[0] + r[2] - 196, r[1] + r[3] - 42, 90, 32]} text="CANCEL" onClick={cancel} />
        <Key r={[r[0] + r[2] - 102, r[1] + r[3] - 42, 90, 32]} text="SAVE" accent onClick={go} disabled={!name.trim()} />
      </Sheet>
      {void ui}
    </div>
  );
}

// Help for the browser's own parts, as the owner worded them. START AUDIO's is Brood's.
const WEB_HELP = {
  '!start': 'AUDIO: starts and stops the sound on this page. Nothing plays until you press it',
  '!mic': 'MIC: your voice or an instrument fed in to Silt, for LIVE and FEED modes. Use headphones',
  '!tempo': 'TEMPO: beats per minute, modulation and time based fx will be synced to this amount',
  '!keys': 'KEYS: play notes here, or on the computer: A to K, Z and X for octaves',
};

// The screen's GAIN (SampleGainBox): the sample's own level. Drag (Shift finer), scroll,
// double-click for 0 dB, right-click to normalise.
function GainBox({ r, text, db, set, step, beginStep, endStep, normalise, openMenu }) {
  const ui = useContext(UI);
  const down = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    let running = db, lastY = e.clientY;
    beginStep('SAMPLE GAIN');
    const move = (m) => { running = Math.max(-24, Math.min(24, running + (lastY - m.clientY) / ui.scale * (m.shiftKey ? 0.02 : 0.15))); lastY = m.clientY; set(running); };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); endStep(); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  return (
    <At r={r} className="gain-box" data-help="!gain" onPointerDown={down}
        onDoubleClick={() => step('SAMPLE GAIN', () => set(0))}
        onWheel={(e) => step('SAMPLE GAIN', () => set(Math.max(-24, Math.min(24, db + (e.deltaY < 0 ? 0.5 : -0.5)))))}
        onContextMenu={(e) => { e.preventDefault(); openMenu({ r, items: [
          { text: 'Normalise (peak at -1 dB)', pick: () => step('SAMPLE GAIN', () => set(normalise())) },
          { text: 'Reset to 0 dB', pick: () => step('SAMPLE GAIN', () => set(0)) },
        ] }); }}
        style={{ ...font.screen(12 * 1.1), color: Math.abs(db) < 0.05 ? '#ffffff' : css(C.screenLime), display: 'flex', alignItems: 'center', justifyContent: 'flex-end', cursor: 'ns-resize' }}>
      {text}
    </At>
  );
}

// The screen's face in canvas pixels: monoFont(size) is B612 Mono at 1.1× the size, by JUCE height.
const screenFont = (size) => (size * 1.1) / 1.215;

// What any RANDOM CHARACTER leaves where it is (everything that sets or shifts tuning), and
// what each keeps to, in the parameter's own units (SiltProcessor's characterLimit).
const SETS_TUNING = new Set(['pitch', 'randPitch', 'scale', 'key', 'root', 'keyMode', 'patternDepth', 'patternSteps', 'resTune', 'resShift',
                             'resNotes', 'resFollow', 'resSpread', 'resDrift', 'colorWow', 'resStretch', 'resCurve', 'resSnap', 'resOffset']);
const CHARACTER_LIMITS = [
  {},
  { reverse: [0, 10], spray: [0, 25], size: [80, 800], rate: [10, 50], jitter: [20, 100], fade: [50, 100], colorBits: [12, 16] },
  { size: [20, 180], rate: [4, 30], jitter: [0, 10], spray: [0, 8], fade: [20, 70], reverse: [0, 30] },
  { size: [30, 500], rate: [15, 80], spray: [30, 100], jitter: [50, 100], randPan: [40, 100], randAmp: [20, 80], fade: [60, 100] },
];

// The browser's own keeping (presets you save, your favourites): this browser only.
function readStored(key, fallback) {
  try { const v = localStorage.getItem(key); return v ? JSON.parse(v) : fallback; } catch { return fallback; }
}
function writeStored(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch { /* private window: kept for this visit only */ }
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
  const [, setVersion] = useState(0);
  const query = new URLSearchParams(location.search);
  const [page, setPage] = useState(() => Math.max(0, Math.min(9, Number(query.get('page')) || 0)));
  const [half, setHalf] = useState(() => Math.max(0, Math.min(3, Number(query.get('half')) || 0)));
  const [modShown, setModShown] = useState(0);
  const [armed, setArmed] = useState(-1);
  const [editing, setEditing] = useState(null);
  const [menu, setMenu] = useState(null);
  const [hover, setHover] = useState(null);
  const [depthReadout, setDepthReadout] = useState('');
  const [flash, setFlash] = useState('');
  const [dropHover, setDropHover] = useState(null);   // { m, id }: a chip held over a knob
  const [routesOpen, setRoutesOpen] = useState(false);
  const lastUsedMod = useRef(0);
  const history = useRef({ undo: [], redo: [], pending: null });
  const [audioState, setAudioState] = useState('off');   // off, starting, running, suspended, error
  const [display, setDisplay] = useState(null);           // what the audio sends back, 30 times a second
  const [sample, setSample] = useState(null);             // { name, factory, channels, sampleRate, seconds, analysis }
  const sampleRef = useRef(null);                          // the same, now (undo reads it mid-step)
  const manualRef = useRef([]);                            // SLICE BY MANUAL's starts (shares)
  const sliceMarksRef = useRef([]);                        // the slices as shown, for edits
  const transientMemo = useRef({});
  // The scope taps RESONATOR, FILTER or COLOR while its page is open.
  useEffect(() => { data?.audio.scope(SCOPE_MODULE[page] ?? -1); }, [data, page]);
  const overview = useMemo(() => makeOverview(sample), [sample]);
  const [tempo, setTempo] = useState(120);
  const [micOn, setMicOn] = useState(false);
  const [held, setHeld] = useState(() => new Set());     // notes down, from any keyboard
  const [octave, setOctave] = useState(0);               // the computer keys' octave shift
  const traces = useRef([[], [], [], []]);                // the rail's modulator traces
  const fileInput = useRef(null);
  const [capture, setCapture] = useState(null);           // ARM's take: { progress, resample }
  const capturedRef = useRef(null);
  const stepRef = useRef(null);
  const regionMemo = useRef({});
  const midiRef = useRef(null);
  const [learnStep, setLearnStep] = useState(-1);         // −1 idle; 0–7 knob n; 8 previous page; 9 next
  const controller = useRef({
    ...readStored('silt-controller-v1', { knobs: [70, 71, 72, 73, 74, 75, 76, 77], previous: 78, next: 79 }),
    learned: [], pageLast: [0, 0], picked: [], last: [], target: [], pickupPage: -1,
  });
  const [randomAmount, setRandomAmount] = useState(0.5);
  const [randomCharacter, setRandomCharacter] = useState(0);
  const [randomExcluded, setRandomExcluded] = useState(() => new Set(['m1Type', 'm2Type', 'm3Type', 'm4Type']));
  const [presetName, setPresetName] = useState('Init');
  const presetAsLoaded = useRef(null);
  const [browserOpen, setBrowserOpen] = useState(false);
  const [saving, setSaving] = useState(false);
  const [userPresets, setUserPresets] = useState(() => readStored('silt-presets-v1', []));
  const [favourites, setFavourites] = useState(() => new Set(readStored('silt-favourites-v1', [])));
  const stage = useRef(null);
  const g = useMemo(() => panelGeometry(true), []);

  useEffect(() => {
    (async () => {
      const get = (name) => fetch(`data/${name}.json`).then((r) => r.json());
      const [params, layout, help, presets] = await Promise.all(['params', 'layout', 'help', 'presets'].map(get));
      // The plug-in's faces, loaded before the first draw so nothing reflows.
      const faces = [['B612 Mono', 'B612Mono-Regular.ttf', {}], ['Overpass', 'Overpass-Medium.ttf', { weight: '500' }],
                     ['Chivo Mono', 'ChivoMono-Regular.ttf', {}], ['Glyph Grotesque', 'GlyphGrotesque-Regular.ttf', {}]];
      await Promise.all(faces.map(async ([family, file, descriptors]) => document.fonts.add(await new FontFace(family, `url(fonts/${file})`, descriptors).load())));
      // The page's own instance of the sound's code: the parameters as the host holds them,
      // their text and the previews. The audio's instance gets every change too (step 5).
      const wasm = await fetch('silt.wasm').then((r) => r.arrayBuffer());
      const silt = await loadSilt(wasm);
      silt.create(true);
      const audio = new Audio({ wasm, paramCount: params.length,
        onDisplay: (d) => {
          for (let m = 0; m < 4; m++) { const t = traces.current[m]; t.push(d.values[params.length + m]); if (t.length > 96) t.shift(); }
          setDisplay(d);
        },
        onState: (st) => setAudioState(st === 'ready' ? 'running' : st === 'closed' ? 'off' : st),
        onCaptured: (take) => capturedRef.current?.(take),
        onMidi: (bytes) => midiRef.current?.(bytes) });
      window.siltAudio = audio;
      setData({ params, layout, help, presets, silt, audio, byId: Object.fromEntries(params.map((p) => [p.id, p])) });
    })().catch((err) => console.error(err));
  }, []);

  // A new instance opens with the first factory sample, as the plug-in does.
  useEffect(() => { if (data && !sample) loadSample({ factory: 0 }).catch((err) => console.error(err)); }, [data]);
  // The computer's keys play notes: A W S E D F T G Y H U J K O L P (C to D# an octave up), Z and X
  // move the octave. Not while a value is being typed.
  useEffect(() => {
    if (!data) return undefined;
    const keys = 'awsedftgyhujkolp';
    const down = new Map();
    const onDown = (e) => {
      if (e.repeat || e.metaKey || e.ctrlKey || e.altKey || /INPUT|TEXTAREA/.test(document.activeElement?.tagName ?? '')) return;
      const k = e.key.toLowerCase();
      if (k === 'z' || k === 'x') { setOctave((o) => Math.max(-3, Math.min(4, o + (k === 'z' ? -1 : 1)))); return; }
      const i = keys.indexOf(k);
      if (i < 0 || down.has(k)) return;
      const note = 60 + octaveRef.current * 12 + i;
      down.set(k, note);
      noteOn(note);
    };
    const onUp = (e) => {
      const k = e.key.toLowerCase();
      if (!down.has(k)) return;
      noteOff(down.get(k));
      down.delete(k);
    };
    window.addEventListener('keydown', onDown);
    window.addEventListener('keyup', onUp);
    return () => { window.removeEventListener('keydown', onDown); window.removeEventListener('keyup', onUp); };
  }, [data]);
  const octaveRef = useRef(0);
  octaveRef.current = octave;
  // A file dropped anywhere on the page becomes the sample (the plug-in takes it on its screen).
  useEffect(() => {
    if (!data) return undefined;
    const over = (e) => { if (e.dataTransfer?.types?.includes('Files')) e.preventDefault(); };
    const drop = (e) => {
      const file = e.dataTransfer?.files?.[0];
      if (!file) return;
      e.preventDefault();
      loadSample({ file }).catch(() => showFlashRef.current?.("COULDN'T READ THAT FILE"));
    };
    window.addEventListener('dragover', over);
    window.addEventListener('drop', drop);
    return () => { window.removeEventListener('dragover', over); window.removeEventListener('drop', drop); };
  }, [data]);
  const showFlashRef = useRef(null);

  // Notes, from the on-screen keys, the computer's or MIDI: to the audio, and shown held.
  function noteOn(note, velocity = 100) {
    window.siltAudio?.noteOn(note, velocity);
    setHeld((h) => new Set(h).add(note));
  }
  function noteOff(note) {
    window.siltAudio?.noteOff(note);
    setHeld((h) => { const n = new Set(h); n.delete(note); return n; });
  }
  // A sample: a factory one (ROOT to its note) or your file (ROOT back to C3), decoded, cut to
  // 30 s, analysed here on the page, then handed to the audio.
  async function loadSample({ factory, file }) {
    const { audio, silt, byId } = data;
    const bytes = factory != null ? await fetch(`samples/${SAMPLES[factory].file}`).then((r) => r.arrayBuffer()) : await file.arrayBuffer();
    const decoded = await audio.decode(bytes);
    const name = factory != null ? SAMPLES[factory].name : file.name.replace(/\.[^.]+$/, '');
    installSample(decoded, name, factory ?? -1, factory != null ? SAMPLES[factory].root : 60);
    if (decoded.cut) showFlashRef.current?.(`FIRST ${30} S OF ${name.toUpperCase()}`);
  }
  // Any sample in: analysed here, installed by the audio; ROOT set if given (a take keeps it).
  function installSample(decoded, name, factory, root) {
    const { audio, silt, byId } = data;
    const analysis = silt.analyse(decoded.channels, decoded.sampleRate);
    const next = { name, factory, ...decoded, seconds: decoded.channels[0].length / decoded.sampleRate, analysis };
    const put = () => {
      audio.sendSample(decoded, analysis);
      if (root != null) {
        silt.setParam(byId.root.index, root);
        audio.setParam(byId.root.index, silt.getParam(byId.root.index));
      }
      sampleRef.current = next;
      setSample(next);
    };
    // An undo step (the sample and ROOT together), except the one a new page opens with.
    if (sampleRef.current) stepRef.current(name.toUpperCase(), put); else put();
    setVersion((n) => n + 1);
  }

  if (!data) return <div className="frame" style={{ width: 1200 * scale, height: 820 * scale }} />;

  const { byId, silt } = data;
  const value = (id) => (byId[id] ? silt.getParam(byId[id].index) : 0);
  const text = (id, v) => silt.text(byId[id].index, v);
  const setParam = (id, v) => {
    if (!byId[id]) return;
    silt.setParam(byId[id].index, v);
    window.siltAudio?.setParam(byId[id].index, v);
    setVersion((n) => n + 1);
  };
  // --- undo (SiltProcessor's one history): a step is the sound before and after, every
  // parameter and route; a drag is one step, as is a pick, RANDOM, a reset or a preset.
  const targets = data.params.filter((p) => silt.isTarget(p.index)).map((p) => p.index);
  const snapshot = () => ({
    sample: sampleRef.current,
    p: data.params.map((p) => silt.getParam(p.index)),
    r: targets.flatMap((t) => [0, 1, 2, 3].map((m) => [silt.routeAmount(m, t), silt.routePolarity(m, t)])),
    sl: manualRef.current,
  });
  const sameSound = (a, b) => a.sample === b.sample && a.sl.length === b.sl.length && a.sl.every((v, i) => v === b.sl[i]) && a.p.every((v, i) => v === b.p[i]) && a.r.every((v, i) => v[0] === b.r[i][0] && v[1] === b.r[i][1]);
  const restore = (snap) => {
    if (snap.sample && snap.sample !== sampleRef.current) {
      data.audio.sendSample(snap.sample, snap.sample.analysis);
      sampleRef.current = snap.sample;
      setSample(snap.sample);
    }
    if (snap.sl !== manualRef.current) setManualSlices(snap.sl);
    data.params.forEach((p, i) => { if (silt.getParam(p.index) !== snap.p[i]) { silt.setParam(p.index, snap.p[i]); window.siltAudio?.setParam(p.index, snap.p[i]); } });
    targets.forEach((t, k) => [0, 1, 2, 3].forEach((m) => {
      const [amount, polarity] = snap.r[k * 4 + m];
      if (silt.routeAmount(m, t) !== amount || silt.routePolarity(m, t) !== polarity) { silt.setRoute(m, t, amount, polarity); window.siltAudio?.setRoute(m, t, amount, polarity); }
    }));
    setVersion((n) => n + 1);
  };
  const h = history.current;
  // SLICE BY MANUAL: your own slice starts (SiltProcessor::setManualSlices): sorted, inside the
  // sample, at most 128, none closer than 256 samples' worth. The page's own (saved with undo).
  const sliceGap = () => (sampleRef.current ? 256 / sampleRef.current.channels[0].length : 0.0005);
  function setManualSlices(marks) {
    const sorted = [...marks].sort((a, b) => a - b), gap = sliceGap(), kept = [];
    for (const m of sorted)
      if (m >= 0 && m < 1 && (kept.length === 0 || m - kept[kept.length - 1] >= gap) && kept.length < 128) kept.push(Math.fround(m));
    manualRef.current = kept;
    data.audio.setSlices(kept);
    setVersion((n) => n + 1);
  }
  // From EQUAL or AT HITS: their slices become MANUAL's, then the edit applies.
  const makeSlicesManual = () => {
    if (Math.round(value('sliceMode')) === 2 && manualRef.current.length > 0) return;
    setManualSlices(sliceMarksRef.current);
    setParam('sliceMode', 2);
  };
  const sliceEdits = {
    move(index, share) {
      makeSlicesManual();
      const marks = [...manualRef.current];
      if (index < 0 || index >= marks.length) return;
      // Between its neighbours: slices keep their order (and their minimum size).
      const gap = sliceGap() * 1.01;
      const lo = index > 0 ? marks[index - 1] + gap : 0, hi = index + 1 < marks.length ? marks[index + 1] - gap : 0.999;
      marks[index] = Math.max(lo, Math.min(Math.max(lo, hi), share));
      setManualSlices(marks);
    },
    add(share) { makeSlicesManual(); setManualSlices([...manualRef.current, Math.max(0, Math.min(0.999, share))]); },
    remove(index) {
      makeSlicesManual();
      const marks = [...manualRef.current];
      if (marks.length > 1 && index >= 0 && index < marks.length) marks.splice(index, 1);
      setManualSlices(marks);
    },
  };
  const beginStep = (name) => { if (!h.pending) h.pending = { name, before: snapshot() }; };
  const endStep = () => {
    if (!h.pending) return;
    const after = snapshot();
    if (!sameSound(h.pending.before, after)) {
      h.undo.push({ name: h.pending.name, before: h.pending.before, after });
      if (h.undo.length > 2000) h.undo.shift();
      h.redo = [];
      // Samples and takes: the last four stay undoable (as Brood keeps its takes, and the plug-in
      // lets old takes give way); a fifth lets the oldest go, and everything before it.
      const changes = h.undo.map((st, i) => (st.before.sample !== st.after.sample ? i : -1)).filter((i) => i >= 0);
      if (changes.length > 4) h.undo.splice(0, changes[changes.length - 5] + 1);
    }
    h.pending = null;
    setVersion((n) => n + 1);
  };
  const step = (name, fn) => { if (h.pending) { fn(); return; } beginStep(name); fn(); endStep(); };
  stepRef.current = step;
  // A controller knob sends a message per move: moves of one parameter close together are one
  // step (SiltProcessor's ParameterChange merging).
  const mergedStep = (key, name, fn) => {
    const top = h.undo[h.undo.length - 1], now = performance.now();
    if (top && top.key === key && now - top.time < 1000) { fn(); top.after = snapshot(); top.time = now; setVersion((n) => n + 1); return; }
    step(name, fn);
    const added = h.undo[h.undo.length - 1];
    if (added && added !== top) { added.key = key; added.time = now; }
  };
  const undo = () => { endStep(); const st = h.undo.pop(); if (st) { restore(st.before); h.redo.push(st); } };
  const redo = () => { endStep(); const st = h.redo.pop(); if (st) { restore(st.after); h.undo.push(st); } };

  // --- RANDOM and RESET (SiltProcessor::randomisePage, resetPage) -------------------------
  const randomisable = (p) => data.layout.instrument.filter((e) => e.page === p && ['knob', 'menu', 'button'].includes(e.kind)
    && !e.id.startsWith('!') && e.id !== 'inputGain' && e.id !== 'mix' && !e.alwaysShown && isShown(e, context) && byId[e.id]);
  const steps01 = (p) => (p.kind === 'choice' ? p.choices.length : p.kind === 'switch' ? 2 : p.kind === 'integer' ? p.high - p.low + 1 : 2);
  const randomise = () => {
    let changed = 0;
    step(`RANDOM ${PAGE_NAMES[page]}`, () => {
      for (const e of randomisable(page)) {
        if (randomExcluded.has(e.id)) continue;
        const p = byId[e.id];
        const before = silt.to01(p.index, value(e.id));
        let after = before;
        if (e.kind === 'knob') after = before + (Math.random() - before) * randomAmount;
        else if (Math.random() < randomAmount) { const n = Math.max(2, steps01(p)); after = Math.floor(Math.random() * n) / (n - 1); }
        if (randomCharacter > 0 && SETS_TUNING.has(e.id)) continue;   // a CHARACTER never touches tuning
        const limit = CHARACTER_LIMITS[randomCharacter]?.[e.id];
        if (limit) after = Math.max(silt.to01(p.index, limit[0]), Math.min(silt.to01(p.index, limit[1]), after));
        const target = silt.from01(p.index, after);
        if (silt.to01(p.index, target) === before) continue;
        setParam(e.id, target);
        ++changed;
      }
    });
    showFlash(changed > 0 ? `RANDOM ${PAGE_NAMES[page]}: ${changed} ${changed === 1 ? 'CHANGE' : 'CHANGES'}` : `RANDOM ${PAGE_NAMES[page]}: NOTHING TO CHANGE`);
  };
  const pageParameters = (p, everyModulator = false) => (p === 8
    ? data.params.filter((x) => [0, 1, 2, 3].some((m) => (everyModulator || m === modShown) && x.id.startsWith(`m${m + 1}`))).map((x) => x.id)
    : [...new Set(data.layout.instrument.filter((e) => e.page === p && !e.id.startsWith('!') && e.id !== 'root' && byId[e.id]).map((e) => e.id))]);
  const resetPages = (which) => {
    const ids = which < 0 ? [...new Set(PAGE_NAMES.flatMap((_, p) => pageParameters(p, true)))] : pageParameters(which);
    let changed = 0;
    step(`RESET ${which < 0 ? 'ALL' : PAGE_NAMES[which]}`, () => {
      for (const id of ids) {
        const p = byId[id];
        if (silt.to01(p.index, value(id)) === silt.to01(p.index, p.default)) continue;
        setParam(id, p.default);
        ++changed;
      }
    });
    const name = which < 0 ? 'ALL PAGES' : PAGE_NAMES[which];
    showFlash(changed > 0 ? `RESET ${name}: ${changed} ${changed === 1 ? 'CHANGE' : 'CHANGES'}` : `RESET ${name}: ALREADY AT DEFAULTS`);
  };
  const resetMenu = (p, r) => setMenu({ r, items: [
    { text: `Reset ${PAGE_NAMES[p]}${p === 8 ? ` ${modShown + 1}` : ''}`, pick: () => resetPages(p) },
    { text: 'Reset all pages', pick: () => resetPages(-1) },
  ] });
  const randomMenu = () => {
    const entries = randomisable(page), now = Math.round(randomAmount * 100);
    setMenu({ r: g.random, items: [
      { header: true, text: 'AMOUNT' },
      ...[10, 25, 50, 75, 100].map((a) => ({ text: `${a}%${a <= 10 ? '  nudge' : a === 100 ? '  anything goes' : ''}`, ticked: now === a, pick: () => setRandomAmount(a / 100) })),
      { header: true, text: 'CHARACTER' },
      ...['Anything', 'Tonal: stays in tune', 'Rhythmic: short, regular', 'Texture: dense, scattered'].map((t, i) => ({ text: t, ticked: randomCharacter === i, pick: () => setRandomCharacter(i) })),
      { separator: true },
      { text: `Leave out on ${PAGE_NAMES[page]}`, disabled: entries.length === 0, items: [
        ...entries.map((e) => ({ text: e.label, ticked: randomExcluded.has(e.id), pick: () => setRandomExcluded((x) => { const n = new Set(x); n.has(e.id) ? n.delete(e.id) : n.add(e.id); return n; }) })),
        { separator: true },
        { text: 'Leave all out', pick: () => setRandomExcluded((x) => new Set([...x, ...entries.map((e) => e.id)])) },
        { text: 'Include all', pick: () => setRandomExcluded((x) => new Set([...x].filter((id) => !entries.some((e) => e.id === id)))) },
      ] },
      { separator: true },
      { text: `Reset ${PAGE_NAMES[page]}${page === 8 ? ` ${modShown + 1}` : ''}`, pick: () => resetPages(page) },
      { text: 'Reset all pages', pick: () => resetPages(-1) },
    ] });
  };

  // --- presets (Presets.cpp, kit::PresetLibrary): a sound without the sample ------------
  const capturePreset = (name, category) => ({
    name, category,
    values: Object.fromEntries(data.params.filter((p) => p.inPresets && p.builds !== 'fx').map((p) => [p.id, silt.getParam(p.index)])),
    routes: routesNow().map((rt) => [rt.m, rt.id, rt.amount, silt.routePolarity(rt.m, byId[rt.id].index)]),
    modShown,
  });
  const routesNow = () => targets.flatMap((t) => [0, 1, 2, 3].map((m) => ({ m, id: data.params[t].id, amount: silt.routeAmount(m, t) }))).filter((rt) => rt.amount !== 0);
  const applyPresetValues = (preset) => {
    for (const p of data.params)
      if (p.inPresets && p.builds !== 'fx') setParam(p.id, preset.values[p.id] ?? p.default);
    for (const t of targets) for (let m = 0; m < 4; m++) if (silt.routeAmount(m, t) !== 0 || silt.routePolarity(m, t) !== 0) setRoute(m, data.params[t].id, 0, 0);
    for (const [m, id, amount, polarity = 0] of preset.routes) if (byId[id]) setRoute(m, id, amount, polarity);
    setModShown(Math.max(0, Math.min(3, preset.modShown ?? 0)));
    setPresetName(preset.name);
    presetAsLoaded.current = snapshot();
  };
  const applyPreset = (preset) => step('PRESET', () => applyPresetValues(preset));
  const presetList = () => [
    ...data.presets.factory.map((f) => ({ ...f, factory: true })),
    ...[...userPresets].sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true })).map((u) => ({ ...u, factory: false })),
  ];
  const stepPreset = (direction) => {
    const list = presetList(), n = list.length;
    const at = list.findIndex((p) => p.name === presetName);
    applyPreset(list[((at < 0 ? (direction > 0 ? -1 : 0) : at) + direction + n) % n]);
  };
  const presetEdited = presetAsLoaded.current != null && !sameSound(presetAsLoaded.current, snapshot());
  const savePreset = (name, category) => {
    const preset = capturePreset(name, category);
    const next = [...userPresets.filter((u) => u.name !== name), preset];
    setUserPresets(next);
    writeStored('silt-presets-v1', next);
    setPresetName(name);
    presetAsLoaded.current = snapshot();
  };
  const setFavourite = (name, on) => {
    const next = new Set(favourites);
    on ? next.add(name) : next.delete(name);
    setFavourites(next);
    writeStored('silt-favourites-v1', [...next]);
  };

  const startStop = async () => {
    if (audioState === 'running') { await data.audio.stop(); return; }
    setAudioState('starting');
    try {
      await data.audio.start({
        params: data.params.map((p) => [p.index, silt.getParam(p.index)]),
        routes: targets.flatMap((t) => [0, 1, 2, 3].map((m) => [m, t, silt.routeAmount(m, t), silt.routePolarity(m, t)])).filter((r) => r[2] !== 0),
      });
      data.audio.transport(false, tempo);
      data.audio.midiOn();
    } catch (err) {
      setAudioState('off');
      showFlash(String(err.message ?? err).toUpperCase());
    }
  };
  // ARM: the bars at TEMPO (4/4, as a stopped host), up to 30 s; again to cancel (SiltProcessor::armCapture).
  const arm = async () => {
    if (capture) { data.audio.cancelCapture(); setCapture(null); return; }
    if (audioState !== 'running') await startStop();
    const bars = [1, 2, 4, 8][Math.max(0, Math.min(3, Math.round(value('captureBars'))))];
    const resample = value('captureFeed') > 0.5;
    data.audio.capture(Math.min(30, bars * 4 * 60 / Math.max(20, Math.min(400, tempo))), resample);
    setCapture({ progress: 0, resample });
  };
  capturedRef.current = (take) => {
    setCapture(null);
    if (!take) return;
    const time = new Date().toTimeString().slice(0, 8);
    installSample({ channels: take.channels, sampleRate: take.sampleRate }, `${take.resample ? 'RESAMPLE' : 'CAPTURE'} ${time}`, -1, null);
  };

  // The 8-knob controller (SiltProcessor::handleController, applyControllerMoves): its knobs move
  // the open page's eight slots, taking over once they reach the value; two buttons step pages.
  const slotParameter = (p, slot) => data.layout.instrument.find((e) => e.page === p && e.slot === slot
    && !['underButton', 'underMenu'].includes(e.kind) && isShown(e, context))?.id ?? '';
  const handleController = (number, cc) => {
    const ct = controller.current;
    if (learnStep >= 0) {
      if (ct.learned.slice(0, Math.min(learnStep, 8)).includes(number) || (learnStep === 9 && number === ct.previous)) return;
      if (learnStep < 8) { ct.knobs[learnStep] = number; ct.learned[learnStep] = number; }
      else if (learnStep === 8) ct.previous = number;
      else ct.next = number;
      writeStored('silt-controller-v1', { knobs: ct.knobs, previous: ct.previous, next: ct.next });
      setLearnStep(learnStep < 9 ? learnStep + 1 : -1);
      return;
    }
    if (number === ct.previous || number === ct.next) {
      const which = number === ct.next ? 1 : 0;
      if (cc > 63 && ct.pageLast[which] <= 63) {
        if (page === 4 && half < 3 && which === 1) setHalf(half + 1);
        else if (page === 4 && half > 0 && which === 0) setHalf(half - 1);
        else {
          const to = (page + (which ? 1 : 9)) % 10;
          if (to === 4) setHalf(which ? 0 : 3);
          setPage(to);
        }
      }
      ct.pageLast[which] = cc;
      return;
    }
    const slot = ct.knobs.indexOf(number);
    if (slot < 0) return;
    if (ct.pickupPage !== page) { ct.pickupPage = page; ct.picked = []; ct.last = []; }
    const id = slotParameter(page, slot);
    if (id !== ct.target[slot]) { ct.target[slot] = id; ct.picked[slot] = false; ct.last[slot] = -1; }
    if (!id) return;
    const last = ct.last[slot] ?? -1;
    if (armed >= 0 && page !== 8 && !id.startsWith('!')) {   // ASSIGN: the armed modulator's depth here, centre = none
      if (ui.isTarget(id)) mergedStep(`route${armed}${id}`, 'MODULATION', () => setRoute(armed, id, Math.max(-1, Math.min(1, (cc - 64) / 63))));
      ct.last[slot] = cc;
      return;
    }
    if (id === '!modSelect') { setModShown(Math.max(0, Math.min(3, Math.floor(cc * 4 / 128)))); ct.last[slot] = cc; return; }
    if (id === '!assign') { if (cc > 63 && last <= 63) setArmed((a) => (a >= 0 ? -1 : modShown)); ct.last[slot] = cc; return; }
    if (id === '!newPattern') { if (cc > 63 && last <= 63) ui.setParam('patternSeed', 1 + Math.floor(Math.random() * 9999)); ct.last[slot] = cc; return; }
    const p = byId[id];
    if (!p) return;
    const target = cc / 127;
    if (!ct.picked[slot]) {
      const current = silt.to01(p.index, value(id)), previous = last >= 0 ? last / 127 : target;
      ct.picked[slot] = Math.abs(target - current) < 0.04 || (previous - current) * (target - current) <= 0;
      ct.last[slot] = cc;
      if (!ct.picked[slot]) return;
    }
    ct.last[slot] = cc;
    mergedStep(`param${id}`, p.name, () => setParam(id, silt.from01(p.index, target)));
  };
  // Web MIDI: everything to the sound (notes, pedal, wheel, pressure, bend); controllers but the
  // pedal and all-notes-off also to the controller above.
  window.siltMidi = (bytes) => midiRef.current?.(bytes);   // the same path a MIDI device takes (tests use it)
  midiRef.current = (bytes) => {
    const [status, d1 = 0, d2 = 0] = bytes;
    const kind = status & 0xf0;
    if (kind === 0x90 && d2 > 0) noteOn(d1, d2);
    else if (kind === 0x80 || kind === 0x90) noteOff(d1);
    else data.audio.midi(bytes);
    if (kind === 0xb0 && d1 !== 64 && d1 !== 120 && d1 !== 123) handleController(d1, d2);
  };

  const toggleMic = async () => {
    if (micOn) { data.audio.micOff(); setMicOn(false); return; }
    try { await data.audio.micOn(); setMicOn(true); } catch (err) { showFlash(String(err.message ?? err).toUpperCase()); }
  };
  const changeTempo = (bpm) => { const b = Math.max(20, Math.min(400, Math.round(bpm * 10) / 10)); setTempo(b); data.audio.transport(false, b); };
  const loadMenu = () => setMenu({ r: g.load, items: [
    { text: 'Load file...', pick: () => fileInput.current?.click() },
    { header: true, text: 'FACTORY SAMPLES' },
    ...SAMPLES.map((smp, i) => ({ text: smp.name, ticked: sample?.factory === i, pick: () => loadSample({ factory: i }) })),
  ] });
  const showFlash = (message) => {
    setFlash(message);
    clearTimeout(showFlash.timer);
    showFlash.timer = setTimeout(() => setFlash(''), 2000);
  };
  showFlashRef.current = showFlash;
  const routeAmount = (m, id) => (byId[id] ? silt.routeAmount(m, byId[id].index) : 0);
  const setRoute = (m, id, amount, polarity = silt.routePolarity(m, byId[id].index)) => {
    silt.setRoute(m, byId[id].index, amount, polarity);
    window.siltAudio?.setRoute(m, byId[id].index, amount, polarity);
    setVersion((n) => n + 1);
  };
  // What a route moves its parameter over, in its own units (SiltProcessor::depthSpan, depthText).
  const depthSpan = (m, id) => {
    const amount = routeAmount(m, id), base = silt.to01(byId[id].index, value(id));
    const bipolar = silt.routeBipolar(m, byId[id].index);
    const lo = Math.max(0, Math.min(1, base + (bipolar ? -Math.abs(amount) : Math.min(0, amount))));
    const hi = Math.max(0, Math.min(1, base + (bipolar ? Math.abs(amount) : Math.max(0, amount))));
    const at = (p) => text(id, silt.from01(byId[id].index, p));
    return `${at(lo)} to ${at(hi)}`;
  };
  const depthText = (m, id) => {
    const percent = roundToInt(routeAmount(m, id) * 100);
    return `${depthSpan(m, id)}  (${percent > 0 ? '+' : ''}${percent}%)`;
  };
  const selectModulator = (m, arm) => {
    setModShown(m);
    if (arm) setArmed((a) => (a === m ? -1 : m));
  };
  const ui = {
    scale, editing,
    stageLeft: () => stage.current.getBoundingClientRect().left,
    stageTop: () => stage.current.getBoundingClientRect().top,
    to01: (id) => silt.to01(byId[id].index, value(id)),
    setFrom01: (id, p) => setParam(id, silt.from01(byId[id].index, p)),
    setParam: (id, v) => step(byId[id]?.name ?? id, () => setParam(id, v)),
    resetToDefault: (id) => step(byId[id].name, () => setParam(id, byId[id].default)),
    // As the plug-in reads typing: the parameter's own text to value (JUCE's getFloatValue,
    // getIntValue, a choice by name, on/off).
    typeValue: (id, typed) => {
      const p = byId[id];
      const t = typed.trim();
      let v;
      if (p.kind === 'choice') v = Math.max(0, p.choices.findIndex((c) => c === t));
      else if (p.kind === 'switch') v = /^(on|yes|true)$/i.test(t) ? 1 : 0;
      else if (p.kind === 'integer') v = parseInt(t, 10) || 0;
      else v = parseFloat(t) || 0;
      step(p.name, () => setParam(id, v));
    },
    setEditing, beginGesture: (id) => beginStep(byId[id].name), endGesture: () => endStep(),
    beginStep, endStep, step,
    openMenu: setMenu,
    armed, dropHover,
    depthSpan, paramName: (id) => byId[id].name,
    undo, redo, canUndo: h.undo.length > 0, canRedo: h.redo.length > 0,
    resetMenu,
    isTarget: (id) => !!byId[id] && silt.isTarget(byId[id].index),
    isStepped: (id) => byId[id]?.kind === 'integer' || byId[id]?.kind === 'choice',
    to01Of: (id, v) => silt.to01(byId[id].index, v),
    from01Of: (id, p) => silt.from01(byId[id].index, p),
    // Where modulation has it now, from the audio; before it runs, the knob's own place.
    modulated01: (id) => (display && display.values[byId[id].index] >= 0 ? display.values[byId[id].index] : silt.to01(byId[id].index, value(id))),
    routeAmount,
    routeBipolar: (m, id) => silt.routeBipolar(m, byId[id].index),
    modulatorOn: (m) => value(`m${m + 1}Enable`) > 0.5,
    // A new route gesture (SiltProcessor::beginRouteGesture): a route removed earlier starts natural.
    beginRouteGesture: () => silt.naturalWhereUnrouted(),
    setAmount: (m, id, amount) => {
      if (amount !== 0 && !silt.routeCanWork(m, byId[id].index)) { showFlash('PER GRAIN ONLY REACHES GRAIN SETTINGS'); return; }
      lastUsedMod.current = m;
      step('MODULATION', () => setRoute(m, id, amount));
    },
    togglePolarity: (m, id) => { silt.naturalWhereUnrouted(); step('MODULATION', () => setRoute(m, id, routeAmount(m, id), silt.routeBipolar(m, byId[id].index) ? 1 : 2)); },
    clearTarget: (id) => { silt.naturalWhereUnrouted(); step('MODULATION', () => { for (let m = 0; m < 4; m++) setRoute(m, id, 0); }); },
    // Which modulator an Alt-drag sets (SiltEditor::depthModulatorFor): the armed one; else the
    // only one routed here; else the one last used.
    depthModulator: (id) => {
      if (armed >= 0) return armed;
      const routed = [0, 1, 2, 3].filter((m) => routeAmount(m, id) !== 0);
      return routed.length === 1 ? routed[0] : lastUsedMod.current;
    },
    depthDragged: (m, id, dragging) => setDepthReadout(dragging ? `MOD ${m + 1} > ${byId[id].name.slice(0, 16).toUpperCase()}  ${depthText(m, id)}` : ''),
    setDropHover,
    dropChip: (m, id) => {
      if (!ui.isTarget(id) || routeAmount(m, id) !== 0) return;
      silt.naturalWhereUnrouted();
      ui.setAmount(m, id, 0.25);   // a musical starting depth
    },
    selectModulator,
    setPage: (p) => setPage(p),
    openModule: (m) => {
      if (page === 4 + m && m === 0) setHalf((h) => (h + 1) % 4);   // RES again: its next page
      setPage(4 + m);
    },
  };

  const context = { value, modShown, resonatorHalf: half, delayForced: false };

  // The region as it will play, snapped (SiltProcessor::shownRegion), and SLICE's starts
  // (sliceMarks: equal, at the sample's hits, by hand).
  const mode = Math.round(value('keyMode'));
  const regionKey = `${value('start')}|${value('length')}|${Math.round(value('snap'))}`;
  if (regionMemo.current.sample !== sample || regionMemo.current.key !== regionKey) {
    let reg = [value('start') * 0.01, value('length') * 0.01];
    if (sample) {
      const frames = sample.channels[0].length;
      const begin = reg[0] * (frames - 1), end = Math.min(frames, begin + Math.max(1, reg[1] * frames));
      const [b, e] = silt.snapRegion(begin, end, frames, Math.round(value('snap')), sample.analysis);
      reg = [b / frames, (e - b) / frames];
    }
    regionMemo.current = { sample, key: regionKey, region: reg };
  }
  const shownRegion = regionMemo.current.region;
  const sliceMarks = (() => {
    if (mode !== 3 || !sample) return [];
    if (Math.round(value('sliceMode')) === 2 && manualRef.current.length > 0) return manualRef.current;
    const [start, length] = shownRegion;
    if (Math.round(value('sliceMode')) === 1) {
      const frames = sample.channels[0].length, a = sample.analysis, nz = a[0], nt = a[1];
      const minimum = 256 / frames, end = start + length, marks = [start];
      for (let i = 0; i < nt; i++) {
        const at = a[3 + nz + i] / frames;
        if (at > marks[marks.length - 1] + minimum && at < end - minimum && marks.length < 128) marks.push(at);
      }
      return marks;
    }
    const count = Math.max(1, Math.min(128, Math.round(value('sliceCount'))));
    return Array.from({ length: count }, (_, i) => start + length * i / count);
  })();
  sliceMarksRef.current = sliceMarks;
  // SNAP to hits: the transients it can land on, ticked along the waveform's top.
  if (transientMemo.current.sample !== sample) {
    const a = sample?.analysis, frames = sample?.channels[0].length;
    transientMemo.current = { sample, marks: sample ? Array.from({ length: a[1] }, (_, i) => a[3 + a[0] + i] / frames) : [] };
  }
  const transients = transientMemo.current.marks;
  const wordWidth = Math.ceil(measure('SILT', `${font.wordmark(48).fontSize}px "Glyph Grotesque"`)) + 4;
  const shapes = ['SINE', 'TRIANGLE', 'SAW', 'SQUARE'];
  // Every route, in target order then modulator (SiltProcessor::allRoutes).
  const routes = [];
  for (const p of data.params)
    if (silt.isTarget(p.index))
      for (let m = 0; m < 4; m++) {
        const amount = silt.routeAmount(m, p.index);
        if (amount !== 0) routes.push({ m, id: p.id, amount });
      }
  const routeCounts = [0, 1, 2, 3].map((m) => routes.filter((r) => r.m === m).length);
  const labelOf = (id) => data.layout.instrument.find((e) => e.id === id && e.label)?.label;
  const pageOfParam = (id) => { const e = data.layout.instrument.find((x) => x.id === id); return e ? PAGE_NAMES[e.page] : ''; };
  const modTitles = [0, 1, 2, 3].map((m) => {
    const first = routes.find((r) => r.m === m);
    if (first) return `TO ${labelOf(first.id) ?? byId[first.id].name.slice(0, 12).toUpperCase()}`;   // ModChip's chipTitle
    const type = Math.round(value(`m${m + 1}Type`));
    return type !== 0 ? TYPE_NAMES[type] : shapes[Math.max(0, Math.min(3, Math.round(value(`m${m + 1}Shape`))))];
  });

  // The readout: hover help first, then an armed modulator, then the standing figure (statusText).
  const helpLine = hover ? helpFor({ ...data.help, ...WEB_HELP }, hover) : '';
  const learnText = learnStep < 0 ? '' : learnStep < 8 ? `LEARN: TURN KNOB ${learnStep + 1}` : learnStep === 8 ? 'LEARN: PRESS PREVIOUS PAGE' : 'LEARN: PRESS NEXT PAGE';
  const captureText = capture ? `${capture.resample ? 'RESAMPLE ' : ''}REC ${Math.round(Math.max(0, display?.capture ?? 0) * 100)}%` : '';
  const status = depthReadout || flash || learnText || helpLine || captureText || (armed >= 0 ? `MOD ${armed + 1} ARMED: DRAG A KNOB'S OUTER RING` : statusFigure(micOn ? 'MIC' : null, display && micOn ? 20 * Math.log10(Math.max(1e-6, display.inputPeak)) : -100, tempo));

  const over = (e) => {
    const el = e.target.closest?.('[data-help]');
    setHover(el ? el.getAttribute('data-help') : null);
  };

  return (
    <UI.Provider value={ui}>
      <div className="frame" style={{ width: 1200 * scale, height: 820 * scale }}>
        <div ref={stage} className="stage" style={{ width: 1200, height: 820, transform: `scale(${scale})`, background: css(S.page) }}
             onPointerOver={over} onPointerLeave={() => setHover(null)}>
          {/* Masthead */}
          <At r={[g.header[0], g.header[1] + g.header[3] - 2, g.header[2], 2]} style={{ background: css(S.ink) }} />
          <At r={[g.header[0], g.header[1], 200, g.header[3] - 10]} style={{ ...font.wordmark(48), color: css(S.ink), display: 'flex', alignItems: 'center' }}>
            SILT
          </At>
          <At r={[g.header[0] + wordWidth + 14, g.header[1] + 14, 400, 34]} style={{ ...font.name(15.5, 0.06), color: css(S.soft), lineHeight: '17px' }}>
            GLYPH AUDIO<br />GRANULAR SAMPLER
          </At>
          <div data-help="!preset">
            <PresetBar r={g.presets} name={`${presetName.toUpperCase()}${presetEdited ? ' *' : ''}`} onStep={stepPreset} onBrowse={() => setBrowserOpen(true)} onSave={() => setSaving(true)} />
          </div>
          <Key r={g.tour} text="?" help="!tour" />

          {/* Strip */}
          <Source g={g} onLoad={loadMenu} onArm={arm} capturing={!!capture} bars={Math.round(value('captureBars'))} onBars={(i) => ui.setParam('captureBars', i)}
                  resample={value('captureFeed') > 0.5} onResample={() => ui.setParam('captureFeed', value('captureFeed') > 0.5 ? 0 : 1)} monitor={value('monitor') > 0.5} onMonitor={() => ui.setParam('monitor', value('monitor') > 0.5 ? 0 : 1)} />
          <Chain g={g} value={value} openModule={page >= 4 && page <= 7 ? page - 4 : -1} half={half} chainOpen={page === 9} />
          <Readout r={g.lcd} title="SILT" status={status} />

          {/* The screen: the waveform, the grains, LIVE's ring (screen.jsx). */}
          <Screen r={g.screen} scale={scale} audio={data.audio} overview={overview} region={shownRegion} slices={sliceMarks}
                  gain={Math.pow(10, value('sampleGain') / 20)} live={mode === 1 || mode === 2} feed={mode === 2} frozen={value('freeze') > 0.5}
                  fontPx={screenFont} transients={Math.round(value('snap')) > 1.5 ? transients : null}
                  scope={SCOPE_MODULE[page] !== undefined ? MODULE_NAMES[SCOPE_MODULE[page]] : null}
                  zoomBox={[g.screen[0] + g.screen[2] - 30 - 200 - 78, g.screen[1] + 22, 78, 16]}
                  sliceEdits={{ begin: () => beginStep('SLICES'), end: endStep, ...sliceEdits,
                                add: (share) => step('SLICES', () => sliceEdits.add(share)), remove: (i) => step('SLICES', () => sliceEdits.remove(i)) }} />
          {(
            <GainBox r={[g.screen[0] + g.screen[2] - 30 - 200 - 78 - 130, g.screen[1] + 22, 130, 16]} text={`GAIN ${text('sampleGain', value('sampleGain')).toUpperCase()}`}
                     db={value('sampleGain')} set={(db) => setParam('sampleGain', db)} step={step} beginStep={beginStep} endStep={endStep}
                     normalise={() => { let peak = 0; for (const ch of sample?.channels ?? []) for (let i = 0; i < ch.length; i++) peak = Math.max(peak, Math.abs(ch[i]));
                                        return peak > 1e-6 ? Math.max(-24, Math.min(24, -1 - 20 * Math.log10(peak))) : 0; }}
                     openMenu={setMenu} />
          )}

          {/* The grip between the screen and the keys (screenDivider): three short dashes. */}
          <div data-help="!grip">
            {[-1, 0, 1].map((i) => (
              <At key={i} r={[g.well[0] + g.well[2] / 2 + i * 10 - 3, g.screen[1] + g.screen[3] + 3 + 3 - 1 + 0.5, 6, 2]} style={{ background: css(C.inkSoft) }} />
            ))}
          </div>

          {/* Key row */}
          {[0, 1, 2, 3, 8].map((p) => (
            <React.Fragment key={p}>
              <Key r={g.pageKeys[p]} text={PAGE_NAMES[p]} on={p === page && !routesOpen} tint={p === page ? mix(pageColour(p), S.panel, 0.25) : pageColour(p)} flat
                   onClick={() => { setPage(p); setRoutesOpen(false); }} onMenu={() => resetMenu(p, g.pageKeys[p])} help="!tab" />
              {p === page && !routesOpen && (
                <At r={[g.pageKeys[p][0] + 1, g.pageKeys[p][1] + g.pageKeys[p][3] - 4, g.pageKeys[p][2] - 2, g.well[1] - (g.pageKeys[p][1] + g.pageKeys[p][3]) + 5]}
                    style={{ background: css(pageColour(p)) }} />
              )}
            </React.Fragment>
          ))}
          <Key r={g.learn} text="LEARN" led={learnStep >= 0} help="!learn" onClick={() => { controller.current.learned = []; setLearnStep(learnStep >= 0 ? -1 : 0); }} />
          {learnStep >= 0 && learnStep < 8 && (
            // While learning, the slot whose knob to turn.
            <At r={[g.slots[learnStep].column[0] + 2, g.slots[learnStep].column[1] + 2, g.slots[learnStep].column[2] - 4, g.slots[learnStep].column[3] - 4]}
                style={{ border: `3px solid ${css(S.accent)}`, pointerEvents: 'none', zIndex: 3 }} />
          )}
          <Key r={g.random} text="RANDOM" help="!random" onClick={randomise} onMenu={randomMenu} />
          <AmountBox r={g.amount} amount={randomAmount} set={setRandomAmount} />
          <Key r={g.routes} text="MOD ROUTES" help="!routes" on={routesOpen} onClick={() => setRoutesOpen(!routesOpen)} />
          <Key r={g.undo} text="UNDO" help="!undo" onClick={undo} disabled={h.undo.length === 0 && !h.pending} />
          <Key r={g.redo} text="REDO" help="!redo" onClick={redo} disabled={h.redo.length === 0} />

          {/* Well */}
          <Well traces={traces.current} g={g} page={page} entries={data.layout.instrument} context={context} value={value} text={text} params={byId}
                grainCurve={silt.grainCurve} modTitles={modTitles} armed={armed} routeCounts={routeCounts} routesOpen={routesOpen}
                routeList={<RouteList r={[g.controlRow[0], g.controlRow[1] + 2, g.controlRow[2], g.controlRow[3] - 4]} routes={routes}
                                      pageName={pageOfParam} currentPage={PAGE_NAMES[page]} close={() => setRoutesOpen(false)} />} />

          {/* The browser's row, where the plug-in has the Bench: audio, the microphone, tempo, keys. */}
          <BrowserRow r={g.bench} state={audioState} onStart={startStop} mic={micOn} onMic={toggleMic} tempo={tempo} onTempo={changeTempo}
                      held={held} noteOn={noteOn} noteOff={noteOff} />
          <input ref={fileInput} type="file" accept="audio/*" style={{ display: 'none' }}
                 onChange={(e) => { const f = e.target.files?.[0]; e.target.value = ''; if (f) loadSample({ file: f }).catch(() => showFlash("COULDN'T READ THAT FILE")); }} />

          {browserOpen && (
            <PresetBrowser r={[g.screen[0], g.screen[1], g.screen[2] + 3, g.well[1] + g.well[3] - g.screen[1] + 3]} list={presetList()} current={presetName}
                           categories={data.presets.categories} favourites={favourites} setFavourite={setFavourite}
                           begin={() => ({ sound: snapshot(), name: presetName, loaded: presetAsLoaded.current })}
                           audition={(preset) => applyPresetValues(preset)}
                           keep={(before) => { if (!sameSound(before.sound, snapshot())) { h.undo.push({ name: 'PRESET', before: before.sound, after: snapshot() }); h.redo = []; } setBrowserOpen(false); }}
                           cancel={(before) => { restore(before.sound); setPresetName(before.name); presetAsLoaded.current = before.loaded; setBrowserOpen(false); }}
                           saveAs={() => setSaving(true)} />
          )}
          {saving && <SaveDialog categories={data.presets.categories} start={presetName === 'Init' ? '' : presetName}
                                 save={(name, category) => { savePreset(name, category); setSaving(false); }} cancel={() => setSaving(false)} />}
          {menu && <Popup menu={menu} close={() => setMenu(null)} />}
        </div>
      </div>
    </UI.Provider>
  );
}

createRoot(document.getElementById('root')).render(<App />);
