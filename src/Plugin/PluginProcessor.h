#pragma once
#include <JuceHeader.h>
#include "../DSP/Equalizer.h"
#include "../DSP/LinearPhase.h"
#include "Spectrum.h"
#include <array>
#include <atomic>
#include <vector>

class LibrEQAudioProcessor final : public juce::AudioProcessor,
                                   private juce::AudioProcessorListener,
                                   private juce::Timer {
public:
    static constexpr int numBands = static_cast<int>(libreq::Equalizer::maxBands);

    struct BandParameters {
        juce::RangedAudioParameter* used {}, * on {}, * type {}, * frequency {}, * gain {}, * q {}, * slope {}, * placement {}, * dynamic {}, * threshold {};
        std::atomic<float>* usedRaw {}, * onRaw {}, * typeRaw {}, * frequencyRaw {}, * gainRaw {}, * qRaw {}, * slopeRaw {}, * placementRaw {}, * dynamicRaw {}, * thresholdRaw {};
    };

    LibrEQAudioProcessor();
    ~LibrEQAudioProcessor() override;

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
    libreq::Band readBand(int index) const noexcept;       // The values as set by the user.
    libreq::Band scaledBand(int index) const noexcept;     // With the global scale applied.
    // Gain a band applies right now, including dynamics. Safe to call from the UI.
    float getLiveGainDb(int index) const noexcept { return eq.getLiveGainDb(static_cast<std::size_t>(index)); }
    float getScale() const noexcept { return scaleRaw->load() * 0.01f; }
    double currentSampleRate() const noexcept { return sampleRateForUi.load(); }
    bool isLinearPhase() const noexcept { return phaseRaw->load() > 0.5f; }
    juce::RangedAudioParameter* getPhaseParameter() const { return phaseParameter; }

    static juce::String bandParameterId(const char* name, int index) { return juce::String(name) + juce::String(index + 1); }

    // Highest absolute output sample per channel since the last call. Call from the UI thread.
    std::array<float, 2> takeOutputPeaks() noexcept {
        return { outputPeaks[0].exchange(0.0f, std::memory_order_relaxed), outputPeaks[1].exchange(0.0f, std::memory_order_relaxed) };
    }

    // Undo history. All of this runs on the message thread.
    void flushHistory();
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // A/B comparison.
    int getActiveSlot() const { return activeSlot; }
    void switchSlot(int slot);
    void copyToOtherSlot();

    // Presets.
    static juce::StringArray getFactoryPresetNames();
    void loadFactoryPreset(int index);
    static juce::File getUserPresetFolder();
    juce::Array<juce::File> getUserPresets() const;
    bool savePreset(const juce::File&);
    bool loadPreset(const juce::File&);
    juce::String getPresetName() const { return presetName; }

    // Background work; public so that tests can run it directly.
    void runBackgroundDesign(bool force = false);
    void updateLatency();
    float getAutoGainDb() const noexcept { return juce::Decibels::gainToDecibels(autoGainValue.load()); }

    juce::AudioProcessorValueTreeState parameters;
    libreq::SpectrumTap preSpectrum, postSpectrum;
    std::atomic<bool> analyzerWanted { false };
    std::atomic<int> analyzerMode { 2 };
    std::atomic<int> displayRange { 30 };   // Graph range in dB: 6, 12 or 30.
    std::atomic<float> uiScale { 1.0f };    // Editor size: 0.75 to 1.5.
    static int sanitiseRange(int range) { return range <= 6 ? 6 : range <= 12 ? 12 : 30; }
    static float sanitiseScale(float scale) { return juce::jlimit(0.75f, 1.5f, std::isfinite(scale) ? scale : 1.0f); }
    std::atomic<int> soloBand { -1 };

private:
    class Worker final : public juce::Thread {
    public:
        explicit Worker(LibrEQAudioProcessor& p) : juce::Thread("LibrEQ design"), owner(p) {}
        void run() override {
            while (! threadShouldExit()) {
                owner.runBackgroundDesign();
                wait(30);
            }
        }
    private:
        LibrEQAudioProcessor& owner;
    };

    void timerCallback() override;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override;
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override;

    using Values = std::vector<float>;
    Values captureValues() const;
    void applyValues(const Values&);
    juce::ValueTree encodeValues(const Values&, const juce::Identifier& type) const;
    Values decodeValues(const juce::ValueTree&) const;
    void resetHistory();
    void pushHistoryAfterProgramChange();
    enum class Engine { Iir, Fir, IirBesideFir };
    libreq::Band dspBand(int index, int solo, Engine) const noexcept;
    void pushBandsToIir(int solo) noexcept;
    float computeAutoGain() const;
    void processChunk(float* const* channels, int numChannels, int numSamples) noexcept;

    static constexpr int maxChunk = 256;

    libreq::Equalizer eq;
    libreq::LinearPhaseEngine linearPhase;
    Worker worker { *this };
    std::array<BandParameters, numBands> bands {};
    juce::RangedAudioParameter* bypassParameter {}, * phaseParameter {};
    std::atomic<float>* bypassRaw {}, * outputRaw {}, * scaleRaw {}, * autoGainRaw {}, * phaseRaw {};
    juce::Array<juce::RangedAudioParameter*> snapshotParameters;

    juce::SmoothedValue<float> outputGain, wetMix, modeGain;
    std::array<std::vector<float>, 2> dryRing;
    std::array<std::array<float, maxChunk>, 2> dryChunk {};
    int dryWrite { 0 };
    bool activeLinear { false };
    bool prepared { false };

    std::atomic<double> sampleRateForUi { 48000.0 };
    std::atomic<float> autoGainValue { 1.0f };
    std::array<std::atomic<float>, 2> outputPeaks {};

    // Touched only by the background thread, or by tests while it is stopped.
    libreq::LinearPhaseEngine::Bands lastDesigned {};
    std::array<libreq::Band, numBands> lastForGain {};
    bool lastDesignLinear { false }, designedOnce { false };
    juce::CriticalSection designLock;

    std::vector<Values> undoStack, redoStack;
    Values lastSnapshot;
    bool historyPending { false }, restoring { false };
    int activeGestures { 0 };

    std::array<Values, 2> abSlots;
    int activeSlot { 0 };
    juce::String presetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessor)
};
