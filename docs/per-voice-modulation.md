# Per-voice modulation — design note and cost

**Status, 26 Oct 2026: implemented** (stages A–D), with these decisions on the open
questions (§12):

1. **Per modulator, not one global knob:** each modulator has a **VOICE** switch (under
   slot 2 on the MOD page; a V badge on its rail chip). Off is bit-identical to before.
   It's an on/off "spread" (each voice runs its own copy, or none do) rather than a
   0–1 blend.
2. **Voice stealing resets** the voice's modulators (as does every new note on a voice).
3. **Envelope** restarts per note (no legato mode yet).
4. **Live mode:** the drone voice is a voice like any other, so per-voice modulators run
   on it, triggered by the follower.

How it's built: the engine (still JUCE-free) has a small `silt::VoiceParams` interface it
calls when a note starts or ends on a voice and, while active, to get each voice's
parameters (`Engine::startNote`, `Engine::releaseVoice`). The processor's
`VoiceModulation` implements it: per voice, its own copies of the per-voice modulators and
its own note context (shared clock, CCs and input level; its own note, velocity and held
state), stepped once per 64-sample chunk; each voice's grain parameters are worked out once
per chunk (only grain targets take per-voice values; the base's 0–1 conversion is done once
per target, §4.3) and copied per stretch. Effects follow the shared modulator.

UI: rings on grain parameters show a pip per sounding voice where its own modulation has the
value (`voiceShown`, `voiceModulated01`). Tests: `tests/TestEngine.cpp` (inactive provider is
bit-identical; notes reach their voices), `tests/TestController.cpp` (per-note envelopes,
decorrelated per-voice random, effects stay global).

The rest of this note is the original proposal (25 Oct 2026).

---

Companion docs: [`review-2026-10-25.md`](review-2026-10-25.md) (§F13 is load-bearing here),
[`mod-section-design.md`](mod-section-design.md) (§8 lists per-voice spread as "next").

## 1. Recommendation

Per-voice modulation is **affordable and well-scoped**, but it should be done in a specific
order:

1. **Now, while the mod path is open:** fix the per-voice *note context* problem (§6) and add
   a `modSpread` parameter defaulting to 0. With `modSpread = 0` the output is bit-identical
   to today, so it can ship in the same pass as the mod-UI work without risk.
2. **Later, once the UI work lands:** apply offsets inside the engine's voice loop (§5). This
   is ~40 lines in `Engine.cpp` plus a param struct.
3. **Only then:** make `modSpread` do something user-visible.

Doing (2) before (1) means writing per-voice modulator state against a global `ModContext`,
which is the part that would then need rewriting. Doing (1) first costs almost nothing and
removes the obstacle.

Measured cost of the *sources*: **about 0.06 % of one core** for 16 voices × 4 modulators
(§4). The cost that matters is elsewhere, and is avoidable (§4.2).

## 2. What "per-voice" can mean here

The signal chain in `processBlock` is:

```
16 voices → sum → Resonator → Filter → Color → Space → gain
```

Only the first stage has voices. So:

| Parameter group | Per-voice possible? | Why |
|---|---|---|
| Grain params (`size`, `rate`, `spray`, `pitch`, `fade`, `reverse`, `jitter`, `randAmp`, `randPan`, `start`, `length`, `scan`, envelope A/D/S/R, `heads`, `headSpread`, pattern depth…) | **Yes** — ~20 continuous targets | Each voice renders independently from its own `GrainParams` |
| Resonator, Filter, Color, Space | **No**, not without N parallel effect chains | They run once on the voice sum; per-voice would be 16× the DSP cost of the whole tail |
| Global knobs (`gain`, `mix`, `monitor`) | No | Not per-voice by definition |

That split is worth stating in the UI eventually: per-voice modulation changes the *grains*,
not the *effects*. A user expecting a per-voice filter sweep will not get one, and labelled
rings can show which targets are voice-scoped.

## 3. How it works today

```cpp
// PluginProcessor.cpp, once per 64-sample chunk
applyModulation();                 // 4 modulators advance once, globally
auto grainParams = readParams();   // reads the modulated copies
engine.process (..., grainParams);
```

and inside the engine, one `GrainParams` per block segment is handed to every voice:

```cpp
for (auto& v : voices)
    v.render (left + pos, right + pos, end - pos, segment, source);
```

`GrainVoice` latches parameters when a grain starts, so a per-voice value only needs to be
right at grain start — not sample-accurate mid-grain. That is a big simplification: no
per-sample parameter smoothing is required for the new values.

## 4. Measured cost

Built and run on Linux, gcc 12, `-O2`, 2 cores, a deliberately slow VM, using `silt_core`
objects compiled from the repo and a small harness kept outside it.

### 4.1 The engine overall

48 kHz, stereo, 512-sample blocks, default patch unless stated:

| Scenario | Share of one core | Realtime factor |
|---|---|---|
| 1 note | 3.6 % | 28× |
| chord of 8 | 8.3 % | 12× |
| 16 voices | 12.5 % | 8× |
| 16 voices, 4 heads | 12.7 % | 8× |
| 16 voices, 4 heads, full spray | 13.0 % | 8× |

This excludes the JUCE wrapper, parameter atomics, and the per-chunk parameter struct
assembly, so treat it as a floor. It says the engine is not close to a limit: **nothing in
this feature needs to be optimised for CPU.**

### 4.2 The modulator itself — cheap

60 s of audio, one `advance()` per 64-sample chunk, worst-case settings:

| Configuration | Share of one core |
|---|---|
| 4 global modulators (today) | 0.004 % |
| 64 per-voice modulators (16 voices × 4) | 0.066 % |
| 64 per-voice RANDOM modulators | 0.055 % |

**Per-voice sources add ≈ 0.06 % of a core.** The memory is a few kilobytes.

### 4.3 The cost that actually matters

`applyModulation` converts each target to and from the normalised 0–1 domain. Per-voice that
would be ~20 grain targets × 16 voices = 320 conversions per chunk, ~4.5× more. It is
unnecessary: the base value is the same for all voices within a chunk, so convert the base
once per chunk per target and only the moved value per voice.

## 5. Where the offsets plug in

The engine loop is the natural insertion point because it already has a per-voice object in
hand: copy the segment's params per voice, apply that voice's offsets, render. The copy is
~350 B × 16 per segment, negligible, and keeps `GrainVoice` unchanged. `handleEvent` needs
the same treatment for note-ons, because the voice is chosen inside it: either pass the
offsets in, or return the chosen index (cleaner; it also lets the engine report which voice
it started).

## 6. The real obstacle: note context (`ModContext`)

Today there is one `ModContext` and one held-notes count for the whole plugin. Several
modulator types read it:

| Type | Needs per-voice context? | Notes |
|---|---|---|
| WAVE + RETRIG | **Yes** | "restart on each note" is meaningless globally |
| ENVELOPE | **Yes** | It is *defined* by the notes holding it |
| RANDOM | No, but should be seeded per voice | Otherwise 16 voices move in lockstep |
| FOLLOW | No | Input level is a global property of the buffer |
| MIDI velocity / note | **Yes** | Inherently per-note |
| MIDI mod wheel / aftertouch / pitch bend | No | Channel-wide |

So each voice gets its own `Modulator` set *and* its own `ModContext`, with shared fields
(bpm, ppq, input peak, channel CCs) copied in.

### 6.1 A bug that becomes serious here

A held-notes *counter* (review **F13**) miscounts repeated note-ons; per-voice, each voice
needs to know *which* notes are held. Fix first: a 128-entry held set (done with F13).

### 6.2 Seeding

Seed per-voice sources with `voiceSeed ^ voiceIndex` so voices decorrelate; with spread off
the output should be bit-identical to today.

## 7. The parameter that makes it shippable

Add one parameter now, defaulting to the behaviour that exists (`0`: every voice uses the
global values, bit-identical; `1`: each voice runs its own sources). Reserve the semantic now
so a saved project records the intent. *(Built as a per-modulator VOICE switch; see Status.)*

## 8. UI consequences

- **Rings show the global value**; per-voice adds a range across voices (built as a pip per
  sounding voice).
- **Which targets can take it** must be visible, so nobody expects a per-voice reverb.
- **SPREAD is a MOD-page control**, not a per-target one, in the first version.

## 9. Testing plan

1. Spread off is bit-identical. 2. Held-note set: duplicate note-on then one note-off
releases the ENVELOPE (F13). 3. Per-voice isolation: two voices don't share phase (seeds
differ). 4. No cross-talk between voices. 5. Voice stealing resets modulator state.
6. CPU guard by hand, not in CI.

## 10. Licensing — worth knowing before any public build

Not legal advice; confirm against the primary text before release. The repo has **no
`LICENSE` file**, so the terms are whatever JUCE requires and nothing more.

- **JUCE 8** is dual licensed: AGPLv3, or a commercial JUCE licence. Without AGPL you need a
  JUCE licence while distributing binaries. Tiers per JUCE's EULA page: Starter (to
  $20,000/yr, free), Indie (to $300,000/yr, $800 perpetual or $40/mo), Pro (no limit, $3,500
  or $175/mo), Educational (non-commercial). The revenue limit counts the individual's or
  entity's total revenue from all sources (read that clause carefully), not just the plugin's.
- **AGPLv3** is free but copyleft: source for the plugin and its linked code.
- **CLAP** and `clap-juce-extensions` are MIT: no obstacle.
- The repo pins **JUCE 8.0.14**; the 8.x line has been security-only since 2026-07-21 and
  JUCE 9 keeps the same pricing and licensing. Pinning 8.0.14 is defensible for a 1.x
  project, but should be a written decision.
- **VST3** needs no separate Steinberg licence (the SDK ships in JUCE); **AAX** would need an
  Avid licence and isn't built.

## 11. Staged plan

| Stage | Work | Risk |
|---|---|---|
| **A** | Held-note set (§6.1); spread parameter defaulting to off; per-voice seeds | Very low |
| **B** | Per-voice `GrainParams` in the engine loop and note starts; spread honoured | Low |
| **C** | Per-voice `ModContext` (retrigger, envelope, velocity) | Medium |
| **D** | UI: spread on rings, voice-scoped target hints, per-voice meters | Medium |

## 12. Open questions

1. One global spread knob, or per-modulator?
2. When a voice is stolen, should modulator state reset or carry?
3. Does the ENVELOPE modulator want a retrigger-legato mode per voice?
4. Do per-voice offsets make sense in Live mode, with a single drone voice and no keys?
