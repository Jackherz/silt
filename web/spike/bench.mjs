// node web/spike/bench.mjs <silt-spike.wasm> <sample.f32> <frames> <sampleRate>
// The WebAssembly twin of bench.cpp: same scenes, same timing.
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import { loadSpike, benchmark } from './spike-wasm.js';

const [wasmPath, samplePath, framesArg, rateArg] = process.argv.slice(2);
const frames = Number(framesArg);
const raw = readFileSync(samplePath);
const all = new Float32Array(raw.buffer, raw.byteOffset, frames * 2);

const spike = await loadSpike(readFileSync(wasmPath));
const rate = 48000;
spike.init(rate);
const t0 = performance.now();
spike.setSample([all.subarray(0, frames), all.subarray(frames)], Number(rateArg));
console.log(`analyse: ${(performance.now() - t0).toFixed(1)} ms`);
for (const r of benchmark(spike, rate, () => performance.now()))
  console.log(`${r.scene}: ${r.percent.toFixed(1)}% of real time, peak ${r.peak.toFixed(2)}, up to ${r.grains} grains`);
