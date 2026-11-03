#pragma once
#include "PluginProcessor.h"

class LibrEQAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit LibrEQAudioProcessorEditor(LibrEQAudioProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
private:
    LibrEQAudioProcessor& processor;
    int selectedBand{-1};
    juce::ComboBox typeBox;
    juce::Label bandLabel;
    juce::Rectangle<int> graphBounds() const;
    float xToFrequency(float) const;
    float frequencyToX(float) const;
    float yToGain(float) const;
    float gainToY(float) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessorEditor)
};
