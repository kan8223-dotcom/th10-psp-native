// Checks TH10_FAST_FEATURES (game/BulletFeatures.cpp mul_add, spawn_speed) and
// TH10_FAST_OUTSIDE (game/BulletFrame.cpp bullet_outside_playfield general
// test) against the original Extended operations: random and edge operands
// (zeros, the 2^-20/2^20 range edges, exact cancellations, touching edges,
// subnormals, infinities, NaNs, playfield values), every precision/rounding
// mode the game uses.
//   cmake --build <dir> --target th10_feature_check && <dir>/th10_feature_check [millions]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
struct V3 {float x,y,z;};
// ---- originals (verbatim) ----
static float mul_add_original(float a,float b,float c){return (number(a)*number(b)+number(c)).to_float();}
static float spawn_original(float fractional,float speed){return (number(5.f)-number(fractional)*number(.3125f)+number(speed)).to_float();}
static bool outside_original(const V3& point,float width,float height,bool extended_top){
    const auto half_x=number(width)*number(.5f),half_y=number(height)*number(.5f);
    const auto left=half_x+number(point.x),right=number(point.x)-half_x,top=half_y+number(point.y),bottom=number(point.y)-half_y;
    const auto top_edge=number(extended_top?-64.f:0.f);
    return left<number(-192.f)||left==number(-192.f)||number(192.f)<right||right==number(192.f)||top<top_edge||top==top_edge||number(448.f)<bottom||bottom==number(448.f);
}
// ---- fast paths (verbatim apart from counting) ----
static unsigned long long hits_mul_add=0,hits_spawn=0,hits_outside=0;
inline bool feature_operand(float v){return arithmetic::representable(arithmetic::bits_of(v));}
inline bool accepted_mul(float x,float y,float r){const u32 b=arithmetic::bits_of(r);return (b&0x7fffffffu)?arithmetic::nonzero_accepted(b):(x==0||y==0);}
inline bool accepted_add(float x,float y,float r){const u32 b=arithmetic::bits_of(r);return (b&0x7fffffffu)?arithmetic::nonzero_accepted(b):x==-y;}
inline bool accepted_sub(float x,float y,float r){const u32 b=arithmetic::bits_of(r);return (b&0x7fffffffu)?arithmetic::nonzero_accepted(b):x==y;}
static float mul_add(float a,float b,float c){
    if(single_precision_nearest()&&feature_operand(a)&&feature_operand(b)&&feature_operand(c)){
        const float product=a*b;if(accepted_mul(a,b,product)){const float sum=product+c;if(accepted_add(product,c,sum)){++hits_mul_add;return sum;}}}
    return (number(a)*number(b)+number(c)).to_float();
}
static float spawn_speed(float fractional,float speed){
    if(single_precision_nearest()&&feature_operand(fractional)&&feature_operand(speed)){
        const float step=fractional*.3125f;
        if(accepted_mul(fractional,.3125f,step)){const float rest=5.f-step;
            if(accepted_sub(5.f,step,rest)){const float sum=rest+speed;if(accepted_add(rest,speed,sum)){++hits_spawn;return sum;}}}}
    return (number(5.f)-number(fractional)*number(.3125f)+number(speed)).to_float();
}
static bool outside_fast(const V3& point,float width,float height,bool extended_top){
    {   const auto in_range=[](float v){const float a=std::fabs(v);return a==0||(a>=0x1p-20f&&a<=0x1p20f);};
        if(single_precision_nearest()&&in_range(width)&&in_range(height)&&in_range(point.x)&&in_range(point.y)){
            const float half_x=width*.5f,half_y=height*.5f,left=half_x+point.x,right=point.x-half_x,top=half_y+point.y,bottom=point.y-half_y,top_edge=extended_top?-64.f:0.f;
            ++hits_outside;return left<=-192.f||192.f<=right||top<=top_edge||448.f<=bottom;
        }
    }
    return outside_original(point,width,height,extended_top);
}
static u64 state=0x3c6ef372fe94f82bull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x35800000u,0x357fffffu,0x49800000u,0x49800001u,
    0x3f800000u,0x40a00000u,0x3ea00000u,0x41800000u,0x43400000u,0x43e00000u,0x42800000u,0x7f7fffffu,0x7f800000u,0x7fc00000u};
static float draw_value(bool in_range){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(in_range?2+r%4:r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%2400)-1200)/4.0f));                 // playfield, quarter units
    case 3:return fbits(bits(float((r>>8)%4096)/256.0f)|sign);                      // 0..16, timer fractions, speeds
    case 4:return fbits(((u32(105+(r>>8)%8)<<23)|u32((r>>16)&0x7fffffu))|sign);   // near 2^-20
    default:return fbits(((u32(127+(r>>8)%20)<<23)|u32((r>>16)&0x7fffffu))|sign); // 1..2^20
    }
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        const u64 n=(m.p==Precision::Single&&m.r==Rounding::NearestEven?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){const bool in=k&1;
            const float a=(next()&3)?1.0f:draw_value(in),b=draw_value(in);float c=draw_value(in);if((next()&7)==0)c=fbits(bits(a*b)^0x80000000u);
            const float r1=mul_add_original(a,b,c),r2=mul_add(a,b,c);++checked;
            if(bits(r1)!=bits(r2)&&!(std::isnan(r1)&&std::isnan(r2))){if(mismatches<8)std::printf("MULADD MISMATCH %08x %08x %08x\n",bits(a),bits(b),bits(c));++mismatches;}
            const float f=draw_value(in);float sp=draw_value(in);if((next()&7)==0)sp=fbits(bits(5.f-f*.3125f)^0x80000000u);
            const float s1=spawn_original(f,sp),s2=spawn_speed(f,sp);++checked;
            if(bits(s1)!=bits(s2)&&!(std::isnan(s1)&&std::isnan(s2))){if(mismatches<8)std::printf("SPAWN MISMATCH %08x %08x\n",bits(f),bits(sp));++mismatches;}
            V3 p{draw_value(in),draw_value(in),0};const float w=draw_value(in),h=draw_value(in);const bool top=next()&1;
            if((next()&3)==0)p.x=-192.f-w*.5f;else if((next()&3)==0)p.y=448.f+h*.5f;   // touching edges
            const bool o1=outside_original(p,w,h,top),o2=outside_fast(p,w,h,top);++checked;
            if(o1!=o2){if(mismatches<8)std::printf("OUTSIDE MISMATCH x=%08x y=%08x w=%08x h=%08x\n",bits(p.x),bits(p.y),bits(w),bits(h));++mismatches;}
        }}
    std::printf("feature_check: %llu cases (float path: mul_add %llu, spawn %llu, outside %llu), %llu mismatches\n",checked,hits_mul_add,hits_spawn,hits_outside,mismatches);
    return mismatches?1:0;
}
