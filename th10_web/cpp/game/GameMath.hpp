#pragma once
#include "Arithmetic.hpp"
namespace th10 {
Extended normalize_angle(float radians) noexcept;
Extended add_angle(float radians,float delta) noexcept;
// th10_port: add_angle(radians,delta).to_float() without the Extended round trip
// on TH10_FAST_ANGLE's float path. Off, call sites compile exactly to that expression.
#if defined(TH10_FAST_ANGLE_FLOAT) && TH10_FAST_ANGLE_FLOAT
float add_angle_float(float radians,float delta) noexcept;
#define TH10_ADD_ANGLE_FLOAT(radians,delta) add_angle_float(radians,delta)
#else
#define TH10_ADD_ANGLE_FLOAT(radians,delta) add_angle(radians,delta).to_float()
#endif
Extended angle_difference(float target,float current) noexcept;
Extended sine(Extended radians) noexcept;
Extended cosine(Extended radians) noexcept;
Extended tangent(Extended radians) noexcept;
Extended arccosine(Extended value) noexcept;
Extended remainder(Extended value,Extended divisor) noexcept;
Extended angle_to(Extended y, Extended x) noexcept;
// th10_port: angle_to(y, x).to_float(), exact (DfAtan2 when it can decide).
float angle_to_float(Extended y, Extended x) noexcept;
Vec2 polar(float radians, float length) noexcept;
// th10_port: polar() for an object that asks again with the same angle (see GameMath.cpp).
// Off, the call sites compile exactly to polar(radians,length) (byte-identical builds).
#if defined(TH10_TRIG_MEMO) && TH10_TRIG_MEMO
Vec2 polar_memo(const void* owner,float radians,float length) noexcept;
#define TH10_POLAR_MEMO(owner,radians,length) polar_memo(owner,radians,length)
#else
#define TH10_POLAR_MEMO(owner,radians,length) polar(radians,length)
#endif
void sincos_float(float radians, float& sine_value, float& cosine_value) noexcept;
}
