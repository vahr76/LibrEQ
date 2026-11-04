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
    juce::ScopedJuceInitialiser_GUI gui;
    {
        LibrEQAudioProcessor processor;
        processor.setPlayConfigDetails(2, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);

        check("new instance is transparent", std::abs(rmsDbAfter(processor, 1000.0)) < 0.001);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
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

        // Choice parameters must reach the DSP with the right index.
        for (int i = 0; i < 6; ++i) {
            processor.band(0).type->setValueNotifyingHost(processor.band(0).type->convertTo0to1(static_cast<float>(i)));
            check("type parameter maps to the same filter", static_cast<int>(processor.readBand(0).type) == i);
        }
        processor.band(0).type->setValueNotifyingHost(0.0f);

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

        check("editor stays usable at minimum size", [&] { editor->setSize(760, 460); editor->refreshNow(); return graph.getWidth() > 0 && ! editor->createComponentSnapshot(editor->getLocalBounds()).isNull(); }());
        editor->setSize(1040, 620);
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
        base.reset();
    }
    std::printf(failures == 0 ? "\nAll UI checks passed\n" : "\n%d UI checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
