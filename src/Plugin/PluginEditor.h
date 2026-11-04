#pragma once
#include "PluginProcessor.h"

class LibrEQAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit LibrEQAudioProcessorEditor(LibrEQAudioProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void timerCallback() override;
private:
    LibrEQAudioProcessor& processor;
    int selectedBand{-1};
    juce::ComboBox typeBox;
    juce::Label bandLabel;
    juce::Slider frequencySlider, gainSlider, qSlider;
    juce::Label frequencyLabel, gainLabel, qLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment, gainAttachment, qAttachment;
    void bindSelectedBand();
    void drawSpectrum(juce::Graphics&, juce::Rectangle<int>) const;
    juce::Rectangle<int> graphBounds() const;
    float xToFrequency(float) const;
    float frequencyToX(float) const;
    float yToGain(float) const;
    float gainToY(float) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessorEditor)
};
