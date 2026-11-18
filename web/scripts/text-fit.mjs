// The text-fit check (the owner's rule: nothing on the screen or readout is ever cut off).
// Walks the panel through every page and view, every knob at its lowest and highest, every
// help line in the readout, every preset name and each tour card, and measures each piece of
// text where the browser actually drew it against the box it lives in. Then it proves itself:
// one label made too long on purpose must be caught.
//   node scripts/text-fit.mjs [url]
// The canvas screen's own lines are drawn and sized as the plug-in draws them; this checks the
// panel round it.
import { chromium } from 'playwright-core';

const [url = 'http://localhost:5176/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser' });
const page = await browser.newPage({ viewport: { width: 1200, height: 820 } });
await page.addInitScript(() => localStorage.setItem('silt-tour-v1', 'seen'));
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto(url);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION' && window.siltPanel);
await page.evaluate(() => document.fonts.ready);

// In the page: every text's drawn rectangles against its box (the nearest positioned .at with a
// size). Across, a tenth of a pixel over is cut off. Down, the glyphs' own height may pass a
// tight box a little (as in the plug-in) but not clip: only boxes that hide what overflows
// count, and lists that scroll count across only.
await page.addScriptTag({ content: `
window.fitProblems = (where) => {
  const stage = document.querySelector('.stage');
  const k = stage.getBoundingClientRect().width / 1200;
  const out = [];
  const walker = document.createTreeWalker(stage, NodeFilter.SHOW_TEXT);
  for (let n; (n = walker.nextNode());) {
    const text = n.textContent.trim();
    if (!text) continue;
    const el = n.parentElement;
    const style = getComputedStyle(el);
    if (style.visibility === 'hidden' || style.display === 'none') continue;
    if (el.closest('[aria-hidden="true"]')) continue;
    let box = el.closest('.at');
    while (box && (box.getBoundingClientRect().width < 1 || box.getBoundingClientRect().height < 1)) box = box.parentElement?.closest('.at');
    if (!box) continue;
    const b = box.getBoundingClientRect();
    if (getComputedStyle(box).opacity === '0') continue;
    const scrolls = !!el.closest('.plugin-list, .preset-list, .route-rows');
    const range = document.createRange();
    range.selectNodeContents(n);
    for (const r of range.getClientRects()) {
      if (r.width < 0.5) continue;
      const over = Math.max(b.left - r.left, r.right - b.right) / k;
      const clips = !scrolls && getComputedStyle(box).overflow !== 'visible';
      const down = Math.max(b.top - r.top, r.bottom - b.bottom) / k;
      if (over > 0.1 || (clips && down > 0.5))
        out.push({ where, text: text.slice(0, 60), over: +over.toFixed(1), down: +down.toFixed(1), box: box.className });
    }
  }
  return out;
};`});

const problems = [];
const measure = async (where) => { await page.waitForTimeout(40); problems.push(...(await page.evaluate((w) => window.fitProblems(w), where))); };
const set = (id, v) => page.evaluate(([i, x]) => window.siltPanel.setParam(i, x), [id, v]);
let states = 0;

// Every page, RESONATOR's quarters, PLAY's modes and MOD's types; on each, every control at its
// lowest and highest (its value box and anything the change shows).
const params = await page.evaluate(() => window.siltPanel.params.map((p) => ({ id: p.id, low: p.low, high: p.high, page: p.page, kind: p.kind })));
const views = [];
for (let p = 0; p < 10; p++) views.push({ page: p });
for (let h = 1; h < 4; h++) views.push({ page: 4, half: h });
for (let m = 0; m < 4; m++) views.push({ page: 3, set: [['keyMode', m]] });
for (let t = 0; t < 7; t++) views.push({ page: 8, set: [['m1Type', t]] });
for (const v of views) {
  await page.evaluate((x) => { window.siltPanel.setPage(x.page); window.siltPanel.setHalf(x.half ?? 0); }, v);
  for (const [id, value] of v.set ?? []) await set(id, value);
  const label = `page ${v.page}${v.half ? ` half ${v.half}` : ''}${v.set ? ` ${v.set.map(([a, b]) => `${a}=${b}`).join()}` : ''}`;
  await measure(label); states++;
  // The knobs and menus this view shows: lowest, highest, then back.
  const shown = await page.evaluate(() => [...document.querySelectorAll('.stage [data-help]')].map((e) => e.dataset.help).filter((h) => !h.startsWith('!')));
  for (const id of new Set(shown)) {
    const p = params.find((q) => q.id === id);
    if (!p) continue;
    for (const value of [p.low, p.high]) { await set(id, value); await measure(`${label} ${id}=${value}`); states++; }
    await page.evaluate((i) => { const q = window.siltPanel.params.find((x) => x.id === i); window.siltPanel.setParam(i, q.default); }, id);
  }
  for (const [id] of v.set ?? []) await page.evaluate((i) => { const q = window.siltPanel.params.find((x) => x.id === i); window.siltPanel.setParam(i, q.default); }, id);
}
await page.evaluate(() => window.siltPanel.setPage(0));

// Every help line, as the readout shows it on hover.
const helpKeys = await page.evaluate(() => window.siltPanel.helpKeys());
for (const key of helpKeys) {
  await page.evaluate((k) => window.siltPanel.setHover(k), key);
  await measure(`help ${key}`); states++;
}
await page.evaluate(() => window.siltPanel.setHover(null));

// Every preset's name in the preset bar.
const names = [];
await page.getByText('INIT', { exact: true }).click();
const presetNames = await page.evaluate(() => [...document.querySelectorAll('.preset-row')].map((r) => r.innerText.split('\n')[0]));
await page.keyboard.press('Escape');
for (const name of [...names, ...presetNames]) {
  if (!name) continue;
  await page.evaluate((n) => window.siltPanel.setPresetName(n), name);
  await measure(`preset ${name}`); states++;
}
await page.evaluate(() => window.siltPanel.setPresetName('Init'));

// The tour's cards and IN THE PLUG-IN.
await page.getByText('?', { exact: true }).click();
for (let i = 0; i < 9; i++) {
  await measure(`tour ${i + 1}`); states++;
  await page.getByText(i === 8 ? 'DONE' : 'NEXT', { exact: true }).click();
}
await page.getByText('IN THE PLUG-IN', { exact: true }).click();
await measure('in the plug-in'); states++;
await page.keyboard.press('Escape');

// The proof: a label made too long on purpose must be caught.
await page.evaluate(() => {
  const label = [...document.querySelectorAll('.stage .key')].find((k) => k.textContent.trim() === 'LEARN');
  const span = label.querySelector('span') ?? label;
  span.textContent = 'LEARN THE WHOLE CONTROLLER NOW';
  span.style.transform = 'none';
});
const forced = await page.evaluate(() => window.fitProblems('forced'));
const caught = forced.some((p) => p.text.startsWith('LEARN THE WHOLE'));

await browser.close();
const seen = new Map();
for (const p of problems) { const key = `${p.text}|${p.box}`; if (!seen.has(key)) seen.set(key, p); }
for (const p of seen.values()) console.log(`CUT  ${p.where}: "${p.text}" over ${p.over} px across, ${p.down} px down (${p.box})`);
console.log(`${states} states measured; ${seen.size} cut off; the forced overflow was ${caught ? 'caught' : 'MISSED'}${errors.length ? `; page errors: ${errors.join('; ')}` : ''}`);
process.exit(seen.size === 0 && caught && errors.length === 0 ? 0 : 1);
