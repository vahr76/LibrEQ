#include "Equalizer.h"
#include <algorithm>
#include <cmath>
namespace arielo {
void Equalizer::prepare(double rate,int,int){sr=rate;update();reset();}
void Equalizer::reset(){for(auto& x:state)for(auto& y:x)y={};}
void Equalizer::setBand(std::size_t i,Band b){if(i<maxBands){bands[i]=b;update();}}
void Equalizer::update(){for(std::size_t i=0;i<maxBands;++i){auto b=bands[i];float A=std::pow(10.f,b.gainDb/40.f),w=2.f*float(M_PI)*b.frequency/float(sr),a=std::sin(w)/(2.f*std::max(.05f,b.q)),co=std::cos(w),b0=1,b1=0,b2=0,a0=1,a1=0,a2=0;if(b.type==FilterType::Bell){b0=1+a*A;b1=-2*co;b2=1-a*A;a0=1+a/A;a1=-2*co;a2=1-a/A;}else if(b.type==FilterType::LowPass||b.type==FilterType::HighPass){bool h=b.type==FilterType::HighPass;b0=(h?(1+co):(1-co))/2;b1=h?-(1+co):1-co;b2=b0;a0=1+a;a1=-2*co;a2=1-a;}else if(b.type==FilterType::Notch){b1=-2*co;b2=1;a0=1+a;a1=-2*co;a2=1-a;}c[i]={b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};}}
void Equalizer::process(float** ch,int nc,int n)noexcept{for(int x=0;x<n;++x)for(int k=0;k<std::min(nc,2);++k){float y=ch[k][x];for(std::size_t i=0;i<maxBands;++i)if(bands[i].enabled){auto&s=state[i][k];auto&p=c[i];float o=p[0]*y+s.z1;s.z1=p[1]*y-p[3]*o+s.z2;s.z2=p[2]*y-p[4]*o;y=o;}ch[k][x]=y;}}
}
