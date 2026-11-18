// The tour (plugin/Tour.cpp): a card at a time, each lighting a few parts of the panel. The rest
// dims and doesn't take clicks; the lit parts stay live, so you try things as the card says, and
// it ticks once you have. It never moves on by itself: NEXT, BACK and SKIP TOUR (or the arrow
// keys, Return and Esc) do. It opens by itself on a first visit, and from ? after.
// Also here: IN THE PLUG-IN, how this page and the plug-in differ.
import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { C, S, css, font, rgba } from './theme.js';

export const TOUR_SEEN = 'silt-tour-v1';
export function tourSeen() { try { return localStorage.getItem(TOUR_SEEN) === 'seen'; } catch { return true; } }
export function markTourSeen() { try { localStorage.setItem(TOUR_SEEN, 'seen'); } catch { /* private window */ } }

const CARD_W = 380, PAD = 18, BUTTON_ROW = 30, DONE_GREEN = 0x4f7a2c;
const grow = ([x, y, w, h], d) => [x - d, y - d, w + 2 * d, h + 2 * d];
const overlaps = (a, b) => a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
export const union = (rects) => {
  const x0 = Math.min(...rects.map((r) => r[0])), y0 = Math.min(...rects.map((r) => r[1]));
  return [x0, y0, Math.max(...rects.map((r) => r[0] + r[2])) - x0, Math.max(...rects.map((r) => r[1] + r[3])) - y0];
};

// Somewhere it covers none of the lit parts: below, above or beside one of them, else in a
// corner, else centred (Tour::cardArea).
function cardArea(spots, h, W, H) {
  const bounds = [12, 12, W - 24, H - 24];
  const w = Math.min(CARD_W, bounds[2]);
  const centred = [bounds[0] + Math.trunc((bounds[2] - w) / 2), bounds[1] + Math.trunc((bounds[3] - h) / 2), w, h];
  if (!spots.length) return centred;
  const candidates = [];
  for (const r of spots) {
    const x = r[0] + Math.trunc(r[2] / 2) - Math.trunc(w / 2), y = r[1] + Math.trunc(r[3] / 2) - Math.trunc(h / 2);
    candidates.push([x, r[1] + r[3] + 12, w, h], [x, r[1] - 12 - h, w, h], [r[0] + r[2] + 12, y, w, h], [r[0] - 12 - w, y, w, h]);
  }
  for (const [cx, cy] of [[bounds[0] + bounds[2], bounds[1] + bounds[3]], [bounds[0], bounds[1] + bounds[3]], [bounds[0] + bounds[2], bounds[1]], [bounds[0], bounds[1]]])
    candidates.push([cx - Math.trunc(w / 2), cy - Math.trunc(h / 2), w, h]);
  for (const c of candidates) {
    // constrainedWithin
    const card = [Math.max(bounds[0], Math.min(bounds[0] + bounds[2] - c[2], c[0])), Math.max(bounds[1], Math.min(bounds[1] + bounds[3] - c[3], c[1])), c[2], c[3]];
    if (!spots.some((s) => overlaps(s, card))) return card;
  }
  return centred;
}

// A key as the card's buttons are drawn (the kit's TextButton): a plate and a name.
function CardKey({ r, text, onClick, disabled }) {
  return (
    <div className={`at key tour-key${disabled ? ' disabled' : ''}`} onClick={disabled ? undefined : onClick}
         style={{ left: r[0], top: r[1], width: r[2], height: r[3] }}>
      <div className="at plate" style={{ left: 0, top: 0, width: r[2], height: r[3], borderRadius: 4, background: rgba(S.ink, disabled ? 0.04 : 0.08) }} />
      <div className="at" style={{ left: 0, top: 0, width: r[2], height: r[3], ...font.name(12), color: rgba(S.ink, disabled ? 0.35 : 1),
                                   display: 'flex', alignItems: 'center', justifyContent: 'center', paddingTop: 2 }}>{text}</div>
    </div>
  );
}

export function Tour({ steps, step, go, done, W = 1200, H = 820 }) {
  const s = steps[step];
  const last = step + 1 === steps.length;
  const spots = (s.spots ?? []).map((r) => grow(r, 6));
  const open = s.open ?? [];   // left open without a frame (the row under the panel, to play)
  const body = useRef(null);
  const [bodyH, setBodyH] = useState(120);
  // The card is as tall as its words need (laid out unscaled, as the panel is).
  useLayoutEffect(() => { if (body.current) setBodyH(Math.ceil(body.current.offsetHeight)); });
  const h = PAD + 24 + 6 + bodyH + 14 + BUTTON_ROW + PAD;
  const card = cardArea([...spots, ...open], h, W, H);
  const inner = [card[0] + PAD, card[1] + PAD, card[2] - 2 * PAD, card[3] - 2 * PAD];
  const buttons = [inner[0], inner[1] + inner[3] - (BUTTON_ROW - 4), inner[2], BUTTON_ROW - 4];

  // The arrow keys, Return and Esc, wherever the focus is (not while typing).
  useEffect(() => {
    const keys = (e) => {
      if (/INPUT|TEXTAREA/.test(document.activeElement?.tagName ?? '')) return;
      if (e.key === 'ArrowRight' || e.key === 'Enter') { e.preventDefault(); go(step + 1); }
      else if (e.key === 'ArrowLeft') { e.preventDefault(); go(step - 1); }
      else if (e.key === 'Escape') { e.preventDefault(); go(steps.length); }
    };
    window.addEventListener('keydown', keys, true);
    return () => window.removeEventListener('keydown', keys, true);
  }, [step, go, steps.length]);

  // The shade: everything but the lit parts (rounded holes; a hole takes no clicks, so the
  // lit parts stay live), and a lime frame round each.
  const round = ([x, y, w, hh], r = 8) => `M${x + r} ${y}H${x + w - r}A${r} ${r} 0 0 1 ${x + w} ${y + r}V${y + hh - r}A${r} ${r} 0 0 1 ${x + w - r} ${y + hh}H${x + r}A${r} ${r} 0 0 1 ${x} ${y + hh - r}V${y + r}A${r} ${r} 0 0 1 ${x + r} ${y}Z`;
  const shade = `M0 0H${W}V${H}H0Z${[...spots, ...open].map((r) => round(r)).join('')}`;
  return (
    <div className="tour" role="dialog" aria-label={`Tour: ${s.title}`}>
      <svg className="at tour-shade" width={W} height={H} viewBox={`0 0 ${W} ${H}`} style={{ left: 0, top: 0 }}>
        <path d={shade} fillRule="evenodd" fill={rgba(C.frame, 0.72)} />
        {spots.map((r, i) => <rect key={i} x={r[0]} y={r[1]} width={r[2]} height={r[3]} fill="none" stroke={css(C.screenLime)} strokeWidth={2.5} style={{ pointerEvents: 'none' }} />)}
      </svg>
      <div className="at" style={{ left: card[0] + 3, top: card[1] + 4, width: card[2], height: card[3], background: 'rgba(0,0,0,0.25)' }} />
      <div className="at tour-card" style={{ left: card[0], top: card[1], width: card[2], height: card[3], background: css(C.bone), boxShadow: `inset 0 0 0 2px ${css(C.ink)}` }} />
      <div className="at" style={{ left: inner[0], top: inner[1], width: inner[2] - 60, height: 24, ...font.name(17), color: css(C.rust), display: 'flex', alignItems: 'center', whiteSpace: 'nowrap' }}>
        {s.title.toUpperCase()}
      </div>
      <div className="at" style={{ left: inner[0] + inner[2] - 60, top: inner[1], width: 60, height: 24, ...font.screen(11 * 1.1), color: css(C.inkSoft), display: 'flex', alignItems: 'center', justifyContent: 'flex-end' }}>
        {step + 1} / {steps.length}
      </div>
      <div ref={body} className="at tour-body" style={{ left: inner[0], top: inner[1] + 24 + 6, width: inner[2], ...font.name(13.5), lineHeight: `${13.5 * 1.08 + 3}px`, color: css(C.ink), whiteSpace: 'pre-wrap' }}>
        {s.body}
        {s.tryIt && (
          <div style={{ marginTop: 8 * 1.08 + 3 }}>
            {done
              ? <span className="tour-done" style={{ ...font.name(13), color: css(DONE_GREEN) }}>{'✓'}  NICE. PRESS NEXT WHEN YOU'RE READY.</span>
              : <><span style={{ ...font.name(13), color: css(C.rust) }}>TRY IT   </span>{s.tryIt}</>}
          </div>
        )}
      </div>
      {!last && <CardKey r={[buttons[0], buttons[1], 100, buttons[3]]} text="SKIP TOUR" onClick={() => go(steps.length)} />}
      <CardKey r={[buttons[0] + buttons[2] - 78 - 6 - 70, buttons[1], 70, buttons[3]]} text="BACK" disabled={step === 0} onClick={() => go(step - 1)} />
      <CardKey r={[buttons[0] + buttons[2] - 78, buttons[1], 78, buttons[3]]} text={last ? 'DONE' : 'NEXT'} onClick={() => go(step + 1)} />
    </div>
  );
}

// How the plug-in and this page differ, both ways round (the owner's wording, 18 November 2026).
// An entry with no text is a heading.
export const PLUGIN_ONLY = [
  ['ONLY IN THE PLUG-IN', ''],
  ['THE BENCH', 'Passes takes between Silt and Brood in a DAW.'],
  ['DRAG', 'Drags the sample, or LIVE\'s last 8 seconds, into your DAW as audio.'],
  ['SIDECHAIN', 'ARM records a track from your DAW, and LIVE and FEED hear it. Here they hear the microphone.'],
  ['TEMPO', 'Follows the host, and ARM waits for the next bar while the host plays. Here you set it under the panel, and ARM starts at once.'],
  ['SILT FX', 'The effect version, for a track\'s own audio. Here LIVE with MIC does that job.'],
  ['AUTOMATION', 'The host can move every control, and keeps them with the session.'],
  ['THEMES', 'The ? button changes the colour theme.'],
  ['PRESETS', 'Kept as files that every host sees. Here they stay in this browser.'],
  ['ONLY ON THIS PAGE', ''],
  ['SOURCE', 'START AUDIO, MIC, TEMPO and the keys, where the plug-in has the Bench.'],
];
