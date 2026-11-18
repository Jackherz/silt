// The spike's AudioWorklet: Silt's core in WebAssembly, rendering each 128-frame block.
import { loadSpike } from './spike-wasm.js';

const clock = globalThis.performance ? () => performance.now() : () => Date.now();

class SiltSpikeProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.spike = null;
    this.busy = 0;       // ms spent rendering since the last report
    this.blocks = 0;
    this.port.onmessage = (e) => this.message(e.data);
  }

  async message(m) {
    if (m.type === 'wasm') {
      const spike = await loadSpike(m.bytes);
      spike.init(sampleRate);
      this.spike = spike;
      this.port.postMessage({ type: 'ready', sampleRate, timer: globalThis.performance ? 'performance.now' : 'Date.now' });
      return;
    }
    if (!this.spike) return;
    if (m.type === 'sample') {
      const t = clock();
      this.spike.setSample(m.channels, m.sampleRate);
      this.port.postMessage({ type: 'sampleLoaded', ms: clock() - t });
    } else if (m.type === 'scene') this.spike.scene(m.scene);
    else if (m.type === 'note') this.spike.note(m.note, m.velocity, m.on);
    else if (m.type === 'allNotesOff') this.spike.allNotesOff();
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.spike || out.length === 0) return true;
    const frames = out[0].length;
    const t = clock();
    const [l, r] = this.spike.process(frames);
    this.busy += clock() - t;
    out[0].set(l);
    (out[1] ?? out[0]).set(out[1] ? r : l);

    // About twice a second: how much of each block's time went on rendering.
    if (++this.blocks >= Math.round(sampleRate / frames / 2)) {
      const available = (this.blocks * frames * 1000) / sampleRate;
      this.port.postMessage({ type: 'load', percent: (100 * this.busy) / available, grains: this.spike.activeGrains() });
      this.busy = 0;
      this.blocks = 0;
    }
    return true;
  }
}

registerProcessor('silt-spike', SiltSpikeProcessor);
