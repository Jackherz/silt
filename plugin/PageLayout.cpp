#include "PageLayout.h"

namespace silt::layout
{

const char* pageName (int page)
{
    static const char* names[] { "GRAIN", "SHAPE", "PITCH", "PLAY", "RESONATOR", "FILTER", "COLOR", "SPACE", "MOD", "CHAIN" };
    return names[juce::jlimit (0, numPages - 1, page)];
}

namespace
{
using Shown = std::function<bool (const Context&)>;

Shown is (juce::String id, int value) { return [=] (const Context& c) { return juce::roundToInt (c.v (id)) == value; }; }
Shown on (juce::String id) { return [=] (const Context& c) { return c.v (id) > 0.5f; }; }
Shown off (juce::String id) { return [=] (const Context& c) { return c.v (id) <= 0.5f; }; }
Shown both (Shown a, Shown b) { return [=] (const Context& c) { return a (c) && b (c); }; }

std::vector<Entry> build (bool instrument)
{
    std::vector<Entry> e;
    const auto knob = [&] (int page, int slot, const juce::String& id, const juce::String& label, Shown shown = {})
    {
        e.push_back ({ page, slot, Kind::knob, id, label, {}, {}, 0, std::move (shown), {} });
    };
    const auto menu = [&] (int page, int slot, const juce::String& id, const juce::String& label, juce::StringArray items, Shown shown = {})
    {
        e.push_back ({ page, slot, Kind::menu, id, label, std::move (items), {}, 0, std::move (shown), {} });
    };
    const auto button = [&] (int page, int slot, const juce::String& id, const juce::String& label, const juce::String& text, Shown shown = {})
    {
        e.push_back ({ page, slot, Kind::button, id, label, {}, text, 0, std::move (shown), {} });
    };
    const auto under = [&] (Kind kind, int page, int slot, const juce::String& id, int width, juce::StringArray items, const juce::String& text, Shown shown = {})
    {
        e.push_back ({ page, slot, kind, id, {}, std::move (items), text, width, std::move (shown), {} });
    };

    // GRAIN: what the grains are. SIZE and RATE change with SYNC and the rate mode.
    const auto grainsMode = is ("rateMode", 2);
    knob (grain, 0, "pitch", "PITCH");
    knob (grain, 1, "size", "SIZE", [] (const Context& c) { return c.v ("sizeSync") <= 0.5f && juce::roundToInt (c.v ("rateMode")) != 2; });
    knob (grain, 1, "sizeDiv", "SIZE", on ("sizeSync"));
    knob (grain, 1, "grainSize", "SIZE", both (off ("sizeSync"), grainsMode));
    knob (grain, 2, "rate", "RATE", is ("rateMode", 0));
    knob (grain, 2, "rateDiv", "RATE", is ("rateMode", 1));
    knob (grain, 2, "grains", "GRAINS", grainsMode);
    knob (grain, 3, "spray", "SPRAY");
    knob (grain, 4, "jitter", "JITTER");
    knob (grain, 5, "reverse", "REVERSE");
    knob (grain, 6, "randAmp", "RND AMP");
    knob (grain, 7, "randPan", "RND PAN");
    under (Kind::underButton, grain, 1, "sizeSync", 60, {}, "SYNC");
    under (Kind::underMenu, grain, 2, "rateMode", 92, { "FREE", "SYNC", "GRAINS" }, {});

    // SHAPE: the grain's envelope, then the note's (both previewed in slot 7).
    knob (shape, 0, "fade", "FADE");
    knob (shape, 1, "slant", "SLANT");
    knob (shape, 2, "ease", "EASE");
    knob (shape, 3, "attack", "ATTACK");
    knob (shape, 4, "decay", "DECAY");
    knob (shape, 5, "sustain", "SUSTAIN");
    knob (shape, 6, "release", "RELEASE");

    // PITCH: random pitch, the scale it snaps to, a repeating pattern, and heads.
    knob (pitch, 0, "randPitch", "RND PITCH");
    menu (pitch, 1, "scale", "SCALE", { "OFF", "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN",
                                        "MIXOLYDIAN", "MAJOR PENT", "MINOR PENT", "WHOLE TONE", "OCTAVES", "FIFTHS" });
    menu (pitch, 2, "key", "KEY", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
    knob (pitch, 3, "patternDepth", "PATTERN");
    knob (pitch, 4, "patternSteps", "STEPS");
    e.push_back ({ pitch, 5, Kind::action, "!newPattern", "NEW PATTERN", {}, "NEW", 0, {}, {} });
    knob (pitch, 6, "heads", "HEADS");
    knob (pitch, 7, "headSpread", "SPREAD");

    // PLAY: the mode, then what it plays from. PITCH: a region of the sample and
    // how the playhead moves through it. LIVE: how far behind the input the
    // grains read, feedback, freeze, and what plays grains without notes.
    const auto pitchMode = is ("keyMode", 0), liveMode = is ("keyMode", 1);
    menu (play, 0, "keyMode", "MODE", { "PITCH", "LIVE" });
    knob (play, 1, "start", "START", pitchMode);
    knob (play, 2, "length", "LENGTH", pitchMode);
    knob (play, 3, "scan", "SCAN", pitchMode);
    menu (play, 4, "endMode", "AT THE END", { "LOOP", "HOLD", "STOP" }, pitchMode);
    under (Kind::underMenu, play, 1, "snap", 104, { "SNAP OFF", "SNAP ZERO", "SNAP TRANS" }, {}, pitchMode);
    knob (play, 1, "liveDelay", "DELAY", both (liveMode, off ("delaySync")));
    knob (play, 1, "delayDiv", "DELAY", both (liveMode, on ("delaySync")));
    knob (play, 2, "feedback", "FEEDBACK", liveMode);
    button (play, 3, "freeze", "FREEZE", "FREEZE", liveMode);
    menu (play, 4, "liveRun", "NO NOTES", { "OFF", "ALWAYS", "FOLLOW" }, liveMode);
    under (Kind::underButton, play, 1, "delaySync", 60, {}, "SYNC", liveMode);
    knob (play, 5, "root", "ROOT");
    knob (play, 6, "gain", "OUTPUT");
    if (! instrument)
        knob (play, 7, "mix", "MIX");

    // RESONATOR: a bank of ringing bands on the grains. SCALE notes come from the
    // PITCH page's scale and key; FOLLOW tunes the bank to the note played.
    // RESONATOR, in two halves (the tab, or the controller's page buttons, step between them).
    const Shown firstHalf = [] (const Context& c) { return c.resonatorHalf == 0; };
    const Shown secondHalf = [] (const Context& c) { return c.resonatorHalf == 1; };
    knob (resonator, 0, "resTune", "TUNE", firstHalf);
    menu (resonator, 1, "resNotes", "NOTES", { "SCALE", "HARMONICS", "BELL", "SAMPLE" }, firstHalf);
    knob (resonator, 2, "resRange", "RANGE", firstHalf);
    knob (resonator, 3, "resDecay", "DECAY", firstHalf);
    knob (resonator, 4, "resBright", "BRIGHT", firstHalf);
    knob (resonator, 5, "resWidth", "WIDTH", firstHalf);
    button (resonator, 6, "resFollow", "FOLLOW", "FOLLOW", firstHalf);
    knob (resonator, 7, "resMix", "MIX", firstHalf);
    knob (resonator, 0, "resReso", "RESONANCE", secondHalf);
    knob (resonator, 1, "resBands", "BANDS", secondHalf);
    knob (resonator, 2, "resDrift", "DRIFT", secondHalf);
    knob (resonator, 3, "resDriftRate", "DRIFT RATE", secondHalf);
    knob (resonator, 4, "resSpread", "SPREAD", secondHalf);
    knob (resonator, 5, "resOddEven", "ODD/EVEN", secondHalf);
    knob (resonator, 6, "resDrive", "DRIVE", secondHalf);
    knob (resonator, 7, "resDamp", "DAMP", secondHalf);

    // FILTER: after the resonator. MORPH blends low, band and high pass; ENV lets
    // the level move the cutoff; KEY follows the notes played.
    menu (filter, 0, "filterMode", "MODE", { "OFF", "12 DB", "24 DB" });
    knob (filter, 1, "filterCutoff", "CUTOFF");
    knob (filter, 2, "filterReso", "RESO");
    knob (filter, 3, "filterMorph", "MORPH");
    knob (filter, 4, "filterDrive", "DRIVE");
    knob (filter, 5, "filterEnv", "ENV");
    knob (filter, 6, "filterKey", "KEY");
    knob (filter, 7, "filterMix", "MIX");

    // COLOR: after the filter; each stage is out of the way at its default.
    knob (color, 0, "colorDrive", "DRIVE");
    knob (color, 1, "colorTone", "TONE");
    knob (color, 2, "colorBits", "BITS");
    knob (color, 3, "colorRate", "RATE");
    knob (color, 4, "colorComp", "COMP");
    knob (color, 5, "colorNoise", "NOISE");
    knob (color, 6, "colorWow", "WOW");
    knob (color, 7, "colorMix", "MIX");

    // SPACE: delay and reverb, both sends. SYNC under TIME, the delay mode under
    // FEEDBACK, FREEZE (both tails) under DECAY.
    knob (space, 0, "spaceTime", "TIME", off ("spaceSync"));
    knob (space, 0, "spaceDiv", "TIME", on ("spaceSync"));
    knob (space, 1, "spaceFeedback", "FEEDBACK");
    knob (space, 2, "spaceDelay", "DELAY");
    knob (space, 3, "spaceSize", "SIZE");
    knob (space, 4, "spaceDecay", "DECAY");
    knob (space, 5, "spaceTone", "TONE");
    knob (space, 6, "spaceShimmer", "SHIMMER");
    knob (space, 7, "spaceReverb", "REVERB");
    under (Kind::underButton, space, 0, "spaceSync", 60, {}, "SYNC");
    under (Kind::underMenu, space, 1, "spaceMode", 104, { "PING-PONG", "STEREO" }, {});
    under (Kind::underButton, space, 4, "spaceFreeze", 72, {}, "FREEZE");
    under (Kind::underMenu, space, 7, "spaceDuck", 104, { "NO DUCK", "DUCK LOW", "DUCK MID", "DUCK HIGH" }, {});

    // MOD: the shown modulator (picked on the rail, or by knob 1), its type,
    // five settings for that type, and ASSIGN (arms it for the controller).
    e.push_back ({ mod, 0, Kind::banner, "!modSelect", "MODULATOR", {}, {}, 0, {}, {} });
    for (int m = 0; m < 4; ++m)
    {
        const auto id = [m] (const char* suffix) { return "m" + juce::String (m + 1) + suffix; };
        const Shown shownMod = [m] (const Context& c) { return c.modShown == m; };
        const auto type = [=] (int t) { return Shown ([=] (const Context& c) { return c.modShown == m && juce::roundToInt (c.v (id ("Type"))) == t; }); };
        const auto wave = type (0), random = type (1), envelope = type (2), follow = type (3), midi = type (4), macro = type (5);
        const Shown timed = [=] (const Context& c) { return wave (c) || random (c); };

        menu (mod, 1, id ("Type"), "TYPE", { "WAVE", "RANDOM", "ENVELOPE", "FOLLOW", "MIDI", "MACRO" }, shownMod);
        under (Kind::underButton, mod, 1, id ("Enable"), 50, {}, "ON", shownMod);
        under (Kind::underButton, mod, 2, id ("Voice"), 64, {}, "VOICE", shownMod);

        knob (mod, 2, id ("Shape"), "SHAPE", wave);
        knob (mod, 3, id ("Rate"), "RATE", both (timed, off (id ("Sync"))));
        knob (mod, 3, id ("Div"), "RATE", both (timed, on (id ("Sync"))));
        under (Kind::underButton, mod, 3, id ("Sync"), 50, {}, "SYNC", timed);
        e.back().dx = -24;
        // Synced, a WAVE can't go negative: REV runs it backwards against the bar.
        under (Kind::underButton, mod, 3, id ("Rev"), 42, {}, "REV", both (wave, on (id ("Sync"))));
        e.back().dx = 25;
        knob (mod, 4, id ("Phase"), "PHASE", wave);
        under (Kind::underButton, mod, 4, id ("Retrig"), 64, {}, "RETRIG", wave);
        knob (mod, 5, id ("Sym"), "SYM", wave);
        knob (mod, 6, id ("Curve"), "CURVE", wave);
        // A square's ±1 doesn't bend: CURVE greys out on it.
        e.back().dimmed = [=] (const Context& c) { return c.v (id ("Shape")) >= 2.99f; };

        knob (mod, 2, id ("Smooth"), "SMOOTH", random);
        knob (mod, 4, id ("Jitter"), "JITTER", random);

        knob (mod, 2, id ("Attack"), "ATTACK", envelope);
        knob (mod, 3, id ("Decay"), "DECAY", envelope);
        knob (mod, 4, id ("Sustain"), "SUSTAIN", envelope);
        knob (mod, 5, id ("Release"), "RELEASE", envelope);
        button (mod, 6, id ("Repeat"), "LOOP", "REPEAT", envelope);

        knob (mod, 2, id ("FAttack"), "ATTACK", follow);
        knob (mod, 3, id ("FRelease"), "RELEASE", follow);
        knob (mod, 4, id ("Gain"), "GAIN", follow);

        menu (mod, 2, id ("Source"), "SOURCE", { "VELOCITY", "NOTE", "MOD WHEEL", "AFTERTOUCH", "PITCH BEND" }, midi);

        // MACRO: one knob that moves everything it's routed to.
        knob (mod, 3, id ("Macro"), "MACRO", macro);
    }
    e.push_back ({ mod, 7, Kind::action, "!assign", "CONTROLLER", {}, "ASSIGN", 0, {}, {} });

    // CHAIN: the modules' order. The screen shows it and lets you drag them;
    // the knob steps through all 24 orders.
    knob (chain, 0, "chainOrder", "ORDER");
    return e;
}
} // namespace

const std::vector<Entry>& entries (bool instrument)
{
    static const auto forInstrument = build (true), forEffect = build (false);
    return instrument ? forInstrument : forEffect;
}

juce::String slotParameter (bool instrument, int page, int slot, const Context& c)
{
    for (const auto& entry : entries (instrument))
        if (entry.page == page && entry.slot == slot && entry.inSlot() && entry.isShown (c))
            return entry.id;
    return {};
}
} // namespace silt::layout
