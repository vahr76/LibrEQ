#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < 8; ++i) {
        const auto id = juce::String(i + 1);
        layout.add(std::make_unique<juce::AudioParameterFloat>("freq" + id, "Band " + id + " Frequency", juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.25f), 1000.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("gain" + id, "Band " + id + " Gain", -24.0f, 24.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("q" + id, "Band " + id + " Q", juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.35f), 0.707f));
    }
    return layout;
}

LibrEQAudioProcessor::LibrEQAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createLayout()) {}

void LibrEQAudioProcessor::prepareToPlay(double sampleRate, int blockSize) { eq.prepare(sampleRate, blockSize, getTotalNumOutputChannels()); }

void LibrEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    float* channels[2] = { buffer.getWritePointer(0), buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0) };
    eq.process(channels, buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* LibrEQAudioProcessor::createEditor() { return new LibrEQAudioProcessorEditor(*this); }
void LibrEQAudioProcessor::getStateInformation(juce::MemoryBlock& dest) { juce::MemoryOutputStream stream(dest, true); parameters.state.writeToStream(stream); }
void LibrEQAudioProcessor::setStateInformation(const void* data, int size) { if (auto state = juce::ValueTree::readFromData(data, size); state.isValid()) parameters.replaceState(state); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LibrEQAudioProcessor(); }
