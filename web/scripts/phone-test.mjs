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
// Two fingers: one holds a key, the other turns SIZE. The note goes on sounding, the knob
// turns, and the page doesn't zoom (Chrome's touch input, as a phone sends it).
{
  const cdp = await page.context().newCDPSession(page);
  const keyBox = await page.locator('.piano').nth(14).boundingBox();
  const knob = page.locator('.knob-hit').nth(1);
  await knob.scrollIntoViewIfNeeded();
  const kb2 = await knob.boundingBox();
  const keyBox2 = await page.locator('.piano').nth(14).boundingBox();
  void keyBox;
  const keyPt = { x: keyBox2.x + keyBox2.width / 2, y: Math.min(keyBox2.y + keyBox2.height - 6, 840), id: 1 };
  const knobPt = { x: kb2.x + kb2.width / 2, y: kb2.y + kb2.height / 2, id: 2 };
  const sizeBefore = await value();
  const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map((p) => ({ x: p.x, y: p.y, id: p.id })) });
  await touch('touchStart', [keyPt]);
  await touch('touchStart', [keyPt, knobPt]);
  for (let i = 1; i <= 6; i++) await touch('touchMove', [keyPt, { ...knobPt, y: knobPt.y - i * 8 }]);
  const held = await page.evaluate(() => document.querySelectorAll('.piano[style*="232, 106, 44"], .piano[style*="#e86a2c"]').length);
  await touch('touchMove', [keyPt]);   // the knob finger lifts (a point left out is released; touchEnd lifts them all)
  await page.waitForTimeout(100);
  const stillHeld = await page.evaluate(() => document.querySelectorAll('.piano[style*="232, 106, 44"], .piano[style*="#e86a2c"]').length);
  await touch('touchEnd', []);
  const zoom = await page.evaluate(() => window.visualViewport?.scale ?? 1);
  check('two fingers: the key sounds while the knob turns', held === 1 && (await value()) !== sizeBefore, `held ${held}, ${sizeBefore} -> ${await value()}`);
  check('lifting the knob finger leaves the key sounding', stillHeld === 1, String(stillHeld));
  check('two fingers don\'t zoom the page', Math.abs(zoom - 1) < 0.01, String(zoom));
}
await page.getByText('FREE', { exact: true }).tap();
check('a menu opens', (await page.locator('.popup-item').count()) > 0);
await page.locator('.popup-item').first().tap();
check('no page errors', errors.length === 0, errors.join('; '));
await browser.close();
process.exit(results.every(Boolean) ? 0 : 1);
