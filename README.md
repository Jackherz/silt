# Silt

A granular sampler for VST3 / AU / CLAP, built twice from one codebase:

| Build | Goes on | Captures from | AU type |
|---|---|---|---|
| **Silt** (instrument) | a MIDI track | a sidechain input (another track) | `aumu Slti SltA` |
| **Silt FX** (effect) | an audio track | its own input; MIDI optional | `aumf Sltf SltA` |

Design and build plan: [`../granular-sampler-vst-research/09-implementation-plan.md`](../granular-sampler-vst-research/09-implementation-plan.md).

**Status: M2, playable sampler (macOS).** Both plug-ins play the grain engine from MIDI:

- **Get a sample in:** drag a file onto the panel, press **LOAD**, or **ARM** and
  record. Capture waits for the next bar while the host is playing (or starts at once
  when stopped), records 1/2/4/8 bars of the capture source (the sidechain on Silt, the
  track's input on Silt FX), then swaps it in. Samples hold up to 30 s.
- **Play it:** 16 voices with ADSR, keys transpose around the root note.
- **Saved with the project:** parameters plus the sample as FLAC, checked end to end by
  `silt_hostcheck`.
- **Panel:** a working native panel in the plan §5 palette: the sample with its region
  and a playhead per voice, capture and load, and a knob for every parameter. The
  finished UI waits on Spike B (WebView).

Not yet (M3): tempo-synced size/rate, scale quantize, Pattern, Slice and Live modes,
feedback, Heads.

## Engine (core/)

16 MIDI-played voices, each a playhead moving through a region of the sample (Start +
Length) with an ADSR, a grain scheduler (Sync timing), a 64-grain pool, cubic or linear
interpolation, and a grain envelope after GRM Play: Fade, Slant, Ease and a Declick floor.

- **Controls:** pitch, size, rate, spray (start scatter up to ±1 s, plus stereo
  scatter), reverse chance, random amp, fade/slant/ease, start, length, scan (playhead
  speed, −4…+4×), loop / hold / stop at the region end, attack/decay/sustain/release,
  root note.
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

Grains never read across the seam between a sample's end and start, and the output has
a soft safety clip from −1.9 dBFS.

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
build/silt_hostcheck_artefacts/Release/silt_hostcheck build/Silt_artefacts/Release/VST3/Silt.vst3
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
