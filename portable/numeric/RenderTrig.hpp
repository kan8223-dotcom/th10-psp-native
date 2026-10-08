#pragma once
// th10_port: fast binary32 sine/cosine for draw-only geometry (sprite and
// billboard rotation). Not bit-exact: within 2 ulp (9.3e-8 absolute) of the
// correctly rounded values for |angle| <= 1024, measured on 2e7 angles
// (Cephes sinf/cosf polynomials and reduction). Nothing that feeds game
// state may use it; the exact sincos_float (DfTrig, ~430 instructions with
// its table) stays the default, and platform builds opt in per call site
// with TH10_RENDER_FAST_TRIG.
#include <cstdint>

namespace touhou::numeric {
// false: |angle| > 1024 or not finite (the caller keeps its exact path).
inline bool render_sincos(float angle,float& sine,float& cosine){
    const float magnitude=angle<0?-angle:angle;
    if(!(magnitude<=1024.0f))return false;
    const float scaled=angle*0.6366197466850281f;
    const int quadrant=static_cast<int>(scaled+(scaled>=0?0.5f:-0.5f));
    const float k=static_cast<float>(quadrant);
    // Cephes' three-part pi/2 (DP1 has 8 significant bits, DP2 12): k*DP1 and
    // k*DP2 are exact for |k| <= 652, so the reduction error is ~k*2^-50.
    const float r=((angle-k*1.5703125f)-k*4.837512969970703125e-4f)-k*7.54978995489188216e-8f;
    const float z=r*r;
    const float s=r+r*z*(-1.6666654611e-1f+z*(8.3321608736e-3f+z*-1.9515295891e-4f));
    const float c=(1.0f-0.5f*z)+z*z*(4.166664568298827e-2f+z*(-1.388731625493765e-3f+z*2.443315711809948e-5f));
    switch(quadrant&3){
    case 0:sine=s;cosine=c;break;
    case 1:sine=c;cosine=-s;break;
    case 2:sine=-s;cosine=-c;break;
    default:sine=-c;cosine=s;break;
    }
    return true;
}
}
