// The panel's show and dim rules from layout.json, evaluated exactly as the
// plug-in's silt::layout::Rule does (plugin/PageLayout.cpp).
//
// context: { value(id) → number, modShown, resonatorHalf, delayForced }

// JUCE's roundToInt: to nearest, halves to even.
export function roundToInt(v) {
  const r = Math.round(v);
  return Math.abs(v % 1) === 0.5 && r % 2 !== 0 ? r - 1 : r;
}

// Single-precision, as the plug-in compares (e.g. a SHAPE ≥ 2.99).
const f32 = Math.fround;

export function evaluate(rule, c) {
  if (rule == null) return true;
  const value = (id) => f32(c.value(id.startsWith('@') ? `m${c.modShown + 1}${id.slice(1)}` : id));
  if ('all' in rule) return rule.all.every((r) => evaluate(r, c));
  if ('any' in rule) return rule.any.some((r) => evaluate(r, c));
  if ('not' in rule) return !evaluate(rule.not, c);
  if ('is' in rule) return roundToInt(value(rule.is)) === rule.value;
  if ('in' in rule) return rule.values.includes(roundToInt(value(rule.in)));
  if ('on' in rule) return value(rule.on) > 0.5;
  if ('off' in rule) return value(rule.off) <= 0.5;
  if ('atLeast' in rule) return value(rule.atLeast) >= f32(rule.value);
  if ('modShown' in rule) return c.modShown === rule.modShown;
  if ('resonatorHalf' in rule) return c.resonatorHalf === rule.resonatorHalf;
  if ('delayForced' in rule) return !!c.delayForced;
  throw new Error(`unknown rule ${JSON.stringify(rule)}`);
}

export const isShown = (entry, c) => evaluate(entry.shown, c);
export const isDimmed = (entry, c) => entry.dimmed != null && evaluate(entry.dimmed, c);

// The help line for an id, as plugin/Help.h's helpFor: a modulator's setting
// (m1Rate) falls back to its shared line (mod:Rate).
export function helpFor(help, id) {
  if (id in help) return help[id];
  if (id.length > 2 && id[0] === 'm' && /\d/.test(id[1]) && `mod:${id.slice(2)}` in help) return help[`mod:${id.slice(2)}`];
  return '';
}
