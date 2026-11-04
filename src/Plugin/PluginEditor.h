#pragma once
#include "PluginProcessor.h"
#include <functional>

class LibrEQLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    LibrEQLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
};

class EqGraph final : public juce::Component {
public:
    explicit EqGraph(LibrEQAudioProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    void refresh();
    void setSelected(int band);
    int getSelected() const { return selected; }
    void setAnalyzerVisible(bool);

    juce::Point<float> nodePosition(int band) const;
    float frequencyToX(float) const;
    float gainToY(float) const;

    std::function<void(int)> onSelectionChanged;

private:
    int hitTestNode(juce::Point<float>) const;
    int addBand(juce::Point<float>);
    void removeBand(int);
    void showBandMenu(int);
    float xToFrequency(float) const;
    float yToGain(float) const;
    float dbToSpectrumY(float) const;
    juce::Rectangle<float> plot() const;
    juce::Path spectrumCurve(const std::vector<float>& smoothed) const;
    void drawSpectrum(juce::Graphics&, const std::vector<float>& smoothed, juce::Colour, float fillAlpha, float lineAlpha, float lineWidth) const;
    void updateSpectrum(libreq::SpectrumTap&, std::vector<float>& smoothed);

    LibrEQAudioProcessor& processor;
    int selected { -1 }, hovered { -1 };
    bool dragging { false }, analyzerVisible { true };
    std::array<libreq::Band, LibrEQAudioProcessor::numBands> cachedBands {};
    std::array<bool, LibrEQAudioProcessor::numBands> cachedUsed {};
    std::array<std::vector<float>, LibrEQAudioProcessor::numBands> bandCurves;
    std::vector<float> totalCurve, scratch, preSmoothed, postSmoothed;
    double cachedRate { 0.0 };
    bool curvesDirty { true };
};

class LibrEQAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit LibrEQAudioProcessorEditor(LibrEQAudioProcessor&);
    ~LibrEQAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    EqGraph& getGraph() { return graph; }
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void bindBand(int);
    void updateStripVisibility();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    LibrEQAudioProcessor& processor;
    LibrEQLookAndFeel lookAndFeel;
    EqGraph graph;
    juce::TextButton analyzerButton { "Analyzer" }, bypassButton { "Bypass" }, bandOnButton { "On" }, removeButton { "Remove" };
    juce::Slider outputKnob, frequencyKnob, gainKnob, qKnob;
    juce::Label outputLabel, frequencyLabel, gainLabel, qLabel, bandLabel, emptyLabel;
    juce::ComboBox typeBox, slopeBox;
    std::unique_ptr<SliderAttachment> outputAttachment, frequencyAttachment, gainAttachment, qAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment, bandOnAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment, slopeAttachment;
    int boundBand { -2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessorEditor)
};
