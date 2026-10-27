#pragma once

#include "PluginProcessor.h"

// Presets: a sound (every parameter, the modulation routes and the shown
// modulator), not the sample, the controller mapping or the window. A factory
// set is built in; user presets are .siltpreset files (XML) in the standard
// preset folder:
//   macOS    ~/Library/Audio/Presets/Silt Audio/<plug-in>/
//   Windows  %APPDATA%/Silt Audio/<plug-in>/Presets/
// Each has a category; favourites are kept in the same folder (favourites.txt).
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
        juce::String category;
    };

    // The browser's categories, in order (ALL and FAVOURITES are views, not categories).
    static const juce::StringArray& categories();   // PADS, TEXTURES, DRONES, TONAL, RHYTHMIC, SLICES, USER

    // folder: where user presets live (tests pass their own).
    explicit PresetManager (SiltProcessor&, juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    const juce::File& getFolder() const noexcept { return folder; }

    std::vector<Entry> list() const;           // factory first, then user, by name
    bool load (const Entry&);                  // one undo step
    bool audition (const Entry&);              // the same, not recorded (the browser commits once)
    bool save (const juce::String& name, const juce::String& category = "USER");   // as a user preset (replaces one of that name)
    bool step (int direction);                 // the next or previous in list()
    juce::String currentName() const { return silt.presetName; }

    // User presets only: rename, move to the bin, or file under another category.
    bool rename (const Entry&, const juce::String& newName);
    bool remove (const Entry&);
    static inline bool toTrash = true;   // tests delete their own files instead
    bool setCategory (const Entry&, const juce::String& category);

    // Favourites, by name, for this preset folder.
    bool isFavourite (const juce::String& name) const;
    void setFavourite (const juce::String& name, bool favourite);

    // Whether an entry shows under a view (ALL, FAVOURITES or a category) and a
    // search (matching its name or category, any case).
    bool shows (const Entry&, const juce::String& view, const juce::String& search) const;

    static int numFactoryPresets();
    static std::unique_ptr<juce::XmlElement> factoryPreset (int index);

private:
    std::unique_ptr<juce::XmlElement> read (const Entry&) const;
    juce::StringArray favourites() const;

    SiltProcessor& silt;
    juce::File folder;
};

// The preset browser, over the screen: a search box, category keys, and the
// list (★ to favourite). Clicking a preset (or the arrow keys) plays it at
// once; KEEP (Return, double-click) keeps it as one undo step, CANCEL (Esc)
// puts back the sound there was. Right-click a user preset to rename, file or
// remove it.
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (SiltProcessor&);
    ~PresetBrowser() override;

    void open();                 // shows it, remembering the sound to go back to
    void close (bool keep);      // keep: one undo step; otherwise back to the sound before
    std::function<void()> onClosed;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // For tests.
    int numShown() const { return (int) shown.size(); }
    juce::String shownName (int row) const { return row >= 0 && row < (int) shown.size() ? shown[(size_t) row].name : juce::String(); }
    void setView (const juce::String& view);
    void setSearch (const juce::String& text);
    void choose (int row);       // audition the row, as a click does
    void toggleFavourite (int row);   // its star, as a click on it does
    bool isFavourite (int row) const;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void refresh();
    void showRowMenu (int row);
    static constexpr int starWidth = 30;
    void saveAs();

    SiltProcessor& silt;
    PresetManager presets;
    std::vector<PresetManager::Entry> shown;
    juce::String view { "ALL" };
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> viewKeys;
    juce::StringArray viewNames;   // each key's view, in the same order
    juce::ListBox list;
    juce::TextButton keep { "KEEP" }, cancel { "CANCEL" }, saveAsButton { "SAVE AS..." }, folderButton { "FOLDER" };
    std::unique_ptr<juce::XmlElement> soundBefore;
    juce::String nameBefore;
    bool auditioned = false, choosing = false;
    std::unique_ptr<juce::AlertWindow> asking;
};

// The header's preset bar: previous, the name (click for the browser), next,
// and SAVE.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (SiltProcessor&);
    void resized() override;
    void refresh();   // shows the current preset's name

    std::function<void()> onBrowse;   // the name was clicked

private:
    void saveAs();

    PresetManager presets;
    juce::TextButton previous, next, name, save { "SAVE" };
    std::unique_ptr<juce::AlertWindow> asking;
};
