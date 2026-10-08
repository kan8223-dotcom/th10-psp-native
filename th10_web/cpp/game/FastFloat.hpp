#pragma once
#include "Arithmetic.hpp"
// th10_port: plain-float steps for Extended expressions. Each helper performs
// one float operation and reports whether the corresponding Extended fast path
// (Arithmetic.hpp *_fast: precision 32 nearest, float operands, a result with
// biased exponent 2..254 or an exact zero) would accept it; when every step of
// an expression is accepted, the float result equals the Extended one, and the
// caller falls back to its original Extended expression otherwise.
namespace th10::fast_float {
inline bool operand(float v) noexcept { return arithmetic::representable(arithmetic::bits_of(v)); }
inline bool mul(float x, float y, float& r) noexcept {
    r = x * y; const u32 b = arithmetic::bits_of(r);
    return (b & 0x7fffffffu) ? arithmetic::nonzero_accepted(b) : (x == 0 || y == 0);
}
inline bool add(float x, float y, float& r) noexcept {
    r = x + y; const u32 b = arithmetic::bits_of(r);
    return (b & 0x7fffffffu) ? arithmetic::nonzero_accepted(b) : x == -y;
}
inline bool sub(float x, float y, float& r) noexcept {
    r = x - y; const u32 b = arithmetic::bits_of(r);
    return (b & 0x7fffffffu) ? arithmetic::nonzero_accepted(b) : x == y;
}
#if defined(TH10_FAST_PLAYFIELD) && TH10_FAST_PLAYFIELD
// th10_port (TH10_FAST_PLAYFIELD): the playfield test of outside_playfield
// (PlayerShooting.cpp) and laser_outside_playfield (LaserFrame.cpp), whose
// bodies are the same: left = w+x, right = x-w, top = h+y, bottom = y-h, each
// compared with -192 / 192 / 0 / 448 by "<" or "==". When every step is
// accepted (precision 32 nearest, representable operands, add/sub above), the
// four values are the tagged Extended results, and comparisons of tagged
// values are float comparisons (less_fast/equal_fast); the values are finite,
// so "a < c || a == c" is "a <= c". Returns false when a step is declined:
// the caller then runs its original statements.
inline bool outside_playfield(float x, float y, float w, float h, bool& outside) noexcept {
    float left, right, top, bottom;
    if (!(single_precision_nearest() && operand(x) && operand(y) && operand(w) && operand(h) &&
          add(w, x, left) && sub(x, w, right) && add(h, y, top) && sub(y, h, bottom))) return false;
    outside = left <= -192.f || 192.f <= right || top <= 0.f || 448.f <= bottom;
    return true;
}
#endif
#if defined(TH10_FAST_CANCEL) && TH10_FAST_CANCEL
// th10_port (TH10_FAST_CANCEL): cancel_bullet_circle's (BulletCancellation.cpp)
// reach = size*.5+radius, distance = dy*dy+dx*dx and the test
// reach*reach < distance. With every step accepted as above, reach, distance
// and reach*reach are the tagged Extended values and the comparison is the
// float comparison (no NaN on this path). 1: outside the circle (the
// original's "continue"), 0: inside, -1: a step was declined (the caller runs
// the original statement).
inline int circle_outside(float size, float radius, float dx, float dy) noexcept {
    float half, reach, dy2, dx2, distance, limit;
    if (!(single_precision_nearest() && operand(size) && operand(radius) && operand(dx) && operand(dy) &&
          mul(size, .5f, half) && add(half, radius, reach) && mul(dy, dy, dy2) && mul(dx, dx, dx2) &&
          add(dy2, dx2, distance) && mul(reach, reach, limit))) return -1;
    return limit < distance;
}
#endif
}
