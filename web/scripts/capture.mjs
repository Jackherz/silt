// Captures the panel at the plug-in's size and 2× (as silt_snapshot renders),
// for comparing side by side with the snapshot PNGs.
//   node scripts/capture.mjs <url> <out.png>
import { chromium } from 'playwright-core';

const [url = 'http://localhost:5176/', out = '../build-web/panel.png'] = process.argv.slice(2);
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
});
const page = await browser.newPage({ viewport: { width: 1200, height: 820 }, deviceScaleFactor: 2 });
// The tour opens on a first visit; these runs start with it seen (the tour has its own checks).
await page.addInitScript(() => { if (!location.search.includes('tour=first')) localStorage.setItem('silt-tour-v1', 'seen'); });
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.at').length > 50);
await page.waitForTimeout(300);
await page.screenshot({ path: out });
await browser.close();
console.log(out);
