# Silt manual

Everything Silt does, page by page. For what Silt is and how to build it, see the
[README](../README.md); for the code, tests and host results, see
[DEVELOPMENT.md](DEVELOPMENT.md).

- [The two builds](#the-two-builds)
- [The panel](#the-panel)
- [Getting a sample in](#getting-a-sample-in)
- [Playing](#playing)
- [The effects](#the-effects)
- [Modulation](#modulation)
- [Presets, RANDOM and RESET](#presets-random-and-reset)
- [Undo](#undo)
- [8-knob controller](#8-knob-controller)
- [The Bench](#the-bench)
- [The screen](#the-screen)

## The two builds

| Build | Goes on | Captures from | AU type |
|---|---|---|---|
| **Silt** (instrument) | a MIDI track | a sidechain input (another track) | `aumu Slti SltA` |
| **Silt FX** (effect) | an audio track | its own input; MIDI optional | `aumf Sltf SltA` |

Silt FX has no sample of its own: it grains its track's audio, always in Live mode, so
it has no LOAD, ARM, RESAMPLE, DRAG, sample GAIN or sample tools, and no PITCH, FEED or
SLICE modes. Factory presets that need a sample or notes are left out of it.

## The panel

Under the masthead, a strip holds, left to right in signal order: **SOURCE** (Silt FX:
**LEVELS**), **CHAIN** and the **readout**, which explains whatever the mouse rests on.
The screen below runs the full width: the sample or the live input, the grains and
voices, and in its bottom-right corner a picture of the open page. Under it are the page
keys, in three groups (the grains: GRAIN, SHAPE, PITCH, PLAY; the effects: RES, FILTER,
COLOR, SPACE; and MOD), then the open page's eight controls beside the mod rail, and the
Bench along the bottom. Each page has its own colour, on its key, along the top of its
controls and on its chain tile. Shift-drag any knob for 10x finer moves, click its value
to type one, double-click to reset it.

**LEVELS** (Silt FX, always on screen): **INPUT** (-24 to +24 dB, how loud the track
goes into the grains; presets and RANDOM leave it alone. Past 0 dB it drives an old
tape machine: a soft ceiling instead of a peak, more bass, less top and a little hiss
the harder you push), **MIX** and **OUTPUT**.

**MIX** (Silt FX, and LIVE and FEED on Silt): the dry input against the grains. Both go
on through the chain together, so a filter or reverb works on the dry signal too, and
MIX at 0 makes Silt FX a plain effects chain. RANDOM leaves it alone.

**CPU:** while no one can see the window (closed, minimised, on another Space or fully
covered) nothing is drawn; it picks up again when the window comes back. The sound
carries on regardless. Silt keeps running in silence, so free-running modulators stay in
step and long decays aren't cut off.

## Getting a sample in

Drag a file onto the panel, press **LOAD**, or **ARM** and record. Capture waits for the
next bar while the host is playing (or starts at once when stopped), records 1, 2, 4 or 8
bars of the sidechain, then swaps it in. **RESAMPLE** records Silt's own output instead.

**Samples:** WAV, AIFF, FLAC, MP3 and Ogg; up to 30 s (a longer file plays its first 30 s
and says so), two channels (a file with more plays its first two), 1 kHz–768 kHz. A file Silt
can't read leaves the sample there was and says so. The screen shows the length, rate,
mono or stereo, and the memory it takes.

The sample is saved with the project as FLAC.

**Sample gain** (GAIN on the screen's top bar): the sample's level, ±24 dB, for
samples that come in too loud or too soft. Drag it up or down (Shift for fine),
scroll, double-click for 0 dB, or right-click to normalise (peak at −1 dB). The
waveform is drawn at the gain, with anything pushed past full scale in red. Saved
with the set and undoable; not part of presets (it belongs to the sample).

**SNAP and fine steps:** START and LENGTH move in 0.001 % steps. SNAP (under START): OFF, ZERO (region
edges on the nearest zero crossing, for click-free loops) or TRANS (edges on detected
transients, so START/LENGTH pick slices; ticks on the waveform show them). The sample is
analysed once when it's loaded or recorded.

## Playing

16 voices with ADSR; keys transpose around the root note. MODE on the PLAY page picks how
notes play the sample: **PITCH** (the default), **LIVE**, **FEED** or **SLICE**.

**MIDI:** notes on any channel (omni). The sustain pedal (CC 64) holds notes until it's let
up; all-notes-off / all-sound-off end everything, pedal included. Pitch bend, the mod wheel,
aftertouch and velocity are MIDI modulator sources, not fixed pitch bend.

**Playing through a sample** (PLAY page): AT THE END is LOOP, HOLD, STOP or BOUNCE (back and forth, never crossing the seam). SYNC under SCAN sets one pass of the region in bars, 1/4 to 32, from the host's tempo and time signature: the speed follows the host, the position isn't locked to its bar. FREEZE stops the playhead while grains keep coming.

**PITCH page:** RND PITCH (±0–24 st per grain), SCALE (off, chromatic, major, minor,
modes, pentatonics, whole tone, octaves, fifths) and KEY (every grain's pitch is added up
first, then snapped once), PATTERN (a repeating pitch pattern, stepping each grain) with
STEPS and NEW (a new pattern, saved with the set), and HEADS (1–8 playheads per note,
evenly around the region at full SPREAD; in Live mode, further back in time).

**Live mode** (PLAY page, MODE: LIVE on Silt; Silt FX is always Live and has no MODE,
so PITCH, FEED and SLICE are Silt only): the input is written into
an 8 s ring buffer and grains read a DELAY behind the newest audio (ms or synced note
value), with FEEDBACK (grain output back into the ring, DC-blocked, low-passed and
soft-clipped), FREEZE (stop writing) and NO NOTES, what plays grains while no keys are held: OFF,
ALWAYS (a constant voice at the root note) or FOLLOW (the default on Silt FX: the input's
level plays the note envelope; getting louder starts Attack/Decay/Sustain, going quiet
Releases, a sudden jump retriggers, and louder hits play louder). Keys still transpose. Grains are kept
clear of the write head. The screen scrolls the input with the newest audio at the right.

**FEED** (Silt): like LIVE, but the ring takes only Silt's own output, so the grains keep
re-granulating what they just played, blooming as you hold. It starts from the sample.

**SLICE** (PLAY page, MODE: SLICE): keys from C1 up each play one slice of the region,
at the sample's pitch; keys below C1 or past the last slice are silent. SLICE BY: EQUAL
(2–64 parts), AT HITS (the sample's transients) or MANUAL. Drag a slice line on the
waveform to move it, double-click to add or remove one (any of these switches to MANUAL,
copying the slices first); each drag, add or remove is one undo step, and MANUAL slices are
saved with the set. Scroll on the waveform to zoom (to 32×; Shift or a sideways swipe moves
along), drag the bar under it or the ZOOM label, double-click the label to see it all.

## The effects

**CHAIN:** always on the panel, from any page. Each module's switch says ON or OFF. A module that's on but has nothing to do says why (MIX 0, OPEN, NEUTRAL, LEVELS 0); otherwise a meter shows how much it's changing the sound, measured, modulation included. The arrow after a module lights while sound is leaving it. Drag the four effects into any order, click
one to open its page. Each has an ON switch that cuts it at once, fading over 5 ms so it
doesn't click. A controller can still page to CHAIN for ORDER and the four switches.

**RESONATOR** (four pages: press RES again for the next): a modal bank of up to 128
ringing modes. A hit rings each mode as hard at any decay, and retuning one mid-ring
can't click.

- **Page 1:** TUNE (the lowest band), NOTES (SCALE: the PITCH page's scale and key, or
  the root and fifths with no scale; HARMONICS; BELL partials; SAMPLE: up to 128 of the
  sample's own spectral peaks, each as strong as it is in the sample), RANGE (1-8
  octaves the bands reach), DECAY (how long the bands ring once the sound stops, 50 ms
  to 20 s; while sound comes in they stay narrow and pitched, so short decays still
  colour it), BRIGHT (how much quieter and shorter the high bands are), WIDTH (bands
  alternate left and right), FOLLOW (tune to the notes played; TUNE becomes SHIFT,
  which moves the bands up to three octaves either way without leaving the key), MIX
  (0 is an exact bypass).
- **Chords:** with FOLLOW on, every held note gets its own share of the bank and keeps
  its fundamental, and the reshaping below acts on each note from its own fundamental,
  so a chord stays in tune. A note two chords share keeps ringing across the change.
  Letting go of a chord leaves it ringing; a note let go while others stay down leaves
  the chord a moment later.
- **Page 2:** RESONANCE (how narrow the bands are while sound comes in), BANDS (1-128),
  DRIFT, SPREAD (detunes the bands, each its own way), ODD/EVEN, DRIVE (saturates what
  goes in, so it excites more of the bank), DAMP (high bands ring shorter).
- **Page 3** reshapes the series: STRETCH squeezes the top of the bank down towards
  metal or pulls it up, CURVE sets where along the bank that acts, SNAP pulls every mode
  back to whole-number tuning, OFFSET retunes every other mode by up to an octave;
  RIPPLE, DEPTH and SWEEP lay a moving highlight over the bank, picking out peaks
  without changing their tuning.
- **Page 4** sets what makes it ring: STRIKE has each new grain hit the bank directly,
  POSITION is where it lands (route PER GRAIN to it and each grain lands somewhere new),
  HARDNESS is how far up the modes a hit reaches, PARTICLES and DENSITY add crackle that
  follows the sound coming in, TOP brightens what goes in so a dull sound rings the
  high modes, and SATURATE has loud modes hold back and spill into the others while
  quiet ones ring as they were.

The rings are held to the input's peaks, so a tone right on a band can't run away and a
short hit isn't crushed; anything left over saturates softly rather than clips.

**FILTER page** (after the resonator): MODE (OFF, an exact bypass; 12 or 24 dB/oct),
A transistor-ladder model, each of its four stages saturating, run at twice the sample rate.
CUTOFF, RESO (up to just under self-oscillation; the bass dips a little as it rises, and
loud signals press the peak down rather than spiking through), MORPH (low pass → band pass → high pass, blended), DRIVE (up to
+24 dB into the stages, with a little second harmonic), ENV (± the input's level moves the cutoff up to 4
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

## Modulation

Four modulators, each a WAVE (sine → triangle → saw → square; RATE
runs through zero, −50 to +50 Hz: below zero the wave runs backwards and the middle
is STOP, so another modulator on RATE can slow it, hold it and reverse it; synced,
REV runs it backwards against the bar; phase, SYM (where the cycle turns round: 50% as drawn, the ends make
saws and pulse widths), CURVE (bulging and lazy to spiked and snappy; greyed on a
square, which it can't bend), retrigger on notes), RANDOM (smooth to stepped, jitter, free or
synced), ENVELOPE (ADSR from the notes played; REPEAT loops it while held; CURVE bends all of it, sustain level included, so stages run late or early), FOLLOW
(the input's level), MIDI (velocity, note, mod wheel, aftertouch, pitch bend),
MACRO (a knob, automatable and controller-mappable, that moves everything it's
routed to) or PER GRAIN (a fresh roll for each grain and each route, SPREAD even,
centred or at the edges; it reaches only what a grain takes up as it starts, such as
PITCH, SIZE, RATE, SPRAY, REVERSE and DELAY). Design: `docs/mod-section-design.md`.

- **The mod rail**, beside the knobs on every page (drag the bar between them to resize): a chip per modulator with its colour and
  number, type, a two-second trace of what it's doing (its shape, round a centre
  line for bipolar sources) and, when it has routes, a badge with how many. Click
  arms it (outlined in its colour; click again to disarm), Shift-click only selects it, drag it onto a knob
  to make a +25% route, hover × (twice) clears its routes, right-click to edit,
  mute, show or clear. Double-click a chip (or Shift+1–4) to open that modulator's MOD page; again goes back to where you were. Keys 1–4 arm, Esc disarms. The header's four stripes are the
  same four modulators as meters.
- **Depth on any knob:** the coloured rings are handles: press one and drag up or
  down (Shift for fine; it snaps to zero). With a modulator armed, every knob shows
  a dashed ring in its colour: drag that to make the route. Hovering a ring changes
  the cursor and the readout says what it does. Alt-drag on the knob is a shortcut (the
  armed modulator, or the knob's only route, or the last one used); double-clicking a ring
  or Alt+double-clicking the knob removes it. Plain drag on the knob is always its value; double-click resets it.
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
  buttons, controller turns, and modulation edits; the readout says what. A drag, a
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

## Presets, RANDOM and RESET

**Presets** (header: ◀ name ▶, SAVE): a preset is the sound (every parameter, the
modulation routes, the shown modulator), not the sample, the controller mapping or
the window. Factory presets are built in; those that need a sample or notes (SLICE, FEED, BOUNCE, synced SCAN, FREEZE on a sample) are left out of Silt FX. A * after the name means the sound has moved from its preset. Right-click a preset in the browser for "Load only its effects": RES, FILTER, COLOR, SPACE and their order, keeping your grains, routes and name. Loading one is a single undo step. User presets are
`.siltpreset` XML files in `~/Library/Audio/Presets/Glyph Audio/<plug-in>/` on macOS
(`%APPDATA%/Glyph Audio/<plug-in>/Presets` on Windows). SAVE updates a user preset,
or asks for a name and a category. Click the name for the **browser**: search, category
keys (PADS, TEXTURES, DRONES, TONAL, RHYTHMIC, SLICES, USER) and ★ favourites (click a
star; kept in `favourites.txt` in the preset folder). Clicking a preset, or the arrow keys,
plays it at once; KEEP (Return, double-click) keeps it as one undo step, CANCEL (Esc) puts
back the sound you had. Right-click a user preset to rename it, change its category, or
move it to the Trash.

**RANDOM and RESET:** RANDOM's menu (right-click) sets a CHARACTER: ANYTHING, TONAL (stays in tune), RHYTHMIC (short and regular) or TEXTURE (dense and scattered); the last three never touch anything that sets tuning, on any page: PITCH, RND PITCH, SCALE, KEY, PATTERN, ROOT, MODE, or the RESONATOR's TUNE, NOTES, FOLLOW, SPREAD and DRIFT, and COLOR's WOW. RANDOM moves the open page's controls by the amount beside it
(right-click: amounts, which controls to leave out; right-click a knob to leave it out).
RESET (right-click a page key, or RANDOM's menu) puts a page, or every page, back to its
defaults, keeping the routes, the sample and ROOT. Both are one undo step.

## Undo

UNDO and REDO in the strip (and Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z when the
host passes them on) step back through everything changed by hand: knobs, menus,
buttons, controller turns, and modulation edits; the readout says what. A drag, a
controller turn or a clear-all is one step. Depths read in the parameter's units first
("480 Hz to 2.64 kHz (±35%)").

What is undoable: knobs, menus and buttons (per gesture), routes, samples (loads, takes, trim,
reverse, normalise), slices, RANDOM, RESET and preset loads. Not undone: host automation,
and opening a project (which starts its own history).

## 8-knob controller

A controller's 8 knobs move the 8 slots of the current page
(GRAIN / SHAPE / PITCH / PLAY / RESONATOR / FILTER / COLOR / SPACE / MOD), following the modes
(the editor and the controller read the same page table, and a test checks they agree); two buttons step pages; works with
the window closed. Defaults: knobs CC 70–77, pages CC 78/79. **LEARN**: turn knobs 1–8,
then press previous and next page. A knob takes over only when it reaches the
parameter's value (pickup), so changing page never makes values jump. The mapping and
page are saved with the set. Moves are applied off the audio thread as normal automation.

## The Bench

Along the bottom of the panel. SEND records the exact output for 4, 8 or 16 bars from the
next bar line (at once if the host is stopped) and passes it on, or sends the sample if you
choose that. TAKE makes what an effect sent the sample (Silt FX has nothing to take into).
FOLDER opens it in the Finder, chooses another folder, or picks a category, which is a folder
inside it. Silt and Silt FX share a folder, separate from Brood's (`~/Music/Glyph Audio/Silt/` until
you choose), and sending between plug-ins works whichever you choose. Silt keeps the
first 30 seconds of what it takes.

## The screen

**Page pictures:** the screen's bottom-right corner draws the open page as a picture,
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

**SPACE's sky:** on the SPACE page the whole screen becomes a night sky behind the waveform
and the grains, fading in with DELAY and REVERB. DECAY fades from a close, small space (fewer,
larger stars) to a vast one (dense faint stars, the Milky Way); SIZE brings the band in;
REVERB is nebula that breathes with what SPACE is adding; TONE runs from warm dust to blue
nebula and twinkling; SHIMMER sends sparkles up off the waveform. DELAY: light echoes, as round
a star that flares: each note flares, and every echo sends a ring of lit dust out from it, so
TIME is the spacing between the rings, FEEDBACK how many (each fainter), DELAY how bright;
ping-pong alternates them between two stars side by side. FREEZE stops the sky; DUCK dims it
while a note sounds.
