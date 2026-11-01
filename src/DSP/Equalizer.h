#pragma once
#include <array>
#include <cstddef>
namespace arielo { enum class FilterType { Bell, LowShelf, HighShelf, HighPass, LowPass, Notch }; struct Band { FilterType type{FilterType::Bell}; float frequency{1000}; float gainDb{}; float q{.707f}; bool enabled{true}; }; class Equalizer { public: static constexpr std::size_t maxBands=24; void prepare(double,int,int); void reset(); void setBand(std::size_t,Band); void process(float**,int,int) noexcept; private: struct State{float z1{},z2{};}; void update(); double sr{44100}; std::array<Band,maxBands> bands{}; std::array<std::array<float,5>,maxBands> c{}; std::array<std::array<State,2>,maxBands> state{}; }; }
