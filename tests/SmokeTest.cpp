#include "DSP/Equalizer.h"
#include <array>
#include <cassert>
int main(){arielo::Equalizer e;e.prepare(48000,64,2);std::array<float,64> a{},b{};a[0]=1;float*c[]={a.data(),b.data()};e.process(c,2,64);assert(a[0]==a[0]);}
