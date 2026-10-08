// Checks TH10_FAST_EASING (game/Interpolation.cpp): inside the stated range,
// easing_as on SpriteNumber must equal easing() on Extended bit for bit, and
// the Vec2/Vec3 combinations must equal the original Extended expressions.
// Every mode, every integer elapsed below every duration up to 4096 (and a
// sampled grid up to 2^16), plus random coordinates including range edges.
//   cmake --build <dir> --target th10_easing_check && <dir>/th10_easing_check [millions]
#define TH10_FAST_EASING 1
#include "../../th10_web/cpp/game/Interpolation.hpp"
#include "../../portable/numeric/SpriteNumber.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
using touhou::numeric::SpriteNumber;
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
// easing_as: the template from Interpolation.cpp (the text of easing()).
template<class Number> auto easing_as(Number number, float elapsed, float duration, InterpolationMode mode) noexcept -> decltype(number(0.0f)) {
    auto t = number(elapsed) / number(duration);
    const auto one = number(1.0f), two = number(2.0f), half = number(0.5f);
    switch (mode) {
    case InterpolationMode::Accelerate2: return t*t;
    case InterpolationMode::Accelerate3: return t*t*t;
    case InterpolationMode::Accelerate4: return t*t*t*t;
    case InterpolationMode::Decelerate2: t=one-t; return one-t*t;
    case InterpolationMode::Decelerate3: t=one-t; return one-t*t*t;
    case InterpolationMode::Decelerate4: t=one-t; return one-t*t*t*t;
    case InterpolationMode::Smooth2:
        t=t+t; if(t<one)return t*t*half; t=two-t; return (two-t*t)*half;
    case InterpolationMode::Smooth3:
        t=t+t; if(t<one)return t*t*t*half; t=two-t; return (two-t*t*t)*half;
    case InterpolationMode::Smooth4:
        t=t+t; if(t<one)return t*t*t*t*half; t=two-t; return (two-t*t*t*t)*half;
    case InterpolationMode::FastSlow2:
        t=t+t; if(t<one){t=one-t;return half-t*t*half;} t=t-one; return t*t*half+half;
    case InterpolationMode::FastSlow3:
        t=t+t; if(t<one){t=one-t;return half-t*t*t*half;} t=t-one; return t*t*t*half+half;
    case InterpolationMode::FastSlow4:
        t=t+t; if(t<one){t=one-t;return half-t*t*t*t*half;} t=t-one; return t*t*t*t*half+half;
    case InterpolationMode::HoldStart: return number(0.0f);
    case InterpolationMode::HoldEnd: return one;
    default: return t;
    }
}
static bool same_value(const Extended& e,SpriteNumber s){return e.tagged()&&e.reserved32==bits(s.value);}
static u64 state=0x510e527fade682d1ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static float coordinate(){const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%5){case 0:return 0.0f;case 1:return fbits(u32(127-20)<<23|sign);case 2:return fbits(u32(127+20)<<23|sign);
    case 3:return fbits(bits(float(i32((r>>8)%4000)-2000)/8.0f));default:return fbits(((u32(107+(r>>8)%40)<<23)|u32((r>>16)&0x7fffffu))|sign);}}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):10;
    arithmetic_mode(Precision::Single,Rounding::NearestEven);
    const auto as_float=[](float v){return SpriteNumber(v);};const auto as_extended=[](float v){return number(v);};
    unsigned long long checked=0,mismatches=0;
    const InterpolationMode modes[]={InterpolationMode::Linear,InterpolationMode::Accelerate2,InterpolationMode::Accelerate3,InterpolationMode::Accelerate4,
        InterpolationMode::Decelerate2,InterpolationMode::Decelerate3,InterpolationMode::Decelerate4,InterpolationMode::Smooth2,InterpolationMode::Smooth3,
        InterpolationMode::Smooth4,InterpolationMode::FastSlow2,InterpolationMode::FastSlow3,InterpolationMode::FastSlow4,InterpolationMode::HoldStart,InterpolationMode::HoldEnd};
    // 1. easing: all integer elapsed in [0,duration] for durations 1..4096, sampled beyond
    for(int d=1;d<=65536;d=d<4096?d+1:d+97)for(int e=0;e<=d;e+=(d<=4096?1:61))for(auto m:modes){
        const float el=float(e),du=float(d);const Extended x=easing(el,du,m);const SpriteNumber y=easing_as(as_float,el,du,m);++checked;
        if(!same_value(x,y)){if(mismatches<8)std::printf("EASING MISMATCH mode=%d e=%d d=%d\n",int(m),e,d);++mismatches;}}
    // 2. the Vec2/Vec3 combinations (the sample() bodies) on random coordinates
    for(u64 k=0;k<millions*1000000ull;++k){
        const int d=1+int(next()%65536),e=int(next()%(u64(d)+1));const float el=float(e),du=float(d);const auto m=modes[next()%15];
        const float s0=coordinate(),e0=coordinate(),s1=coordinate(),e1=coordinate();
        const auto tx=easing(el,du,m);const auto tf=easing_as(as_float,el,du,m);
        const float a=(number(((number(e0)-number(s0))*tx).to_float())+number(s0)).to_float();
        const float b=(SpriteNumber(((SpriteNumber(e0)-SpriteNumber(s0))*tf).to_float())+SpriteNumber(s0)).to_float();
        const float c=((number(e1)-number(s1))*tx+number(s1)).to_float();
        const float f=((SpriteNumber(e1)-SpriteNumber(s1))*tf+SpriteNumber(s1)).to_float();
        const auto dx=number(Scalar::sub(e1,s0));const auto df=SpriteNumber(Scalar::sub(e1,s0));
        const float g=(number((dx*tx).to_float())+number(s1)).to_float(),h=(SpriteNumber((df*tf).to_float())+SpriteNumber(s1)).to_float();
        checked+=3;
        if(bits(a)!=bits(b)||bits(c)!=bits(f)||bits(g)!=bits(h)){if(mismatches<8)std::printf("COMBINE MISMATCH mode=%d e=%d d=%d\n",int(m),e,d);++mismatches;}
    }
    std::printf("easing_check: %llu cases, %llu mismatches\n",checked,mismatches);
    return mismatches?1:0;
}
