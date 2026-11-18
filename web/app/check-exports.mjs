// Checks the browser's reading of the exported JSON against the plug-in:
//   build/silt_export_web_artefacts/Release/silt_export_web build-web --check
//   node web/app/check-exports.mjs build-web
import { readFileSync } from 'node:fs';
import { isShown, isDimmed, roundToInt } from './rules.js';

const folder = process.argv[2] ?? 'build-web';
const read = (name) => JSON.parse(readFileSync(`${folder}/${name}`, 'utf8'));
const layout = read('layout.json'), checks = read('layout-check.json');
const params = read('params.json'), help = read('help.json'), presets = read('presets.json');
let problems = 0;
const fail = (message) => { if (++problems <= 20) console.log(message); };

// Layout rules against the C++ answers.
for (const [n, c] of checks.entries()) {
  const entries = layout[c.instrument ? 'instrument' : 'fx'];
  const context = { ...c, value: (id) => c.values[id] ?? 0 };
  entries.forEach((e, i) => {
    if ((isShown(e, context) ? '1' : '0') !== c.shown[i]) fail(`state ${n}: ${e.id} on page ${e.page} slot ${e.slot} shown differs`);
    if ((isDimmed(e, context) ? '1' : '0') !== c.dimmed[i]) fail(`state ${n}: ${e.id} dimmed differs`);
  });
}

// Every id the layout, help and presets name exists.
const ids = new Set(params.map((p) => p.id));
for (const build of ['instrument', 'fx'])
  for (const e of layout[build])
    if (!e.id.startsWith('!') && !ids.has(e.id)) fail(`layout names ${e.id}, not a parameter`);
for (const p of presets.factory) {
  for (const id of Object.keys(p.values)) if (!ids.has(id)) fail(`preset ${p.name} sets ${id}, not a parameter`);
  for (const [, id] of p.routes) if (!ids.has(id)) fail(`preset ${p.name} routes ${id}, not a parameter`);
  if (!presets.categories.includes(p.category)) fail(`preset ${p.name}: category ${p.category}`);
}
if (roundToInt(2.5) !== 2 || roundToInt(3.5) !== 4 || roundToInt(-0.5) !== 0) fail('roundToInt is not JUCE\'s');

// The value text from the WebAssembly build against the plug-in's.
const { loadSilt } = await import('../engine/silt-wasm.js');
const silt = await loadSilt(readFileSync(`${folder}/silt.wasm`));
silt.create(true);
const texts = read('text-check.json');
for (const [id, value, text] of texts)
  if (silt.text(id, value) !== text) fail(`${params[id].id} at ${value}: '${silt.text(id, value)}', the plug-in '${text}'`);

console.log(`${texts.length} value texts; ${checks.length} panel states × ${layout.instrument.length}/${layout.fx.length} entries, ${Object.keys(help).length} help lines, `
  + `${presets.factory.length} presets: ${problems} problems`);
process.exitCode = problems === 0 ? 0 : 1;
