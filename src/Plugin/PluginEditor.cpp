#include "PluginEditor.h"
#include <cmath>

namespace {
namespace colours {
const juce::Colour window { 0xff0f1218 }, panel { 0xff171b23 }, panelRaised { 0xff262c37 }, plot { 0xff0b0e13 };
const juce::Colour gridMajor = juce::Colours::white.withAlpha(0.075f), gridMinor = juce::Colours::white.withAlpha(0.03f), zero { 0xff3c4654 };
const juce::Colour text { 0xffe6ebf0 }, textDim { 0xff8a95a3 }, accent { 0xff58d6bd };
const juce::Colour preSpectrum { 0xff536273 }, postSpectrum { 0xff49b9a4 };
const juce::Colour meterHot { 0xfff0a95b }, meterClip { 0xffef5b5b };
} // namespace colours

constexpr float minFrequency = 20.0f, maxFrequency = 20000.0f, graphDb = 30.0f;
constexpr float spectrumTop = 0.0f, spectrumBottom = -90.0f, spectrumTiltPerOctave = 4.5f;
constexpr float axisRight = 34.0f, axisBottom = 20.0f;
constexpr float nodeRadius = 10.0f, qRingRadius = 27.0f;
constexpr float minQ = 0.1f, maxQ = 20.0f;
constexpr int meterWidth = 40, panelHeight = 104;

juce::Colour bandColour(int band) {
    return juce::Colour::fromHSV(std::fmod(0.47f + static_cast<float>(band) * 0.137f, 1.0f), 0.62f, 0.97f, 1.0f);
}

juce::String formatFrequency(float f) {
    return f >= 1000.0f ? juce::String(f / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz";
}

void setPlain(juce::RangedAudioParameter* parameter, float value) {
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void setPlainGesture(juce::RangedAudioParameter* parameter, float value) {
    parameter->beginChangeGesture();
    setPlain(parameter, value);
    parameter->endChangeGesture();
}

// A small picture of the filter shape, drawn inside a box centred on the node.
juce::Path filterIcon(libreq::FilterType type, juce::Point<float> c, float s) {
    juce::Path p;
    const float l = c.x - s, r = c.x + s, top = c.y - s * 0.62f, mid = c.y + s * 0.1f, low = c.y + s * 0.68f;
    switch (type) {
        case libreq::FilterType::Bell:
            p.startNewSubPath(l, low);
            p.cubicTo(c.x - s * 0.45f, low, c.x - s * 0.35f, top, c.x, top);
            p.cubicTo(c.x + s * 0.35f, top, c.x + s * 0.45f, low, r, low);
            break;
        case libreq::FilterType::LowShelf:
            p.startNewSubPath(l, top);
            p.cubicTo(c.x, top, c.x, low, r, low);
            break;
        case libreq::FilterType::HighShelf:
            p.startNewSubPath(l, low);
            p.cubicTo(c.x, low, c.x, top, r, top);
            break;
        case libreq::FilterType::HighPass:
            p.startNewSubPath(c.x - s * 0.55f, low);
            p.cubicTo(c.x - s * 0.1f, mid, c.x - s * 0.1f, top, r, top);
            break;
        case libreq::FilterType::LowPass:
            p.startNewSubPath(l, top);
            p.cubicTo(c.x + s * 0.1f, top, c.x + s * 0.1f, mid, c.x + s * 0.55f, low);
            break;
        case libreq::FilterType::BandPass:
        case libreq::FilterType::Notch:
            p.startNewSubPath(l, top);
            p.cubicTo(c.x - s * 0.5f, top, c.x - s * 0.3f, low, c.x, low);
            p.cubicTo(c.x + s * 0.3f, low, c.x + s * 0.5f, top, r, top);
            break;
    }
    return p;
}

float qToAngle(float q) {
    const float t = std::log(juce::jlimit(minQ, maxQ, q) / minQ) / std::log(maxQ / minQ);
    return juce::degreesToRadians(-135.0f + 270.0f * t);
}
} // namespace

LibrEQLookAndFeel::LibrEQLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, colours::textDim);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::arrowColourId, colours::textDim);
    setColour(juce::PopupMenu::backgroundColourId, colours::panelRaised);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour(juce::TextButton::textColourOffId, colours::textDim.brighter(0.25f));
    setColour(juce::TextButton::textColourOnId, colours::window);
}

juce::Font LibrEQLookAndFeel::getLabelFont(juce::Label& label) {
    const auto current = label.getFont();
    return current.isBold() ? fonts->bold(current.getHeight()) : fonts->regular(current.getHeight());
}

juce::Font LibrEQLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return fonts->regular(12.0f); }
juce::Font LibrEQLookAndFeel::getComboBoxFont(juce::ComboBox&) { return fonts->regular(12.5f); }
juce::Font LibrEQLookAndFeel::getPopupMenuFont() { return fonts->regular(13.0f); }

void LibrEQLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + position * (end - start);
    const bool active = slider.isMouseOverOrDragging();
    auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    if (! slider.isEnabled())
        colour = colours::textDim;

    const auto arcStroke = juce::PathStrokeType(active ? 3.4f : 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track, value;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(colours::panelRaised.brighter(0.15f));
    g.strokePath(track, arcStroke);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float origin = bipolar ? (start + end) * 0.5f : start;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(origin, angle), juce::jmax(origin, angle), true);
    if (active) {
        g.setColour(colour.withAlpha(0.25f));
        g.strokePath(value, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(active ? colour.brighter(0.2f) : colour);
    g.strokePath(value, arcStroke);

    const float capRadius = radius * 0.72f;
    const auto cap = juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f).withCentre(centre);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillEllipse(cap.translated(0.0f, 1.5f));
    g.setGradientFill(juce::ColourGradient(colours::panelRaised.brighter(active ? 0.45f : 0.3f), centre.x, cap.getY(),
                                           colours::panelRaised.darker(0.25f), centre.x, cap.getBottom(), false));
    g.fillEllipse(cap);
    g.setColour(colours::text);
    g.drawLine({ centre.getPointOnCircumference(capRadius * 0.45f, angle), centre.getPointOnCircumference(capRadius * 0.92f, angle) }, 2.0f);
}

void LibrEQLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId) : colours::panelRaised;
    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha(0.45f);
    if (down)
        fill = fill.darker(0.2f);
    else if (highlighted)
        fill = fill.brighter(0.18f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(button.getToggleState() ? 0.0f : 0.06f));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

void LibrEQLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) {
    const auto bounds = juce::Rectangle<int>(width, height).toFloat().reduced(0.5f);
    g.setColour(box.isMouseOver(true) ? colours::panelRaised.brighter(0.18f) : colours::panelRaised);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    juce::Path arrow;
    const auto x = static_cast<float>(width) - 15.0f, y = static_cast<float>(height) * 0.5f;
    arrow.addTriangle(x, y - 2.5f, x + 8.0f, y - 2.5f, x + 4.0f, y + 3.0f);
    g.setColour(colours::textDim);
    g.fillPath(arrow);
}

EqGraph::EqGraph(LibrEQAudioProcessor& p) : processor(p) {
    setWantsKeyboardFocus(true);
    setOpaque(true);
}

juce::Rectangle<float> EqGraph::plot() const {
    auto area = getLocalBounds().toFloat();
    area.removeFromRight(axisRight);
    area.removeFromBottom(axisBottom);
    return area;
}

float EqGraph::frequencyToX(float frequency) const {
    const auto r = plot();
    const float t = std::log(juce::jlimit(minFrequency, maxFrequency, frequency) / minFrequency) / std::log(maxFrequency / minFrequency);
    return r.getX() + t * r.getWidth();
}

float EqGraph::xToFrequency(float x) const {
    const auto r = plot();
    const float t = juce::jlimit(0.0f, 1.0f, (x - r.getX()) / r.getWidth());
    return minFrequency * std::pow(maxFrequency / minFrequency, t);
}

float EqGraph::gainToY(float db) const {
    const auto r = plot();
    return juce::jmap(db, graphDb, -graphDb, r.getY(), r.getBottom());
}

float EqGraph::yToGain(float y) const {
    const auto r = plot();
    return juce::jlimit(-graphDb, graphDb, juce::jmap(y, r.getY(), r.getBottom(), graphDb, -graphDb));
}

float EqGraph::dbToSpectrumY(float db) const {
    const auto r = plot();
    return juce::jmap(db, spectrumTop, spectrumBottom, r.getY(), r.getBottom());
}

juce::Point<float> EqGraph::nodePosition(int band) const {
    const auto settings = processor.scaledBand(band);
    return { frequencyToX(settings.frequency), gainToY(libreq::usesGain(settings.type) ? settings.gainDb : 0.0f) };
}

juce::Point<float> EqGraph::qHandlePosition(int band) const {
    return nodePosition(band).getPointOnCircumference(qRingRadius, qToAngle(processor.readBand(band).q));
}

bool EqGraph::hitsQHandle(juce::Point<float> position) const {
    return selected >= 0 && processor.isBandUsed(selected) && position.getDistanceFrom(qHandlePosition(selected)) < 7.0f;
}

int EqGraph::hitTestNode(juce::Point<float> position) const {
    int nearest = -1;
    float best = 14.0f;
    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        if (! processor.isBandUsed(i))
            continue;
        if (const float distance = position.getDistanceFrom(nodePosition(i)); distance < best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

void EqGraph::setSelected(int band) {
    if (band == selected)
        return;
    selected = band;
    selectionAppear = 0.0f;
    repaint();
    if (onSelectionChanged)
        onSelectionChanged(selected);
}

void EqGraph::setAnalyzerMode(AnalyzerMode mode) {
    analyzerMode = mode;
    processor.analyzerWanted.store(mode != AnalyzerMode::Off);
    preSmoothed.clear();
    postSmoothed.clear();
    repaint();
}

int EqGraph::addBand(juce::Point<float> position) {
    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        if (processor.isBandUsed(i))
            continue;
        const auto& p = processor.band(i);
        setPlainGesture(p.type, 0.0f);
        setPlainGesture(p.slope, 0.0f);
        setPlainGesture(p.q, 1.0f);
        setPlainGesture(p.frequency, xToFrequency(position.x));
        setPlainGesture(p.gain, yToGain(position.y) / juce::jmax(0.05f, processor.getScale()));
        setPlainGesture(p.placement, 0.0f);
        setPlainGesture(p.on, 1.0f);
        setPlainGesture(p.used, 1.0f);
        return i;
    }
    return -1;
}

void EqGraph::removeBand(int band) {
    if (band < 0)
        return;
    setPlainGesture(processor.band(band).used, 0.0f);
    if (selected == band)
        setSelected(-1);
    hovered = -1;
}

void EqGraph::showBandMenu(int band) {
    const auto settings = processor.readBand(band);
    juce::PopupMenu menu;
    const juce::StringArray types { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
    for (int i = 0; i < types.size(); ++i)
        menu.addItem(i + 1, types[i], true, static_cast<int>(settings.type) == i);
    menu.addSeparator();
    menu.addItem(100, "Band on", true, settings.enabled);
    menu.addItem(101, "Remove band");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                       [safe = juce::Component::SafePointer<EqGraph>(this), band](int result) {
                           if (safe == nullptr)
                               return;
                           const auto& p = safe->processor.band(band);
                           if (result >= 1 && result <= 6)
                               setPlainGesture(p.type, static_cast<float>(result - 1));
                           else if (result == 100)
                               setPlainGesture(p.on, p.onRaw->load() > 0.5f ? 0.0f : 1.0f);
                           else if (result == 101)
                               safe->removeBand(band);
                       });
}

void EqGraph::mouseMove(const juce::MouseEvent& e) {
    cursor = e.position;
    const int hit = hitTestNode(e.position);
    const bool overHandle = hitsQHandle(e.position);
    hovered = hit;
    setMouseCursor(overHandle ? juce::MouseCursor::UpDownLeftRightResizeCursor
                              : hit >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void EqGraph::mouseExit(const juce::MouseEvent&) {
    hovered = -1;
    cursor = { -1.0f, -1.0f };
    repaint();
}

void EqGraph::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (! e.mods.isPopupMenu() && hitsQHandle(e.position)) {
        draggingQ = true;
        dragStartQ = processor.readBand(selected).q;
        processor.band(selected).q->beginChangeGesture();
        return;
    }

    const int hit = hitTestNode(e.position);
    setSelected(hit);
    if (hit < 0)
        return;

    const auto& p = processor.band(hit);
    if (e.mods.isPopupMenu()) {
        showBandMenu(hit);
        return;
    }
    if (e.mods.isAltDown()) {
        setPlainGesture(p.on, p.onRaw->load() > 0.5f ? 0.0f : 1.0f);
        return;
    }
    p.frequency->beginChangeGesture();
    p.gain->beginChangeGesture();
    dragging = true;
}

void EqGraph::mouseDrag(const juce::MouseEvent& e) {
    cursor = e.position;
    if (draggingQ && selected >= 0) {
        const auto offset = e.getOffsetFromDragStart().toFloat();
        const float q = dragStartQ * std::pow(2.0f, (offset.x - offset.y) / 70.0f);
        setPlain(processor.band(selected).q, juce::jlimit(minQ, maxQ, q));
        repaint();
        return;
    }
    if (! dragging || selected < 0)
        return;
    const auto& p = processor.band(selected);
    setPlain(p.frequency, xToFrequency(e.position.x));
    if (libreq::usesGain(processor.readBand(selected).type))
        setPlain(p.gain, juce::jlimit(-30.0f, 30.0f, yToGain(e.position.y) / juce::jmax(0.05f, processor.getScale())));
    repaint();
}

void EqGraph::mouseUp(const juce::MouseEvent&) {
    if (draggingQ) {
        draggingQ = false;
        if (selected >= 0)
            processor.band(selected).q->endChangeGesture();
        return;
    }
    if (! dragging)
        return;
    dragging = false;
    if (selected >= 0) {
        processor.band(selected).frequency->endChangeGesture();
        processor.band(selected).gain->endChangeGesture();
    }
}

void EqGraph::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;
    if (const int hit = hitTestNode(e.position); hit >= 0)
        removeBand(hit);
    else if (plot().contains(e.position))
        if (const int added = addBand(e.position); added >= 0)
            setSelected(added);
}

void EqGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const int hit = hitTestNode(e.position);
    const int band = hit >= 0 ? hit : selected;
    if (band < 0)
        return;
    const auto& p = processor.band(band);
    setPlainGesture(p.q, juce::jlimit(minQ, maxQ, p.qRaw->load() * std::pow(2.0f, wheel.deltaY)));
    repaint();
}

bool EqGraph::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z')) {
        if (key.getModifiers().isShiftDown())
            processor.redo();
        else
            processor.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'y' || key.getKeyCode() == 'Y')) {
        processor.redo();
        return true;
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected >= 0) {
        removeBand(selected);
        return true;
    }
    return false;
}

void EqGraph::resized() {
    curvesDirty = true;
    preSmoothed.clear();
    postSmoothed.clear();
}

void EqGraph::updateSpectrum(libreq::SpectrumTap& tap, std::vector<float>& smoothed) {
    if (! tap.compute(scratch))
        return;

    const auto r = plot();
    const int width = juce::jmax(1, static_cast<int>(r.getWidth()));
    if (smoothed.size() != static_cast<size_t>(width))
        smoothed.assign(static_cast<size_t>(width), spectrumBottom);

    std::vector<double> frequencies(static_cast<size_t>(width));
    for (int px = 0; px < width; ++px)
        frequencies[static_cast<size_t>(px)] = static_cast<double>(xToFrequency(r.getX() + static_cast<float>(px)));

    std::vector<float> display;
    libreq::renderSpectrumDisplay(scratch, processor.currentSampleRate() / libreq::SpectrumTap::fftSize, frequencies, display);

    for (int px = 0; px < width; ++px) {
        const float db = display[static_cast<size_t>(px)]
                         + spectrumTiltPerOctave * static_cast<float>(std::log2(frequencies[static_cast<size_t>(px)] / 1000.0));
        auto& value = smoothed[static_cast<size_t>(px)];
        value += (db - value) * (db > value ? 0.45f : 0.12f);
    }
}

juce::Path EqGraph::spectrumCurve(const std::vector<float>& smoothed) const {
    const auto r = plot();
    constexpr int step = 5;
    const int count = static_cast<int>(smoothed.size());
    const auto point = [&](int px) {
        const int clamped = juce::jlimit(0, count - 1, px);
        const auto y = juce::jmin(r.getBottom() + 8.0f, dbToSpectrumY(smoothed[static_cast<size_t>(clamped)]));
        return juce::Point<float>(r.getX() + static_cast<float>(clamped), y);
    };

    // Catmull-Rom spline written as cubic Bezier segments.
    juce::Path path;
    path.startNewSubPath(point(0));
    for (int px = 0; px < count - 1; px += step) {
        const auto p0 = point(px - step), p1 = point(px), p2 = point(px + step), p3 = point(px + 2 * step);
        path.cubicTo(p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
    return path;
}

void EqGraph::drawSpectrum(juce::Graphics& g, const std::vector<float>& smoothed, juce::Colour colour, float fillAlpha, float lineAlpha, float lineWidth) const {
    const auto r = plot();
    const auto curve = spectrumCurve(smoothed);
    const auto fade = [&](float alpha) {
        // Bright near the top of the plot, fading to nothing at the bottom.
        juce::ColourGradient gradient(colour.withAlpha(alpha), r.getTopLeft(), colour.withAlpha(0.0f), r.getBottomLeft(), false);
        gradient.addColour(0.55, colour.withAlpha(alpha * 0.45f));
        gradient.addColour(0.82, colour.withAlpha(alpha * 0.1f));
        return gradient;
    };

    juce::Path fill(curve);
    fill.lineTo(r.getRight(), r.getBottom() + 8.0f);
    fill.lineTo(r.getX(), r.getBottom() + 8.0f);
    fill.closeSubPath();
    g.setGradientFill(fade(fillAlpha));
    g.fillPath(fill);

    g.setGradientFill(fade(lineAlpha));
    g.strokePath(curve, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EqGraph::refresh() {
    const double rate = processor.currentSampleRate();
    const auto r = plot();
    const int width = juce::jmax(1, static_cast<int>(r.getWidth()));
    bool changed = curvesDirty || ! juce::exactlyEqual(rate, cachedRate);

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto index = static_cast<size_t>(i);
        const auto settings = processor.scaledBand(i);
        const bool used = processor.isBandUsed(i);
        if (! (settings == cachedBands[index]) || used != cachedUsed[index]) {
            cachedBands[index] = settings;
            cachedUsed[index] = used;
            changed = true;
        }
        nodeAppear[index] += ((used ? 1.0f : 0.0f) - nodeAppear[index]) * 0.35f;
    }
    selectionAppear += (1.0f - selectionAppear) * 0.3f;

    if (changed) {
        cachedRate = rate;
        curvesDirty = false;
        totalCurve.assign(static_cast<size_t>(width), 0.0f);
        secondaryCurve.clear();
        bool hasSecondary = false;
        for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
            const auto& b = cachedBands[static_cast<size_t>(i)];
            hasSecondary = hasSecondary || (cachedUsed[static_cast<size_t>(i)] && b.enabled
                                            && (b.placement == libreq::Placement::Right || b.placement == libreq::Placement::Side));
        }
        if (hasSecondary)
            secondaryCurve.assign(static_cast<size_t>(width), 0.0f);
        for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
            const auto index = static_cast<size_t>(i);
            auto& curve = bandCurves[index];
            if (! cachedUsed[index]) {
                curve.clear();
                continue;
            }
            curve.resize(static_cast<size_t>(width));
            const auto design = libreq::designBand(cachedBands[index], rate);
            for (int px = 0; px < width; ++px) {
                const auto f = static_cast<double>(xToFrequency(r.getX() + static_cast<float>(px)));
                curve[static_cast<size_t>(px)] = static_cast<float>(libreq::magnitudeDb(design, f, rate));
                if (! cachedBands[index].enabled)
                    continue;
                // The main curve shows left or mid; a second one appears for right or side.
                const auto placement = cachedBands[index].placement;
                const auto value = curve[static_cast<size_t>(px)];
                if (placement == libreq::Placement::Stereo || placement == libreq::Placement::Left || placement == libreq::Placement::Mid)
                    totalCurve[static_cast<size_t>(px)] += value;
                if (hasSecondary && (placement == libreq::Placement::Stereo || placement == libreq::Placement::Right || placement == libreq::Placement::Side))
                    secondaryCurve[static_cast<size_t>(px)] += value;
            }
        }
    }

    if (analyzerMode != AnalyzerMode::Off) {
        if (analyzerMode == AnalyzerMode::InputOutput)
            updateSpectrum(processor.preSpectrum, preSmoothed);
        else
            processor.preSpectrum.compute(scratch); // Keep its queue empty.
        updateSpectrum(processor.postSpectrum, postSmoothed);
    }
    repaint();
}

void EqGraph::drawGrid(juce::Graphics& g) const {
    const auto r = plot();
    static constexpr float gridFrequencies[] { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    for (float f : gridFrequencies) {
        const int rounded = juce::roundToInt(f);
        const bool major = rounded == 100 || rounded == 1000 || rounded == 10000;
        g.setColour(major ? colours::gridMajor : colours::gridMinor);
        g.drawVerticalLine(juce::roundToInt(frequencyToX(f)), r.getY(), r.getBottom());
    }
    for (int db = -24; db <= 24; db += 12) {
        g.setColour(db == 0 ? colours::zero : colours::gridMinor);
        g.drawHorizontalLine(juce::roundToInt(gainToY(static_cast<float>(db))), r.getX(), r.getRight());
    }
}

juce::Path EqGraph::curvePath(const std::vector<float>& curve) const {
    const auto r = plot();
    juce::Path path;
    for (size_t px = 0; px < curve.size(); ++px) {
        const auto x = r.getX() + static_cast<float>(px);
        const auto y = juce::jlimit(r.getY() - 20.0f, r.getBottom() + 20.0f, gainToY(curve[px]));
        if (px == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    return path;
}

void EqGraph::drawBandCurves(juce::Graphics& g) const {
    const auto r = plot();
    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto& curve = bandCurves[static_cast<size_t>(i)];
        if (curve.empty() || ! cachedBands[static_cast<size_t>(i)].enabled)
            continue;
        const bool isSelected = i == selected, isHovered = i == hovered;
        const auto path = curvePath(curve);
        juce::Path fill(path);
        fill.lineTo(r.getRight(), gainToY(0.0f));
        fill.lineTo(r.getX(), gainToY(0.0f));
        fill.closeSubPath();
        g.setColour(bandColour(i).withAlpha(isSelected ? 0.2f : isHovered ? 0.14f : 0.07f));
        g.fillPath(fill);
        if (isSelected || isHovered) {
            g.setColour(bandColour(i).withAlpha(0.7f));
            g.strokePath(path, juce::PathStrokeType(1.2f));
        }
    }

    if (! totalCurve.empty()) {
        const auto total = curvePath(totalCurve);
        juce::Path fill(total);
        fill.lineTo(r.getRight(), gainToY(0.0f));
        fill.lineTo(r.getX(), gainToY(0.0f));
        fill.closeSubPath();
        g.setColour(colours::accent.withAlpha(0.055f));
        g.fillPath(fill);
        const auto rounded = [](float width) { return juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
        if (! secondaryCurve.empty()) {
            g.setColour(colours::meterHot.withAlpha(0.85f));
            g.strokePath(curvePath(secondaryCurve), rounded(1.4f));
        }
        g.setColour(colours::text.withAlpha(0.05f));
        g.strokePath(total, rounded(10.0f));
        g.setColour(colours::text.withAlpha(0.12f));
        g.strokePath(total, rounded(5.0f));
        g.setColour(colours::text);
        g.strokePath(total, rounded(1.9f));
    }
}

void EqGraph::drawNodes(juce::Graphics& g) const {
    const auto r = plot();
    const auto iconStroke = juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto appear = nodeAppear[static_cast<size_t>(i)];
        if (appear < 0.02f)
            continue;
        const auto position = nodePosition(i);
        const auto settings = processor.readBand(i);
        const bool used = processor.isBandUsed(i);
        const bool enabled = settings.enabled || ! used;
        const auto colour = enabled ? bandColour(i) : colours::textDim;
        const bool isSelected = i == selected && used, active = isSelected || i == hovered;
        const float radius = nodeRadius * appear * (active ? 1.08f : 1.0f);

        if (isSelected) {
            const float ring = qRingRadius * (0.75f + 0.25f * selectionAppear);
            const float start = juce::degreesToRadians(-135.0f), end = juce::degreesToRadians(135.0f);
            const float value = qToAngle(settings.q);
            juce::Path track, arc;
            track.addCentredArc(position.x, position.y, ring, ring, 0.0f, start, end, true);
            arc.addCentredArc(position.x, position.y, ring, ring, 0.0f, start, value, true);
            g.setColour(colours::text.withAlpha(0.14f * selectionAppear));
            g.strokePath(track, juce::PathStrokeType(2.0f));
            g.setColour(colour.withAlpha(0.9f * selectionAppear));
            g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto handle = position.getPointOnCircumference(ring, value);
            const float handleRadius = hitsQHandle(cursor) || draggingQ ? 6.5f : 4.5f;
            g.setColour(colours::text.withAlpha(selectionAppear));
            g.fillEllipse(juce::Rectangle<float>(handleRadius * 2.0f, handleRadius * 2.0f).withCentre(handle));
            g.setColour(colour.darker(0.2f));
            g.drawEllipse(juce::Rectangle<float>(handleRadius * 2.0f, handleRadius * 2.0f).withCentre(handle), 1.2f);
        } else if (active) {
            g.setColour(colour.withAlpha(0.18f));
            g.fillEllipse(juce::Rectangle<float>(38.0f, 38.0f).withCentre(position));
            g.setColour(colour.withAlpha(0.65f));
            g.drawEllipse(juce::Rectangle<float>(29.0f, 29.0f).withCentre(position), 1.1f);
        }

        const auto node = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(position);
        g.setColour(juce::Colours::black.withAlpha(0.45f * appear));
        g.fillEllipse(node.translated(0.0f, 1.5f).expanded(0.5f));
        g.setColour((enabled ? colour : colours::panelRaised).withMultipliedAlpha(appear));
        g.fillEllipse(node);
        g.setColour((isSelected ? colours::text : colour.darker(0.45f)).withMultipliedAlpha(appear));
        g.drawEllipse(node, isSelected ? 2.0f : 1.0f);
        g.setColour((enabled ? colours::window : colours::textDim).withMultipliedAlpha(appear));
        g.strokePath(filterIcon(settings.type, position, radius * 0.56f), iconStroke);

        g.setFont(fonts->bold(9.5f));
        g.setColour(colour.withMultipliedAlpha(appear));
        static constexpr const char* placementTags[] { "", " L", " R", " M", " S" };
        g.drawText(juce::String(i + 1) + placementTags[static_cast<int>(settings.placement)], juce::Rectangle<float>(position.x + radius * 0.7f, position.y - radius - 9.0f, 30.0f, 12.0f), juce::Justification::centredLeft);

        if (active && used) {
            juce::String info = formatFrequency(settings.frequency);
            if (libreq::usesGain(settings.type))
                info << "   " << (settings.gainDb > 0.0f ? "+" : "") << juce::String(settings.gainDb, 1) << " dB";
            info << "   Q " << juce::String(settings.q, 2);
            const bool below = position.y - r.getY() < 74.0f;
            auto box = juce::Rectangle<float>(162.0f, 22.0f).withCentre(position.translated(0.0f, below ? 48.0f : -48.0f));
            box.setX(juce::jlimit(r.getX() + 4.0f, r.getRight() - box.getWidth() - 4.0f, box.getX()));
            g.setColour(colours::panelRaised.withAlpha(0.94f));
            g.fillRoundedRectangle(box, 5.0f);
            g.setColour(colour.withAlpha(0.75f));
            g.fillRoundedRectangle(box.withWidth(3.0f), 2.0f);
            g.setColour(colours::text);
            g.setFont(fonts->regular(11.5f));
            g.drawText(info, box, juce::Justification::centred);
        }
    }
}

void EqGraph::drawCursor(juce::Graphics& g) const {
    const auto r = plot();
    if (! r.contains(cursor) || hovered >= 0 || dragging || draggingQ || hitsQHandle(cursor))
        return;

    g.setColour(colours::text.withAlpha(0.16f));
    g.drawVerticalLine(juce::roundToInt(cursor.x), r.getY(), r.getBottom());

    const float frequency = xToFrequency(cursor.x);
    juce::String info = formatFrequency(frequency) + "   " + libreq::noteLabel(static_cast<double>(frequency));
    const auto px = static_cast<size_t>(juce::jlimit(0, static_cast<int>(r.getWidth()) - 1, static_cast<int>(cursor.x - r.getX())));
    if (px < totalCurve.size()) {
        const float db = totalCurve[px];
        info << "   " << (db > 0.0f ? "+" : "") << juce::String(db, 1) << " dB";
    }
    auto box = juce::Rectangle<float>(cursor.x + 8.0f, r.getBottom() - 26.0f, 176.0f, 20.0f);
    if (box.getRight() > r.getRight() - 4.0f)
        box.setX(cursor.x - box.getWidth() - 8.0f);
    g.setColour(colours::panelRaised.withAlpha(0.92f));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(colours::text);
    g.setFont(fonts->regular(11.0f));
    g.drawText(info, box, juce::Justification::centred);
}

void EqGraph::drawAxes(juce::Graphics& g) const {
    const auto r = plot();
    g.setFont(fonts->regular(10.5f));
    g.setColour(colours::textDim);
    for (int db = -24; db <= 24; db += 12) {
        const auto y = gainToY(static_cast<float>(db));
        g.drawText((db > 0 ? "+" : "") + juce::String(db), juce::Rectangle<float>(r.getRight() + 3.0f, y - 8.0f, axisRight - 7.0f, 16.0f), juce::Justification::centredRight);
    }

    static constexpr int labels[] { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (int f : labels) {
        const auto text = f >= 1000 ? juce::String(f / 1000) + "k" : juce::String(f);
        auto box = juce::Rectangle<float>(40.0f, 15.0f).withY(r.getBottom() + 3.0f).withCentre({ frequencyToX(static_cast<float>(f)), r.getBottom() + 10.5f });
        auto justification = juce::Justification::centred;
        if (f == 20) { box.setX(r.getX() + 4.0f); justification = juce::Justification::centredLeft; }
        if (f == 20000) { box.setX(r.getRight() - box.getWidth() - 2.0f); justification = juce::Justification::centredRight; }
        g.drawText(text, box, justification);
    }
}

void EqGraph::paint(juce::Graphics& g) {
    g.fillAll(colours::window);
    const auto r = plot();
    g.setGradientFill(juce::ColourGradient(colours::plot.brighter(0.08f), r.getTopLeft(), colours::plot.darker(0.18f), r.getBottomLeft(), false));
    g.fillRect(r);
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());

    drawGrid(g);

    if (analyzerMode != AnalyzerMode::Off) {
        if (analyzerMode == AnalyzerMode::InputOutput && ! preSmoothed.empty())
            drawSpectrum(g, preSmoothed, colours::preSpectrum.brighter(0.5f), 0.34f, 0.8f, 1.0f);
        if (! postSmoothed.empty())
            drawSpectrum(g, postSmoothed, colours::postSpectrum.brighter(0.35f), 0.62f, 1.0f, 1.4f);

        g.setFont(fonts->regular(9.5f));
        g.setColour(colours::textDim.withAlpha(0.55f));
        for (int db = -20; db >= -80; db -= 20)
            g.drawText(juce::String(db), juce::Rectangle<float>(r.getX() + 6.0f, dbToSpectrumY(static_cast<float>(db)) - 7.0f, 26.0f, 14.0f), juce::Justification::centredLeft);
    }

    drawBandCurves(g);

    bool anyUsed = false;
    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i)
        anyUsed = anyUsed || processor.isBandUsed(i);
    if (! anyUsed) {
        const auto hint = r.withTrimmedBottom(r.getHeight() * 0.3f);
        g.setColour(colours::text.withAlpha(0.8f));
        g.setFont(fonts->bold(16.0f));
        g.drawText("Double-click to add a band", hint.translated(0.0f, -12.0f), juce::Justification::centred);
        g.setColour(colours::textDim);
        g.setFont(fonts->regular(12.0f));
        g.drawText("Drag nodes   |   Scroll or use the ring for Q   |   Alt-click toggles   |   Right-click for options", hint.translated(0.0f, 14.0f), juce::Justification::centred);
    }

    drawCursor(g);
    drawNodes(g);
    g.restoreState();
    drawAxes(g);
}

void OutputMeter::update(std::array<float, 2> peaks) {
    for (size_t ch = 0; ch < 2; ++ch) {
        const float db = juce::Decibels::gainToDecibels(peaks[ch], -100.0f);
        levelDb[ch] = db > levelDb[ch] ? db : juce::jmax(db, levelDb[ch] - 1.2f);
        if (db >= holdDb[ch]) {
            holdDb[ch] = db;
            holdFrames[ch] = 45;
        } else if (--holdFrames[ch] < 0) {
            holdDb[ch] = juce::jmax(levelDb[ch], holdDb[ch] - 0.8f);
        }
        maxDb = juce::jmax(maxDb, db);
    }
    repaint();
}

void OutputMeter::paint(juce::Graphics& g) {
    constexpr float top = 6.0f, topDb = 6.0f, bottomDb = -60.0f;
    auto area = getLocalBounds().toFloat().reduced(7.0f, 0.0f);
    area.removeFromBottom(axisBottom);

    g.setFont(fonts->bold(10.0f));
    g.setColour(maxDb > 0.0f ? colours::meterClip : colours::textDim);
    const float shownMax = std::abs(maxDb) < 0.05f ? 0.0f : maxDb;
    const auto maxText = maxDb <= -99.0f ? juce::String("-inf") : (shownMax > 0.0f ? "+" : "") + juce::String(shownMax, 1);
    g.drawText(maxText, getLocalBounds().toFloat().removeFromTop(20.0f).translated(0.0f, top - 3.0f), juce::Justification::centred);

    area.removeFromTop(24.0f);
    const auto toY = [&](float db) { return juce::jmap(juce::jlimit(bottomDb, topDb, db), topDb, bottomDb, area.getY(), area.getBottom()); };
    const float barWidth = (area.getWidth() - 3.0f) * 0.5f;

    for (size_t ch = 0; ch < 2; ++ch) {
        const auto bar = juce::Rectangle<float>(area.getX() + static_cast<float>(ch) * (barWidth + 3.0f), area.getY(), barWidth, area.getHeight());
        g.setColour(colours::panelRaised.withAlpha(0.55f));
        g.fillRoundedRectangle(bar, 2.0f);

        juce::ColourGradient gradient(colours::meterClip, 0.0f, toY(topDb), colours::accent.darker(0.15f), 0.0f, area.getBottom(), false);
        const double zeroAt = static_cast<double>(topDb) / static_cast<double>(topDb - bottomDb);
        gradient.addColour(zeroAt, colours::meterHot);
        gradient.addColour(zeroAt + 0.14, colours::accent);
        g.setGradientFill(gradient);
        const float y = toY(levelDb[ch]);
        if (levelDb[ch] > bottomDb)
            g.fillRoundedRectangle(bar.withTop(y), 2.0f);

        if (holdDb[ch] > bottomDb) {
            g.setColour(holdDb[ch] > 0.0f ? colours::meterClip : colours::text);
            g.fillRect(bar.withTop(toY(holdDb[ch])).withHeight(1.5f));
        }
    }
    g.setColour(colours::text.withAlpha(0.35f));
    g.drawHorizontalLine(juce::roundToInt(toY(0.0f)), area.getX(), area.getRight());
}

void BandPanel::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), 9.0f);
    g.setColour(colours::panel.withAlpha(0.93f));
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(accent.withAlpha(0.45f));
    g.drawRoundedRectangle(bounds, 9.0f, 1.0f);
}

LibrEQAudioProcessorEditor::LibrEQAudioProcessorEditor(LibrEQAudioProcessor& p) : AudioProcessorEditor(p), processor(p), graph(p) {
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(graph);
    addAndMakeVisible(meter);
    addChildComponent(panel);
    graph.onSelectionChanged = [this](int band) { bindBand(band); };

    const auto setupKnob = [this](juce::Component& parent, juce::Slider& knob, juce::Label& label, const juce::String& name, juce::Slider::TextEntryBoxPosition box, int boxWidth) {
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(box, false, boxWidth, 18);
        knob.setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
        knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        label.setText(name, juce::dontSendNotification);
        label.setFont(fonts->regular(10.5f));
        label.setJustificationType(juce::Justification::centred);
        parent.addAndMakeVisible(knob);
        parent.addAndMakeVisible(label);
    };
    setupKnob(*this, outputKnob, outputLabel, "Output", juce::Slider::TextBoxRight, 56);
    setupKnob(panel, frequencyKnob, frequencyLabel, "Frequency", juce::Slider::TextBoxBelow, 78);
    setupKnob(panel, gainKnob, gainLabel, "Gain", juce::Slider::TextBoxBelow, 78);
    setupKnob(panel, qKnob, qLabel, "Q", juce::Slider::TextBoxBelow, 78);
    outputKnob.setTextValueSuffix(" dB");
    frequencyKnob.setTextValueSuffix(" Hz");
    gainKnob.setTextValueSuffix(" dB");
    outputAttachment = std::make_unique<SliderAttachment>(processor.parameters, "output", outputKnob);

    for (auto* button : { &bypassButton, &bandOnButton }) {
        button->setClickingTogglesState(true);
        button->setColour(juce::TextButton::buttonOnColourId, colours::accent);
    }
    addAndMakeVisible(bypassButton);
    panel.addAndMakeVisible(bandOnButton);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, colours::meterHot);
    bypassAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "bypass", bypassButton);

    addAndMakeVisible(analyzerButton);
    analyzerButton.onClick = [this] { showAnalyzerMenu(); };
    setAnalyzerMode(static_cast<EqGraph::AnalyzerMode>(juce::jlimit(0, 2, processor.analyzerMode.load())));

    setupKnob(*this, scaleKnob, scaleLabel, "Scale", juce::Slider::TextBoxRight, 50);
    scaleKnob.setTextValueSuffix(" %");
    scaleAttachment = std::make_unique<SliderAttachment>(processor.parameters, "scale", scaleKnob);

    autoGainButton.setClickingTogglesState(true);
    autoGainButton.setColour(juce::TextButton::buttonOnColourId, colours::accent);
    addAndMakeVisible(autoGainButton);
    autoGainAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "autogain", autoGainButton);

    addAndMakeVisible(phaseButton);
    phaseButton.onClick = [this] { setPlainGesture(processor.getPhaseParameter(), processor.isLinearPhase() ? 0.0f : 1.0f); updateHeader(); };

    addAndMakeVisible(presetButton);
    presetButton.onClick = [this] { showPresetMenu(); };
    for (auto* button : { &slotAButton, &slotBButton }) {
        button->setColour(juce::TextButton::buttonOnColourId, colours::accent);
        addAndMakeVisible(*button);
    }
    slotAButton.onClick = [this] { processor.switchSlot(0); updateHeader(); };
    slotBButton.onClick = [this] { processor.switchSlot(1); updateHeader(); };
    addAndMakeVisible(slotCopyButton);
    slotCopyButton.onClick = [this] { processor.copyToOtherSlot(); };
    addAndMakeVisible(undoButton);
    undoButton.onClick = [this] { processor.undo(); updateHeader(); };
    addAndMakeVisible(redoButton);
    redoButton.onClick = [this] { processor.redo(); updateHeader(); };

    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, colours::meterHot);
    soloButton.onClick = [this] {
        const int band = graph.getSelected();
        processor.soloBand.store(soloButton.getToggleState() && band >= 0 ? band : -1);
    };
    panel.addAndMakeVisible(soloButton);
    placementBox.addItemList({ "Stereo", "Left", "Right", "Mid", "Side" }, 1);
    panel.addAndMakeVisible(placementBox);

    removeButton.onClick = [this] { graph.removeSelectedBand(); };
    panel.addAndMakeVisible(removeButton);

    typeBox.addItemList({ "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" }, 1);
    slopeBox.addItemList({ "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1);
    panel.addAndMakeVisible(typeBox);
    panel.addAndMakeVisible(slopeBox);

    bandLabel.setFont(fonts->bold(13.0f));
    bandLabel.setJustificationType(juce::Justification::centred);
    panel.addAndMakeVisible(bandLabel);

    setResizable(true, true);
    setResizeLimits(1120, 520, 2400, 1500);
    setSize(1160, 680);
    bindBand(-1);
    startTimerHz(30);
}

LibrEQAudioProcessorEditor::~LibrEQAudioProcessorEditor() {
    stopTimer();
    processor.analyzerWanted.store(false);
    processor.soloBand.store(-1);
    setLookAndFeel(nullptr);
}

void LibrEQAudioProcessorEditor::setAnalyzerMode(EqGraph::AnalyzerMode mode) {
    graph.setAnalyzerMode(mode);
    processor.analyzerMode.store(static_cast<int>(mode));
    analyzerButton.setButtonText(mode == EqGraph::AnalyzerMode::Off ? "Spectrum: Off"
                                 : mode == EqGraph::AnalyzerMode::Output ? "Spectrum: Out" : "Spectrum: I/O");
}

void LibrEQAudioProcessorEditor::showAnalyzerMenu() {
    const auto current = graph.getAnalyzerMode();
    juce::PopupMenu menu;
    menu.addItem(1, "Input + Output", true, current == EqGraph::AnalyzerMode::InputOutput);
    menu.addItem(2, "Output only", true, current == EqGraph::AnalyzerMode::Output);
    menu.addItem(3, "Off", true, current == EqGraph::AnalyzerMode::Off);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&analyzerButton),
                       [safe = juce::Component::SafePointer<LibrEQAudioProcessorEditor>(this)](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           safe->setAnalyzerMode(result == 1 ? EqGraph::AnalyzerMode::InputOutput
                                                 : result == 2 ? EqGraph::AnalyzerMode::Output : EqGraph::AnalyzerMode::Off);
                       });
}

void LibrEQAudioProcessorEditor::showPresetMenu() {
    juce::PopupMenu menu;
    const auto factory = LibrEQAudioProcessor::getFactoryPresetNames();
    menu.addSectionHeader("Factory");
    for (int i = 0; i < factory.size(); ++i)
        menu.addItem(1 + i, factory[i]);

    const auto user = processor.getUserPresets();
    if (! user.isEmpty()) {
        menu.addSectionHeader("User");
        for (int i = 0; i < user.size(); ++i)
            menu.addItem(100 + i, user[i].getFileNameWithoutExtension());
    }
    menu.addSeparator();
    menu.addItem(1000, "Save preset...");
    menu.addItem(1001, "Open preset file...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton),
                       [safe = juce::Component::SafePointer<LibrEQAudioProcessorEditor>(this), user](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           auto& self = *safe;
                           if (result >= 1000) {
                               const bool saving = result == 1000;
                               const auto folder = LibrEQAudioProcessor::getUserPresetFolder();
                               folder.createDirectory();
                               self.fileChooser = std::make_unique<juce::FileChooser>(saving ? "Save preset" : "Open preset", folder, "*.librq");
                               const int chooserFlags = (saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                                         : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles;
                               self.fileChooser->launchAsync(chooserFlags, [safe, saving](const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (safe == nullptr || file == juce::File())
                                       return;
                                   if (saving)
                                       safe->processor.savePreset(file.withFileExtension("librq"));
                                   else
                                       safe->processor.loadPreset(file);
                                   safe->updateHeader();
                               });
                           } else if (result >= 100) {
                               self.processor.loadPreset(user[result - 100]);
                           } else {
                               self.processor.loadFactoryPreset(result - 1);
                           }
                           self.updateHeader();
                       });
}

void LibrEQAudioProcessorEditor::updateHeader() {
    presetButton.setButtonText(processor.getPresetName());
    slotAButton.setToggleState(processor.getActiveSlot() == 0, juce::dontSendNotification);
    slotBButton.setToggleState(processor.getActiveSlot() == 1, juce::dontSendNotification);
    undoButton.setEnabled(processor.canUndo());
    redoButton.setEnabled(processor.canRedo());
    phaseButton.setButtonText(processor.isLinearPhase() ? "Linear Phase" : "Zero Latency");
    phaseButton.setColour(juce::TextButton::textColourOffId, processor.isLinearPhase() ? colours::accent : colours::textDim.brighter(0.25f));
}

void LibrEQAudioProcessorEditor::timerCallback() {
    processor.flushHistory();
    updateHeader();
    if (const int band = graph.getSelected(); band >= 0 && ! processor.isBandUsed(band))
        graph.setSelected(-1);
    if (const int band = graph.getSelected(); band >= 0 && processor.readBand(band).type != laidOutType)
        layoutPanel();
    graph.refresh();
    meter.update(processor.takeOutputPeaks());
    movePanel(false);
}

void LibrEQAudioProcessorEditor::bindBand(int band) {
    if (band == boundBand)
        return;
    boundBand = band;
    processor.soloBand.store(-1);
    soloButton.setToggleState(false, juce::dontSendNotification);
    placementAttachment.reset();
    frequencyAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    bandOnAttachment.reset();
    typeAttachment.reset();
    slopeAttachment.reset();

    panel.setVisible(band >= 0);
    if (band < 0)
        return;

    const auto id = [band](const char* base) { return LibrEQAudioProcessor::bandParameterId(base, band); };
    frequencyAttachment = std::make_unique<SliderAttachment>(processor.parameters, id("freq"), frequencyKnob);
    gainAttachment = std::make_unique<SliderAttachment>(processor.parameters, id("gain"), gainKnob);
    qAttachment = std::make_unique<SliderAttachment>(processor.parameters, id("q"), qKnob);
    bandOnAttachment = std::make_unique<ButtonAttachment>(processor.parameters, id("on"), bandOnButton);
    typeAttachment = std::make_unique<ComboAttachment>(processor.parameters, id("type"), typeBox);
    slopeAttachment = std::make_unique<ComboAttachment>(processor.parameters, id("slope"), slopeBox);
    placementAttachment = std::make_unique<ComboAttachment>(processor.parameters, id("placement"), placementBox);

    const auto colour = bandColour(band);
    bandLabel.setText("Band " + juce::String(band + 1), juce::dontSendNotification);
    bandLabel.setColour(juce::Label::textColourId, colour);
    panel.accent = colour;
    for (auto* knob : { &frequencyKnob, &gainKnob, &qKnob })
        knob->setColour(juce::Slider::rotarySliderFillColourId, colour);
    bandOnButton.setColour(juce::TextButton::buttonOnColourId, colour);

    layoutPanel();
    movePanel(true);
}

void LibrEQAudioProcessorEditor::layoutPanel() {
    const int band = graph.getSelected();
    if (band < 0)
        return;

    laidOutType = processor.readBand(band).type;
    const bool showGain = libreq::usesGain(laidOutType);
    const bool showSlope = libreq::usesSlope(laidOutType);
    gainKnob.setVisible(showGain);
    gainLabel.setVisible(showGain);
    slopeBox.setVisible(showSlope);

    constexpr int pad = 10, gap = 10, knobWidth = 80, columnWidth = 84, choiceWidth = 112;
    const int numKnobs = showGain ? 3 : 2;
    panel.setSize(pad * 2 + columnWidth + choiceWidth + gap * 2 + numKnobs * knobWidth, panelHeight);

    auto area = panel.getLocalBounds().reduced(pad, 8);
    auto column = area.removeFromLeft(columnWidth);
    bandLabel.setBounds(column.removeFromTop(24));
    column.removeFromTop(3);
    auto toggles = column.removeFromTop(26);
    bandOnButton.setBounds(toggles.removeFromLeft(toggles.getWidth() / 2 - 1));
    toggles.removeFromLeft(2);
    soloButton.setBounds(toggles);
    column.removeFromTop(3);
    removeButton.setBounds(column.removeFromTop(26));
    area.removeFromLeft(gap);

    auto choices = area.removeFromLeft(choiceWidth);
    const int rows = showSlope ? 3 : 2;
    const int rowHeight = 26, rowGap = 3;
    choices.removeFromTop((choices.getHeight() - (rows * rowHeight + (rows - 1) * rowGap)) / 2);
    typeBox.setBounds(choices.removeFromTop(rowHeight));
    choices.removeFromTop(rowGap);
    if (showSlope) {
        slopeBox.setBounds(choices.removeFromTop(rowHeight));
        choices.removeFromTop(rowGap);
    }
    placementBox.setBounds(choices.removeFromTop(rowHeight));
    area.removeFromLeft(gap);

    const auto place = [&area](juce::Slider& knob, juce::Label& label) {
        auto slot = area.removeFromLeft(knobWidth);
        label.setBounds(slot.removeFromTop(13));
        knob.setBounds(slot);
    };
    place(frequencyKnob, frequencyLabel);
    if (showGain)
        place(gainKnob, gainLabel);
    place(qKnob, qLabel);
}

void LibrEQAudioProcessorEditor::movePanel(bool immediately) {
    const int band = graph.getSelected();
    if (band < 0 || ! panel.isVisible())
        return;
    // Do not move the panel while its own controls are being used.
    if (! immediately && panel.isMouseOverOrDragging(true))
        return;

    const auto node = graph.nodePosition(band) + graph.getPosition().toFloat();
    const auto limits = graph.getBounds().toFloat().withTrimmedBottom(axisBottom).reduced(6.0f);
    // Dock against the bottom of the plot, or the top when the node is low, to keep the curves visible.
    const float height = static_cast<float>(panel.getHeight()), width = static_cast<float>(panel.getWidth());
    const float bottomDock = limits.getBottom() - height;
    const bool useTop = node.y > bottomDock - 44.0f;
    const auto target = juce::Point<float>(
        juce::jlimit(limits.getX(), juce::jmax(limits.getX(), limits.getRight() - axisRight - width), node.x - width * 0.5f),
        useTop ? limits.getY() : bottomDock);

    const auto current = panel.getPosition().toFloat();
    const auto next = immediately ? target : current + (target - current) * 0.3f;
    panel.setTopLeftPosition(next.toInt());
}

void LibrEQAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(colours::window);
    g.setColour(colours::panelRaised.withAlpha(0.55f));
    g.fillRect(0, 45, getWidth(), 1);
    g.setFont(fonts->bold(21.0f));
    g.setColour(colours::text);
    g.drawText("Libr", 16, 0, 44, 46, juce::Justification::centredLeft);
    const int width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(fonts->bold(21.0f), "Libr"));
    g.setColour(colours::accent);
    g.drawText("EQ", 16 + width, 0, 40, 46, juce::Justification::centredLeft);
}

void LibrEQAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(46).reduced(10, 8);
    meter.setBounds(area.removeFromRight(meterWidth));
    graph.setBounds(area);

    outputLabel.setBounds(header.removeFromRight(40));
    outputKnob.setBounds(header.removeFromRight(88));
    header.removeFromRight(4);
    scaleLabel.setBounds(header.removeFromRight(32));
    scaleKnob.setBounds(header.removeFromRight(82));
    header.removeFromRight(8);
    bypassButton.setBounds(header.removeFromRight(58));
    header.removeFromRight(5);
    autoGainButton.setBounds(header.removeFromRight(44));
    header.removeFromRight(5);
    phaseButton.setBounds(header.removeFromRight(92));
    header.removeFromRight(5);
    analyzerButton.setBounds(header.removeFromRight(108));

    header.removeFromLeft(74);
    presetButton.setBounds(header.removeFromLeft(126));
    header.removeFromLeft(6);
    slotAButton.setBounds(header.removeFromLeft(26));
    header.removeFromLeft(2);
    slotBButton.setBounds(header.removeFromLeft(26));
    header.removeFromLeft(2);
    slotCopyButton.setBounds(header.removeFromLeft(44));
    header.removeFromLeft(6);
    undoButton.setBounds(header.removeFromLeft(44));
    header.removeFromLeft(2);
    redoButton.setBounds(header.removeFromLeft(44));

    movePanel(true);
}
