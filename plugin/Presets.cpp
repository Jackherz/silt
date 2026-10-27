#include "Presets.h"
#include "Palette.h"

using namespace silt::ui;

namespace
{
const juce::String extension (".siltpreset");

// A factory preset: values in each parameter's own units (choices by index,
// switches 0/1), anything unlisted at its default; routes as
// "modulator(1–4):parameter:amount".
struct Factory
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;   // owned: an initializer_list here would dangle
    std::vector<const char*> routes;
    bool instrumentOnly = false;   // played from notes (SLICE, FEED): not in Silt FX, which usually gets none
};

// Division indexes: 1/32 = 1, 1/16 = 3, 1/8 = 6, 1/8D = 8, 1/4 = 9, 1/2 = 12, 1 bar = 14, 2 bars = 15.
// MODE: 0 PITCH, 1 LIVE, 2 FEED, 3 SLICE. Chain orders: 0 R>F>C>S, 12 C>R>F>S.
const std::vector<Factory>& allFactories()
{
    static const std::vector<Factory> list {
        { "Init", {}, {} },

        { "Glass Cloud",
          { { "size", 180.0f }, { "rate", 30.0f }, { "spray", 30.0f }, { "jitter", 60.0f }, { "randPan", 70.0f },
            { "randPitch", 0.1f }, { "fade", 80.0f }, { "attack", 120.0f }, { "release", 1500.0f },
            { "spaceReverb", 45.0f }, { "spaceDecay", 8.0f }, { "spaceSize", 85.0f }, { "spaceShimmer", 35.0f }, { "spaceTone", 60.0f },
            { "m1Rate", 0.15f }, { "m2Type", 1.0f }, { "m2Rate", 2.0f } },
          { "1:spray:0.2", "2:pitch:0.02" } },

        { "Tape Choir",
          { { "rateMode", 2.0f }, { "grains", 12.0f }, { "grainSize", 120.0f }, { "randPitch", 0.15f }, { "randPan", 50.0f },
            { "colorWow", 45.0f }, { "colorDrive", 30.0f }, { "colorTone", -25.0f }, { "colorNoise", 20.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 3.0f } },
          {} },

        { "Rust Rhythm",
          { { "rateMode", 1.0f }, { "rateDiv", 3.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 3.0f }, { "spray", 10.0f },
            { "filterMode", 2.0f }, { "filterCutoff", 1800.0f }, { "filterReso", 40.0f },
            { "colorBits", 7.0f }, { "colorRate", 9000.0f }, { "colorComp", 50.0f },
            { "spaceDelay", 30.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 45.0f },
            { "m1Sync", 1.0f }, { "m1Div", 14.0f } },
          { "1:filterCutoff:0.35" } },

        { "Bell Garden",
          { { "scale", 8.0f }, { "spray", 20.0f }, { "randPitch", 7.0f }, { "release", 900.0f },
            { "resMix", 70.0f }, { "resNotes", 2.0f }, { "resDecay", 3.5f }, { "resBright", 70.0f }, { "resWidth", 80.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 5.0f } },
          {} },

        // Per-voice: every note wanders through the sample on its own.
        { "Wander",
          { { "length", 40.0f }, { "scan", 0.5f }, { "size", 220.0f }, { "rate", 14.0f }, { "spray", 10.0f },
            { "m1Type", 1.0f }, { "m1Rate", 0.7f }, { "m1Voice", 1.0f },
            { "m2Type", 2.0f }, { "m2Attack", 900.0f }, { "m2Decay", 1500.0f }, { "m2Sustain", 30.0f }, { "m2Voice", 1.0f },
            { "spaceReverb", 25.0f } },
          { "1:start:0.25", "1:spray:0.2", "2:size:0.4" } },

        { "Dark Drone",
          { { "pitch", -12.0f }, { "size", 900.0f }, { "rate", 8.0f }, { "spray", 25.0f }, { "attack", 1500.0f }, { "release", 4000.0f },
            { "filterMode", 2.0f }, { "filterCutoff", 700.0f }, { "filterReso", 25.0f }, { "colorWow", 20.0f },
            { "spaceReverb", 60.0f }, { "spaceDecay", 20.0f }, { "spaceSize", 100.0f }, { "spaceTone", 30.0f },
            { "m1Rate", 0.05f } },
          { "1:filterCutoff:0.3" } },

        // MOD 4 is a macro: one knob opens the filter and the space together.
        { "Shimmer Pad",
          { { "attack", 800.0f }, { "release", 3000.0f }, { "fade", 100.0f }, { "size", 300.0f }, { "spray", 15.0f },
            { "filterMode", 1.0f }, { "filterCutoff", 1200.0f },
            { "spaceShimmer", 60.0f }, { "spaceReverb", 55.0f }, { "spaceDecay", 12.0f },
            { "m4Type", 5.0f }, { "m4Macro", 30.0f } },
          { "4:filterCutoff:0.5", "4:spaceReverb:0.3", "4:spaceShimmer:0.3" } },

        // SLICE at the sample's hits: C1 up plays each hit, cut short at the next.
        { "Hit Chop",
          { { "keyMode", 3.0f }, { "sliceMode", 1.0f }, { "endMode", 2.0f },
            { "rateMode", 2.0f }, { "grains", 4.0f }, { "grainSize", 70.0f }, { "attack", 2.0f }, { "release", 120.0f },
            { "colorDrive", 25.0f }, { "colorComp", 40.0f },
            { "spaceDelay", 25.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 30.0f } },
          {}, true },

        // SLICE, sixteen equal parts, as synced stutters through crunch.
        { "Stutter Grid",
          { { "keyMode", 3.0f }, { "sliceCount", 16.0f }, { "endMode", 0.0f },
            { "rateMode", 1.0f }, { "rateDiv", 1.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 1.0f }, { "fade", 40.0f },
            { "attack", 1.0f }, { "release", 60.0f },
            { "colorBits", 9.0f }, { "colorRate", 12000.0f }, { "filterMode", 1.0f }, { "filterCutoff", 5000.0f }, { "filterReso", 30.0f },
            { "m1Type", 1.0f }, { "m1Sync", 1.0f }, { "m1Div", 6.0f }, { "m1Smooth", 0.0f } },
          { "1:filterCutoff:0.25", "1:reverse:0.5" }, true },

        // FEED: the grains keep re-granulating their own output, blooming as you hold.
        { "Feed Bloom",
          { { "keyMode", 2.0f }, { "liveRun", 0.0f }, { "feedback", 92.0f }, { "size", 260.0f }, { "rate", 12.0f },
            { "spray", 30.0f }, { "randPitch", 0.1f }, { "randPan", 60.0f }, { "attack", 400.0f }, { "release", 2500.0f },
            { "filterMode", 1.0f }, { "filterCutoff", 3500.0f },
            { "spaceReverb", 40.0f }, { "spaceDecay", 6.0f }, { "spaceSize", 75.0f },
            { "m1Rate", 0.1f } },
          { "1:filterCutoff:0.2" }, true },

        // The resonator tuned to the sample's own pitches, drifting and damped.
        { "Sample Strings",
          { { "size", 220.0f }, { "spray", 20.0f }, { "release", 1800.0f },
            { "resMix", 65.0f }, { "resNotes", 3.0f }, { "resFollow", 1.0f }, { "resDecay", 4.0f }, { "resBright", 55.0f }, { "resWidth", 70.0f },
            { "resReso", 70.0f }, { "resDrift", 35.0f }, { "resDriftRate", 0.3f }, { "resSpread", 15.0f }, { "resOddEven", -40.0f },
            { "resDrive", 25.0f }, { "resDamp", 35.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 4.0f } },
          {} },

        // Grit first: COLOR runs before the resonator, so the distortion rings.
        { "Grit Bells",
          { { "chainOrder", 12.0f }, { "size", 90.0f }, { "rate", 25.0f }, { "spray", 15.0f }, { "release", 1200.0f },
            { "colorDrive", 55.0f }, { "colorBits", 10.0f }, { "colorTone", 20.0f },
            { "resMix", 55.0f }, { "resNotes", 2.0f }, { "resDecay", 2.5f }, { "resDamp", 20.0f },
            { "spaceReverb", 30.0f }, { "spaceDecay", 5.0f } },
          {} },

        // Echoes on the dotted eighth that dip while you play and swell after.
        { "Ducked Echoes",
          { { "rateMode", 1.0f }, { "rateDiv", 6.0f }, { "sizeSync", 1.0f }, { "sizeDiv", 3.0f }, { "spray", 20.0f }, { "release", 600.0f },
            { "spaceDelay", 55.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 8.0f }, { "spaceFeedback", 60.0f }, { "spaceDuck", 2.0f },
            { "spaceReverb", 25.0f }, { "spaceDecay", 4.0f }, { "spaceTone", 40.0f } },
          {} },

        // Per-voice SPREAD: each note of a chord rides its own place on the wave.
        { "Spread Chorus",
          { { "size", 160.0f }, { "rate", 22.0f }, { "spray", 10.0f }, { "randPan", 40.0f }, { "attack", 300.0f }, { "release", 1500.0f },
            { "m1Rate", 0.4f }, { "m1Voice", 1.0f }, { "m1Spread", 80.0f },
            { "colorWow", 15.0f }, { "spaceReverb", 35.0f }, { "spaceDecay", 5.0f } },
          { "1:pitch:0.004", "1:start:0.15", "1:randPan:0.3" } },

        // Four playheads stepping a minor-pentatonic pattern.
        { "Head Patterns",
          { { "heads", 4.0f }, { "headSpread", 60.0f }, { "patternDepth", 12.0f }, { "patternSteps", 8.0f },
            { "scale", 9.0f }, { "key", 4.0f }, { "rateMode", 1.0f }, { "rateDiv", 3.0f }, { "size", 140.0f }, { "release", 900.0f },
            { "spaceDelay", 30.0f }, { "spaceSync", 1.0f }, { "spaceDiv", 6.0f }, { "spaceReverb", 25.0f } },
          {} },
    };
    return list;
}

// This build's presets: Silt FX leaves out the ones played from notes.
const std::vector<Factory>& factories()
{
    static const auto list = []
    {
        std::vector<Factory> mine;
        for (const auto& f : allFactories())
            if (SiltProcessor::isInstrument || ! f.instrumentOnly)
                mine.push_back (f);
        return mine;
    }();
    return list;
}
} // namespace

PresetManager::PresetManager (SiltProcessor& p, juce::File f) : silt (p), folder (std::move (f)) {}

juce::File PresetManager::defaultFolder()
{
    const auto data = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    return data.getChildFile ("Audio/Presets/Silt Audio").getChildFile (JucePlugin_Name);
#else
    return data.getChildFile ("Silt Audio").getChildFile (JucePlugin_Name).getChildFile ("Presets");
#endif
}

int PresetManager::numFactoryPresets() { return (int) factories().size(); }

std::unique_ptr<juce::XmlElement> PresetManager::factoryPreset (int index)
{
    const auto& f = factories()[(size_t) juce::jlimit (0, numFactoryPresets() - 1, index)];
    auto xml = std::make_unique<juce::XmlElement> ("SiltPreset");
    xml->setAttribute ("name", f.name);
    auto* values = xml->createNewChildElement ("PARAMS");
    for (const auto& [id, value] : f.values)
        values->setAttribute (id, value);
    juce::StringArray routes;
    for (const auto* route : f.routes)
    {
        // Factory routes number modulators 1–4; the state format uses 0–3.
        const auto parts = juce::StringArray::fromTokens (route, ":", "");
        routes.add (juce::String (parts[0].getIntValue() - 1) + ":" + parts[1] + ":" + parts[2]);
    }
    xml->setAttribute ("routes", routes.joinIntoString (";"));
    return xml;
}

std::vector<PresetManager::Entry> PresetManager::list() const
{
    std::vector<Entry> entries;
    for (int i = 0; i < numFactoryPresets(); ++i)
        entries.push_back ({ factories()[(size_t) i].name, true, i, {} });

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*" + extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    for (const auto& file : files)
        entries.push_back ({ file.getFileNameWithoutExtension(), false, -1, file });
    return entries;
}

bool PresetManager::load (const Entry& entry)
{
    std::unique_ptr<juce::XmlElement> xml = entry.factory ? factoryPreset (entry.factoryIndex) : juce::XmlDocument::parse (entry.file);
    if (xml == nullptr || ! xml->hasTagName ("SiltPreset"))
        return false;
    silt.applyPreset (*xml, entry.name);
    return true;
}

bool PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! folder.createDirectory())
        return false;
    const auto xml = silt.capturePreset (clean);
    if (! xml->writeTo (folder.getChildFile (clean + extension)))
        return false;
    silt.presetName = clean;
    return true;
}

bool PresetManager::step (int direction)
{
    const auto entries = list();
    if (entries.empty())
        return false;
    int at = -1;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == silt.presetName)
            at = (int) i;
    const int n = (int) entries.size();
    return load (entries[(size_t) (((at < 0 ? (direction > 0 ? -1 : 0) : at) + direction + n) % n)]);
}

// --- PresetBar ------------------------------------------------------------------------

PresetBar::PresetBar (SiltProcessor& p) : presets (p)
{
    previous.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x25c0));
    next.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x25b6));
    previous.onClick = [this] { presets.step (-1); refresh(); };
    next.onClick = [this] { presets.step (1); refresh(); };
    name.onClick = [this] { showMenu(); };
    save.onClick = [this]
    {
        // SAVE keeps a user preset's name; anything else asks for one.
        const auto current = presets.currentName();
        const auto entries = presets.list();
        const bool isUser = std::any_of (entries.begin(), entries.end(), [&] (const auto& e) { return ! e.factory && e.name == current; });
        if (isUser)
            presets.save (current);
        else
            saveAs();
    };
    for (auto* b : { &previous, &next, &name, &save })
        addAndMakeVisible (b);
    refresh();
}

void PresetBar::showMenu()
{
    const auto entries = presets.list();
    juce::PopupMenu menu, factory, user;
    for (size_t i = 0; i < entries.size(); ++i)
        (entries[i].factory ? factory : user).addItem ((int) i + 1, entries[i].name, true, entries[i].name == presets.currentName());
    menu.addSectionHeader ("FACTORY");
    for (juce::PopupMenu::MenuItemIterator it (factory); it.next();)
        menu.addItem (it.getItem());
    menu.addSectionHeader ("USER");
    if (user.getNumItems() == 0)
        menu.addItem (-1, "(none yet: SAVE makes one)", false);
    for (juce::PopupMenu::MenuItemIterator it (user); it.next();)
        menu.addItem (it.getItem());
    menu.addSeparator();
    menu.addItem (10000, "Save as...");
    menu.addItem (10001, "Show preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name), [this, entries] (int chosen)
    {
        if (chosen == 10000)
            saveAs();
        else if (chosen == 10001)
        {
            presets.getFolder().createDirectory();
            presets.getFolder().revealToUser();
        }
        else if (chosen >= 1 && chosen <= (int) entries.size())
            presets.load (entries[(size_t) chosen - 1]);
        refresh();
    });
}

void PresetBar::saveAs()
{
    asking = std::make_unique<juce::AlertWindow> ("Save preset", "A name for this sound:", juce::MessageBoxIconType::NoIcon, this);
    asking->addTextEditor ("name", presets.currentName() == "Init" ? juce::String() : presets.currentName());
    asking->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    asking->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    asking->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && asking != nullptr)
            presets.save (asking->getTextEditorContents ("name"));
        asking = nullptr;
        refresh();
    }));
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    save.setBounds (r.removeFromRight (52));
    r.removeFromRight (6);
    previous.setBounds (r.removeFromLeft (24));
    next.setBounds (r.removeFromRight (24));
    name.setBounds (r.reduced (3, 0));
}

void PresetBar::refresh()
{
    name.setButtonText (presets.currentName().isEmpty() ? juce::String ("INIT") : presets.currentName().toUpperCase());
}
