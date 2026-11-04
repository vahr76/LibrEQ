#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
const juce::StringArray filterTypeNames { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
const juce::StringArray slopeNames { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };

juce::AudioParameterFloatAttributes decimals(int places) {
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction([places](float v, int) { return juce::String(v, places); });
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    using Id = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(Id { "bypass", 1 }, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(Id { "output", 1 }, "Output Gain", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f, decimals(1)));

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto name = "Band " + juce::String(i + 1) + " ";
        const auto id = [i](const char* base) { return Id { LibrEQAudioProcessor::bandParameterId(base, i), 1 }; };
        layout.add(std::make_unique<juce::AudioParameterBool>(id("used"), name + "Used", false, juce::AudioParameterBoolAttributes().withAutomatable(false)));
        layout.add(std::make_unique<juce::AudioParameterBool>(id("on"), name + "On", true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(id("type"), name + "Type", filterTypeNames, 0));
        juce::NormalisableRange<float> frequency(20.0f, 20000.0f, 0.01f);
        frequency.setSkewForCentre(632.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("freq"), name + "Frequency", frequency, 1000.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, v < 100.0f ? 1 : 0); })));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("gain"), name + "Gain", juce::NormalisableRange<float>(-30.0f, 30.0f, 0.01f), 0.0f, decimals(1)));
        juce::NormalisableRange<float> q(0.1f, 20.0f, 0.001f);
        q.setSkewForCentre(1.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("q"), name + "Q", q, 0.707f, decimals(2)));
        layout.add(std::make_unique<juce::AudioParameterChoice>(id("slope"), name + "Slope", slopeNames, 0));
    }
    return layout;
}
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
    }
    bypassParameter = parameters.getParameter("bypass");
    bypassRaw = parameters.getRawParameterValue("bypass");
    outputRaw = parameters.getRawParameterValue("output");
}

bool LibrEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet() == output
           && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void LibrEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRateForUi.store(sampleRate);
    eq.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputRaw->load()));
    setLatencySamples(0);
}

libreq::Band LibrEQAudioProcessor::readBand(int index) const noexcept {
    const auto& p = bands[static_cast<size_t>(index)];
    libreq::Band result;
    // Choice and bool parameters are stored as their plain value (index or 0/1).
    result.type = static_cast<libreq::FilterType>(juce::jlimit(0, 5, static_cast<int>(p.typeRaw->load() + 0.5f)));
    result.slope = static_cast<libreq::Slope>(juce::jlimit(0, 3, static_cast<int>(p.slopeRaw->load() + 0.5f)));
    result.frequency = p.frequencyRaw->load();
    result.gainDb = p.gainRaw->load();
    result.q = p.qRaw->load();
    result.enabled = p.usedRaw->load() > 0.5f && p.onRaw->load() > 0.5f;
    return result;
}

void LibrEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    const bool analyzer = analyzerWanted.load(std::memory_order_relaxed);

    if (analyzer)
        preSpectrum.push(buffer.getArrayOfReadPointers(), numChannels, numSamples);

    const bool bypassed = bypassRaw->load() > 0.5f;
    if (! bypassed) {
        if (wasBypassed)
            eq.reset();
        for (int i = 0; i < numBands; ++i)
            eq.setBand(static_cast<std::size_t>(i), readBand(i));
        eq.process(buffer.getArrayOfWritePointers(), numChannels, numSamples);
        outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputRaw->load()));
        outputGain.applyGain(buffer, numSamples);
    }
    wasBypassed = bypassed;

    if (analyzer)
        postSpectrum.push(buffer.getArrayOfReadPointers(), numChannels, numSamples);
}

juce::AudioProcessorEditor* LibrEQAudioProcessor::createEditor() { return new LibrEQAudioProcessorEditor(*this); }

void LibrEQAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream stream(dest, true);
    parameters.copyState().writeToStream(stream);
}

void LibrEQAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(size)); state.isValid() && state.hasType(parameters.state.getType()))
        parameters.replaceState(state);
}

#if ! JUCE_STANDALONE_APPLICATION
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LibrEQAudioProcessor(); }
#endif
