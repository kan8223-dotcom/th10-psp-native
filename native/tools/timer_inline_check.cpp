// Checks TH10_TIMER_INLINE (game/Timer.hpp tick_inline/advance_inline and the
// scaled float path in game/Timer.cpp Timer::tick, built here with
// TH10_FAST_TIMER and TH10_EXTENDED_INLINE=0 as on the PSP) against verbatim
// copies of the original Timer::tick/Timer::advance (all-Extended): every
// fractional bit pattern (2^32) for tick() at rate 1 and for the stop
// instruction's advance(-1) at rate 1, then random and edge rates/frames/fractional values
// (zeros, subnormals, the normal floor, the 0.99/1.01 unscaled boundaries and
// their neighbours, exact cancellations rate == -fractional, sums at +-2^31,
// huge values, infinities, NaNs) in every precision/rounding mode. previous,
// current and the bits of fractional must all match.
//   cmake --build <dir> --target th10_timer_inline_check && <dir>/th10_timer_inline_check [millions] [step]
#include "../../th10_web/cpp/game/Timer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
static u64 state=0x9e3779b97f4a7c15ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x01000000u,0x01800000u,0x3f800000u,
    0x3f7d70a4u,0x3f7d70a3u,0x3f7d70a5u,0x3f8147aeu,0x3f8147adu,0x3f8147afu,0x3f000000u,0x40000000u,0x4b000000u,0x4b7fffffu,
    0x4effffffu,0x4f000000u,0x4f000001u,0x7f7fffffu,0x7f800000u,0x7fc00000u,0x41700000u,0x3dcccccdu,0x3e99999au};
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%20001)-10000)));                  // integer frame counts
    case 3:return fbits(bits(float((r>>8)%4096)/64.0f)|sign);                    // fractional counts
    case 4:return fbits((0x4effff00u+u32((r>>8)&0x1ff))|sign);                   // around 2^31
    default:return fbits(((u32(1+(r>>8)%4)<<23)|u32((r>>16)&0x7fffffu))|sign);  // near the normal floor
    }
}
static bool unscaled(float rate) noexcept { return 0.99f < rate && rate < 1.01f; }
// The originals (Timer.cpp without TH10_FAST_TIMER/TH10_TIMER_INLINE), verbatim.
static i32 tick_original(Timer& t) noexcept {
    t.previous = t.current;
    if (unscaled(*t.rate)) {
        t.current = wrapping_add(t.current, 1);
        t.fractional = Scalar::add(t.fractional,1.0f);
    } else {
        const auto sum = number(*t.rate) + number(t.fractional);
        t.fractional = sum.to_float();
        t.current = sum.truncate_int();
    }
    return t.current;
}
static void advance_original(Timer& t,float frames) noexcept {
    t.previous = t.current;
    auto delta = number(frames);
    if (!unscaled(*t.rate)) delta = delta * number(*t.rate);
    t.fractional = (delta + number(t.fractional)).to_float();
    t.current = Scalar::truncate(t.fractional);
}
static bool same(const Timer& a,const Timer& b){return a.previous==b.previous&&a.current==b.current&&bits(a.fractional)==bits(b.fractional);}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;const u64 step=argc>2?std::strtoull(argv[2],nullptr,0):1;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::TowardZero},{Precision::Single,Rounding::Down},{Precision::Single,Rounding::Up},
        {Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0,unchanged_fast=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);const bool main_mode=m.p==Precision::Single&&m.r==Rounding::NearestEven;
        const float one=1.0f;
        for(u64 b=0;b<=0xffffffffull;b+=main_mode?step:65537){   // tick() and advance(-1) at rate 1, every fractional
            Timer a{7,int(b&1023),fbits(u32(b)),&one},c=a;const i32 ra=tick_original(a),rc=c.tick_inline();++checked;
            if(!same(a,c)||ra!=rc){if(mismatches<8)std::printf("MISMATCH tick mode=%d/%d fractional=%08x\n",int(m.p),int(m.r),u32(b));++mismatches;}
            Timer d{7,int(b&1023),fbits(u32(b)),&one},e=d;advance_original(d,-1.0f);e.advance_inline(-1.0f);++checked;
            if(!same(d,e)){if(mismatches<8)std::printf("MISMATCH stop mode=%d/%d fractional=%08x\n",int(m.p),int(m.r),u32(b));++mismatches;}}
        const u64 n=(main_mode?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            const u64 pick=next();
            const float rate=(pick&3)==0?1.0f:(pick&3)==1?fbits(special[(pick>>4)%(sizeof(special)/4)]|(u32(pick>>40)&1)<<31):draw_value();
            const float frames=draw_value();
            float held=draw_value();if(((pick>>8)&7)==0)held=-rate;else if(((pick>>8)&7)==1)held=-frames;
            Timer a{3,int(next()&0xffff),held,&rate},c=a,g=a;const i32 ra=tick_original(a),rc=c.tick_inline(),rg=g.tick();checked+=2;
            if(!same(a,c)||!same(a,g)||ra!=rc||ra!=rg){if(mismatches<8)std::printf("MISMATCH tick mode=%d/%d rate=%08x fractional=%08x\n",int(m.p),int(m.r),bits(rate),bits(held));++mismatches;}
            Timer d{3,int(next()&0xffff),held,&rate},e=d;advance_original(d,frames);e.advance_inline(frames);++checked;
            if(!same(d,e)){if(mismatches<8)std::printf("MISMATCH advance mode=%d/%d rate=%08x frames=%08x fractional=%08x\n",int(m.p),int(m.r),bits(rate),bits(frames),bits(held));++mismatches;}
        }}
    (void)unchanged_fast;
    std::printf("timer_inline_check: %llu cases, %llu mismatches\n",checked,mismatches);
    return mismatches?1:0;
}
