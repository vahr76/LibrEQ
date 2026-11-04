// Measures the audio actually processed by the engine. Does not depend on assert(), so it also runs in Release.
#include "DSP/Equalizer.h"
#include "DSP/LinearPhase.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <memory>
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

// Level of each output channel for a stereo sine, in dB relative to the input.
struct StereoLevels { double left, right; };

template <typename Process>
StereoLevels measureStereo(Process&& process, double frequency, double sr, double leftAmp, double rightAmp) {
    const int total = static_cast<int>(sr * 2.0), settle = static_cast<int>(sr * 1.5), block = 256;
    std::vector<float> left(256), right(256);
    double inL = 0.0, inR = 0.0, outL = 0.0, outR = 0.0;
    for (int done = 0; done < total; done += block) {
        for (int i = 0; i < block; ++i) {
            const double v = std::sin(2.0 * std::numbers::pi * frequency * (done + i) / sr);
            left[static_cast<std::size_t>(i)] = static_cast<float>(leftAmp * v);
            right[static_cast<std::size_t>(i)] = static_cast<float>(rightAmp * v);
            if (done >= settle) { inL += leftAmp * leftAmp * v * v; inR += rightAmp * rightAmp * v * v; }
        }
        float* channels[] = { left.data(), right.data() };
        process(channels);
        if (done >= settle) {
            for (float v : left) outL += static_cast<double>(v) * v;
            for (float v : right) outR += static_cast<double>(v) * v;
        }
    }
    return { 10.0 * std::log10(outL / inL), 10.0 * std::log10(outR / inR) };
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

    // High-frequency accuracy: compare with the analog response at the lowest common rate.
    {
        struct Case { const char* name; Band band; std::vector<double> frequencies; };
        const std::vector<Case> cases {
            { "bell 12 kHz +12 dB matches analog", make(FilterType::Bell, 12000, 12, 1), { 2000, 6000, 12000, 16000, 20000 } },
            { "bell 8 kHz -9 dB Q4 matches analog", make(FilterType::Bell, 8000, -9, 4), { 4000, 8000, 11000, 20000 } },
            { "high shelf 10 kHz +6 dB matches analog", make(FilterType::HighShelf, 10000, 6), { 1000, 5000, 10000, 15000, 20000 } },
            { "low pass 12 kHz matches analog", make(FilterType::LowPass, 12000), { 3000, 8000, 12000, 16000, 20000 } },
            { "high pass 40 Hz matches analog", make(FilterType::HighPass, 40), { 10, 20, 40, 100, 1000 } },
        };
        for (const auto& c : cases) {
            const auto design = designBand(c.band, 44100.0);
            double worst = 0.0;
            for (double f : c.frequencies)
                worst = std::max(worst, std::abs(magnitudeDb(design, f, 44100.0) - analogMagnitudeDb(c.band, f)));
            expectNear(c.name, worst, 0.0, 0.9);
        }
    }

    // Stereo placement.
    {
        const auto levels = [](Placement placement, double leftAmp, double rightAmp) {
            auto eq = std::make_shared<Equalizer>();
            eq->prepare(48000.0, 256, 2);
            Band b = make(FilterType::Bell, 1000, -12, 1);
            b.placement = placement;
            eq->setBand(0, b);
            return measureStereo([eq](float** channels) { eq->process(channels, 2, 256); }, 1000.0, 48000.0, leftAmp, rightAmp);
        };
        expectNear("left band cuts left", levels(Placement::Left, 0.25, 0.25).left, -12.0, 0.05);
        expectNear("left band leaves right", levels(Placement::Left, 0.25, 0.25).right, 0.0, 0.001);
        expectNear("right band cuts right", levels(Placement::Right, 0.25, 0.25).right, -12.0, 0.05);
        expectNear("right band leaves left", levels(Placement::Right, 0.25, 0.25).left, 0.0, 0.001);
        expectNear("mid band cuts a centred signal", levels(Placement::Mid, 0.25, 0.25).left, -12.0, 0.05);
        expectNear("side band ignores a centred signal", levels(Placement::Side, 0.25, 0.25).left, 0.0, 0.001);
        expectNear("side band cuts an anti-phase signal", levels(Placement::Side, 0.25, -0.25).left, -12.0, 0.05);
        expectNear("mid band ignores an anti-phase signal", levels(Placement::Mid, 0.25, -0.25).left, 0.0, 0.001);
    }

    // Solo.
    expectNear("soloed bell passes its centre", single(soloedBand(make(FilterType::Bell, 1000, 9, 2)), 1000), 0.0, 0.05);
    expectTrue("soloed bell rejects far frequencies", single(soloedBand(make(FilterType::Bell, 1000, 9, 2)), 100) < -20.0);
    expectTrue("soloed high shelf rejects lows", single(soloedBand(make(FilterType::HighShelf, 4000, 6)), 200) < -40.0);
    expectTrue("soloed high pass stays a high pass", soloedBand(make(FilterType::HighPass, 100)).type == FilterType::HighPass);

    // Linear phase.
    {
        const auto makeEngine = [](const std::vector<Band>& list, double sr = 48000.0) {
            auto engine = std::make_shared<LinearPhaseEngine>();
            engine->prepare(sr);
            LinearPhaseEngine::Bands bands {};
            for (auto& b : bands) b.enabled = false;
            for (std::size_t i = 0; i < list.size(); ++i) bands[i] = list[i];
            engine->design(bands);
            return engine;
        };
        const auto impulse = [](LinearPhaseEngine& engine, int length) {
            std::vector<float> left(static_cast<std::size_t>(length)), right(static_cast<std::size_t>(length));
            left[5000] = 1.0f;
            right[5000] = 1.0f;
            float* channels[] = { left.data(), right.data() };
            for (int done = 0; done < length; done += 256) {
                float* block[] = { channels[0] + done, channels[1] + done };
                engine.process(block, 2, std::min(256, length - done));
            }
            return left;
        };

        auto flatEngine = makeEngine({});
        const int latency = flatEngine->getLatencySamples();
        expectTrue("latency is 4096 samples at 48 kHz", latency == 4096);
        const auto flat = impulse(*flatEngine, 40000);
        const auto flatPeak = std::distance(flat.begin(), std::max_element(flat.begin(), flat.end()));
        expectTrue("impulse comes out exactly at the reported latency", flatPeak == 5000 + latency);
        expectNear("flat response keeps unity gain", flat[static_cast<std::size_t>(flatPeak)], 1.0, 0.01);

        auto eqEngine = makeEngine({ make(FilterType::Bell, 1000, 9, 2), make(FilterType::HighPass, 80, 0, 0.707f, Slope::Db24) });
        const auto response = impulse(*eqEngine, 40000);
        const auto centre = static_cast<std::size_t>(5000 + latency);
        double asymmetry = 0.0, peak = 0.0;
        for (std::size_t i = 1; i < 2000; ++i) {
            asymmetry = std::max(asymmetry, static_cast<double>(std::abs(response[centre + i] - response[centre - i])));
            peak = std::max(peak, static_cast<double>(std::abs(response[centre + i])));
        }
        std::printf("     asymmetry %.3e, peak %.3e\n", asymmetry, peak);
        expectTrue("impulse response is symmetric", asymmetry < peak * 1.0e-3);

        const auto level = [](std::shared_ptr<LinearPhaseEngine> engine, double f, double l = 0.25, double r = 0.25) {
            return measureStereo([engine](float** channels) { engine->process(channels, 2, 256); }, f, 48000.0, l, r);
        };
        expectNear("linear phase bell at centre", level(makeEngine({ make(FilterType::Bell, 1000, 9, 2) }), 1000).left, 9.0, 0.15);
        expectNear("linear phase bell far away", level(makeEngine({ make(FilterType::Bell, 1000, 9, 2) }), 8000).left, 0.0, 0.05);
        expectNear("linear phase high shelf", level(makeEngine({ make(FilterType::HighShelf, 6000, -6) }), 18000).left, -6.0, 0.3);
        Band midBand = make(FilterType::Bell, 1000, -12, 1);
        midBand.placement = Placement::Mid;
        expectNear("linear phase mid on a centred signal", level(makeEngine({ midBand }), 1000).left, -12.0, 0.15);
        expectNear("linear phase mid ignores anti-phase", level(makeEngine({ midBand }), 1000, 0.25, -0.25).left, 0.0, 0.05);
        Band rightBand = make(FilterType::Bell, 1000, -12, 1);
        rightBand.placement = Placement::Right;
        expectNear("linear phase right band", level(makeEngine({ rightBand }), 1000).right, -12.0, 0.15);
        expectNear("linear phase right leaves left", level(makeEngine({ rightBand }), 1000).left, 0.0, 0.05);

        auto hiRate = makeEngine({}, 96000.0);
        expectTrue("latency scales with sample rate", hiRate->getLatencySamples() == 8192);

        // Redesign while processing: no glitch beyond the signal's own level.
        auto live = makeEngine({ make(FilterType::Bell, 1000, 6, 1) });
        std::vector<float> a(256), b(256);
        float* channels[] = { a.data(), b.data() };
        double phase = 0.0, maxAbs = 0.0;
        bool finite = true;
        for (int block = 0; block < 400; ++block) {
            if (block % 7 == 0) {
                LinearPhaseEngine::Bands bands {};
                for (auto& band : bands) band.enabled = false;
                bands[0] = make(FilterType::Bell, 1000, block % 14 == 0 ? 6.0f : -6.0f, 1);
                live->design(bands);
            }
            for (int i = 0; i < 256; ++i) { a[static_cast<std::size_t>(i)] = b[static_cast<std::size_t>(i)] = static_cast<float>(0.25 * std::sin(phase)); phase += 0.13; }
            live->process(channels, 2, 256);
            for (float v : a) { finite = finite && std::isfinite(v); maxAbs = std::max(maxAbs, static_cast<double>(std::abs(v))); }
        }
        expectTrue("redesign while playing stays finite", finite);
        expectTrue("redesign while playing does not overshoot", maxAbs < 0.25 * 2.0 * 1.15);
    }

    std::printf(failures == 0 ? "\nAll DSP checks passed\n" : "\n%d DSP checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
