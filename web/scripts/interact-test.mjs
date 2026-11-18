// Drives the panel as a person would and checks what it shows.
//   node scripts/interact-test.mjs [url]
import { chromium } from 'playwright-core';

const url = process.argv[2] ?? 'http://localhost:5176/';
const browser = await chromium.launch({
  args: ['--autoplay-policy=no-user-gesture-required'],
  executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
});
const page = await browser.newPage({ viewport: { width: 1200, height: 820 } });
// (Audio: the browser is launched with autoplay allowed; see launch args.)
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8 && document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 10000 });

let failed = 0;
const check = (what, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${detail ? `: ${detail}` : ''}`);
  if (!ok) failed++;
};
const readout = () => page.evaluate(() => document.querySelector('[data-help]') && [...document.querySelectorAll('.at')].find((n) => n.style.left === '887px' && n.style.top === '114px')?.innerText);
const valueText = (i) => page.evaluate((i) => document.querySelectorAll('.knob-hit')[i].parentElement.innerText.trim(), i);

// Knob: drag up 125 px moves half the travel; Shift is ten times finer.
const knob = async (i) => (await page.locator('.knob-hit').nth(i).boundingBox());
let b = await knob(1);   // SIZE
const before = await valueText(1);
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
await page.mouse.down();
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2 - 60, { steps: 6 });
await page.mouse.up();
const after = await valueText(1);
check('dragging SIZE up raises it', before !== after, `${before} -> ${after}`);

await page.mouse.dblclick(b.x + b.width / 2, b.y + b.height / 2);
check('double-click returns SIZE to its default', (await valueText(1)) === '100 ms', await valueText(1));

// Typing a value.
b = await knob(2);   // RATE
await page.mouse.click(b.x + b.width / 2, b.y + b.height + 8);
await page.keyboard.press('Meta+a');
await page.keyboard.type('5');
await page.keyboard.press('Enter');
check('typing 5 into RATE', (await valueText(2)) === '5.00 Hz', await valueText(2));

// A menu under a slot: RATE MODE → GRAINS shows the GRAINS knob.
await page.getByText('FREE', { exact: true }).click();
await page.locator('.popup-item', { hasText: 'GRAINS' }).click();
check('RATE MODE → GRAINS shows GRAINS', (await page.getByText('8 grains').count()) === 1);

// Page keys and the chain.
await page.getByText('PLAY', { exact: true }).click();
check('PLAY opens its page', (await page.getByText('AT THE END').count()) === 1);
await page.getByText('FILTER', { exact: true }).first().click({ force: true });
check('the FILTER block opens FILTER', (await page.getByText('CUTOFF').count()) === 1);
await page.getByText('RES', { exact: true }).click({ force: true });
await page.getByText('RES 1/4').click({ force: true });
check('RES again: its second page', (await page.getByText('RES 2/4').count()) === 1 && (await page.getByText('RESONANCE').count()) === 1);

// Hover help on the readout, word for word.
await page.getByText('CUTOFF').count();
await page.getByText('MOD ROUTES').hover();
const help = await page.evaluate(() => document.body.innerText.replace(/\s+/g, ' ').includes('MOD ROUTES: every modulation connection'));
check('hover help reads the plug-in\'s line', help);

// Modulation, on a fresh page.
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8 && document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 10000 });
const routeCount = () => page.evaluate(() => document.querySelectorAll('.route-row').length);
// Alt-drag on SIZE: a route from MOD 1 (the last used, at first).
b = await knob(1);
await page.keyboard.down('Alt');
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
await page.mouse.down();
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2 - 30, { steps: 5 });
const readoutDuring = await page.evaluate(() => document.body.innerText.replace(/\s+/g, ' '));
await page.mouse.up();
await page.keyboard.up('Alt');
check('Alt-drag shows the depth on the readout', /MOD 1 > SIZE .* to .*\(\+20%\)/.test(readoutDuring), readoutDuring.match(/MOD 1 > [^)]*\)/)?.[0] ?? 'none');
check('the chip says where it goes', (await page.getByText('TO SIZE').count()) === 1);
// Drag chip 2 onto RATE: a +25% route.
const chip = await page.locator('.chip .plate').nth(1).boundingBox();
b = await knob(2);
await page.mouse.move(chip.x + 40, chip.y + 10);
await page.mouse.down();
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2, { steps: 8 });
await page.mouse.up();
check('a chip dropped on RATE routes it', (await page.getByText('TO RATE').count()) === 1);
// The list.
await page.getByText('MOD ROUTES', { exact: true }).first().click();
check('MOD ROUTES lists both', (await routeCount()) === 2, String(await routeCount()));
check('the dropped route starts at +25%', (await page.getByText('+25%').count()) === 1);
await page.getByText('M2', { exact: true }).first().click();
check('the M2 filter shows one', (await routeCount()) === 1);
await page.getByText('ALL', { exact: true }).click();
await page.getByText('CLEAR ALL').click();
await page.getByText('CLICK AGAIN').click();
check('CLEAR ALL, twice, clears them', (await routeCount()) === 0);

// RANDOM, UNDO and REDO.
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8 && document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 10000 });
const allValues = () => page.evaluate(() => [...document.querySelectorAll('.knob-hit')].map((k) => k.parentElement.innerText.trim()).join('|'));
const start = await allValues();
await page.getByText('RANDOM', { exact: true }).click();
const randomised = await allValues();
check('RANDOM moves GRAIN\'s knobs', randomised !== start);
check('RANDOM says what it did', /RANDOM GRAIN: \d+ CHANGES?/.test(await page.evaluate(() => document.body.innerText.replace(/\s+/g, ' '))));
await page.getByText('UNDO', { exact: true }).click();
check('UNDO puts them back', (await allValues()) === start);
await page.getByText('REDO', { exact: true }).click();
check('REDO brings RANDOM back', (await allValues()) === randomised);

// Presets: the arrows, the edited mark, the browser and SAVE.
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8 && document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 10000 });
const arrows = page.locator('[data-help="!preset"] .key .plate');
await arrows.nth(2).click({ force: true });   // next
check('the next arrow loads GLASS CLOUD', (await page.getByText('GLASS CLOUD', { exact: true }).count()) === 1);
check('its SIZE is 180 ms', (await valueText(1)) === '180 ms', await valueText(1));
b = await knob(0);
await page.mouse.dblclick(b.x + b.width / 2, b.y + b.height / 2);   // PITCH to default: no change, so not edited
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2); await page.mouse.down();
await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2 - 20, { steps: 3 }); await page.mouse.up();
check('a change marks it edited', (await page.getByText('GLASS CLOUD *', { exact: true }).count()) === 1);
await page.getByText('GLASS CLOUD *', { exact: true }).click();
check('the name opens the browser', (await page.locator('.preset-row').count()) === 42, String(await page.locator('.preset-row').count()));
await page.locator('.preset-row', { hasText: 'TAPE CHOIR' }).click();
check('a click plays a preset at once', (await valueText(1)) === '120 ms', await valueText(1));
await page.keyboard.press('Escape');
check('CANCEL puts the sound back', (await page.getByText('GLASS CLOUD *', { exact: true }).count()) === 1);
await page.getByText('SAVE', { exact: true }).click();
await page.keyboard.press('Meta+a');
await page.keyboard.type('My Sound');
await page.keyboard.press('Enter');
check('SAVE keeps it under its name', (await page.getByText('MY SOUND', { exact: true }).count()) === 1);
await page.getByText('MY SOUND', { exact: true }).click();
check('it lists as USER', (await page.locator('.preset-row', { hasText: 'MY SOUND' }).innerText()).includes('USER'));
await page.keyboard.press('Escape');

// Sound: START AUDIO, the keys, LOAD.
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8 && document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 10000 });
await page.getByText('START AUDIO', { exact: true }).click({ force: true });
await page.waitForFunction(() => window.siltAudio?.context?.state === 'running', null, { timeout: 5000 }).catch(() => {});
check('START AUDIO starts it', await page.evaluate(() => window.siltAudio?.context?.state === 'running'));
const grainsWhile = async (play) => page.evaluate(async (play) => {
  const seen = []; const port = window.siltAudio.node.port; const prev = port.onmessage;
  port.onmessage = (e) => { prev(e); if (e.data.type === 'display') seen.push(e.data.grains); };
  await window[play.start]?.();
  await new Promise((r) => setTimeout(r, 700));
  port.onmessage = prev;
  return Math.max(0, ...seen);
}, play);
const c3 = page.locator('.piano').nth(24);   // C3 (MIDI 60): the 25th key up from C1
const box3 = await c3.boundingBox();
await page.mouse.move(box3.x + box3.width / 2, box3.y + box3.height - 4);
await page.mouse.down();
const keyGrains = await grainsWhile({});
await page.mouse.up();
check('an on-screen key plays grains', keyGrains > 0, String(keyGrains));
await page.getByText('LOAD', { exact: true }).click();
await page.locator('.popup-item', { hasText: 'Keys Drone' }).click();
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'KEYS-DRONE', null, { timeout: 5000 }).catch(() => {});
check('LOAD → Keys Drone shows it', (await page.evaluate(() => document.querySelector('canvas[data-sample]')?.dataset.sample)) === 'KEYS-DRONE');
await page.getByText('PLAY', { exact: true }).click();
check('its ROOT is G2 (MIDI 55)', (await page.getByText('G2', { exact: true }).count()) === 1);
await page.getByText('GRAIN', { exact: true }).click();
await page.keyboard.down('a');
const typedGrains = await grainsWhile({});
await page.keyboard.up('a');
check('the computer\'s A key plays grains', typedGrains > 0, String(typedGrains));

// RESAMPLE: a note held, one bar at 120 BPM (2 s) of Silt's own output becomes the sample.
await page.getByText('RESAMPLE', { exact: true }).click({ force: true });
await page.getByText('2 BARS', { exact: true }).click();
await page.locator('.popup-item', { hasText: '1 BAR' }).first().click();
await page.keyboard.down('a');
await page.getByText('ARM', { exact: true }).click({ force: true });
await page.mouse.move(600, 20);   // off ARM: its hover help outranks the progress, as in the plug-in
await page.waitForTimeout(600);
const during = await page.evaluate(() => document.body.innerText.replace(/\s+/g, ' '));
check('RESAMPLE shows its progress', /RESAMPLE REC \d+%/.test(during));
await page.waitForFunction(() => /RESAMPLE-\d\d:\d\d:\d\d/.test(document.querySelector('canvas[data-sample]')?.dataset.sample ?? ''), null, { timeout: 6000 }).catch(() => {});
await page.keyboard.up('a');
check('the take becomes the sample', /RESAMPLE-\d\d:\d\d:\d\d/.test(await page.evaluate(() => document.querySelector('canvas[data-sample]')?.dataset.sample ?? '')));

// MIDI: a note plays; LEARN takes a knob; the controller picks SIZE up only when it reaches it.
const midiGrains = await page.evaluate(async () => {
  const seen = []; const port = window.siltAudio.node.port; const prev = port.onmessage;
  port.onmessage = (e) => { prev(e); if (e.data.type === 'display') seen.push(e.data.grains); };
  window.siltMidi([0x90, 64, 100]);
  await new Promise((r) => setTimeout(r, 600));
  window.siltMidi([0x80, 64, 0]);
  port.onmessage = prev;
  return Math.max(0, ...seen);
});
check('a MIDI note plays grains', midiGrains > 0, String(midiGrains));
await page.getByText('LEARN', { exact: true }).click({ force: true });
check('LEARN asks for knob 1', await page.evaluate(() => document.body.innerText.includes('LEARN: TURN KNOB 1')));
await page.evaluate(() => window.siltMidi([0xb0, 21, 10]));
await page.waitForTimeout(50);
check('a CC moves LEARN on to knob 2', await page.evaluate(() => document.body.innerText.includes('LEARN: TURN KNOB 2')));
await page.getByText('LEARN', { exact: true }).click({ force: true });
const sizeBefore = await valueText(1);
await page.evaluate(() => window.siltMidi([0xb0, 71, 127]));   // knob 2's default CC, far from SIZE: no pickup yet
await page.waitForTimeout(50);
check('a far-off controller knob doesn\'t jump SIZE', (await valueText(1)) === sizeBefore, await valueText(1));
await page.evaluate(() => window.siltMidi([0xb0, 71, 0]));
await page.waitForTimeout(50);
await page.evaluate(() => window.siltMidi([0xb0, 71, 90]));   // crosses it: picked up
await page.waitForTimeout(50);
check('crossing the value takes SIZE over', (await valueText(1)) !== sizeBefore, `${sizeBefore} -> ${await valueText(1)}`);

// Samples undo: LOAD is a step (with ROOT); five loads, and only the last four undo.
await page.goto(url);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 8000 });
const loadFactory = async (name) => {
  await page.getByText('LOAD', { exact: true }).click();
  await page.locator('.popup-item', { hasText: name }).click();
  await page.waitForFunction((n) => document.querySelector('canvas[data-sample]')?.dataset.sample === n, name.toUpperCase().replace(/ /g, '-'), { timeout: 8000 });
};
await loadFactory('Keys Drone');
await page.getByText('UNDO', { exact: true }).click();
check('UNDO after LOAD brings the sample back', await page.evaluate(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION'));
await page.getByText('PLAY', { exact: true }).click();
check('and its ROOT (E3)', (await page.getByText('E3', { exact: true }).count()) === 1);
await page.getByText('REDO', { exact: true }).click();
check('REDO loads it again', await page.evaluate(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'KEYS-DRONE') && (await page.getByText('G2', { exact: true }).count()) === 1);
for (const n of ['Rain Bells', 'Guitar Harmonics', 'Ahhh Progression', 'Keys Drone']) await loadFactory(n);   // five loads in all
let undos = 0;
while (undos < 10 && !(await page.locator('.key.disabled', { hasText: 'UNDO' }).count())) { await page.getByText('UNDO', { exact: true }).click(); undos++; }
check('five loads: four undo', undos === 4, String(undos));
check('back to the first kept: Keys Drone', await page.evaluate(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'KEYS-DRONE'));

// The screen: SLICE's lines (add, remove, drag, undo), the wheel's zoom, the scope.
await page.goto(url);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 8000 });
await page.getByText('INIT', { exact: true }).click();
await page.locator('.preset-row', { hasText: 'STUTTER GRID' }).click();
await page.keyboard.press('Enter');
const screen = page.locator('canvas[data-sample]');
const slicesNow = async () => (await screen.getAttribute('data-slices')).split(',').filter(Boolean).map(Number);
check('STUTTER GRID shows 16 slices', (await slicesNow()).length === 16, String((await slicesNow()).length));
const sb = await screen.boundingBox();
const W = await screen.evaluate((c) => parseFloat(c.style.width)), k = sb.width / W;
const waveX = (share) => sb.x + (16 + share * (W - 32)) * k, waveY = sb.y + 80 * k;
await page.mouse.dblclick(waveX(1 / 32), waveY);
check('a double-click on the waveform adds a slice', (await slicesNow()).length === 17, String((await slicesNow()).length));
await page.mouse.dblclick(waveX(4 / 16), waveY);
check('a double-click on a line removes it', (await slicesNow()).length === 16 && !(await slicesNow()).some((m) => Math.abs(m - 0.25) < 0.001), String(await slicesNow()));
const slicesBefore = await slicesNow();
await page.mouse.move(waveX(8 / 16), waveY); await page.mouse.down();
await page.mouse.move(waveX(8 / 16) + 40 * k, waveY, { steps: 4 }); await page.mouse.up();
const moved = await slicesNow();
check('a line drags', moved.length === 16 && moved.some((m, i) => Math.abs(m - slicesBefore[i]) > 0.01), String(moved.slice(6, 10)));
await page.getByText('UNDO', { exact: true }).click();
check('UNDO puts the dragged line back', (await slicesNow()).join() === slicesBefore.join(), String((await slicesNow()).slice(6, 10)));
await page.getByText('UNDO', { exact: true }).click();
await page.getByText('UNDO', { exact: true }).click();
check('UNDO twice more: 16 equal slices again', (await slicesNow()).length === 16 && Math.abs((await slicesNow())[4] - 0.25) < 0.001, String(await slicesNow()));
await page.mouse.move(waveX(0.5), waveY);
await page.mouse.wheel(0, -60);
await page.waitForTimeout(50);
const zoomed = Number(await screen.getAttribute('data-zoom'));
check('scrolling up on the waveform zooms in', zoomed > 1.05, String(zoomed));
check('ZOOM reads it', (await page.locator('.zoom-box').innerText()).startsWith('ZOOM '), await page.locator('.zoom-box').innerText());
await page.locator('.zoom-box').dblclick();
check('a double-click on ZOOM shows it all', Number(await screen.getAttribute('data-zoom')) === 1, await screen.getAttribute('data-zoom'));
await page.goto(`${url}?page=5`);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 8000 });
await page.getByText('START AUDIO', { exact: true }).click({ force: true });
await page.waitForFunction(() => window.siltAudio?.lastScope?.input?.length === 4096, null, { timeout: 5000 }).catch(() => {});
check('FILTER\'s page: the scope reads the module', await page.evaluate(() => window.siltAudio?.lastScope?.input?.length === 4096));

// The corner pictures: each page with one draws it, while a note plays, without an error.
for (const [n, name] of [[2, 'PITCH'], [4, 'RESONATOR'], [5, 'FILTER'], [6, 'COLOR'], [7, 'SPACE'], [8, 'MOD']]) {
  await page.goto(`${url}?page=${n}`);
  await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION', null, { timeout: 8000 });
  await page.getByText('START AUDIO', { exact: true }).click({ force: true });
  await page.waitForFunction(() => window.siltAudio?.lastDisplay, null, { timeout: 5000 }).catch(() => {});
  await page.evaluate(() => window.siltAudio.noteOn(60));
  await page.waitForTimeout(400);
  // The picture's ground (deepBack) fills its frame, bottom right of the screen.
  const ground = await page.evaluate(() => {
    const c = document.querySelector('canvas[data-sample]'), k = c.width / parseFloat(c.style.width);
    const w = parseFloat(c.style.width), h = parseFloat(c.style.height);
    const top = 12 + (h - 24) - (Math.round((h - 24 - 36) * 0.36) + 20);   // insetArea's top: by the title, right of it
    const d = c.getContext('2d').getImageData(Math.round((w - 30) * k), Math.round((top + 5) * k), 1, 1).data;
    return [d[0], d[1], d[2]];
  });
  const errorsBefore = errors.length;
  check(`${name}'s corner picture draws`, ground[0] < 20 && ground[1] < 25 && ground[2] < 25 && errors.length === errorsBefore, ground.join());
}

check('no page errors', errors.length === 0, errors.join('; '));
await browser.close();
process.exitCode = failed ? 1 : 0;
