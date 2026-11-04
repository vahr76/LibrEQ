// Measures the audio actually processed by the engine. Does not depend on assert(), so it also runs in Release.
#include "DSP/Equalizer.h"
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace libreq;

namespace {
int failures = 0;

void expectNear(const char* name, double value, double expected, double tolerance) {
    const bool ok = std::abs(value - expected) <= tolerance;
    std::printf("%s %-46s %9.3f (expected %.3f +/- %.3f)\n", ok ? "ok  " : "FAIL", name, value, expected, tolerance);
    failures += ok ? 0 : 1;
}

void expectTrue(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    failures += ok ? 0 : 1;
}

double measureDb(Equalizer& eq, double frequency, double sr) {
    const int total = static_cast<int>(sr * 2.0), settle = static_cast<int>(sr * 1.5), block = 256;
    std::vector<float> left(static_cast<std::size_t>(block)), right(static_cast<std::size_t>(block));
    double inEnergy = 0.0, outEnergy = 0.0;
    for (int done = 0; done < total; done += block) {
        for (int i = 0; i < block; ++i) {
            const auto v = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * frequency * (done + i) / sr));
            left[static_cast<std::size_t>(i)] = v;
            right[static_cast<std::size_t>(i)] = v;
            if (done >= settle) inEnergy += static_cast<double>(v) * v;
        }
        float* channels[] = { left.data(), right.data() };
        eq.process(channels, 2, block);
        if (done >= settle)
            for (float v : left) outEnergy += static_cast<double>(v) * v;
    }
    return 10.0 * std::log10(outEnergy / inEnergy);
}

double single(Band band, double frequency, double sr = 48000.0) {
    Equalizer eq;
    eq.prepare(sr, 256, 2);
    eq.setBand(0, band);
    return measureDb(eq, frequency, sr);
}

Band make(FilterType type, float frequency, float gain = 0.0f, float q = 0.707f, Slope slope = Slope::Db12) {
    Band b;
    b.type = type; b.frequency = frequency; b.gainDb = gain; b.q = q; b.slope = slope;
    return b;
}
} // namespace

int main() {
    Equalizer flat;
    flat.prepare(48000.0, 256, 2);
    expectNear("unused engine is transparent", measureDb(flat, 1000.0, 48000.0), 0.0, 0.001);

    expectNear("bell +6 dB at centre", single(make(FilterType::Bell, 1000, 6, 1), 1000), 6.0, 0.02);
    expectNear("bell -12 dB at centre", single(make(FilterType::Bell, 1000, -12, 4), 1000), -12.0, 0.02);
    expectNear("bell far away stays flat", single(make(FilterType::Bell, 1000, 6, 4), 50), 0.0, 0.05);

    expectNear("low shelf +6 dB below corner", single(make(FilterType::LowShelf, 500, 6), 30), 6.0, 0.05);
    expectNear("low shelf flat above corner", single(make(FilterType::LowShelf, 500, 6), 10000), 0.0, 0.05);
    expectNear("high shelf -6 dB above corner", single(make(FilterType::HighShelf, 2000, -6), 20000), -6.0, 0.05);
    expectNear("high shelf flat below corner", single(make(FilterType::HighShelf, 2000, -6), 50), 0.0, 0.05);

    expectNear("high pass -3 dB at cutoff", single(make(FilterType::HighPass, 1000), 1000), -3.01, 0.05);
    expectNear("low pass -3 dB at cutoff", single(make(FilterType::LowPass, 1000), 1000), -3.01, 0.05);
    expectNear("high pass 48 dB/oct -3 dB at cutoff", single(make(FilterType::HighPass, 1000, 0, 0.707f, Slope::Db48), 1000), -3.01, 0.05);
    expectNear("high pass 12 dB/oct, two octaves down", single(make(FilterType::HighPass, 1600, 0, 0.707f, Slope::Db12), 400), -24.1, 0.5);
    expectNear("high pass 24 dB/oct, two octaves down", single(make(FilterType::HighPass, 1600, 0, 0.707f, Slope::Db24), 400), -48.2, 0.5);
    expectNear("high pass 36 dB/oct, two octaves down", single(make(FilterType::HighPass, 1600, 0, 0.707f, Slope::Db36), 400), -72.2, 0.7);
    expectNear("low pass 24 dB/oct, two octaves up", single(make(FilterType::LowPass, 500, 0, 0.707f, Slope::Db24), 2000), -48.2, 0.5);
    expectTrue("notch rejects its centre", single(make(FilterType::Notch, 1000, 0, 2), 1000) < -50.0);

    for (auto sr : { 44100.0, 96000.0 }) {
        const auto b = make(FilterType::Bell, 3000, 9, 2);
        const auto design = designBand(b, sr);
        for (double f : { 100.0, 2000.0, 3000.0, 5000.0, 15000.0 })
            expectNear("displayed curve equals processed audio", magnitudeDb(design, f, sr), single(b, f, sr), 0.02);
    }

    {
        Band off = make(FilterType::Bell, 1000, 12, 1);
        off.enabled = false;
        expectNear("disabled band does not affect audio", single(off, 1000), 0.0, 0.001);
    }

    {
        Equalizer eq;
        eq.prepare(48000.0, 256, 2);
        for (std::size_t i = 0; i < Equalizer::maxBands; ++i)
            eq.setBand(i, make(FilterType::Bell, 1000, 1, 1));
        expectNear("24 bands add up", measureDb(eq, 1000.0, 48000.0), 24.0, 0.05);
    }

    {
        Equalizer eq;
        eq.prepare(48000.0, 256, 1);
        eq.setBand(0, make(FilterType::Bell, 1000, 0, 1));
        std::vector<float> block(256);
        float* channels[] = { block.data() };
        bool finite = true;
        double phase = 0.0;
        for (int b = 0; b < 8000; ++b) {
            eq.setBand(0, make(FilterType::Bell, b % 2 ? 20.0f : 20000.0f, b % 3 ? 30.0f : -30.0f, b % 5 ? 0.1f : 20.0f));
            for (auto& v : block) { v = static_cast<float>(0.5 * std::sin(phase)); phase += 0.07; }
            eq.process(channels, 1, 256);
            for (float v : block) finite = finite && std::isfinite(v) && std::abs(v) < 1000.0f;
        }
        expectTrue("stays stable under extreme modulation (mono)", finite);
    }

    {
        Equalizer eq;
        eq.prepare(48000.0, 64, 2);
        eq.setBand(0, make(FilterType::Bell, 40000, 12, 1));
        expectTrue("frequency above Nyquist stays finite", std::isfinite(measureDb(eq, 1000.0, 48000.0)));
    }

    std::printf(failures == 0 ? "\nAll DSP checks passed\n" : "\n%d DSP checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
