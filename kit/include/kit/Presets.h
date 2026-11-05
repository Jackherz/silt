#pragma once

#include "Look.h"

// Presets, the same in every plug-in: a factory set built in, user presets as XML
// files in the standard preset folder, a category per preset and favourites in the
// same folder. A plug-in says how to capture and apply a sound; the rest is here.
namespace kit
{
class PresetLibrary
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

    PresetLibrary (juce::File folderToUse, juce::String xmlTag, juce::String fileExtension)
        : folder (std::move (folderToUse)), tag (std::move (xmlTag)), extension (std::move (fileExtension)) {}
    virtual ~PresetLibrary() = default;

    // What the plug-in supplies.
    virtual const juce::StringArray& categoryList() const = 0;   // the last is where a new preset goes by default
    virtual int factoryCount() const = 0;
    virtual juce::String factoryName (int index) const = 0;
    virtual juce::String factoryCategory (int index) const = 0;
    virtual std::unique_ptr<juce::XmlElement> factoryXml (int index) const = 0;
    virtual std::unique_ptr<juce::XmlElement> capture (const juce::String& name) const = 0;
    virtual void apply (const juce::XmlElement&, const juce::String& name) = 0;        // one undo step, where there's undo
    virtual void applyQuietly (const juce::XmlElement&, const juce::String& name) = 0;  // the same, not recorded
    virtual void commit (std::unique_ptr<juce::XmlElement> before, const juce::String& beforeName) = 0;
    virtual juce::String currentName() const = 0;
    virtual void setCurrentName (const juce::String&) = 0;

    const juce::File& getFolder() const noexcept { return folder; }

    std::vector<Entry> list() const   // factory first, then user, by name
    {
        std::vector<Entry> entries;
        for (int i = 0; i < factoryCount(); ++i)
            entries.push_back ({ factoryName (i), true, i, {}, factoryCategory (i) });

        auto files = folder.findChildFiles (juce::File::findFiles, false, "*" + extension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
        for (const auto& file : files)
        {
            // The category is in the file; one that's unreadable or has none goes last.
            juce::String category = categoryList()[categoryList().size() - 1];
            if (const auto xml = juce::XmlDocument::parse (file); xml != nullptr && categoryList().contains (xml->getStringAttribute ("category")))
                category = xml->getStringAttribute ("category");
            entries.push_back ({ file.getFileNameWithoutExtension(), false, -1, file, category });
        }
        return entries;
    }

    bool load (const Entry& entry)
    {
        const auto xml = read (entry);
        if (xml == nullptr)
            return false;
        apply (*xml, entry.name);
        return true;
    }

    bool audition (const Entry& entry)
    {
        const auto xml = read (entry);
        if (xml == nullptr)
            return false;
        applyQuietly (*xml, entry.name);
        return true;
    }

    bool save (const juce::String& name, const juce::String& category = {})
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty() || ! folder.createDirectory())
            return false;
        const auto xml = capture (clean);
        xml->setAttribute ("category", categoryList().contains (category) ? category : categoryList()[categoryList().size() - 1]);
        if (! xml->writeTo (folder.getChildFile (clean + extension)))
            return false;
        setCurrentName (clean);
        return true;
    }

    bool step (int direction)   // the next or previous in list()
    {
        const auto entries = list();
        if (entries.empty())
            return false;
        int at = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == currentName())
                at = (int) i;
        const int n = (int) entries.size();
        return load (entries[(size_t) (((at < 0 ? (direction > 0 ? -1 : 0) : at) + direction + n) % n)]);
    }

    // User presets only: rename, move to the bin, or file under another category.
    bool rename (const Entry& entry, const juce::String& newName)
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
        if (currentName() == entry.name)
            setCurrentName (clean);
        return true;
    }

    bool remove (const Entry& entry)
    {
        // To the bin, not gone: it can be fished back out.
        if (entry.factory || ! (toTrash ? entry.file.moveToTrash() : entry.file.deleteFile()))
            return false;
        setFavourite (entry.name, false);
        return true;
    }
    static inline bool toTrash = true;   // tests delete their own files instead

    bool setCategory (const Entry& entry, const juce::String& category)
    {
        if (entry.factory || ! categoryList().contains (category))
            return false;
        const auto xml = juce::XmlDocument::parse (entry.file);
        if (xml == nullptr)
            return false;
        xml->setAttribute ("category", category);
        return xml->writeTo (entry.file);
    }

    bool isFavourite (const juce::String& name) const { return favourites().contains (name); }

    void setFavourite (const juce::String& name, bool favourite)
    {
        auto names = favourites();
        if (favourite)
            names.addIfNotAlreadyThere (name);
        else
            names.removeString (name);
        if (folder.createDirectory())
            folder.getChildFile ("favourites.txt").replaceWithText (names.joinIntoString ("\n"));
    }

    // Whether an entry shows under a view (ALL, FAVOURITES or a category) and a search.
    bool shows (const Entry& entry, const juce::String& view, const juce::String& search) const
    {
        const bool inView = view == "ALL" || (view == "FAVOURITES" ? isFavourite (entry.name) : entry.category == view);
        const auto query = search.trim();
        return inView && (query.isEmpty() || entry.name.containsIgnoreCase (query) || entry.category.containsIgnoreCase (query));
    }

private:
    std::unique_ptr<juce::XmlElement> read (const Entry& entry) const
    {
        auto xml = entry.factory ? factoryXml (entry.factoryIndex) : juce::XmlDocument::parse (entry.file);
        return xml != nullptr && xml->hasTagName (tag) ? std::move (xml) : nullptr;
    }

    juce::StringArray favourites() const
    {
        juce::StringArray names;
        names.addLines (folder.getChildFile ("favourites.txt").loadFileAsString());
        names.removeEmptyStrings();
        return names;
    }

    juce::File folder;
    juce::String tag, extension;
};

// Asks for a name and a category, then saves. Shared by the bar and the browser.
inline std::unique_ptr<juce::AlertWindow> askToSave (PresetLibrary& presets, juce::Component* over, int startCategory,
                                                     const juce::String& startName, std::function<void (bool)> onDone)
{
    auto window = std::make_unique<juce::AlertWindow> ("Save preset", "A name for this sound:", juce::MessageBoxIconType::NoIcon, over);
    window->addTextEditor ("name", startName);
    window->addComboBox ("category", presets.categoryList(), "Category");
    if (auto* box = window->getComboBoxComponent ("category"))
        box->setSelectedItemIndex (startCategory);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* raw = window.get();
    raw->enterModalState (true, juce::ModalCallbackFunction::create ([&presets, raw, done = std::move (onDone)] (int result)
    {
        bool saved = false;
        if (result == 1)
        {
            const auto* box = raw->getComboBoxComponent ("category");
            saved = presets.save (raw->getTextEditorContents ("name"), box != nullptr ? box->getText() : juce::String());
        }
        if (done)
            done (saved);
    }));
    return window;
}

// The browser, over the plug-in's main sheet: a search box, category keys and the
// list (the star favourites it). Clicking a preset or moving with the arrow keys plays
// it at once; KEEP (Return, double-click) keeps it, CANCEL (Esc) puts back the sound
// there was. Right-click a user preset to rename, file or remove it.
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    PresetBrowser (PresetLibrary& library, const Stock& stockToUse) : presets (library), s (stockToUse)
    {
        setWantsKeyboardFocus (true);
        search.setTextToShowWhenEmpty ("SEARCH", s.onSunk.withAlpha (0.6f));
        search.setFont (label (18.0f));
        search.setIndents (6, 3);
        search.onTextChange = [this] { refresh(); };
        search.onReturnKey = [this] { close (true); };
        search.onEscapeKey = [this] { close (false); };
        addAndMakeVisible (search);

        juce::StringArray views { "ALL", "FAVOURITES" };
        views.addArray (presets.categoryList());
        viewNames = views;
        for (const auto& v : views)
        {
            auto* key = viewKeys.add (new juce::TextButton (v == "FAVOURITES" ? juce::String ("FAV") : v));
            key->setClickingTogglesState (true);
            key->setRadioGroupId (2);
            // Only the key being turned on: a radio group also "clicks" the one it turns off.
            key->onClick = [this, v, key] { if (key->getToggleState()) setView (v); };
            key->setTooltip (v == "FAVOURITES" ? "Your starred presets" : "");
            addAndMakeVisible (key);
        }
        viewKeys[0]->setToggleState (true, juce::dontSendNotification);

        list.setModel (this);
        list.setRowHeight (26);
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
        keep.setTooltip ("Keep this sound (Return)");
        cancel.setTooltip ("Back to the sound you had (Esc)");
        keep.getProperties().set ("accent", true);
        for (auto* b : { &keep, &cancel, &saveAsButton, &folderButton })
            addAndMakeVisible (b);
        setVisible (false);
    }

    ~PresetBrowser() override { list.setModel (nullptr); }

    std::function<void()> onClosed;

    void open()   // shows it, remembering the sound to go back to
    {
        soundBefore = presets.capture (presets.currentName());
        nameBefore = presets.currentName();
        auditioned = false;
        search.clear();
        refresh();
        setVisible (true);
        toFront (true);
        // The current preset selected, without playing it again.
        for (size_t i = 0; i < shown.size(); ++i)
            if (shown[i].name == presets.currentName())
            {
                const juce::ScopedValueSetter<bool> quiet (choosing, true);
                list.selectRow ((int) i);
            }
        search.grabKeyboardFocus();
    }

    void close (bool keepIt)   // keep: one undo step; otherwise back to the sound before
    {
        if (! isVisible())
            return;
        if (auditioned)
        {
            if (keepIt)
                presets.commit (std::move (soundBefore), nameBefore);
            else if (soundBefore != nullptr)
                presets.applyQuietly (*soundBefore, nameBefore);
        }
        soundBefore = nullptr;
        auditioned = false;
        setVisible (false);
        if (onClosed)
            onClosed();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3), s, "P", "PRESETS",
                   juce::String ((int) shown.size()) + (shown.size() == 1 ? " SOUND" : " SOUNDS"));
        g.setColour (s.soft);
        g.setFont (label (15.0f));
        g.drawText ("CLICK TO HEAR   RETURN KEEPS   ESC GOES BACK", keysFoot, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3).withTrimmedTop (panelHeadHeight).reduced (10, 8);
        auto top = r.removeFromTop (30);
        search.setBounds (top.removeFromLeft (juce::jmin (200, top.getWidth() / 3)));
        top.removeFromLeft (8);
        const int keyWidth = top.getWidth() / juce::jmax (1, viewKeys.size());
        for (auto* key : viewKeys)
            key->setBounds (top.removeFromLeft (keyWidth).reduced (1, 0));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (32);
        r.removeFromBottom (8);
        list.setBounds (r);
        for (auto* b : { &keep, &cancel })
            b->setBounds (bottom.removeFromRight (90).reduced (3, 0));
        for (auto* b : { &saveAsButton, &folderButton })
            b->setBounds (bottom.removeFromLeft (100).reduced (3, 0));
        keysFoot = bottom;
    }

    bool keyPressed (const juce::KeyPress& key) override
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

    // For tests.
    int numShown() const { return (int) shown.size(); }
    juce::String shownName (int row) const { return row >= 0 && row < (int) shown.size() ? shown[(size_t) row].name : juce::String(); }
    void setView (const juce::String& v)
    {
        view = v;
        for (int i = 0; i < viewKeys.size(); ++i)
            viewKeys[i]->setToggleState (viewNames[i] == v, juce::dontSendNotification);   // just this one lit
        refresh();
    }
    void setSearch (const juce::String& text) { search.setText (text, true); refresh(); }
    void choose (int row)   // audition the row, as a click does
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        if (presets.audition (shown[(size_t) row]))
            auditioned = true;
        list.repaint();
    }
    void toggleFavourite (int row)   // its star, as a click on it does
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        presets.setFavourite (shown[(size_t) row].name, ! presets.isFavourite (shown[(size_t) row].name));
        refresh();
    }
    bool isFavourite (int row) const { return row >= 0 && row < (int) shown.size() && presets.isFavourite (shown[(size_t) row].name); }

private:
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto& e = shown[(size_t) row];
        const bool current = e.name == presets.currentName();
        auto r = juce::Rectangle<int> (width, height);
        const auto text = selected ? s.panel : s.ink, quiet = selected ? s.panel : s.soft;
        if (selected)
        {
            g.setColour (s.ink);
            g.fillRect (r);
        }
        else
        {
            g.setColour (s.rule);
            g.drawHorizontalLine (height - 1, 0.0f, (float) width);
        }
        // The star: click it to favourite (filled) or not (outlined).
        {
            const auto box = r.removeFromLeft (starWidth).toFloat();
            juce::Path star;
            star.addStar (box.getCentre(), 5, 3.4f, 7.5f, 0.0f);   // a point at the top
            if (presets.isFavourite (e.name))
            {
                g.setColour (selected ? text : s.accent);
                g.fillPath (star);
            }
            else
            {
                g.setColour (quiet);
                g.strokePath (star, juce::PathStrokeType (1.2f));
            }
        }
        g.setFont (label (17.0f));
        g.setColour (quiet);
        g.drawText (e.factory ? "FACTORY" : "USER", r.removeFromRight (80), juce::Justification::centredLeft);
        g.drawText (e.category, r.removeFromRight (120), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (label (19.0f));
        g.drawText ((current ? "> " : "") + e.name.toUpperCase(), r, juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
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

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        choose (row);
        close (true);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        // The arrow keys play as they move (a click plays, or stars, in listBoxItemClicked).
        if (! choosing && lastRowSelected >= 0 && list.hasKeyboardFocus (true)
            && ! juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown())
            choose (lastRowSelected);
    }

    void refresh()
    {
        shown.clear();
        for (auto& entry : presets.list())
            if (presets.shows (entry, view, search.getText()))
                shown.push_back (std::move (entry));
        list.updateContent();
        list.repaint();
        repaint();
    }

    void showRowMenu (int row)
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto entry = shown[(size_t) row];
        juce::PopupMenu menu, file;
        const bool fav = presets.isFavourite (entry.name);
        menu.addItem (1, fav ? "Remove from favourites" : "Add to favourites");
        if (! entry.factory)
        {
            for (int i = 0; i < presets.categoryList().size(); ++i)
                file.addItem (100 + i, presets.categoryList()[i], true, entry.category == presets.categoryList()[i]);
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
                presets.setCategory (entry, presets.categoryList()[chosen - 100]);
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

    void saveAs()
    {
        const int here = presets.categoryList().indexOf (view);
        const auto start = presets.currentName();
        asking = askToSave (presets, this, here >= 0 ? here : presets.categoryList().size() - 1, start == "Init" ? juce::String() : start, [this] (bool saved)
        {
            if (saved)
            {
                soundBefore = presets.capture (presets.currentName());   // saved: it's the sound to keep now
                nameBefore = presets.currentName();
                auditioned = false;
            }
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PresetBrowser> (this)]
            {
                if (safe != nullptr)
                {
                    safe->asking = nullptr;
                    safe->refresh();
                }
            });
        });
    }

    static constexpr int starWidth = 30;

    PresetLibrary& presets;
    const Stock& s;
    std::vector<PresetLibrary::Entry> shown;
    juce::String view { "ALL" };
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> viewKeys;
    juce::StringArray viewNames;   // each key's view, in the same order
    juce::ListBox list;
    juce::TextButton keep { "KEEP" }, cancel { "CANCEL" }, saveAsButton { "SAVE AS..." }, folderButton { "FOLDER" };
    juce::Rectangle<int> keysFoot;
    std::unique_ptr<juce::XmlElement> soundBefore;
    juce::String nameBefore;
    bool auditioned = false, choosing = false;
    std::unique_ptr<juce::AlertWindow> asking;
};

// The masthead's patch box: previous, the name (click for the browser), next, SAVE.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetLibrary& library) : presets (library)
    {
        previous.getProperties().set ("arrow", -1);
        next.getProperties().set ("arrow", 1);
        previous.setTitle ("Previous preset");
        next.setTitle ("Next preset");
        previous.onClick = [this] { presets.step (-1); refresh(); changed(); };
        next.onClick = [this] { presets.step (1); refresh(); changed(); };
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
            asking = askToSave (presets, this, presets.categoryList().size() - 1, current == "Init" ? juce::String() : current, [this] (bool)
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PresetBar> (this)]
                {
                    if (safe != nullptr)
                    {
                        safe->asking = nullptr;
                        safe->refresh();
                    }
                });
            });
        };
        for (auto* b : { &previous, &next, &name, &save })
            addAndMakeVisible (b);
        refresh();
    }

    std::function<void()> onBrowse;    // the name was clicked
    std::function<void()> onStepped;   // an arrow moved to another preset

    void resized() override
    {
        auto r = getLocalBounds();
        save.setBounds (r.removeFromRight (58));
        r.removeFromRight (8);
        previous.setBounds (r.removeFromLeft (32));
        next.setBounds (r.removeFromRight (32));
        name.setBounds (r);
    }

    void refresh()   // shows the current preset's name
    {
        const auto current = presets.currentName();
        name.setButtonText (current.isEmpty() ? juce::String ("INIT") : current.toUpperCase());
    }

private:
    void changed() { if (onStepped) onStepped(); }

    PresetLibrary& presets;
    juce::TextButton previous, next, name, save { "SAVE" };
    std::unique_ptr<juce::AlertWindow> asking;
};
} // namespace kit
