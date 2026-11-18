#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SiltSamples.h"

namespace
{
// The factory samples (resources/samples), with the note each plays at as
// recorded: ROOT is set to it when one loads.
struct FactorySample
{
    const char* name;
    const char* data;
    int size;
    int root;
};

const std::array<FactorySample, 4>& factorySamples()
{
    static const std::array<FactorySample, 4> list { {
        { "Ahhh Progression", SiltSamples::ahhhprogression_flac, SiltSamples::ahhhprogression_flacSize, 64 },   // E minor
        { "Keys Drone", SiltSamples::keysdrone_flac, SiltSamples::keysdrone_flacSize, 55 },                     // G
        { "Rain Bells", SiltSamples::rainbells_flac, SiltSamples::rainbells_flacSize, 63 },                     // D sharp
        { "Guitar Harmonics", SiltSamples::guitarharmonics_flac, SiltSamples::guitarharmonics_flacSize, 59 },   // B minor
    } };
    return list;
}

} // namespace

namespace
{
using Set = juce::AudioChannelSet;
using Float = juce::AudioParameterFloat;
using Range = juce::NormalisableRange<float>;

constexpr int stateMagic = 0x544c4953;   // "SILT"

// The host's parameters, from silt::params (the JUCE-free table the browser
// version reads too), in its order. The ranges are JUCE's own, built from the
// table's numbers; silt-golden checks the two agree to the bit.
juce::AudioProcessorValueTreeState::ParameterLayout makeParameters()
{
    namespace sp = silt::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int id = 0; id < sp::count; ++id)
    {
        if (! sp::inBuild (id, SiltProcessor::isInstrument))
            continue;
        const auto& p = sp::info (id);
        const juce::ParameterID pid { p.id, 1 };
        const auto name = juce::String::fromUTF8 (p.name);
        const float def = sp::defaultFor (id, SiltProcessor::isInstrument);
        const auto text = [id] (float v, int) { return juce::String::fromUTF8 (sp::text (id, v).c_str()); };

        switch (p.kind)
        {
            case sp::Kind::real:
            {
                Range range;
                switch (p.mapping())
                {
                    case sp::Mapping::linear:
                        range = Range (p.low, p.high, p.interval);
                        break;
                    case sp::Mapping::skewed:
                        range = Range (p.low, p.high);
                        range.setSkewForCentre (p.centre);
                        break;
                    case sp::Mapping::cubicRate:
                        // Through zero: −50…+50 Hz on a cubic (most of the travel at slow rates,
                        // smooth through 0, so modulation can sweep across it). Below zero
                        // a WAVE runs backwards; near 0 it holds (STOP).
                        range = Range (p.low, p.high,
                                       [] (float, float, float x) { const float u = 2.0f * x - 1.0f; return 50.0f * u * u * u; },
                                       [] (float, float, float v) { return 0.5f * (std::cbrt (juce::jlimit (-50.0f, 50.0f, v) / 50.0f) + 1.0f); },
                                       [] (float, float, float v) { return std::abs (v) < 0.01f ? 0.0f : v; });
                        break;
                }
                layout.add (std::make_unique<Float> (pid, name, range, def,
                                                     juce::AudioParameterFloatAttributes().withStringFromValueFunction (text)));
                break;
            }
            case sp::Kind::integer:
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, name, (int) p.low, (int) p.high, (int) def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([text] (int n, int length) { return text ((float) n, length); })));
                break;
            case sp::Kind::boolean:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, name, def > 0.5f));
                break;
            case sp::Kind::choice:
            {
                juce::StringArray choices;
                for (const auto* c : p.choices)
                    choices.add (juce::String::fromUTF8 (c));
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, name, choices, (int) def));
                break;
            }
        }
    }
    return layout;
}

bool isMonoOrStereo (const Set& set) { return set == Set::mono() || set == Set::stereo(); }

} // namespace

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
      instrument (isInstrument),
      params (*this, nullptr, "SILT", makeParameters())
{
    // The instrument reads every parameter straight from the host's values; the
    // targets' JUCE parameters are kept for their text and the editor.
    namespace sp = silt::params;
    for (int id = 0; id < sp::count; ++id)
        if (sp::inBuild (id, isInstrument))
            instrument.bindParameter (id, params.getRawParameterValue (sp::info (id).id));
    instrument.syncModulated();
    for (int t = 0; t < instrument.numTargets(); ++t)
        targetParameters.push_back (dynamic_cast<juce::RangedAudioParameter*> (params.getParameter (sp::info (instrument.targetParameter (t)).id)));

    for (auto* parameter : getParameters())
        parameter->addListener (this);
    sampleGain = params.getRawParameterValue ("sampleGain");
    captureFeedRaw = params.getRawParameterValue ("captureFeed");   // looked up once: a name lookup can allocate

    formats.registerBasicFormats();
    for (size_t i = 0; i < knobCCs.size(); ++i)
        knobCCs[i].store (70 + (int) i);   // a common default for 8-knob controllers
    lastKnobValue.fill (-1);
    startTimerHz (30);

    // A new instance opens with the first factory sample, unless the host
    // restores a state (or a sample arrives) before it's ready. Silt FX has
    // no sample: it only grains its track.
    if constexpr (isInstrument)
        loadInBackground ([] { return openFactorySample (0); }, factorySampleName (0), 0, factorySamples()[0].root, true);
}

SiltProcessor::~SiltProcessor()
{
    for (auto* parameter : getParameters())
        parameter->removeListener (this);
    stopTimer();
    loader.removeAllJobs (true, 5000);
}

void SiltProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    instrument.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    midiEvents.reserve (2048);
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

    silt::Transport transport;
    if (position)
    {
        transport.known = true;
        transport.playing = position->getIsPlaying();
        transport.ppq = position->getPpqPosition().orFallback (0.0);
        if (const auto bpm = position->getBpm())
        {
            transport.hasBpm = true;
            transport.bpm = *bpm;
        }
        if (const auto sig = position->getTimeSignature())
        {
            transport.hasSignature = true;
            transport.numerator = sig->numerator;
            transport.denominator = sig->denominator;
        }
    }

    // MIDI goes to the instrument as it came. Controllers also move the 8-knob
    // controller's slots and LEARN, all but the pedal and all-notes-off.
    midiEvents.clear();
    for (const auto event : midi)
    {
        silt::MidiEvent e;
        e.offset = event.samplePosition;
        for (int i = 0; i < juce::jmin (3, event.numBytes); ++i)
            e.bytes[i] = event.data[i];
        if (midiEvents.size() < midiEvents.capacity())
            midiEvents.push_back (e);
        if (event.numBytes >= 3 && (e.bytes[0] & 0xf0) == 0xb0 && e.bytes[1] != 64 && e.bytes[1] != 120 && e.bytes[1] != 123)
            handleController (e.bytes[1], e.bytes[2]);
    }

    auto source = CaptureSource::none;
    auto capture = findCaptureBus (buffer, source);
    captureSource.store (source, std::memory_order_relaxed);
    if (capture.getNumChannels() > 0)
        captureMeter.process (capture.getArrayOfReadPointers(), capture.getNumChannels(), numSamples);
    const bool resampling = captureFeedRaw->load (std::memory_order_relaxed) > 0.5f;
    if (! resampling)
        runCapture (capture, position ? &*position : nullptr);

    auto output = getBusBuffer (buffer, false, 0);
    instrument.process (output.getWritePointer (0), output.getNumChannels() > 1 ? output.getWritePointer (1) : nullptr, numSamples,
                        capture.getNumChannels() > 0 ? capture.getArrayOfReadPointers() : nullptr, capture.getNumChannels(),
                        midiEvents.data(), (int) midiEvents.size(), transport);

    // RESAMPLE: the capture takes what Silt just played, sample for sample with
    // the block's position, so it still starts on the bar.
    if (resampling)
        runCapture (output, position ? &*position : nullptr);

    // The Bench's bars: the same, kept apart from the sample and sent on.
    if (output.getNumChannels() > 0)
    {
        const bool playing = position && position->getIsPlaying();
        const double blockPpq = position ? position->getPpqPosition().orFallback (0.0) : 0.0;
        const double bpm = position ? position->getBpm().orFallback (hostBpm.load (std::memory_order_relaxed))
                                    : hostBpm.load (std::memory_order_relaxed);
        const float quarters = (float) timeSigNumerator.load (std::memory_order_relaxed) * 4.0f / (float) juce::jmax (1, timeSigDenominator.load (std::memory_order_relaxed));
        outputRecorder.process (output.getReadPointer (0), output.getReadPointer (output.getNumChannels() > 1 ? 1 : 0), numSamples, playing, blockPpq,
                                position ? position->getPpqPositionOfLastBarStart().orFallback (0.0) : 0.0, quarters, bpm);
    }
}

bool SiltProcessor::routeCanWork (int modulator, int target) const noexcept { return instrument.routeCanWork (modulator, target); }
silt::ModulatorParams SiltProcessor::readModulatorParams (int m) const noexcept { return instrument.readModulatorParams (m); }

int SiltProcessor::modulationTarget (const juce::String& id) const
{
    return instrument.targetFor (silt::params::find (id.toStdString()));
}

float SiltProcessor::routeAmount (int modulator, int target) const noexcept { return instrument.routeAmount (modulator, target); }
void SiltProcessor::setRouteAmount (int modulator, int target, float amount) noexcept { instrument.setRouteAmount (modulator, target, amount); }
void SiltProcessor::setRoutePolarity (int modulator, int target, int polarity) noexcept { instrument.setRoutePolarity (modulator, target, polarity); }
int SiltProcessor::routePolarityOf (int modulator, int target) const noexcept { return instrument.routePolarityOf (modulator, target); }
bool SiltProcessor::routeIsBipolar (int modulator, int target) const noexcept { return instrument.routeIsBipolar (modulator, target); }

// "modulator:parameter:amount[:b|u]" separated by ';' (modulator 0–3; the polarity
// only when it's been set).
juce::String SiltProcessor::routesToString() const
{
    juce::StringArray routes;
    for (const auto& r : allRoutes())
    {
        const int polarity = routePolarityOf (r.modulator, r.target);
        routes.add (juce::String (r.modulator) + ":" + targetParameters[(size_t) r.target]->getParameterID() + ":" + juce::String (r.amount, 4)
                    + (polarity == bipolar ? ":b" : polarity == unipolar ? ":u" : ""));
    }
    return routes.joinIntoString (";");
}

void SiltProcessor::routesFromString (const juce::String& text)
{
    instrument.clearAllRoutes();
    for (const auto& route : juce::StringArray::fromTokens (text, ";", ""))
    {
        const auto parts = juce::StringArray::fromTokens (route, ":", "");
        if (parts.size() < 3)
            continue;
        const int m = parts[0].getIntValue(), t = modulationTarget (parts[1]);
        setRouteAmount (m, t, parts[2].getFloatValue());
        if (parts.size() > 3)
            setRoutePolarity (m, t, parts[3] == "b" ? bipolar : parts[3] == "u" ? unipolar : natural);
    }
}

void SiltProcessor::clearRoutesOnTarget (int target) noexcept
{
    for (int m = 0; m < numModulators; ++m)
        setRouteAmount (m, target, 0.0f);
}

void SiltProcessor::clearRoutesForModulator (int modulator) noexcept
{
    for (int t = 0; t < numTargets(); ++t)
        setRouteAmount (modulator, t, 0.0f);
}

int SiltProcessor::routeCount (int modulator) const noexcept
{
    int n = 0;
    for (int t = 0; t < numTargets(); ++t)
        n += routeAmount (modulator, t) != 0.0f ? 1 : 0;
    return n;
}

std::vector<SiltProcessor::Route> SiltProcessor::allRoutes() const
{
    std::vector<Route> routes;
    for (int t = 0; t < numTargets(); ++t)
        for (int m = 0; m < numModulators; ++m)
            if (const float amount = routeAmount (m, t); amount != 0.0f)
                routes.push_back ({ m, t, amount });
    return routes;
}

bool SiltProcessor::isSteppedTarget (int target) const
{
    return dynamic_cast<juce::AudioParameterInt*> (targetParameters[(size_t) target]) != nullptr
        || dynamic_cast<juce::AudioParameterChoice*> (targetParameters[(size_t) target]) != nullptr;   // the divisions
}

bool SiltProcessor::isModulatorEnabled (int modulator) const noexcept
{
    return instrument.isModulatorEnabled (modulator);
}

// What a route moves its parameter over, in the parameter's own units (both
// ways from the knob for bipolar sources): the readout that means the same for
// cutoff and for mix, which a percentage doesn't.
juce::String SiltProcessor::depthSpan (int modulator, int target) const
{
    const float amount = routeAmount (modulator, target);
    auto& p = *targetParameters[(size_t) target];
    const float base = p.getValue();
    const bool bipolar = routeIsBipolar (modulator, target);
    const float lo = juce::jlimit (0.0f, 1.0f, base + (bipolar ? -std::abs (amount) : juce::jmin (0.0f, amount)));
    const float hi = juce::jlimit (0.0f, 1.0f, base + (bipolar ? std::abs (amount) : juce::jmax (0.0f, amount)));
    return p.getText (lo, 0) + " to " + p.getText (hi, 0);
}

// The span, then the depth, signed like an Elektron LFO's: + moves the value
// up first, − down first (bipolar routes then swing back past the knob).
juce::String SiltProcessor::depthText (int modulator, int target) const
{
    const int percent = juce::roundToInt (routeAmount (modulator, target) * 100.0f);
    return depthSpan (modulator, target) + "  (" + (percent > 0 ? "+" : "") + juce::String (percent) + "%)";
}

// --- route undo ---------------------------------------------------------------------

// One undo step: every route before and after. Steps in one gesture merge, so
// the first "before" and the last "after" are kept.
class SiltProcessor::RoutesChange final : public juce::UndoableAction
{
public:
    RoutesChange (SiltProcessor& p, std::vector<float> b, std::vector<float> a)
        : processor (p), before (std::move (b)), after (std::move (a)) {}

    bool perform() override { processor.restoreRoutes (after); return true; }
    bool undo() override { processor.restoreRoutes (before); return true; }
    int getSizeInUnits() override { return 1; }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* change = dynamic_cast<RoutesChange*> (next))
            return new RoutesChange (processor, before, change->after);
        return nullptr;
    }

private:
    SiltProcessor& processor;
    std::vector<float> before, after;
};

std::vector<float> SiltProcessor::routeSnapshot() const
{
    // Amount and polarity per route.
    std::vector<float> s ((size_t) (2 * numModulators * numTargets()));
    for (int m = 0; m < numModulators; ++m)
        for (int t = 0; t < numTargets(); ++t)
        {
            s[(size_t) (2 * (m * numTargets() + t))] = routeAmount (m, t);
            s[(size_t) (2 * (m * numTargets() + t) + 1)] = (float) routePolarityOf (m, t);
        }
    return s;
}

void SiltProcessor::restoreRoutes (const std::vector<float>& s) noexcept
{
    for (int m = 0; m < numModulators; ++m)
        for (int t = 0; t < numTargets(); ++t)
        {
            setRouteAmount (m, t, s[(size_t) (2 * (m * numTargets() + t))]);
            setRoutePolarity (m, t, (int) s[(size_t) (2 * (m * numTargets() + t) + 1)]);
        }
}

// One parameter's value before and after a gesture. Gestures on the same
// parameter close together (a controller knob sends one per message) merge.
class SiltProcessor::ParameterChange final : public juce::UndoableAction
{
public:
    ParameterChange (SiltProcessor& p, int i, float b, float a) : processor (p), index (i), before (b), after (a) {}

    bool perform() override { set (after); return true; }
    bool undo() override { set (before); return true; }
    int getSizeInUnits() override { return 1; }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* change = dynamic_cast<ParameterChange*> (next); change != nullptr && change->index == index)
            return new ParameterChange (processor, index, before, change->after);
        return nullptr;
    }

private:
    void set (float value)
    {
        auto* parameter = processor.getParameters()[index];
        const juce::ScopedValueSetter<bool> quiet (processor.replaying, true);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (value);
        parameter->endChangeGesture();
    }

    SiltProcessor& processor;
    int index;
    float before, after;
};

// Gestures from the panel, the controller and the mod rail: a value at the
// start, a step at the end if it changed. Only on the message thread; never
// while undo, redo or a preset is setting values.
void SiltProcessor::parameterGestureChanged (int index, bool starting)
{
    if (replaying || ! juce::MessageManager::existsAndIsCurrentThread())
        return;
    auto* parameter = getParameters()[index];
    if (starting)
    {
        gestureStart[index] = parameter->getValue();
        return;
    }
    const auto it = gestureStart.find (index);
    if (it == gestureStart.end())
        return;
    const float before = it->second, after = parameter->getValue();
    gestureStart.erase (it);
    if (juce::exactlyEqual (before, after))
        return;

    const auto now = juce::Time::getMillisecondCounter();
    if (index != lastGestureParameter || now - lastGestureTime > 800)
        routeUndo.beginNewTransaction (parameter->getName (24).toUpperCase());
    lastGestureParameter = index;
    lastGestureTime = now;
    const juce::ScopedValueSetter<bool> quiet (replaying, true);   // perform() sets the value it already has
    routeUndo.perform (new ParameterChange (*this, index, before, after));
}

std::vector<const silt::layout::Entry*> SiltProcessor::randomisable (int page) const
{
    using silt::layout::Kind;
    const auto context = layoutContext();
    std::vector<const silt::layout::Entry*> out;
    for (const auto& e : silt::layout::entries (isInstrument))
        if (e.page == page && (e.kind == Kind::knob || e.kind == Kind::menu || e.kind == Kind::button)
            && ! e.id.startsWith ("!") && e.id != "inputGain" && e.id != "mix" && ! e.alwaysShown && e.isShown (context) && params.getParameter (e.id) != nullptr)
            out.push_back (&e);   // not INPUT or MIX: how the track comes in, not part of a grain's sound
    return out;
}

juce::StringArray SiltProcessor::pageParameters (int page, bool everyModulator) const
{
    juce::StringArray ids;
    if (page == modPage)
    {
        // A modulator's whole set (not only the controls its type shows).
        for (int m = 0; m < numModulators; ++m)
            if (everyModulator || m == modShown.load())
                for (auto* parameter : getParameters())
                    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (parameter);
                        p != nullptr && p->getParameterID().startsWith ("m" + juce::String (m + 1)))
                        ids.addIfNotAlreadyThere (p->getParameterID());
        return ids;
    }
    for (const auto& e : silt::layout::entries (isInstrument))
        if (e.page == page && ! e.id.startsWith ("!") && e.id != "root" && params.getParameter (e.id) != nullptr)
            ids.addIfNotAlreadyThere (e.id);
    return ids;
}

namespace
{
int resetToDefaults (juce::AudioProcessorValueTreeState& params, const juce::StringArray& ids,
                     const std::function<void (juce::RangedAudioParameter&, float before)>& record)
{
    int changed = 0;
    for (const auto& id : ids)
        if (auto* parameter = params.getParameter (id))
        {
            const float before = parameter->getValue();
            if (juce::exactlyEqual (before, parameter->getDefaultValue()))
                continue;
            record (*parameter, before);
            ++changed;
        }
    return changed;
}
} // namespace

int SiltProcessor::resetPage (int page)
{
    routeUndo.beginNewTransaction ("RESET " + juce::String (silt::layout::pageName (page)));
    lastGestureParameter = -1;
    const int changed = resetToDefaults (params, pageParameters (page), [this] (juce::RangedAudioParameter& p, float before)
    {
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        routeUndo.perform (new ParameterChange (*this, p.getParameterIndex(), before, p.getDefaultValue()));
    });
    lastGestureParameter = -1;
    return changed;
}

int SiltProcessor::resetAllPages()
{
    routeUndo.beginNewTransaction ("RESET ALL");
    lastGestureParameter = -1;
    juce::StringArray ids;
    for (int page = 0; page < numPages; ++page)
        ids.addArray (pageParameters (page, true));
    ids.removeDuplicates (false);
    const int changed = resetToDefaults (params, ids, [this] (juce::RangedAudioParameter& p, float before)
    {
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        routeUndo.perform (new ParameterChange (*this, p.getParameterIndex(), before, p.getDefaultValue()));
    });
    lastGestureParameter = -1;
    return changed;
}

namespace
{
struct CharacterLimit { const char* id; float low, high; };

// What any CHARACTER leaves exactly where it is: everything that sets or shifts tuning.
bool setsTuning (const juce::String& id)
{
    static const juce::StringArray ids { "pitch", "randPitch", "scale", "key", "root", "keyMode", "patternDepth", "patternSteps",
                                         "resTune", "resShift", "resNotes", "resFollow", "resSpread", "resDrift", "colorWow",
                                         "resStretch", "resCurve", "resSnap", "resOffset" };
    return ids.contains (id);
}

// What RANDOM keeps to, in each parameter's own units. Anything unlisted roams as usual.
const CharacterLimit* characterLimit (int character, const juce::String& id)
{
    static const std::vector<CharacterLimit> limits[] {
        {},
        // TONAL: stays in tune, long enough to hear a pitch.
        { { "reverse", 0.0f, 10.0f }, { "spray", 0.0f, 25.0f },
          { "size", 80.0f, 800.0f }, { "rate", 10.0f, 50.0f }, { "jitter", 20.0f, 100.0f },
          { "fade", 50.0f, 100.0f }, { "colorBits", 12.0f, 16.0f } },
        // RHYTHMIC: short, regular grains that stay put.
        { { "size", 20.0f, 180.0f }, { "rate", 4.0f, 30.0f },
          { "jitter", 0.0f, 10.0f }, { "spray", 0.0f, 8.0f }, { "fade", 20.0f, 70.0f }, { "reverse", 0.0f, 30.0f } },
        // TEXTURE: dense, scattered and wide.
        { { "size", 30.0f, 500.0f }, { "rate", 15.0f, 80.0f },
          { "spray", 30.0f, 100.0f }, { "jitter", 50.0f, 100.0f }, { "randPan", 40.0f, 100.0f }, { "randAmp", 20.0f, 80.0f },
          { "fade", 60.0f, 100.0f } },
    };
    if (character <= 0 || character >= (int) std::size (limits))
        return nullptr;
    for (const auto& limit : limits[character])
        if (id == limit.id)
            return &limit;
    return nullptr;
}
} // namespace

int SiltProcessor::randomisePage (int page)
{
    const float amount = juce::jlimit (0.0f, 1.0f, randomAmount.load());
    routeUndo.beginNewTransaction ("RANDOM " + juce::String (silt::layout::pageName (page)));
    ++randomPresses;
    lastGestureParameter = -1;
    int changed = 0;
    for (const auto* e : randomisable (page))
    {
        if (isRandomExcluded (e->id))
            continue;
        auto* parameter = params.getParameter (e->id);
        const float before = parameter->getValue();
        float after = before;
        if (e->kind == silt::layout::Kind::knob)
            after = before + (randomiser.nextFloat() - before) * amount;
        else if (randomiser.nextFloat() < amount)
        {
            const int steps = juce::jmax (2, parameter->getNumSteps());
            after = (float) randomiser.nextInt (steps) / (float) (steps - 1);
        }
        // A CHARACTER keeps what it names within reason, and never touches tuning.
        if (randomCharacter.load() > 0 && setsTuning (e->id))
            continue;
        if (const auto* limit = characterLimit (randomCharacter.load(), e->id))
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            after = juce::jlimit (ranged->convertTo0to1 (limit->low), ranged->convertTo0to1 (limit->high), after);
        }
        after = parameter->convertTo0to1 (parameter->convertFrom0to1 (after));   // onto the parameter's steps
        if (juce::exactlyEqual (before, after))
            continue;
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        routeUndo.perform (new ParameterChange (*this, parameter->getParameterIndex(), before, after));
        ++changed;
    }
    lastGestureParameter = -1;   // the next change starts its own step
    return changed;
}

void SiltProcessor::setRandomExcluded (const juce::String& id, bool excluded)
{
    if (excluded)
        randomExcluded.addIfNotAlreadyThere (id);
    else
        randomExcluded.removeString (id);
}

bool SiltProcessor::undoing (const std::function<bool()>& step)
{
    lastGestureParameter = -1;   // the next change starts its own step
    return step();
}

// A route removed earlier starts natural when it's made again. Only here, at a
// gesture's start: a depth dragged through 0 keeps its polarity.
void SiltProcessor::beginRouteGesture()
{
    instrument.naturalWhereUnrouted();
    routeUndo.beginNewTransaction ("MODULATION");
}

void SiltProcessor::editRoutes (const std::function<void()>& edit)
{
    auto before = routeSnapshot();
    edit();
    auto after = routeSnapshot();
    if (before != after)
        routeUndo.perform (new RoutesChange (*this, std::move (before), std::move (after)));
}

float SiltProcessor::modulatedValue01 (int target) const noexcept { return instrument.modulatedValue01 (target); }
bool SiltProcessor::isVoiceTarget (int target) const noexcept { return instrument.isVoiceTarget (target); }
bool SiltProcessor::isModulatorPerVoice (int m) const noexcept { return instrument.isModulatorPerVoice (m); }

// --- presets ---------------------------------------------------------------------------

namespace
{
// Session and sample settings (ROOT is the sample's key, INPUT the track's level), not part of a sound.
bool inPresets (const juce::String& id)
{
    return id != "monitor" && id != "captureBars" && id != "captureFeed" && id != "sampleGain" && id != "inputGain" && id != "root";
}
} // namespace

std::unique_ptr<juce::XmlElement> SiltProcessor::capturePreset (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> ("SiltPreset");
    xml->setAttribute ("name", name);
    xml->setAttribute ("version", 1);
    auto* values = xml->createNewChildElement ("PARAMS");
    for (auto* parameter : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (parameter); p != nullptr && inPresets (p->getParameterID()))
            values->setAttribute (p->getParameterID(), p->convertFrom0to1 (p->getValue()));
    xml->setAttribute ("routes", routesToString());
    xml->setAttribute ("modShown", modShown.load());
    return xml;
}

// A preset load as one undo step: the sound before it and the preset.
class SiltProcessor::PresetChange final : public juce::UndoableAction
{
public:
    PresetChange (SiltProcessor& p, std::unique_ptr<juce::XmlElement> b, juce::String bName, std::unique_ptr<juce::XmlElement> a, juce::String aName)
        : processor (p), before (std::move (b)), after (std::move (a)), beforeName (std::move (bName)), afterName (std::move (aName)) {}
    bool perform() override
    {
        if (first)
            first = false;   // recorded after the load: nothing to do yet
        else
            processor.applyPresetValues (*after, afterName);
        return true;
    }
    bool undo() override { processor.applyPresetValues (*before, beforeName); return true; }
    int getSizeInUnits() override { return 4; }

private:
    SiltProcessor& processor;
    std::unique_ptr<juce::XmlElement> before, after;
    juce::String beforeName, afterName;
    bool first = true;
};

// Loading a preset: one undo step back to the sound before it.
void SiltProcessor::applyPreset (const juce::XmlElement& xml, const juce::String& name)
{
    auto before = capturePreset (presetName);
    const auto beforeName = presetName;
    applyPresetValues (xml, name);
    commitPresetChange (std::move (before), beforeName);
}

void SiltProcessor::commitPresetChange (std::unique_ptr<juce::XmlElement> before, const juce::String& beforeName)
{
    if (before == nullptr)
        return;
    routeUndo.beginNewTransaction ("PRESET " + presetName.toUpperCase());
    lastGestureParameter = -1;
    {
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        routeUndo.perform (new PresetChange (*this, std::move (before), beforeName, capturePreset (presetName), presetName));
    }
    lastGestureParameter = -1;
}

// Every parameter takes the preset's value, or its default if the preset
// doesn't list it (so presets are complete), as host-visible gestures. Routes
// are replaced.
void SiltProcessor::applyPresetValues (const juce::XmlElement& xml, const juce::String& name)
{
    const juce::ScopedValueSetter<bool> quiet (replaying, true);   // one load, not 150 undo steps
    const auto* values = xml.getChildByName ("PARAMS");
    for (auto* parameter : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (parameter); p != nullptr && inPresets (p->getParameterID()))
        {
            const auto id = p->getParameterID();
            const float normalised = values != nullptr && values->hasAttribute (id)
                                   ? p->convertTo0to1 ((float) values->getDoubleAttribute (id))
                                   : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }

    routesFromString (xml.getStringAttribute ("routes"));
    modShown.store (juce::jlimit (0, numModulators - 1, xml.getIntAttribute ("modShown", 0)));
    presetName = name;
    markPresetClean();
}

// The sound as a preset would keep it, without what's only about the view.
std::unique_ptr<juce::XmlElement> SiltProcessor::comparablePreset() const
{
    auto xml = capturePreset ({});
    xml->removeAttribute ("modShown");
    return xml;
}

void SiltProcessor::markPresetClean()
{
    presetAsLoaded = comparablePreset();
}

bool SiltProcessor::presetIsEdited()
{
    if (presetAsLoaded == nullptr || presetRestored.exchange (false))   // a set just opened: edited from here on
        markPresetClean();
    return ! comparablePreset()->isEquivalentTo (presetAsLoaded.get(), false);
}

// The effects chain out of a preset: its four blocks and their order, one undo step. The rest of
// the sound stays, under its own name, which now reads as edited.
void SiltProcessor::applyPresetEffects (const juce::XmlElement& xml)
{
    auto before = capturePreset (presetName);
    const auto keepName = presetName;
    {
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        const auto* values = xml.getChildByName ("PARAMS");
        for (auto* parameter : getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            {
                const auto id = p->getParameterID();
                if (! (id.startsWith ("res") || id.startsWith ("filter") || id.startsWith ("color") || id.startsWith ("space") || id == "chainOrder"))
                    continue;
                p->beginChangeGesture();
                p->setValueNotifyingHost (values != nullptr && values->hasAttribute (id) ? p->convertTo0to1 ((float) values->getDoubleAttribute (id)) : p->getDefaultValue());
                p->endChangeGesture();
            }
    }
    commitPresetChange (std::move (before), keepName);
}

juce::AudioProcessorEditor* SiltProcessor::createEditor()
{
    return new SiltEditor (*this);
}

// --- message thread -----------------------------------------------------------

// Audio thread: learn, page buttons, or queue a knob move.
void SiltProcessor::handleController (int number, int value) noexcept
{
    if (const int step = learnStep.load(); step >= 0)
    {
        // A knob sends many messages and a button sends press and release: only a
        // number not yet taken in this learn moves it on.
        for (int i = 0; i < juce::jmin (step, slotsPerPage); ++i)
            if (learnedThisRun[(size_t) i] == number)
                return;
        if (step == 9 && number == previousPageCC.load())
            return;

        if (step < slotsPerPage)
        {
            knobCCs[(size_t) step].store (number);
            learnedThisRun[(size_t) step] = number;
        }
        else if (step == 8)
            previousPageCC.store (number);
        else
            nextPageCC.store (number);
        learnStep.store (step < 9 ? step + 1 : -1);
        return;
    }

    if (number == previousPageCC.load() || number == nextPageCC.load())
    {
        const int which = number == nextPageCC.load() ? 1 : 0;
        if (value > 63 && pageButtonLast[(size_t) which] <= 63)
        {
            // RESONATOR's pages of controls are stops of their own.
            const int pageNow = currentPage.load(), half = resonatorHalf.load();
            if (pageNow == silt::layout::resonator && half < numResonatorHalves - 1 && which == 1)
                resonatorHalf.store (half + 1);
            else if (pageNow == silt::layout::resonator && half > 0 && which == 0)
                resonatorHalf.store (half - 1);
            else
            {
                const int to = (pageNow + (which == 1 ? 1 : numPages - 1)) % numPages;
                if (to == silt::layout::resonator)
                    resonatorHalf.store (which == 1 ? 0 : numResonatorHalves - 1);
                currentPage.store (to);
            }
        }
        pageButtonLast[(size_t) which] = value;
        return;
    }

    for (int slot = 0; slot < slotsPerPage; ++slot)
        if (knobCCs[(size_t) slot].load() == number)
        {
            int start1, size1, start2, size2;
            controllerFifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 > 0)
                controllerMoves[(size_t) start1] = { slot, value };
            controllerFifo.finishedWrite (size1);
            return;
        }
}

// Which parameter a page slot moves right now: the editor's page table, read
// with the current parameter values.
juce::String SiltProcessor::slotParameter (int page, int slot) const
{
    return silt::layout::slotParameter (isInstrument, page, slot, layoutContext());
}

silt::layout::Context SiltProcessor::layoutContext() const
{
    silt::layout::Context c;
    c.value = [this] (const juce::String& id)
    {
        if (id == "keyMode")
            return (float) playMode();   // Silt FX's pages always show LIVE's controls
        const auto* value = params.getRawParameterValue (id);
        jassert (value != nullptr);   // a typo in an id
        return value != nullptr ? value->load() : 0.0f;
    };
    c.modShown = modShown.load();
    c.resonatorHalf = resonatorHalf.load();
    c.delayForced = delayForced.load (std::memory_order_relaxed);
    return c;
}

double SiltProcessor::divisionQuarters (int index)
{
    return silt::params::quartersFor (index);
}

void SiltProcessor::newPattern()
{
    if (auto* seed = dynamic_cast<juce::RangedAudioParameter*> (params.getParameter ("patternSeed")))
        seed->setValueNotifyingHost (seed->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 10000 })));
}

// Message thread: applies queued knob moves with pickup.
void SiltProcessor::applyControllerMoves()
{
    const int page = currentPage.load();
    if (page != pickupPage)
    {
        pickupPage = page;
        pickedUp.fill (false);
        lastKnobValue.fill (-1);
    }

    int start1, size1, start2, size2;
    controllerFifo.prepareToRead (controllerFifo.getNumReady(), start1, size1, start2, size2);

    const auto apply = [this, page] (std::pair<int, int> move)
    {
        const auto [slot, value] = move;
        const auto id = slotParameter (page, slot);
        auto& last = lastKnobValue[(size_t) slot];
        if (id != slotTarget[(size_t) slot])
        {
            slotTarget[(size_t) slot] = id;   // a mode switched what this slot moves
            pickedUp[(size_t) slot] = false;
            last = -1;
        }
        if (id.isEmpty())
            return;

        // ASSIGN on and on another page: a knob sets this modulator's amount on
        // its slot's parameter, centre = none (no pickup needed: it's relative to nothing).
        if (const int m = armedMod.load(); m >= 0 && page != modPage && ! id.startsWith ("!"))
        {
            if (const int t = modulationTarget (id); t >= 0)
            {
                // One undo step per knob turn: a new step on another route or after a pause.
                const auto now = juce::Time::getMillisecondCounter();
                if (m * maxTargets + t != lastControllerRoute || now - lastControllerRouteTime > 1000)
                    beginRouteGesture();
                lastControllerRoute = m * maxTargets + t;
                lastControllerRouteTime = now;
                editRoutes ([&] { setRouteAmount (m, t, juce::jlimit (-1.0f, 1.0f, (float) (value - 64) / 63.0f)); });
            }
            last = value;
            return;
        }

        if (id == "!modSelect")   // a knob picks the modulator shown
        {
            modShown.store (juce::jlimit (0, numModulators - 1, value * numModulators / 128));
            last = value;
            return;
        }

        if (id == "!assign")   // a button: ASSIGN on or off for the shown modulator
        {
            if (value > 63 && last <= 63)
                armedMod.store (armedMod.load() >= 0 ? -1 : modShown.load());
            last = value;
            return;
        }

        if (id == "!newPattern")   // a button: new pattern on each press
        {
            if (value > 63 && last <= 63)
                newPattern();
            last = value;
            return;
        }

        auto* parameter = params.getParameter (id);
        if (parameter == nullptr)
            return;

        const float target = (float) value / 127.0f;
        if (! pickedUp[(size_t) slot])
        {
            // Take over once the knob reaches the value: close to it, or moved across it.
            const float current = parameter->getValue();
            const float previous = last >= 0 ? (float) last / 127.0f : target;
            pickedUp[(size_t) slot] = std::abs (target - current) < 0.04f || (previous - current) * (target - current) <= 0.0f;
            last = value;
            if (! pickedUp[(size_t) slot])
                return;
        }
        last = value;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (target);
        parameter->endChangeGesture();
    };

    for (int i = 0; i < size1; ++i)
        apply (controllerMoves[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        apply (controllerMoves[(size_t) (start2 + i)]);
    controllerFifo.finishedRead (size1 + size2);
}

void SiltProcessor::timerCallback()
{
    engine.collectGarbage();
    applyControllerMoves();

    const auto state = captureState.load (std::memory_order_acquire);

    if (state == CaptureState::done)
    {
        captureTarget.store (nullptr, std::memory_order_release);
        analyseThenInstall (std::move (captureOwned), (capturingFeed ? "RESAMPLE " : "CAPTURE ") + juce::Time::getCurrentTime().formatted ("%H:%M:%S"),
                            -1, -1.0f, true);
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

    const int bars = silt::params::captureBarCounts[juce::jlimit (0, 3, (int) instrument.value (silt::params::captureBars))];
    const double barQuarters = timeSigNumerator.load() * 4.0 / juce::jmax (1, timeSigDenominator.load());
    const double bpm = juce::jlimit (20.0, 400.0, hostBpm.load());
    const double seconds = juce::jmin (maxCaptureSeconds, bars * barQuarters * 60.0 / bpm);

    captureOwned = std::make_unique<silt::SampleBuffer> ((int) (seconds * currentSampleRate), currentSampleRate);
    capturingFeed = params.getRawParameterValue ("captureFeed")->load() > 0.5f;
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
    // D9: samples hold up to 30 s, and Silt plays two channels. A header
    // that can't be right (a corrupt file or project) gets nothing, rather
    // than a buffer sized by it.
    if (! (reader.sampleRate >= 1000.0 && reader.sampleRate <= 768000.0) || reader.numChannels == 0 || reader.lengthInSamples <= 0)
        return nullptr;
    const int frames = (int) juce::jmin (reader.lengthInSamples, (juce::int64) (maxCaptureSeconds * reader.sampleRate));
    auto audio = std::make_unique<juce::AudioBuffer<float>> ((int) juce::jmin (2u, reader.numChannels), frames);
    reader.read (audio.get(), 0, frames, 0, true, reader.numChannels > 1);
    return audio;
}

std::array<int, SiltProcessor::numModules> SiltProcessor::chainOrder (int index)
{
    return silt::Instrument::chainOrder (index);
}

int SiltProcessor::chainOrderIndex (const std::array<int, numModules>& order)
{
    for (int i = 0; i < numChainOrders; ++i)
        if (silt::Instrument::chainOrder (i) == order)
            return i;
    return 0;
}

juce::String SiltProcessor::moduleName (int module)
{
    static const char* names[] { "RESONATOR", "FILTER", "COLOR", "SPACE" };
    return names[juce::jlimit (0, numModules - 1, module)];
}

juce::String SiltProcessor::moduleOnId (int module)
{
    static const char* ids[] { "resOn", "filterOn", "colorOn", "spaceOn" };
    return ids[juce::jlimit (0, numModules - 1, module)];
}

juce::String SiltProcessor::chainOrderName (int index)
{
    juce::StringArray letters;
    for (const int m : chainOrder (index))
        letters.add (moduleName (m).substring (0, 1));
    return letters.joinIntoString (">");
}

unsigned SiltProcessor::readScope (float* in, float* out, int count) const
{
    return instrument.readScope (in, out, count);
}

void SiltProcessor::editSample (SampleEdit edit)
{
    juce::AudioBuffer<float> audio;
    double rate = 0.0;
    juce::String name;
    {
        const juce::ScopedLock sl (sampleLock);
        if (sampleCopy.getNumSamples() == 0)
            return;
        audio = sampleCopy;
        rate = sampleCopyRate;
        name = sampleName;
    }
    const int frames = audio.getNumSamples();
    switch (edit)
    {
        case SampleEdit::trimToRegion:
        {
            const auto [start, length] = shownRegion();
            const int from = juce::jlimit (0, frames - 1, juce::roundToInt (start * (float) frames));
            const int count = juce::jlimit (64, frames - from, juce::roundToInt (length * (float) frames));
            juce::AudioBuffer<float> kept (audio.getNumChannels(), count);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                kept.copyFrom (ch, 0, audio, ch, from, count);
            audio = std::move (kept);
            name += " TRIM";
            // The region was the whole point: now it's the whole sample.
            for (const auto& [id, value] : { std::pair<const char*, float> { "start", 0.0f }, { "length", 100.0f } })
                if (auto* p = params.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            break;
        }
        case SampleEdit::reverse:
            audio.reverse (0, frames);
            name += " REV";
            break;
        case SampleEdit::normalise:
        {
            const float peak = audio.getMagnitude (0, frames);
            if (peak < 1.0e-6f)
                return;
            audio.applyGain (juce::Decibels::decibelsToGain (-1.0f) / peak);
            name += " NORM";
            break;
        }
    }
    analyseThenInstall (makeBuffer (audio, rate), name, -1, -1.0f, true);
}

juce::String SiltProcessor::audioName() const
{
    if (liveMode.load())
        return juce::String (playMode() == modeFeed ? "FEED " : "LIVE ")
             + juce::Time::getCurrentTime().formatted ("%H-%M-%S");
    const juce::ScopedLock sl (sampleLock);
    return sampleName.isEmpty() ? juce::String ("SILT") : sampleName;
}

juce::File SiltProcessor::takesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Silt").getChildFile ("Takes");
}

bool SiltProcessor::writeAudioFile (const juce::File& file) const
{
    juce::AudioBuffer<float> audio;
    double rate = 0.0;
    if (liveMode.load())
    {
        // The ring, oldest first: from the write head round to just behind it.
        // It's still being written as we copy; at worst the newest few ms tear.
        const auto* ring = engine.getLiveBuffer();
        if (ring == nullptr)
            return false;
        const int frames = ring->getNumFrames(), head = juce::jlimit (0, frames - 1, engine.getLiveWritePosition());
        audio.setSize (2, frames);
        for (int ch = 0; ch < 2; ++ch)
        {
            audio.copyFrom (ch, 0, ring->getReadPointer (ch) + head, frames - head);
            audio.copyFrom (ch, frames - head, ring->getReadPointer (ch), head);
        }
        rate = ring->getSampleRate();
    }
    else
    {
        const juce::ScopedLock sl (sampleLock);
        if (sampleCopy.getNumSamples() == 0)
            return false;
        audio = sampleCopy;
        rate = sampleCopyRate;
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void SiltProcessor::loadFile (const juce::File& file)
{
    loadInBackground ([this, file] { return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)); },
                      file.getFileNameWithoutExtension(), -1, 60, false);
}

int SiltProcessor::numFactorySamples() { return (int) factorySamples().size(); }

juce::String SiltProcessor::factorySampleName (int index)
{
    return factorySamples()[(size_t) juce::jlimit (0, numFactorySamples() - 1, index)].name;
}

void SiltProcessor::loadFactorySample (int index)
{
    index = juce::jlimit (0, numFactorySamples() - 1, index);
    loadInBackground ([index] { return openFactorySample (index); }, factorySampleName (index), index, factorySamples()[(size_t) index].root, false);
}

int SiltProcessor::getFactorySample() const
{
    const juce::ScopedLock sl (sampleLock);
    return factorySample;
}

std::unique_ptr<juce::AudioFormatReader> SiltProcessor::openFactorySample (int index)
{
    const auto& f = factorySamples()[(size_t) index];
    return std::unique_ptr<juce::AudioFormatReader> (
        juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (f.data, (size_t) f.size, false), true));
}

void SiltProcessor::loadInBackground (std::function<std::unique_ptr<juce::AudioFormatReader>()> open, const juce::String& name,
                                      int factory, int rootNote, bool onlyIfUnchanged)
{
    juce::WeakReference<SiltProcessor> weak (this);
    const int generation = sampleGeneration.load();

    loader.addJob ([this, weak, open, name, factory, rootNote, onlyIfUnchanged, generation]
    {
        const auto reader = open();
        if (reader == nullptr)
        {
            juce::MessageManager::callAsync ([weak, name]
            {
                if (weak != nullptr)
                    weak->lastLoadError = "CAN'T READ " + name.toUpperCase();
            });
            return;
        }

        const auto audio = decode (*reader);
        const bool trimmed = audio != nullptr && reader->lengthInSamples > audio->getNumSamples();
        if (audio == nullptr)
        {
            juce::MessageManager::callAsync ([weak, name]
            {
                if (weak != nullptr)
                    weak->lastLoadError = "CAN'T READ " + name.toUpperCase();
            });
            return;
        }
        // Passed on by moving the buffer out of a shared holder: no copy.
        auto buffer = std::make_shared<std::unique_ptr<silt::SampleBuffer>> (makeBuffer (*audio, reader->sampleRate));
        (*buffer)->analyse();

        juce::MessageManager::callAsync ([weak, buffer, name, factory, rootNote, onlyIfUnchanged, generation, trimmed]
        {
            if (weak == nullptr || (onlyIfUnchanged && (weak->sampleGeneration.load() != generation || weak->stateRestored.load())))
                return;
            weak->lastLoadError.clear();
            weak->loadNote = trimmed ? "FIRST " + juce::String ((int) maxCaptureSeconds) + " S OF " + name.toUpperCase() : juce::String();
            auto before = onlyIfUnchanged ? nullptr : weak->snapshotSample();   // the opening sample isn't a step
            weak->installBuffer (std::move (*buffer), name, factory);
            if (auto* root = weak->params.getParameter ("root"))
                root->setValueNotifyingHost (root->convertTo0to1 ((float) rootNote));
            if (before != nullptr)
                weak->recordSampleChange (std::move (before));
        });
    });
}

std::unique_ptr<silt::SampleBuffer> SiltProcessor::makeBuffer (const juce::AudioBuffer<float>& audio, double rate)
{
    const int frames = audio.getNumSamples();
    auto buffer = std::make_unique<silt::SampleBuffer> (frames, rate);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::copy (buffer->getWritePointer (ch),
                                           audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1)), frames);
    return buffer;
}

// Analysis (zero crossings and transients, for SNAP) takes a moment on long
// samples, so it runs on the loader thread; the sample is installed after.
void SiltProcessor::analyseThenInstall (std::unique_ptr<silt::SampleBuffer> buffer, const juce::String& name, int factory,
                                        float rootNote, bool record)
{
    juce::WeakReference<SiltProcessor> weak (this);
    auto holder = std::make_shared<std::unique_ptr<silt::SampleBuffer>> (std::move (buffer));   // moved on, not copied
    loader.addJob ([weak, holder, name, factory, rootNote, record]
    {
        (*holder)->analyse();
        juce::MessageManager::callAsync ([weak, holder, name, factory, rootNote, record]
        {
            if (weak == nullptr)
                return;
            auto before = record ? weak->snapshotSample() : nullptr;
            weak->installBuffer (std::move (*holder), name, factory);
            if (auto* root = weak->params.getParameter ("root"); root != nullptr && rootNote >= 0.0f)
                root->setValueNotifyingHost (root->convertTo0to1 (rootNote));
            if (before != nullptr)
                weak->recordSampleChange (std::move (before));
        });
    });
}

// --- sample undo -------------------------------------------------------------------------

std::shared_ptr<const SiltProcessor::SampleSnapshot> SiltProcessor::snapshotSample() const
{
    auto s = std::make_shared<SampleSnapshot>();
    {
        const juce::ScopedLock sl (sampleLock);
        s->audio = sampleCopy;
        s->rate = sampleCopyRate;
        s->name = sampleName;
        s->factory = factorySample;
    }
    if (auto* root = params.getParameter ("root"))
        s->root = root->convertFrom0to1 (root->getValue());
    return s;
}

// Puts a remembered sample back (analysed on the loader thread, as a load is).
void SiltProcessor::restoreSample (const std::shared_ptr<const SampleSnapshot>& s)
{
    if (s == nullptr || s->audio.getNumSamples() == 0)
        return;
    analyseThenInstall (makeBuffer (s->audio, s->rate), s->name, s->factory, s->root, false);
}

class SiltProcessor::SampleChange final : public juce::UndoableAction
{
public:
    SampleChange (SiltProcessor& p, std::shared_ptr<const SampleSnapshot> b, std::shared_ptr<const SampleSnapshot> a)
        : processor (p), before (std::move (b)), after (std::move (a)) {}

    bool perform() override
    {
        if (first)
            first = false;   // recorded after the sample went in: nothing to do yet
        else
            processor.restoreSample (after);
        return true;
    }
    bool undo() override { processor.restoreSample (before); return true; }
    int getSizeInUnits() override
    {
        const auto bytes = [] (const std::shared_ptr<const SampleSnapshot>& s)
        { return s == nullptr ? 0 : (std::size_t) s->audio.getNumSamples() * (std::size_t) s->audio.getNumChannels() * sizeof (float); };
        return 1 + (int) ((bytes (before) + bytes (after)) / 65536);
    }

private:
    SiltProcessor& processor;
    std::shared_ptr<const SampleSnapshot> before, after;
    bool first = true;
};

void SiltProcessor::recordSampleChange (std::shared_ptr<const SampleSnapshot> before)
{
    auto after = snapshotSample();
    routeUndo.beginNewTransaction ("SAMPLE " + after->name.toUpperCase());
    lastGestureParameter = -1;
    routeUndo.perform (new SampleChange (*this, std::move (before), std::move (after)));
    lastGestureParameter = -1;   // the next change starts its own step
}

// Message thread. The buffer is already analysed.
void SiltProcessor::installBuffer (std::unique_ptr<silt::SampleBuffer> buffer, const juce::String& name, int factory)
{
    const int frames = buffer->getNumFrames();
    const double rate = buffer->getSampleRate();
    {
        const juce::ScopedLock sl (sampleLock);
        sampleZeroCrossings = buffer->getZeroCrossings();
        const auto& modes = buffer->getModes();
        const int count = juce::jmin ((int) modes.size(), (int) samplePitchHz.size());
        for (int i = 0; i < count; ++i)
        {
            samplePitchHz[(size_t) i].store ((float) modes[(size_t) i].hz, std::memory_order_relaxed);
            samplePitchLevel[(size_t) i].store (modes[(size_t) i].level, std::memory_order_relaxed);
        }
        samplePitchCount.store (count, std::memory_order_release);
        sampleTransients = buffer->getTransients();
        sampleCopy.setSize (2, frames);
        for (int ch = 0; ch < 2; ++ch)
            sampleCopy.copyFrom (ch, 0, buffer->getReadPointer (ch), frames);
        sampleCopyRate = rate;
        sampleName = name;
        factorySample = factory;
        encodedSample.reset();
    }

    engine.setBuffer (std::move (buffer));
    ++sampleGeneration;
}

std::array<float, SiltProcessor::liveColumns> SiltProcessor::getLivePeaks() const
{
    return instrument.getLivePeaks();
}

int SiltProcessor::readGrainViews (std::array<silt::GrainView, maxGrainViews>& out) const
{
    return instrument.readGrainViews (out);
}

// The region as it will play (snapped), as shares of the sample, for the screen.
std::pair<float, float> SiltProcessor::shownRegion() const
{
    const float start = params.getRawParameterValue ("start")->load() * 0.01f;
    const float length = params.getRawParameterValue ("length")->load() * 0.01f;
    const juce::ScopedLock sl (sampleLock);
    const int frames = sampleCopy.getNumSamples();
    if (frames == 0)
        return { start, length };

    const double begin = start * (frames - 1.0);
    const double end = std::min ((double) frames, begin + std::max (1.0, (double) length * frames));
    const auto snapped = silt::snapRegion (begin, end, frames,
                                           (silt::Snap) juce::jlimit (0, 2, (int) params.getRawParameterValue ("snap")->load()),
                                           sampleZeroCrossings, sampleTransients);
    return { (float) (snapped.begin / frames), (float) ((snapped.end - snapped.begin) / frames) };
}

// SLICE: where each slice starts, as the engine cuts them (GrainVoice::regionFor).
std::vector<float> SiltProcessor::sliceMarks() const
{
    if (playMode() != modeSlice)
        return {};
    if (juce::roundToInt (instrument.value (silt::params::sliceMode)) == 2 && sliceCountManual.load() > 0)
        return manualSlices();
    const auto [start, length] = shownRegion();
    std::vector<float> marks;
    if (juce::roundToInt (instrument.value (silt::params::sliceMode)) == 1)
    {
        const juce::ScopedLock sl (sampleLock);
        const int frames = sampleCopy.getNumSamples();
        if (frames == 0)
            return {};
        const float minimum = 256.0f / (float) frames, end = start + length;
        marks.push_back (start);
        for (const int t : sampleTransients)
        {
            const float at = (float) t / (float) frames;
            if (at > marks.back() + minimum && at < end - minimum && marks.size() < 128)
                marks.push_back (at);
        }
        return marks;
    }
    const int count = juce::jlimit (1, 128, juce::roundToInt (instrument.value (silt::params::sliceCount)));
    for (int i = 0; i < count; ++i)
        marks.push_back (start + length * (float) i / (float) count);
    return marks;
}

std::vector<float> SiltProcessor::manualSlices() const
{
    std::vector<float> marks ((size_t) sliceCountManual.load());
    for (size_t i = 0; i < marks.size(); ++i)
        marks[i] = sliceTable[i].load();
    return marks;
}

// Sorted, inside the sample, at most maxSlices, and no two closer than 256
// samples' worth (a slice too short to hold a grain).
float SiltProcessor::minimumSliceGap() const
{
    const juce::ScopedLock sl (sampleLock);
    return sampleCopy.getNumSamples() > 0 ? 256.0f / (float) sampleCopy.getNumSamples() : 0.0005f;
}

void SiltProcessor::setManualSlices (std::vector<float> marks)
{
    std::sort (marks.begin(), marks.end());
    const float gap = minimumSliceGap();
    std::vector<float> kept;
    for (const float m : marks)
        if (m >= 0.0f && m < 1.0f && (kept.empty() || m - kept.back() >= gap) && (int) kept.size() < maxSlices)
            kept.push_back (m);
    sliceCountManual.store (0, std::memory_order_release);   // the audio thread reads none while they change
    for (size_t i = 0; i < kept.size(); ++i)
        sliceTable[i].store (kept[i], std::memory_order_relaxed);
    sliceCountManual.store ((int) kept.size(), std::memory_order_release);
}

SiltProcessor::SliceState SiltProcessor::sliceState() const
{
    return { manualSlices(), params.getParameter ("sliceMode")->getValue() };
}

void SiltProcessor::restoreSlices (const SliceState& s)
{
    setManualSlices (s.marks);
    if (auto* mode = params.getParameter ("sliceMode"))
        mode->setValueNotifyingHost (s.mode);
}

class SiltProcessor::SliceChange final : public juce::UndoableAction
{
public:
    SliceChange (SiltProcessor& p, SliceState b, SliceState a) : processor (p), before (std::move (b)), after (std::move (a)) {}
    bool perform() override
    {
        if (first)
            first = false;   // recorded after the edit: nothing to do yet
        else
            processor.restoreSlices (after);
        return true;
    }
    bool undo() override { processor.restoreSlices (before); return true; }
    int getSizeInUnits() override { return 1; }

private:
    SiltProcessor& processor;
    SliceState before, after;
    bool first = true;
};

void SiltProcessor::beginSliceEdit() { sliceEditStart = sliceState(); }

void SiltProcessor::endSliceEdit()
{
    auto now = sliceState();
    if (now.sameAs (sliceEditStart))
        return;
    routeUndo.beginNewTransaction ("SLICES");
    lastGestureParameter = -1;
    {
        const juce::ScopedValueSetter<bool> quiet (replaying, true);
        routeUndo.perform (new SliceChange (*this, sliceEditStart, std::move (now)));
    }
    lastGestureParameter = -1;
}

// From EQUAL or AT HITS: their slices become MANUAL's, then the edit applies.
void SiltProcessor::makeSlicesManual()
{
    if (juce::roundToInt (instrument.value (silt::params::sliceMode)) == 2 && sliceCountManual.load() > 0)
        return;
    setManualSlices (sliceMarks());
    if (auto* mode = params.getParameter ("sliceMode"))
        mode->setValueNotifyingHost (mode->convertTo0to1 (2.0f));
}

void SiltProcessor::moveSlice (int index, float share)
{
    makeSlicesManual();
    auto marks = manualSlices();
    if (index < 0 || index >= (int) marks.size())
        return;
    // Between its neighbours: slices keep their order (and their minimum size).
    const float gap = minimumSliceGap() * 1.01f;
    const float lo = index > 0 ? marks[(size_t) index - 1] + gap : 0.0f;
    const float hi = index + 1 < (int) marks.size() ? marks[(size_t) index + 1] - gap : 0.999f;
    marks[(size_t) index] = juce::jlimit (lo, juce::jmax (lo, hi), share);
    setManualSlices (std::move (marks));
}

void SiltProcessor::addSlice (float share)
{
    makeSlicesManual();
    auto marks = manualSlices();
    marks.push_back (juce::jlimit (0.0f, 0.999f, share));
    setManualSlices (std::move (marks));
}

void SiltProcessor::removeSlice (int index)
{
    makeSlicesManual();
    auto marks = manualSlices();
    if (marks.size() > 1 && index >= 0 && index < (int) marks.size())
        marks.erase (marks.begin() + index);
    setManualSlices (std::move (marks));
}

// The gain that puts the sample's peak at −1 dBFS (0 with no sample).
float SiltProcessor::normalisingGainDb() const
{
    const juce::ScopedLock sl (sampleLock);
    const float peak = sampleCopy.getNumSamples() > 0 ? sampleCopy.getMagnitude (0, sampleCopy.getNumSamples()) : 0.0f;
    return peak > 1.0e-6f ? juce::jlimit (-24.0f, 24.0f, -1.0f - juce::Decibels::gainToDecibels (peak)) : 0.0f;
}

SiltProcessor::Overview SiltProcessor::getOverview() const
{
    const juce::ScopedLock sl (sampleLock);
    Overview o;
    const int frames = sampleCopy.getNumSamples();
    if (frames == 0)
        return o;

    o.seconds = frames / sampleCopyRate;
    o.sampleRate = sampleCopyRate;
    o.mono = std::equal (sampleCopy.getReadPointer (0), sampleCopy.getReadPointer (0) + frames, sampleCopy.getReadPointer (1));
    o.megabytes = (double) frames * 2.0 * sizeof (float) * 2.0 / (1024.0 * 1024.0);
    o.name = sampleName;
    for (int t : sampleTransients)
        o.transients.push_back ((float) t / (float) frames);
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
    juce::StringArray ccs;
    for (const auto& cc : knobCCs)
        ccs.add (juce::String (cc.load()));
    state.setProperty ("knobCCs", ccs.joinIntoString (","), nullptr);
    state.setProperty ("pageCCs", juce::String (previousPageCC.load()) + "," + juce::String (nextPageCC.load()), nullptr);
    state.setProperty ("page", currentPage.load(), nullptr);
    state.setProperty ("resonatorHalf", resonatorHalf.load(), nullptr);
    state.setProperty ("modRoutes", routesToString(), nullptr);
    state.setProperty ("modShown", modShown.load(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("randomAmount", randomAmount.load(), nullptr);
    state.setProperty ("randomCharacter", randomCharacter.load(), nullptr);
    {
        juce::StringArray marks;
        for (const float m : manualSlices())
            marks.add (juce::String (m, 6));
        state.setProperty ("slices", marks.joinIntoString (","), nullptr);
    }
    state.setProperty ("randomExcluded", randomExcluded.joinIntoString (","), nullptr);
    if (editorWidth.load() > 0)
    {
        state.setProperty ("editorWidth", editorWidth.load(), nullptr);
        state.setProperty ("editorHeight", editorHeight.load(), nullptr);
        state.setProperty ("layout", juce::String (layoutWell.load()), nullptr);
    }

    juce::MemoryBlock flac;
    state.removeProperty ("factorySample", nullptr);
    {
        const juce::ScopedLock sl (sampleLock);
        if (factorySample >= 0)
        {
            state.setProperty ("factorySample", factorySample, nullptr);   // built in: no need to carry the audio
            state.setProperty ("sampleName", sampleName, nullptr);
        }
        else if (sampleCopy.getNumSamples() > 0)
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
        // Each part's length, kept within what's really left (a corrupt length
        // can't ask for more than the state holds).
        const auto part = [&in] (juce::MemoryBlock& block)
        {
            const auto length = in.readInt64();
            if (length > 0 && length <= in.getNumBytesRemaining())
                in.readIntoMemoryBlock (block, (juce::pointer_sized_int) length);
        };
        juce::MemoryBlock xmlBlock;
        part (xmlBlock);
        xml = getXmlFromBinary (xmlBlock.getData(), (int) xmlBlock.getSize());
        part (flac);
    }
    else
    {
        xml = getXmlFromBinary (data, sizeInBytes);   // M0 states: parameters only
    }

    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;

    stateRestored.store (true);
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto name = tree.getProperty ("sampleName").toString();
    const int factory = tree.getProperty ("factorySample", -1);
    params.replaceState (tree);

    if (const auto ccs = juce::StringArray::fromTokens (tree.getProperty ("knobCCs").toString(), ",", ""); ccs.size() == slotsPerPage)
        for (int i = 0; i < slotsPerPage; ++i)
            knobCCs[(size_t) i].store (juce::jlimit (0, 127, ccs[i].getIntValue()));
    if (const auto pages = juce::StringArray::fromTokens (tree.getProperty ("pageCCs").toString(), ",", ""); pages.size() == 2)
    {
        previousPageCC.store (juce::jlimit (0, 127, pages[0].getIntValue()));
        nextPageCC.store (juce::jlimit (0, 127, pages[1].getIntValue()));
    }
    currentPage.store (juce::jlimit (0, numPages - 1, (int) tree.getProperty ("page", 0)));
    resonatorHalf.store (juce::jlimit (0, numResonatorHalves - 1, (int) tree.getProperty ("resonatorHalf", 0)));
    routesFromString (tree.getProperty ("modRoutes").toString());
    modShown.store (juce::jlimit (0, numModulators - 1, (int) tree.getProperty ("modShown", 0)));
    routeUndo.clearUndoHistory();   // a loaded set starts its own history
    presetName = tree.getProperty ("presetName", "Init").toString();
    presetRestored.store (true);
    randomAmount.store (juce::jlimit (0.0f, 1.0f, (float) tree.getProperty ("randomAmount", 0.5f)));
    randomCharacter.store (juce::jlimit (0, 3, (int) tree.getProperty ("randomCharacter", 0)));
    {
        std::vector<float> marks;
        for (const auto& m : juce::StringArray::fromTokens (tree.getProperty ("slices").toString(), ",", ""))
            marks.push_back (m.getFloatValue());
        setManualSlices (std::move (marks));
    }
    if (tree.hasProperty ("randomExcluded"))
        randomExcluded = juce::StringArray::fromTokens (tree.getProperty ("randomExcluded").toString(), ",", "");
    editorWidth.store ((int) tree.getProperty ("editorWidth", 0));
    editorHeight.store ((int) tree.getProperty ("editorHeight", 0));
    if (const auto layout = juce::StringArray::fromTokens (tree.getProperty ("layout").toString(), ",", ""); ! layout.isEmpty())
        layoutWell.store (juce::jlimit (0.1f, 0.9f, layout[0].getFloatValue()));

    if constexpr (! isInstrument)
        return;   // Silt FX has no sample (a state from before it went LIVE only may carry one)

    std::unique_ptr<juce::AudioFormatReader> reader;
    if (! flac.isEmpty())
        reader.reset (juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (flac, true), true));
    else if (factory >= 0 && factory < numFactorySamples())
        reader = openFactorySample (factory);
    if (reader == nullptr)
        return;

    const auto audio = decode (*reader);
    if (audio == nullptr)
        return;   // the sample there was stays
    auto buffer = makeBuffer (*audio, reader->sampleRate);
    buffer->analyse();   // on the host's thread: hosts restore state off the audio thread
    installBuffer (std::move (buffer), name, flac.isEmpty() ? factory : -1);

    const juce::ScopedLock sl (sampleLock);
    encodedSample = flac;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SiltProcessor();
}
