// The tour's cards, for comparing with the plug-in's tour snapshots.
//   node scripts/tour-shots.mjs [url]
import { chromium } from 'playwright-core';
const [url = 'http://localhost:5176/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser' });
const page = await browser.newPage({ viewport: { width: 1200, height: 820 }, deviceScaleFactor: 2 });
await page.goto(`${url}?tour=first`);
await page.waitForFunction(() => document.querySelector('canvas[data-sample]')?.dataset.sample === 'AHHH-PROGRESSION');
for (let i = 1; i <= 9; i++) {
  await page.waitForTimeout(150);
  await page.screenshot({ path: `../build-web/tour-${i}.png` });
  await page.getByText(i === 9 ? 'DONE' : 'NEXT', { exact: true }).click();
}
await browser.close();
