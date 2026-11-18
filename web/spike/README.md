# WASM spike (handoff-browser.md §9, step 1)

Silt's `core/`, unchanged, compiled to WebAssembly and played in an AudioWorklet.
`SiltSpike.cpp` is throwaway glue with two hard-coded scenes: typical (plug-in defaults,
4 notes) and worst (the plug-in benchmark's settings, 16 notes, without per-voice modulation).

```bash
mkdir -p build-spike/web
em++ -std=c++20 -O3 -DNDEBUG -Icore/include -Ikit/include core/src/*.cpp web/spike/SiltSpike.cpp \
  --no-entry -sSTANDALONE_WASM -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
  -o build-spike/web/silt-spike.wasm
cp web/spike/{index.html,spike-wasm.js,spike-worklet.js} resources/samples/rain-bells.flac build-spike/web/
python3 -m http.server 8790 --bind 127.0.0.1 --directory build-spike/web
```

Native and node twins of the benchmark (same scenes, same timing):

```bash
ffmpeg -i resources/samples/rain-bells.flac -f f32le -ac 2 build-spike/rb-inter.f32   # then de-interleave to rb.f32
clang++ -std=c++20 -O3 -DNDEBUG -Icore/include -Ikit/include core/src/*.cpp web/spike/SiltSpike.cpp web/spike/bench.cpp -o build-spike/spike-bench
build-spike/spike-bench build-spike/rb.f32 624001 48000
node web/spike/bench.mjs build-spike/web/silt-spike.wasm build-spike/rb.f32 624001 48000
```

Results, 18 November 2026, owner's Mac (48 kHz, 128-frame blocks; % of real time on one core):

| | native | WASM in node | WASM in Chrome (main thread) | WASM in the worklet, live |
| --- | --- | --- | --- | --- |
| typical | 0.6% | 1.3% | 1.2% | about 2.4% (44.1 kHz) |
| worst | 17.3% | 35.5% | 35.6% | about 44% (44.1 kHz, 416 grains) |
| sample analysis | 62 ms | 83 ms | 71 ms | 79 ms |

`-msimd128` made no difference.

Owner's iPhone (iOS 18.7, Brave, so WebKit), main-thread benchmark over Wi-Fi: typical 2.5%,
worst 55.2%, analysis 136 ms. Same peaks and grain counts as the Mac. Live worklet audio not
tried on the phone (needs https).
