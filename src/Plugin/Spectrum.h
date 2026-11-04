#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace libreq {

// push() runs on the audio thread (lock-free, no allocation); compute() on the UI thread.
class SpectrumTap {
public:
    static constexpr int fftOrder = 13;
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

// Turns raw FFT magnitudes into a smooth display curve, one value per pixel:
// a 1/12-octave weighted average that favours peaks, followed by a light Gaussian blur.
// A pure tone can read about 11 dB low on this curve; SpectrumTap::compute() stays calibrated.
inline void renderSpectrumDisplay(const std::vector<float>& binsDb, double binWidth,
                                  const std::vector<double>& pixelFrequencies, std::vector<float>& output) {
    const int numBins = static_cast<int>(binsDb.size());
    const int width = static_cast<int>(pixelFrequencies.size());
    output.resize(pixelFrequencies.size());
    if (numBins < 4 || width == 0)
        return;

    std::vector<double> powerMomentSum(static_cast<size_t>(numBins) + 1, 0.0);
    for (int i = 0; i < numBins; ++i) {
        const double power = std::pow(10.0, static_cast<double>(binsDb[static_cast<size_t>(i)]) / 10.0);
        powerMomentSum[static_cast<size_t>(i) + 1] = powerMomentSum[static_cast<size_t>(i)] + power * power * power * power;
    }

    const double halfBand = std::pow(2.0, 1.0 / 24.0);
    std::vector<float> raw(pixelFrequencies.size());
    for (int px = 0; px < width; ++px) {
        const double centre = pixelFrequencies[static_cast<size_t>(px)] / binWidth;
        const double low = centre / halfBand, high = centre * halfBand;
        float db;
        if (high - low < 2.0) {
            // Below a couple of bins per band, interpolate with a Catmull-Rom spline.
            const int b1 = juce::jlimit(1, numBins - 3, static_cast<int>(centre));
            const auto t = static_cast<float>(juce::jlimit(0.0, 1.0, centre - b1));
            const float p0 = binsDb[static_cast<size_t>(b1 - 1)], p1 = binsDb[static_cast<size_t>(b1)];
            const float p2 = binsDb[static_cast<size_t>(b1 + 1)], p3 = binsDb[static_cast<size_t>(b1 + 2)];
            db = 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                         + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
        } else {
            const int first = juce::jlimit(0, numBins - 1, static_cast<int>(std::floor(low)));
            const int last = juce::jlimit(first + 1, numBins, static_cast<int>(std::ceil(high)) + 1);
            const double mean = (powerMomentSum[static_cast<size_t>(last)] - powerMomentSum[static_cast<size_t>(first)]) / (last - first);
            db = static_cast<float>(2.5 * std::log10(juce::jmax(mean, 1.0e-56)));
        }
        raw[static_cast<size_t>(px)] = db;
    }

    constexpr int radius = 8;
    constexpr double sigma = 2.8;
    std::array<float, radius + 1> kernel {};
    float kernelSum = 0.0f;
    for (int i = 0; i <= radius; ++i) {
        kernel[static_cast<size_t>(i)] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
        kernelSum += kernel[static_cast<size_t>(i)] * (i == 0 ? 1.0f : 2.0f);
    }
    for (int px = 0; px < width; ++px) {
        float sum = 0.0f;
        for (int i = -radius; i <= radius; ++i)
            sum += raw[static_cast<size_t>(juce::jlimit(0, width - 1, px + i))] * kernel[static_cast<size_t>(std::abs(i))];
        output[static_cast<size_t>(px)] = sum / kernelSum;
    }
}

} // namespace libreq
