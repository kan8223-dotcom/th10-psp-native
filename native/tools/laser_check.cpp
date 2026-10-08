// Checks TH10_FAST_LASER (game/PlayerCollision.cpp Player::collide_laser
// rotation): the fast rotation must give the same rotated_x bits and the same
// rotated_y Extended bytes as the original libm/Extended lines, for random and
// edge operands (zeros, angles a few ulps around multiples of pi/2, the DF
// length range edges, subnormals, infinities, NaNs, playfield values), in
// every precision/rounding mode the game uses.
//   cmake --build <dir> --target th10_laser_check && <dir>/th10_laser_check [millions]
#include "../../th10_web/cpp/game/GameMath.hpp"
#include "../../th10_web/cpp/game/FastFloat.hpp"
#include "../../portable/numeric/DfTrig.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
#if defined(TH10_STATIC_I386_FMOD)
// glibc 2.39's static i386 libm exports only __ieee754_fmod (main.cpp does the same).
extern "C" double __ieee754_fmod(double,double);
extern "C" double fmod(double x,double y){return __ieee754_fmod(x,y);}
#endif
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
static unsigned long long hits=0;
// ---- original (verbatim) ----
static void rotate_original(float x,float y,float angle,float& rotated_x,Extended& rotated_y){
    const auto sin=sine(-number(angle)),cos=cosine(-number(angle));
    rotated_x=(number(x)*cos-sin*number(y)).to_float();rotated_y=sin*number(x)+cos*number(y);
}
// ---- TH10_FAST_LASER (verbatim apart from counting) ----
static void rotate_fast(float x,float y,float angle,float& rotated_x,Extended& rotated_y){
    const touhou::numeric::df::SinCosAngle a=touhou::numeric::df::sincos_angle(-angle);float xc,xs,yc,ys,rx,ry;
    if(single_precision_nearest()&&touhou::numeric::df::sincos_length(a,-angle,x,xc,xs)&&touhou::numeric::df::sincos_length(a,-angle,y,yc,ys)&&
       fast_float::sub(xc,ys,rx)&&fast_float::add(xs,yc,ry)){rotated_x=rx;rotated_y=arithmetic::tagged(arithmetic::bits_of(ry));++hits;}
    else{const auto sin=sine(-number(angle)),cos=cosine(-number(angle));
        rotated_x=(number(x)*cos-sin*number(y)).to_float();rotated_y=sin*number(x)+cos*number(y);}
}
static u64 state=0xbb67ae8584caa73bull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x80000000u,0x00000001u,0x007fffffu,0x00800000u,0x3f800000u,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc00000u,
    0x1d800000u,0x1d7fffffu,0x5d800000u,0x5d7fffffu,0x3fc90fdbu,0x40490fdbu,0x40c90fdbu,0x4096cbe4u};
static float value(bool in){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(in?2+r%4:r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%3200)-1600)/4.0f));                    // player - laser origin, quarter units
    case 3:return fbits(bits(float((r>>8)%65536)/128.0f)|sign);                        // 0..512
    case 4:return fbits(bits(float(i32((r>>8)%20000)-10000)*0.00062831855f));         // angles around +-2pi
    default:return fbits(((u32(100+(r>>8)%50)<<23)|u32((r>>16)&0x7fffffu))|sign);    // 2^-27..2^22
    }
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):10;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        const u64 n=(m.p==Precision::Single&&m.r==Rounding::NearestEven?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){const bool in=k&3;
            const float x=value(in),y=value(in);float angle=value(in);
            if((next()&3)==0){const u32 base=special[14+next()%4];angle=fbits((base+u32(i32(next()%9)-4))|(next()&1?0x80000000u:0));}   // a few ulps around k pi/2
            float x1,x2;Extended y1,y2;rotate_original(x,y,angle,x1,y1);rotate_fast(x,y,angle,x2,y2);++checked;
            const bool same_x=bits(x1)==bits(x2)||(std::isnan(x1)&&std::isnan(x2));
            const bool same_y=!std::memcmp(&y1,&y2,sizeof(Extended))||(y1.is_nan()&&y2.is_nan());
            if(!same_x||!same_y){if(mismatches<8)std::printf("MISMATCH mode=%d/%d x=%08x y=%08x angle=%08x rx %08x/%08x\n",int(m.p),int(m.r),bits(x),bits(y),bits(angle),bits(x1),bits(x2));++mismatches;}
        }}
    std::printf("laser_check: %llu cases (%llu on the fast path), %llu mismatches\n",checked,hits,mismatches);
    return mismatches?1:0;
}
