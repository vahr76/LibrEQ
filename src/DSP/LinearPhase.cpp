#include "LinearPhase.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace libreq {

void ComplexFft::prepare(int size) {
    n = size;
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;
    reversed.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if ((i >> b) & 1)
                r |= 1 << (bits - 1 - b);
        reversed[static_cast<std::size_t>(i)] = r;
    }
    twiddles.resize(static_cast<std::size_t>(n / 2));
    for (int i = 0; i < n / 2; ++i)
        twiddles[static_cast<std::size_t>(i)] = std::polar(1.0, -2.0 * std::numbers::pi * i / n);
}

void ComplexFft::transform(std::complex<double>* data, bool inverse) const noexcept {
    for (int i = 0; i < n; ++i)
        if (const int r = reversed[static_cast<std::size_t>(i)]; r > i)
            std::swap(data[i], data[r]);

    for (int length = 2; length <= n; length *= 2) {
        const int half = length / 2, step = n / length;
        for (int start = 0; start < n; start += length) {
            for (int k = 0; k < half; ++k) {
                auto w = twiddles[static_cast<std::size_t>(k * step)];
                if (inverse)
                    w = std::conj(w);
                const double re = data[start + half + k].real() * w.real() - data[start + half + k].imag() * w.imag();
                const double im = data[start + half + k].real() * w.imag() + data[start + half + k].imag() * w.real();
                const std::complex<double> odd(re, im);
                data[start + half + k] = data[start + k] - odd;
                data[start + k] += odd;
            }
        }
    }

    if (inverse) {
        const double scale = 1.0 / n;
        for (int i = 0; i < n; ++i)
            data[i] *= scale;
    }
}

void LinearPhaseEngine::prepare(double sampleRate) {
    sr = sampleRate;
    firLength = sampleRate <= 50000.0 ? 4096 : sampleRate <= 100000.0 ? 8192 : 16384;
    fftSize = firLength * 2;
    hop = firLength / 2;
    firFft.prepare(firLength);
    blockFft.prepare(fftSize);

    const auto bins = static_cast<std::size_t>(fftSize / 2 + 1);
    for (auto& slot : slots) {
        slot.a.assign(bins, {});
        slot.b.assign(bins, {});
        slot.c.assign(bins, {});
        slot.d.assign(bins, {});
        slot.crossfeed = false;
    }
    for (auto& table : gainTables)
        table.assign(static_cast<std::size_t>(firLength / 2 + 1), 0.0);
    designBuffer.assign(static_cast<std::size_t>(fftSize), {});
    for (auto& h : history)
        h.assign(static_cast<std::size_t>(fftSize), 0.0f);
    for (auto& o : outputBlock)
        o.assign(static_cast<std::size_t>(hop), 0.0f);
    input.assign(static_cast<std::size_t>(fftSize), {});
    output.assign(static_cast<std::size_t>(fftSize), {});
    previousOutput.assign(static_cast<std::size_t>(fftSize), {});

    designSlot = 0;
    currentSlot = 1;
    previousSlot = 2;
    sharedSlot.store(3);

    // Start flat, in the slot that is already current.
    Bands flat {};
    for (auto& band : flat)
        band.enabled = false;
    design(flat);
    const int fresh = sharedSlot.exchange(currentSlot) & ~dirtyFlag;
    currentSlot = fresh;
    reset();
}

void LinearPhaseEngine::reset() noexcept {
    for (auto& h : history)
        std::fill(h.begin(), h.end(), 0.0f);
    for (auto& o : outputBlock)
        std::fill(o.begin(), o.end(), 0.0f);
    position = 0;
}

void LinearPhaseEngine::makeSpectrum(const std::vector<double>& gains, Spectrum& result) {
    // Frequency sampling at firLength points gives a zero-phase, circularly even response.
    auto* data = designBuffer.data();
    const int half = firLength / 2;
    for (int k = 0; k <= half; ++k)
        data[k] = gains[static_cast<std::size_t>(k)];
    for (int k = 1; k < half; ++k)
        data[firLength - k] = gains[static_cast<std::size_t>(k)];
    firFft.transform(data, true);

    // Centre it on firLength / 2 with a Hann window, then pad to the block size.
    std::vector<double> impulse(static_cast<std::size_t>(firLength));
    for (int i = 0; i < firLength; ++i) {
        const double window = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * i / firLength));
        impulse[static_cast<std::size_t>(i)] = data[(i + half) % firLength].real() * window;
    }
    for (int i = 0; i < fftSize; ++i)
        data[i] = i < firLength ? impulse[static_cast<std::size_t>(i)] : 0.0;
    blockFft.transform(data, false);
    std::copy(data, data + fftSize / 2 + 1, result.begin());
}

void LinearPhaseEngine::design(const Bands& bands) {
    auto& slot = slots[static_cast<std::size_t>(designSlot)];

    struct Active { BandDesign design; Placement placement; };
    std::vector<Active> active;
    bool crossfeed = false;
    for (const auto& band : bands) {
        if (! band.enabled || (usesGain(band.type) && std::abs(band.gainDb) < 1.0e-4f))
            continue;
        active.push_back({ designBand(band, sr), band.placement });
        crossfeed = crossfeed || band.placement == Placement::Mid || band.placement == Placement::Side;
    }

    const int half = firLength / 2;
    for (int k = 0; k <= half; ++k) {
        // Above 0 Hz only: a magnitude of zero at DC would be fine, but the log needs a floor.
        const double frequency = k == 0 ? 0.25 * sr / firLength : k * sr / firLength;
        double leftDb = 0.0, rightDb = 0.0, midDb = 0.0, sideDb = 0.0;
        for (const auto& item : active) {
            const double db = magnitudeDb(item.design, frequency, sr);
            switch (item.placement) {
                case Placement::Stereo: leftDb += db; rightDb += db; break;
                case Placement::Left: leftDb += db; break;
                case Placement::Right: rightDb += db; break;
                case Placement::Mid: midDb += db; break;
                case Placement::Side: sideDb += db; break;
            }
        }
        const double left = std::pow(10.0, leftDb / 20.0), right = std::pow(10.0, rightDb / 20.0);
        const double mid = std::pow(10.0, midDb / 20.0), side = std::pow(10.0, sideDb / 20.0);
        const auto index = static_cast<std::size_t>(k);
        gainTables[0][index] = 0.5 * (mid + side) * left;
        gainTables[1][index] = 0.5 * (mid - side) * right;
        gainTables[2][index] = 0.5 * (mid - side) * left;
        gainTables[3][index] = 0.5 * (mid + side) * right;
    }

    makeSpectrum(gainTables[0], slot.a);
    makeSpectrum(gainTables[3], slot.d);
    slot.crossfeed = crossfeed;
    if (crossfeed) {
        makeSpectrum(gainTables[1], slot.b);
        makeSpectrum(gainTables[2], slot.c);
    }

    designSlot = sharedSlot.exchange(designSlot | dirtyFlag) & ~dirtyFlag;
}

void LinearPhaseEngine::convolve(const Slot& slot, std::complex<double>* out) noexcept {
    const int half = fftSize / 2;
    for (int k = 0; k <= half; ++k) {
        const auto index = static_cast<std::size_t>(k);
        const auto mirror = input[static_cast<std::size_t>((fftSize - k) % fftSize)];
        // Both channels share one complex transform.
        const auto x = input[index];
        const auto left = 0.5 * (x + std::conj(mirror));
        const auto right = std::complex<double>(0.0, -0.5) * (x - std::conj(mirror));

        auto outLeft = slot.a[index] * left, outRight = slot.d[index] * right;
        if (slot.crossfeed) {
            outLeft += slot.b[index] * right;
            outRight += slot.c[index] * left;
        }
        const std::complex<double> j(0.0, 1.0);
        out[k] = outLeft + j * outRight;
        if (k > 0 && k < half)
            out[fftSize - k] = std::conj(outLeft) + j * std::conj(outRight);
    }
    blockFft.transform(out, true);
}

void LinearPhaseEngine::runHop(int numChannels) noexcept {
    for (int i = 0; i < fftSize; ++i) {
        const auto index = static_cast<std::size_t>(i);
        input[index] = { history[0][index], numChannels > 1 ? history[1][index] : 0.0f };
    }
    blockFft.transform(input.data(), false);

    bool fading = false;
    if ((sharedSlot.load(std::memory_order_acquire) & dirtyFlag) != 0) {
        const int fresh = sharedSlot.exchange(previousSlot, std::memory_order_acq_rel) & ~dirtyFlag;
        previousSlot = currentSlot;
        currentSlot = fresh;
        fading = true;
    }

    convolve(slots[static_cast<std::size_t>(currentSlot)], output.data());
    if (fading)
        convolve(slots[static_cast<std::size_t>(previousSlot)], previousOutput.data());

    const int first = fftSize - hop;
    for (int i = 0; i < hop; ++i) {
        auto sample = output[static_cast<std::size_t>(first + i)];
        if (fading) {
            const double t = (i + 0.5) / hop;
            sample = sample * t + previousOutput[static_cast<std::size_t>(first + i)] * (1.0 - t);
        }
        outputBlock[0][static_cast<std::size_t>(i)] = static_cast<float>(sample.real());
        outputBlock[1][static_cast<std::size_t>(i)] = static_cast<float>(sample.imag());
    }
}

void LinearPhaseEngine::process(float* const* channels, int numChannels, int numSamples) noexcept {
    const int nc = std::min(numChannels, 2);
    for (int i = 0; i < numSamples; ++i) {
        for (int ch = 0; ch < nc; ++ch) {
            const auto c = static_cast<std::size_t>(ch);
            const float in = channels[ch][i];
            channels[ch][i] = outputBlock[c][static_cast<std::size_t>(position)];
            history[c][static_cast<std::size_t>(fftSize - hop + position)] = in;
        }
        if (++position == hop) {
            position = 0;
            runHop(nc);
            for (int ch = 0; ch < nc; ++ch) {
                auto& h = history[static_cast<std::size_t>(ch)];
                std::copy(h.begin() + hop, h.end(), h.begin());
            }
        }
    }
}

} // namespace libreq
