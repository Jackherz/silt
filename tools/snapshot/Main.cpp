// silt-snapshot: renders the Silt panel to PNGs without a host or a window, so
// UI changes can be checked from the command line.
//
//   silt_snapshot sample.wav out-folder [param=value ...] [route=mod:param:amount ...]
//
// Loads the sample, holds a note so grains are sounding, then writes one PNG
// per page (grain.png, shape.png, pitch.png, play.png, resonator.png, filter.png, color.png, space.png, mod.png, chain.png, then routes.png with the route list open). Parameter values are in the
// parameter's own units (ms, %, st...).

#include "../../plugin/PluginEditor.h"

#include <iostream>

namespace
{
void runMessageLoop (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

// The sample also streams into the input (the sidechain), so Live mode has
// something to granulate.
juce::AudioBuffer<float> input;
int inputPos = 0;

void play (SiltProcessor& silt, double seconds, bool startNote)
{
    juce::AudioBuffer<float> buffer (juce::jmax (silt.getTotalNumInputChannels(), silt.getTotalNumOutputChannels()), 512);
    for (int done = 0; done < (int) (seconds * 48000.0); done += 512)
    {
        buffer.clear();
        for (int ch = 0; ch < silt.getTotalNumInputChannels(); ++ch)   // main input and sidechain alike
            for (int i = 0; i < 512 && input.getNumSamples() > 0; ++i)
                buffer.setSample (ch, i, input.getSample (ch % input.getNumChannels(), (inputPos + i) % input.getNumSamples()));
        inputPos += 512;
        juce::MidiBuffer midi;
        if (startNote && done == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        silt.processBlock (buffer, midi);
    }
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: silt_snapshot sample.wav out-folder [param=value ...]\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juce;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto sample = cwd.getChildFile (argv[1]);
    const auto outDir = cwd.getChildFile (argv[2]);
    outDir.createDirectory();

    SiltProcessor silt;
    silt.enableAllBuses();
    silt.setRateAndBufferSizeDetails (48000.0, 512);
    silt.prepareToPlay (48000.0, 512);

    for (int i = 3; i < argc; ++i)
    {
        const juce::String setting (argv[i]);
        const auto id = setting.upToFirstOccurrenceOf ("=", false, false);
        if (id == "armed")   // armed=modulator (1–4)
        {
            silt.armedMod.store (setting.fromFirstOccurrenceOf ("=", false, false).getIntValue() - 1);
            continue;
        }
        if (id == "route")   // route=modulator:parameter:amount (modulator 1–4)
        {
            const auto parts = juce::StringArray::fromTokens (setting.fromFirstOccurrenceOf ("=", false, false), ":", "");
            silt.setRouteAmount (parts[0].getIntValue() - 1, silt.modulationTarget (parts[1]), parts[2].getFloatValue());
            continue;
        }
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (silt.params.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (setting.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
        else
            std::cerr << "unknown parameter " << id << "\n";
    }

    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (sample)); reader != nullptr)
        {
            input.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&input, 0, input.getNumSamples(), 0, true, true);
        }
    }

    silt.loadFile (sample);
    const auto loaded = [&] { return silt.getOverview().name == sample.getFileNameWithoutExtension(); };
    for (int i = 0; i < 50 && ! loaded(); ++i)
        runMessageLoop (20);
    if (! loaded())
    {
        std::cerr << "couldn't load " << sample.getFullPathName() << "\n";
        return 1;
    }

    play (silt, 3.0, true);

    SiltEditor::tourOnFirstOpen = false;
    silt::ui::Tour::remember = false;
    std::unique_ptr<juce::AudioProcessorEditor> editor (silt.createEditor());
    editor->setVisible (true);
    runMessageLoop (100);

    for (const auto* name : { "GRAIN", "SHAPE", "PITCH", "PLAY", "RESONATOR", "FILTER", "COLOR", "SPACE", "MOD", "CHAIN" })
    {
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<juce::TextButton*> (child); tab != nullptr && tab->getButtonText() == name)
                tab->triggerClick();

        play (silt, 0.05, false);
        runMessageLoop (60);
        play (silt, 0.05, false);   // again: a page's scope taps only once it's shown
        runMessageLoop (60);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outDir.getChildFile (juce::String (name).toLowerCase() + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << file.getFullPathName() << "\n";
    }

    // Tour cards: the first, the effects and the movement steps (tour-1.png …).
    if (auto* siltEditor = dynamic_cast<SiltEditor*> (editor.get()))
    {
        siltEditor->startTour();
        silt::ui::Tour* tour = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* t = dynamic_cast<silt::ui::Tour*> (child))
                tour = t;
        for (const int step : { 0, 2, 5, 7, 8 })
        {
            tour->go (step);
            play (silt, 0.05, false);
            runMessageLoop (80);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            const auto file = outDir.getChildFile ("tour-" + juce::String (step + 1) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
            std::cout << file.getFullPathName() << "\n";
        }
        tour->go (99);
    }

    // The route list, over the last page.
    if (auto* siltEditor = dynamic_cast<SiltEditor*> (editor.get()))
    {
        siltEditor->showRoutes (-1);
        runMessageLoop (120);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outDir.getChildFile ("routes.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << file.getFullPathName() << "\n";
    }

    // Processing cost: ten seconds of audio in 512-sample blocks, against real time.
    {
        const auto started = juce::Time::getMillisecondCounterHiRes();
        play (silt, 10.0, false);
        const double ms = juce::Time::getMillisecondCounterHiRes() - started;
        std::cout << "processing: " << juce::String (ms / 100.0, 2) << "% of one core\n";
    }

    // Drawing cost: the whole panel, repeatedly, at 1x (the plug-in itself
    // repaints only the screen, 30 times a second, while grains are sounding).
    constexpr int frames = 120;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < frames; ++i)
        editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const double msPerFrame = (juce::Time::getMillisecondCounterHiRes() - started) / frames;
    std::cout << "full-panel draw: " << juce::String (msPerFrame, 2) << " ms per frame, "
              << juce::String (msPerFrame * 30.0 / 10.0, 1) << "% of one core at 30 fps\n";

    editor = nullptr;
    silt.releaseResources();
    return 0;
}
