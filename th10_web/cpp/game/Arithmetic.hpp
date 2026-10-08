#pragma once
#include "Types.hpp"
#include <cstring>
#include "../../../portable/numeric/ScalarMath.hpp"

namespace th10 {
// Numeric compatibility for the original game's extended intermediate values.
// This is a number type, not an x86 register, instruction or execution engine.
// The platform adapter chooses precision and rounding for the current context.
//
// th10_port: significand/exponent always hold the value. A value that is
// exactly a normal binary32 or a signed zero also carries those float bits in
// reserved32, marked by reserved16 == float_tag, so the single-precision fast
// paths work on floats without converting. Code that changes significand or
// exponent directly must keep reserved32 in step (see magnitude()). The fast
// paths on tagged operands are inline below; every other case goes to the
// general functions in Arithmetic.cpp, which give the same results.
struct Extended {
    u64 significand = 0;
    u16 exponent = 0;
    u16 reserved16 = 0;
    u32 reserved32 = 0;
    static constexpr u16 float_tag = 0xf32f;
    Extended() = default;
    constexpr Extended(u64 significand_, u16 exponent_, u16 reserved16_, u32 reserved32_) noexcept
        : significand(significand_), exponent(exponent_), reserved16(reserved16_), reserved32(reserved32_) {}
    static Extended from_float(float value) noexcept;
    static Extended from_double(double value) noexcept;
    static Extended from_int(i32 value) noexcept;
    static Extended from_int64(i64 value) noexcept;
    float to_float() const noexcept;
    double to_double() const noexcept;
    i32 truncate_int() const noexcept;
    Extended round_to_integer() const noexcept;
    Extended square_root() const noexcept;
    bool is_nan() const noexcept;
    bool tagged() const noexcept { return reserved16 == float_tag; }
    // |value|: clears the sign of both representations.
    Extended magnitude() const noexcept { auto result = *this; result.exponent &= 0x7fff; result.reserved32 &= 0x7fffffffu; return result; }
    Extended operator-() const noexcept { auto result = *this; result.exponent ^= 0x8000; result.reserved32 ^= 0x80000000u; return result; }
    friend Extended operator+(const Extended& a, const Extended& b) noexcept;
    friend Extended operator-(const Extended& a, const Extended& b) noexcept;
    friend Extended operator*(const Extended& a, const Extended& b) noexcept;
    friend Extended operator/(const Extended& a, const Extended& b) noexcept;
    friend bool operator<(const Extended& a, const Extended& b) noexcept;
    friend bool operator==(const Extended& a, const Extended& b) noexcept;
};
static_assert(sizeof(Extended) == 16);
enum class Precision : u8 { Single = 32, Double = 64, Extended = 80 };
enum class Rounding : u8 { NearestEven = 0, TowardZero = 1, Down = 2, Up = 3 };
void arithmetic_mode(Precision precision, Rounding rounding) noexcept;
namespace arithmetic { extern bool single_nearest; }
// Precision 32 and round-to-nearest (kept by arithmetic_mode()).
inline bool single_precision_nearest() noexcept { return arithmetic::single_nearest; }
// th10_port: precision << 8 | rounding, for caches of mode-dependent results.
u32 arithmetic_mode_key() noexcept;

namespace arithmetic {
extern bool single_nearest;   // precision 32 and round-to-nearest (arithmetic_mode)
inline u32 bits_of(float value) noexcept { u32 bits; std::memcpy(&bits, &value, 4); return bits; }
inline float float_of(u32 bits) noexcept { float value; std::memcpy(&value, &bits, 4); return value; }
// Normal binary32 or signed zero (touhou::numeric::from_float accepts these).
inline bool representable(u32 bits) noexcept { const u32 biased = (bits >> 23) & 255u; return biased != 255u && (biased || !(bits & 0x7fffffu)); }
// A nonzero binary32 result that touhou::numeric::binary accepts: biased
// exponent 2..254 (not denormal, not at the f32 normal boundary, finite).
inline bool nonzero_accepted(u32 bits) noexcept { return ((bits >> 23) & 255u) - 2u < 253u; }
// The value of representable float bits in both representations
// (touhou::numeric::from_float's significand/exponent).
inline Extended tagged(u32 bits) noexcept {
    const u32 biased = (bits >> 23) & 255u;
    return Extended(biased ? u64((bits & 0x7fffffu) | 0x800000u) << 40 : 0,
                    static_cast<u16>(((bits >> 16) & 0x8000u) | (biased ? biased + 16256u : 0u)), Extended::float_tag, bits);
}
inline bool both_tagged(const Extended& a, const Extended& b) noexcept { return a.reserved16 == Extended::float_tag && b.reserved16 == Extended::float_tag; }
Extended from_float_general(float value) noexcept;
float to_float_general(const Extended& value) noexcept;
Extended from_int_general(i32 value) noexcept;
i32 truncate_general(const Extended& value) noexcept;
Extended add_general(const Extended& a, const Extended& b) noexcept;
Extended sub_general(const Extended& a, const Extended& b) noexcept;
Extended mul_general(const Extended& a, const Extended& b) noexcept;
Extended div_general(const Extended& a, const Extended& b) noexcept;
bool less_general(const Extended& a, const Extended& b) noexcept;
bool equal_general(const Extended& a, const Extended& b) noexcept;
}
namespace arithmetic {
// The fast paths. TH10_EXTENDED_INLINE (default 1) expands them at every use;
// 0 keeps one out-of-line copy each in Arithmetic.cpp (smaller code).
inline Extended from_float_fast(float value) noexcept {
    const u32 bits = bits_of(value);
    if (__builtin_expect(representable(bits), 1)) return tagged(bits);
    return from_float_general(value);
}
inline float to_float_fast(const Extended& value) noexcept {
    if (__builtin_expect(value.reserved16 == Extended::float_tag, 1)) return float_of(value.reserved32);
    return to_float_general(value);
}
// |value| <= 2^24 is exact in binary32, in the same normalized form.
inline Extended from_int_fast(i32 value) noexcept {
    if (__builtin_expect(value >= -16777216 && value <= 16777216, 1)) return tagged(bits_of(static_cast<float>(value)));
    return from_int_general(value);
}
// A tagged value below 2^31 in magnitude truncates like the MSVC helper's
// low word of the signed 64-bit conversion.
inline i32 truncate_fast(const Extended& value) noexcept {
    if (__builtin_expect(value.reserved16 == Extended::float_tag, 1)) {
        const float f = float_of(value.reserved32);
        if (f > -2147483648.0f && f < 2147483648.0f) return static_cast<i32>(f);
    }
    return truncate_general(value);
}
// touhou::numeric::binary's single-precision fast path. A zero result is
// accepted only where it is not an underflow (x == -y, x == y, a zero
// factor, x == 0); anything declined here is decided by *_general.
inline Extended add_fast(const Extended& a, const Extended& b) noexcept {
    if (__builtin_expect(single_nearest && both_tagged(a, b), 1)) {
        const float x = float_of(a.reserved32), y = float_of(b.reserved32); const u32 bits = bits_of(x + y);
        if (__builtin_expect((bits & 0x7fffffffu) ? nonzero_accepted(bits) : x == -y, 1)) return tagged(bits);
    }
    return add_general(a, b);
}
inline Extended sub_fast(const Extended& a, const Extended& b) noexcept {
    if (__builtin_expect(single_nearest && both_tagged(a, b), 1)) {
        const float x = float_of(a.reserved32), y = float_of(b.reserved32); const u32 bits = bits_of(x - y);
        if (__builtin_expect((bits & 0x7fffffffu) ? nonzero_accepted(bits) : x == y, 1)) return tagged(bits);
    }
    return sub_general(a, b);
}
inline Extended mul_fast(const Extended& a, const Extended& b) noexcept {
    if (__builtin_expect(single_nearest && both_tagged(a, b), 1)) {
        const float x = float_of(a.reserved32), y = float_of(b.reserved32); const u32 bits = bits_of(x * y);
        if (__builtin_expect((bits & 0x7fffffffu) ? nonzero_accepted(bits) : (x == 0 || y == 0), 1)) return tagged(bits);
    }
    return mul_general(a, b);
}
inline Extended div_fast(const Extended& a, const Extended& b) noexcept {
    if (__builtin_expect(single_nearest && both_tagged(a, b), 1)) {
        const float x = float_of(a.reserved32), y = float_of(b.reserved32); const u32 bits = bits_of(x / y);
        if (__builtin_expect((bits & 0x7fffffffu) ? nonzero_accepted(bits) : x == 0, 1)) return tagged(bits);
    }
    return div_general(a, b);
}
// Comparisons are exact in any mode.
inline bool less_fast(const Extended& a, const Extended& b) noexcept {
    if (__builtin_expect(both_tagged(a, b), 1)) return float_of(a.reserved32) < float_of(b.reserved32);
    return less_general(a, b);
}
inline bool equal_fast(const Extended& a, const Extended& b) noexcept {
    if (__builtin_expect(both_tagged(a, b), 1)) return float_of(a.reserved32) == float_of(b.reserved32);
    return equal_general(a, b);
}
}
#ifndef TH10_EXTENDED_INLINE
#define TH10_EXTENDED_INLINE 1
#endif
#if TH10_EXTENDED_INLINE
inline Extended Extended::from_float(float value) noexcept { return arithmetic::from_float_fast(value); }
inline float Extended::to_float() const noexcept { return arithmetic::to_float_fast(*this); }
inline Extended Extended::from_int(i32 value) noexcept { return arithmetic::from_int_fast(value); }
inline i32 Extended::truncate_int() const noexcept { return arithmetic::truncate_fast(*this); }
inline Extended operator+(const Extended& a, const Extended& b) noexcept { return arithmetic::add_fast(a, b); }
inline Extended operator-(const Extended& a, const Extended& b) noexcept { return arithmetic::sub_fast(a, b); }
inline Extended operator*(const Extended& a, const Extended& b) noexcept { return arithmetic::mul_fast(a, b); }
inline Extended operator/(const Extended& a, const Extended& b) noexcept { return arithmetic::div_fast(a, b); }
inline bool operator<(const Extended& a, const Extended& b) noexcept { return arithmetic::less_fast(a, b); }
inline bool operator==(const Extended& a, const Extended& b) noexcept { return arithmetic::equal_fast(a, b); }
#endif
using Scalar=touhou::numeric::ScalarMath<Extended,single_precision_nearest>;
inline Extended number(float value) noexcept { return Extended::from_float(value); }
}
