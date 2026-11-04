#pragma once
#include "PluginProcessor.h"
#include "UiHelpers.h"
#include <functional>

class LibrEQLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    LibrEQLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

private:
    juce::SharedResourcePointer<libreq::UiFonts> fonts;
};

class EqGraph final : public juce::Component {
public:
    enum class AnalyzerMode { Off, Output, InputOutput };

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
    void setAnalyzerMode(AnalyzerMode);
    AnalyzerMode getAnalyzerMode() const { return analyzerMode; }
    void removeSelectedBand() { removeBand(selected); }

    juce::Point<float> nodePosition(int band) const;
    juce::Point<float> qHandlePosition(int band) const;
    float frequencyToX(float) const;
    float gainToY(float) const;
    juce::Rectangle<float> plot() const;

    std::function<void(int)> onSelectionChanged;

private:
    int hitTestNode(juce::Point<float>) const;
    bool hitsQHandle(juce::Point<float>) const;
    int addBand(juce::Point<float>);
    void removeBand(int);
    void showBandMenu(int);
    float xToFrequency(float) const;
    float yToGain(float) const;
    float dbToSpectrumY(float) const;
    juce::Path spectrumCurve(const std::vector<float>& smoothed) const;
    void drawSpectrum(juce::Graphics&, const std::vector<float>& smoothed, juce::Colour, float fillAlpha, float lineAlpha, float lineWidth) const;
    void drawGrid(juce::Graphics&) const;
    void drawBandCurves(juce::Graphics&) const;
    void drawNodes(juce::Graphics&) const;
    void drawCursor(juce::Graphics&) const;
    void drawAxes(juce::Graphics&) const;
    juce::Path curvePath(const std::vector<float>& curve) const;
    void updateSpectrum(libreq::SpectrumTap&, std::vector<float>& smoothed);

    LibrEQAudioProcessor& processor;
    juce::SharedResourcePointer<libreq::UiFonts> fonts;
    int selected { -1 }, hovered { -1 };
    bool dragging { false }, draggingQ { false };
    float dragStartQ { 1.0f };
    juce::Point<float> cursor { -1.0f, -1.0f };
    AnalyzerMode analyzerMode { AnalyzerMode::InputOutput };
    std::array<libreq::Band, LibrEQAudioProcessor::numBands> cachedBands {};
    std::array<bool, LibrEQAudioProcessor::numBands> cachedUsed {};
    std::array<float, LibrEQAudioProcessor::numBands> nodeAppear {};
    float selectionAppear { 0.0f };
    std::array<std::vector<float>, LibrEQAudioProcessor::numBands> bandCurves;
    std::vector<float> totalCurve, secondaryCurve, scratch, preSmoothed, postSmoothed;
    double cachedRate { 0.0 };
    bool curvesDirty { true };
};

class OutputMeter final : public juce::Component {
public:
    void update(std::array<float, 2> peaks);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }
    float getMaxDb() const { return maxDb; }

private:
    juce::SharedResourcePointer<libreq::UiFonts> fonts;
    std::array<float, 2> levelDb { -100.0f, -100.0f }, holdDb { -100.0f, -100.0f };
    std::array<int, 2> holdFrames {};
    float maxDb { -100.0f };
};

class BandPanel final : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    juce::Colour accent { juce::Colours::white };
};

class LibrEQAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit LibrEQAudioProcessorEditor(LibrEQAudioProcessor&);
    ~LibrEQAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    EqGraph& getGraph() { return graph; }
    BandPanel& getPanel() { return panel; }
    OutputMeter& getMeter() { return meter; }
    void refreshNow() { timerCallback(); }
    juce::TextButton& getSoloButton() { return soloButton; }

private:
    void timerCallback() override;
    void bindBand(int);
    void layoutPanel();
    void movePanel(bool immediately);
    void showAnalyzerMenu();
    void showPresetMenu();
    void updateHeader();
    void setAnalyzerMode(EqGraph::AnalyzerMode);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    LibrEQAudioProcessor& processor;
    LibrEQLookAndFeel lookAndFeel;
    juce::SharedResourcePointer<libreq::UiFonts> fonts;
    EqGraph graph;
    OutputMeter meter;
    BandPanel panel;
    juce::TextButton analyzerButton, bypassButton { "Bypass" }, bandOnButton { "On" }, soloButton { "Solo" }, removeButton { "Remove" };
    juce::TextButton presetButton, slotAButton { "A" }, slotBButton { "B" }, slotCopyButton { "Copy" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton phaseButton, autoGainButton { "Auto" };
    juce::Slider outputKnob, scaleKnob, frequencyKnob, gainKnob, qKnob;
    juce::Label outputLabel, scaleLabel, frequencyLabel, gainLabel, qLabel, bandLabel;
    juce::ComboBox typeBox, slopeBox, placementBox;
    std::unique_ptr<SliderAttachment> outputAttachment, scaleAttachment, frequencyAttachment, gainAttachment, qAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment, autoGainAttachment, bandOnAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment, slopeAttachment, placementAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int boundBand { -2 };
    libreq::FilterType laidOutType { libreq::FilterType::Bell };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrEQAudioProcessorEditor)
};
