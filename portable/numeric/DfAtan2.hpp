#pragma once
// th10_port: bit-exact binary32 result of libm binary64 atan2 on binary32
// inputs, from binary32 arithmetic only, adapted from the TH08 PSP port
// (psp/item_atan2_fastpath_math.hpp, psp/double_float_math.hpp).
//
//   atan2_float(y, x, out): out = round32(atan2((double)y, (double)x))
//
// atan2 is evaluated in double-float (Dekker/Knuth) arithmetic to about 2^-44
// relative (table of atan(k/16) plus a series in the reduced argument), and a
// binary32 rounding is accepted only when the whole interval of 2^-38
// relative around it stays inside one rounding cell. That covers the
// double-float error and libm's binary64 error (< 1 ulp), so accepted results
// equal the canonical ones. Zero, non-finite, extreme or very unequal inputs
// and rounding-boundary cases return false and the caller runs libm.
//
// Requirements: as DfTrig.hpp (binary32 round-to-nearest-even, no FMA, no
// excess precision). Intermediates stay above 2^-90, so flush-to-zero of
// denormals (Allegrex) cannot matter.
#include "DfTrig.hpp"

namespace touhou::numeric::df {
// Two-term long division: relative error about 2^-46.
inline DoubleFloat div(DoubleFloat x,DoubleFloat y){
    const float q1=x.hi/y.hi;DoubleFloat r=add(x,neg(mul_f(y,q1)));
    const float q2=r.hi/y.hi;r=add(r,neg(mul_f(y,q2)));
    const float q3=r.hi/y.hi;return add(quick_two_sum(q1,q2),{q3,0.0f});
}
namespace atan2_detail {
// atan(k/16), k = 0..16, as {fl32(v), fl32(v - fl32(v))}.
constexpr DoubleFloat table[17]={
    {0.0f,0.0f},{0.06241881102323532f,-1.0272779293885037e-09f},{0.12435499578714371f,-1.240382241363136e-09f},
    {0.18534794449806213f,5.49763257140512e-09f},{0.244978666305542f,-3.1786777654474463e-09f},{0.30288487672805786f,-8.353086222712136e-09f},
    {0.3587706685066223f,1.7639498750554594e-09f},{0.4124104380607605f,3.5366267692182873e-09f},{0.46364760398864746f,5.01215868808913e-09f},
    {0.5123894810676575f,-2.0756919738573743e-08f},{0.5585992932319641f,2.2111597886009804e-08f},{0.6022873520851135f,-5.950149262190507e-09f},
    {0.6435011029243469f,5.868937336117597e-09f},{0.6823165416717529f,1.3202995141625706e-08f},{0.7188299894332886f,1.0188335508587443e-08f},
    {0.7531512975692749f,-1.660708015549517e-08f},{0.7853981852531433f,-2.1855694143368964e-08f}};
constexpr DoubleFloat half_pi{1.5707963705062866f,-4.371138828673793e-08f},pi{3.1415927410125732f,-8.742277657347586e-08f};
constexpr DoubleFloat inv3{0.3333333432674408f,-9.934107758624577e-09f},inv5{0.20000000298023224f,-2.9802322831784522e-09f},
    inv7{0.1428571492433548f,-6.38621200366174e-09f},inv9{0.1111111119389534f,-8.278422947149977e-10f},inv11{0.09090909361839294f,-2.709302115988521e-09f};
constexpr float relative_bound=3.637978807091713e-12f;   // 2^-38
constexpr float tiny_ratio=9.5367431640625e-07f;          // 2^-20
constexpr std::uint32_t min_exponent=67u,max_exponent=187u; // 2^-60 .. 2^60
}
inline bool atan2_float(float y,float x,float& out){
    using namespace atan2_detail;
    const std::uint32_t xb=float_bits(x),yb=float_bits(y),xe=(xb>>23)&0xffu,ye=(yb>>23)&0xffu;
    if(xe==0xffu||ye==0xffu||!(xb&0x7fffffffu)||!(yb&0x7fffffffu))return false;
    if(xe<min_exponent||xe>max_exponent||ye<min_exponent||ye>max_exponent)return false;
    const float ax=bits_float(xb&0x7fffffffu),ay=bits_float(yb&0x7fffffffu);
    const bool swapped=ay>ax;const float mn=swapped?ax:ay,mx=swapped?ay:ax;
    if(mn<mx*tiny_ratio)return false;
    // t = mn/mx in [2^-20, 1]; u = (t - c)/(1 + t c) with c = k/16, |u| <= 1/32.
    const DoubleFloat t=div({mn,0.0f},{mx,0.0f});
    int k=static_cast<int>(t.hi*16.0f+0.5f);if(k<0)k=0;if(k>16)k=16;
    const float c=static_cast<float>(k)*0.0625f;
    const DoubleFloat u=div(add(t,{-c,0.0f}),add({1.0f,0.0f},mul_f(t,c))),w=mul(u,u);
    // atan(u) = u (1 - w/3 + w^2/5 - w^3/7 + w^4/9 - w^5/11); the w^6/13 term is below 2^-65 |u|.
    DoubleFloat p=neg(inv11);
    p=add(mul(p,w),inv9);p=add(mul(p,w),neg(inv7));p=add(mul(p,w),inv5);p=add(mul(p,w),neg(inv3));p=add(mul(p,w),{1.0f,0.0f});
    DoubleFloat a=add(table[k],mul(u,p));
    if(swapped)a=add(half_pi,neg(a));
    if(xb&0x80000000u)a=add(pi,neg(a));
    if(yb&0x80000000u)a=neg(a);
    return accept_binary32(a,relative_bound,out);
}
}
