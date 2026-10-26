# MOD section — layout, advanced assign, per-source colours, easy removal

A design proposal for the three things you asked about, written against the code as it is
today. Companion to [`review-2026-10-25.md`](review-2026-10-25.md); findings F1–F14 are
referenced where they matter.

**Status, 26 Oct 2026: implemented** (phases 0–9), with these differences:

- Clamped routes end in a stop across the ring rather than a striped arc.
- The route list shows a route's span in the parameter's units ("480 Hz to 2.64 kHz")
  rather than a delta ("+2.4 oct"), which reads the same for every parameter type.
- Muting is also on the MOD page (an ON button under TYPE), not only the chip menu.
- "Clear N routes…" from a chip's menu asks in a dialog; the hover × asks by a second click.
- Routes saved on parameters that are no longer targets (choices, bools) are dropped
  on load rather than listed, since nothing can apply them.
- MOD slot 7 (ASSIGN) arms the shown modulator and lights only while that one is armed.

§8 "later" ideas: per-voice spread is done (see `per-voice-modulation.md`); the rest follow.

---

## 1. What's wrong with the MOD page today

### 1.1 You can only see one modulator, and only if you navigate to it

Every modulator's entire control set is created up front, all in the **same eight slots**,
and gated by a `shown` lambda (`PluginEditor.cpp:684-757`):

```cpp
addKnob (Page::mod, 2, id ("Shape").toRawUTF8(), "SHAPE").shown = wave;
addKnob (Page::mod, 2, id ("Smooth").toRawUTF8(), "SMOOTH").shown = random;
addKnob (Page::mod, 2, id ("Attack").toRawUTF8(), "ATTACK").shown = envelope;
```

That's 17 controls × 4 modulators = **68 controls for 7–8 visible at a time**, and the only
way to know what the other three are doing is to switch the `modSelect` combo. There is no
overview, no live value, and no route information anywhere on the page.

### 1.2 Assigning costs two page changes

To route MOD 3 to CUTOFF you must: go to MOD → set the picker to MOD 3 → press ASSIGN → go
to FILTER → drag CUTOFF. Then to check the depth you go back to MOD. Every depth edit is a
round trip, and while ASSIGN is on **the knob's own value can't be changed** — dragging is
hijacked (`FineSlider::mouseDown` returns early, `PluginEditor.h:44-50`).

### 1.3 There is no way to see or audit what is routed

`routeAmounts` is a 4 × 160 array. Nothing lists it. A patch with 12 routes across four
modulators is unauditable: you can only discover routes by hovering every knob on eight
pages and reading its rings. Related: routes created from the controller on menu slots are
invisible *and* unclearable (finding **F1**).

### 1.4 Nothing shows the modulators' output

`modOutputs` is computed every 64 samples (`PluginProcessor.cpp:862`) and never read
(finding **F14**). So a WAVE mod at 0.2 Hz gives no feedback at all: you cannot tell whether
it is running, retriggering, in release, or clamped.

### 1.5 The colour story is inconsistent

The four mod colours are a local `static` inside `drawModulation` (`:949`), used only for
rings; the MOD page shows no colour at all, and the four header stripes use a different set
of four colours (**F8**). So "different colours per mod source" is half-implemented: it
exists on knobs and nowhere else.

### 1.6 Removing is one gesture, in one mode

Double-click under ASSIGN removes the armed mod's route; right-click gives "Clear MOD n
(37%)" / "Clear all modulation". Both are decent, but they're attached to *the knob*, not to
the *modulator*: there is no "clear everything MOD 3 does", no way to remove a route for a
menu/button target, and no confirmation for the destructive "clear all".

---

## 2. Principles for the redesign

1. **Never require a mode switch to assign.** Arming a source should be a side-effect of a
   gesture you're already making, and un-arming should be automatic or one key.
2. **Plain drag always means "value".** Depth must be a modifier gesture (Alt) so you can
   still play the instrument while assigning.
3. **Colour is the modulator's identity, everywhere.** One palette, one meaning: M1 is rust
   on every page, in the rail, in menus, in the route list, and in the header.
4. **Every route is inspectable and removable from one place** (a list), no matter what kind
   of control created it — that also closes F1 permanently.
5. **Don't delete modulators.** Four fixed slots, ids `m1…m4`, saved by parameter id. "Remove
   a mod source" = remove its routes and/or mute it. Deleting would renumber ids and break
   saved projects; four idle modulators cost ~nothing (an idle mod is one `advance()` per 64
   samples).
6. **Don't move the controller mapping.** The redesign is a UI addition; `slotParameter()`
   keeps the same page/slot layout so existing 8-knob muscle memory and saved CC maps still
   work.

---

## 3. The new MOD page

### 3.1 A permanent mod rail (visible on every page)

Add a 118 px rail to the left of the control row, spanning all pages. The layout budget
today is 1008 × 158 px for the control row, so after the rail there are 890 px for eight
slots (111 px each instead of 126 — knobs lose 15 px of width, still comfortable).

```
 page tabs   GRAIN  SHAPE  PITCH  PLAY  RESONATOR  FILTER  COLOR  SPACE  MOD
 ┌────────────┬───────────────────────────────────────────────────────────┐
 │ ■ M1 WAVE  │                                                           │
 │ ▁▂▅▇▅▂▁  3 │                                                           │
 │ ■ M2 RAND  │                    (8 slots, as today)                    │
 │ ▂▁▃▂▁▃▂  0 │                                                           │
 │ ■ M3 ENV   │                                                           │
 │ ▁▁▅▆▅▁▁  2 │                                                           │
 │ ■ M4 MIDI  │                                                           │
 │ ▁▁▁▁▁▁▁  1 │                                                           │
 └────────────┴───────────────────────────────────────────────────────────┘
    118 px                               890 px
```

Each chip (34 px tall) shows:

- a filled swatch + `M2` in the modulator's colour (the identity),
- its type (`WAVE`, `RANDOM`, `ENV`, `FOLLOW`, `MIDI`),
- a live meter fed by `modOutputs[m]` — bipolar sources draw from the centre outwards,
  unipolar ones from the left, so an envelope reads differently from an LFO at a glance,
- the number of routes it has (`3`), which is the "where is this used" answer at a glance,
- **ARMED** state: the chip inverts to its colour when it's the armed source,
- on hover: a small `×` that clears that modulator's routes (two-step, see §6).

Rail interactions:

| Gesture | Result |
|---|---|
| click | select on the MOD page **and arm** (so you can go straight to a knob) |
| shift-click | select without arming |
| right-click | `Edit`, `Mute`, `Clear 3 routes…`, `Mute all others` |
| drag onto a knob | create a route at +25 % (§4.4) |

The rail is also the fix for F10: the armed state is always on screen, so a controller user
can see that ASSIGN is on.

### 3.2 The focused editor, still 8 slots

The MOD page keeps its controller mapping exactly (slot 0 = picker, 1 = type, 2–6 =
type-specific, 7 = ASSIGN), so no controller behaviour changes. On screen, slot 0 no longer
needs a combo box — the rail selects — so that cell becomes a read-only banner for the
selected source (`M2 · RANDOM · 3 ROUTES`), and the cell's knob (controller knob 1) still
picks a modulator exactly as today. Slot 1 stays the TYPE menu, 2–6 the type settings, and
7 ASSIGN.

The **ROUTES** button lives at the right end of the strip row (the ARM / LOAD / LEARN row),
not in a page slot, so it is reachable from every page and doesn't cost a knob.

### 3.3 The route list (the "audit / remove" view)

An overlay panel over the control row (no layout budget needed), opened from:

- the `ROUTES` button in the strip (always visible),
- any knob's right-click → `All routes…`,
- a chip's right-click → `Show routes`.

```
┌ ROUTES ─────────────────────────────────────  ALL ▾   [×] ──┐
│ ● M1  Filter cutoff      +37 %   (+2.4 oct)            [×] │
│ ● M1  Spray              −18 %                         [×] │
│ ● M2  Grain size         +62 %   (+90 ms)              [×] │
│ ● M2  Dry/wet            +12 %                         [×] │
│ ● M4  Resonator decay    −40 %   (−0.6 s)              [×] │
│                                                             │
│ 5 routes · 2 muted sources            [ CLEAR ALL… ]        │
└─────────────────────────────────────────────────────────────┘
```

- Rows are 22 px; six fit; the list scrolls in a `Viewport` (at most 72–73 targets × 4
  modulators, but only non-zero routes are listed, so realistically 5–30 rows).
- Drag a row's percentage up/down to edit the depth in place; double-click to type it.
- `×` removes one route; `CLEAR ALL…` is two-step.
- The depth in the parameter's own units (`+2.4 oct`, `+90 ms`) comes free from
  `RangedAudioParameter::getTextFromValue` on the base and modulated values — this is worth
  having because ±100 % of *cutoff* means something very different from ±100 % of *mix*.
- Filter by modulator (`ALL / M1 / M2 / M3 / M4`) and by page — the "where did I put that
  random mod" answer.
- Sorted by page/slot order, which is the same order the parameters were created in, so it
  reads like the panel top to bottom.

This view is also what makes F1 fixable: any route, including one on a menu or button
target, appears here and can be deleted — even if no knob can show it.

### 3.4 Pixel budget check

```
editor 1040 × 600
  header 44     screen 272     strip 34     page panel 192 (tabs 28 + control row 158)
                                                       └ rail 118 inside the 192
route list overlay: covers the control row (890 × 158) → 6 rows + footer. Scrolls.
```

No change to the screen, header or strip is required. The MOD page itself needs no extra
page, which matters because the nine tabs already fill the tab row (**F11**).

---

## 4. Advanced mode: assign without leaving the page

The name in the UI: **DEPTH** (on the rail) or **QUICK ASSIGN**. Two ingredients: an armed
source, and a gesture that doesn't conflict with editing values.

### 4.1 Arming

`SiltProcessor::assigning` already *is* "the armed modulator" (−1 = none). Rename it
`armedMod` for clarity; keep the semantics for the controller identical (knob turn while
armed = depth, as today).

Mode-free arming:

- click a chip (§3.1),
- `1`–`4` on the keyboard to arm M1–M4, `Esc` to disarm,
- drag a chip onto a knob (§4.4).

### 4.2 Depth gesture (mouse)

| Gesture | Today | Proposed |
|---|---|---|
| drag knob | value, unless ASSIGN is on | **always value** |
| Alt+drag knob | — | **depth for the armed mod** (or the knob's only route) |
| Shift+Alt+drag | — | fine depth (10×) |
| right-click knob | clear routes | **route editor** (§4.3) |
| double-click knob | reset value, or clear route under ASSIGN | **always reset value** |
| Alt+double-click | — | remove the armed mod's route (replaces today's ASSIGN double-click) |
| drag chip → knob | — | create route at +25 % |
| keys `1`–`4` / `Esc` | — | arm / disarm |

`FineSlider` already has the machinery — `settingAmount`, `amountAtStart`, and the
150 px/1000 px drag scale — so Alt+drag is a small change:

```cpp
void mouseDown (const juce::MouseEvent& e) override
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }

    const int m = depthModulatorFor (target);      // armed → only route → last used
    depthDrag = e.mods.isAltDown() && m >= 0;
    if (depthDrag)
    {
        depthMod = m;
        amountAtStart = routeAmount (m, target);
        showDepthReadout = true;          // "MOD 2 → CUTOFF  +37%"
        return;
    }
    juce::Slider::mouseDown (e);          // plain drag is always the value now
}
```

which modulator Alt-drag picks, in order:

1. the armed one, if any;
2. else, if the knob has exactly one route, that modulator (so you can tweak an existing
   route without arming anything);
3. else the last-used modulator (editor-only state, defaults to M1), and flash
   `MOD 1 — ALT-DRAG AGAIN TO CHANGE` in the LCD, with `1`–`4` to pick.

Rule 2 is the one that removes most round trips in practice: "adjust what's already there"
never needs a visit to MOD.

### 4.3 Right-click: the route editor for one knob

Replace the current "Clear MOD n (37 %)" list with per-modulator depth editing. JUCE's
`PopupMenu::addColouredItem` gives each entry its source colour, so the menu teaches the
colour mapping:

```
┌─────────────────────────────────────────┐
│ ● MOD 1  WAVE          37 %          ▸  │   ← submenu: +50 / +25 / +10 / 0 / −10 …
│ ● MOD 2  RANDOM        —                │   ← "Add at +25 %" when absent
│ ● MOD 3  ENV          −18 %          ▸  │
│ ● MOD 4  MIDI         —                 │
├─────────────────────────────────────────┤
│ Clear all modulation on this knob       │
│ Show all routes…                        │
└─────────────────────────────────────────┘
```

That's 2 clicks to remove one route and 2 to clear a knob, from any page, without arming
anything — the current "double-click under ASSIGN" becomes an optional shortcut rather than
the only path.

### 4.4 Drag a source onto a knob

Standard modulation UX and the fastest possible assign: `SiltEditor` inherits
`juce::DragAndDropContainer`, `ModChip::mouseDrag` calls `startDragging`, and each knob's
`isInterestedInDragSource` / `itemDropped` creates the route.

```cpp
// FineSlider also inherits juce::DragAndDropTarget
bool isInterestedInDragSource (const SourceDetails& d) override
{
    return d.description.toString().startsWith ("mod:");
}
void itemDropped (const SourceDetails& d) override
{
    const int m = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue() - 1;
    setDepth (m, target, 0.25f);          // a musical starting depth
    repaint();
}
```

Add a hover highlight while a chip is dragged over a knob, and draw a preview arc at the
would-be depth. This is the single most satisfying part of the redesign and it is ~60 lines.

### 4.5 Feedback while doing it

- Rings already show depth; add the **modulator number** at the arc's inner start so colour
  isn't the only cue.
- While Alt-dragging: the LCD shows `MOD 2 → FILTER CUTOFF   +37 %  (+2.4 oct)`.
- A route that would be clamped (base + depth > 100 %) draws its arc to the clamp point in a
  striped style, so "why does more depth do nothing" is visible.
- When armed, every modulatable knob gets a faint dashed ring in the armed colour, so you
  can see at a glance what *can* be targeted (menus and buttons won't have one — which also
  documents F2's policy).

---

## 5. Colours per mod source

### 5.1 One palette, four sources

New file `plugin/ModColours.h`, the single source of truth (also replaces the duplicated
values in `drawModulation` and the header stripes):

```cpp
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

namespace silt::ui
{
struct ModColour
{
    juce::Colour base;    // rings, chips, menu items
    juce::Colour dim;     // unfilled arcs, muted state
    juce::Colour ink;     // text drawn on top of base
    const char*   name;   // spoken / written label, never rely on hue alone
};

inline const std::array<ModColour, 4> modColours { {
    { juce::Colour (0xffa64826), juce::Colour (0xff5c2714), juce::Colour (0xffefe6cf), "RUST"   },
    { juce::Colour (0xffd29a2c), juce::Colour (0xff7a5513), juce::Colour (0xff2a2420), "BRASS"  },
    { juce::Colour (0xff6f8f4e), juce::Colour (0xff3a4d28), juce::Colour (0xffefe6cf), "MOSS"   },
    { juce::Colour (0xff4f62a8), juce::Colour (0xff2a3560), juce::Colour (0xffefe6cf), "COBALT" },
} };

inline const ModColour& modColour (int m) noexcept
{
    return modColours[(size_t) juce::jlimit (0, 3, m)];
}
} // namespace silt::ui
```

These are the four colours already on the rings (kept, because they sit well on the bone
panel and the brass knobs), promoted to a single token file.

### 5.2 Where the colour appears

| Place | How |
|---|---|
| Knob rings + live dot | as today, from `modColour(m)` instead of a local array |
| Ring number | small `M1`…`M4` at the arc start (colour + number, not colour alone) |
| Mod rail chip | swatch background, type text, meter bar |
| Header stripes | the four stripes become the four modulator colours **and animate as meters** — the existing motif finally means something (fixes F8) |
| Right-click menu | `addColouredItem` per source |
| Route list | colour dot + `M1` label |
| MOD tab | a 4-colour underline on the tab while armed, so the tab itself reports state |
| Muted source | chip drawn in `dim`, ring hidden entirely |

### 5.3 Accessibility

Rust and moss are the pair most likely to blur for deuteranopia, so colour is never the only
channel:

- every ring carries its `M` number,
- every chip and row carries text (`M2 RANDOM`),
- each source gets a distinct ring texture: solid / long dash / short dash / dotted
  (a `PathStrokeType` with `createDashedStroke`, or four precomputed stroke types — no per-frame
  allocation).

That also makes screenshots and printed docs readable in greyscale, which matters for the
`README` and the listening-set notes.

---

## 6. Removing mod sources, in three levels

The phrase "remove a mod source" can mean three things; all three should be one or two clicks:

### 6.1 Remove one route

- Right-click a knob → the source's submenu → `Remove`, or the `×` in the route list,
- `Alt+double-click` the knob while that source is armed,
- `Alt+drag` its depth to 0 (with a small snap zone near 0, since the drag is 150 px for the
  full ±100 % range — 0 is 1.5 px wide otherwise).

### 6.2 Clear everything one source does

- Hover `×` on the chip → clears that modulator's routes, with a two-step label
  (`CLEAR 3 ROUTES` → `CLICK AGAIN`) because there's no undo (see §8),
- right-click the chip → `Clear 3 routes…`,
- `CLEAR ALL` in the route list footer for everything, also two-step.

Processor additions (message thread):

```cpp
void clearRoutesForModulator (int m) noexcept;              // one pass over the targets
int routeCount (int m) const noexcept;                      // for the chip's badge
std::vector<Route> allRoutes() const;                       // for the route list
juce::String targetName (int target) const;                 // "Filter cutoff"
juce::String depthText (int m, int target) const;           // "+37%  (+2.4 oct)"
```

`clearRoutes(target)` exists; naming the pair `clearRoutesForModulator` /
`clearRoutesOnTarget` stops the ambiguity that makes this feature hard to discuss.

### 6.3 Disable a source without losing its routes

New per-modulator parameter, added additively (ids stay stable, old projects load with
`true`):

```cpp
layout.add (std::make_unique<juce::AudioParameterBool> (
    juce::ParameterID { id ("Enable"), 1 }, name ("enabled"), true));

// applyModulation
value[(size_t) m] = enabled[m] ? modulators[(size_t) m].advance (mp, modContext) : 0.0f;
```

Muting means: its contribution disappears, its routes survive, the ring goes dim, the chip
shows `MUTED`. That is the "remove a mod source" that most people actually want (turn off the
wobble for a section, keep it for the next one) and it's a one-line DSP change.

Note this adds 4 parameters (153 → 157 per build). Targets stay at 72–73 because `m*` params are already
excluded, so `maxTargets` headroom is unchanged (but do F4 anyway).

### 6.4 What not to do

Do **not** implement "delete modulator / add modulator". Reasons:

- The state format stores routes as `modulator:parameterId:amount` with modulator `0…3`.
  Removing one renumbers the rest and silently rewrites every saved project's routing.
- Every `mN*` parameter id is part of the host's automation surface; deleting them breaks
  automation lanes in existing sessions.
- The UI cost of four idle chips is 136 px; the DSP cost of four idle modulators is
  negligible (`applyModulation` already skips targets with a zero offset).

If you later want more than four, the migration-safe route is **add** `m5…m8` (default
disabled, chips appear only when used) rather than making the count dynamic. That keeps every
existing id and route string valid.

---

## 7. Implementation plan

Phase order is chosen so each step is shippable and none of them require the next.

| Phase | Work | Files | Effort | Ships |
|---|---|---|---|---|
| **0** | Fixes from the review that touch this area: exclude switch-like targets (F2/F1), `maxTargets` guard (F4), `static views` → member (F5), `param()` null-safe (F6), `<cstddef>` (F3) | processor, headers | 0.5 d | bug-fix release |
| **1** | `ModColours.h`; palette used by rings, header stripes (as static colours first), right-click menu items | `ModColours.h`, editor | 0.5 d | colour identity |
| **2** | Mod rail: 4 chips with swatch, type, route count, live meter from `modOutputs`; click = select + arm; shift-click = select | new `ModPanel.*`, editor | 1.5 d | **overview + arming without the MOD page** |
| **3** | Alt+drag depth, `depthModulatorFor()`, LCD readout, Alt+double-click remove, dashed "assignable" hints | `FineSlider`, editor | 1–1.5 d | **the advanced mode you asked for** |
| **4** | Right-click route editor per knob (per-source submenus, add/remove, clear) | editor | 0.5–1 d | **removal from anywhere** |
| **5** | Route list overlay (viewport, edit in place, remove, clear all, filters) + `allRoutes()`/`routeCount()`/`depthText()` | processor + new `RouteList.*` | 1.5–2 d | **audit + bulk removal** |
| **6** | `mNEnable` mute + chip menu + dim/muted states | processor, editor | 0.5 d | disable a source |
| **7** | Drag chip → knob, keys 1–4/Esc, animated header stripes | editor | 1 d | polish |
| **8** | Shared page table (single source of truth for editor + `slotParameter`), split `PluginEditor.cpp`, resizable window (F11) | all plugin files | 1.5–2 d | maintainability |
| **9** | Tests + snapshot pages | `tests/`, `tools/snapshot` | 1 d | safety net |

Total ≈ 8–10 days. Phases 2–4 are the actual request and are ~3–4 days; phase 5 is what makes
removal genuinely easy and is worth doing right after.

### Tests to add alongside

1. `depthModulatorFor()` resolution order (armed → single existing route → last used).
2. Alt-drag creates exactly one route and leaves the parameter's own value untouched.
3. Route list enumeration equals the set of non-zero `routeAmount` pairs, for every
   modulator/target combination — including a manually created route on a menu target.
4. `clearRoutesForModulator(m)` only touches `m`.
5. Muted modulator contributes 0 and round-trips through state.
6. **Editor layout ↔ `slotParameter` equality** (review test #1). This one is worth doing
   *before* phase 2, because the rail changes layout code and the test protects the
   controller mapping while you do it.
7. A `silt_snapshot` PNG per page including MOD and the route list, so UI PRs have a visual
   diff.

### Risks

| Risk | Mitigation |
|---|---|
| Rail steals horizontal space from knobs | 118 px leaves 111 px slots; verify on the snapshot tool before committing to it; a collapsed rail (28 px, swatches only) is a fallback |
| Alt is claimed by the host or OS on some platforms | Alt+drag is additive; right-click and drag-and-drop cover the same need |
| Keyboard shortcuts fire while typing a value | check `getCurrentlyFocusedComponent()` isn't a `TextEditor` before handling `1`–`4`/`Esc` |
| Route list grows large | it lists only live routes, in a `Viewport`, with filters; 30 rows is a busy patch |
| Colour-only communication | numbers, text labels and ring textures everywhere (§5.3) |
| No undo for "clear all" | two-step confirm, and §8 below |

---

## 8. Ideas for later (not part of this)

- **Undo.** `params` is built with a `nullptr` undo manager
  (`PluginProcessor.cpp:382`). Wiring a `juce::UndoManager` would make route edits and
  "clear all" undoable with host undo, and it's mostly plumbing.
- **Depth in real units as the default readout** rather than %: the route list already
  needs `depthText()`; the knob readout can reuse it.
- **Macros.** A fifth modulator *type* — `MACRO` — whose value is a knob (host-automatable)
  gives four macro knobs that route exactly like any other source, for the cost of one
  `advance()` case. Very high value per line given this architecture, and it fits the rail
  as a second row of chips.
- **Per-voice spread** (already on the roadmap): modulation is applied once per 64-sample
  chunk into a global `modulated[]`. Per-voice needs the offset applied *inside* the voice
  loop instead — e.g. keep `modulated[]` as the block base and add a per-voice `spread`
  offset when `GrainVoice` reads its parameters. Worth designing before the mod matrix grows,
  because the read path is shared by eight effect classes.
- **Cross-modulation** (MOD B → MOD A's rate) is a one-line DSP change but needs a decision
  first, because modulator params are currently read straight from the parameter rather than
  from `modulated[]`; mixing the two would make the update order matter.
- **Presets.** 72–73 modulatable parameters and no preset browser is the biggest remaining
  product gap, and `modRoutes` already serialises cleanly.
- **Resizable panel** (F11) is a prerequisite for the rail on smaller screens; a
  `setResizeLimits` plus persisting the editor size in the state tree is enough.
