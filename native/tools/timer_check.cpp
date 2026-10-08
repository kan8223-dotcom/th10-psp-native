// Checks the TH10_FAST_TIMER float path (game/Timer.cpp Timer::advance)
// against the original Extended operations: every fractional value (2^32
// bit patterns) for the stop instruction's advance(-1) at rate 1, then random
// and edge frames/rates/fractional values (zeros, subnormals, the normal
// floor, exact cancellations, huge values, infinities, NaNs) in every
// precision/rounding mode the game uses. previous, current and the bits of
// fractional must all match.
//   cmake --build <dir> --target th10_timer_check && <dir>/th10_timer_check [millions] [step]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
struct T {i32 previous;i32 current;float fractional;const float* rate;};
static bool unscaled(float rate) noexcept { return 0.99f < rate && rate < 1.01f; }
// The original (Timer.cpp), verbatim apart from the struct name.
static void advance_original(T& t,float frames) noexcept {
    t.previous = t.current;
    auto delta = number(frames);
    if (!unscaled(*t.rate)) delta = delta * number(*t.rate);
    t.fractional = (delta + number(t.fractional)).to_float();
    t.current = Scalar::truncate(t.fractional);
}
// The TH10_FAST_TIMER version, verbatim apart from the struct name and counting.
static unsigned long long fast_hits=0;
static void advance_fast(T& t,float frames) noexcept {
    t.previous = t.current;
    if (single_precision_nearest()) {
        using namespace arithmetic;
        const float held = t.fractional, scale = *t.rate;
        if (representable(bits_of(frames)) && representable(bits_of(held))) {
            float delta = frames; bool accepted = true;
            if (!unscaled(scale)) {
                const float product = frames * scale; const u32 product_bits = bits_of(product);
                accepted = representable(bits_of(scale)) && ((product_bits & 0x7fffffffu) ? nonzero_accepted(product_bits) : (frames == 0 || scale == 0));
                delta = product;
            }
            const float sum = delta + held; const u32 sum_bits = bits_of(sum);
            if (accepted && ((sum_bits & 0x7fffffffu) ? nonzero_accepted(sum_bits) : delta == -held)) {
                t.fractional = sum;
                t.current = Scalar::truncate(t.fractional);
                ++fast_hits;return;
            }
        }
    }
    auto delta = number(frames);
    if (!unscaled(*t.rate)) delta = delta * number(*t.rate);
    t.fractional = (delta + number(t.fractional)).to_float();
    t.current = Scalar::truncate(t.fractional);
}
static u64 state=0x6a09e667f3bcc909ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x01000000u,0x3f800000u,0x3f7d70a4u,0x3f8147aeu,
    0x3f7ae148u,0x3f828f5cu,0x3f000000u,0x40000000u,0x4b000000u,0x4f000000u,0x7f7fffffu,0x7f800000u,0x7fc00000u,0x41700000u};
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%5){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%20001)-10000)));                   // integer frame counts
    case 3:return fbits(bits(float((r>>8)%4096)/64.0f)|sign);                     // fractional counts
    default:return fbits(((u32(1+(r>>8)%4)<<23)|u32((r>>16)&0x7fffffu))|sign);   // near the normal floor
    }
}
static bool same(const T& a,const T& b){return a.previous==b.previous&&a.current==b.current&&bits(a.fractional)==bits(b.fractional);}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;const u64 step=argc>2?std::strtoull(argv[2],nullptr,0):1;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);const bool main_mode=m.p==Precision::Single&&m.r==Rounding::NearestEven;
        const float one=1.0f;
        for(u64 b=0;b<=0xffffffffull;b+=main_mode?step:65537){   // the stop instruction: advance(-1) at rate 1
            T a{7,int(b&1023),fbits(u32(b)),&one},c=a;advance_original(a,-1.0f);advance_fast(c,-1.0f);++checked;
            if(!same(a,c)){if(mismatches<8)std::printf("MISMATCH stop mode=%d/%d fractional=%08x\n",int(m.p),int(m.r),u32(b));++mismatches;}}
        const u64 n=(main_mode?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            const float rate=(next()&1)?draw_value():((next()&1)?1.0f:0.5f),frames=draw_value(),held=(next()&7)==0?-frames:draw_value();
            T a{3,int(next()&0xffff),held,&rate},c=a;advance_original(a,frames);advance_fast(c,frames);++checked;
            if(!same(a,c)){if(mismatches<8)std::printf("MISMATCH mode=%d/%d rate=%08x frames=%08x fractional=%08x\n",int(m.p),int(m.r),bits(rate),bits(frames),bits(held));++mismatches;}
        }}
    std::printf("timer_check: %llu cases (%llu on the float path), %llu mismatches\n",checked,fast_hits,mismatches);
    return mismatches?1:0;
}
