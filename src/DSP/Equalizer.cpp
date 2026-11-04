#include "Equalizer.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace libreq {
namespace {

constexpr double pi = std::numbers::pi;
constexpr double butterworthQ = std::numbers::sqrt2 / 2.0;

struct Analog {
    double n2, n1, n0, d2, d1, d0;

    double powerAt(double x) const noexcept {
        const double nr = n0 - n2 * x * x, ni = n1 * x, dr = d0 - d2 * x * x, di = d1 * x;
        return (nr * nr + ni * ni) / std::max(dr * dr + di * di, 1.0e-300);
    }
};

// Second-order analog prototypes in s / w0.
Analog prototype(FilterType type, double gainDb, double q) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0), rootA = std::sqrt(A);
    switch (type) {
        case FilterType::LowShelf: return { A, A * rootA / q, A * A, A, rootA / q, 1.0 };
        case FilterType::HighShelf: return { A * A, A * rootA / q, A, 1.0, rootA / q, A };
        case FilterType::HighPass: return { 1.0, 0.0, 0.0, 1.0, 1.0 / q, 1.0 };
        case FilterType::LowPass: return { 0.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0 };
        case FilterType::Notch: return { 1.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0 };
        case FilterType::BandPass: return { 0.0, 1.0 / q, 0.0, 1.0, 1.0 / q, 1.0 };
        case FilterType::Bell: break;
    }
    return { 1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0 };
}

// Matches the analog magnitude instead of warping it towards Nyquist, in the manner of
// Vicanek's "Matched Second Order Digital Filters": the poles come from the impulse-invariant
// mapping, and the numerator from the power response at DC, Nyquist and a third frequency.
Biquad secondOrder(FilterType type, double frequency, double gainDb, double q, double sampleRate) noexcept {
    const double w0 = 2.0 * pi * frequency / sampleRate;
    const Analog h = prototype(type, gainDb, q);

    Biquad f;
    const double discriminant = h.d1 * h.d1 - 4.0 * h.d2 * h.d0;
    if (discriminant < 0.0) {
        const double re = -h.d1 / (2.0 * h.d2);
        const double angle = std::min(std::sqrt(-discriminant) / (2.0 * h.d2) * w0, 0.9 * pi);
        f.a1 = -2.0 * std::exp(re * w0) * std::cos(angle);
        f.a2 = std::exp(2.0 * re * w0);
    } else {
        const double root = std::sqrt(discriminant);
        const double p1 = (-h.d1 + root) / (2.0 * h.d2), p2 = (-h.d1 - root) / (2.0 * h.d2);
        f.a1 = -(std::exp(p1 * w0) + std::exp(p2 * w0));
        f.a2 = std::exp((p1 + p2) * w0);
    }

    const double A0 = (1.0 + f.a1 + f.a2) * (1.0 + f.a1 + f.a2);
    const double A1 = (1.0 - f.a1 + f.a2) * (1.0 - f.a1 + f.a2);
    const double A2 = -4.0 * f.a2;

    if (type == FilterType::Notch) {
        // Keep the zeros exactly on the unit circle, so the notch stays complete.
        const double cosW = std::cos(w0), scale = (1.0 + f.a1 + f.a2) / (2.0 - 2.0 * cosW);
        f.b0 = scale;
        f.b1 = -2.0 * cosW * scale;
        f.b2 = scale;
        return f;
    }

    // In phi0 = cos^2(w/2), phi1 = sin^2(w/2), a biquad's power is linear in (B0, B1, B2).
    const double w3 = std::min(w0, 0.72 * pi);
    const double phi1 = std::sin(w3 * 0.5) * std::sin(w3 * 0.5), phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;
    const double B0 = h.powerAt(0.0) * A0;
    const double B1 = h.powerAt(pi / w0) * A1;
    const double target = h.powerAt(w3 / w0) * (A0 * phi0 + A1 * phi1 + A2 * phi2);
    const double B2 = (target - B0 * phi0 - B1 * phi1) / phi2;

    // Minimum-phase factorisation of the numerator power.
    const double root0 = std::sqrt(std::max(B0, 0.0)), root1 = std::sqrt(std::max(B1, 0.0));
    const double w = 0.5 * (root0 + root1);
    f.b0 = 0.5 * (w + std::sqrt(std::max(w * w + B2, 0.0)));
    f.b1 = 0.5 * (root0 - root1);
    f.b2 = f.b0 > 0.0 ? -B2 / (4.0 * f.b0) : 0.0;
    return f;
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

double analogMagnitudeDb(const Band& band, double frequency) noexcept {
    const double q = std::max(0.05, static_cast<double>(band.q));
    const double x = frequency / std::max(1.0, static_cast<double>(band.frequency));
    if (usesSlope(band.type)) {
        const int n = static_cast<int>(band.slope) + 1;
        double db = 0.0;
        for (int k = 0; k < n; ++k) {
            double sectionQ = 1.0 / (2.0 * std::cos((2 * k + 1) * pi / (4.0 * n)));
            if (k == n - 1)
                sectionQ *= q / butterworthQ;
            db += 10.0 * std::log10(std::max(prototype(band.type, 0.0, sectionQ).powerAt(x), 1.0e-30));
        }
        return db;
    }
    return 10.0 * std::log10(std::max(prototype(band.type, band.gainDb, q).powerAt(x), 1.0e-30));
}

Band soloedBand(Band band) noexcept {
    if (band.type == FilterType::LowShelf) {
        band.type = FilterType::LowPass;
        band.slope = Slope::Db12;
    } else if (band.type == FilterType::HighShelf) {
        band.type = FilterType::HighPass;
        band.slope = Slope::Db12;
    } else if (band.type == FilterType::Bell || band.type == FilterType::Notch) {
        band.type = FilterType::BandPass;
    }
    band.gainDb = 0.0f;
    return band;
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
    if (slot.snap || slot.current.type != target.type || slot.current.slope != target.slope || slot.current.placement != target.placement) {
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
    std::array<std::array<double, chunkSize>, 2> buffer;

    const auto runBand = [&buffer](Slot& slot, int channel, int stateChannel, int n) noexcept {
        auto& samples = buffer[static_cast<std::size_t>(channel)];
        for (int i = 0; i < slot.design.count; ++i) {
            const auto section = static_cast<std::size_t>(i);
            const auto& f = slot.design.sections[section];
            auto& s = slot.state[section][static_cast<std::size_t>(stateChannel)];
            double z1 = s.z1, z2 = s.z2;
            for (int x = 0; x < n; ++x) {
                auto& value = samples[static_cast<std::size_t>(x)];
                const double in = value;
                const double out = f.b0 * in + z1;
                z1 = f.b1 * in - f.a1 * out + z2;
                z2 = f.b2 * in - f.a2 * out;
                value = out;
            }
            s.z1 = z1;
            s.z2 = z2;
        }
    };

    for (int offset = 0; offset < numSamples; offset += chunkSize) {
        const int n = std::min(chunkSize, numSamples - offset);
        advanceSmoothing();

        for (int ch = 0; ch < nc; ++ch)
            std::copy(channels[ch] + offset, channels[ch] + offset + n, buffer[static_cast<std::size_t>(ch)].begin());

        for (auto& slot : slots) {
            if (! slot.active)
                continue;

            const auto placement = slot.current.placement;
            if (nc < 2) {
                // On a mono signal the only content is left or mid.
                if (placement != Placement::Right && placement != Placement::Side)
                    runBand(slot, 0, 0, n);
            } else if (placement == Placement::Stereo) {
                runBand(slot, 0, 0, n);
                runBand(slot, 1, 1, n);
            } else if (placement == Placement::Left) {
                runBand(slot, 0, 0, n);
            } else if (placement == Placement::Right) {
                runBand(slot, 1, 1, n);
            } else {
                for (int x = 0; x < n; ++x) {
                    const auto i = static_cast<std::size_t>(x);
                    const double l = buffer[0][i], r = buffer[1][i];
                    buffer[0][i] = 0.5 * (l + r);
                    buffer[1][i] = 0.5 * (l - r);
                }
                const int encoded = placement == Placement::Mid ? 0 : 1;
                runBand(slot, encoded, encoded, n);
                for (int x = 0; x < n; ++x) {
                    const auto i = static_cast<std::size_t>(x);
                    const double mid = buffer[0][i], side = buffer[1][i];
                    buffer[0][i] = mid + side;
                    buffer[1][i] = mid - side;
                }
            }
        }

        for (int ch = 0; ch < nc; ++ch) {
            float* samples = channels[ch] + offset;
            for (int x = 0; x < n; ++x)
                samples[x] = static_cast<float>(buffer[static_cast<std::size_t>(ch)][static_cast<std::size_t>(x)]);
        }
    }
}

} // namespace libreq
