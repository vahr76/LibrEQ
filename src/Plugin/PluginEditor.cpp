#include "PluginEditor.h"

LibrEQAudioProcessorEditor::LibrEQAudioProcessorEditor(LibrEQAudioProcessor& p) : AudioProcessorEditor(p), processor(p) {
    setSize(960, 560);
    typeBox.addItemList(juce::StringArray { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" }, 1);
    typeBox.setSelectedId(1);
    typeBox.onChange = [this] {
        if (selectedBand >= 0) {
            auto* parameter = processor.parameters.getParameter("type" + juce::String(selectedBand + 1));
            if (parameter != nullptr) parameter->setValueNotifyingHost(parameter->convertTo0to1((float) typeBox.getSelectedItemIndex()));
            bandLabel.setText("Band " + juce::String(selectedBand + 1) + "  ·  " + typeBox.getText(), juce::dontSendNotification);
        }
        repaint();
    };
    addAndMakeVisible(typeBox);
    bandLabel.setText("Band 1  ·  Bell", juce::dontSendNotification);
    bandLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd7e2e8));
    bandLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(bandLabel);
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& text) {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 22);
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff58d6bd));
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff52616d));
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(0xff9eabb5));
        addAndMakeVisible(slider); addAndMakeVisible(label);
    };
    setupSlider(frequencySlider, frequencyLabel, "Frequency");
    setupSlider(gainSlider, gainLabel, "Gain");
    setupSlider(qSlider, qLabel, "Q");
    selectedBand = 0;
    bindSelectedBand();
}

void LibrEQAudioProcessorEditor::resized() {
    typeBox.setBounds(getWidth() - 220, 22, 190, 30);
    bandLabel.setBounds(getWidth() - 440, 22, 200, 30);
    const int y = getHeight() - 50, width = (getWidth() - 72) / 3;
    frequencyLabel.setBounds(24, y, 78, 22); frequencySlider.setBounds(100, y, width - 78, 22);
    gainLabel.setBounds(24 + width, y, 54, 22); gainSlider.setBounds(78 + width, y, width - 54, 22);
    qLabel.setBounds(24 + width * 2, y, 30, 22); qSlider.setBounds(54 + width * 2, y, width - 30, 22);
}

void LibrEQAudioProcessorEditor::bindSelectedBand() {
    if (selectedBand < 0) return;
    const auto id = juce::String(selectedBand + 1);
    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "freq" + id, frequencySlider);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "gain" + id, gainSlider);
    qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "q" + id, qSlider);
}

void LibrEQAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121419));
    g.setColour(juce::Colour(0xffe8edf2));
    g.setFont(juce::FontOptions(24.0f));
    g.drawText("LibrEQ", 24, 18, 220, 34, juce::Justification::left);
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(juce::Colour(0xff8d98a5));
    g.drawText("Open equalization for everyone", 25, 48, 240, 22, juce::Justification::left);
    auto graph = getLocalBounds().reduced(24).withTop(88).withBottom(getHeight() - 78);
    g.setColour(juce::Colour(0xff1b2028));
    g.fillRoundedRectangle(graph.toFloat(), 8.0f);
    g.setColour(juce::Colour(0x1816dec0));
    g.fillRoundedRectangle(graph.reduced(1).toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff343b46));
    for (int i = 0; i <= 8; ++i) {
        g.setColour(i == 4 ? juce::Colour(0xff4b5865) : juce::Colour(0xff303945));
        g.drawHorizontalLine(graph.getY() + i * graph.getHeight() / 8, (float)graph.getX(), (float)graph.getRight());
    }
    for (int i = 0; i <= 10; ++i) {
        g.setColour(juce::Colour(0xff2b333e));
        g.drawVerticalLine(graph.getX() + i * graph.getWidth() / 10, (float)graph.getY(), (float)graph.getBottom());
    }
    g.setColour(juce::Colour(0xff8994a1));
    g.setFont(juce::FontOptions(11.0f));
    for (int i = 0; i <= 8; ++i) { const auto db = 24 - i * 6; g.drawText(juce::String(db) + " dB", graph.getX() + 8, graph.getY() + i * graph.getHeight() / 8 - 8, 60, 16, juce::Justification::left); }
    const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : freqs) g.drawText(juce::String((int)f) + " Hz", (int)frequencyToX(f) - 24, graph.getBottom() - 24, 52, 16, juce::Justification::centred);
    juce::Path curve;
    juce::Path curveFill;
    curve.startNewSubPath((float)graph.getX(), gainToY(0.0f));
    for (int px = 0; px <= graph.getWidth(); px += 4) {
        const float f = xToFrequency((float)(graph.getX() + px));
        float db = 0.0f;
        for (int band = 0; band < 8; ++band) {
            const auto* gain = processor.parameters.getRawParameterValue("gain" + juce::String(band + 1));
            const auto* freq = processor.parameters.getRawParameterValue("freq" + juce::String(band + 1));
            const auto* type = processor.parameters.getRawParameterValue("type" + juce::String(band + 1));
            const float distance = std::log2(std::max(20.0f, f) / std::max(20.0f, freq->load()));
            const float shape = std::exp(-distance * distance * 5.0f);
            switch (juce::jlimit(0, 5, juce::roundToInt(type->load() * 5.0f))) {
                case 1: db += gain->load() * (1.0f - std::tanh(distance * 4.0f)) * 0.5f; break;
                case 2: db += gain->load() * (1.0f + std::tanh(distance * 4.0f)) * 0.5f; break;
                case 3: db += (f < freq->load() ? -18.0f : 0.0f); break;
                case 4: db += (f > freq->load() ? -18.0f : 0.0f); break;
                case 5: db += -18.0f * shape; break;
                default: db += gain->load() * shape; break;
            }
        }
        curve.lineTo((float)(graph.getX() + px), gainToY(juce::jlimit(-24.0f, 24.0f, db)));
    }
    curveFill = curve;
    curveFill.lineTo((float)graph.getRight(), gainToY(0.0f));
    curveFill.closeSubPath();
    g.setColour(juce::Colour(0x2458d6bd));
    g.fillPath(curveFill);
    g.setColour(juce::Colour(0x3558d6bd));
    g.strokePath(curve, juce::PathStrokeType(6.0f));
    g.setColour(juce::Colour(0xff58d6bd));
    g.strokePath(curve, juce::PathStrokeType(2.4f));
    g.setColour(juce::Colour(0xff58d6bd));
    for (int i = 0; i < 8; ++i) {
        const auto* freq = processor.parameters.getRawParameterValue("freq" + juce::String(i + 1));
        const auto* gain = processor.parameters.getRawParameterValue("gain" + juce::String(i + 1));
        const float x = frequencyToX(freq->load()), y = gainToY(gain->load());
        if (i == selectedBand) {
            g.setColour(juce::Colour(0x4058d6bd));
            g.fillEllipse(x - 14.0f, y - 14.0f, 28.0f, 28.0f);
            g.setColour(juce::Colour(0xfff5fffc));
            g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 2.0f);
        }
        g.setColour(i == selectedBand ? juce::Colour(0xff58d6bd) : juce::Colour(0xff8be6d3));
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour(juce::Colour(0xff121419));
        g.drawText(juce::String(i + 1), (int)x - 5, (int)y - 7, 10, 14, juce::Justification::centred);
        if (i == selectedBand) {
            const auto freqText = juce::String(freq->load(), 0) + " Hz";
            const auto gainText = juce::String(gain->load(), 1) + " dB";
            g.setColour(juce::Colour(0xffdce9e7));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(freqText + "  " + gainText, (int)x - 58, (int)y - 30, 116, 16, juce::Justification::centred);
        }
    }
}

juce::Rectangle<int> LibrEQAudioProcessorEditor::graphBounds() const { return getLocalBounds().reduced(24).withTop(88).withBottom(getHeight() - 78); }
float LibrEQAudioProcessorEditor::frequencyToX(float f) const { auto r = graphBounds(); return (float)r.getX() + juce::jmap(std::log10(juce::jlimit(20.0f, 20000.0f, f)), std::log10(20.0f), std::log10(20000.0f), 0.0f, (float)r.getWidth()); }
float LibrEQAudioProcessorEditor::xToFrequency(float x) const { auto r = graphBounds(); return std::pow(10.0f, juce::jmap(juce::jlimit((float)r.getX(), (float)r.getRight(), x), (float)r.getX(), (float)r.getRight(), std::log10(20.0f), std::log10(20000.0f))); }
float LibrEQAudioProcessorEditor::gainToY(float db) const { auto r = graphBounds(); return juce::jmap(juce::jlimit(-24.0f, 24.0f, db), 24.0f, -24.0f, (float)r.getY(), (float)r.getBottom()); }
float LibrEQAudioProcessorEditor::yToGain(float y) const { auto r = graphBounds(); return juce::jmap(juce::jlimit((float)r.getY(), (float)r.getBottom(), y), (float)r.getY(), (float)r.getBottom(), 24.0f, -24.0f); }
void LibrEQAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) { selectedBand = -1; float nearest = 16.0f; for (int i = 0; i < 8; ++i) { const auto* f = processor.parameters.getRawParameterValue("freq" + juce::String(i + 1)); const auto* g = processor.parameters.getRawParameterValue("gain" + juce::String(i + 1)); const float d = e.position.getDistanceFrom({frequencyToX(f->load()), gainToY(g->load())}); if (d < nearest) { nearest = d; selectedBand = i; } } if (selectedBand >= 0) { const int type = juce::jlimit(0, 5, juce::roundToInt(processor.parameters.getRawParameterValue("type" + juce::String(selectedBand + 1))->load() * 5.0f)); typeBox.setSelectedItemIndex(type, juce::dontSendNotification); bandLabel.setText("Band " + juce::String(selectedBand + 1) + "  ·  " + typeBox.getItemText(type + 1), juce::dontSendNotification); bindSelectedBand(); } repaint(); }
void LibrEQAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e) { if (selectedBand < 0) return; const auto id = juce::String(selectedBand + 1); auto* f = processor.parameters.getParameter("freq" + id); auto* g = processor.parameters.getParameter("gain" + id); f->setValueNotifyingHost(f->convertTo0to1(xToFrequency(e.position.x))); g->setValueNotifyingHost(g->convertTo0to1(yToGain(e.position.y))); repaint(); }
