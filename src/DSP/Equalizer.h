#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace libreq {

// BandPass is internal: it is what a soloed Bell or Notch band plays.
enum class FilterType { Bell, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass };
enum class Slope { Db12, Db24, Db36, Db48 };
enum class Placement { Stereo, Left, Right, Mid, Side };

struct Band {
    FilterType type { FilterType::Bell };
    float frequency { 1000.0f };
    float gainDb { 0.0f };
    float q { 0.707f };
    Slope slope { Slope::Db12 };
    Placement placement { Placement::Stereo };
    // Dynamic bands apply up to gainDb only while the level in their range exceeds thresholdDb.
    bool dynamic { false };
    float thresholdDb { -24.0f };
    bool enabled { true };

    bool operator==(const Band&) const = default;
};

struct Biquad {
    double b0 { 1.0 }, b1 { 0.0 }, b2 { 0.0 }, a1 { 0.0 }, a2 { 0.0 };
};

struct BandDesign {
    std::array<Biquad, 4> sections {};
    int count { 1 };
};

// Pure functions, safe to call from any thread.
BandDesign designBand(const Band& band, double sampleRate) noexcept;
double magnitudeDb(const BandDesign& design, double frequency, double sampleRate) noexcept;
// Response of the analog filter that designBand() approximates. Independent of the sample rate.
double analogMagnitudeDb(const Band& band, double frequency) noexcept;
// What the band plays while soloed: the frequency range it acts on, at unity gain.
Band soloedBand(Band band) noexcept;
bool usesGain(FilterType type) noexcept;
bool usesSlope(FilterType type) noexcept;

class Equalizer {
public:
    static constexpr std::size_t maxBands = 24;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    // Call from the audio thread. The first call after prepare() applies immediately;
    // later changes are smoothed to avoid zipper noise.
    void setBand(std::size_t index, const Band& band) noexcept;
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    // Gain a band is applying right now. Safe to read from any thread.
    float getLiveGainDb(std::size_t index) const noexcept {
        return index < maxBands ? liveGain[index].load(std::memory_order_relaxed) : 0.0f;
    }

    // How much of the overshoot above the threshold a dynamic band applies.
    static constexpr double dynamicSlope = 0.75;

private:
    static constexpr int chunkSize = 32;

    struct SectionState { double z1 { 0.0 }, z2 { 0.0 }; };
    struct Slot {
        Band target {};
        Band current {};
        BandDesign design {};
        std::array<std::array<SectionState, 2>, 4> state {};
        BandDesign detector {};
        std::array<std::array<SectionState, 2>, 4> detectorState {};
        double envelope { 0.0 };      // Smoothed power in the band's range.
        double dynamicGainDb { 0.0 }; // Gain currently applied by a dynamic band.
        bool snap { true };
        bool moving { false };
        bool active { false };
    };

    void redesign(Slot&, bool withDetector = true) noexcept;
    void advanceSmoothing() noexcept;
    void updateDynamics(Slot&, std::array<std::array<double, chunkSize>, 2>& buffer, int first, int count, int n) noexcept;

    double sr { 44100.0 };
    double smoothing { 0.0 };
    std::array<Slot, maxBands> slots {};
    std::array<std::atomic<float>, maxBands> liveGain {};
};

} // namespace libreq
