# Silt

A granular sampler for VST3 / AU / CLAP, built twice from one codebase:

| Build | Goes on | Captures from | AU type |
|---|---|---|---|
| **Silt** (instrument) | a MIDI track | a sidechain input (another track) | `aumu Slti SltA` |
| **Silt FX** (effect) | an audio track | its own input; MIDI optional | `aumf Sltf SltA` |

Design and build plan: [`../granular-sampler-vst-research/09-implementation-plan.md`](../granular-sampler-vst-research/09-implementation-plan.md).

**Status: M0.** No engine yet. Both builds show a test panel reporting what the host
sends them: capture source and level, MIDI notes, tempo and transport. The instrument
outputs silence unless **Monitor input** is on; Silt FX passes its input through.

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
core/      engine code, plain C++ (no JUCE), shared by both plug-ins, tests and tools
plugin/    the JUCE processor and editor, compiled once per build
```
