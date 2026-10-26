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

**Live mode** (PLAY page, MODE: LIVE; the default on Silt FX): the input is written into
an 8 s ring buffer and grains read a DELAY behind the newest audio (ms or synced note
value), with FEEDBACK (grain output back into the ring, DC-blocked, low-passed and
soft-clipped), FREEZE (stop writing) and NO NOTES, what plays grains while no keys are held: OFF,
ALWAYS (a constant voice at the root note) or FOLLOW (the default on Silt FX: the input's
level plays the note envelope; getting louder starts Attack/Decay/Sustain, going quiet
Releases, a sudden jump retriggers, and louder hits play louder). Keys still transpose. Grains are kept
clear of the write head. The screen scrolls the input with the newest audio at the right.

**PITCH page:** RND PITCH (±0–24 st per grain), SCALE (off, chromatic, major, minor,
modes, pentatonics, whole tone, octaves, fifths) and KEY (every grain's pitch is added up
first, then snapped once), PATTERN (a repeating pitch pattern, stepping each grain) with
STEPS and NEW (a new pattern, saved with the set), and HEADS (1–8 playheads per note,
evenly around the region at full SPREAD; in Live mode, further back in time).

**RESONATOR page:** a bank of up to 12 ringing band-pass filters after the grains.
TUNE (the lowest band), NOTES (SCALE: the PITCH page's scale and key, or the root and
fifths with no scale; HARMONICS; BELL partials), RANGE (1–4 octaves the bands reach),
DECAY (how long the bands ring once the sound stops, 50 ms–20 s; while sound comes in
they stay narrow and pitched, so short decays still colour it), BRIGHT (how much quieter and shorter the high bands
are), WIDTH (bands alternate left and right), FOLLOW (tune to the last note played),
MIX (0 is an exact bypass). The rings are held to the input's level, so a tone
right on a band can't run away; anything left over saturates softly rather than clips.

**FILTER page** (after the resonator): MODE (OFF, an exact bypass; 12 or 24 dB/oct),
CUTOFF, RESO (up to a strong ringing peak; the passband dips as it rises, as on many
analogue filters), MORPH (low pass → band pass → high pass, blended), DRIVE (up to
+24 dB into a soft saturation), ENV (± the input's level moves the cutoff up to 4
octaves), KEY (the cutoff follows the last note played), MIX. Everything glides, so
sweeps and slope changes don't zipper or click.

**COLOR page** (after the filter; every control's default leaves the sound untouched):
DRIVE (up to +30 dB into a slightly uneven tape-style curve, so even harmonics too),
TONE (±6 dB tilt around 800 Hz), BITS (16 down to 2), RATE (sample-and-hold, OFF down
to 200 Hz), COMP (one knob: threshold down, ratio up, makeup to match), NOISE (hiss
that rides the signal's level, so silence stays silent), WOW (slow wow and faster
flutter; no added delay at zero), MIX.

**SPACE page** (last; delay and reverb are sends, so DELAY and REVERB at 0 leave the
sound untouched; design notes in research doc 10): TIME (10 ms–4 s, or note values
with SYNC), FEEDBACK (repeats darken and soften through TONE's filters and a gentle
saturation, so even 100% stays bounded; PING-PONG or STEREO under it), DELAY (level;
the repeats also feed the reverb), SIZE (room to cathedral: line lengths and
pre-delay), DECAY (0.3–30 s, the same at any SIZE; FREEZE under it holds both tails
and shuts the input out), TONE (dark to bright: reverb bandwidth, how fast highs die,
the delay's loop filters), SHIMMER (octave-up feedback in the reverb), REVERB (level).
The reverb is an 8-channel diffuser into an 8-line feedback delay network with
per-line modulation and absorption filters; changing TIME glides like tape.

**Modulation:** four modulators, each a WAVE (sine → triangle → saw → square, free
or synced, phase, retrigger on notes), RANDOM (smooth to stepped, jitter, free or
synced), ENVELOPE (ADSR from the notes played; REPEAT loops it while held), FOLLOW
(the input's level), MIDI (velocity, note, mod wheel, aftertouch, pitch bend) or
MACRO (a knob, automatable and controller-mappable, that moves everything it's
routed to). Design: `docs/mod-section-design.md`.

- **The mod rail**, left of every page: a chip per modulator with its colour and
  number, type, a live meter and its route count. Click arms it (filled with its
  colour; click again to disarm), Shift-click only selects it, drag it onto a knob
  to make a +25% route, hover × (twice) clears its routes, right-click to edit,
  mute, show or clear. Keys 1–4 arm, Esc disarms. The header's four stripes are the
  same four modulators as meters.
- **Depth on any knob:** Alt-drag sets the depth of the armed modulator (or the
  knob's only route, or the last one used); Shift+Alt is finer; it snaps to zero
  near it. Alt+double-click removes it. Plain drag is always the value, and
  double-click resets the value. Right-click a knob for each modulator's depth
  (quick values, add, remove) in its colour.
- **Rings** round a knob: an arc per route in its modulator's colour and texture
  (solid, long dash, short dash, dotted), numbered at its far end, with a stop
  where the range clamps it, and a dot where the value is now. Stepped parameters
  show whole steps. An armed modulator draws a faint ring on every knob it can reach.
- **ROUTES** (strip, or right-click): every route in one list, filtered by
  modulator or page. Drag a depth (double-click to type it), see its span in the
  parameter's units, × removes one, CLEAR ALL (twice) removes them all.
- **Undo:** route changes (drags, drops, menus, clears, controller turns) undo
  and redo with Cmd/Ctrl+Z and Shift+Cmd/Ctrl+Z, the route list's UNDO/REDO, or a
  knob's right-click; a whole drag or a clear-all is one step. Depths read in the
  parameter's units first ("480 Hz to 2.64 kHz (±35%)").
- **Mute** a modulator (ON under TYPE, or the chip's menu): it contributes nothing
  and keeps its routes.
- **MOD page:** the shown modulator (click the banner for the next), its type, five
  settings for that type, and ASSIGN, which arms it for the controller: turning a
  controller knob on any page then sets that slot's depth (centre = none).

- **Per voice** (VOICE under slot 2 on the MOD page; a V on the chip): each voice
  runs its own copy of the modulator from its own note (its own envelope, random
  stream, velocity, retrigger). It reaches the grain parameters; the effects follow
  the shared copy. Rings show a pip per sounding voice. Off is exactly as before.
  Design: `docs/per-voice-modulation.md`.

- **Cross-modulation:** a modulator's continuous settings (rate, shape, phase,
  smooth, jitter, envelope and follow times, sustain, gain) take routes like any
  knob, so one modulator can move another, or itself. Modulators read each other
  a chunk (1.3 ms) late, so the order never matters.

Targets are the continuous and stepped parameters; switches (menus, buttons),
the pattern seed and MACRO knobs aren't. Modulation updates every 64 samples (the
knobs' own smoothing takes it from there); routes are saved with the set.

**8-knob controller:** a controller's 8 knobs move the 8 slots of the current page
(GRAIN / SHAPE / PITCH / PLAY / RESONATOR / FILTER / COLOR / SPACE / MOD), following the modes
(the editor and the controller read the same page table, and a test checks they agree); two buttons step pages; works with
the window closed. Defaults: knobs CC 70–77, pages CC 78/79. **LEARN**: turn knobs 1–8,
then press previous and next page. A knob takes over only when it reaches the
parameter's value (pickup), so changing page never makes values jump. The mapping and
page are saved with the set. Moves are applied off the audio thread as normal automation.

**Fine control and SNAP:** shift-drag any knob for 10× finer moves, or click its value
and type. START and LENGTH move in 0.001 % steps. SNAP (under START): OFF, ZERO (region
edges on the nearest zero crossing, for click-free loops) or TRANS (edges on detected
transients, so START/LENGTH pick slices; ticks on the waveform show them). The sample is
analysed once when it's loaded or recorded.

Not yet (M3): Slice mode.

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

CI (`.github/workflows/build.yml`) builds on macOS: unit tests, pluginval on both VST3
builds and the state round trip. A Linux job builds just the JUCE-free core and its
tests (`-DSILT_CORE_ONLY=ON`) as a portability check. Windows rejoins the matrix once
its branch lands.

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
plugin/        the JUCE processor and editor, compiled once per build:
               PageLayout (the page/slot table the editor and controller share),
               PanelLook, GrainScreen, ModPanel (rail, banner, route list),
               ModColours, Palette
docs/          code review and the MOD section design
tests/         Catch2 unit tests for core/
tools/render/  offline renderer and the listening-set script
```
