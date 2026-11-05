#pragma once

#include "Look.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>

// The Bench: what passes between plug-ins, either way. A plug-in sends a moment and
// another takes it. A moment is a WAV in the sender's own folder, which you can choose
// (with a category folder inside it, if you like), and a note in Application Support, so it works whichever process
// or host each plug-in lives in, neither needs the other open, and everything sent stays
// there to use in the Finder.
namespace kit::bench
{
struct Moment
{
    juce::File audio;
    juce::String fromChip, fromName;
    double seconds = 0.0;
    juce::int64 stamp = 0;   // when it was sent: a later one is a new moment
};

inline juce::File folderForTests;   // tests keep away from the real Bench

// Where the notes and the settings live. They stay put when the audio folder moves.
inline juce::File configFolder()
{
    if (folderForTests != juce::File())
        return folderForTests;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Bench");
}

inline juce::String setting (const juce::String& name)
{
    const auto xml = juce::XmlDocument::parse (configFolder().getChildFile ("settings.xml"));
    return xml != nullptr ? xml->getStringAttribute (name) : juce::String();
}

inline void setSetting (const juce::String& name, const juce::String& value)
{
    const auto file = configFolder().getChildFile ("settings.xml");
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("BENCH");
    xml->setAttribute (name, value);
    configFolder().createDirectory();
    xml->writeTo (file);
}

// Each plug-in keeps its recordings in its own folder, which you can choose, and its own
// categories inside that: `owner` is its name, "BROOD" or "SILT". Only the notes above are
// shared, which is all that sending between plug-ins needs.
inline juce::File audioRoot (const juce::String& owner)
{
    if (const auto chosen = setting ("folder_" + owner); chosen.isNotEmpty())
        return juce::File (chosen);
    if (const auto shared = setting ("folder"); shared.isNotEmpty())
        return juce::File (shared);   // chosen before each plug-in had its own
    if (folderForTests != juce::File())
        return folderForTests;
    const auto name = owner.substring (0, 1) + owner.substring (1).toLowerCase();
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Bench").getChildFile (name);
}

// A category is a folder inside it: what you send goes there until you change it.
inline juce::StringArray categories (const juce::String& owner)
{
    juce::StringArray names;
    for (const auto& dir : audioRoot (owner).findChildFiles (juce::File::findDirectories, false))
        names.add (dir.getFileName());
    names.sort (true);
    return names;
}

inline juce::File audioFolder (const juce::String& owner)
{
    const auto category = setting ("category_" + owner);
    return category.isNotEmpty() ? audioRoot (owner).getChildFile (category) : audioRoot (owner);
}

// Where a new moment's audio goes: named for who sent it and when, so it reads in the Finder.
inline juce::File newAudioFile (const juce::String& owner, const juce::String& name)
{
    const auto folder = audioFolder (owner);
    folder.createDirectory();
    const auto when = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
    return folder.getNonexistentChildFile (juce::File::createLegalFileName (name + " " + when), ".wav", false);
}

// A finished WAV goes on the Bench. The note is written last, so a reader never finds
// half a moment. Each sender keeps one note: its latest.
inline bool publish (const juce::File& audio, double seconds, const juce::String& chip, const juce::String& name,
                     const juce::File& notes = configFolder())
{
    notes.createDirectory();
    juce::XmlElement note ("MOMENT");
    note.setAttribute ("file", audio.getFullPathName());
    note.setAttribute ("chip", chip);
    note.setAttribute ("name", name);
    note.setAttribute ("seconds", seconds);
    note.setAttribute ("stamp", juce::String (juce::Time::currentTimeMillis()));
    return note.writeTo (notes.getChildFile ("." + juce::File::createLegalFileName (chip) + ".moment"));
}

inline bool send (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& chip, const juce::String& name, const juce::String& owner)
{
    if (audio.getNumSamples() == 0 || sampleRate <= 0.0 || ! audioFolder (owner).createDirectory())
        return false;
    const auto file = newAudioFile (owner, name);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return false;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")   // the replacement isn't in every JUCE this builds against
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, (unsigned int) audio.getNumChannels(), 24, {}, 0));
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        if (writer == nullptr)
            return false;
        stream.release();   // the writer owns it now
        if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return false;
    }
    return publish (file, (double) audio.getNumSamples() / sampleRate, chip, name);
}

// The newest moment from anyone but `myChip`.
inline std::optional<Moment> latest (const juce::String& myChip)
{
    std::optional<Moment> best;
    for (const auto& noteFile : configFolder().findChildFiles (juce::File::findFiles, false, ".*.moment"))
    {
        const auto xml = juce::XmlDocument::parse (noteFile);
        if (xml == nullptr || ! xml->hasTagName ("MOMENT") || xml->getStringAttribute ("chip") == myChip)
            continue;
        Moment m { juce::File (xml->getStringAttribute ("file")), xml->getStringAttribute ("chip"), xml->getStringAttribute ("name"),
                   xml->getDoubleAttribute ("seconds"), xml->getStringAttribute ("stamp").getLargeIntValue() };
        if (m.audio.existsAsFile() && (! best.has_value() || m.stamp > best->stamp))
            best = m;
    }
    return best;
}

// Bars of the exact output, from the next bar line (at once if the host is stopped). The
// message thread arms it and collects it; the audio thread only writes into what was
// allocated first.
class OutputRecorder
{
public:
    enum class State { idle, waiting, recording, done, cancelling };

    void arm (int bars, double barQuarters, double bpm, double sampleRate)
    {
        if (state() != State::idle)
            return;
        const double seconds = bars * barQuarters * 60.0 / juce::jmax (1.0, bpm);
        rate = sampleRate;
        audio.setSize (2, juce::jmax (1, juce::roundToInt (juce::jmin (seconds, 300.0) * sampleRate)));
        written = 0;
        current = (int) State::waiting;
    }
    void cancel() { current = (int) State::cancelling; }
    State state() const { return (State) current.load(); }
    float progress() const { return audio.getNumSamples() > 0 ? (float) written.load() / (float) audio.getNumSamples() : 0.0f; }

    bool take (juce::AudioBuffer<float>& out, double& sampleRate)
    {
        if (state() != State::done)
            return false;
        out = audio;
        sampleRate = rate;
        audio.setSize (2, 0);
        current = (int) State::idle;
        return true;
    }

    // From the audio thread, after the block's output is final.
    void process (const float* left, const float* right, int n, bool playing, double ppq, double barStart, double barQuarters, double bpm)
    {
        if (state() == State::cancelling)
            current = (int) State::idle;
        int from = 0;
        if (state() == State::waiting)
        {
            from = -1;
            if (! playing)
                from = 0;
            else
            {
                const double intoBar = ppq - barStart;
                const double toBar = intoBar < 1e-6 ? 0.0 : (barQuarters - intoBar) * 60.0 / juce::jmax (1.0, bpm) * rate;
                if (toBar < n)
                    from = (int) toBar;
            }
            if (from >= 0)
                current = (int) State::recording;
        }
        if (state() == State::recording)
        {
            const int at = written.load(), count = juce::jmin (n - from, audio.getNumSamples() - at);
            audio.copyFrom (0, at, left + from, count);
            audio.copyFrom (1, at, right + from, count);
            written = at + count;
            if (at + count >= audio.getNumSamples())
                current = (int) State::done;
        }
    }

private:
    std::atomic<int> current { 0 }, written { 0 };
    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
};

// The strip along the bottom of every plug-in: its own name and state, what's waiting
// from elsewhere, and SEND, TAKE and the folder it all lives in.
class Strip final : public juce::Component
{
public:
    Strip (const Stock& stockToUse, juce::String chipText, juce::String nameText, juce::String ownerText)
        : s (stockToUse), chip (std::move (chipText)), name (std::move (nameText)), owner (std::move (ownerText))
    {
        sendKey.onClick = [this] { if (onSend) onSend(); };
        takeKey.onClick = [this] { if (onTake) onTake(); };
        folderKey.onClick = [this] { showFolderMenu(); };
        refreshFolderKey();
        addAndMakeVisible (sendKey);
        addAndMakeVisible (takeKey);
        addAndMakeVisible (folderKey);
    }

    std::function<void()> onSend, onTake;
    int reservedRight = 0;   // room left beside the keys for a control the plug-in adds
    void setTakeShown (bool shown) { takeKey.setVisible (shown); resized(); repaint(); }   // for a plug-in that has nothing to take into

    void set (const juce::String& state, const juce::String& momentText, const juce::String& sendText, bool sendOn, bool takeOn)
    {
        if (state == stateText && momentText == moment && sendText == sendKey.getButtonText() && sendOn == sendKey.isEnabled() && takeOn == takeKey.isEnabled())
            return;
        stateText = state;
        moment = momentText;
        sendKey.setButtonText (sendText);
        sendKey.setEnabled (sendOn);
        takeKey.setEnabled (takeOn);
        repaint();
    }

    // Where the plug-in's own control goes, in this strip's coordinates.
    juce::Rectangle<int> reservedArea() const { return keysArea().withWidth (reservedRight - 8).withX (sendKey.getX() - reservedRight); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3);
        g.setColour (s.shadow);
        g.fillRect (r.translated (3, 3));
        g.setColour (s.head);
        g.fillRect (r);
        g.setColour (s.ink);
        g.drawRect (r, 1);

        auto row = r.reduced (1, 1);
        auto title = row.removeFromLeft (92);
        g.setFont (wordmark (9.0f));
        g.drawText ("THE", title.removeFromTop (title.getHeight() / 2).withTrimmedLeft (10), juce::Justification::centredBottom);
        g.drawText ("BENCH", title.withTrimmedLeft (10), juce::Justification::centredTop);

        g.drawVerticalLine (row.getX(), (float) row.getY(), (float) row.getBottom());
        auto mine = row.removeFromLeft (row.getWidth() * 3 / 10).withTrimmedLeft (1);
        g.setColour (s.panel);
        g.fillRect (mine);
        auto text = mine.reduced (12, 0);
        const auto font = label (18.0f);
        g.setFont (font);
        g.setColour (s.ink);
        const auto head = chip + "  " + name;
        g.drawText (head, text.removeFromLeft (textWidth (font, head) + 4), juce::Justification::centredLeft);
        text.removeFromLeft (10);
        g.setColour (s.soft);
        g.drawFittedText (stateText, text, juce::Justification::centredLeft, 1, 0.7f);

        g.setColour (s.ink);
        g.drawVerticalLine (row.getX(), (float) row.getY(), (float) row.getBottom());
        auto rest = row.reduced (12, 0).withRight (sendKey.getX() - reservedRight - 6);
        g.setColour (s.soft);
        g.drawFittedText (moment, rest, juce::Justification::centredLeft, 1, 0.7f);
    }

    void resized() override
    {
        auto r = keysArea();
        folderKey.setBounds (r.removeFromRight (keyWidth));
        r.removeFromRight (keyGap);
        if (takeKey.isVisible())
        {
            takeKey.setBounds (r.removeFromRight (keyWidth));
            r.removeFromRight (keyGap);
        }
        sendKey.setBounds (r.removeFromRight (keyWidth));
    }

private:
    void refreshFolderKey()
    {
        const auto category = setting ("category_" + owner);
        folderKey.setButtonText (category.isNotEmpty() ? category.toUpperCase() : juce::String ("FOLDER"));
        folderKey.setTooltip ("Sending to " + audioFolder (owner).getFullPathName());
    }

    // Where sent audio goes: open it, choose another folder, or send into a category.
    void showFolderMenu()
    {
        juce::PopupMenu menu;
        const auto names = categories (owner);
        const auto current = setting ("category_" + owner);
        menu.addSectionHeader (audioFolder (owner).getFullPathName());
        menu.addItem (1, "Show in Finder");
        menu.addItem (2, "Choose a folder...");
        menu.addSectionHeader ("SEND INTO");
        menu.addItem (3, "No category", true, current.isEmpty());
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (100 + i, names[i], true, names[i] == current);
        menu.addItem (4, "New category...");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderKey), [this, names] (int chosen)
        {
            if (chosen == 1)
            {
                audioFolder (owner).createDirectory();
                audioFolder (owner).revealToUser();
            }
            else if (chosen == 2)
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose where " + name + " keeps what it sends", audioRoot (owner));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() != juce::File())
                    {
                        setSetting ("folder_" + owner, fc.getResult().getFullPathName());
                        setSetting ("category_" + owner, {});
                        refreshFolderKey();
                    }
                });
            }
            else if (chosen == 3)
            {
                setSetting ("category_" + owner, {});
                refreshFolderKey();
            }
            else if (chosen == 4)
            {
                asking = std::make_unique<juce::AlertWindow> ("New category", "A name, such as Guitar. It becomes a folder inside the one you send to.", juce::MessageBoxIconType::NoIcon, this);
                asking->addTextEditor ("name", {});
                asking->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
                asking->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                asking->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
                {
                    const auto category = juce::File::createLegalFileName (asking->getTextEditorContents ("name").trim());
                    if (result == 1 && category.isNotEmpty())
                    {
                        audioRoot (owner).getChildFile (category).createDirectory();
                        setSetting ("category_" + owner, category);
                        refreshFolderKey();
                    }
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Strip> (this)] { if (safe != nullptr) safe->asking = nullptr; });
                }));
            }
            else if (chosen >= 100)
            {
                setSetting ("category_" + owner, names[chosen - 100]);
                refreshFolderKey();
            }
        });
    }

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> asking;
    static constexpr int keyWidth = 78, keyGap = 6;
    juce::Rectangle<int> keysArea() const { return getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3).reduced (6, 7); }

    const Stock& s;
    juce::String chip, name, owner, stateText, moment;
    juce::TextButton sendKey { "SEND >" }, takeKey { "< TAKE" }, folderKey { "FOLDER" };
};
} // namespace kit::bench
