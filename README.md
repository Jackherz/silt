# Silt

A granular sampler for VST3 / AU / CLAP, built twice from one codebase:

| Build | Goes on | Captures from | AU type |
|---|---|---|---|
| **Silt** (instrument) | a MIDI track | a sidechain input (another track) | `aumu Slti SltA` |
| **Silt FX** (effect) | an audio track | its own input; MIDI optional | `aumf Sltf SltA` |

Design and build plan: [`../granular-sampler-vst-research/09-implementation-plan.md`](../granular-sampler-vst-research/09-implementation-plan.md).

**Status: M1 (engine core, headless).** The grain engine runs in the unit tests and the
offline render tool, not yet inside the plug-ins. The plug-ins are still the M0 test
panel reporting what the host sends them: capture source and level, MIDI notes, tempo
and transport. The instrument outputs silence unless **Monitor input** is on; Silt FX
passes its input through.

## Engine (core/)

One MIDI-played voice: a playhead moving through the sample, a grain scheduler (Sync
timing), a 64-grain pool, Hann envelopes, cubic or linear interpolation.

- **Controls:** pitch, size, rate, spray (start scatter up to ±1 s, plus stereo
  scatter), start, scan (playhead speed, −4…+4×), reverse chance, root note, and what
  the playhead does at the end of the sample (loop / hold / stop).
- **Grains latch their settings** when they start; a full pool drops new grains rather
  than cutting old ones off.
- **Buffer swaps are lock-free:** grains already playing finish on the old buffer, which
  then goes back to the message thread to be freed.

## Tests and listening

```bash
ctest --test-dir build --output-on-failure      # or ./build/silt_tests
tools/render/make-listening-set.sh               # renders build/listen/*.wav
```

The tests cover the window and interpolation maths, the reverse-grain span, deterministic
output per seed, ten minutes of noise without NaN or denormals, click-free output over a
192-setting grid, buffer swaps and key transposition. A negative control checks that the
click detector does catch a real click.

The listening set renders a sine, a synthesised drum loop and text-to-speech through the
recipes from research doc 01 §4 (time-stretch, pad, glitch, chops, reverse, one-shot).
`silt_render` also takes your own files:

```bash
build/silt_render_artefacts/Release/silt_render --in loop.wav --out out.wav \
    --note 64 --hold 6 --set size=150 --set spray=0.2 --set scan=0.5
```

**Known limits, for M2:** a grain that crosses the loop point of a sample that doesn't
loop cleanly clicks (needs a loop crossfade); there's no output limiter yet, so dense
settings on loud material can peak just over 0 dBFS.

## Build

Needs CMake 3.22+, Ninja and the Xcode command line tools (macOS) or Visual Studio 2022
(Windows). JUCE 8 and clap-juce-extensions are fetched at configure time.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Builds land in `build/Silt_artefacts/Release/` and `build/SiltFX_artefacts/Release/`
(AU, VST3, CLAP, Standalone), and are copied into `~/Library/Audio/Plug-Ins/` so DAWs
find them. Pass `-DSILT_COPY_AFTER_BUILD=OFF` to skip the copy.

## Validate

```bash
pluginval --strictness-level 10 --skip-gui-tests --validate ~/Library/Audio/Plug-Ins/VST3/Silt.vst3
auval -v aumu Slti SltA
auval -v aumf Sltf SltA
```

CI (`.github/workflows/build.yml`) builds on macOS and Windows and runs pluginval on
both VST3 builds.

## Spike A: does Ableton feed the instrument a sidechain?

The instrument build depends on this (plan gap 11). In Live 12, on macOS and later Windows:

1. Put **Silt** (VST3) on a MIDI track and open its panel.
2. In the device's sidechain section, pick an audio track that's playing.
   The panel should show `CAPTURE SIDECHAIN (2 CH)` and a moving level.
3. Turn **Monitor input** on: you should hear that track through Silt.
4. Play notes from a clip: `MIDI` should count them and show the last note.
5. Repeat with the AU and CLAP builds.
6. Put **Silt FX** on an audio track: level should move, audio should pass through.
   Route a MIDI track to it (*MIDI To* → the audio track → Silt FX) and check notes arrive.

Record which build and format work in the plan before starting M1.

**Results so far (Live 12, macOS, 2026-10-24):**

| Build / format | Result |
|---|---|
| Silt VST3 | ✅ sidechain arrives, level moves |
| Silt AU (two input buses) | ❌ no sidechain level |
| Silt AU (one input bus) | ✅ after a plug-in rescan: AU instruments now get a single "Sidechain" input bus |
| Logic / GarageBand (AU) | ⏳ not tested |
| Silt FX VST3 | ✅ on a track with audio |
| CLAP | — Live doesn't host CLAP; test in Reaper or Bitwig later |
| Windows | ⏳ not tested |

## Identity

Company name, manufacturer code (`SltA`), plugin codes (`Slti`, `Sltf`) and bundle IDs
(`com.siltaudio.*`) are **provisional** and set in one block in `CMakeLists.txt`. Lock
them in before the first public build; after that they never change.

## Layout

```
core/          engine code, plain C++ (no JUCE), shared by both plug-ins, tests and tools
plugin/        the JUCE processor and editor, compiled once per build
tests/         Catch2 unit tests for core/
tools/render/  offline renderer and the listening-set script
```
