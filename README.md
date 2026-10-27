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

**Modulation:** four modulators, each a WAVE (sine → triangle → saw → square; RATE
runs through zero, −50 to +50 Hz: below zero the wave runs backwards and the middle
is STOP, so another modulator on RATE can slow it, hold it and reverse it; synced,
REV runs it backwards against the bar; phase, SYM (where the cycle turns round: 50% as drawn, the ends make
saws and pulse widths), CURVE (bulging and lazy to spiked and snappy; greyed on a
square, which it can't bend), retrigger on notes), RANDOM (smooth to stepped, jitter, free or
synced), ENVELOPE (ADSR from the notes played; REPEAT loops it while held), FOLLOW
(the input's level), MIDI (velocity, note, mod wheel, aftertouch, pitch bend) or
MACRO (a knob, automatable and controller-mappable, that moves everything it's
routed to). Design: `docs/mod-section-design.md`.

- **The mod rail**, left of every page: a chip per modulator with its colour and
  number, type, a two-second trace of what it's doing (its shape, round a centre
  line for bipolar sources) and, when it has routes, a badge with how many. Click
  arms it (outlined in its colour; click again to disarm), Shift-click only selects it, drag it onto a knob
  to make a +25% route, hover × (twice) clears its routes, right-click to edit,
  mute, show or clear. Keys 1–4 arm, Esc disarms. The header's four stripes are the
  same four modulators as meters.
- **Depth on any knob:** the coloured rings are handles: press one and drag up or
  down (Shift for fine; it snaps to zero). With a modulator armed, every knob shows
  a dashed ring in its colour: drag that to make the route. Hovering a ring changes
  the cursor and the LCD says what it does. Alt-drag on the knob is a shortcut (the
  armed modulator, or the knob's only route, or the last one used); Alt+double-click
  removes it. Plain drag on the knob is always its value; double-click resets it.
  Right-click a knob for each modulator's depth, polarity and removal, in its colour.
- **Polarity per route:** bipolar (±, both ways round the knob) or unipolar (+, one
  way). A route starts as its source is (WAVE and RANDOM bipolar, the rest unipolar);
  change it with Shift+Alt-click on its ring, the knob's right-click, or ± in the
  route list. Saved with the set and in presets.
- **Rings** round a knob: an arc per route in its modulator's colour and texture
  (solid, long dash, short dash, dotted). The half it moves into first is solid with
  an arrow and the modulator's number at its end; a bipolar route's return half is
  faint. Depths are signed, as on an Elektron LFO: + moves up first, − down first.
  A stop marks where the range clamps it, and a dot where the value is now. Stepped parameters
  show whole steps. An armed modulator draws a faint ring on every knob it can reach.
- **ROUTES** (strip, or right-click): every route in one list, filtered by
  modulator or page. Drag a depth (double-click to type it), see its span in the
  parameter's units, × removes one, CLEAR ALL (twice) removes them all.
- **Undo:** UNDO and REDO in the strip (and Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z when the
  host passes them on) step back through everything changed by hand: knobs, menus,
  buttons, controller turns, and modulation edits; the LCD says what. A drag, a
  controller turn or a clear-all is one step. Host automation and preset loads
  aren't recorded (a preset starts the history over). Depths read in the
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

**Sample gain** (GAIN on the screen's top bar): the sample's level, ±24 dB, for
samples that come in too loud or too soft. Drag it up or down (Shift for fine),
scroll, double-click for 0 dB, or right-click to normalise (peak at −1 dB). The
waveform is drawn at the gain, with anything pushed past full scale in red. Saved
with the set and undoable; not part of presets (it belongs to the sample).

**Screen insets:** the screen's bottom-right corner draws the open page as a picture,
from the modulated values, so it moves with modulation: FILTER the response curve
(slope, morph, resonance, mix, cutoff line, ENV's reach), RESONATOR the bands
(across by pitch, height level, width ring time; fading in with MIX), COLOR the
drive curve with the bit steps beside TONE's tilt (wobbling with WOW), RATE's fold
point with the aliased band shaded, NOISE's hiss floor and COMP's squeeze, SPACE
the delay taps (alternating sides for ping-pong, each FEEDBACK quieter, fading in
with DELAY) and a reverb tail after the sound and after each repeat (fading in with
REVERB, tinted by TONE, sparkling with SHIMMER, held when FROZEN), PITCH the scale
on a keyboard and the pattern's steps, MOD the shown modulator (one cycle of a WAVE
from its PHASE, drawn the way it runs, with a dot when it RETRIGs; an ENVELOPE's
shape; a moving playhead on both; or the others' trace).

**Presets** (header: ◀ name ▶, SAVE): a preset is the sound (every parameter, the
modulation routes, the shown modulator), not the sample, the controller mapping or
the window. Sixteen factory presets are built in (Init, Glass Cloud, Tape Choir, Rust
Rhythm, Bell Garden, Wander, Dark Drone, Shimmer Pad, Hit Chop, Stutter Grid, Feed Bloom,
Sample Strings, Grit Bells, Ducked Echoes, Spread Chorus, Head Patterns; the three played
from notes in SLICE or FEED are left out of Silt FX). Loading one is a single undo step. User presets are
`.siltpreset` XML files in `~/Library/Audio/Presets/Silt Audio/<plug-in>/` on macOS
(`%APPDATA%/Silt Audio/<plug-in>/Presets` on Windows). SAVE updates a user preset,
or asks for a name; the name menu lists both sets, SAVE AS… and the folder.

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

**SLICE** (PLAY page, MODE: SLICE): keys from C1 up each play one slice of the region,
at the sample's pitch; keys below C1 or past the last slice are silent. SLICE BY: EQUAL
(2–64 parts), AT HITS (the sample's transients) or MANUAL. Drag a slice line on the
waveform to move it, double-click to add or remove one (any of these switches to MANUAL,
copying the slices first); each drag, add or remove is one undo step, and MANUAL slices are
saved with the set. Scroll on the waveform to zoom (to 32×; Shift or a sideways swipe moves
along), drag the bar under it or the ZOOM label, double-click the label to see it all.

**RANDOM and RESET:** RANDOM moves the open page's controls by the amount beside it
(right-click: amounts, which controls to leave out; right-click a knob to leave it out).
RESET (right-click a page key, or RANDOM's menu) puts a page, or every page, back to its
defaults, keeping the routes, the sample and ROOT. Both are one undo step.

**CHAIN:** drag the four effects into any order; each has an ON switch (on its block and
the page's last four slots) that cuts it at once, fading over 5 ms so it doesn't click.

**MIDI:** notes on any channel (omni). The sustain pedal (CC 64) holds notes until it's let
up; all-notes-off / all-sound-off end everything, pedal included. Pitch bend, the mod wheel,
aftertouch and velocity are MIDI modulator sources, not fixed pitch bend.

**Undo:** knobs, menus and buttons (per gesture), routes, samples (loads, takes, trim,
reverse, normalise), slices, RANDOM, RESET and preset loads. Not undone: host automation,
and opening a project (which starts its own history).

**Samples:** WAV, AIFF, FLAC, MP3 and Ogg; up to 30 s (a longer file plays its first 30 s
and says so), two channels (a file with more plays its first two), 1 kHz–768 kHz. A file Silt
can't read leaves the sample there was and says so. The screen shows the length, rate,
mono or stereo, and the memory it takes.

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
