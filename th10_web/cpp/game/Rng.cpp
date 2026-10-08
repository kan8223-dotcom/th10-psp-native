#include "Rng.hpp"
namespace th10 {
u16 Rng::next_word() noexcept {
    const u16 mixed = static_cast<u16>((seed ^ 0x9630u) + 0x9aadu);
    seed = static_cast<u16>((mixed << 2) | (mixed >> 14));
    ++calls;
    return seed;
}
// 0x44b9e0 / 0x44ba30. The first generated word is the high word.
u32 Rng::next_u32() noexcept {
    const u32 high = next_word();
    return (high << 16) | next_word();
}
// 0x43cb80. A zero bound consumes no random numbers.
u32 Rng::bounded(u32 exclusive_maximum) noexcept {
    return exclusive_maximum ? next_u32() % exclusive_maximum : 0;
}
static Extended unsigned_number(u32 value) noexcept {
    i32 signed_value; std::memcpy(&signed_value, &value, sizeof(value));
    auto result = Extended::from_int(signed_value);
    if (signed_value < 0) result = result + number(4294967296.0f);
    return result;
}
#if defined(TH10_FAST_RNG_UNIT) && TH10_FAST_RNG_UNIT
// th10_port (exact): in Precision 32 / nearest, signed_unit rounds u to 24 bits
// once (RNE): in the conversion's add of 2^32 when u >= 2^31, in the scaling
// multiply otherwise; the 2^-31 scale is exact at these magnitudes and the
// final subtraction runs on the float path and returns a tagged value. So the
// result is the tagged float rne24(u) * 2^-31 - 1.0f, without the SoftFloat
// add or multiply (one per call; 2,100 calls in one stage-4 tick, the device's
// 65 ms EnemiesUpdate spike). tools/rng_unit_check.cpp compares all 2^32 u.
float rng_rne24(u32 u) noexcept {
    if (u <= 0x1000000u) return static_cast<float>(static_cast<i32>(u));
    const u32 shift = 8u - static_cast<u32>(__builtin_clz(u)), half = 1u << (shift - 1u);   // bits beyond 24
    u32 kept = u >> shift; const u32 rest = u & ((1u << shift) - 1u);
    if (rest > half || (rest == half && (kept & 1u))) ++kept;
    return static_cast<float>(static_cast<i32>(kept)) * static_cast<float>(static_cast<i32>(1u << shift));
}
#endif
// 0x44bb20 / 0x44bb90. Preserve rounding after unsigned conversion.
Extended Rng::unit() noexcept { return unsigned_number(next_u32()) * number(0x1p-32f); }
Extended Rng::signed_unit() noexcept {
#if defined(TH10_FAST_RNG_UNIT) && TH10_FAST_RNG_UNIT
    if (arithmetic::single_nearest) return arithmetic::tagged(arithmetic::bits_of(rng_rne24(next_u32()) * 0x1p-31f - 1.0f));
#endif
    return unsigned_number(next_u32()) * number(0x1p-31f) - number(1.0f);
}
}
