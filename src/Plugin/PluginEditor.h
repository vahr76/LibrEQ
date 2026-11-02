#pragma once
#include "PluginProcessor.h"

class LibrEQAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit LibrEQAudioProcessorEditor(LibrEQAudioProcessor& p) : AudioProcessorEditor(p), processor(p) { setSize(960, 560); }
    void paint(juce::Graphics&) override;
    void resized() override {}
private:
    LibrEQAudioProcessor& processor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessorEditor)
};

