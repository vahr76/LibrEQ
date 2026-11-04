#include "PluginEditor.h"
#include <cmath>

namespace {
namespace colours {
const juce::Colour window { 0xff0f1218 }, panel { 0xff171b23 }, panelRaised { 0xff222832 }, plot { 0xff0b0e13 };
const juce::Colour gridMajor { 0xff29303b }, gridMinor { 0xff191e26 }, zero { 0xff465160 };
const juce::Colour text { 0xffe6ebf0 }, textDim { 0xff8a95a3 }, accent { 0xff58d6bd };
const juce::Colour preSpectrum { 0xff4a5462 }, postSpectrum { 0xff3f8f83 };
} // namespace colours

constexpr float minFrequency = 20.0f, maxFrequency = 20000.0f, graphDb = 30.0f;
constexpr float spectrumTop = 0.0f, spectrumBottom = -90.0f, spectrumTiltPerOctave = 4.5f;
constexpr float axisRight = 38.0f, axisBottom = 20.0f;

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
    setColour(juce::TextButton::textColourOffId, colours::textDim);
    setColour(juce::TextButton::textColourOnId, colours::window);
}

void LibrEQLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + position * (end - start);
    const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);

    juce::Path track, value;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(colours::gridMajor);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float origin = bipolar ? (start + end) * 0.5f : start;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(origin, angle), juce::jmax(origin, angle), true);
    g.setColour(colour);
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(colours::panelRaised);
    g.fillEllipse(juce::Rectangle<float>(radius * 1.45f, radius * 1.45f).withCentre(centre));
    const auto tip = centre.getPointOnCircumference(radius * 0.68f, angle);
    g.setColour(colours::text);
    g.drawLine({ centre.getPointOnCircumference(radius * 0.32f, angle), tip }, 2.0f);
}

void LibrEQLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId) : colours::panelRaised;
    g.setColour(highlighted ? fill.brighter(0.12f) : fill);
    g.fillRoundedRectangle(bounds, 4.0f);
}

void LibrEQLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) {
    g.setColour(colours::panelRaised);
    g.fillRoundedRectangle(juce::Rectangle<int>(width, height).toFloat(), 4.0f);
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
    const auto settings = processor.readBand(band);
    return { frequencyToX(settings.frequency), gainToY(libreq::usesGain(settings.type) ? settings.gainDb : 0.0f) };
}

int EqGraph::hitTestNode(juce::Point<float> position) const {
    int nearest = -1;
    float best = 13.0f;
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
    repaint();
    if (onSelectionChanged)
        onSelectionChanged(selected);
}

void EqGraph::setAnalyzerVisible(bool visible) {
    analyzerVisible = visible;
    processor.analyzerWanted.store(visible);
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
        setPlainGesture(p.gain, yToGain(position.y));
        setPlainGesture(p.on, 1.0f);
        setPlainGesture(p.used, 1.0f);
        return i;
    }
    return -1;
}

void EqGraph::removeBand(int band) {
    if (band < 0)
        return;
    const auto& p = processor.band(band);
    setPlainGesture(p.used, 0.0f);
    setPlainGesture(p.gain, 0.0f);
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
    if (const int hit = hitTestNode(e.position); hit != hovered) {
        hovered = hit;
        setMouseCursor(hit >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqGraph::mouseExit(const juce::MouseEvent&) {
    hovered = -1;
    repaint();
}

void EqGraph::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
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
    if (! dragging || selected < 0)
        return;
    const auto& p = processor.band(selected);
    setPlain(p.frequency, xToFrequency(e.position.x));
    if (libreq::usesGain(processor.readBand(selected).type))
        setPlain(p.gain, yToGain(e.position.y));
    repaint();
}

void EqGraph::mouseUp(const juce::MouseEvent&) {
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
    const float q = p.qRaw->load() * std::pow(2.0f, wheel.deltaY);
    setPlainGesture(p.q, juce::jlimit(0.1f, 20.0f, q));
    repaint();
}

bool EqGraph::keyPressed(const juce::KeyPress& key) {
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

    const double rate = processor.currentSampleRate();
    const double binWidth = rate / libreq::SpectrumTap::fftSize;
    const int lastBin = static_cast<int>(scratch.size()) - 1;

    for (int px = 0; px < width; ++px) {
        const auto frequency = static_cast<double>(xToFrequency(r.getX() + static_cast<float>(px)));
        const auto nextFrequency = static_cast<double>(xToFrequency(r.getX() + static_cast<float>(px + 1)));
        const double first = frequency / binWidth, last = nextFrequency / binWidth;
        float db;
        if (last - first < 1.0) {
            // Low frequencies span less than one bin per pixel: interpolate.
            const int bin = juce::jlimit(0, lastBin - 1, static_cast<int>(first));
            const auto t = static_cast<float>(first - bin);
            db = scratch[static_cast<size_t>(bin)] * (1.0f - t) + scratch[static_cast<size_t>(bin + 1)] * t;
        } else {
            db = -140.0f;
            for (int bin = static_cast<int>(first); bin <= juce::jmin(lastBin, static_cast<int>(last)); ++bin)
                db = juce::jmax(db, scratch[static_cast<size_t>(bin)]);
        }
        db += spectrumTiltPerOctave * static_cast<float>(std::log2(frequency / 1000.0));

        auto& value = smoothed[static_cast<size_t>(px)];
        value = db > value ? value + (db - value) * 0.65f : value + (db - value) * 0.18f;
    }
}

juce::Path EqGraph::spectrumPath(const std::vector<float>& smoothed) const {
    const auto r = plot();
    juce::Path path;
    path.startNewSubPath(r.getX(), r.getBottom());
    for (size_t px = 0; px < smoothed.size(); ++px)
        path.lineTo(r.getX() + static_cast<float>(px), juce::jmin(r.getBottom(), dbToSpectrumY(smoothed[px])));
    path.lineTo(r.getRight(), r.getBottom());
    path.closeSubPath();
    return path;
}

void EqGraph::refresh() {
    const double rate = processor.currentSampleRate();
    const auto r = plot();
    const int width = juce::jmax(1, static_cast<int>(r.getWidth()));
    bool changed = curvesDirty || ! juce::exactlyEqual(rate, cachedRate);

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto settings = processor.readBand(i);
        const bool used = processor.isBandUsed(i);
        if (! (settings == cachedBands[static_cast<size_t>(i)]) || used != cachedUsed[static_cast<size_t>(i)]) {
            cachedBands[static_cast<size_t>(i)] = settings;
            cachedUsed[static_cast<size_t>(i)] = used;
            changed = true;
        }
    }

    if (changed) {
        cachedRate = rate;
        curvesDirty = false;
        totalCurve.assign(static_cast<size_t>(width), 0.0f);
        for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
            auto& curve = bandCurves[static_cast<size_t>(i)];
            if (! cachedUsed[static_cast<size_t>(i)]) {
                curve.clear();
                continue;
            }
            curve.resize(static_cast<size_t>(width));
            const auto design = libreq::designBand(cachedBands[static_cast<size_t>(i)], rate);
            for (int px = 0; px < width; ++px) {
                const auto f = static_cast<double>(xToFrequency(r.getX() + static_cast<float>(px)));
                curve[static_cast<size_t>(px)] = static_cast<float>(libreq::magnitudeDb(design, f, rate));
                if (cachedBands[static_cast<size_t>(i)].enabled)
                    totalCurve[static_cast<size_t>(px)] += curve[static_cast<size_t>(px)];
            }
        }
    }

    if (analyzerVisible) {
        updateSpectrum(processor.preSpectrum, preSmoothed);
        updateSpectrum(processor.postSpectrum, postSmoothed);
    }
    repaint();
}

void EqGraph::paint(juce::Graphics& g) {
    g.fillAll(colours::window);
    const auto r = plot();
    g.setColour(colours::plot);
    g.fillRect(r);
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());

    static constexpr float gridFrequencies[] { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    for (float f : gridFrequencies) {
        const int rounded = juce::roundToInt(f);
        const bool major = rounded == 100 || rounded == 1000 || rounded == 10000;
        g.setColour(major ? colours::gridMajor : colours::gridMinor);
        g.drawVerticalLine(juce::roundToInt(frequencyToX(f)), r.getY(), r.getBottom());
    }
    for (int db = -24; db <= 24; db += 6) {
        g.setColour(db == 0 ? colours::zero : colours::gridMinor);
        g.drawHorizontalLine(juce::roundToInt(gainToY(static_cast<float>(db))), r.getX(), r.getRight());
    }

    if (analyzerVisible) {
        if (! preSmoothed.empty()) {
            g.setColour(colours::preSpectrum.withAlpha(0.28f));
            g.fillPath(spectrumPath(preSmoothed));
        }
        if (! postSmoothed.empty()) {
            const auto path = spectrumPath(postSmoothed);
            g.setColour(colours::postSpectrum.withAlpha(0.32f));
            g.fillPath(path);
            g.setColour(colours::postSpectrum.withAlpha(0.75f));
            g.strokePath(path, juce::PathStrokeType(1.0f));
        }
    }

    const auto pathFor = [&](const std::vector<float>& curve) {
        juce::Path path;
        for (size_t px = 0; px < curve.size(); ++px) {
            const auto x = r.getX() + static_cast<float>(px);
            const auto y = juce::jlimit(r.getY() - 20.0f, r.getBottom() + 20.0f, gainToY(curve[px]));
            if (px == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        return path;
    };

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        const auto& curve = bandCurves[static_cast<size_t>(i)];
        if (curve.empty() || (i != selected && i != hovered))
            continue;
        auto path = pathFor(curve);
        juce::Path fill(path);
        fill.lineTo(r.getRight(), gainToY(0.0f));
        fill.lineTo(r.getX(), gainToY(0.0f));
        fill.closeSubPath();
        g.setColour(bandColour(i).withAlpha(i == selected ? 0.22f : 0.12f));
        g.fillPath(fill);
        g.setColour(bandColour(i).withAlpha(0.7f));
        g.strokePath(path, juce::PathStrokeType(1.2f));
    }

    if (! totalCurve.empty()) {
        const auto total = pathFor(totalCurve);
        g.setColour(colours::text.withAlpha(0.12f));
        g.strokePath(total, juce::PathStrokeType(5.0f));
        g.setColour(colours::text);
        g.strokePath(total, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    bool anyUsed = false;
    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i)
        anyUsed = anyUsed || processor.isBandUsed(i);
    if (! anyUsed) {
        g.setColour(colours::textDim);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText("Double-click to add a band", r.withTrimmedBottom(r.getHeight() * 0.3f), juce::Justification::centred);
    }

    for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i) {
        if (! processor.isBandUsed(i))
            continue;
        const auto position = nodePosition(i);
        const auto settings = processor.readBand(i);
        const auto colour = settings.enabled ? bandColour(i) : colours::textDim;
        const bool active = i == selected || i == hovered;
        if (active) {
            g.setColour(colour.withAlpha(0.28f));
            g.fillEllipse(juce::Rectangle<float>(30.0f, 30.0f).withCentre(position));
        }
        const auto node = juce::Rectangle<float>(18.0f, 18.0f).withCentre(position);
        g.setColour(settings.enabled ? colour : colours::panelRaised);
        g.fillEllipse(node);
        g.setColour(i == selected ? colours::text : colour.darker(0.4f));
        g.drawEllipse(node, i == selected ? 2.0f : 1.0f);
        g.setColour(settings.enabled ? colours::window : colours::textDim);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(i + 1), node, juce::Justification::centred);

        if (active) {
            juce::String info = formatFrequency(settings.frequency);
            if (libreq::usesGain(settings.type))
                info << "   " << (settings.gainDb > 0.0f ? "+" : "") << juce::String(settings.gainDb, 1) << " dB";
            info << "   Q " << juce::String(settings.q, 2);
            auto box = juce::Rectangle<float>(160.0f, 22.0f).withCentre(position.translated(0.0f, position.y - r.getY() < 48.0f ? 32.0f : -32.0f));
            box.setX(juce::jlimit(r.getX() + 4.0f, r.getRight() - box.getWidth() - 4.0f, box.getX()));
            g.setColour(colours::panelRaised.withAlpha(0.94f));
            g.fillRoundedRectangle(box, 4.0f);
            g.setColour(colours::text);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(info, box, juce::Justification::centred);
        }
    }
    g.restoreState();

    g.setFont(juce::FontOptions(11.0f));
    g.setColour(colours::textDim);
    for (int db = -24; db <= 24; db += 12) {
        const auto y = gainToY(static_cast<float>(db));
        g.drawText((db > 0 ? "+" : "") + juce::String(db), juce::Rectangle<float>(r.getRight() + 3.0f, y - 8.0f, axisRight - 7.0f, 16.0f), juce::Justification::centredRight);
    }
    static constexpr float labels[] { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (float f : labels) {
        const auto text = f >= 1000.0f ? juce::String(static_cast<int>(f / 1000.0f)) + "k" : juce::String(static_cast<int>(f));
        g.drawText(text, juce::Rectangle<float>(frequencyToX(f) - 20.0f, r.getBottom() + 3.0f, 40.0f, 15.0f), juce::Justification::centred);
    }
}

LibrEQAudioProcessorEditor::LibrEQAudioProcessorEditor(LibrEQAudioProcessor& p) : AudioProcessorEditor(p), processor(p), graph(p) {
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(graph);
    graph.onSelectionChanged = [this](int band) { bindBand(band); };

    const auto setupKnob = [this](juce::Slider& knob, juce::Label& label, const juce::String& name) {
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 20);
        knob.setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
        knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        label.setText(name, juce::dontSendNotification);
        label.setFont(juce::FontOptions(11.0f));
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knob);
        addAndMakeVisible(label);
    };
    setupKnob(outputKnob, outputLabel, "Output");
    setupKnob(frequencyKnob, frequencyLabel, "Frequency");
    setupKnob(gainKnob, gainLabel, "Gain");
    setupKnob(qKnob, qLabel, "Q");
    outputKnob.setTextValueSuffix(" dB");
    frequencyKnob.setTextValueSuffix(" Hz");
    gainKnob.setTextValueSuffix(" dB");
    outputAttachment = std::make_unique<SliderAttachment>(processor.parameters, "output", outputKnob);

    for (auto* button : { &analyzerButton, &bypassButton, &bandOnButton }) {
        button->setClickingTogglesState(true);
        button->setColour(juce::TextButton::buttonOnColourId, colours::accent);
        addAndMakeVisible(*button);
    }
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff0a95b));
    bypassAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "bypass", bypassButton);
    analyzerButton.setToggleState(true, juce::dontSendNotification);
    analyzerButton.onClick = [this] { graph.setAnalyzerVisible(analyzerButton.getToggleState()); };
    graph.setAnalyzerVisible(true);

    removeButton.onClick = [this] {
        if (const int band = graph.getSelected(); band >= 0) {
            juce::KeyPress del(juce::KeyPress::deleteKey);
            graph.keyPressed(del);
        }
    };
    addAndMakeVisible(removeButton);

    typeBox.addItemList({ "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" }, 1);
    slopeBox.addItemList({ "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1);
    addAndMakeVisible(typeBox);
    addAndMakeVisible(slopeBox);

    bandLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(bandLabel);
    emptyLabel.setText("Double-click the graph to add a band  ·  Drag nodes  ·  Scroll for Q  ·  Alt-click toggles  ·  Right-click for options", juce::dontSendNotification);
    emptyLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(emptyLabel);

    setResizable(true, true);
    setResizeLimits(760, 460, 2200, 1400);
    setSize(1040, 620);
    bindBand(-1);
    startTimerHz(30);
}

LibrEQAudioProcessorEditor::~LibrEQAudioProcessorEditor() {
    stopTimer();
    processor.analyzerWanted.store(false);
    setLookAndFeel(nullptr);
}

void LibrEQAudioProcessorEditor::timerCallback() {
    if (const int band = graph.getSelected(); band >= 0 && ! processor.isBandUsed(band))
        graph.setSelected(-1);
    updateStripVisibility();
    graph.refresh();
}

void LibrEQAudioProcessorEditor::bindBand(int band) {
    if (band == boundBand)
        return;
    boundBand = band;
    frequencyAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    bandOnAttachment.reset();
    typeAttachment.reset();
    slopeAttachment.reset();

    if (band >= 0) {
        const auto id = [band](const char* base) { return LibrEQAudioProcessor::bandParameterId(base, band); };
        frequencyAttachment = std::make_unique<SliderAttachment>(processor.parameters, id("freq"), frequencyKnob);
        gainAttachment = std::make_unique<SliderAttachment>(processor.parameters, id("gain"), gainKnob);
        qAttachment = std::make_unique<SliderAttachment>(processor.parameters, id("q"), qKnob);
        bandOnAttachment = std::make_unique<ButtonAttachment>(processor.parameters, id("on"), bandOnButton);
        typeAttachment = std::make_unique<ComboAttachment>(processor.parameters, id("type"), typeBox);
        slopeAttachment = std::make_unique<ComboAttachment>(processor.parameters, id("slope"), slopeBox);
        bandLabel.setText("Band " + juce::String(band + 1), juce::dontSendNotification);
        const auto colour = bandColour(band);
        bandLabel.setColour(juce::Label::textColourId, colour);
        for (auto* knob : { &frequencyKnob, &gainKnob, &qKnob }) {
            knob->setColour(juce::Slider::rotarySliderFillColourId, colour);
            knob->repaint();
        }
    }
    updateStripVisibility();
}

void LibrEQAudioProcessorEditor::updateStripVisibility() {
    const int band = graph.getSelected();
    const bool hasBand = band >= 0;
    const auto type = hasBand ? processor.readBand(band).type : libreq::FilterType::Bell;
    const bool showGain = hasBand && libreq::usesGain(type);
    const bool showSlope = hasBand && libreq::usesSlope(type);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &bandLabel, &bandOnButton, &removeButton, &typeBox, &frequencyKnob, &frequencyLabel, &qKnob, &qLabel })
        c->setVisible(hasBand);
    gainKnob.setVisible(showGain);
    gainLabel.setVisible(showGain);
    slopeBox.setVisible(showSlope);
    emptyLabel.setVisible(! hasBand);
}

void LibrEQAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(colours::window);
    g.setColour(colours::panel);
    g.fillRect(getLocalBounds().removeFromBottom(84));
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    g.drawText("Libr", 16, 0, 44, 46, juce::Justification::centredLeft);
    g.setColour(colours::accent);
    g.drawText("EQ", 51, 0, 40, 46, juce::Justification::centredLeft);
}

void LibrEQAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(46).reduced(10, 8);
    auto strip = area.removeFromBottom(84).reduced(14, 10);
    graph.setBounds(area);

    outputLabel.setBounds(header.removeFromRight(46));
    header.removeFromRight(0);
    outputKnob.setBounds(header.removeFromRight(112));
    header.removeFromRight(12);
    bypassButton.setBounds(header.removeFromRight(70));
    header.removeFromRight(6);
    analyzerButton.setBounds(header.removeFromRight(78));

    emptyLabel.setBounds(strip);

    bandLabel.setBounds(strip.removeFromLeft(72));
    auto buttons = strip.removeFromLeft(72).reduced(0, 2);
    bandOnButton.setBounds(buttons.removeFromTop(buttons.getHeight() / 2).withTrimmedBottom(2));
    removeButton.setBounds(buttons.withTrimmedTop(2));
    strip.removeFromLeft(14);

    auto choices = strip.removeFromLeft(124).reduced(0, 2);
    typeBox.setBounds(choices.removeFromTop(choices.getHeight() / 2).withTrimmedBottom(2));
    slopeBox.setBounds(choices.withTrimmedTop(2));
    strip.removeFromLeft(14);

    const auto placeKnob = [&strip](juce::Slider& knob, juce::Label& label) {
        auto slot = strip.removeFromLeft(158);
        label.setBounds(slot.removeFromTop(14).withWidth(52));
        knob.setBounds(slot);
    };
    placeKnob(frequencyKnob, frequencyLabel);
    placeKnob(gainKnob, gainLabel);
    placeKnob(qKnob, qLabel);
}
