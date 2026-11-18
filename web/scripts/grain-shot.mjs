// Holds a note and photographs the screen, for comparing the grain field with the plug-in's.
//   node scripts/grain-shot.mjs [url] [out.png]
import { chromium } from 'playwright-core';
const [url = 'http://localhost:5176/', out = '../build-web/grain-live.png'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
  args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1200, height: 820 }, deviceScaleFactor: 2 });
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.at').length > 50);
await page.getByText('START AUDIO').click();
await page.waitForFunction(() => window.siltAudio?.lastDisplay);
await page.evaluate(() => window.siltAudio.noteOn(60));
await page.waitForTimeout(1500);
const grains = await page.evaluate(() => window.siltAudio.lastDisplay.grains);
await page.screenshot({ path: out, clip: { x: 20, y: 150, width: 1160, height: 360 } });
await browser.close();
console.log(out, 'grains', grains);
