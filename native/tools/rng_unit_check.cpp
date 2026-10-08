// th10_port: TH10_FAST_RNG_UNIT audit. Rng::signed_unit's original Extended
// expression against the fast float form for every u32 in [begin, end), in
// Precision 32 / nearest: significand, exponent, the tag and the tagged bits.
// usage: th10_rng_unit_check [begin] [end]   (hex or decimal; default all 2^32)
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace th10 { float rng_rne24(u32 u) noexcept; }
using namespace th10;
static Extended original(u32 value) {
    i32 signed_value; std::memcpy(&signed_value, &value, sizeof(value));
    auto result = Extended::from_int(signed_value);
    if (signed_value < 0) result = result + number(4294967296.0f);
    return result * number(0x1p-31f) - number(1.0f);
}
int main(int argc, char** argv) {
    arithmetic_mode(Precision::Single, Rounding::NearestEven);
    const unsigned long long begin = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 0, end = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 0x100000000ull;
    unsigned long long checked = 0, bad = 0;
    for (unsigned long long v = begin; v < end; ++v) {
        const u32 u = static_cast<u32>(v);
        const Extended a = original(u), b = arithmetic::tagged(arithmetic::bits_of(rng_rne24(u) * 0x1p-31f - 1.0f));
        const bool same = a.significand == b.significand && a.exponent == b.exponent && a.reserved16 == b.reserved16 && (a.reserved16 != Extended::float_tag || a.reserved32 == b.reserved32);
        ++checked;
        if (!same && bad++ < 10) std::printf("MISMATCH u=%08x orig %016llx %04x %04x %08x fast %016llx %04x %04x %08x\n", u, (unsigned long long)a.significand, a.exponent, a.reserved16, a.reserved32, (unsigned long long)b.significand, b.exponent, b.reserved16, b.reserved32);
    }
    std::printf("rng_unit_check [%llx,%llx) checked=%llu mismatches=%llu\n", begin, end, checked, bad);
    return bad ? 1 : 0;
}
