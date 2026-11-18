// The jackherz.com preview's Silt page: the frame fits the panel, Silt plays inside it, and its
// START AUDIO pauses the site's music player. Desktop and phone sizes.
//   node scripts/site-check.mjs [site url]
import { chromium } from 'playwright-core';
const [site = 'http://127.0.0.1:8765/'] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH ?? '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser', args: ['--autoplay-policy=no-user-gesture-required'] });
const results = [];
const check = (what, ok, detail = '') => { results.push(ok); console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${detail ? `: ${detail}` : ''}`); };
for (const [label, viewport, mobile] of [['desktop', { width: 1440, height: 900 }, false], ['phone', { width: 390, height: 844 }, true]]) {
  const page = await browser.newPage({ viewport, deviceScaleFactor: 2, isMobile: mobile, hasTouch: mobile });
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto(`${site}#silt`);
  const frame = page.frameLocator('#silt-frame');
  await frame.locator('canvas[data-sample="AHHH-PROGRESSION"]').waitFor({ timeout: 15000 });
  await page.waitForTimeout(400);
  const fit = await page.evaluate(() => { const f = document.getElementById('silt-frame'); return { h: f.getBoundingClientRect().height, w: f.getBoundingClientRect().width, body: f.contentDocument.body.scrollHeight }; });
  check(`${label}: the frame is as tall as the panel`, Math.abs(fit.h - fit.w * 820 / 1200) < 2 && Math.abs(fit.h - fit.body) < 2, `${fit.w.toFixed(0)} x ${fit.h.toFixed(0)}`);
  check(`${label}: the tour opens on a first visit`, (await frame.locator('.tour').count()) === 1);
  await page.screenshot({ path: `../build-web/site-${label}.png`, fullPage: false });
  await frame.getByText('SKIP TOUR', { exact: true }).click();
  // A song in the header, then START AUDIO in Silt: the song pauses.
  await page.evaluate(() => { const a = document.getElementById('site-audio'); a.muted = true; return a.play?.().catch(() => {}); });
  await page.waitForTimeout(300);
  const before = await page.evaluate(() => !document.getElementById('site-audio').paused);
  await frame.getByText('START AUDIO', { exact: true }).click();
  await page.waitForTimeout(800);
  const after = await page.evaluate(() => document.getElementById('site-audio').paused);
  check(`${label}: START AUDIO pauses the site's player`, after, `was playing: ${before}`);
  await page.keyboard.down('a');
  await page.waitForTimeout(600);
  const grains = await frame.locator('body').evaluate(() => window.siltAudio?.lastDisplay?.grains ?? 0);
  await page.keyboard.up('a');
  check(`${label}: a key plays grains in the frame`, grains > 0, String(grains));
  check(`${label}: no page errors`, errors.length === 0, errors.join('; '));
  await page.close();
}
await browser.close();
process.exit(results.every(Boolean) ? 0 : 1);
