// Loads silt-spike.wasm and wraps its C functions. Shared by the AudioWorklet,
// the page's main-thread benchmark and bench.mjs (node).

export async function loadSpike(bytes) {
  const module = await WebAssembly.compile(bytes);
  // Stub whatever the C++ runtime imports (abort paths, memory growth notices):
  // none of it is reached in normal running.
  const imports = {};
  for (const { module: ns, name, kind } of WebAssembly.Module.imports(module)) {
    if (kind !== 'function') continue;
    (imports[ns] ??= {})[name] = (...args) => {
      if (name === 'emscripten_notify_memory_growth') return;
      if (name === 'proc_exit' || name.includes('abort')) throw new Error(`wasm ${name}(${args})`);
      return 0;
    };
  }
  const instance = await WebAssembly.instantiate(module, imports);
  const x = instance.exports;
  x._initialize?.();
  const memory = x.memory;
  const view = (ptr, length) => new Float32Array(memory.buffer, ptr, length);
  const maxFrames = x.spike_max_frames();

  return {
    init: (rate) => x.spike_init(rate),
    setSample(channels, sampleRate) {
      const frames = channels[0].length;
      x.spike_sample_begin(frames, sampleRate);
      for (let ch = 0; ch < 2; ch++)
        view(x.spike_sample_channel(ch), frames).set(channels[Math.min(ch, channels.length - 1)]);
      x.spike_sample_end();
    },
    scene: (n) => x.spike_scene(n),
    note: (note, velocity, on) => x.spike_note(note, velocity, on ? 1 : 0),
    allNotesOff: () => x.spike_all_notes_off(),
    // Returns [left, right] views, valid until the next call.
    process(frames) {
      const ptr = x.spike_process(frames);
      return [view(ptr, frames), view(ptr + maxFrames * 4, frames)];
    },
    activeGrains: () => x.spike_active_grains(),
  };
}

// The scenes' notes, as in the plug-in's benchmark: 48, 51, 54…
export const sceneNotes = (scene) =>
  Array.from({ length: scene === 0 ? 4 : 16 }, (_, n) => 48 + n * 3);

// Renders four seconds of each scene and times it.
export function benchmark(spike, rate, now) {
  const results = [];
  for (const scene of [0, 1]) {
    spike.scene(scene);
    for (const n of sceneNotes(scene)) spike.note(n, 100 / 127, true);
    const blocks = Math.floor((4 * rate) / 128);
    let seconds = 0, peak = 0, grains = 0;
    for (let b = 0; b < blocks; b++) {
      const t = now();
      const [l, r] = spike.process(128);
      seconds += (now() - t) / 1000;
      for (let i = 0; i < 128; i++) peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
      grains = Math.max(grains, spike.activeGrains());
    }
    spike.allNotesOff();
    results.push({ scene: scene === 0 ? 'typical' : 'worst', percent: (100 * seconds) / 4, peak, grains });
  }
  return results;
}
