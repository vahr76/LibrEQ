#pragma once
#include <JuceHeader.h>
#include "../DSP/Equalizer.h"
#include "Spectrum.h"
#include <array>
#include <atomic>

class LibrEQAudioProcessor final : public juce::AudioProcessor {
public:
    static constexpr int numBands = static_cast<int>(libreq::Equalizer::maxBands);

    struct BandParameters {
        juce::RangedAudioParameter* used {}, * on {}, * type {}, * frequency {}, * gain {}, * q {}, * slope {};
        std::atomic<float>* usedRaw {}, * onRaw {}, * typeRaw {}, * frequencyRaw {}, * gainRaw {}, * qRaw {}, * slopeRaw {};
    };

    LibrEQAudioProcessor();
    ~LibrEQAudioProcessor() override = default;

    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
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
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    const BandParameters& band(int index) const { return bands[static_cast<size_t>(index)]; }
    bool isBandUsed(int index) const { return band(index).usedRaw->load() > 0.5f; }
    libreq::Band readBand(int index) const noexcept;
    double currentSampleRate() const noexcept { return sampleRateForUi.load(); }

    static juce::String bandParameterId(const char* name, int index) { return juce::String(name) + juce::String(index + 1); }

    juce::AudioProcessorValueTreeState parameters;
    libreq::SpectrumTap preSpectrum, postSpectrum;
    std::atomic<bool> analyzerWanted { false };

    // Highest absolute output sample per channel since the last call. Call from the UI thread.
    std::array<float, 2> takeOutputPeaks() noexcept {
        return { outputPeaks[0].exchange(0.0f, std::memory_order_relaxed), outputPeaks[1].exchange(0.0f, std::memory_order_relaxed) };
    }

private:
    libreq::Equalizer eq;
    std::array<BandParameters, numBands> bands {};
    juce::RangedAudioParameter* bypassParameter {};
    std::atomic<float>* bypassRaw {}, * outputRaw {};
    juce::SmoothedValue<float> outputGain;
    std::atomic<double> sampleRateForUi { 48000.0 };
    bool wasBypassed { false };
    std::array<std::atomic<float>, 2> outputPeaks {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessor)
};
