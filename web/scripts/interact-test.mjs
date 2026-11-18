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

check('no page errors', errors.length === 0, errors.join('; '));
await browser.close();
process.exitCode = failed ? 1 : 0;
