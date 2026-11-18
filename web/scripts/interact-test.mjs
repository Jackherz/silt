// Drives the panel as a person would and checks what it shows.
//   node scripts/interact-test.mjs [url]
import { chromium } from 'playwright-core';

const url = process.argv[2] ?? 'http://localhost:5176/';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
});
const page = await browser.newPage({ viewport: { width: 1200, height: 820 } });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8);

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
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8);
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
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8);
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
await page.waitForFunction(() => document.querySelectorAll('.knob-hit').length >= 8);
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

check('no page errors', errors.length === 0, errors.join('; '));
await browser.close();
process.exitCode = failed ? 1 : 0;
