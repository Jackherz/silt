#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Set = juce::AudioChannelSet;
using Float = juce::AudioParameterFloat;
using Range = juce::NormalisableRange<float>;

constexpr int stateMagic = 0x544c4953;   // "SILT"

// Synced Size and Rate: note values, in quarter notes (T = triplet, D = dotted).
const juce::StringArray divisionNames { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T",
                                        "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1 bar", "2 bars", "4 bars" };
constexpr double divisionQuarters[] { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2, 2.0 / 3,
                                      3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 3.0, 4.0, 8.0, 16.0 };
constexpr int sixteenth = 3;
constexpr int rateModeSync = 1, rateModeGrains = 2;   // "rateMode" choices; 0 is Free
constexpr double maxSyncedSizeMs = 4000.0;

double quartersFor (const std::atomic<float>* choice) noexcept
{
    return divisionQuarters[juce::jlimit (0, (int) std::size (divisionQuarters) - 1, (int) choice->load (std::memory_order_relaxed))];
}
const juce::StringArray captureBarChoices { "1 bar", "2 bars", "4 bars", "8 bars" };
constexpr int captureBars[] { 1, 2, 4, 8 };

Range skewed (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

// How values read on the panel and in the host: a few significant digits and the unit.
using Format = std::function<juce::String (float, int)>;

juce::String formatMs (float v, int)
{
    // juce::String (v, 0) means "no limit", not "no decimals": round instead.
    if (v >= 1000.0f)
        return juce::String (v / 1000.0f, 2) + " s";
    return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms";
}

juce::String formatHz (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; }
juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
juce::String formatSemitones (float v, int) { return (v >= 0.005f ? "+" : v <= -0.005f ? "" : " ") + juce::String (std::abs (v) < 0.005f ? 0.0f : v, 2) + " st"; }
juce::String formatTimes (float v, int) { return juce::String (v, 2) + "x"; }
juce::String formatDb (float v, int) { return v <= -47.95f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; }

std::unique_ptr<Float> percent (const char* id, const char* name, float def)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, Range (0.0f, 100.0f, 0.1f), def,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPercent));
}

std::unique_ptr<Float> withUnit (const char* id, const char* name, Range range, float def, Format format)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, range, def,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (format)));
}

juce::AudioProcessorValueTreeState::ParameterLayout makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Grain
    layout.add (withUnit ("pitch", "Pitch", Range (-24.0f, 24.0f, 0.01f), 0.0f, formatSemitones),
                withUnit ("size", "Size", skewed (10.0f, 2000.0f, 150.0f), 100.0f, formatMs),
                withUnit ("rate", "Rate", skewed (0.1f, 100.0f, 10.0f), 20.0f, formatHz),
                percent ("spray", "Spray", 0.0f),
                percent ("jitter", "Jitter", 50.0f),
                percent ("reverse", "Reverse", 0.0f),
                percent ("randAmp", "Random amp", 0.0f),
                percent ("randPan", "Random pan", 0.0f),
                percent ("fade", "Fade", 100.0f),
                percent ("slant", "Slant", 50.0f),
                percent ("ease", "Ease", 50.0f));

    // Tempo sync for Size and Rate
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sizeSync", 1 }, "Size sync", false),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sizeDiv", 1 }, "Size (synced)",
                                                              divisionNames, sixteenth),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rateMode", 1 }, "Rate mode",
                                                              juce::StringArray { "Free", "Sync", "Grains" }, 0),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rateDiv", 1 }, "Rate (synced)",
                                                              divisionNames, sixteenth),
                // Size in Grains mode: its own range, with half the knob below 60 ms
                // where the texture changes most.
                withUnit ("grainSize", "Size (grains)", skewed (10.0f, 500.0f, 60.0f), 80.0f, formatMs),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "grains", 1 }, "Grains", 1, 32, 8,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int n, int)
                    {
                        return juce::String (n) + (n == 1 ? " grain" : " grains");
                    })));

    // Region and playhead
    layout.add (percent ("start", "Start", 0.0f),
                percent ("length", "Length", 100.0f),
                withUnit ("scan", "Scan", Range (-4.0f, 4.0f, 0.01f), 1.0f, formatTimes),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "endMode", 1 }, "End",
                                                              juce::StringArray { "Loop", "Hold", "Stop" }, 0));

    // Voice
    layout.add (withUnit ("attack", "Attack", skewed (0.0f, 5000.0f, 100.0f), 5.0f, formatMs),
                withUnit ("decay", "Decay", skewed (1.0f, 5000.0f, 200.0f), 200.0f, formatMs),
                percent ("sustain", "Sustain", 100.0f),
                withUnit ("release", "Release", skewed (1.0f, 10000.0f, 400.0f), 300.0f, formatMs),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "root", 1 }, "Root note", 0, 127, 60,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int note, int)
                    {
                        return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
                    })));

    // Capture and output
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "captureBars", 1 }, "Capture length",
                                                              captureBarChoices, 1),
                withUnit ("gain", "Output", Range (-48.0f, 12.0f, 0.1f), 0.0f, formatDb));

    if constexpr (SiltProcessor::isInstrument)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "monitor", 1 }, "Monitor input", false));
    else
        layout.add (percent ("mix", "Mix", 100.0f));

    return layout;
}

bool isMonoOrStereo (const Set& set) { return set == Set::mono() || set == Set::stereo(); }

// Linear to −1.9 dBFS, then a soft knee to full scale (plan gap 7: a safety
// net for dense patches, not a sound).
float safetyClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    return std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}
} // namespace

struct SiltProcessor::RawParams
{
    explicit RawParams (juce::AudioProcessorValueTreeState& s)
    {
        const auto p = [&s] (const char* id) { return s.getRawParameterValue (id); };
        pitch = p ("pitch"); size = p ("size"); rate = p ("rate"); spray = p ("spray"); jitter = p ("jitter");
        reverse = p ("reverse"); randAmp = p ("randAmp"); randPan = p ("randPan"); fade = p ("fade"); slant = p ("slant"); ease = p ("ease");
        sizeSync = p ("sizeSync"); sizeDiv = p ("sizeDiv"); rateMode = p ("rateMode"); rateDiv = p ("rateDiv"); grains = p ("grains"); grainSize = p ("grainSize");
        start = p ("start"); length = p ("length"); scan = p ("scan"); endMode = p ("endMode");
        attack = p ("attack"); decay = p ("decay"); sustain = p ("sustain"); release = p ("release"); root = p ("root");
        captureBars = p ("captureBars"); gain = p ("gain");
        monitor = p ("monitor"); mix = p ("mix");   // one of these is null, per build
    }

    std::atomic<float>* pitch, *size, *rate, *spray, *jitter, *reverse, *randAmp, *randPan, *fade, *slant, *ease;
    std::atomic<float>* sizeSync, *sizeDiv, *rateMode, *rateDiv, *grains, *grainSize;
    std::atomic<float>* start, *length, *scan, *endMode;
    std::atomic<float>* attack, *decay, *sustain, *release, *root;
    std::atomic<float>* captureBars, *gain, *monitor, *mix;
};

juce::AudioProcessor::BusesProperties SiltProcessor::makeBuses()
{
    if constexpr (isInstrument)
    {
        // Spike A (Live 12, macOS): Live feeds a VST3 instrument's second (aux)
        // bus, but an AU instrument only gets sidechain audio on a single input
        // bus. Logic is still untested.
        if (juce::PluginHostType::getPluginLoadedAs() == wrapperType_AudioUnit)
            return BusesProperties()
                .withInput ("Sidechain", Set::stereo(), true)
                .withOutput ("Output", Set::stereo(), true);

        // The main input stays off by default; the host's sidechain feeds bus 1.
        return BusesProperties()
            .withInput ("Input", Set::stereo(), false)
            .withInput ("Sidechain", Set::stereo(), true)
            .withOutput ("Output", Set::stereo(), true);
    }
    else
        return BusesProperties()
            .withInput ("Input", Set::stereo(), true)
            .withOutput ("Output", Set::stereo(), true);
}

SiltProcessor::SiltProcessor()
    : AudioProcessor (makeBuses()),
      params (*this, nullptr, "SILT", makeParameters()),
      raw (std::make_unique<RawParams> (params))
{
    formats.registerBasicFormats();
    for (auto& v : voicePlayheads)
        v.store (-1.0f);
    startTimerHz (30);
}

SiltProcessor::~SiltProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 5000);
}

void SiltProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    wet.setSize (2, juce::jmax (1, maximumExpectedSamplesPerBlock));
    noteEvents.reserve (1024);
    captureMeter.reset();
}

bool SiltProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != Set::stereo())
        return false;

    for (const auto& input : layouts.inputBuses)
        if (! input.isDisabled() && ! isMonoOrStereo (input))
            return false;

    // Silt FX always needs its own input; the instrument can run with none.
    return isInstrument || ! layouts.getMainInputChannelSet().isDisabled();
}

juce::AudioBuffer<float> SiltProcessor::findCaptureBus (juce::AudioBuffer<float>& buffer, CaptureSource& source)
{
    const auto usable = [this] (int bus)
    {
        const auto* b = getBus (true, bus);
        return b != nullptr && b->isEnabled() && b->getNumberOfChannels() > 0;
    };

    // The sidechain is the last input bus: bus 1 normally, the only bus on AU.
    const int sidechainBus = getBusCount (true) - 1;

    if (isInstrument && usable (sidechainBus))
    {
        source = CaptureSource::sidechain;
        return getBusBuffer (buffer, true, sidechainBus);
    }

    if (usable (0))
    {
        source = CaptureSource::mainInput;
        return getBusBuffer (buffer, true, 0);
    }

    source = CaptureSource::none;
    return {};
}

silt::GrainParams SiltProcessor::readParams() const noexcept
{
    const auto v = [] (const std::atomic<float>* a) { return (double) a->load (std::memory_order_relaxed); };

    silt::GrainParams p;
    p.pitchSemitones = v (raw->pitch);
    p.sizeMs = v (raw->size);
    p.rateHz = v (raw->rate);
    p.spray = v (raw->spray) * 0.01;
    p.jitter = v (raw->jitter) * 0.01;
    p.reverseChance = v (raw->reverse) * 0.01;
    p.randomAmp = v (raw->randAmp) * 0.01;
    p.randomPan = v (raw->randPan) * 0.01;
    p.fade = v (raw->fade) * 0.01;
    p.slant = v (raw->slant) * 0.01;
    p.ease = v (raw->ease) * 0.01;
    p.start = v (raw->start) * 0.01;
    p.length = v (raw->length) * 0.01;
    p.scan = v (raw->scan);
    p.endMode = (silt::EndMode) juce::jlimit (0, 2, (int) v (raw->endMode));
    p.attackMs = v (raw->attack);
    p.decayMs = v (raw->decay);
    p.sustain = v (raw->sustain) * 0.01;
    p.releaseMs = v (raw->release);
    p.rootNote = (int) v (raw->root);

    // Synced values follow the host tempo (the last one seen while stopped).
    const double msPerQuarter = 60000.0 / juce::jlimit (20.0, 400.0, hostBpm.load (std::memory_order_relaxed));
    if (v (raw->sizeSync) > 0.5)
        p.sizeMs = juce::jlimit (10.0, maxSyncedSizeMs, quartersFor (raw->sizeDiv) * msPerQuarter);
    switch ((int) v (raw->rateMode))
    {
        case rateModeSync:
            p.gridDivision = quartersFor (raw->rateDiv);
            p.rateHz = 1000.0 / (p.gridDivision * msPerQuarter);
            break;
        case rateModeGrains:
            p.grainCount = (int) v (raw->grains);
            if (v (raw->sizeSync) < 0.5)
                p.sizeMs = v (raw->grainSize);
            break;
        default:
            break;
    }
    return p;
}

// Audio thread. Armed: wait for the next bar line (or start now when the host
// is stopped), then record until the target is full.
void SiltProcessor::runCapture (const juce::AudioBuffer<float>& capture, const juce::AudioPlayHead::PositionInfo* pos)
{
    auto state = captureState.load (std::memory_order_acquire);

    if (state == CaptureState::cancelRequested)
    {
        captureState.store (CaptureState::cancelled, std::memory_order_release);
        return;
    }

    if (state != CaptureState::armed && state != CaptureState::recording)
        return;

    auto* target = captureTarget.load (std::memory_order_acquire);
    if (target == nullptr)
        return;

    const int numSamples = capture.getNumSamples();
    int offset = 0;

    if (state == CaptureState::armed)
    {
        if (pos != nullptr && pos->getIsPlaying() && pos->getPpqPosition() && pos->getBpm())
        {
            const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature {});
            const double barPpq = sig.numerator * 4.0 / juce::jmax (1, sig.denominator);
            const double ppq = *pos->getPpqPosition();
            const double nextBar = std::ceil (ppq / barPpq - 1.0e-9) * barPpq;
            const double samplesToBar = (nextBar - ppq) * 60.0 / *pos->getBpm() * currentSampleRate;

            if (samplesToBar >= numSamples)
                return;
            offset = juce::jmax (0, (int) samplesToBar);
        }

        captureWritePos = 0;
        captureState.store (CaptureState::recording, std::memory_order_release);
    }

    const int count = juce::jmin (numSamples - offset, target->getNumFrames() - captureWritePos);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* dest = target->getWritePointer (ch) + captureWritePos;
        if (capture.getNumChannels() > 0)
            juce::FloatVectorOperations::copy (dest, capture.getReadPointer (juce::jmin (ch, capture.getNumChannels() - 1), offset), count);
        else
            juce::FloatVectorOperations::clear (dest, count);
    }

    captureWritePos += count;
    captureProgress.store ((float) captureWritePos / (float) target->getNumFrames(), std::memory_order_relaxed);

    if (captureWritePos >= target->getNumFrames())
        captureState.store (CaptureState::done, std::memory_order_release);
}

void SiltProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (auto* playHead = getPlayHead())
        position = playHead->getPosition();

    if (position)
    {
        if (const auto bpm = position->getBpm())
            hostBpm.store (*bpm, std::memory_order_relaxed);
        if (const auto sig = position->getTimeSignature())
        {
            timeSigNumerator.store (sig->numerator, std::memory_order_relaxed);
            timeSigDenominator.store (sig->denominator, std::memory_order_relaxed);
        }
        hostPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
    }

    noteEvents.clear();
    bool allOff = false;
    for (const auto event : midi)
    {
        const auto m = event.getMessage();
        if ((m.isNoteOnOrOff()) && noteEvents.size() < noteEvents.capacity())
            noteEvents.push_back ({ event.samplePosition, m.getNoteNumber(), m.getFloatVelocity(), m.isNoteOn() });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allOff = true;
    }
    if (allOff)
        engine.allNotesOff();

    auto source = CaptureSource::none;
    auto capture = findCaptureBus (buffer, source);
    captureSource.store (source, std::memory_order_relaxed);
    if (capture.getNumChannels() > 0)
        captureMeter.process (capture.getArrayOfReadPointers(), capture.getNumChannels(), numSamples);
    runCapture (capture, position ? &*position : nullptr);

    // Engine output. Hosts can send a block bigger than announced; render in chunks.
    auto output = getBusBuffer (buffer, false, 0);
    const float gain = juce::Decibels::decibelsToGain (raw->gain->load (std::memory_order_relaxed), -48.0f);
    const bool monitoring = isInstrument && raw->monitor->load (std::memory_order_relaxed) > 0.5f;
    const float mix = isInstrument ? 1.0f : raw->mix->load (std::memory_order_relaxed) * 0.01f;
    auto grainParams = readParams();

    // Synced Rate locks grains to the beat grid while the host plays; stopped,
    // they free-run at the synced rate.
    double blockPpq = 0.0;
    if ((int) raw->rateMode->load (std::memory_order_relaxed) == rateModeSync && position && position->getIsPlaying())
        if (const auto ppq = position->getPpqPosition())
            if (const auto bpm = position->getBpm(); bpm && *bpm > 0.0)
            {
                grainParams.gridSync = true;
                grainParams.ppqPerSample = *bpm / 60.0 / currentSampleRate;
                blockPpq = *ppq;
            }

    for (int start = 0; start < numSamples; start += wet.getNumSamples())
    {
        const int len = juce::jmin (wet.getNumSamples(), numSamples - start);
        grainParams.gridPpq = blockPpq + start * grainParams.ppqPerSample;

        // Events for this chunk, shifted to its start.
        const auto first = std::find_if (noteEvents.begin(), noteEvents.end(), [start] (const auto& e) { return e.sampleOffset >= start; });
        const auto last = std::find_if (first, noteEvents.end(), [start, len] (const auto& e) { return e.sampleOffset >= start + len; });
        for (auto it = first; it != last; ++it)
            it->sampleOffset -= start;

        engine.process (wet.getWritePointer (0), wet.getWritePointer (1), len,
                        noteEvents.data() + (first - noteEvents.begin()), (int) (last - first), grainParams);

        // Sample by sample, reading the input before writing the output: the
        // two can share memory.
        const int inCh = capture.getNumChannels();
        for (int i = 0; i < len; ++i)
        {
            const float inL = inCh > 0 ? capture.getSample (0, start + i) : 0.0f;
            const float inR = inCh > 1 ? capture.getSample (1, start + i) : inL;
            const float dryL = isInstrument ? (monitoring ? inL : 0.0f) : inL * (1.0f - mix);
            const float dryR = isInstrument ? (monitoring ? inR : 0.0f) : inR * (1.0f - mix);

            output.setSample (0, start + i, safetyClip ((dryL + wet.getSample (0, i) * mix) * gain));
            if (output.getNumChannels() > 1)
                output.setSample (1, start + i, safetyClip ((dryR + wet.getSample (1, i) * mix) * gain));
        }
    }

    activeGrains.store (engine.getActiveGrainCount(), std::memory_order_relaxed);

    samplesSinceGrainViews += numSamples;
    if (samplesSinceGrainViews >= (int) (currentSampleRate / 60.0))
    {
        samplesSinceGrainViews = 0;
        grainViewSequence.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        grainViewCount.store (engine.collectGrainViews (grainViews.data(), maxGrainViews), std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        grainViewSequence.fetch_add (1, std::memory_order_release);
    }
    for (int v = 0; v < silt::Engine::numVoices; ++v)
    {
        const auto& voice = engine.getVoice (v);
        voicePlayheads[(size_t) v].store (voice.isActive() ? voice.getPlayheadPosition() : -1.0f, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* SiltProcessor::createEditor()
{
    return new SiltEditor (*this);
}

// --- message thread -----------------------------------------------------------

void SiltProcessor::timerCallback()
{
    engine.collectGarbage();

    const auto state = captureState.load (std::memory_order_acquire);

    if (state == CaptureState::done)
    {
        captureTarget.store (nullptr, std::memory_order_release);
        auto captured = std::move (captureOwned);

        juce::AudioBuffer<float> audio (2, captured->getNumFrames());
        for (int ch = 0; ch < 2; ++ch)
            audio.copyFrom (ch, 0, captured->getReadPointer (ch), captured->getNumFrames());

        installSample (audio, captured->getSampleRate(),
                       "CAPTURE " + juce::Time::getCurrentTime().formatted ("%H:%M:%S"));
        captureState.store (CaptureState::idle, std::memory_order_release);
    }
    else if (state == CaptureState::cancelled)
    {
        captureTarget.store (nullptr, std::memory_order_release);
        captureOwned.reset();
        captureState.store (CaptureState::idle, std::memory_order_release);
    }
}

void SiltProcessor::armCapture()
{
    if (captureState.load() != CaptureState::idle)
        return;

    const int bars = captureBars[juce::jlimit (0, 3, (int) raw->captureBars->load())];
    const double barQuarters = timeSigNumerator.load() * 4.0 / juce::jmax (1, timeSigDenominator.load());
    const double bpm = juce::jlimit (20.0, 400.0, hostBpm.load());
    const double seconds = juce::jmin (maxCaptureSeconds, bars * barQuarters * 60.0 / bpm);

    captureOwned = std::make_unique<silt::SampleBuffer> ((int) (seconds * currentSampleRate), currentSampleRate);
    captureProgress.store (0.0f);
    captureTarget.store (captureOwned.get(), std::memory_order_release);
    captureState.store (CaptureState::armed, std::memory_order_release);
}

void SiltProcessor::cancelCapture()
{
    const auto state = captureState.load();
    if (state == CaptureState::armed || state == CaptureState::recording)
        captureState.store (CaptureState::cancelRequested, std::memory_order_release);
}

bool SiltProcessor::isAudioFile (const juce::File& file) const
{
    return formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

std::unique_ptr<juce::AudioBuffer<float>> SiltProcessor::decode (juce::AudioFormatReader& reader) const
{
    // D9: samples hold up to 30 s.
    const int frames = (int) juce::jmin (reader.lengthInSamples, (juce::int64) (maxCaptureSeconds * reader.sampleRate));
    auto audio = std::make_unique<juce::AudioBuffer<float>> ((int) juce::jmax (1u, reader.numChannels), juce::jmax (1, frames));
    reader.read (audio.get(), 0, frames, 0, true, true);
    return audio;
}

void SiltProcessor::loadFile (const juce::File& file)
{
    juce::WeakReference<SiltProcessor> weak (this);

    loader.addJob ([this, weak, file]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            juce::MessageManager::callAsync ([weak, file]
            {
                if (weak != nullptr)
                    weak->lastLoadError = "CAN'T READ " + file.getFileName().toUpperCase();
            });
            return;
        }

        std::shared_ptr<juce::AudioBuffer<float>> audio (decode (*reader));
        const double rate = reader->sampleRate;

        juce::MessageManager::callAsync ([weak, audio, rate, file]
        {
            if (weak != nullptr)
            {
                weak->lastLoadError.clear();
                weak->installSample (*audio, rate, file.getFileNameWithoutExtension());
            }
        });
    });
}

void SiltProcessor::installSample (const juce::AudioBuffer<float>& audio, double rate, const juce::String& name)
{
    const int frames = audio.getNumSamples();
    auto buffer = std::make_unique<silt::SampleBuffer> (frames, rate);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::copy (buffer->getWritePointer (ch),
                                           audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1)), frames);
    {
        const juce::ScopedLock sl (sampleLock);
        sampleCopy.setSize (2, frames);
        for (int ch = 0; ch < 2; ++ch)
            sampleCopy.copyFrom (ch, 0, buffer->getReadPointer (ch), frames);
        sampleCopyRate = rate;
        sampleName = name;
        encodedSample.reset();
    }

    engine.setBuffer (std::move (buffer));
    ++sampleGeneration;
}

int SiltProcessor::readGrainViews (std::array<silt::GrainView, maxGrainViews>& out) const
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = grainViewSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        const int n = juce::jlimit (0, maxGrainViews, grainViewCount.load (std::memory_order_relaxed));
        std::copy (grainViews.begin(), grainViews.begin() + n, out.begin());
        std::atomic_thread_fence (std::memory_order_acquire);
        if (grainViewSequence.load (std::memory_order_relaxed) == before)
            return n;
    }
    return 0;   // the audio thread kept updating; the next frame will catch it
}

SiltProcessor::Overview SiltProcessor::getOverview() const
{
    const juce::ScopedLock sl (sampleLock);
    Overview o;
    const int frames = sampleCopy.getNumSamples();
    if (frames == 0)
        return o;

    o.seconds = frames / sampleCopyRate;
    o.name = sampleName;
    o.minimum.resize (overviewColumns);
    o.maximum.resize (overviewColumns);

    for (int c = 0; c < overviewColumns; ++c)
    {
        const int from = (int) ((juce::int64) c * frames / overviewColumns);
        const int to = juce::jmax (from + 1, (int) ((juce::int64) (c + 1) * frames / overviewColumns));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (sampleCopy.getReadPointer (ch, from), to - from);
            lo = juce::jmin (lo, range.getStart());
            hi = juce::jmax (hi, range.getEnd());
        }
        o.minimum[(size_t) c] = lo;
        o.maximum[(size_t) c] = hi;
    }
    return o;
}

// State: magic, version, the parameter XML, then the sample as FLAC (plan D10).
void SiltProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);

    juce::MemoryBlock flac;
    {
        const juce::ScopedLock sl (sampleLock);
        if (sampleCopy.getNumSamples() > 0)
        {
            if (encodedSample.isEmpty())
            {
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (encodedSample, false);
                if (auto writer = juce::FlacAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                                        .withSampleRate (sampleCopyRate)
                                                                                        .withNumChannels (2)
                                                                                        .withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (sampleCopy, 0, sampleCopy.getNumSamples());
            }
            flac = encodedSample;
            state.setProperty ("sampleName", sampleName, nullptr);
        }
    }

    juce::MemoryBlock xmlBlock;
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, xmlBlock);

    juce::MemoryOutputStream out (destData, false);
    out.writeInt (stateMagic);
    out.writeInt (stateVersion);
    out.writeInt64 ((juce::int64) xmlBlock.getSize());
    out.write (xmlBlock.getData(), xmlBlock.getSize());
    out.writeInt64 ((juce::int64) flac.getSize());
    out.write (flac.getData(), flac.getSize());
}

void SiltProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    std::unique_ptr<juce::XmlElement> xml;
    juce::MemoryBlock flac;

    if (sizeInBytes > 8 && in.readInt() == stateMagic)
    {
        in.readInt();   // version
        juce::MemoryBlock xmlBlock;
        in.readIntoMemoryBlock (xmlBlock, (juce::pointer_sized_int) in.readInt64());
        xml = getXmlFromBinary (xmlBlock.getData(), (int) xmlBlock.getSize());
        in.readIntoMemoryBlock (flac, (juce::pointer_sized_int) in.readInt64());
    }
    else
    {
        xml = getXmlFromBinary (data, sizeInBytes);   // M0 states: parameters only
    }

    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto name = tree.getProperty ("sampleName").toString();
    params.replaceState (tree);

    if (flac.isEmpty())
        return;

    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (flac, true), true));
    if (reader == nullptr)
        return;

    const auto audio = decode (*reader);
    installSample (*audio, reader->sampleRate, name);

    const juce::ScopedLock sl (sampleLock);
    encodedSample = flac;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SiltProcessor();
}
