#pragma once
#include <JuceHeader.h>
#include "../DSP/Equalizer.h"

class LibrEQAudioProcessor final : public juce::AudioProcessor {
public:
    LibrEQAudioProcessor();
    ~LibrEQAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "LibrEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState parameters;
    arielo::Equalizer eq;
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize * 2> fftData {};
    int fftIndex { 0 };
    std::array<std::atomic<float>, fftSize / 2> spectrum {};
public:
    float getSpectrumBin(int index) const noexcept;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessor)
};
