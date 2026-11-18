// replay.cpp's twin through the WebAssembly build: plays silt-golden's scene
// logs and compares with the plug-in's renders.
//   node web/engine/replay.mjs <golden-folder> <instrument 0|1> [build-web/silt.wasm]
import { readFileSync } from 'node:fs';
import { loadSilt } from './silt-wasm.js';

const [folder, instrumentArg, wasmPath = 'build-web/silt.wasm'] = process.argv.slice(2);
const instrument = instrumentArg === '1';
const rate = 48000;
const names = ['pitch-chord-sustain', 'worst-case', 'chain-and-switches', 'modulation', 'midi-sources-and-follow',
               'synced-host', 'grains-mode-region', 'slice', 'live-input', 'feed'];
const params = Object.fromEntries(JSON.parse(readFileSync('build-web/params.json', 'utf8')).map((p) => [p.id, p.index]));

const silt = await loadSilt(readFileSync(wasmPath));
const raw = instrument ? readFileSync(`${folder}/sample.f32`) : null;
const frames = raw ? raw.byteLength / 8 : 0;
const sample = raw ? new Float32Array(raw.buffer, raw.byteOffset, frames * 2) : null;

const testInput = (i, channel) => {   // as silt-golden's
  const t = i / rate;
  const chirp = Math.sin(6.283185307179586 * (110.0 * t + 40.0 * t * t)) * 0.3;
  const pulse = (t % 0.5) < 0.05 ? 0.4 * Math.sin(6.283185307179586 * 880.0 * t) : 0.0;
  return Math.fround(Math.fround(chirp + pulse) * (channel === 0 ? 1.0 : Math.fround(0.8)));
};

let allSame = true;
const report = [];
for (const name of names) {
  const lines = readFileSync(`${folder}/${name}.log`, 'utf8').trim().split('\n').map((l) => l.split(' '));
  const [, , totalText, blockText, inputText, playingText, hasInputText] = lines.shift();
  const total = +totalText, blockSize = +blockText, input = inputText === '1', playing = playingText === '1';
  const inputChannels = hasInputText === '1' ? 2 : 0;

  silt.create(instrument);
  silt.prepare(rate, 512);
  if (sample) silt.setSample([sample.subarray(0, frames), sample.subarray(frames)], rate);

  const out = new Float32Array(total * 2);
  let next = 0;
  const t0 = performance.now();
  for (let start = 0; start < total; start += blockSize) {
    const n = Math.min(blockSize, total - start);
    const midi = [];
    for (; next < lines.length && +lines[next][1] < start + n; ++next) {
      const [kind, , ...a] = lines[next];
      if (kind === 'set') silt.setParam(params[a[0]], +a[1]);
      else if (kind === 'route') silt.setRoute(+a[0], params[a[1]], +a[2], +a[3]);
      else midi.push(lines[next]);
    }
    for (let sub = 0; sub < n; sub += 128) {
      const m = Math.min(128, n - sub);
      for (const e of midi) {
        const at = +e[1];
        if (at >= start + sub && at < start + sub + m) silt.midi(at - start - sub, +e[2], +e[3], +e[4]);
      }
      silt.transport(playing, (start + sub) / rate * 120 / 60, 120);
      const [inL, inR] = silt.input();
      for (let i = 0; i < m; i++) {
        inL[i] = input ? testInput(start + sub + i, 0) : 0;
        inR[i] = input ? testInput(start + sub + i, 1) : 0;
      }
      const [l, r] = silt.process(m, inputChannels);
      for (let i = 0; i < m; i++) {
        out[(start + sub + i) * 2] = l[i];
        out[(start + sub + i) * 2 + 1] = r[i];
      }
    }
  }
  const ms = performance.now() - t0;

  const refRaw = readFileSync(`${folder}/${name}.f32`);
  const ref = new Float32Array(refRaw.buffer, refRaw.byteOffset, out.length);
  let first = -1, worst = 0, diffEnergy = 0, refEnergy = 0;
  for (let i = 0; i < out.length; i++) {
    const d = out[i] - ref[i];
    if (d !== 0 && first < 0) first = i;
    worst = Math.max(worst, Math.abs(d));
    diffEnergy += d * d;
    refEnergy += ref[i] * ref[i];
  }
  const snr = diffEnergy === 0 ? Infinity : 10 * Math.log10(refEnergy / diffEnergy);
  if (first >= 0) allSame = false;
  report.push({ name, identical: first < 0, from: first < 0 ? null : first / 2 / rate, worst, snr, realtime: (total / rate * 1000) / ms });
}
for (const r of report)
  console.log(`${r.name.padEnd(26)} ${r.identical ? 'identical' : `differs from ${r.from.toFixed(4)} s, by up to ${r.worst.toExponential(2)}, ${r.snr.toFixed(1)} dB below the sound`}  (${r.realtime.toFixed(0)}x real time)`);
process.exitCode = allSame ? 0 : 1;
