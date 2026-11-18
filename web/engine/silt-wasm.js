// Loads build-web/silt.wasm (web/engine/build.sh) and wraps its C functions.
// For the AudioWorklet, the page and node alike.

export async function loadSilt(bytes) {
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
  const x = (await WebAssembly.instantiate(module, imports)).exports;
  x._initialize?.();
  const floats = (ptr, length) => new Float32Array(x.memory.buffer, ptr, length);
  const maxBlock = x.silt_max_block();

  return {
    maxBlock,
    exports: x,
    create: (isInstrument) => x.silt_create(isInstrument ? 1 : 0),
    prepare: (sampleRate, maxBlockSize = maxBlock) => x.silt_prepare(sampleRate, maxBlockSize),
    setParam: (id, value) => x.silt_set_param(id, value),
    getParam: (id) => x.silt_get_param(id),
    setRoute: (modulator, id, amount, polarity = 0) => x.silt_set_route(modulator, id, amount, polarity),
    setSample(channels, sampleRate) {
      const frames = channels[0].length;
      x.silt_sample_begin(frames, sampleRate);
      for (let ch = 0; ch < 2; ch++)
        floats(x.silt_sample_channel(ch), frames).set(channels[Math.min(ch, channels.length - 1)]);
      x.silt_sample_end();
    },
    midi: (offset, b0, b1 = 0, b2 = 0) => x.silt_midi(offset, b0, b1, b2),
    transport: (playing, ppq, bpm, numerator = 4, denominator = 4) =>
      x.silt_transport(playing ? 1 : 0, ppq, bpm, numerator, denominator),
    input: () => [floats(x.silt_input(), maxBlock), floats(x.silt_input() + maxBlock * 4, maxBlock)],
    // Returns [left, right] views, valid until the next call.
    process(frames, inputChannels = 0) {
      const ptr = x.silt_process(frames, inputChannels);
      return [floats(ptr, frames), floats(ptr + maxBlock * 4, frames)];
    },
    activeGrains: () => x.silt_active_grains(),
    // SHAPE's grain preview: [[t, level], …], as the plug-in draws it.
    grainCurve(fade, slant, ease) {
      const n = x.silt_grain_curve(fade, slant, ease);
      const p = floats(x.silt_curve_points(), n * 2);
      return Array.from({ length: n }, (_, i) => [p[2 * i], p[2 * i + 1]]);
    },
    // How a value reads, exactly as in the plug-in.
    text(id, value) {
      const ptr = x.silt_param_text(id, value);
      const bytes = new Uint8Array(x.memory.buffer, ptr);
      return new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
    },
  };
}
