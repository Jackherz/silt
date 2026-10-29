#include "Presets.h"
#include "Palette.h"

#include <map>

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
            { "resReso", 70.0f }, { "resDrift", 35.0f }, { "resSpread", 15.0f }, { "resOddEven", -40.0f },
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

// Each factory preset's category in the browser.
juce::String factoryCategory (const juce::String& name)
{
    static const std::map<juce::String, const char*> categoryOf {
        { "Init", "TONAL" },          { "Glass Cloud", "PADS" },     { "Tape Choir", "TEXTURES" },  { "Rust Rhythm", "RHYTHMIC" },
        { "Bell Garden", "TONAL" },   { "Wander", "TEXTURES" },      { "Dark Drone", "DRONES" },    { "Shimmer Pad", "PADS" },
        { "Hit Chop", "SLICES" },     { "Stutter Grid", "SLICES" },  { "Feed Bloom", "TEXTURES" },  { "Sample Strings", "TONAL" },
        { "Grit Bells", "TONAL" },    { "Ducked Echoes", "RHYTHMIC" }, { "Spread Chorus", "PADS" }, { "Head Patterns", "RHYTHMIC" },
    };
    const auto it = categoryOf.find (name);
    return it != categoryOf.end() ? it->second : "TONAL";
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

const juce::StringArray& PresetManager::categories()
{
    static const juce::StringArray list { "PADS", "TEXTURES", "DRONES", "TONAL", "RHYTHMIC", "SLICES", "USER" };
    return list;
}

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
    xml->setAttribute ("category", factoryCategory (f.name));
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
        entries.push_back ({ factories()[(size_t) i].name, true, i, {}, factoryCategory (factories()[(size_t) i].name) });

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*" + extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    for (const auto& file : files)
    {
        // The category is in the file; one that's unreadable or has none is USER.
        juce::String category = "USER";
        if (const auto xml = juce::XmlDocument::parse (file); xml != nullptr && categories().contains (xml->getStringAttribute ("category")))
            category = xml->getStringAttribute ("category");
        entries.push_back ({ file.getFileNameWithoutExtension(), false, -1, file, category });
    }
    return entries;
}

std::unique_ptr<juce::XmlElement> PresetManager::read (const Entry& entry) const
{
    auto xml = entry.factory ? factoryPreset (entry.factoryIndex) : juce::XmlDocument::parse (entry.file);
    return xml != nullptr && xml->hasTagName ("SiltPreset") ? std::move (xml) : nullptr;
}

bool PresetManager::load (const Entry& entry)
{
    const auto xml = read (entry);
    if (xml == nullptr)
        return false;
    silt.applyPreset (*xml, entry.name);
    return true;
}

bool PresetManager::audition (const Entry& entry)
{
    const auto xml = read (entry);
    if (xml == nullptr)
        return false;
    silt.applyPresetValues (*xml, entry.name);
    return true;
}

bool PresetManager::save (const juce::String& name, const juce::String& category)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! folder.createDirectory())
        return false;
    const auto xml = silt.capturePreset (clean);
    xml->setAttribute ("category", categories().contains (category) ? category : juce::String ("USER"));
    if (! xml->writeTo (folder.getChildFile (clean + extension)))
        return false;
    silt.presetName = clean;
    return true;
}

bool PresetManager::rename (const Entry& entry, const juce::String& newName)
{
    const auto clean = juce::File::createLegalFileName (newName.trim());
    if (entry.factory || clean.isEmpty())
        return false;
    const auto target = folder.getChildFile (clean + extension);
    if (target.exists() || ! entry.file.moveFileTo (target))
        return false;
    if (const auto xml = juce::XmlDocument::parse (target))
    {
        xml->setAttribute ("name", clean);
        xml->writeTo (target);
    }
    if (isFavourite (entry.name))
    {
        setFavourite (entry.name, false);
        setFavourite (clean, true);
    }
    if (silt.presetName == entry.name)
        silt.presetName = clean;
    return true;
}

bool PresetManager::remove (const Entry& entry)
{
    // To the bin, not gone: it can be fished back out.
    if (entry.factory || ! (toTrash ? entry.file.moveToTrash() : entry.file.deleteFile()))
        return false;
    setFavourite (entry.name, false);
    return true;
}

bool PresetManager::setCategory (const Entry& entry, const juce::String& category)
{
    if (entry.factory || ! categories().contains (category))
        return false;
    const auto xml = juce::XmlDocument::parse (entry.file);
    if (xml == nullptr)
        return false;
    xml->setAttribute ("category", category);
    return xml->writeTo (entry.file);
}

juce::StringArray PresetManager::favourites() const
{
    juce::StringArray names;
    names.addLines (folder.getChildFile ("favourites.txt").loadFileAsString());
    names.removeEmptyStrings();
    return names;
}

bool PresetManager::isFavourite (const juce::String& name) const { return favourites().contains (name); }

void PresetManager::setFavourite (const juce::String& name, bool favourite)
{
    auto names = favourites();
    if (favourite)
        names.addIfNotAlreadyThere (name);
    else
        names.removeString (name);
    if (folder.createDirectory())
        folder.getChildFile ("favourites.txt").replaceWithText (names.joinIntoString ("\n"));
}

bool PresetManager::shows (const Entry& entry, const juce::String& view, const juce::String& search) const
{
    const bool inView = view == "ALL" || (view == "FAVOURITES" ? isFavourite (entry.name) : entry.category == view);
    const auto query = search.trim();
    return inView && (query.isEmpty() || entry.name.containsIgnoreCase (query) || entry.category.containsIgnoreCase (query));
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

// --- PresetBrowser ------------------------------------------------------------------------

PresetBrowser::PresetBrowser (SiltProcessor& p) : silt (p), presets (p)
{
    setWantsKeyboardFocus (true);
    search.setTextToShowWhenEmpty ("SEARCH", screenPale.withAlpha (0.5f));
    search.setFont (monoFont (13.0f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
    search.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    search.setColour (juce::TextEditor::outlineColourId, screenLight.withAlpha (0.5f));
    search.setColour (juce::TextEditor::focusedOutlineColourId, screenLime);
    search.onTextChange = [this] { refresh(); };
    search.onReturnKey = [this] { close (true); };
    search.onEscapeKey = [this] { close (false); };
    addAndMakeVisible (search);

    juce::StringArray views { "ALL", "FAVOURITES" };
    views.addArray (PresetManager::categories());
    viewNames = views;
    for (const auto& v : views)
    {
        auto* key = viewKeys.add (new juce::TextButton (v == "FAVOURITES" ? juce::String::fromUTF8 ("\xe2\x98\x85") : v));
        key->setClickingTogglesState (true);
        key->setRadioGroupId (2);
        // Only the key being turned on: a radio group also "clicks" the one it turns off.
        key->onClick = [this, v, key] { if (key->getToggleState()) setView (v); };
        key->setTooltip (v == "FAVOURITES" ? "Your starred presets" : "");
        addAndMakeVisible (key);
    }
    viewKeys[0]->setToggleState (true, juce::dontSendNotification);

    list.setModel (this);
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    keep.onClick = [this] { close (true); };
    cancel.onClick = [this] { close (false); };
    saveAsButton.onClick = [this] { saveAs(); };
    folderButton.onClick = [this]
    {
        presets.getFolder().createDirectory();
        presets.getFolder().revealToUser();
    };
    keep.setTooltip ("Keep this sound (Return): one undo step");
    cancel.setTooltip ("Back to the sound you had (Esc)");
    for (auto* b : { &keep, &cancel, &saveAsButton, &folderButton })
        addAndMakeVisible (b);
    setVisible (false);
}

PresetBrowser::~PresetBrowser() { list.setModel (nullptr); }

void PresetBrowser::open()
{
    soundBefore = silt.capturePreset (silt.presetName);
    nameBefore = silt.presetName;
    auditioned = false;
    search.clear();
    refresh();
    setVisible (true);
    toFront (true);
    // The current preset selected, without playing it again.
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].name == silt.presetName)
        {
            const juce::ScopedValueSetter<bool> quiet (choosing, true);
            list.selectRow ((int) i);
        }
    search.grabKeyboardFocus();
}

void PresetBrowser::close (bool keepIt)
{
    if (! isVisible())
        return;
    if (auditioned)
    {
        if (keepIt)
            silt.commitPresetChange (std::move (soundBefore), nameBefore);
        else if (soundBefore != nullptr)
            silt.applyPresetValues (*soundBefore, nameBefore);
    }
    soundBefore = nullptr;
    auditioned = false;
    setVisible (false);
    if (onClosed)
        onClosed();
}

void PresetBrowser::setView (const juce::String& v)
{
    view = v;
    for (int i = 0; i < viewKeys.size(); ++i)
        viewKeys[i]->setToggleState (viewNames[i] == v, juce::dontSendNotification);   // just this one lit
    refresh();
}

void PresetBrowser::setSearch (const juce::String& text) { search.setText (text, true); refresh(); }

void PresetBrowser::refresh()
{
    shown.clear();
    for (auto& entry : presets.list())
        if (presets.shows (entry, view, search.getText()))
            shown.push_back (std::move (entry));
    list.updateContent();
    list.repaint();
}

void PresetBrowser::toggleFavourite (int row)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    presets.setFavourite (shown[(size_t) row].name, ! presets.isFavourite (shown[(size_t) row].name));
    refresh();
}

bool PresetBrowser::isFavourite (int row) const
{
    return row >= 0 && row < (int) shown.size() && presets.isFavourite (shown[(size_t) row].name);
}

void PresetBrowser::choose (int row)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    if (presets.audition (shown[(size_t) row]))
        auditioned = true;
    list.repaint();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    const auto& e = shown[(size_t) row];
    const bool current = e.name == silt.presetName;
    auto r = juce::Rectangle<int> (width, height);
    if (selected || current)
    {
        g.setColour (screenLight.withAlpha (selected ? 0.4f : 0.2f));
        g.fillRect (r);
    }
    // The star: click it to favourite (filled) or not (outlined).
    {
        const bool favourite = presets.isFavourite (e.name);
        const auto box = r.removeFromLeft (starWidth).toFloat();
        juce::Path star;
        star.addStar (box.getCentre(), 5, 3.4f, 7.5f, 0.0f);   // a point at the top
        if (favourite)
        {
            g.setColour (screenLime);
            g.fillPath (star);
        }
        else
        {
            g.setColour (screenPale.withAlpha (0.7f));
            g.strokePath (star, juce::PathStrokeType (1.2f));
        }
    }
    g.setFont (monoFont (12.0f));
    g.setColour (screenPale.withAlpha (0.55f));
    g.drawText (e.factory ? "FACTORY" : "USER", r.removeFromRight (70), juce::Justification::centredLeft);
    g.drawText (e.category, r.removeFromRight (100), juce::Justification::centredLeft);
    g.setColour (current ? screenLime : juce::Colours::white);
    g.drawText (e.name.toUpperCase(), r, juce::Justification::centredLeft);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showRowMenu (row);
        return;
    }
    if (e.x < starWidth)
    {
        toggleFavourite (row);   // the star column: favourite, don't play
        return;
    }
    choose (row);
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    choose (row);
    close (true);
}

void PresetBrowser::selectedRowsChanged (int lastRowSelected)
{
    // The arrow keys play as they move (a click plays, or stars, in listBoxItemClicked).
    if (! choosing && lastRowSelected >= 0 && list.hasKeyboardFocus (true)
        && ! juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown())
        choose (lastRowSelected);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close (false);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        close (true);
        return true;
    }
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        const int row = juce::jlimit (0, std::max (0, (int) shown.size() - 1), list.getSelectedRow() + (key == juce::KeyPress::downKey ? 1 : -1));
        list.selectRow (row);
        choose (row);
        return true;
    }
    return false;
}

void PresetBrowser::showRowMenu (int row)
{
    if (row < 0 || row >= (int) shown.size())
        return;
    const auto entry = shown[(size_t) row];
    juce::PopupMenu menu, file;
    const bool fav = presets.isFavourite (entry.name);
    menu.addItem (1, fav ? "Remove from favourites" : "Add to favourites");
    if (! entry.factory)
    {
        for (int i = 0; i < PresetManager::categories().size(); ++i)
            file.addItem (100 + i, PresetManager::categories()[i], true, entry.category == PresetManager::categories()[i]);
        menu.addSeparator();
        menu.addSubMenu ("Category", file);
        menu.addItem (2, "Rename...");
        menu.addItem (3, "Move to Trash");
    }
    juce::PopupMenu::dismissAllActiveMenus();
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, entry, fav] (int chosen)
    {
        if (chosen == 1)
            presets.setFavourite (entry.name, ! fav);
        else if (chosen >= 100)
            presets.setCategory (entry, PresetManager::categories()[chosen - 100]);
        else if (chosen == 3)
            presets.remove (entry);
        else if (chosen == 2)
        {
            asking = std::make_unique<juce::AlertWindow> ("Rename preset", "A new name:", juce::MessageBoxIconType::NoIcon, this);
            asking->addTextEditor ("name", entry.name);
            asking->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            asking->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            asking->enterModalState (true, juce::ModalCallbackFunction::create ([this, entry] (int result)
            {
                if (result == 1 && asking != nullptr)
                    presets.rename (entry, asking->getTextEditorContents ("name"));
                asking = nullptr;
                refresh();
            }));
            return;
        }
        refresh();
    });
}

void PresetBrowser::saveAs()
{
    asking = std::make_unique<juce::AlertWindow> ("Save preset", "A name for this sound:", juce::MessageBoxIconType::NoIcon, this);
    asking->addTextEditor ("name", silt.presetName == "Init" ? juce::String() : silt.presetName);
    asking->addComboBox ("category", PresetManager::categories(), "Category");
    if (auto* box = asking->getComboBoxComponent ("category"))
        box->setSelectedItemIndex (PresetManager::categories().indexOf (view) >= 0 ? PresetManager::categories().indexOf (view) : PresetManager::categories().size() - 1);
    asking->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    asking->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    asking->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && asking != nullptr)
        {
            const auto* box = asking->getComboBoxComponent ("category");
            presets.save (asking->getTextEditorContents ("name"), box != nullptr ? box->getText() : juce::String ("USER"));
            soundBefore = silt.capturePreset (silt.presetName);   // saved: it's the sound to keep now
            nameBefore = silt.presetName;
            auditioned = false;
        }
        asking = nullptr;
        refresh();
    }));
}

void PresetBrowser::paint (juce::Graphics& g)
{
    // A dark panel like the screen's pictures.
    g.setColour (insetBack.interpolatedWith (juce::Colours::black, 0.55f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (screenLight.withAlpha (0.6f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (screenLime);
    g.setFont (monoFont (12.0f));
    g.drawText ("PRESETS  " + juce::String (shown.size()) + (shown.size() == 1 ? " SOUND" : " SOUNDS"),
                getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (screenPale.withAlpha (0.6f));
    g.setFont (monoFont (10.0f));
    g.drawText ("CLICK TO HEAR   RETURN KEEPS   ESC GOES BACK", getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::centredRight);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (20);
    auto top = r.removeFromTop (26);
    search.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth() / 3)));
    top.removeFromLeft (8);
    const int keyWidth = top.getWidth() / juce::jmax (1, viewKeys.size());
    for (auto* key : viewKeys)
        key->setBounds (top.removeFromLeft (keyWidth).reduced (2, 0));
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (28);
    r.removeFromBottom (6);
    list.setBounds (r);
    for (auto* b : { &keep, &cancel })
        b->setBounds (bottom.removeFromRight (90).reduced (3, 0));
    for (auto* b : { &saveAsButton, &folderButton })
        b->setBounds (bottom.removeFromLeft (100).reduced (3, 0));
}

// --- PresetBar ------------------------------------------------------------------------

PresetBar::PresetBar (SiltProcessor& p) : presets (p)
{
    // Arrows drawn by the panel's look (the lettering's font has none).
    previous.setButtonText ({});
    next.setButtonText ({});
    previous.getProperties().set ("arrow", -1);
    next.getProperties().set ("arrow", 1);
    previous.setTitle ("Previous preset");
    next.setTitle ("Next preset");
    previous.onClick = [this] { presets.step (-1); refresh(); };
    next.onClick = [this] { presets.step (1); refresh(); };
    name.onClick = [this]
    {
        if (onBrowse)
            onBrowse();
    };
    name.setTooltip ("Browse presets: search, categories, favourites");
    save.onClick = [this]
    {
        // SAVE keeps a user preset's name and category; anything else asks for them.
        const auto current = presets.currentName();
        for (const auto& e : presets.list())
            if (! e.factory && e.name == current)
            {
                presets.save (current, e.category);
                return;
            }
        saveAs();
    };
    for (auto* b : { &previous, &next, &name, &save })
        addAndMakeVisible (b);
    refresh();
}

void PresetBar::saveAs()
{
    asking = std::make_unique<juce::AlertWindow> ("Save preset", "A name for this sound:", juce::MessageBoxIconType::NoIcon, this);
    asking->addTextEditor ("name", presets.currentName() == "Init" ? juce::String() : presets.currentName());
    asking->addComboBox ("category", PresetManager::categories(), "Category");
    if (auto* box = asking->getComboBoxComponent ("category"))
        box->setSelectedItemIndex (PresetManager::categories().size() - 1);
    asking->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    asking->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    asking->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && asking != nullptr)
        {
            const auto* box = asking->getComboBoxComponent ("category");
            presets.save (asking->getTextEditorContents ("name"), box != nullptr ? box->getText() : juce::String ("USER"));
        }
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
