// The phone layout, whole: a 390-wide page at 2x, as an iPhone shows it.
//   node scripts/phone-shot.mjs [url] [out.png]
import { chromium } from 'playwright-core';
const [url = 'http://localhost:5176/', out = '../build-web/phone.png'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser' });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
await page.addInitScript(() => { if (!location.search.includes('tour=first')) localStorage.setItem('silt-tour-v1', 'seen'); });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto(url);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'GUITAR-HARMONICS', null, { timeout: 15000 });
await page.waitForTimeout(400);
await page.screenshot({ path: out, fullPage: true });
await browser.close();
console.log(out, errors.length ? `errors: ${errors.join('; ')}` : 'no errors');
