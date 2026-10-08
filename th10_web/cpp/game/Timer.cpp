#include "Timer.hpp"
namespace th10 {
// 0x401f40 / 0x401f90: rate is retained, including custom rates.
void Timer::reset() noexcept { initialize(-999999); }
void Timer::initialize(i32 previous_value) noexcept {
    previous = previous_value;
    current = 0;
    fractional = 0;
}
static bool unscaled(float rate) noexcept { return 0.99f < rate && rate < 1.01f; }
// 0x404ed0 / 0x44bfa0. The scaled branch converts the extended sum
// to an integer BEFORE the rounded float stored in fractional is reloaded.
i32 Timer::tick() noexcept {
    previous = current;
    if (unscaled(*rate)) {
        current = wrapping_add(current, 1);
        fractional = Scalar::add(fractional,1.0f);
    } else {
#if TH10_TIMER_INLINE
        // th10_port: the scaled sum in plain floats where Extended's add_fast
        // accepts it (Arithmetic.hpp: precision 32 nearest, normal or zero
        // operands, a result with biased exponent 2..254 or an exact zero
        // cancellation) and truncate_fast's |sum| < 2^31 holds: the same
        // fractional and current; otherwise the original operations.
        if (single_precision_nearest()) {
            using namespace arithmetic;
            const float scale = *rate, held = fractional;
            if (representable(bits_of(scale)) && representable(bits_of(held))) {
                const float sum = scale + held; const u32 sum_bits = bits_of(sum);
                if (((sum_bits & 0x7fffffffu) ? nonzero_accepted(sum_bits) : scale == -held) && sum > -2147483648.0f && sum < 2147483648.0f) {
                    fractional = sum; current = static_cast<i32>(sum); return current;
                }
            }
        }
#endif
        const auto sum = number(*rate) + number(fractional);
        fractional = sum.to_float();
        current = sum.truncate_int();
    }
    return current;
}
// 0x44bf40: unlike tick(), this path reloads the rounded float first.
void Timer::advance(float frames) noexcept {
    previous = current;
#if TH10_FAST_TIMER
    // th10_port: the same operations in plain floats. Each is accepted by the
    // same test as its Extended fast path (Arithmetic.hpp: precision 32
    // nearest, float operands, a result with biased exponent 2..254 or an
    // exact zero); if any test fails, the original operations below run.
    if (single_precision_nearest()) {
        using namespace arithmetic;
        const float held = fractional, scale = *rate;
        if (representable(bits_of(frames)) && representable(bits_of(held))) {
            float delta = frames; bool accepted = true;
            if (!unscaled(scale)) {
                const float product = frames * scale; const u32 product_bits = bits_of(product);
                accepted = representable(bits_of(scale)) && ((product_bits & 0x7fffffffu) ? nonzero_accepted(product_bits) : (frames == 0 || scale == 0));
                delta = product;
            }
            const float sum = delta + held; const u32 sum_bits = bits_of(sum);
            if (accepted && ((sum_bits & 0x7fffffffu) ? nonzero_accepted(sum_bits) : delta == -held)) {
                fractional = sum;
                current = Scalar::truncate(fractional);
                return;
            }
        }
    }
#endif
    auto delta = number(frames);
    if (!unscaled(*rate)) delta = delta * number(*rate);
    fractional = (delta + number(fractional)).to_float();
    current = Scalar::truncate(fractional);
}
}
