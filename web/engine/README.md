# Silt's sound in the browser

`SiltWeb.cpp` puts `silt::Instrument` (core/, the plug-ins' own sound) behind plain C
functions; `build.sh` compiles it to `build-web/silt.wasm` with Emscripten, and
`silt-wasm.js` loads it (AudioWorklet, page or node). Parameters are set by
`silt::params` id in their own units and held exactly as a JUCE host holds them.

```bash
web/engine/build.sh                                   # build-web/silt.wasm
cmake --build build --target silt_export_web
build/silt_export_web_artefacts/Release/silt_export_web build-web --check
node web/app/check-exports.mjs build-web
```

`silt_export_web` writes what the panel reads, from the plug-in's own sources:
`params.json` (silt::params), `layout.json` (plugin/PageLayout.cpp, show and dim rules
as data), `help.json` (plugin/Help.h, verbatim) and `presets.json` (the factory
presets). With `--check` it also writes 2,000 random panel states with the C++ answers
and the value text at 50 points per parameter; `check-exports.mjs` checks the browser's
rule evaluator (`web/app/rules.js`) and the WebAssembly build's text against them.

## Checking it sounds like the plug-in

`silt-golden` plays ten scenes through the plug-in and writes each one's output and a
log of what it did (parameters, routes, MIDI). The replayers play those logs through
this bridge and compare:

```bash
build/silt_golden_artefacts/Release/silt_golden build-golden            # and silt_golden_fx … build-golden-fx
clang++ -std=c++20 -O3 -DNDEBUG -Icore/include -Ikit/include core/src/*.cpp \
    web/engine/SiltWeb.cpp web/engine/replay.cpp -o build-web/replay
build-web/replay build-golden build-golden/sample.f32 624001 1         # native: identical to the bit
node web/engine/replay.mjs build-golden 1                               # WebAssembly
```

Natively the bridge is identical to the plug-in in all ten scenes, in both builds. The
WebAssembly build differs by rounding only, 60–136 dB below the sound (18 November
2026): the browser's own sin, exp and pow, and no flush-to-zero for denormals.
