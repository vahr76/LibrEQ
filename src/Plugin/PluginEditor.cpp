#include "PluginEditor.h"

void LibrEQAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121419));
    g.setColour(juce::Colour(0xffe8edf2));
    g.setFont(juce::FontOptions(24.0f));
    g.drawText("LibrEQ", 24, 18, 220, 34, juce::Justification::left);
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(juce::Colour(0xff8d98a5));
    g.drawText("Open equalization for everyone", 25, 48, 240, 22, juce::Justification::left);
    auto graph = getLocalBounds().reduced(24).withTop(88).withBottom(getHeight() - 56);
    g.setColour(juce::Colour(0xff20252d));
    g.fillRoundedRectangle(graph.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff343b46));
    for (int i = 0; i <= 8; ++i) g.drawHorizontalLine(graph.getY() + i * graph.getHeight() / 8, (float)graph.getX(), (float)graph.getRight());
    for (int i = 0; i <= 10; ++i) g.drawVerticalLine(graph.getX() + i * graph.getWidth() / 10, (float)graph.getY(), (float)graph.getBottom());
    g.setColour(juce::Colour(0xff8994a1));
    g.setFont(juce::FontOptions(11.0f));
    for (int i = 0; i <= 8; ++i) { const auto db = 24 - i * 6; g.drawText(juce::String(db) + " dB", graph.getX() + 8, graph.getY() + i * graph.getHeight() / 8 - 8, 60, 16, juce::Justification::left); }
    const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : freqs) g.drawText(juce::String((int)f) + " Hz", (int)frequencyToX(f) - 24, graph.getBottom() - 24, 52, 16, juce::Justification::centred);
    juce::Path curve;
    curve.startNewSubPath((float)graph.getX(), gainToY(0.0f));
    for (int px = 0; px <= graph.getWidth(); px += 4) {
        const float f = xToFrequency((float)(graph.getX() + px));
        float db = 0.0f;
        for (int band = 0; band < 8; ++band) {
            const auto* gain = processor.parameters.getRawParameterValue("gain" + juce::String(band + 1));
            const auto* freq = processor.parameters.getRawParameterValue("freq" + juce::String(band + 1));
            const float distance = std::log2(std::max(20.0f, f) / std::max(20.0f, freq->load()));
            db += gain->load() * std::exp(-distance * distance * 5.0f);
        }
        curve.lineTo((float)(graph.getX() + px), gainToY(juce::jlimit(-24.0f, 24.0f, db)));
    }
    g.setColour(juce::Colour(0xff58d6bd));
    g.strokePath(curve, juce::PathStrokeType(2.4f));
    g.setColour(juce::Colour(0xff58d6bd));
    for (int i = 0; i < 8; ++i) {
        const auto* freq = processor.parameters.getRawParameterValue("freq" + juce::String(i + 1));
        const auto* gain = processor.parameters.getRawParameterValue("gain" + juce::String(i + 1));
        const float x = frequencyToX(freq->load()), y = gainToY(gain->load());
        g.setColour(i == selectedBand ? juce::Colour(0xffffffff) : juce::Colour(0xff58d6bd));
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour(juce::Colour(0xff121419));
        g.drawText(juce::String(i + 1), (int)x - 5, (int)y - 7, 10, 14, juce::Justification::centred);
    }
}

juce::Rectangle<int> LibrEQAudioProcessorEditor::graphBounds() const { return getLocalBounds().reduced(24).withTop(88).withBottom(getHeight() - 56); }
float LibrEQAudioProcessorEditor::frequencyToX(float f) const { auto r = graphBounds(); return (float)r.getX() + juce::jmap(std::log10(juce::jlimit(20.0f, 20000.0f, f)), std::log10(20.0f), std::log10(20000.0f), 0.0f, (float)r.getWidth()); }
float LibrEQAudioProcessorEditor::xToFrequency(float x) const { auto r = graphBounds(); return std::pow(10.0f, juce::jmap(juce::jlimit((float)r.getX(), (float)r.getRight(), x), (float)r.getX(), (float)r.getRight(), std::log10(20.0f), std::log10(20000.0f))); }
float LibrEQAudioProcessorEditor::gainToY(float db) const { auto r = graphBounds(); return juce::jmap(juce::jlimit(-24.0f, 24.0f, db), 24.0f, -24.0f, (float)r.getY(), (float)r.getBottom()); }
float LibrEQAudioProcessorEditor::yToGain(float y) const { auto r = graphBounds(); return juce::jmap(juce::jlimit((float)r.getY(), (float)r.getBottom(), y), (float)r.getY(), (float)r.getBottom(), 24.0f, -24.0f); }
void LibrEQAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) { selectedBand = -1; float nearest = 16.0f; for (int i = 0; i < 8; ++i) { const auto* f = processor.parameters.getRawParameterValue("freq" + juce::String(i + 1)); const auto* g = processor.parameters.getRawParameterValue("gain" + juce::String(i + 1)); const float d = e.position.getDistanceFrom({frequencyToX(f->load()), gainToY(g->load())}); if (d < nearest) { nearest = d; selectedBand = i; } } repaint(); }
void LibrEQAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e) { if (selectedBand < 0) return; const auto id = juce::String(selectedBand + 1); auto* f = processor.parameters.getParameter("freq" + id); auto* g = processor.parameters.getParameter("gain" + id); f->setValueNotifyingHost(f->convertTo0to1(xToFrequency(e.position.x))); g->setValueNotifyingHost(g->convertTo0to1(yToGain(e.position.y))); repaint(); }
