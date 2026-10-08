#pragma once
#include "Arithmetic.hpp"
namespace th10 {
struct Timer {
    i32 previous;
    i32 current;
    float fractional;
    const float* rate;
    void reset() noexcept;
    void initialize(i32 previous_value) noexcept;
    i32 tick() noexcept;
    void advance(float frames) noexcept;
#if TH10_TIMER_INLINE
    // th10_port: tick() and advance() at the per-bullet/per-VM hot sites
    // (TH10_TIMER_TICK/TH10_TIMER_ADVANCE below) without the out-of-line call
    // for the common unscaled rate (0.99 < rate < 1.01). Each decides with
    // nothing written, else the out-of-line function runs from the same
    // state: tick() takes Scalar::add(fractional, 1)'s fast path, which
    // accepts exactly the normal or zero fractional values (their sum with 1
    // is finite, -1 + 1 an exact cancellation, any other at least 2^-24 in
    // magnitude), advance() TH10_FAST_TIMER's float path (Timer.cpp). The
    // scaled tick's float path is in Timer::tick. tools/timer_inline_check.cpp
    // (all 2^32 fractional values at rate 1).
    static bool unscaled_inline(float rate) noexcept { return 0.99f < rate && rate < 1.01f; }
    __attribute__((always_inline)) i32 tick_inline() noexcept {
        const u32 magnitude = arithmetic::bits_of(fractional) & 0x7fffffffu;
        if (unscaled_inline(*rate) && single_precision_nearest() && (magnitude == 0 || magnitude - 0x00800000u < 0x7f000000u)) {
            previous = current; current = wrapping_add(current, 1); fractional = fractional + 1.0f; return current;
        }
        return tick();
    }
    __attribute__((always_inline)) void advance_inline(float frames) noexcept {
#if TH10_FAST_TIMER
        const float held = fractional;
        if (unscaled_inline(*rate) && single_precision_nearest() && arithmetic::representable(arithmetic::bits_of(frames)) && arithmetic::representable(arithmetic::bits_of(held))) {
            const float sum = frames + held; const u32 sum_bits = arithmetic::bits_of(sum);
            if ((sum_bits & 0x7fffffffu) ? arithmetic::nonzero_accepted(sum_bits) : frames == -held) {
                previous = current; fractional = sum; current = Scalar::truncate(sum); return;
            }
        }
#endif
        advance(frames);
    }
#endif
};
static_assert(offsetof(Timer, fractional) == 8 && offsetof(Timer, rate) == 12);
}
#if TH10_TIMER_INLINE
#define TH10_TIMER_TICK(timer) (timer).tick_inline()
#define TH10_TIMER_ADVANCE(timer, frames) (timer).advance_inline(frames)
#else
#define TH10_TIMER_TICK(timer) (timer).tick()
#define TH10_TIMER_ADVANCE(timer, frames) (timer).advance(frames)
#endif
