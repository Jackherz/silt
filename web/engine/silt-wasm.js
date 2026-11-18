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
    setParamRaw: (id, value) => x.silt_set_param_raw(id, value),
    getParam: (id) => x.silt_get_param(id),
    to01: (id, value) => x.silt_to01(id, value),
    from01: (id, proportion) => x.silt_from01(id, proportion),
    setRoute: (modulator, id, amount, polarity = 0) => x.silt_set_route(modulator, id, amount, polarity),
    isTarget: (id) => x.silt_is_target(id) === 1,
    routeAmount: (modulator, id) => x.silt_route_amount(modulator, id),
    routePolarity: (modulator, id) => x.silt_route_polarity(modulator, id),
    routeBipolar: (modulator, id) => x.silt_route_bipolar(modulator, id) === 1,
    routeCanWork: (modulator, id) => x.silt_route_can_work(modulator, id) === 1,
    naturalWhereUnrouted: () => x.silt_natural_where_unrouted(),
    modulatorPerVoice: (modulator) => x.silt_modulator_per_voice(modulator) === 1,
    clearRoutes: () => x.silt_clear_routes(),
    setSample(channels, sampleRate) {
      const frames = channels[0].length;
      x.silt_sample_begin(frames, sampleRate);
      for (let ch = 0; ch < 2; ch++)
        floats(x.silt_sample_channel(ch), frames).set(channels[Math.min(ch, channels.length - 1)]);
      x.silt_sample_end();
    },
    // The page analyses; the worklet installs (no stall on the audio thread).
    analyse(channels, sampleRate) {
      const frames = channels[0].length;
      x.silt_sample_begin(frames, sampleRate);
      for (let ch = 0; ch < 2; ch++)
        floats(x.silt_sample_channel(ch), frames).set(channels[Math.min(ch, channels.length - 1)]);
      const n = x.silt_analyse();
      return floats(x.silt_analysis(), n).slice();
    },
    setSampleAnalysed(channels, sampleRate, analysis) {
      const frames = channels[0].length;
      x.silt_sample_begin(frames, sampleRate);
      for (let ch = 0; ch < 2; ch++)
        floats(x.silt_sample_channel(ch), frames).set(channels[Math.min(ch, channels.length - 1)]);
      floats(x.silt_analysis_in(analysis.length), analysis.length).set(analysis);
      x.silt_sample_end_analysed();
    },
    display: (count) => floats(x.silt_display(), count + 4).slice(),
    screen: () => floats(x.silt_screen_data(), x.silt_screen()).slice(),
    livePeaks: () => floats(x.silt_live_peaks(), 1024).slice(),
    // SLICE BY MANUAL's starts (sorted and spaced already).
    setSlices(marks) {
      const n = Math.min(128, marks.length);
      floats(x.silt_slices_in(), 128).set(marks.slice(0, n));
      x.silt_set_slices(n);
    },
    // The scope: which module it taps (-1 none); read gives { written, input, output }.
    scopeModule: (m) => x.silt_scope_module(m),
    readScope(count) {
      const written = x.silt_read_scope(count) >>> 0;
      return { written, input: floats(x.silt_scope_in(), count).slice(), output: floats(x.silt_scope_out(), count).slice() };
    },
    // Region edges snapped as the engine plays them, given the sample's analysis.
    snapRegion(begin, end, frames, snap, analysis) {
      floats(x.silt_analysis_in(analysis.length), analysis.length).set(analysis);
      const r = floats(x.silt_snap_region(begin, end, frames, snap), 2);
      return [r[0], r[1]];
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
