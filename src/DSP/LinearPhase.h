#pragma once
#include "Equalizer.h"
#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <vector>

namespace libreq {

// Radix-2 complex FFT. Inverse transforms are normalised.
class ComplexFft {
public:
    void prepare(int size);
    void transform(std::complex<double>* data, bool inverse) const noexcept;

private:
    int n { 0 };
    std::vector<int> reversed;
    std::vector<std::complex<double>> twiddles;
};

// Applies the magnitude of all bands as one symmetric FIR filter, using overlap-save.
// design() runs on one background thread and process() on the audio thread; they share
// no locks. prepare() and reset() must not run at the same time as either of them.
class LinearPhaseEngine {
public:
    using Bands = std::array<Band, Equalizer::maxBands>;

    void prepare(double sampleRate);
    void reset() noexcept;
    void design(const Bands& bands);
    void process(float* const* channels, int numChannels, int numSamples) noexcept;
    int getLatencySamples() const noexcept { return hop + firLength / 2; }

private:
    using Spectrum = std::vector<std::complex<double>>;

    // Stereo matrix per frequency: left = a L + b R, right = c L + d R.
    struct Slot {
        Spectrum a, b, c, d;
        bool crossfeed { false };
    };

    void makeSpectrum(const std::vector<double>& gains, Spectrum& output);
    void runHop(int numChannels) noexcept;
    void convolve(const Slot&, std::complex<double>* output) noexcept;

    double sr { 48000.0 };
    int firLength { 0 }, fftSize { 0 }, hop { 0 };
    ComplexFft firFft, blockFft;

    std::array<Slot, 4> slots;
    int designSlot { 0 }, currentSlot { 1 }, previousSlot { 2 };
    std::atomic<int> sharedSlot { 3 };
    static constexpr int dirtyFlag = 8;

    std::array<std::vector<double>, 4> gainTables;
    std::vector<std::complex<double>> designBuffer;

    std::array<std::vector<float>, 2> history, outputBlock;
    std::vector<std::complex<double>> input, output, previousOutput;
    int position { 0 };
};

} // namespace libreq
