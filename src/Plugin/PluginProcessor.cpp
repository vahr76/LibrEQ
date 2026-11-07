#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
const juce::StringArray filterTypeNames { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
const juce::StringArray slopeNames { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
const juce::StringArray placementNames { "Stereo", "Left", "Right", "Mid", "Side" };

juce::AudioParameterFloatAttributes decimals(int places) {
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction([places](float v, int) { return juce::String(v, places); });
}

// Accepts "1500", "1.5k", "1.5 kHz" or "1500 Hz".
float frequencyFromText(const juce::String& text) {
    const auto t = text.trim().toLowerCase();
    const float value = t.getFloatValue();
    return t.containsChar('k') ? value * 1000.0f : value;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    using Id = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(Id { "bypass", 1 }, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(Id { "output", 1 }, "Output Gain", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f, decimals(1)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(Id { "scale", 1 }, "Scale", juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 100.0f, decimals(0)));
    layout.add(std::make_unique<juce::AudioParameterBool>(Id { "autogain", 1 }, "Auto Gain", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(Id { "phase", 1 }, "Phase Mode", juce::StringArray { "Zero Latency", "Linear Phase" }, 0));

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto name = "Band " + juce::String(i + 1) + " ";
        const auto id = [i](const char* base) { return Id { LibrEQAudioProcessor::bandParameterId(base, i), 1 }; };
        layout.add(std::make_unique<juce::AudioParameterBool>(id("used"), name + "Used", false, juce::AudioParameterBoolAttributes().withAutomatable(false)));
        layout.add(std::make_unique<juce::AudioParameterBool>(id("on"), name + "On", true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(id("type"), name + "Type", filterTypeNames, 0));
        juce::NormalisableRange<float> frequency(20.0f, 20000.0f, 0.01f);
        frequency.setSkewForCentre(632.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("freq"), name + "Frequency", frequency, 1000.0f, juce::AudioParameterFloatAttributes()
                                                                   .withStringFromValueFunction([](float v, int) { return juce::String(v, v < 100.0f ? 1 : 0); })
                                                                   .withValueFromStringFunction(frequencyFromText)));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("gain"), name + "Gain", juce::NormalisableRange<float>(-30.0f, 30.0f, 0.01f), 0.0f, decimals(1)));
        juce::NormalisableRange<float> q(0.1f, 20.0f, 0.001f);
        q.setSkewForCentre(1.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("q"), name + "Q", q, 0.707f, decimals(2)));
        layout.add(std::make_unique<juce::AudioParameterChoice>(id("slope"), name + "Slope", slopeNames, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(id("placement"), name + "Placement", placementNames, 0));
        layout.add(std::make_unique<juce::AudioParameterBool>(id("dyn"), name + "Dynamic", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("thresh"), name + "Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f, decimals(1)));
    }
    return layout;
}

struct FactoryBand {
    libreq::FilterType type;
    float frequency, gain, q;
    libreq::Slope slope;
    libreq::Placement placement;
};

struct FactoryPreset {
    const char* name;
    std::vector<FactoryBand> bands;
};

const std::vector<FactoryPreset>& factoryPresets() {
    using T = libreq::FilterType;
    using S = libreq::Slope;
    using P = libreq::Placement;
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },
        { "Vocal Presence", { { T::HighPass, 90.0f, 0.0f, 0.707f, S::Db24, P::Stereo }, { T::Bell, 280.0f, -2.5f, 1.2f, S::Db12, P::Stereo }, { T::Bell, 3200.0f, 2.5f, 0.9f, S::Db12, P::Stereo }, { T::HighShelf, 11000.0f, 2.0f, 0.7f, S::Db12, P::Stereo } } },
        { "Warm Bass", { { T::HighPass, 32.0f, 0.0f, 0.707f, S::Db24, P::Stereo }, { T::LowShelf, 110.0f, 3.0f, 0.7f, S::Db12, P::Stereo }, { T::Bell, 350.0f, -2.5f, 1.4f, S::Db12, P::Stereo }, { T::LowPass, 9000.0f, 0.0f, 0.707f, S::Db12, P::Stereo } } },
        { "Clean Low End", { { T::HighPass, 45.0f, 0.0f, 0.707f, S::Db36, P::Stereo }, { T::HighPass, 180.0f, 0.0f, 0.707f, S::Db12, P::Side } } },
        { "Air Lift", { { T::HighShelf, 12000.0f, 3.5f, 0.7f, S::Db12, P::Stereo }, { T::Bell, 6500.0f, -1.0f, 2.0f, S::Db12, P::Stereo } } },
        { "Wide Highs", { { T::HighShelf, 7000.0f, 3.0f, 0.7f, S::Db12, P::Side }, { T::Bell, 300.0f, -2.0f, 0.9f, S::Db12, P::Side } } },
        { "Telephone", { { T::HighPass, 400.0f, 0.0f, 0.707f, S::Db24, P::Stereo }, { T::LowPass, 3200.0f, 0.0f, 0.707f, S::Db24, P::Stereo }, { T::Bell, 1400.0f, 4.0f, 1.0f, S::Db12, P::Stereo } } },
    };
    return presets;
}

const juce::Identifier extraStateId { "LIBREQ_EXTRA" }, slotStateId { "SLOT" }, presetStateId { "LIBREQ_PRESET" };
} // namespace

LibrEQAudioProcessor::LibrEQAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createLayout()) {
    for (int i = 0; i < numBands; ++i) {
        auto& b = bands[static_cast<size_t>(i)];
        const auto bind = [this, i](const char* base, juce::RangedAudioParameter*& parameter, std::atomic<float>*& raw) {
            const auto id = bandParameterId(base, i);
            parameter = parameters.getParameter(id);
            raw = parameters.getRawParameterValue(id);
        };
        bind("used", b.used, b.usedRaw);
        bind("on", b.on, b.onRaw);
        bind("type", b.type, b.typeRaw);
        bind("freq", b.frequency, b.frequencyRaw);
        bind("gain", b.gain, b.gainRaw);
        bind("q", b.q, b.qRaw);
        bind("slope", b.slope, b.slopeRaw);
        bind("placement", b.placement, b.placementRaw);
        bind("dyn", b.dynamic, b.dynamicRaw);
        bind("thresh", b.threshold, b.thresholdRaw);
    }
    bypassParameter = parameters.getParameter("bypass");
    phaseParameter = parameters.getParameter("phase");
    bypassRaw = parameters.getRawParameterValue("bypass");
    outputRaw = parameters.getRawParameterValue("output");
    scaleRaw = parameters.getRawParameterValue("scale");
    autoGainRaw = parameters.getRawParameterValue("autogain");
    phaseRaw = parameters.getRawParameterValue("phase");

    for (auto* parameter : getParameters())
        if (parameter != bypassParameter)
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                snapshotParameters.add(ranged);

    lastSnapshot = captureValues();
    addListener(this);
    startTimerHz(10);
}

LibrEQAudioProcessor::~LibrEQAudioProcessor() {
    stopTimer();
    removeListener(this);
    worker.stopThread(2000);
}

bool LibrEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet() == output
           && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void LibrEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    worker.stopThread(2000);

    sampleRateForUi.store(sampleRate);
    eq.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    linearPhase.prepare(sampleRate);
    activeLinear = isLinearPhase();

    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputRaw->load()));
    wetMix.reset(sampleRate, 0.015);
    wetMix.setCurrentAndTargetValue(bypassRaw->load() > 0.5f ? 0.0f : 1.0f);
    modeGain.reset(sampleRate, 0.01);
    modeGain.setCurrentAndTargetValue(1.0f);

    for (auto& ring : dryRing)
        ring.assign(static_cast<size_t>(linearPhase.getLatencySamples() + maxChunk + 1), 0.0f);
    dryWrite = 0;

    designedOnce = false;
    runBackgroundDesign(true);
    prepared = true;
    updateLatency();
    worker.startThread();
}

libreq::Band LibrEQAudioProcessor::readBand(int index) const noexcept {
    const auto& p = bands[static_cast<size_t>(index)];
    libreq::Band result;
    // Choice and bool parameters are stored as their plain value (index or 0/1).
    result.type = static_cast<libreq::FilterType>(juce::jlimit(0, 5, static_cast<int>(p.typeRaw->load() + 0.5f)));
    result.slope = static_cast<libreq::Slope>(juce::jlimit(0, 3, static_cast<int>(p.slopeRaw->load() + 0.5f)));
    result.placement = static_cast<libreq::Placement>(juce::jlimit(0, 4, static_cast<int>(p.placementRaw->load() + 0.5f)));
    result.frequency = p.frequencyRaw->load();
    result.gainDb = p.gainRaw->load();
    result.q = p.qRaw->load();
    result.dynamic = p.dynamicRaw->load() > 0.5f;
    result.thresholdDb = p.thresholdRaw->load();
    result.enabled = p.usedRaw->load() > 0.5f && p.onRaw->load() > 0.5f;
    return result;
}

libreq::Band LibrEQAudioProcessor::scaledBand(int index) const noexcept {
    auto result = readBand(index);
    result.gainDb *= getScale();
    return result;
}

libreq::Band LibrEQAudioProcessor::dspBand(int index, int solo, Engine engine) const noexcept {
    auto result = scaledBand(index);
    if (solo < 0) {
        // Linear phase cannot follow a gain that changes constantly; dynamic bands stay in the IIR engine.
        const bool dynamic = result.dynamic && libreq::usesGain(result.type);
        if ((engine == Engine::Fir && dynamic) || (engine == Engine::IirBesideFir && ! dynamic))
            result.enabled = false;
        return result;
    }
    if (engine == Engine::IirBesideFir) {
        result.enabled = false;
        return result;
    }
    if (index != solo) {
        result.enabled = false;
        return result;
    }
    result = libreq::soloedBand(result);
    result.dynamic = false;
    result.enabled = isBandUsed(index);
    return result;
}

float LibrEQAudioProcessor::computeAutoGain() const {
    // Mean power of the curve over a logarithmic frequency axis, which treats music as roughly pink.
    constexpr int points = 96;
    const double rate = currentSampleRate();
    std::array<libreq::BandDesign, numBands> designs;
    std::array<double, numBands> weights {};
    bool any = false;
    for (int i = 0; i < numBands; ++i) {
        const auto b = scaledBand(i);
        // A dynamic band is usually at rest, so it does not count.
        if (! b.enabled || (b.dynamic && libreq::usesGain(b.type)))
            continue;
        designs[static_cast<size_t>(i)] = libreq::designBand(b, rate);
        weights[static_cast<size_t>(i)] = b.placement == libreq::Placement::Stereo ? 1.0 : 0.5;
        any = true;
    }
    if (! any)
        return 1.0f;

    double sum = 0.0;
    for (int p = 0; p < points; ++p) {
        const double f = juce::jmin(20.0 * std::pow(1000.0, (p + 0.5) / points), rate * 0.49);
        double db = 0.0;
        for (int i = 0; i < numBands; ++i)
            if (weights[static_cast<size_t>(i)] > 0.0)
                db += weights[static_cast<size_t>(i)] * libreq::magnitudeDb(designs[static_cast<size_t>(i)], f, rate);
        sum += std::pow(10.0, db / 10.0);
    }
    const double meanDb = 10.0 * std::log10(sum / points);
    return static_cast<float>(std::pow(10.0, juce::jlimit(-24.0, 24.0, -meanDb) / 20.0));
}

void LibrEQAudioProcessor::runBackgroundDesign(bool force) {
    const juce::ScopedLock lock(designLock);
    const int solo = soloBand.load();
    const bool linear = isLinearPhase();

    libreq::LinearPhaseEngine::Bands target;
    std::array<libreq::Band, numBands> forGain;
    for (int i = 0; i < numBands; ++i) {
        target[static_cast<size_t>(i)] = dspBand(i, solo, Engine::Fir);
        forGain[static_cast<size_t>(i)] = scaledBand(i);
    }

    if (force || ! designedOnce || forGain != lastForGain) {
        autoGainValue.store(computeAutoGain());
        lastForGain = forGain;
    }
    if (linear && (force || ! designedOnce || ! lastDesignLinear || target != lastDesigned)) {
        linearPhase.design(target);
        lastDesigned = target;
    }
    lastDesignLinear = linear;
    designedOnce = true;
}

void LibrEQAudioProcessor::updateLatency() {
    if (! prepared)
        return;
    const int latency = isLinearPhase() ? linearPhase.getLatencySamples() : 0;
    if (getLatencySamples() != latency)
        setLatencySamples(latency);
}

void LibrEQAudioProcessor::timerCallback() {
    updateLatency();
    flushHistory();
}

void LibrEQAudioProcessor::pushBandsToIir(int solo) noexcept {
    const auto engine = activeLinear ? Engine::IirBesideFir : Engine::Iir;
    for (int i = 0; i < numBands; ++i)
        eq.setBand(static_cast<std::size_t>(i), dspBand(i, solo, engine));
}

void LibrEQAudioProcessor::processChunk(float* const* channels, int numChannels, int numSamples) noexcept {
    // The dry path matches the latency of the wet path, so bypass can crossfade.
    const int ringSize = static_cast<int>(dryRing[0].size());
    const int delay = activeLinear ? linearPhase.getLatencySamples() : 0;
    for (int ch = 0; ch < numChannels; ++ch) {
        auto& ring = dryRing[static_cast<size_t>(ch)];
        for (int i = 0; i < numSamples; ++i) {
            const int w = (dryWrite + i) % ringSize;
            ring[static_cast<size_t>(w)] = channels[ch][i];
            dryChunk[static_cast<size_t>(ch)][static_cast<size_t>(i)] = ring[static_cast<size_t>((w - delay + ringSize) % ringSize)];
        }
    }
    dryWrite = (dryWrite + numSamples) % ringSize;

    if (activeLinear)
        linearPhase.process(channels, numChannels, numSamples);
    eq.process(channels, numChannels, numSamples);

    for (int i = 0; i < numSamples; ++i) {
        const float wet = wetMix.getNextValue();
        const float gain = outputGain.getNextValue() * modeGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] = channels[ch][i] * gain * wet + dryChunk[static_cast<size_t>(ch)][static_cast<size_t>(i)] * (1.0f - wet);
    }
}

void LibrEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    if (numChannels <= 0 || ! prepared)
        return;
    const bool analyzer = analyzerWanted.load(std::memory_order_relaxed);

    if (analyzer)
        preSpectrum.push(buffer.getArrayOfReadPointers(), numChannels, numSamples);

    const int solo = soloBand.load(std::memory_order_relaxed);
    pushBandsToIir(solo);

    wetMix.setTargetValue(bypassRaw->load() > 0.5f ? 0.0f : 1.0f);
    float target = juce::Decibels::decibelsToGain(outputRaw->load());
    if (autoGainRaw->load() > 0.5f)
        target *= autoGainValue.load(std::memory_order_relaxed);
    outputGain.setTargetValue(target);

    const bool wantLinear = isLinearPhase();
    float* pointers[2] = { nullptr, nullptr };
    for (int offset = 0; offset < numSamples; offset += maxChunk) {
        // Changing mode: fade the wet path out, swap the engines, fade back in.
        if (wantLinear != activeLinear) {
            modeGain.setTargetValue(0.0f);
            if (modeGain.getCurrentValue() <= 1.0e-4f) {
                activeLinear = wantLinear;
                pushBandsToIir(solo);
                eq.reset();
                linearPhase.reset();
                modeGain.setTargetValue(1.0f);
            }
        } else {
            modeGain.setTargetValue(1.0f);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            pointers[ch] = buffer.getWritePointer(ch) + offset;
        processChunk(pointers, numChannels, juce::jmin(maxChunk, numSamples - offset));
    }

    if (analyzer)
        postSpectrum.push(buffer.getArrayOfReadPointers(), numChannels, numSamples);

    for (int ch = 0; ch < 2; ++ch) {
        const float peak = buffer.getMagnitude(juce::jmin(ch, numChannels - 1), 0, numSamples);
        auto& stored = outputPeaks[static_cast<size_t>(ch)];
        if (peak > stored.load(std::memory_order_relaxed))
            stored.store(peak, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* LibrEQAudioProcessor::createEditor() { return new LibrEQAudioProcessorEditor(*this); }

LibrEQAudioProcessor::Values LibrEQAudioProcessor::captureValues() const {
    Values values;
    values.reserve(static_cast<size_t>(snapshotParameters.size()));
    for (auto* parameter : snapshotParameters)
        values.push_back(parameter->getValue());
    return values;
}

void LibrEQAudioProcessor::applyValues(const Values& values) {
    const juce::ScopedValueSetter<bool> guard(restoring, true);
    const int count = juce::jmin(snapshotParameters.size(), static_cast<int>(values.size()));
    for (int i = 0; i < count; ++i) {
        auto* parameter = snapshotParameters[i];
        const float value = values[static_cast<size_t>(i)];
        if (! juce::approximatelyEqual(parameter->getValue(), value)) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
    }
}

juce::ValueTree LibrEQAudioProcessor::encodeValues(const Values& values, const juce::Identifier& type) const {
    juce::ValueTree tree(type);
    const int count = juce::jmin(snapshotParameters.size(), static_cast<int>(values.size()));
    for (int i = 0; i < count; ++i)
        tree.setProperty(snapshotParameters[i]->paramID, values[static_cast<size_t>(i)], nullptr);
    return tree;
}

LibrEQAudioProcessor::Values LibrEQAudioProcessor::decodeValues(const juce::ValueTree& tree) const {
    Values values;
    values.reserve(static_cast<size_t>(snapshotParameters.size()));
    for (auto* parameter : snapshotParameters)
        values.push_back(tree.hasProperty(parameter->paramID) ? static_cast<float>(tree[parameter->paramID]) : parameter->getDefaultValue());
    return values;
}

void LibrEQAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    flushHistory();
    auto state = parameters.copyState();
    if (auto old = state.getChildWithName(extraStateId); old.isValid())
        state.removeChild(old, nullptr);

    juce::ValueTree extra(extraStateId);
    extra.setProperty("analyzerMode", analyzerMode.load(), nullptr);
    extra.setProperty("displayRange", displayRange.load(), nullptr);
    extra.setProperty("uiScale", uiScale.load(), nullptr);
    extra.setProperty("presetName", presetName, nullptr);
    extra.setProperty("activeSlot", activeSlot, nullptr);
    const auto& other = abSlots[static_cast<size_t>(1 - activeSlot)];
    if (! other.empty())
        extra.appendChild(encodeValues(other, slotStateId), nullptr);
    state.appendChild(extra, nullptr);

    juce::MemoryOutputStream stream(dest, false);
    state.writeToStream(stream);
}

void LibrEQAudioProcessor::setStateInformation(const void* data, int size) {
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(size));
    if (! state.isValid() || ! state.hasType(parameters.state.getType()))
        return;

    parameters.replaceState(state);

    abSlots = {};
    activeSlot = 0;
    if (const auto extra = state.getChildWithName(extraStateId); extra.isValid()) {
        analyzerMode.store(juce::jlimit(0, 2, static_cast<int>(extra.getProperty("analyzerMode", 2))));
        displayRange.store(sanitiseRange(static_cast<int>(extra.getProperty("displayRange", 30))));
        uiScale.store(sanitiseScale(static_cast<float>(extra.getProperty("uiScale", 1.0f))));
        presetName = extra.getProperty("presetName", "Init").toString();
        activeSlot = juce::jlimit(0, 1, static_cast<int>(extra.getProperty("activeSlot", 0)));
        if (const auto slot = extra.getChildWithName(slotStateId); slot.isValid())
            abSlots[static_cast<size_t>(1 - activeSlot)] = decodeValues(slot);
    }
    resetHistory();
}

void LibrEQAudioProcessor::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) {
    ++activeGestures;
}

void LibrEQAudioProcessor::audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) {
    activeGestures = juce::jmax(0, activeGestures - 1);
    if (! restoring)
        historyPending = true;
}

void LibrEQAudioProcessor::resetHistory() {
    undoStack.clear();
    redoStack.clear();
    historyPending = false;
    lastSnapshot = captureValues();
}

void LibrEQAudioProcessor::flushHistory() {
    if (! historyPending || activeGestures > 0)
        return;
    historyPending = false;
    auto current = captureValues();
    if (current == lastSnapshot)
        return;
    undoStack.push_back(std::move(lastSnapshot));
    if (undoStack.size() > 100)
        undoStack.erase(undoStack.begin());
    lastSnapshot = std::move(current);
    redoStack.clear();
}

void LibrEQAudioProcessor::undo() {
    flushHistory();
    if (undoStack.empty())
        return;
    redoStack.push_back(lastSnapshot);
    lastSnapshot = undoStack.back();
    undoStack.pop_back();
    applyValues(lastSnapshot);
}

void LibrEQAudioProcessor::redo() {
    flushHistory();
    if (redoStack.empty())
        return;
    undoStack.push_back(lastSnapshot);
    lastSnapshot = redoStack.back();
    redoStack.pop_back();
    applyValues(lastSnapshot);
}

void LibrEQAudioProcessor::pushHistoryAfterProgramChange() {
    historyPending = true;
    flushHistory();
}

void LibrEQAudioProcessor::switchSlot(int slot) {
    slot = juce::jlimit(0, 1, slot);
    if (slot == activeSlot)
        return;
    flushHistory();
    abSlots[static_cast<size_t>(activeSlot)] = captureValues();
    activeSlot = slot;
    auto& target = abSlots[static_cast<size_t>(slot)];
    if (target.empty())
        target = abSlots[static_cast<size_t>(1 - slot)]; // B starts as a copy of A.
    applyValues(target);
    pushHistoryAfterProgramChange();
}

void LibrEQAudioProcessor::copyToOtherSlot() {
    flushHistory();
    abSlots[static_cast<size_t>(1 - activeSlot)] = captureValues();
}

juce::StringArray LibrEQAudioProcessor::getFactoryPresetNames() {
    juce::StringArray names;
    for (const auto& preset : factoryPresets())
        names.add(preset.name);
    return names;
}

void LibrEQAudioProcessor::loadFactoryPreset(int index) {
    const auto& presets = factoryPresets();
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    const auto& preset = presets[static_cast<size_t>(index)];

    flushHistory();
    Values values;
    for (auto* parameter : snapshotParameters)
        values.push_back(parameter->getDefaultValue());

    const auto set = [&](juce::RangedAudioParameter* parameter, float plain) {
        const int position = snapshotParameters.indexOf(parameter);
        if (position >= 0)
            values[static_cast<size_t>(position)] = parameter->convertTo0to1(plain);
    };
    for (size_t i = 0; i < preset.bands.size(); ++i) {
        const auto& source = preset.bands[i];
        const auto& p = bands[i];
        set(p.used, 1.0f);
        set(p.on, 1.0f);
        set(p.type, static_cast<float>(source.type));
        set(p.frequency, source.frequency);
        set(p.gain, source.gain);
        set(p.q, source.q);
        set(p.slope, static_cast<float>(source.slope));
        set(p.placement, static_cast<float>(source.placement));
    }

    // A preset chooses the curve only; the listening setup stays as it is.
    for (const char* id : { "output", "autogain", "phase" }) {
        auto* parameter = parameters.getParameter(id);
        values[static_cast<size_t>(snapshotParameters.indexOf(parameter))] = parameter->getValue();
    }

    applyValues(values);
    presetName = preset.name;
    pushHistoryAfterProgramChange();
}

juce::File LibrEQAudioProcessor::getUserPresetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("LibrEQ").getChildFile("Presets");
}

juce::Array<juce::File> LibrEQAudioProcessor::getUserPresets() const {
    auto files = getUserPresetFolder().findChildFiles(juce::File::findFiles, false, "*.librq");
    files.sort();
    return files;
}

bool LibrEQAudioProcessor::savePreset(const juce::File& file) {
    flushHistory();
    file.getParentDirectory().createDirectory();
    const auto xml = encodeValues(captureValues(), presetStateId).createXml();
    if (xml == nullptr || ! xml->writeTo(file))
        return false;
    presetName = file.getFileNameWithoutExtension();
    return true;
}

bool LibrEQAudioProcessor::loadPreset(const juce::File& file) {
    const auto xml = juce::parseXML(file);
    if (xml == nullptr)
        return false;
    const auto tree = juce::ValueTree::fromXml(*xml);
    if (! tree.hasType(presetStateId))
        return false;
    flushHistory();
    applyValues(decodeValues(tree));
    presetName = file.getFileNameWithoutExtension();
    pushHistoryAfterProgramChange();
    return true;
}

#if ! JUCE_STANDALONE_APPLICATION
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LibrEQAudioProcessor(); }
#endif
