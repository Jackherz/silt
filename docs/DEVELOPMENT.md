# Developing Silt

How the code is laid out, how to build, test and validate it, and what has been checked in
which hosts. For what the controls do, see the [manual](MANUAL.md).

## Layout

```
core/       the engine: grains, resonator, filter, COLOR, SPACE, modulators. Plain C++
            with no JUCE, shared by both plug-ins, the tests and the tools
plugin/     the JUCE processor and editor, compiled once per build (Silt and Silt FX).
            PageLayout is the page and slot table the editor and the controller share
kit/        the Glyph Audio panel kit (submodule): the look, presets, the Bench
tests/      Catch2 tests: silt_tests for core/, silt_plugin_tests for the plug-in
tools/      render (offline renderer), hostcheck (hosts the built VST3), snapshot
            (draws the panel to PNGs without a window)
docs/       the manual, design notes and reviews
resources/  the factory sample
```

## The engine

16 MIDI-played voices, each a playhead moving through a region of the sample (START and
LENGTH) with an ADSR, a grain scheduler, a 64-grain pool, cubic or linear interpolation,
and a grain envelope after GRM Play's Fade, Slant and Ease with a declick floor.

- **Grains latch their settings** when they start; a full pool drops new grains rather
  than cutting old ones off.
- **Buffer swaps are lock-free:** grains already playing finish on the old buffer, which
  then goes back to the message thread to be freed.
- Grains never read across the seam between a sample's end and its start, and the
  output has a soft safety clip from -1.9 dBFS.

The effects run after the grains in the order CHAIN sets: a 128-mode modal resonator, a
four-stage ladder filter at twice the sample rate, COLOR (drive, tilt, bits, rate,
compressor, noise, wow) and SPACE (a delay and an 8-line feedback delay network reverb).
Design notes: [mod-section-design.md](mod-section-design.md),
[per-voice-modulation.md](per-voice-modulation.md).

## Build

Needs CMake 3.22+, Ninja and the Xcode command line tools. JUCE 8, Catch2 and
clap-juce-extensions are fetched at configure time.

```bash
git clone --recurse-submodules https://github.com/Jackherz/silt.git
cd silt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Builds land in `build/Silt_artefacts/Release/` and `build/SiltFX_artefacts/Release/`
(AU, VST3, CLAP, Standalone) and are copied into `~/Library/Audio/Plug-Ins/`. Pass
`-DSILT_COPY_AFTER_BUILD=OFF` to skip the copy. `-DSILT_CORE_ONLY=ON` builds only the
JUCE-free core and its tests.

A host keeps the plug-in it first loaded until it quits: restart it after a rebuild.

## Test

```bash
build/silt_tests                                                  # the engine
build/silt_plugin_tests_artefacts/Release/silt_plugin_tests       # processor, editor, presets
```

The engine tests cover the window and interpolation maths, deterministic output per
seed, ten minutes of noise without NaN or denormals, click-free output over a
192-setting grid (with a negative control that checks the click detector catches a real
click), buffer swaps, and every effect's bypass, level and stability. The plug-in tests
cover state, presets, undo, modulation routing, the controller and the editor.

Hidden tests, run by tag: `"[benchmark]"` (CPU for typical and worst-case settings),
`"[factory-density]"` (each factory preset under a six-note chord) and `"[frame-rate]"`
(needs a visible window).

## Listen

```bash
tools/render/make-listening-set.sh               # renders build/listen/*.wav
build/silt_render_artefacts/Release/silt_render --in loop.wav --out out.wav \
    --note 64 --hold 6 --set size=150 --set spray=0.2 --set scan=0.5
```

## Validate

```bash
build/silt_hostcheck_artefacts/Release/silt_hostcheck build/Silt_artefacts/Release/VST3/Silt.vst3
pluginval --strictness-level 10 --skip-gui-tests --validate ~/Library/Audio/Plug-Ins/VST3/Silt.vst3
auval -v aumu Slti SltA
auval -v aumf Sltf SltA
```

`silt_hostcheck` hosts the built VST3 and checks the state round trip, including the
sample saved as FLAC (on Silt FX, that a restored sample is dropped and INPUT works).

CI ([build.yml](../.github/workflows/build.yml)) builds on macOS: unit tests, pluginval on
both VST3 builds and the state round trip. A Linux job builds the JUCE-free core and its
tests as a portability check. Windows rejoins the matrix once its build is fixed.

## Hosts

Checked in Ableton Live 12 on macOS:

| Build / format | Result |
|---|---|
| Silt VST3 | Sidechain arrives |
| Silt AU | Sidechain arrives (AU instruments get a single "Sidechain" input bus; rescan after updating) |
| Silt FX VST3 | Works on an audio track; MIDI routed from another track arrives |
| CLAP | Builds; not yet tried in a host (Live doesn't load CLAP) |
| Logic, GarageBand, Windows | Not yet tested |

## Identity

Company `Glyph Audio`, manufacturer code `SltA`, plug-in codes `Slti` (Silt) and `Sltf`
(Silt FX), bundle IDs `com.siltaudio.*`, all set in one block in `CMakeLists.txt`. Saved
sets depend on the codes and IDs: they don't change once a public build is out.
