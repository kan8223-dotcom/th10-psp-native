#pragma once
#include <cmath>
#include <initializer_list>
namespace touhou::numeric {
// Only selected for 24-bit nearest-even geometry, with bounded operands.
// Separate f32 operations retain original rounding order (-ffp-contract=off).
// Other arithmetic modes and unusual scales use the original Extended path.
struct SpriteNumber {
    float value;
    SpriteNumber():value(0){}
    explicit SpriteNumber(float v):value(v){}
    static SpriteNumber from_double(double v){return SpriteNumber(float(v));}
    float to_float()const{return value;}
    double to_double()const{return value;}
    SpriteNumber operator-()const{return SpriteNumber(-value);}
    friend SpriteNumber operator+(SpriteNumber a,SpriteNumber b){return SpriteNumber(a.value+b.value);}
    friend SpriteNumber operator-(SpriteNumber a,SpriteNumber b){return SpriteNumber(a.value-b.value);}
    friend SpriteNumber operator*(SpriteNumber a,SpriteNumber b){return SpriteNumber(a.value*b.value);}
    friend SpriteNumber operator/(SpriteNumber a,SpriteNumber b){return SpriteNumber(a.value/b.value);}
#if TH10_FAST_EASING
    friend bool operator<(SpriteNumber a,SpriteNumber b){return a.value<b.value;}
#endif
};
inline bool sprite_range(std::initializer_list<float> values){
    for(float v:values){const float a=std::fabs(v);if(a!=0&&!(a>=0x1p-20f&&a<=0x1p20f))return false;}
    return true;
}
#if TH10_SPRITE_ROT_RANGE
// th10_port: the rotation's cos/sin need only |v| >= 2^-40 (not 2^-20; the upper
// bound stays 2^20, so this passes whenever sprite_range did). With the
// other operands in {0}u[2^-20,2^20]: half extents >= 2^-41, products with c/s
// >= 2^-81, so every nonzero result is a multiple of 2^-104 (>= 2^-125, the
// Extended fast path's floor) and zeros stay exact cancellations or zero
// factors. Cardinal angles (|cos(pi/2)| = 4.4e-8) no longer fall back.
inline bool sprite_rotation_range(float c,float s){
    const float a=std::fabs(c),b=std::fabs(s);
    return (a==0||(a>=0x1p-40f&&a<=0x1p20f))&&(b==0||(b>=0x1p-40f&&b<=0x1p20f));
}
#endif
}
