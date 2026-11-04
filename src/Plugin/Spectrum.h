#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace libreq {

// push() runs on the audio thread (lock-free, no allocation); compute() on the UI thread.
class SpectrumTap {
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;

    SpectrumTap() : fft(fftOrder), window(static_cast<size_t>(fftSize), juce::dsp::WindowingFunction<float>::hann, false) {}

    void push(const float* const* channels, int numChannels, int numSamples) noexcept {
        if (numChannels <= 0)
            return;
        const float scale = 1.0f / static_cast<float>(numChannels);
        const auto scope = fifo.write(numSamples);
        const auto write = [&](int start, int count, int offset) {
            for (int i = 0; i < count; ++i) {
                float sum = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sum += channels[ch][offset + i];
                ring[static_cast<size_t>(start + i)] = sum * scale;
            }
        };
        write(scope.startIndex1, scope.blockSize1, 0);
        write(scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    // Returns true when new magnitudes (dBFS, one per FFT bin) were produced.
    bool compute(std::vector<float>& magnitudesDb) {
        const int available = fifo.getNumReady();
        if (available == 0)
            return false;

        const auto scope = fifo.read(available);
        const auto read = [this](int start, int count) {
            for (int i = 0; i < count; ++i) {
                history[static_cast<size_t>(historyPos)] = ring[static_cast<size_t>(start + i)];
                historyPos = (historyPos + 1) % fftSize;
            }
        };
        read(scope.startIndex1, scope.blockSize1);
        read(scope.startIndex2, scope.blockSize2);

        for (int i = 0; i < fftSize; ++i)
            frame[static_cast<size_t>(i)] = history[static_cast<size_t>((historyPos + i) % fftSize)];
        std::fill(frame.begin() + fftSize, frame.end(), 0.0f);
        window.multiplyWithWindowingTable(frame.data(), static_cast<size_t>(fftSize));
        fft.performFrequencyOnlyForwardTransform(frame.data(), true);

        // A full-scale sine reads 0 dBFS with a Hann window (coherent gain 0.5).
        magnitudesDb.resize(static_cast<size_t>(fftSize / 2 + 1));
        const float norm = 4.0f / static_cast<float>(fftSize);
        for (size_t i = 0; i < magnitudesDb.size(); ++i)
            magnitudesDb[i] = juce::Decibels::gainToDecibels(frame[i] * norm, -140.0f);
        return true;
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    juce::AbstractFifo fifo { 1 << 15 };
    std::array<float, 1 << 15> ring {};
    std::array<float, fftSize> history {};
    std::array<float, fftSize * 2> frame {};
    int historyPos { 0 };
};

} // namespace libreq
