#include "Arithmetic.hpp"
#include <cmath>
#include "../../../portable/numeric/ExactFloat.hpp"

namespace th10 {
namespace {
struct FloatBits32 { u32 value; };
struct FloatBits64 { u64 value; };
}
// th10_port: kept outside the unnamed namespace. GCC 15 (psp-gcc) gives
// extern "C" variables declared there internal linkage and mangles them.
extern "C" {
extern u8 softfloat_roundingMode, extF80_roundingPrecision;
void f32_to_extF80M(FloatBits32, Extended*);
void f64_to_extF80M(FloatBits64, Extended*);
void i32_to_extF80M(i32, Extended*);
void i64_to_extF80M(i64, Extended*);
FloatBits32 extF80M_to_f32(const Extended*);
FloatBits64 extF80M_to_f64(const Extended*);
i32 extF80M_to_i32(const Extended*, u8, bool);
i64 extF80M_to_i64(const Extended*, u8, bool);
void extF80M_add(const Extended*, const Extended*, Extended*);
void extF80M_sub(const Extended*, const Extended*, Extended*);
void extF80M_mul(const Extended*, const Extended*, Extended*);
void extF80M_div(const Extended*, const Extended*, Extended*);
void extF80M_roundToInt(const Extended*, u8, bool, Extended*);
void extF80M_sqrt(const Extended*, Extended*);
bool extF80M_lt_quiet(const Extended*, const Extended*);
bool extF80M_eq(const Extended*, const Extended*);
}
namespace arithmetic {
// th10_port: extF80_roundingPrecision == 32 && softfloat_roundingMode == 0,
// kept by arithmetic_mode() (SoftFloat starts at precision 80).
bool single_nearest = false;
}
namespace {
using arithmetic::bits_of;using arithmetic::float_of;using arithmetic::representable;using arithmetic::nonzero_accepted;
// SoftFloat stores results by struct assignment, so the padding after its
// {signif, signExp} (our reserved16/reserved32) may receive stack bytes
// (seen on i386: our own tag).
inline Extended& untagged(Extended& value) noexcept { value.reserved16 = 0; return value; }
// touhou::numeric::to_float, answered from the tag when present.
inline bool operand(const Extended& value, float& out) noexcept {
    if (value.reserved16 == Extended::float_tag) { out = float_of(value.reserved32); return true; }
    return touhou::numeric::to_float(value, out);
}
}
void arithmetic_mode(Precision precision, Rounding rounding) noexcept {
    extF80_roundingPrecision = static_cast<u8>(precision);
    softfloat_roundingMode = static_cast<u8>(rounding);
    arithmetic::single_nearest = extF80_roundingPrecision == 32 && softfloat_roundingMode == 0;
}
u32 arithmetic_mode_key() noexcept { return u32(extF80_roundingPrecision) << 8 | softfloat_roundingMode; }
namespace arithmetic {
Extended from_float_general(float value) noexcept {
    const u32 bits = bits_of(value);
    if (representable(bits)) return tagged(bits);
    Extended result; f32_to_extF80M({bits}, &result); return untagged(result);
}
float to_float_general(const Extended& value) noexcept {
    if (value.reserved16 == Extended::float_tag) return float_of(value.reserved32);
    float fast;if(touhou::numeric::to_float(value,fast))return fast;
    // th10_port: exact round-to-nearest-even narrowing of normal results
    // (checked against extF80M_to_f32 on 1.6e9 random/tie/carry cases).
    if(softfloat_roundingMode==0){
        const u32 e=value.exponent&0x7fffu,sign=u32(value.exponent&0x8000u)<<16;
        if(e>=16257u&&e<=16510u&&(value.significand>>63)){
            u64 mantissa=value.significand>>40;const u64 rest=value.significand&0xffffffffffull;
            if(rest>0x8000000000ull||(rest==0x8000000000ull&&(mantissa&1)))++mantissa;
            u32 biased=e-16256u;if(mantissa>>24){mantissa>>=1;++biased;}
            if(biased<=254u){const u32 bits=sign|(biased<<23)|u32(mantissa&0x7fffffu);float result;std::memcpy(&result,&bits,4);return result;}
        }
    }
    const u32 bits = extF80M_to_f32(&value).value;
    float result; std::memcpy(&result, &bits, sizeof(result)); return result;
}
// th10_port: touhou::numeric::binary's single-precision fast path on the
// float operands (tag or exact conversion), then SoftFloat.
Extended add_general(const Extended& a, const Extended& b) noexcept {
    float x,y;
    if(single_nearest&&operand(a,x)&&operand(b,y)){const u32 bits=bits_of(x+y);if((bits&0x7fffffffu)?nonzero_accepted(bits):x==-y)return tagged(bits);}
    Extended result;extF80M_add(&a,&b,&result);return untagged(result);
}
Extended sub_general(const Extended& a, const Extended& b) noexcept {
    float x,y;
    if(single_nearest&&operand(a,x)&&operand(b,y)){const u32 bits=bits_of(x-y);if((bits&0x7fffffffu)?nonzero_accepted(bits):x==y)return tagged(bits);}
    Extended result;extF80M_sub(&a,&b,&result);return untagged(result);
}
Extended mul_general(const Extended& a, const Extended& b) noexcept {
    float x,y;
    if(single_nearest&&operand(a,x)&&operand(b,y)){const u32 bits=bits_of(x*y);if((bits&0x7fffffffu)?nonzero_accepted(bits):(x==0||y==0))return tagged(bits);}
    Extended result;extF80M_mul(&a,&b,&result);return untagged(result);
}
Extended div_general(const Extended& a, const Extended& b) noexcept {
    float x,y;
    if(single_nearest&&operand(a,x)&&operand(b,y)){const u32 bits=bits_of(x/y);if((bits&0x7fffffffu)?nonzero_accepted(bits):x==0)return tagged(bits);}
    Extended result;extF80M_div(&a,&b,&result);return untagged(result);
}
bool less_general(const Extended& a, const Extended& b) noexcept { float x,y;if(operand(a,x)&&operand(b,y))return x<y;return extF80M_lt_quiet(&a,&b); }
bool equal_general(const Extended& a, const Extended& b) noexcept { float x,y;if(operand(a,x)&&operand(b,y))return x==y;return extF80M_eq(&a,&b); }
}
#if !TH10_EXTENDED_INLINE
Extended Extended::from_float(float value) noexcept { return arithmetic::from_float_fast(value); }
float Extended::to_float() const noexcept { return arithmetic::to_float_fast(*this); }
Extended Extended::from_int(i32 value) noexcept { return arithmetic::from_int_fast(value); }
i32 Extended::truncate_int() const noexcept { return arithmetic::truncate_fast(*this); }
Extended operator+(const Extended& a, const Extended& b) noexcept { return arithmetic::add_fast(a, b); }
Extended operator-(const Extended& a, const Extended& b) noexcept { return arithmetic::sub_fast(a, b); }
Extended operator*(const Extended& a, const Extended& b) noexcept { return arithmetic::mul_fast(a, b); }
Extended operator/(const Extended& a, const Extended& b) noexcept { return arithmetic::div_fast(a, b); }
bool operator<(const Extended& a, const Extended& b) noexcept { return arithmetic::less_fast(a, b); }
bool operator==(const Extended& a, const Extended& b) noexcept { return arithmetic::equal_fast(a, b); }
#endif
Extended Extended::from_double(double value) noexcept {
    // th10_port: a double that is exactly a normal binary32 (or a signed zero)
    // is that float, in the same significand/exponent as from_double gives.
    u64 bits; std::memcpy(&bits, &value, 8);
    const u32 biased = static_cast<u32>(bits >> 52) & 2047u, sign = static_cast<u32>(bits >> 32) & 0x80000000u;
    if (!(bits & 0x7fffffffffffffffull)) return arithmetic::tagged(sign);
    if (biased >= 897u && biased <= 1150u && !(bits & 0x1fffffffull))
        return arithmetic::tagged(sign | ((biased - 896u) << 23) | (static_cast<u32>(bits >> 29) & 0x7fffffu));
    Extended fast;if(touhou::numeric::from_double(value,fast))return fast;
    Extended result; f64_to_extF80M({bits}, &result); return untagged(result);
}
Extended arithmetic::from_int_general(i32 value) noexcept {
    // th10_port: |value| <= 2^24 is exact in binary32 (same normalized form).
    if (value >= -16777216 && value <= 16777216) return tagged(bits_of(static_cast<float>(value)));
    return touhou::numeric::from_integer<Extended>(value);
}
Extended Extended::from_int64(i64 value) noexcept {
    return touhou::numeric::from_integer<Extended>(value);
}
double Extended::to_double() const noexcept {
    double fast;if(touhou::numeric::to_double(*this,fast))return fast;
    const u64 bits = extF80M_to_f64(this).value;
    double result; std::memcpy(&result, &bits, sizeof(result)); return result;
}
i32 arithmetic::truncate_general(const Extended& value) noexcept {
    i32 fast;if(touhou::numeric::truncate_low_word(value,fast))return fast;
    // The original MSVC helper converts to signed 64 bits, then uses EAX.
    // This also preserves its low-word result for out-of-range/NaN inputs.
    const u32 bits = static_cast<u32>(extF80M_to_i64(&value, 1, false));
    i32 result; std::memcpy(&result, &bits, sizeof(result)); return result;
}
bool Extended::is_nan() const noexcept {
    return (exponent & 0x7fff) == 0x7fff && (significand & 0x7fffffffffffffffull);
}
Extended Extended::round_to_integer() const noexcept {
    // th10_port: exact fast path for FRNDINT under round-to-nearest-even.
    // Integers below 2^23 are exact in float and (|f|+2^23)-2^23 rounds half
    // to even in IEEE single arithmetic; checked against extF80M_roundToInt
    // for every finite normal float and zero (0 mismatches).
    float f;
    if(softfloat_roundingMode==0&&operand(*this,f)){
        const float magnitude=std::fabs(f);if(magnitude>=8388608.f)return *this;
        const u32 bits=bits_of(std::copysign((magnitude+8388608.f)-8388608.f,f));
        if(representable(bits))return arithmetic::tagged(bits);
    }
    Extended result;extF80M_roundToInt(this,softfloat_roundingMode,false,&result);return untagged(result);}
Extended Extended::square_root() const noexcept {
    float x;
    if(arithmetic::single_nearest&&operand(*this,x)){const u32 bits=bits_of(std::sqrt(x));if(representable(bits))return arithmetic::tagged(bits);}
    Extended result;extF80M_sqrt(this,&result);return untagged(result);
}
}
