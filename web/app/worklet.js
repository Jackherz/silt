// The audio thread: Silt's own sound (silt::Instrument, WebAssembly) in an AudioWorklet.
// The page holds the parameters and routes and sends every change; this plays them, with
// the notes, the tempo and the microphone, and sends back what the panel draws.
import { loadSilt } from '../engine/silt-wasm.js';

const DISPLAY_EVERY = 1 / 30;   // seconds between display messages

class SiltProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.silt = null;
    this.queue = [];             // messages that came before the WebAssembly was ready
    this.playing = false;
    this.bpm = 120;
    this.ppq = 0;
    this.sinceDisplay = 0;
    this.inputPeak = 0;
    this.outputPeak = 0;
    this.paramCount = 0;
    this.port.onmessage = (e) => this.message(e.data);
  }

  async message(m) {
    if (m.type === 'init') {
      const silt = await loadSilt(m.wasm);
      silt.create(true);
      silt.prepare(sampleRate, 128);
      this.paramCount = m.paramCount;
      this.silt = silt;
      for (const q of this.queue) this.apply(q);
      this.queue = [];
      this.port.postMessage({ type: 'ready', sampleRate });
      return;
    }
    if (!this.silt) { this.queue.push(m); return; }
    this.apply(m);
  }

  apply(m) {
    const silt = this.silt;
    switch (m.type) {
      case 'params': for (const [index, value] of m.values) silt.setParamRaw(index, value); break;
      case 'param': silt.setParamRaw(m.index, m.value); break;
      case 'routes': silt.clearRoutes(); for (const [mod, index, amount, polarity] of m.routes) silt.setRoute(mod, index, amount, polarity); break;
      case 'route': silt.setRoute(m.m, m.index, m.amount, m.polarity); break;
      case 'sample': silt.setSampleAnalysed(m.channels, m.sampleRate, m.analysis); break;
      case 'midi': silt.midi(0, m.bytes[0], m.bytes[1] ?? 0, m.bytes[2] ?? 0); break;
      case 'transport':
        if (m.playing && !this.playing) this.ppq = 0;   // PLAY starts from bar 1
        this.playing = m.playing;
        this.bpm = m.bpm;
        break;
      default: break;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.silt || out.length === 0) return true;
    const silt = this.silt;
    const frames = out[0].length;

    // The microphone (or nothing): up to two channels into the instrument's input.
    const input = inputs[0] ?? [];
    const channels = Math.min(2, input.length);
    if (channels > 0) {
      const [l, r] = silt.input();
      l.set(input[0]);
      r.set(input[Math.min(1, channels - 1)]);
      for (let c = 0; c < channels; c++) for (let i = 0; i < frames; i++) this.inputPeak = Math.max(this.inputPeak, Math.abs(input[c][i]));
    }

    silt.transport(this.playing, this.ppq, this.bpm);
    const [left, right] = silt.process(frames, channels);
    out[0].set(left);
    if (out[1]) out[1].set(right);
    for (let i = 0; i < frames; i++) this.outputPeak = Math.max(this.outputPeak, Math.abs(left[i]), Math.abs(right[i]));
    if (this.playing) this.ppq += frames / sampleRate * this.bpm / 60;

    this.sinceDisplay += frames / sampleRate;
    if (this.sinceDisplay >= DISPLAY_EVERY) {
      this.sinceDisplay = 0;
      this.port.postMessage({ type: 'display', values: silt.display(this.paramCount), grains: silt.activeGrains(),
                              inputPeak: this.inputPeak, outputPeak: this.outputPeak, ppq: this.ppq });
      this.inputPeak = 0;
      this.outputPeak = 0;
    }
    return true;
  }
}

registerProcessor('silt', SiltProcessor);
