// Checks the TH10_FAST_ANGLE float path (game/GameMath.cpp add_angle and
// normalize_angle, and TH10_FAST_ANGLE_FLOAT add_angle_float) against the original Extended wrap: every returned field
// (significand, exponent, tag, float bits) must match, for random and edge
// operands (zeros, the 2^-100/2^100 range edges, exact cancellations, values
// that hit the 32-iteration cap, subnormals, infinities, NaNs), in every
// precision/rounding mode the game uses.
//   cmake --build <dir> --target th10_angle_check && <dir>/th10_angle_check [millions]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// The original (GameMath.cpp), verbatim.
static Extended wrap_angle(Extended result) noexcept {
    const auto pi=number(3.1415927410125732421875f),tau=number(6.283185482025146484375f);
    i32 iterations=0;
    while(pi<result){result=result-tau;if(iterations++>32)break;}
    while(result<-pi){result=result+tau;if(iterations++>32)break;}
    return result;
}
static Extended normalize_original(float radians) noexcept {return wrap_angle(number(radians));}
static Extended add_original(float radians,float delta) noexcept {return wrap_angle(number(radians)+number(delta));}
// The TH10_FAST_ANGLE versions, verbatim apart from counting the float path.
static u64 fast_hits=0;
static inline bool angle_operand(float v) noexcept {const float a=std::fabs(v);return a==0||(a>=0x1p-100f&&a<=0x1p100f);}
static inline Extended wrap_angle_float(float result) noexcept {
    i32 iterations=0;
    while(3.1415927410125732421875f<result){result=result-6.283185482025146484375f;if(iterations++>32)break;}
    while(result<-3.1415927410125732421875f){result=result+6.283185482025146484375f;if(iterations++>32)break;}
    ++fast_hits;return arithmetic::tagged(arithmetic::bits_of(result));
}
static Extended normalize_fast(float radians) noexcept {
    if(single_precision_nearest()&&angle_operand(radians))return wrap_angle_float(radians);
    return wrap_angle(number(radians));}
static Extended add_fast(float radians,float delta) noexcept {
    if(single_precision_nearest()&&angle_operand(radians)&&angle_operand(delta))return wrap_angle_float(radians+delta);
    return wrap_angle(number(radians)+number(delta));}
// TH10_FAST_ANGLE_FLOAT add_angle_float, verbatim (wrap_angle_value is wrap_angle_float's loop).
static inline float wrap_angle_value(float result) noexcept {
    i32 iterations=0;
    while(3.1415927410125732421875f<result){result=result-6.283185482025146484375f;if(iterations++>32)break;}
    while(result<-3.1415927410125732421875f){result=result+6.283185482025146484375f;if(iterations++>32)break;}
    return result;
}
static float add_float(float radians,float delta) noexcept {
    if(single_precision_nearest()&&angle_operand(radians)&&angle_operand(delta))return wrap_angle_value(radians+delta);
    return add_fast(radians,delta).to_float();
}
static bool same(const Extended& a,const Extended& b){return a.significand==b.significand&&a.exponent==b.exponent&&a.reserved16==b.reserved16&&a.reserved32==b.reserved32;}
static u64 state=0x2545f4914f6cdd1dull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x0d800000u,0x0d7fffffu,0x71800000u,0x71800001u,0x72000000u,
    0x40490fdbu,0x40c90fdbu,0x3fc90fdbu,0x40490fdcu,0x40490fdau,0x7f7fffffu,0x7f800000u,0x7fc00000u,0x3f800000u,0x4c000000u,0x5f000000u};
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%200001)-100000)/10000.0f));      // -10..10 rad
    case 3:return fbits(bits(float((r>>8)%65536)/65536.0f*6.2831855f)|sign);   // one turn
    case 4:return fbits(((u32(27+(r>>8)%5)<<23)|u32((r>>16)&0x7fffffu))|sign); // near 2^-100
    default:return fbits(((u32(220+(r>>8)%10)<<23)|u32((r>>16)&0x7fffffu))|sign); // near 2^100
    }
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    u64 checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        const u64 n=(m.p==Precision::Single&&m.r==Rounding::NearestEven?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            const float a=draw_value();float d=draw_value();
            if((next()&7)==0)d=fbits(bits(a)^0x80000000u);   // exact cancellation
            const Extended x=add_original(a,d),y=add_fast(a,d),u=normalize_original(a),v=normalize_fast(a);checked+=2;
            const float f1=x.to_float(),f2=add_float(a,d);++checked;
            if(!same(x,y)||!same(u,v)||(bits(f1)!=bits(f2)&&!(std::isnan(f1)&&std::isnan(f2)))){if(mismatches<8)std::printf("MISMATCH mode=%d/%d a=%08x d=%08x\n",int(m.p),int(m.r),bits(a),bits(d));++mismatches;}
        }}
    std::printf("angle_check: %llu cases (%llu on the float path), %llu mismatches\n",(unsigned long long)checked,(unsigned long long)fast_hits,(unsigned long long)mismatches);
    return mismatches?1:0;
}
