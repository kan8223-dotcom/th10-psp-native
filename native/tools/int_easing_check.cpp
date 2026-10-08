// Checks TH10_FAST_EASING_INT (game/Interpolation.cpp Rgb/Alpha sample, with TH10_FAST_EASING_INLINE):
// sample() built with the float path must equal the original Extended expressions
// (easing() on Extended, from_int, *, +, truncate_int) for every easing mode, integer
// elapsed/duration grids, integer start/end edges (0..255, +-2^24, differences
// beyond 2^24, i32 extremes), in every precision/rounding mode (the float path must
// decline outside precision 32 nearest).
//   cmake --build <dir> --target th10_int_easing_check && <dir>/th10_int_easing_check [millions]
#define TH10_FAST_EASING 1
#include "../../th10_web/cpp/game/Interpolation.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static u64 state=0x9e3779b97f4a7c15ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static i32 diff(i32 a,i32 b){const u32 v=u32(a)-u32(b);i32 r;std::memcpy(&r,&v,4);return r;}
static const i32 edges[]={0,1,2,127,128,254,255,-1,-255,256,1000,-1000,16777215,16777216,-16777216,-16777215,16777217,-16777217,8388608,2147483647,-2147483647-1,1<<30,-(1<<30)};
static i32 value(){const u64 r=next();switch(r%4){case 0:return edges[(r>>8)%(sizeof(edges)/4)];case 1:return i32((r>>8)%256);case 2:return i32((r>>8)%600)-300;default:return i32(u32(r>>16));}}
static const InterpolationMode modes[]={InterpolationMode::Linear,InterpolationMode::Accelerate2,InterpolationMode::Accelerate3,InterpolationMode::Accelerate4,
    InterpolationMode::Decelerate2,InterpolationMode::Decelerate3,InterpolationMode::Decelerate4,InterpolationMode::Smooth2,InterpolationMode::Smooth3,
    InterpolationMode::Smooth4,InterpolationMode::FastSlow2,InterpolationMode::FastSlow3,InterpolationMode::FastSlow4,InterpolationMode::HoldStart,InterpolationMode::HoldEnd};
static const float rate_one=1.0f;
static unsigned long long checked=0,mismatches=0;
extern "C" {unsigned long long th10_int_easing_taken=0;}
template<class I> void arm(I& v,int e,int d,InterpolationMode m){
    std::memset(&v,0,sizeof v);v.duration=d;v.mode=m;v.flags=1;v.timer.rate=&rate_one;
    v.timer.current=e-1;v.timer.previous=e-2;v.timer.fractional=float(e-1);   // tick() -> e
}
static void one(int e,int d,InterpolationMode m,i32 s0,i32 e0,i32 s1,i32 e1,i32 s2,i32 e2){
    // the reference: the original expressions after the same tick (e < d, so no finish)
    const auto t=easing(float(e),Extended::from_int(d).to_float(),m);
    AlphaInterpolator a;arm(a,e,d,m);a.start[0]=s0;a.end[0]=e0;
    const i32 ra=(t*Extended::from_int(diff(e0,s0))+Extended::from_int(s0)).truncate_int();
    const i32 ga=sample(a,&rate_one);++checked;
    if(ga!=ra){if(mismatches<8)std::printf("ALPHA MISMATCH mode=%d e=%d d=%d s=%d e=%d got=%d want=%d\n",int(m),e,d,s0,e0,ga,ra);++mismatches;}
    RgbInterpolator c;arm(c,e,d,m);c.start[0]=s0;c.end[0]=e0;c.start[1]=s1;c.end[1]=e1;c.start[2]=s2;c.end[2]=e2;
    const i32 st[3]={s0,s1,s2},en[3]={e0,e1,e2};i32 rc[3];
    for(int k=0;k<3;++k)rc[k]=wrapping_add(st[k],(Extended::from_int(diff(en[k],st[k]))*t).truncate_int());
    const Rgb gc=sample(c,&rate_one);++checked;
    if(gc.blue!=rc[0]||gc.green!=rc[1]||gc.red!=rc[2]){if(mismatches<8)std::printf("RGB MISMATCH mode=%d e=%d d=%d\n",int(m),e,d);++mismatches;}
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):10;
    arithmetic_mode(Precision::Single,Rounding::NearestEven);
    // 1. every mode, every integer elapsed below every duration up to 1024, with color-like and edge endpoints
    for(int d=2;d<=1024;++d)for(int e=0;e<d;++e)for(auto m:modes){
        one(e,d,m,i32(next()%256),i32(next()%256),0,255,255,0);
        one(e,d,m,value(),value(),value(),value(),value(),value());}
    std::printf("section1: cases=%llu fast path taken=%llu\n",checked,th10_int_easing_taken);
    const unsigned long long c1=checked,t1=th10_int_easing_taken;
    // 2. random durations up to 2^16 and beyond (declined), random endpoints
    for(u64 k=0;k<millions*1000000ull;++k){
        const int d=2+int(next()%(next()%8?65535:200000)),e=int(next()%u64(d));
        one(e,d,modes[next()%15],value(),value(),value(),value(),value(),value());}
    std::printf("section2: cases=%llu fast path taken=%llu\n",checked-c1,th10_int_easing_taken-t1);
    const unsigned long long c2=checked,t2=th10_int_easing_taken;
    // 3. the other precision/rounding modes (the float path must decline and the result stay the original)
    const Precision ps[]={Precision::Single,Precision::Double,Precision::Extended};const Rounding rs[]={Rounding::NearestEven,Rounding::TowardZero,Rounding::Down,Rounding::Up};
    for(auto p:ps)for(auto r:rs){if(p==Precision::Single&&r==Rounding::NearestEven)continue;arithmetic_mode(p,r);
        for(int k=0;k<200000;++k){const int d=2+int(next()%4096),e=int(next()%u64(d));one(e,d,modes[next()%15],value(),value(),value(),value(),value(),value());}}
    std::printf("section3 (other modes): cases=%llu fast path taken=%llu\n",checked-c2,th10_int_easing_taken-t2);
    std::printf("int_easing_check: %llu cases, %llu mismatches\n",checked,mismatches);
    return mismatches?1:0;
}
