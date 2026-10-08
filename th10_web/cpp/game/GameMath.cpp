#include "GameMath.hpp"
#include "../../../portable/numeric/DfAtan2.hpp"
#include "../../../portable/numeric/DfTrig.hpp"
#include <cmath>
namespace th10 {
// 0x44bc70. Both loops share a limit; there is no intermediate float store.
namespace {
Extended wrap_angle(Extended result) noexcept {
    const auto pi=number(3.1415927410125732421875f),tau=number(6.283185482025146484375f);
    i32 iterations=0;
    while(pi<result){result=result-tau;if(iterations++>32)break;}
    while(result<-pi){result=result+tau;if(iterations++>32)break;}
    return result;
}
}
#if TH10_FAST_ANGLE
namespace {
// th10_port: wrap_angle(number(a)+number(d)) in plain floats when the Extended
// fast paths accept every step: precision 32 nearest and |a|,|d| in
// {0}u[2^-100,2^100]. Then a, d, pi and tau are multiples of 2^-123, so every
// sum or difference is 0 (an exact cancellation, which the fast path accepts)
// or a multiple of 2^-123 below 2^102 (biased exponent 4..229, accepted), and
// the comparisons are exact: the same floats, returned as the same tagged value.
inline bool angle_operand(float v) noexcept {const float a=std::fabs(v);return a==0||(a>=0x1p-100f&&a<=0x1p100f);}
inline float wrap_angle_value(float result) noexcept {
    i32 iterations=0;
    while(3.1415927410125732421875f<result){result=result-6.283185482025146484375f;if(iterations++>32)break;}
    while(result<-3.1415927410125732421875f){result=result+6.283185482025146484375f;if(iterations++>32)break;}
    return result;
}
inline Extended wrap_angle_float(float result) noexcept {return arithmetic::tagged(arithmetic::bits_of(wrap_angle_value(result)));}
}
#endif
Extended normalize_angle(float radians) noexcept {
#if TH10_FAST_ANGLE
    if(single_precision_nearest()&&angle_operand(radians))return wrap_angle_float(radians);
#endif
    return wrap_angle(number(radians));}
Extended add_angle(float radians,float delta) noexcept {
#if TH10_FAST_ANGLE
    if(single_precision_nearest()&&angle_operand(radians)&&angle_operand(delta))return wrap_angle_float(radians+delta);
#endif
    return wrap_angle(number(radians)+number(delta));}
#if defined(TH10_FAST_ANGLE_FLOAT) && TH10_FAST_ANGLE_FLOAT
// The float path's value is the tagged result's float (tools/angle_check.cpp).
float add_angle_float(float radians,float delta) noexcept {
#if TH10_FAST_ANGLE
    if(single_precision_nearest()&&angle_operand(radians)&&angle_operand(delta))return wrap_angle_value(radians+delta);
#endif
    return add_angle(radians,delta).to_float();
}
#endif
// 0x408660 / 0x428ce0: a single wrap, including the original store boundaries.
Extended angle_difference(float target,float current) noexcept {
    const auto difference=number(target)-number(current);
    if(number(3.1415927410125732f)<difference)return number(target)-(number(current)+number(6.2831854820251465f));
    if(number(3.1415927410125732f)<number(current)-number(target))return number(target)-(number(current)-number(6.2831854820251465f));
    return difference;
}
// These use the same libm precision as the existing web backend. Exact native
// x87 transcendental rounding still requires an independent hardware oracle.
Extended sine(Extended radians) noexcept {return Extended::from_double(std::sin(radians.to_double()));}
Extended cosine(Extended radians) noexcept {return Extended::from_double(std::cos(radians.to_double()));}
Extended tangent(Extended radians) noexcept {return Extended::from_double(std::tan(radians.to_double()));}
Extended arccosine(Extended value) noexcept {return Extended::from_double(std::acos(value.to_double()));}
Extended remainder(Extended value,Extended divisor) noexcept {return Extended::from_double(std::fmod(value.to_double(),divisor.to_double()));}
Extended angle_to(Extended y,Extended x) noexcept {return Extended::from_double(std::atan2(y.to_double(),x.to_double()));}
// th10_port: DfAtan2 gives the bits of the libm result rounded to nearest,
// which is what to_float() makes of it in single-precision/nearest mode.
float angle_to_float(Extended y,Extended x) noexcept {
    float out;
    if(arithmetic::single_nearest&&y.tagged()&&x.tagged()&&touhou::numeric::df::atan2_float(y.to_float(),x.to_float(),out))return out;
    return angle_to(y,x).to_float();
}
// 0x4501b0.
Vec2 polar(float radians,float length) noexcept {
    // th10_port: bit-exact binary32 fast path (portable/numeric/DfTrig.hpp);
    // declines near rounding boundaries and outside its range.
    Vec2 fast;if(touhou::numeric::df::sincos_scaled(radians,length,fast.x,fast.y))return fast;
    return {(cosine(number(radians))*number(length)).to_float(),(sine(number(radians))*number(length)).to_float()};
}
#if defined(TH10_TRIG_MEMO) && TH10_TRIG_MEMO
namespace {
// th10_port (TH08 psp/trig_df_memo.hpp sha d27cce0d, r259 per-bullet memo): the
// angle half of the fast polar (reduction and evaluation) for the last angle an
// object asked for; the length half runs every time. Bullets repeat their angle
// on 79-95% of their velocity updates (PC, stage-4 demo / stage-3 replay).
// Keyed by the object's address: bullets are 0x7f0 bytes apart (127 x 16, odd),
// so 2048 slots on (address >> 4) give each bullet its own slot. The half only
// depends on the angle bits, so a shared slot is only a miss, never a wrong value.
struct PolarMemo {const void* owner;u32 bits;touhou::numeric::df::SinCosAngle angle;};
PolarMemo polar_memo_slots[2048];
}
#ifdef TH10_TRIG_MEMO_PROBE
extern "C" {unsigned long long th10_polar_memo_calls=0,th10_polar_memo_hits=0;}
#endif
Vec2 polar_memo(const void* owner,float radians,float length) noexcept {
    auto& e=polar_memo_slots[(reinterpret_cast<uintptr_t>(owner)>>4)&2047u];u32 bits;__builtin_memcpy(&bits,&radians,4);
#ifdef TH10_TRIG_MEMO_PROBE
    ++th10_polar_memo_calls;if(e.owner==owner&&e.bits==bits)++th10_polar_memo_hits;
#endif
    if(e.owner!=owner||e.bits!=bits){e.owner=owner;e.bits=bits;e.angle=touhou::numeric::df::sincos_angle(radians);}
    Vec2 fast;if(touhou::numeric::df::sincos_length(e.angle,radians,length,fast.x,fast.y))return fast;
    return {(cosine(number(radians))*number(length)).to_float(),(sine(number(radians))*number(length)).to_float()};
}
#endif
// th10_port: sine(number(r)).to_float() and cosine(number(r)).to_float().
void sincos_float(float radians,float& sine_value,float& cosine_value) noexcept {
    if(touhou::numeric::df::sincos_scaled(radians,1.0f,cosine_value,sine_value))return;
    sine_value=sine(number(radians)).to_float();cosine_value=cosine(number(radians)).to_float();
}
}
