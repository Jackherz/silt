// The phone layout at work: a 390-wide touch screen. Starts audio, plays a key, moves the
// octave, turns a knob, opens a menu, and checks nothing runs off the side.
//   node scripts/phone-test.mjs [url]
import { chromium } from 'playwright-core';
const [url = 'http://localhost:5176/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser', args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
await page.addInitScript(() => localStorage.setItem('silt-tour-v1', 'seen'));
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
const results = [];
const check = (what, ok, detail = '') => { results.push(ok); console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${detail ? `: ${detail}` : ''}`); };
await page.goto(url);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'GUITAR-HARMONICS', null, { timeout: 15000 });
check('the phone layout, 390 wide', await page.evaluate(() => document.querySelector('.stage').style.width === '390px' && document.documentElement.classList.contains('phone')));
check('nothing runs off the side', await page.evaluate(() => document.scrollingElement.scrollWidth <= innerWidth + 1));
await page.getByText('START AUDIO', { exact: true }).tap();
await page.waitForFunction(() => window.siltAudio?.context?.state === 'running', null, { timeout: 5000 }).catch(() => {});
check('START AUDIO starts it', await page.evaluate(() => window.siltAudio?.context?.state === 'running'));
const key = page.locator('.piano').nth(12);   // the second octave's C
await key.scrollIntoViewIfNeeded();
const box = await key.boundingBox();
await page.mouse.move(box.x + box.width / 2, box.y + box.height - 6);
await page.mouse.down();
await page.waitForFunction(() => window.siltAudio.lastDisplay?.grains > 0, null, { timeout: 3000 }).catch(() => {});
check('a key plays', await page.evaluate(() => window.siltAudio.lastDisplay?.grains > 0));
await page.mouse.up();
await page.getByText('OCT +', { exact: true }).tap();
check('OCT + moves the keys up', (await page.locator('.piano span', { hasText: 'C3' }).count()) === 1);
const size = page.locator('.knob-hit').nth(1);
await size.scrollIntoViewIfNeeded();
const before = await page.evaluate(() => window.siltPanel.params.find((p) => p.id === 'grainSize') && document.querySelectorAll('.knob-hit').length);
const kb = await size.boundingBox();
const value = () => page.evaluate(() => [...document.querySelectorAll('.stage .at')].map((e) => e.textContent).find((t) => /\d+ ms$/.test(t)));
const was = await value();
await page.mouse.move(kb.x + kb.width / 2, kb.y + kb.height / 2);
await page.mouse.down();
await page.mouse.move(kb.x + kb.width / 2, kb.y + kb.height / 2 - 40, { steps: 5 });
await page.mouse.up();
check('a knob turns', (await value()) !== was, `${was} -> ${await value()}`);
void before;
await page.getByText('FREE', { exact: true }).tap();
check('a menu opens', (await page.locator('.popup-item').count()) > 0);
await page.locator('.popup-item').first().tap();
check('no page errors', errors.length === 0, errors.join('; '));
await browser.close();
process.exit(results.every(Boolean) ? 0 : 1);
