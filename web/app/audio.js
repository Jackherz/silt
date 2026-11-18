// The page's side of the audio: the AudioContext, the worklet running Silt, the factory
// samples and your own files, the microphone, and the notes. The panel calls setParam and
// setRoute for every change (window.siltAudio); START AUDIO calls start().
import workletUrl from './worklet.js?worker&url';

// The factory samples (resources/samples, GPL with the plug-in), with the note each plays at
// as recorded: ROOT goes to it when one loads (SiltProcessor's factorySamples).
export const SAMPLES = [
  { file: 'ahhh-progression.flac', name: 'Ahhh Progression', root: 64 },
  { file: 'keys-drone.flac', name: 'Keys Drone', root: 55 },
  { file: 'rain-bells.flac', name: 'Rain Bells', root: 63 },
  { file: 'guitar-harmonics.flac', name: 'Guitar Harmonics', root: 59 },
];
export const MAX_SECONDS = 30;   // as the plug-in keeps of a file (SiltProcessor::maxCaptureSeconds)

export class Audio {
  constructor({ wasm, paramCount, onDisplay, onState }) {
    this.wasm = wasm;
    this.paramCount = paramCount;
    this.onDisplay = onDisplay;
    this.onState = onState;
    this.context = null;
    this.node = null;
    this.mic = null;
    this.micStream = null;
    this.pending = [];
  }

  get running() { return this.context?.state === 'running'; }

  // Messages before the worklet exists wait, so nothing set before START AUDIO is lost.
  post(message, transfer) {
    if (this.node) this.node.port.postMessage(message, transfer ?? []);
    else this.pending.push([message, transfer]);
  }

  async start(state) {
    if (!this.context) {
      const Context = window.AudioContext || window.webkitAudioContext;
      if (!Context) throw new Error('This browser has no Web Audio.');
      const context = new Context({ latencyHint: 'interactive' });
      if (!context.audioWorklet) { await context.close(); throw new Error('Audio needs a secure page (https).'); }
      await context.audioWorklet.addModule(workletUrl);
      const node = new AudioWorkletNode(context, 'silt', {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
        channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers',
      });
      node.connect(context.destination);
      node.port.onmessage = (e) => {
        if (e.data.type === 'display') this.onDisplay?.(e.data);
        if (e.data.type === 'ready') this.onState?.('ready');
      };
      node.onprocessorerror = () => this.onState?.('error');
      context.onstatechange = () => this.onState?.(context.state);
      this.context = context;
      node.port.postMessage({ type: 'init', wasm: this.wasm, paramCount: this.paramCount });
      this.node = node;
      // Everything as the page has it now, then what queued up.
      this.post({ type: 'params', values: state.params });
      this.post({ type: 'routes', routes: state.routes });
      for (const [m, t] of this.pending) node.port.postMessage(m, t ?? []);
      this.pending = [];
    }
    await this.context.resume();
    this.onState?.(this.context.state);
  }

  async stop() {
    await this.context?.suspend();
    this.onState?.(this.context?.state);
  }

  setParam(index, value) { this.post({ type: 'param', index, value }); }
  setRoute(m, index, amount, polarity) { this.post({ type: 'route', m, index, amount, polarity }); }
  midi(bytes) { this.post({ type: 'midi', bytes }); }
  noteOn(note, velocity = 100) { this.midi([0x90, note, velocity]); }
  noteOff(note) { this.midi([0x80, note, 0]); }
  allNotesOff() { this.midi([0xb0, 123, 0]); }
  transport(playing, bpm) { this.post({ type: 'transport', playing, bpm }); }

  // A sample, decoded (the page's own AudioContext, or a throwaway one before START AUDIO),
  // to the plug-in's 30 s; the page analyses it, the worklet only installs it.
  async decode(bytes) {
    const Context = window.OfflineAudioContext || window.webkitOfflineAudioContext;
    const ctx = this.context ?? new Context(2, 1, 48000);
    const buffer = await ctx.decodeAudioData(bytes);
    const frames = Math.min(buffer.length, Math.round(MAX_SECONDS * buffer.sampleRate));
    const channels = [buffer.getChannelData(0).slice(0, frames),
                      buffer.getChannelData(buffer.numberOfChannels > 1 ? 1 : 0).slice(0, frames)];
    return { channels, sampleRate: buffer.sampleRate, cut: buffer.length > frames, seconds: buffer.duration };
  }

  sendSample({ channels, sampleRate }, analysis) {
    const copy = channels.map((c) => c.slice());
    this.post({ type: 'sample', channels: copy, sampleRate, analysis }, copy.map((c) => c.buffer));
  }

  // The microphone into the instrument's input (LIVE and FEED hear it; MONITOR plays it).
  async micOn() {
    if (!this.context) throw new Error('Press START AUDIO first.');
    if (!navigator.mediaDevices?.getUserMedia) throw new Error('This page can\'t reach a microphone.');
    const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    this.micStream = stream;
    this.mic = this.context.createMediaStreamSource(stream);
    this.mic.connect(this.node);
  }

  micOff() {
    try { this.mic?.disconnect(); } catch { /* already */ }
    this.micStream?.getTracks().forEach((t) => t.stop());
    this.mic = null;
    this.micStream = null;
  }
}
