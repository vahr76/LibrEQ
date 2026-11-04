#pragma once
#include <array>
#include <cstddef>

namespace libreq {

enum class FilterType { Bell, LowShelf, HighShelf, HighPass, LowPass, Notch };
enum class Slope { Db12, Db24, Db36, Db48 };

struct Band {
    FilterType type { FilterType::Bell };
    float frequency { 1000.0f };
    float gainDb { 0.0f };
    float q { 0.707f };
    Slope slope { Slope::Db12 };
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

private:
    static constexpr int chunkSize = 32;

    struct SectionState { double z1 { 0.0 }, z2 { 0.0 }; };
    struct Slot {
        Band target {};
        Band current {};
        BandDesign design {};
        std::array<std::array<SectionState, 2>, 4> state {};
        bool snap { true };
        bool moving { false };
        bool active { false };
    };

    void redesign(Slot&) noexcept;
    void advanceSmoothing() noexcept;

    double sr { 44100.0 };
    double smoothing { 0.0 };
    std::array<Slot, maxBands> slots {};
};

} // namespace libreq
