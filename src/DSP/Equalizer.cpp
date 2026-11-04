#include "Equalizer.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace libreq {
namespace {

constexpr double pi = std::numbers::pi;
constexpr double butterworthQ = std::numbers::sqrt2 / 2.0;

Biquad normalised(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// Standard second-order forms from the public RBJ Audio EQ Cookbook.
Biquad secondOrder(FilterType type, double frequency, double gainDb, double q, double sampleRate) noexcept {
    const double w = 2.0 * pi * frequency / sampleRate;
    const double cosW = std::cos(w);
    const double alpha = std::sin(w) / (2.0 * q);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double beta = 2.0 * std::sqrt(A) * alpha;

    switch (type) {
        case FilterType::LowShelf:
            return normalised(A * ((A + 1) - (A - 1) * cosW + beta),
                              2 * A * ((A - 1) - (A + 1) * cosW),
                              A * ((A + 1) - (A - 1) * cosW - beta),
                              (A + 1) + (A - 1) * cosW + beta,
                              -2 * ((A - 1) + (A + 1) * cosW),
                              (A + 1) + (A - 1) * cosW - beta);
        case FilterType::HighShelf:
            return normalised(A * ((A + 1) + (A - 1) * cosW + beta),
                              -2 * A * ((A - 1) + (A + 1) * cosW),
                              A * ((A + 1) + (A - 1) * cosW - beta),
                              (A + 1) - (A - 1) * cosW + beta,
                              2 * ((A - 1) - (A + 1) * cosW),
                              (A + 1) - (A - 1) * cosW - beta);
        case FilterType::HighPass:
            return normalised((1 + cosW) / 2, -(1 + cosW), (1 + cosW) / 2, 1 + alpha, -2 * cosW, 1 - alpha);
        case FilterType::LowPass:
            return normalised((1 - cosW) / 2, 1 - cosW, (1 - cosW) / 2, 1 + alpha, -2 * cosW, 1 - alpha);
        case FilterType::Notch:
            return normalised(1, -2 * cosW, 1, 1 + alpha, -2 * cosW, 1 - alpha);
        case FilterType::Bell:
            break;
    }
    return normalised(1 + alpha * A, -2 * cosW, 1 - alpha * A, 1 + alpha / A, -2 * cosW, 1 - alpha / A);
}

} // namespace

bool usesGain(FilterType type) noexcept {
    return type == FilterType::Bell || type == FilterType::LowShelf || type == FilterType::HighShelf;
}

bool usesSlope(FilterType type) noexcept {
    return type == FilterType::HighPass || type == FilterType::LowPass;
}

BandDesign designBand(const Band& band, double sampleRate) noexcept {
    const double frequency = std::clamp(static_cast<double>(band.frequency), 5.0, sampleRate * 0.49);
    const double q = std::max(0.05, static_cast<double>(band.q));
    BandDesign design;

    if (usesSlope(band.type)) {
        // A cascade of 2N Butterworth sections; the user's Q shapes the sharpest section.
        const int n = static_cast<int>(band.slope) + 1;
        design.count = n;
        for (int k = 0; k < n; ++k) {
            double sectionQ = 1.0 / (2.0 * std::cos((2 * k + 1) * pi / (4.0 * n)));
            if (k == n - 1)
                sectionQ *= q / butterworthQ;
            design.sections[static_cast<std::size_t>(k)] = secondOrder(band.type, frequency, 0.0, sectionQ, sampleRate);
        }
        return design;
    }

    design.sections[0] = secondOrder(band.type, frequency, band.gainDb, q, sampleRate);
    return design;
}

double magnitudeDb(const BandDesign& design, double frequency, double sampleRate) noexcept {
    const double w = 2.0 * pi * frequency / sampleRate;
    const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    double db = 0.0;
    for (int i = 0; i < design.count; ++i) {
        const auto& s = design.sections[static_cast<std::size_t>(i)];
        const double nr = s.b0 + s.b1 * c1 + s.b2 * c2, ni = s.b1 * s1 + s.b2 * s2;
        const double dr = 1.0 + s.a1 * c1 + s.a2 * c2, di = s.a1 * s1 + s.a2 * s2;
        const double ratio = (nr * nr + ni * ni) / std::max(dr * dr + di * di, 1.0e-300);
        db += 10.0 * std::log10(std::max(ratio, 1.0e-30));
    }
    return db;
}

void Equalizer::prepare(double sampleRate, int, int) {
    sr = sampleRate;
    smoothing = std::exp(-static_cast<double>(chunkSize) / (0.02 * sr));
    for (auto& slot : slots) {
        slot.snap = true;
        slot.current = slot.target;
        redesign(slot);
    }
    reset();
}

void Equalizer::reset() noexcept {
    for (auto& slot : slots)
        slot.state = {};
}

void Equalizer::redesign(Slot& slot) noexcept {
    slot.design = designBand(slot.current, sr);
    slot.active = usesGain(slot.current.type) ? std::abs(slot.current.gainDb) > 1.0e-4f : slot.current.enabled;
}

void Equalizer::setBand(std::size_t index, const Band& band) noexcept {
    if (index >= maxBands)
        return;

    auto& slot = slots[index];
    Band target = band;
    // Disabling a gain band fades it to 0 dB instead of clicking.
    if (usesGain(target.type) && !target.enabled)
        target.gainDb = 0.0f;
    if (slot.target == target && !slot.snap)
        return;

    slot.target = target;
    if (slot.snap || slot.current.type != target.type || slot.current.slope != target.slope) {
        slot.current = target;
        slot.state = {};
        slot.snap = false;
        slot.moving = false;
        redesign(slot);
    } else {
        slot.moving = true;
    }
}

void Equalizer::advanceSmoothing() noexcept {
    const auto approach = [this](float current, float target) {
        return static_cast<float>(target + (static_cast<double>(current) - target) * smoothing);
    };

    for (auto& slot : slots) {
        if (!slot.moving)
            continue;

        auto& c = slot.current;
        const auto& t = slot.target;
        c.frequency = std::exp(approach(std::log(c.frequency), std::log(t.frequency)));
        c.gainDb = approach(c.gainDb, t.gainDb);
        c.q = std::exp(approach(std::log(c.q), std::log(t.q)));
        c.enabled = t.enabled;

        const bool settled = std::abs(c.frequency / t.frequency - 1.0f) < 1.0e-3f
                             && std::abs(c.gainDb - t.gainDb) < 1.0e-3f
                             && std::abs(c.q / t.q - 1.0f) < 1.0e-3f;
        if (settled) {
            c = t;
            slot.moving = false;
        }
        redesign(slot);
    }
}

void Equalizer::process(float* const* channels, int numChannels, int numSamples) noexcept {
    const int nc = std::min(numChannels, 2);
    std::array<double, chunkSize> buffer;

    for (int offset = 0; offset < numSamples; offset += chunkSize) {
        const int n = std::min(chunkSize, numSamples - offset);
        advanceSmoothing();

        for (int ch = 0; ch < nc; ++ch) {
            float* samples = channels[ch] + offset;
            std::copy(samples, samples + n, buffer.begin());

            for (auto& slot : slots) {
                if (!slot.active)
                    continue;
                for (int i = 0; i < slot.design.count; ++i) {
                    const auto section = static_cast<std::size_t>(i);
                    const auto& f = slot.design.sections[section];
                    auto& s = slot.state[section][static_cast<std::size_t>(ch)];
                    double z1 = s.z1, z2 = s.z2;
                    for (int x = 0; x < n; ++x) {
                        auto& value = buffer[static_cast<std::size_t>(x)];
                        const double in = value;
                        const double out = f.b0 * in + z1;
                        z1 = f.b1 * in - f.a1 * out + z2;
                        z2 = f.b2 * in - f.a2 * out;
                        value = out;
                    }
                    s.z1 = z1;
                    s.z2 = z2;
                }
            }

            for (int x = 0; x < n; ++x)
                samples[x] = static_cast<float>(buffer[static_cast<std::size_t>(x)]);
        }
    }
}

} // namespace libreq
