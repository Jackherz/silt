// Holds a note and photographs the screen, for comparing the grain field with the plug-in's.
//   node scripts/grain-shot.mjs [url] [out.png] [zoom notches] [preset] [hold ms]
import { chromium } from 'playwright-core';
const [url = 'http://localhost:5176/', out = '../build-web/grain-live.png', notches = '0', preset = '', hold = '1500'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
  args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1200, height: 820 }, deviceScaleFactor: 2 });
await page.goto(url);
await page.waitForFunction(() => document.querySelectorAll('.at').length > 50);
if (preset) {
  await page.getByText('INIT', { exact: true }).click();
  await page.locator('.preset-row', { hasText: preset.toUpperCase() }).click();
  await page.keyboard.press('Enter');
}
await page.getByText('START AUDIO').click();
await page.waitForFunction(() => window.siltAudio?.lastDisplay);
const sb = await page.locator('canvas[data-sample]').boundingBox();
await page.mouse.move(sb.x + sb.width * 0.3, sb.y + 80);
for (let i = 0; i < Number(notches); i++) { await page.mouse.wheel(0, -120); await page.waitForTimeout(20); }
await page.evaluate(() => window.siltAudio.noteOn(60));
await page.waitForTimeout(Number(hold));
if (process.env.SECOND_NOTE) {   // a second note: SPACE's light echoes flare on a new note
  await page.evaluate(() => { window.siltAudio.noteOff(60); window.siltAudio.noteOn(64); });
  await page.waitForTimeout(Number(process.env.SECOND_NOTE));
}
const grains = await page.evaluate(() => window.siltAudio.lastDisplay.grains);
await page.screenshot({ path: out, clip: { x: 20, y: 150, width: 1160, height: 360 } });
await browser.close();
console.log(out, 'grains', grains);
