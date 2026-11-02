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
    for (int i = 1; i < 10; ++i) g.drawHorizontalLine(graph.getY() + i * graph.getHeight() / 10, (float)graph.getX(), (float)graph.getRight());
    for (int i = 1; i < 12; ++i) g.drawVerticalLine(graph.getX() + i * graph.getWidth() / 12, (float)graph.getY(), (float)graph.getBottom());
    juce::Path curve;
    curve.startNewSubPath((float)graph.getX(), (float)graph.getCentreY());
    curve.cubicTo(graph.getX() + graph.getWidth() * .25f, graph.getY() + graph.getHeight() * .47f, graph.getX() + graph.getWidth() * .35f, graph.getY() + graph.getHeight() * .57f, graph.getX() + graph.getWidth() * .5f, graph.getCentreY());
    curve.cubicTo(graph.getX() + graph.getWidth() * .7f, graph.getY() + graph.getHeight() * .43f, graph.getX() + graph.getWidth() * .82f, graph.getY() + graph.getHeight() * .54f, (float)graph.getRight(), graph.getCentreY());
    g.setColour(juce::Colour(0xff58d6bd));
    g.strokePath(curve, juce::PathStrokeType(2.4f));
    g.setColour(juce::Colour(0xff58d6bd));
    for (int i = 0; i < 8; ++i) g.fillEllipse((float)(graph.getX() + 70 + i * 105), (float)(graph.getCentreY() - (i % 3) * 16 - 5), 10.0f, 10.0f);
}

