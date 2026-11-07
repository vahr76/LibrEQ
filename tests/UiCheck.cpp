// Drives the real processor and editor without a DAW: state, mouse handling, audio and analyzer.
#include <JuceHeader.h>
#include "Plugin/PluginEditor.h"
#include <cstdio>

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    failures += ok ? 0 : 1;
}

static juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p, now, clicks, false);
}

static double rmsDbAfter(LibrEQAudioProcessor& p, double frequency) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    double phase = 0.0, in = 0.0, out = 0.0;
    for (int b = 0; b < 400; ++b) {
        for (int i = 0; i < 512; ++i) {
            const auto v = static_cast<float>(0.25 * std::sin(phase));
            phase += juce::MathConstants<double>::twoPi * frequency / 48000.0;
            buffer.setSample(0, i, v); buffer.setSample(1, i, v);
            if (b > 200) in += static_cast<double>(v) * v;
        }
        p.processBlock(buffer, midi);
        if (b > 200) for (int i = 0; i < 512; ++i) out += static_cast<double>(buffer.getSample(0, i)) * buffer.getSample(0, i);
    }
    return 10.0 * std::log10(out / in);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // Keep the output if a check crashes.
    juce::ScopedJuceInitialiser_GUI gui;
    {
        LibrEQAudioProcessor processor;
        processor.setPlayConfigDetails(2, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);

        check("new instance is transparent", std::abs(rmsDbAfter(processor, 1000.0)) < 0.001);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        if (argc > 2) {
            auto* first = dynamic_cast<LibrEQAudioProcessorEditor*>(base.get());
            for (int i = 0; i < 3; ++i) first->refreshNow();
            const auto image = first->createComponentSnapshot(first->getLocalBounds(), true, 2.0f);
            juce::File file(juce::String::fromUTF8(argv[2]));
            file.deleteFile();
            juce::FileOutputStream out(file);
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
        auto* editor = dynamic_cast<LibrEQAudioProcessorEditor*>(base.get());
        check("editor opens", editor != nullptr);
        auto& graph = editor->getGraph();
        editor->refreshNow();

        // Double-click at 1 kHz, +12 dB creates band 1 and processes real audio.
        const juce::Point<float> where(graph.frequencyToX(1000.0f), graph.gainToY(12.0f));
        graph.mouseDoubleClick(mouseAt(graph, where, {}, 2));
        check("double-click adds band 1", processor.isBandUsed(0) && graph.getSelected() == 0);
        check("new band: gain follows mouse", std::abs(processor.readBand(0).gainDb - 12.0f) < 0.2f);
        check("new band: frequency follows mouse", std::abs(processor.readBand(0).frequency - 1000.0f) < 15.0f);
        const auto measured = rmsDbAfter(processor, processor.readBand(0).frequency);
        check("audio follows the created band", std::abs(measured - processor.readBand(0).gainDb) < 0.1);

        // Drag band 1 down to -6 dB at 4 kHz.
        graph.mouseDown(mouseAt(graph, graph.nodePosition(0)));
        graph.mouseDrag(mouseAt(graph, { graph.frequencyToX(4000.0f), graph.gainToY(-6.0f) }));
        graph.mouseUp(mouseAt(graph, { graph.frequencyToX(4000.0f), graph.gainToY(-6.0f) }));
        check("drag moves frequency", std::abs(processor.readBand(0).frequency - 4000.0f) < 50.0f);
        check("drag moves gain", std::abs(processor.readBand(0).gainDb + 6.0f) < 0.2f);

        // The wheel changes Q.
        const auto qBefore = processor.readBand(0).q;
        juce::MouseWheelDetails wheel {}; wheel.deltaY = 1.0f;
        graph.mouseWheelMove(mouseAt(graph, graph.nodePosition(0)), wheel);
        check("wheel changes Q", processor.readBand(0).q > qBefore * 1.5f);

        // Alt-click toggles the band; audio returns to flat.
        graph.mouseDown(mouseAt(graph, graph.nodePosition(0), juce::ModifierKeys::altModifier));
        check("alt-click disables band", ! processor.readBand(0).enabled);
        check("disabled band is inaudible", std::abs(rmsDbAfter(processor, processor.readBand(0).frequency)) < 0.01);
        graph.mouseDown(mouseAt(graph, graph.nodePosition(0), juce::ModifierKeys::altModifier));

        // The ring handle changes Q in octaves per 70 pixels of drag.
        {
            graph.setSelected(0);
            const auto q0 = processor.readBand(0).q;
            const auto handle = graph.qHandlePosition(0);
            graph.mouseDown(mouseAt(graph, handle));
            auto drag = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), handle.translated(70.0f, 0.0f), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &graph, &graph, juce::Time::getCurrentTime(), handle, juce::Time::getCurrentTime(), 1, true);
            graph.mouseDrag(drag);
            graph.mouseUp(drag);
            check("ring handle doubles Q", std::abs(processor.readBand(0).q / q0 - 2.0f) < 0.05f);
            check("ring handle keeps frequency", std::abs(processor.readBand(0).frequency - 4000.0f) < 50.0f);
            processor.band(0).q->setValueNotifyingHost(processor.band(0).q->convertTo0to1(q0));
        }

        // The floating panel opens for the selected band and stays inside the graph.
        for (int i = 0; i < 12; ++i)
            editor->refreshNow();
        check("panel is visible while a band is selected", editor->getPanel().isVisible());
        check("panel stays inside the editor", editor->getLocalBounds().contains(editor->getPanel().getBounds()));
        {
            const auto node = graph.nodePosition(0) + graph.getPosition().toFloat();
            check("panel does not cover its node", ! editor->getPanel().getBounds().toFloat().expanded(10.0f).contains(node));
        }

        // Meter and helpers.
        rmsDbAfter(processor, 1000.0);
        {
            const auto peaks = processor.takeOutputPeaks();
            check("output peaks are reported", peaks[0] > 0.1f && peaks[1] > 0.1f);
            check("output peaks are consumed", processor.takeOutputPeaks()[0] < 1.0e-6f);
            editor->getMeter().update(peaks);
            check("meter tracks the highest level", editor->getMeter().getMaxDb() > -20.0f);
        }
        check("A4 note label", libreq::noteLabel(440.0) == "A4");
        check("C4 note label", libreq::noteLabel(261.6256) == "C4");
        check("note label shows cents", libreq::noteLabel(445.0) == "A4 +20c");
        check("embedded fonts load", juce::SharedResourcePointer<libreq::UiFonts>()->loaded());

        graph.setAnalyzerMode(EqGraph::AnalyzerMode::Off);
        check("analyzer can be turned off", ! processor.analyzerWanted.load());
        graph.setAnalyzerMode(EqGraph::AnalyzerMode::InputOutput);

        // Choice parameters must reach the DSP with the right index.
        for (int i = 0; i < 6; ++i) {
            processor.band(0).type->setValueNotifyingHost(processor.band(0).type->convertTo0to1(static_cast<float>(i)));
            check("type parameter maps to the same filter", static_cast<int>(processor.readBand(0).type) == i);
        }
        processor.band(0).type->setValueNotifyingHost(0.0f);

        // Shift makes drags finer.
        {
            graph.setSelected(0);
            const auto start = graph.nodePosition(0);
            const float f0 = processor.readBand(0).frequency;
            graph.mouseDown(mouseAt(graph, start, juce::ModifierKeys::shiftModifier));
            graph.mouseDrag(mouseAt(graph, start.translated(100.0f, 0.0f), juce::ModifierKeys::shiftModifier));
            graph.mouseUp(mouseAt(graph, start.translated(100.0f, 0.0f), juce::ModifierKeys::shiftModifier));
            const float expected = f0 * std::pow(1000.0f, 100.0f / graph.plot().getWidth() * 0.15f);
            check("shift drag moves 0.15 times as far", std::abs(processor.readBand(0).frequency / expected - 1.0f) < 0.01f);
            processor.band(0).frequency->setValueNotifyingHost(processor.band(0).frequency->convertTo0to1(4000.0f));
        }

        // Several bands: lasso, move together, Ctrl-click and delete.
        {
            graph.mouseDoubleClick(mouseAt(graph, { graph.frequencyToX(300.0f), graph.gainToY(6.0f) }, {}, 2));
            check("second band added", processor.isBandUsed(1));
            const auto r = graph.plot();
            graph.mouseDown(mouseAt(graph, r.getTopLeft().translated(4.0f, 4.0f)));
            graph.mouseDrag(mouseAt(graph, r.getBottomRight().translated(-4.0f, -4.0f)));
            graph.mouseUp(mouseAt(graph, r.getBottomRight().translated(-4.0f, -4.0f)));
            check("lasso selects both bands", graph.numMarked() == 2 && graph.getSelected() >= 0);

            const float a0 = processor.readBand(0).frequency, b0 = processor.readBand(1).frequency;
            const float ga = processor.readBand(0).gainDb, gb = processor.readBand(1).gainDb;
            const auto node = graph.nodePosition(1);
            graph.mouseDown(mouseAt(graph, node));
            graph.mouseDrag(mouseAt(graph, node.translated(60.0f, -30.0f)));
            graph.mouseUp(mouseAt(graph, node.translated(60.0f, -30.0f)));
            const float ra = processor.readBand(0).frequency / a0, rb = processor.readBand(1).frequency / b0;
            check("group moves by the same interval", ra > 1.1f && std::abs(ra / rb - 1.0f) < 0.002f);
            check("group moves by the same gain", std::abs((processor.readBand(0).gainDb - ga) - (processor.readBand(1).gainDb - gb)) < 0.02f);

            graph.mouseDown(mouseAt(graph, graph.nodePosition(0), juce::ModifierKeys::commandModifier));
            check("ctrl-click removes a band from the selection", graph.numMarked() == 1 && ! graph.isMarked(0));
            graph.mouseDown(mouseAt(graph, graph.nodePosition(0), juce::ModifierKeys::commandModifier));
            check("ctrl-click adds it back", graph.numMarked() == 2 && graph.getSelected() == 0);
            processor.flushHistory(); // The editor's timer does this ten times a second.
            juce::KeyPress del(juce::KeyPress::deleteKey);
            graph.keyPressed(del);
            check("delete removes the whole selection", ! processor.isBandUsed(0) && ! processor.isBandUsed(1) && graph.numMarked() == 0);
            processor.undo();
            check("undo brings the selection back", processor.isBandUsed(0) && processor.isBandUsed(1));
            processor.band(1).used->setValueNotifyingHost(0.0f);
            processor.band(0).frequency->setValueNotifyingHost(processor.band(0).frequency->convertTo0to1(4000.0f));
            processor.band(0).gain->setValueNotifyingHost(processor.band(0).gain->convertTo0to1(-6.0f));
        }

        // Display range: bands beyond it stay on the edge and keep their gain when moved sideways.
        {
            processor.band(0).gain->setValueNotifyingHost(processor.band(0).gain->convertTo0to1(20.0f));
            editor->setRange(6);
            editor->refreshNow();
            const auto r = graph.plot();
            const auto node = graph.nodePosition(0);
            check("range 6 dB keeps nodes inside", node.y >= r.getY() && node.y <= r.getBottom());
            graph.setSelected(0);
            graph.mouseDown(mouseAt(graph, node));
            graph.mouseDrag(mouseAt(graph, node.translated(30.0f, 0.0f)));
            graph.mouseUp(mouseAt(graph, node.translated(30.0f, 0.0f)));
            check("moving sideways keeps an out-of-range gain", std::abs(processor.readBand(0).gainDb - 20.0f) < 0.02f);
            check("range is stored", processor.displayRange.load() == 6);
            editor->setRange(30);
            processor.band(0).gain->setValueNotifyingHost(processor.band(0).gain->convertTo0to1(-6.0f));
            processor.band(0).frequency->setValueNotifyingHost(processor.band(0).frequency->convertTo0to1(4000.0f));
        }

        // Window size.
        {
            const auto contentWidth = editor->getContent().getWidth();
            editor->applyScale(0.75f);
            check("smaller size shrinks the window", editor->getWidth() == juce::roundToInt(contentWidth * 0.75f));
            check("smaller size keeps the layout", editor->getContent().getWidth() == contentWidth);
            check("scaled editor renders", ! editor->createComponentSnapshot(editor->getLocalBounds()).isNull());
            juce::MemoryBlock state;
            processor.getStateInformation(state);
            LibrEQAudioProcessor restored;
            restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            check("size is saved with the project", std::abs(restored.uiScale.load() - 0.75f) < 1.0e-4f);
            editor->applyScale(1.0f);
            check("normal size restores the window", editor->getWidth() == contentWidth);
        }

        check("frequency accepts 1.5k", std::abs(processor.band(0).frequency->convertFrom0to1(processor.band(0).frequency->getValueForText("1.5k")) - 1500.0f) < 0.5f);
        check("frequency accepts 2 kHz", std::abs(processor.band(0).frequency->convertFrom0to1(processor.band(0).frequency->getValueForText("2 kHz")) - 2000.0f) < 0.5f);

        // Delete removes it.
        graph.setSelected(0);
        juce::KeyPress del(juce::KeyPress::deleteKey);
        graph.keyPressed(del);
        check("delete key removes band", ! processor.isBandUsed(0) && graph.getSelected() == -1);
        check("removed band is inaudible", std::abs(rmsDbAfter(processor, 4000.0)) < 0.01);

        // State round trip through the host API.
        const auto setBand = [&](int i, int type, float f, float g, float q, int slope) {
            const auto& b = processor.band(i);
            b.type->setValueNotifyingHost(b.type->convertTo0to1(static_cast<float>(type)));
            b.frequency->setValueNotifyingHost(b.frequency->convertTo0to1(f));
            b.gain->setValueNotifyingHost(b.gain->convertTo0to1(g));
            b.q->setValueNotifyingHost(b.q->convertTo0to1(q));
            b.slope->setValueNotifyingHost(b.slope->convertTo0to1(static_cast<float>(slope)));
            b.on->setValueNotifyingHost(1.0f);
            b.used->setValueNotifyingHost(1.0f);
        };
        setBand(0, 3, 45.0f, 0.0f, 0.8f, 1);
        setBand(1, 1, 130.0f, 3.5f, 0.7f, 0);
        setBand(2, 0, 340.0f, -5.0f, 1.8f, 0);
        setBand(3, 0, 1800.0f, 4.0f, 0.9f, 0);
        setBand(4, 5, 5200.0f, 0.0f, 6.0f, 0);
        setBand(5, 2, 9500.0f, 5.0f, 0.7f, 0);
        processor.band(3).dynamic->setValueNotifyingHost(1.0f);
        processor.band(3).threshold->setValueNotifyingHost(processor.band(3).threshold->convertTo0to1(-50.0f));

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        LibrEQAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check("state restores bands", restored.isBandUsed(5) && restored.readBand(4).type == libreq::FilterType::Notch
                                       && std::abs(restored.readBand(2).gainDb + 5.0f) < 0.01f && restored.readBand(0).slope == libreq::Slope::Db24);

        // Analyzer: a 0 dBFS-ish sine must peak at its own frequency.
        {
            libreq::SpectrumTap tap;
            juce::AudioBuffer<float> b(1, libreq::SpectrumTap::fftSize);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample(0, i, static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 1500.0 * i / 48000.0)));
            tap.push(b.getArrayOfReadPointers(), 1, b.getNumSamples());
            std::vector<float> db;
            tap.compute(db);
            const auto peak = std::max_element(db.begin(), db.end());
            const double peakHz = std::distance(db.begin(), peak) * 48000.0 / libreq::SpectrumTap::fftSize;
            check("analyzer finds the right frequency", std::abs(peakHz - 1500.0) < 12.0);
            check("analyzer level is calibrated", std::abs(*peak) < 1.5f);

            const double binWidth = 48000.0 / libreq::SpectrumTap::fftSize;
            std::vector<double> frequencies(1000);
            for (size_t i = 0; i < frequencies.size(); ++i)
                frequencies[i] = 20.0 * std::pow(1000.0, static_cast<double>(i) / 999.0);
            std::vector<float> display;
            libreq::renderSpectrumDisplay(db, binWidth, frequencies, display);
            const auto displayPeak = std::max_element(display.begin(), display.end());
            const double displayHz = frequencies[static_cast<size_t>(std::distance(display.begin(), displayPeak))];
            check("smoothed display keeps a tone at its frequency", std::abs(std::log2(displayHz / 1500.0)) < 0.02);
            std::printf("     tone peak on display %.2f dB\n", static_cast<double>(*displayPeak));
            check("smoothed display keeps tone level within 12 dB", *displayPeak > -12.0f);

            // White noise: the display must be far less jagged than the raw bins resampled to pixels.
            juce::Random noiseSource(7);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample(0, i, noiseSource.nextFloat() * 0.5f - 0.25f);
            tap.push(b.getArrayOfReadPointers(), 1, b.getNumSamples());
            tap.compute(db);
            libreq::renderSpectrumDisplay(db, binWidth, frequencies, display);
            const auto roughness = [](const std::vector<float>& v) {
                double sum = 0.0;
                for (size_t i = 2; i < v.size(); ++i)
                    sum += std::abs(static_cast<double>(v[i]) - 2.0 * v[i - 1] + v[i - 2]);
                return sum / static_cast<double>(v.size());
            };
            std::vector<float> rawPixels(frequencies.size());
            for (size_t i = 0; i < frequencies.size(); ++i)
                rawPixels[i] = db[juce::jmin(db.size() - 1, static_cast<size_t>(frequencies[i] / binWidth))];
            const double before = roughness(rawPixels), after = roughness(display);
            std::printf("     roughness raw %.3f, display %.3f\n", before, after);
            check("noise display is at least 20x smoother than raw bins", after * 20.0 < before);
        }

        graph.setSelected(3);
        editor->refreshNow();

        graph.mouseExit(mouseAt(graph, { 0.0f, 0.0f }));
        check("editor stays usable at minimum size", [&] { editor->setSize(760, 460); editor->refreshNow(); return graph.getWidth() > 0 && ! editor->createComponentSnapshot(editor->getLocalBounds()).isNull(); }());
        editor->setSize(1160, 680);
        processor.band(5).placement->setValueNotifyingHost(processor.band(5).placement->convertTo0to1(4.0f));
        graph.setSelected(3);

        // Feed pink-ish noise so the screenshot shows a live analyzer.
        juce::Random random(42);
        juce::AudioBuffer<float> noise(2, 512);
        juce::MidiBuffer midi;
        float lp1 = 0, lp2 = 0;
        for (int b = 0; b < 40; ++b) {
            for (int i = 0; i < 512; ++i) {
                const float w = random.nextFloat() * 2 - 1;
                lp1 = 0.97f * lp1 + 0.03f * w; lp2 = 0.7f * lp2 + 0.3f * w;
                const float v = 0.5f * (lp1 * 2.0f + lp2 * 0.3f + w * 0.05f);
                noise.setSample(0, i, v); noise.setSample(1, i, v);
            }
            processor.processBlock(noise, midi);
            editor->refreshNow();
        }

        editor->refreshNow();

        if (argc > 1) {
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            juce::File file(juce::String::fromUTF8(argv[1]));
            file.deleteFile();
            juce::FileOutputStream out(file);
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
        if (argc > 3) {
            graph.setSelected(-1);
            graph.mouseMove(mouseAt(graph, { graph.frequencyToX(440.0f), graph.gainToY(18.0f) }));
            editor->refreshNow();
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            juce::File file(juce::String::fromUTF8(argv[3]));
            file.deleteFile();
            juce::FileOutputStream out(file);
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
        base.reset();
    }
    // Features that live in the processor, on fresh instances.
    {
        const auto prepare = [](LibrEQAudioProcessor& p, int channels = 2) {
            p.setPlayConfigDetails(channels, channels, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);
        };
        const auto gesture = [](juce::RangedAudioParameter* parameter, float plain) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
            parameter->endChangeGesture();
        };
        const auto bell = [&](LibrEQAudioProcessor& p, int i, float f, float g, float q) {
            const auto& b = p.band(i);
            gesture(b.type, 0.0f);
            gesture(b.frequency, f);
            gesture(b.gain, g);
            gesture(b.q, q);
            gesture(b.on, 1.0f);
            gesture(b.used, 1.0f);
        };

        // Undo and redo.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            check("history starts empty", ! p.canUndo() && ! p.canRedo());
            bell(p, 0, 1000.0f, 6.0f, 1.0f);
            p.flushHistory();
            check("adding a band is one undo step", p.canUndo());
            gesture(p.band(0).gain, -3.0f);
            p.flushHistory();
            p.undo();
            check("undo restores the previous gain", std::abs(p.readBand(0).gainDb - 6.0f) < 0.02f);
            p.redo();
            check("redo restores the later gain", std::abs(p.readBand(0).gainDb + 3.0f) < 0.02f);
            p.undo();
            p.undo();
            check("undo removes the band", ! p.isBandUsed(0));
            check("undo stack ends empty", ! p.canUndo());
        }

        // A/B.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 1000.0f, 6.0f, 1.0f);
            p.switchSlot(1);
            check("B starts as a copy of A", std::abs(p.readBand(0).gainDb - 6.0f) < 0.02f && p.getActiveSlot() == 1);
            gesture(p.band(0).gain, -9.0f);
            p.switchSlot(0);
            check("A keeps its value", std::abs(p.readBand(0).gainDb - 6.0f) < 0.02f);
            p.switchSlot(1);
            check("B keeps its value", std::abs(p.readBand(0).gainDb + 9.0f) < 0.02f);

            juce::MemoryBlock state;
            p.getStateInformation(state);
            LibrEQAudioProcessor restored;
            restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            check("active slot is saved", restored.getActiveSlot() == 1 && std::abs(restored.readBand(0).gainDb + 9.0f) < 0.02f);
            restored.switchSlot(0);
            check("other slot is saved", std::abs(restored.readBand(0).gainDb - 6.0f) < 0.02f);

            p.copyToOtherSlot();
            p.switchSlot(0);
            check("copy overwrites the other slot", std::abs(p.readBand(0).gainDb + 9.0f) < 0.02f);
        }

        // Presets.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            const auto names = LibrEQAudioProcessor::getFactoryPresetNames();
            check("factory presets exist", names.size() >= 5 && names[0] == "Init");
            p.loadFactoryPreset(names.indexOf("Vocal Presence"));
            check("factory preset loads bands", p.isBandUsed(3) && ! p.isBandUsed(4) && p.readBand(0).type == libreq::FilterType::HighPass);
            check("preset name is reported", p.getPresetName() == "Vocal Presence");
            p.undo();
            check("a preset is one undo step", ! p.isBandUsed(0));

            bell(p, 2, 700.0f, 4.5f, 2.0f);
            juce::TemporaryFile temp(".librq");
            check("user preset saves", p.savePreset(temp.getFile()));
            p.loadFactoryPreset(0);
            check("init clears the bands", ! p.isBandUsed(2));
            check("user preset loads", p.loadPreset(temp.getFile()) && p.isBandUsed(2) && std::abs(p.readBand(2).gainDb - 4.5f) < 0.02f);
            check("user preset rejects other files", ! p.loadPreset(juce::File("/nonexistent/none.librq")));
        }

        // Analyzer mode is stored with the project.
        {
            LibrEQAudioProcessor p;
            p.analyzerMode.store(1);
            juce::MemoryBlock state;
            p.getStateInformation(state);
            LibrEQAudioProcessor restored;
            restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            check("analyzer mode is saved", restored.analyzerMode.load() == 1);
        }

        // Global scale.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 1000.0f, 12.0f, 1.0f);
            gesture(p.parameters.getParameter("scale"), 50.0f);
            check("scale halves the band in the audio", std::abs(rmsDbAfter(p, 1000.0) - 6.0) < 0.1);
            gesture(p.parameters.getParameter("scale"), 0.0f);
            check("scale at zero is flat", std::abs(rmsDbAfter(p, 1000.0)) < 0.02);
        }

        // Auto gain.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 700.0f, 9.0f, 0.5f);
            p.runBackgroundDesign(true);
            const float comp = p.getAutoGainDb();
            check("boosting gives negative auto gain", comp < -1.0f && comp > -9.0f);
            gesture(p.parameters.getParameter("autogain"), 1.0f);
            check("auto gain reaches the audio", std::abs(rmsDbAfter(p, 700.0) - (9.0 + static_cast<double>(comp))) < 0.15);
            gesture(p.band(0).gain, -9.0f);
            p.runBackgroundDesign(true);
            check("cutting gives positive auto gain", p.getAutoGainDb() > 1.0f);
        }

        // Solo.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 1000.0f, 9.0f, 2.0f);
            bell(p, 1, 100.0f, 12.0f, 1.0f);
            p.soloBand.store(0);
            check("solo passes the band at unity", std::abs(rmsDbAfter(p, 1000.0)) < 0.2);
            check("solo removes other frequencies", rmsDbAfter(p, 100.0) < -20.0);
            p.soloBand.store(-1);
            check("leaving solo restores the curve", std::abs(rmsDbAfter(p, 1000.0) - 9.0) < 0.3);
        }

        // Bypass and mode changes must not click, in both modes.
        for (bool linear : { false, true }) {
            LibrEQAudioProcessor p;
            gesture(p.getPhaseParameter(), linear ? 1.0f : 0.0f);
            prepare(p);
            check(linear ? "linear mode reports its latency" : "zero latency mode reports none", p.getLatencySamples() == (linear ? 4096 : 0));
            bell(p, 0, 3000.0f, 6.0f, 1.0f);
            p.runBackgroundDesign(true);

            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            const double step = juce::MathConstants<double>::twoPi * 200.0 / 48000.0;
            float previous = 0.0f, worst = 0.0f;
            bool finite = true;
            const int toggleBlock = 100, restoreBlock = 180;
            for (int b = 0; b < 300; ++b) {
                if (b == toggleBlock) gesture(p.parameters.getParameter("bypass"), 1.0f);
                if (b == restoreBlock) gesture(p.parameters.getParameter("bypass"), 0.0f);
                for (int i = 0; i < 512; ++i) {
                    const auto v = static_cast<float>(0.5 * std::sin(phase));
                    phase += step;
                    buffer.setSample(0, i, v);
                    buffer.setSample(1, i, v);
                }
                p.processBlock(buffer, midi);
                for (int i = 0; i < 512; ++i) {
                    const float v = buffer.getSample(0, i);
                    finite = finite && std::isfinite(v);
                    if (b > 20) worst = juce::jmax(worst, std::abs(v - previous));
                    previous = v;
                }
            }
            const float sineSlope = static_cast<float>(0.5 * step);
            std::printf("     %s bypass: largest step %.5f, sine slope %.5f\n", linear ? "linear" : "zero-latency", static_cast<double>(worst), static_cast<double>(sineSlope));
            check("bypass does not click", finite && worst < sineSlope * 1.3f);
        }

        // Switching phase mode while playing.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 1000.0f, 6.0f, 1.0f);
            check("zero latency level", std::abs(rmsDbAfter(p, 1000.0) - 6.0) < 0.1);
            gesture(p.getPhaseParameter(), 1.0f);
            p.runBackgroundDesign(true);
            p.updateLatency();
            check("switching reports the latency", p.getLatencySamples() == 4096);
            check("linear phase level", std::abs(rmsDbAfter(p, 1000.0) - 6.0) < 0.2);
            gesture(p.getPhaseParameter(), 0.0f);
            p.updateLatency();
            check("switching back removes the latency", p.getLatencySamples() == 0);
            check("zero latency level returns", std::abs(rmsDbAfter(p, 1000.0) - 6.0) < 0.1);
        }

        // Mono through the whole processor.
        {
            LibrEQAudioProcessor p;
            p.setPlayConfigDetails(1, 1, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);
            bell(p, 0, 1000.0f, 6.0f, 1.0f);
            juce::AudioBuffer<float> mono(1, 512);
            juce::MidiBuffer midi;
            double phase = 0.0, in = 0.0, out = 0.0;
            for (int b = 0; b < 200; ++b) {
                for (int i = 0; i < 512; ++i) {
                    const auto v = static_cast<float>(0.25 * std::sin(phase));
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                    mono.setSample(0, i, v);
                    if (b > 100) in += static_cast<double>(v) * v;
                }
                p.processBlock(mono, midi);
                if (b > 100) for (int i = 0; i < 512; ++i) out += static_cast<double>(mono.getSample(0, i)) * mono.getSample(0, i);
            }
            check("mono is processed", std::abs(10.0 * std::log10(out / in) - 6.0) < 0.1);
        }

        // Large host blocks must be split safely.
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 1000.0f, 6.0f, 1.0f);
            juce::AudioBuffer<float> large(2, 8192);
            juce::MidiBuffer midi;
            large.clear();
            large.setSample(0, 0, 1.0f);
            large.setSample(1, 0, 1.0f);
            p.processBlock(large, midi);
            check("large blocks stay finite", std::isfinite(large.getMagnitude(0, 8192)) && large.getMagnitude(0, 8192) > 0.5f);
        }

        // Dynamic bands through the whole processor, in both modes.
        for (bool linear : { false, true }) {
            LibrEQAudioProcessor p;
            gesture(p.getPhaseParameter(), linear ? 1.0f : 0.0f);
            prepare(p);
            bell(p, 0, 3000.0f, -12.0f, 1.5f);
            gesture(p.band(0).dynamic, 1.0f);
            gesture(p.band(0).threshold, -30.0f);
            bell(p, 1, 300.0f, 6.0f, 1.0f);
            p.runBackgroundDesign(true);
            const char* mode = linear ? " (linear phase)" : " (zero latency)";
            check((juce::String("dynamic band acts on a loud signal") + mode).toRawUTF8(), std::abs(rmsDbAfter(p, 3000.0) + 12.0) < 0.3);
            check((juce::String("live gain is reported") + mode).toRawUTF8(), std::abs(p.getLiveGainDb(0) + 12.0f) < 0.3f);
            check((juce::String("static band still works beside it") + mode).toRawUTF8(), std::abs(rmsDbAfter(p, 300.0) - 6.0) < 0.3);
            gesture(p.band(0).threshold, 0.0f);
            check((juce::String("dynamic band rests below its threshold") + mode).toRawUTF8(), std::abs(rmsDbAfter(p, 3000.0)) < 0.3);
            if (linear)
                check("linear phase keeps its latency with dynamic bands", p.getLatencySamples() == 4096);
        }
        {
            LibrEQAudioProcessor p;
            prepare(p);
            bell(p, 0, 3000.0f, -12.0f, 1.5f);
            gesture(p.band(0).dynamic, 1.0f);
            p.runBackgroundDesign(true);
            check("auto gain ignores dynamic bands", std::abs(p.getAutoGainDb()) < 0.01f);
        }

        // CPU, 24 active bands, one second of stereo audio.
        for (bool linear : { false, true }) {
            LibrEQAudioProcessor p;
            gesture(p.getPhaseParameter(), linear ? 1.0f : 0.0f);
            prepare(p);
            for (int i = 0; i < LibrEQAudioProcessor::numBands; ++i)
                bell(p, i, 40.0f * std::pow(1.28f, static_cast<float>(i)), i % 2 ? 2.0f : -2.0f, 1.0f);
            p.runBackgroundDesign(true);
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            juce::Random random(3);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            int blocks = static_cast<int>(10.0 * 48000.0 / 512.0);
            for (int b = 0; b < blocks; ++b) {
                for (int i = 0; i < 512; ++i) { const float v = random.nextFloat() * 0.2f - 0.1f; buffer.setSample(0, i, v); buffer.setSample(1, i, v); }
                p.processBlock(buffer, midi);
            }
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            std::printf("     CPU %s, 24 bands: %.2f%% of one core\n", linear ? "linear" : "zero-latency", elapsed / 100.0);
        }
    }

    std::printf(failures == 0 ? "\nAll UI checks passed\n" : "\n%d UI checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
