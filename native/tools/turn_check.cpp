// Checks TH10_FAST_TURN (game/BulletFeatures.cpp turn_speed, a turning
// bullet's deceleration) against the original Extended expression: random and
// edge speeds, timer fractions and durations (zeros, subnormals, the normal
// floor, exact cancellations f == d, zero products, quotients that underflow
// to zero, |d| at 2^24 and beyond, d == 0, huge values, infinities, NaNs) in
// every precision/rounding mode, built with TH10_EXTENDED_INLINE=0 as on the PSP.
//   cmake --build <dir> --target th10_turn_check && <dir>/th10_turn_check [millions]
#include "../../th10_web/cpp/game/FastFloat.hpp"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// ---- original (verbatim, BulletFeatures.cpp) ----
static float turn_original(float s,float f,i32 d){return (number(s)-number(s)*number(f)/Extended::from_int(d)).to_float();}
// ---- TH10_FAST_TURN (verbatim apart from counting) ----
static unsigned long long hits=0;
inline float turn_speed(float s,float f,i32 d){
    if(single_precision_nearest()&&fast_float::operand(s)&&fast_float::operand(f)&&d>=-16777216&&d<=16777216){
        float product,difference;
        if(fast_float::mul(s,f,product)){
            const float quotient=product/static_cast<float>(d);const u32 b=arithmetic::bits_of(quotient);
            if(((b&0x7fffffffu)?arithmetic::nonzero_accepted(b):product==0)&&fast_float::sub(s,quotient,difference)){++hits;return difference;}
        }
    }
    return (number(s)-number(s)*number(f)/Extended::from_int(d)).to_float();
}
static u64 state=0x243f6a8885a308d3ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x01000000u,0x01800000u,0x3f800000u,0x3f000000u,
    0x40000000u,0x40400000u,0x3e800000u,0x4b000000u,0x7f7fffffu,0x7f000000u,0x7f800000u,0x7fc00000u,0x40a00000u,0x3dcccccdu};
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return float(i32((r>>8)%401)-200);                                   // timer fractions (frames)
    case 3:return fbits(bits(float((r>>8)%4096)/64.0f)|sign);                    // speeds
    case 4:return fbits(((u32(1+(r>>8)%6)<<23)|u32((r>>16)&0x7fffffu))|sign);   // near the normal floor
    default:return fbits(((u32(250+(r>>8)%5)<<23)|u32((r>>16)&0x7fffffu))|sign); // near the top
    }
}
static i32 draw_duration(){
    const u64 r=next();
    static const i32 edge[]={0,1,-1,2,3,7,60,120,16777215,16777216,16777217,-16777216,-16777217,INT_MAX,INT_MIN,INT_MIN+1,1<<30};
    switch(r%4){case 0:return edge[(r>>8)%(sizeof(edge)/4)];case 1:return i32((r>>8)%200);case 2:return i32(u32(r>>16));default:return i32((r>>8)%(1u<<25))-(1<<24);}
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::TowardZero},{Precision::Single,Rounding::Down},{Precision::Single,Rounding::Up},
        {Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);const bool main_mode=m.p==Precision::Single&&m.r==Rounding::NearestEven;
        const u64 n=(main_mode?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            const u64 pick=next();float s=draw_value();float f=draw_value();i32 d=draw_duration();
            switch(pick&15){
            case 0:f=float(d);break;                                     // s*f/d == s: exact cancellation s - s
            case 1:s=0;break;case 2:f=0;break;                            // zero products
            case 3:d=i32((pick>>8)%121);f=float(i32((pick>>16)%(u32(d)+1)));s=float((pick>>24)%400)/64.0f;break;   // the game's shape
            case 4:s=fbits(0x00800000u|u32((pick>>8)&0xffff));f=fbits(0x3f000000u);d=16777216;break;  // quotient underflow
            default:break;}
            const float a=turn_original(s,f,d),b=turn_speed(s,f,d);++checked;
            if(bits(a)!=bits(b)){if(mismatches<8)std::printf("MISMATCH mode=%d/%d s=%08x f=%08x d=%d original=%08x fast=%08x\n",int(m.p),int(m.r),bits(s),bits(f),d,bits(a),bits(b));++mismatches;}
        }}
    std::printf("turn_check: %llu cases (%llu on the float path), %llu mismatches\n",checked,hits,mismatches);
    return mismatches?1:0;
}
