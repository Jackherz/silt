# Handoff: Silt in the browser, on jackherz.com

Written 18 November 2026. For an agent starting fresh. The goal: an interactive browser version
of Silt that people can play on jackherz.com, the way they can already play Brood there. Read
this whole page before writing code, and settle section 8 with the owner first.

## 1. Where things stand

- **Silt** (`~/Documents/AI/silt-plugin`, GitHub Jackherz/silt, public, GPL v3). Work on `main`;
  the owner wants no other branches. Latest: `c6a5b86` (panel clean-up). The kit submodule is at
  `3910a42`, the same as Brood's.
- The panel at `c6a5b86` is the reference. It is new today; the owner liked it but hasn't tried it
  in Live yet:
  - masthead: the "SILT" wordmark, no chip and no full stop;
  - a strip with SOURCE (Silt FX: LEVELS), CHAIN and the readout;
  - the full-width screen;
  - a key row with GRAIN, SHAPE, PITCH, PLAY and MOD, then LEARN, RANDOM, the amount box,
    MOD ROUTES, UNDO and REDO;
  - the well: the mod rail and eight slots;
  - the Bench along the bottom.

  The effects' pages open from their chain blocks; each block's light is its ON switch. Only the
  open page wears its colour. The fonts are Brood's: B612 Mono for the screen, Overpass Medium
  for names, Chivo Mono for values (`resources/fonts/`, OFL).
- **Brood's browser version** is the model to follow. It is live at jackherz.com/#brood
  (site `8b54711`, built from Brood `21a6009`).

See the reference panel without a window:

```bash
cmake --build build --target silt_snapshot silt_snapshot_fx
build/silt_snapshot_artefacts/Release/silt_snapshot resources/samples/rain-bells.flac out/ [param=value ...]
```

This writes one PNG per page (grain.png … chain.png), plus routes.png, presets.png and tour-N.png.
Test commands: `build/silt_tests` (core) and
`build/silt_plugin_tests_artefacts/Release/silt_plugin_tests` (plug-in). Both must stay green.

## 2. How Brood did it (copy the approach, not the DSP)

Repo `~/Documents/AI/Brood-Source`:

- `src/main.jsx` (React 18), `src/style.css`, `src/audio-engine.js` and `src/brood-worklet.js`
  (the DSP, an AudioWorklet), built with Vite (`package.json`, `vite.config.js`).
- The plug-in's 1200-wide layout is drawn on a fixed design canvas and scaled to the window, so
  nothing reflows. A separate 390-wide phone layout appears at 700 px and narrower. See
  `DESK`/`deskLayout` and `PHONE`/`phoneLayout`.
- Same names, help text, fonts and wording as the plug-in. The help is copied verbatim from the
  editor's source. Text on the dark screen must never be cut off: it shrinks, then wraps
  (`ScreenLine`, `HelpText`).
- A source picker sits in the row where the plug-in has the Bench. It offers DEMO, FILE and MIC,
  LOAD FILE, TEMPO and START AUDIO. An "IN THE PLUG-IN" sheet lists what differs, both ways.
- A tour that opens on a first visit (localStorage `brood-tour-v1`). It fades whole controls
  using `inert`. Steps have ordered tasks and never advance by themselves (the owner's rule).
- The plan it was built from: `docs/plans/handoff-step6-browser.md`. Use it as a model for
  structure and level of detail.
- Tests: `npm test` (node), plus a browser smoke test in Brave:
  `CHROMIUM_PATH="/Applications/Brave Browser.app/Contents/MacOS/Brave Browser" BROOD_TEST_URL=http://localhost:5174/ npm run test:browser`.
- **Brood's DSP was ported to JavaScript by hand** and is kept identical to the C++ by a parity
  harness (`tests/parity`). That is a lot of upkeep. Section 3 recommends something different for
  Silt.

## 3. Architecture (recommended)

### DSP: compile Silt's own C++ to WebAssembly

`core/` is plain C++ with no JUCE: the engine, grains, resonator, filter, COLOR, SPACE,
modulators, onsets and snap. It is about 4,700 lines and uses the kit's JUCE-free DSP headers
(`kit/include`). Compile it with Emscripten into a WASM module running inside an AudioWorklet.
The browser then plays exactly the plug-in's DSP, with no second implementation and no parity
drift.

What's missing is the glue. `SiltProcessor::processBlock` (`plugin/PluginProcessor.cpp`, from
about line 1009) is JUCE code. It does the following:

- MIDI, the sustain pedal and all-notes-off;
- rendering in chunks of 64 samples, with modulation updated per chunk;
- FOLLOW's chord;
- synced rate;
- sample gain and MIX;
- the chain in its order, with about 5 ms fades between orders;
- each module's ON fade;
- the change and output meters behind the chain blocks;
- RESAMPLE capture.

There are two ways to provide it:

- **A (no change to the plug-in):** write `web/engine/SiltWeb.cpp`, a JUCE-free copy of that glue
  that reads parameters from a flat array.
- **B (better long term, touches the plug-in):** move the glue into `core/` as a JUCE-free
  `silt::Instrument` that both the plug-in and the web build use. Then `processBlock` becomes
  parameter reading plus a call into it.

B means one implementation. A means two that can drift apart. Ask the owner (section 8). If B,
the plug-in's tests must pass unchanged, and renders before and after must match:
`tools/render` and `silt_snapshot` give you references.

The audio path:

1. The main thread decodes the sample with `decodeAudioData`.
2. It transfers the channels to the worklet, which copies them into WASM memory
   (`SampleBuffer`).
3. Notes, parameter changes and the transport go over the worklet port.
4. Meters, grain positions and modulator values come back over it at screen rate for the
   display.

The worklet's block is 128 frames; Silt's chunk is 64, so the two fit cleanly. Set
`Engine::prepare` with the context's sample rate.

### Single source of truth for names, ranges, layout, help and presets

These live in the plug-in and use only `juce_core`:

| What | Where |
| --- | --- |
| Parameters (IDs, ranges, defaults, choices) | `createParameterLayout` in `plugin/PluginProcessor.cpp` |
| Which control sits in which page and slot, per mode | `plugin/PageLayout.cpp` |
| Hover help, about 163 entries | `plugin/Help.h` |
| Factory presets and their categories | `plugin/Presets.cpp` |
| Tour steps | `SiltEditor::tourSteps` in `plugin/PluginEditor.cpp` |

Don't retype these. Add a small console tool, `tools/export-web/`, built by CMake from the same
sources. It writes `params.json`, `layout.json`, `help.json` and `presets.json` for the web app,
and runs whenever they change. The page/slot table has a test that checks the editor and the
controller agree; the web layout should read the same JSON.

### Front end

Use Vite + React like Brood (same toolchain, so the site's build steps carry over). Put it in a
`web/` folder in the Silt repo, with its own `package.json`. Brood keeps its browser version in
the same repo, under `src/`.

## 4. What the browser version shows

Same panel, same names, same help, same fonts, same Graphite theme (the plug-in's default,
`defaultTheme = 4` in `plugin/Palette.h`). Draw it on the plug-in's 1200 × 820 design canvas
minus the Bench, scaled to the window. Take every rectangle from `SiltEditor::resized()` and every
colour from `plugin/Palette.h` and `plugin/PageColours.h`.

The parts:

- **Masthead:** the wordmark is Glyph Grotesque at 48. The kit has it in `kit/fonts/` (OFL),
  with "GLYPH AUDIO / GRANULAR SAMPLER" beside it. Presets (arrows, name, SAVE), with the preset
  browser (kit `Presets.h`: search, categories, favourites).
- **Strip:** SOURCE (ARM, bars, RESAMPLE, LOAD, MONITOR), the CHAIN blocks (light, name, meter or
  idle reason or OFF, drag to reorder, click for the page, RES again for its next half), and the
  readout (`kit::drawReadout`: title, LIVE chip, value, hover help that shrinks and wraps).
- **Screen** (`plugin/GrainScreen.h`, `plugin/ScreenInsets.cpp`, `plugin/SpaceScene.cpp`):
  - the waveform with the region, playheads and slices;
  - the grain field, with each theme's look;
  - the IN/OUT scope strip;
  - the page's inset picture in the bottom right corner.

  Draw it with Canvas 2D. ZOOM shows at 1x only while the mouse is over the screen.
- **Key row and well:** the page keys, the actions, the mod rail (four chips with live traces;
  drag a chip onto a knob to route it), and the eight slots with rings for routes. Slots follow
  the modes, from layout.json.
- **Not in the browser:** the Bench (there's no other plug-in to pass audio to), DRAG out to a
  DAW, host automation and the sidechain. A short "In the plug-in" note should say so, as Brood's
  does.

## 5. What the browser needs that the plug-in gets from the host

- **Notes.** Silt is an instrument. Offer:
  - an on-screen keyboard (C1 upwards matters for SLICE: slices start at MIDI 36);
  - the computer keyboard;
  - Web MIDI where the browser allows it. If MIDI works, LEARN can work with CC too.

  The owner decides how much of this goes in (section 8).
- **Tempo and transport.** There's no host, so add TEMPO and a play state for synced rate, ARM
  bars and the delay.
- **Samples:**
  - the four factory samples in `resources/samples/*.flac` (GPL with the plug-in), served next to
    the app;
  - LOAD and drag-and-drop of the user's own file;
  - recording from the microphone, replacing ARM's sidechain.
- **LIVE and FEED modes.** LIVE grains the incoming audio; in the browser that means the
  microphone. Silt FX's role is LIVE mode with a microphone, so one instrument page probably
  covers both (section 8).
- **Start audio.** Browsers need a click before audio starts, and iOS stops audio on screen lock.
  See how Brood handles it in `src/audio-engine.js`.

## 6. The website

The site is `/Users/jack/Documents/Codex/2026-11-14/files-pasted-by-the-user-creative/outputs/jackherz-site`,
private repo Jackherz/jackherz-site. It is plain HTML, CSS and JS on Cloudflare Pages.

- Brood is embedded from `brood-app/`. That folder is a Vite build (`--base /brood-app/`) of a
  pinned Brood commit, plus `brood-app/site-integration.patch` (69 lines):
  - a postMessage when audio starts;
  - `html.embedded` fit-to-width when framed;
  - demo sounds served from `BASE_URL/demos/`.

  The build is made in a scratch `git archive <commit>` copy. `brood-app/SOURCE.md` records the
  commit.
- For Silt, do the same: `silt-app/`, a build of a pinned Silt commit, its own integration patch
  (or, better, build those hooks into the app so no patch is needed), and `SOURCE.md`.
- `app.js` sizes the iframe from the frame's `contentDocument.body.scrollHeight`, watched by the
  frame's own `contentWindow.ResizeObserver`. Wire the Silt page (`#silt`; `content.js` already
  has a Silt entry) the same way Brood's is. `PLUGIN_PAGES_PLAN.md` describes the plug-in pages.
- `index.html` loads `styles.css`, `app.js` and `content.js` with `?v=<first 8 characters of
  sha256>`. Bump that after every edit, or browsers keep the old file.
- To preview locally:
  `python3 -m http.server 8765 --bind 127.0.0.1 --directory <site repo>`, then open
  `http://127.0.0.1:8765/#silt`. The built-in browser pane never loads a lazy iframe while it's
  hidden; force `loading='eager'` when testing.
- **Never commit, push or publish the site until the owner has seen the local preview and said
  yes.**

## 7. Rules from the owner

- Same names and wording as the plug-in. The owner's help text is copied exactly, never
  rewritten. Where the browser has to say something new, keep it short and ask about it.
- Nothing on the screen or readout may be cut off, at desktop or phone size. Check it with a
  script that walks every control and measures, as was done for Brood, and confirm the check can
  catch a forced overflow.
- No generic AI design defaults: no purple gradients, no centred hero sections, no Inter or
  Roboto, nothing that looks like a stock template. The look is the plug-in's.
- A tour, if there is one, never advances by itself.
- Ask for a recording before theorising about a sound or behaviour bug.
- Commit to `main` in each repo. Nothing goes public (site, R2, releases) without the owner's
  yes.
- Licences: Silt and the kit are GPL v3. The fonts are OFL; their licence files ship with the
  app. The factory samples are the owner's own and ship under the GPL with Silt. Keep
  `THIRD_PARTY_NOTICES.md` correct for anything new, such as Emscripten's runtime.

## 8. Questions for the owner before building

1. **Glue:** A (a web copy of processBlock's logic) or B (move it into `core/` as
   `silt::Instrument`, shared by the plug-in and the web build)? B touches the plug-in.
2. **Scope of the first version:** the instrument only, with LIVE fed by the microphone covering
   Silt FX's role? Or a separate FX page that takes a microphone or file as input?
3. **Notes:** on-screen keyboard, computer keys and Web MIDI, all three? And LEARN over Web MIDI?
4. **Phone layout:** Brood has one. Does Silt need one in the first version, or is the scaled
   panel enough at first?
5. **Demo material:** the four factory samples only, or also some of the owner's own loops or
   songs (Brood plays the site's songs through `songs.json`)?
6. **Tour:** port the plug-in's 13-step tour, or a shorter one like Brood's?
7. **Performance budget:** Silt's worst case is dense settings with many notes. What should a
   phone manage? Grain budget and resonator modes could be capped lower in the browser if needed.
   Measure before deciding.

## 9. Suggested order, each step reviewed by the owner

1. **Spike:** build `silt_core` to WASM, run one voice on a factory sample in an AudioWorklet
   with hard-coded settings, and measure CPU on a laptop and a phone. Report back before going
   further.
2. **Glue** (A or B as decided), the JSON export tool, and a parameter bridge. Check that the
   browser and `tools/render` produce the same output for a few fixed settings.
3. **Panel**, static first, matching the snapshot PNGs side by side: masthead, strip, screen,
   key row, well.
4. **Interaction:** knobs (drag, Shift for fine, double-click, typed values), menus, page keys
   and chain blocks, modulation drag and rings, RANDOM, UNDO/REDO, presets and the browser,
   hover help on the readout.
5. **Sources and notes:** factory samples, LOAD, drag-and-drop, mic recording, LIVE, keyboard,
   tempo.
6. **Screen visuals:** the waveform, grains and insets.
7. **Tour, the phone layout if wanted, the "In the plug-in" note, and the text-fit check.**
8. **The site:** `silt-app/` build, the `#silt` embed, cache-busting, local preview. Then wait
   for approval.

Done means: Silt plays in the browser with the plug-in's sound, names and look; the plug-in's
tests still pass; nothing is cut off on screen; the owner has approved the local preview of the
site.
