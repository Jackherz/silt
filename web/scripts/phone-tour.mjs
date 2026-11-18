// The tour on a phone: each step's view, as the phone scrolls to it.
import { chromium } from 'playwright-core';
const browser = await chromium.launch({ executablePath: '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser' });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
await page.goto('http://localhost:5176/?tour=first');
await page.waitForFunction(() => document.querySelector('.tour'), null, { timeout: 15000 });
for (let i = 1; i <= 9; i++) {
  await page.waitForTimeout(700);
  await page.screenshot({ path: `../build-web/phone-tour-${i}.png` });
  const inView = await page.evaluate(() => { const r = document.querySelector('.tour-card').getBoundingClientRect(); return r.top >= -1 && r.bottom <= innerHeight + 1; });
  console.log(`step ${i}: card ${inView ? 'in view' : 'NOT in view'}`);
  await page.getByText(i === 9 ? 'DONE' : 'NEXT', { exact: true }).click({ force: true });
}
await browser.close();
