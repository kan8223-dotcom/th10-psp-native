#pragma once
// th10_port: bit-exact binary32 results of libm binary64 sin/cos (optionally
// times a binary32 length) from binary32 arithmetic only, adapted from the
// TH08 PSP port (psp/double_float_math.hpp, psp/item_sincos_fastpath_math.hpp).
//
//   sincos_scaled(angle, length, x, y):
//     x = round32(cos((double)angle) * length), y = round32(sin(...) * length)
//
// The products are evaluated in double-float (Dekker/Knuth) arithmetic to
// about 2^-45 relative and a binary32 rounding is accepted only when the
// whole interval of 2^-38 relative around it stays inside one rounding cell.
// That covers the double-float error, libm's binary64 error (< 1 ulp) and
// either canonical product form (x87 single-precision exact-then-round, or a
// binary64 product rounded again), so accepted results equal the canonical
// ones. Everything else returns false and the caller runs the libm path.
//
// Requirements: IEEE binary32 add/sub/mul with round-to-nearest-even, no
// fused multiply-add (-ffp-contract=off), no excess precision (SSE on x86).
#include <cstdint>
#include <cstring>

// TH10_DF_LEAN=1 (TH08 r266): the lean node-table evaluation and the integer
// product/acceptance tail. Same accepted values (host sweep), fewer FPU ops.
#ifndef TH10_DF_LEAN
#define TH10_DF_LEAN 0
#endif

namespace touhou::numeric::df {
struct DoubleFloat {float hi,lo;};
inline std::uint32_t float_bits(float v){std::uint32_t b;std::memcpy(&b,&v,4);return b;}
inline float bits_float(std::uint32_t b){float v;std::memcpy(&v,&b,4);return v;}
inline DoubleFloat two_sum(float a,float b){const float s=a+b,bb=s-a;return {s,(a-(s-bb))+(b-bb)};}
// Requires |a| >= |b| (or a == 0).
inline DoubleFloat quick_two_sum(float a,float b){const float s=a+b;return {s,b-(s-a)};}
inline void split(float a,float& hi,float& lo){const float t=4097.0f*a;hi=t-(t-a);lo=a-hi;}
inline DoubleFloat two_prod(float a,float b){
    const float p=a*b;float ah,al,bh,bl;split(a,ah,al);split(b,bh,bl);
    return {p,((ah*bh-p)+ah*bl+al*bh)+al*bl};
}
inline DoubleFloat neg(DoubleFloat x){return {-x.hi,-x.lo};}
inline DoubleFloat add(DoubleFloat x,DoubleFloat y){
    DoubleFloat s=two_sum(x.hi,y.hi);const DoubleFloat t=two_sum(x.lo,y.lo);
    s.lo+=t.hi;s=quick_two_sum(s.hi,s.lo);s.lo+=t.lo;return quick_two_sum(s.hi,s.lo);
}
inline DoubleFloat add_f(DoubleFloat x,float y){DoubleFloat s=two_sum(x.hi,y);s.lo+=x.lo;return quick_two_sum(s.hi,s.lo);}
inline DoubleFloat mul(DoubleFloat x,DoubleFloat y){DoubleFloat p=two_prod(x.hi,y.hi);p.lo+=x.hi*y.lo+x.lo*y.hi;return quick_two_sum(p.hi,p.lo);}
inline DoubleFloat mul_f(DoubleFloat x,float y){DoubleFloat p=two_prod(x.hi,y);p.lo+=x.lo*y;return quick_two_sum(p.hi,p.lo);}
// a.hi is the binary32 nearest to a.hi + a.lo. Accept it as the rounding of
// any real within bound*|a| of a when that interval stays strictly inside
// a.hi's rounding cell (the smaller half ulp is used at a power of two).
inline bool accept_binary32(DoubleFloat a,float bound,float& out){
    const std::uint32_t hi=float_bits(a.hi),e=(hi>>23)&0xffu;
    if(e<2u||e==0xffu)return false;
    const bool pow2=(hi&0x7fffffu)==0u;
    const float half_ulp=bits_float((e-24u-(pow2?1u:0u))<<23);
    const float margin=half_ulp-bits_float(float_bits(a.lo)&0x7fffffffu),limit=bits_float(hi&0x7fffffffu)*bound;
    if(!(margin>limit))return false;
    out=a.hi;return true;
}
#if TH10_DF_LEAN
// TH08 r266 (psp/trig_df_product_int.hpp ProductAccept): round32((a.hi+a.lo)*m)
// with the 2^-38 acceptance of accept_binary32(mul_f(a,m),2^-38,..), from the
// exact significand product (two 32x32->64 multiplies) and no FPU operation.
// a: normalized, normal finite hi; m: normal finite. The cell size is taken
// from V's leading bit (24/23/22): V < 2^46 happens when both significands
// are 1.0 and lo has the other sign (e.g. cos of a small angle times 1.0), and
// TH08's two-way choice would measure that case against a cell twice as wide.
inline bool accept_product_int(DoubleFloat a,float m,float& out){
    const std::uint32_t hb=float_bits(a.hi),lb=float_bits(a.lo),mb=float_bits(m);
    const std::uint32_t eh=(hb>>23)&0xffu,el=(lb>>23)&0xffu,em=(mb>>23)&0xffu;
    const std::uint32_t mm=(mb&0x7fffffu)|0x800000u;
    std::uint64_t v=static_cast<std::uint64_t>((hb&0x7fffffu)|0x800000u)*mm;   // [2^46, 2^48)
    if(el!=0u){                                                                 // lo == 0 or denormal: ignored
        const std::uint64_t q=static_cast<std::uint64_t>((lb&0x7fffffu)|0x800000u)*mm;
        const std::uint32_t d=eh-el;                                            // >= 24 when normalized
        const std::uint64_t qs=d>=64u?0u:(q>>d);                                // truncation < 1 unit
        if((hb^lb)&0x80000000u)v-=qs;else v+=qs;
    }
    const unsigned t=v>=(std::uint64_t(1)<<47)?24u:(v>=(std::uint64_t(1)<<46)?23u:22u);
    std::uint32_t mant=static_cast<std::uint32_t>(v>>t);                      // [2^23, 2^24)
    const std::uint32_t rem=static_cast<std::uint32_t>(v)&((1u<<t)-1u),half=1u<<(t-1u);
    const std::uint32_t eps=static_cast<std::uint32_t>(v>>38)+2u;              // v 2^-38 + truncation
    const std::uint32_t dist=rem>half?rem-half:half-rem;
    if(dist<=eps)return false;
    std::int32_t er=static_cast<std::int32_t>(eh+em+t)-150;
    if(rem>half&&++mant==(1u<<24)){mant=1u<<23;++er;}
    if(er<2||er>254)return false;
    out=bits_float(((hb^mb)&0x80000000u)|(static_cast<std::uint32_t>(er)<<23)|(mant&0x7fffffu));return true;
}
#endif

namespace detail {
constexpr float half_pi_hi=1.5707963705062866f;       // 0x3fc90fdb
constexpr float half_pi_mid=-4.371138828673793e-08f;  // 0xb33bbd2e
constexpr float half_pi_lo=-1.7151245100058819e-15f;  // 0xa6f72ced
constexpr float two_over_pi=0.6366197466850281f;
constexpr float max_angle=1024.0f;                    // |k| <= 652; k*hi, k*mid taken exactly by two_prod
constexpr float relative_bound=3.637978807091713e-12f;// 2^-38
constexpr std::uint32_t min_length_exponent=67u,max_length_exponent=187u; // 2^-60 .. 2^60
// 1/n! as double-float {hi, lo}.
constexpr DoubleFloat f2{0.5f,0.0f},f3{0.1666666716337204f,-4.967053879312289e-09f},f4{0.0416666679084301f,-1.2417634698280722e-09f},
    f5{0.008333333767950535f,-4.34617203337595e-10f},f6{0.0013888889225199819f,-3.3631094437103215e-11f},f7{0.00019841270113829523f,-2.725596874933456e-12f},
    f8{2.4801587642286904e-05f,-3.40699609366682e-13f},f9{2.7557318844628753e-06f,3.793571224297229e-14f},f10{2.755731998149713e-07f,-7.575112209051195e-15f},
    f11{2.5052107943679403e-08f,4.4176230446483665e-16f},f12{2.0876755879584152e-09f,1.1082839147459852e-16f},f13{1.6059044372074283e-10f,-5.352526511562726e-18f},
    f14{1.147074536050896e-11f,2.372207689231238e-19f},f15{7.647163609812713e-13f,1.2200710471178288e-20f},f16{4.7794772561329454e-14f,7.62544404448643e-22f};
// sin(r), cos(r) for |r| <= pi/4; series truncation below 2^-54 relative.
inline void sincos_reduced(DoubleFloat r,DoubleFloat& sine,DoubleFloat& cosine){
    const DoubleFloat w=mul(r,r);
    DoubleFloat s=neg(f15);
    s=add(mul(s,w),f13);s=add(mul(s,w),neg(f11));s=add(mul(s,w),f9);s=add(mul(s,w),neg(f7));
    s=add(mul(s,w),f5);s=add(mul(s,w),neg(f3));s=add(mul(s,w),{1.0f,0.0f});sine=mul(r,s);
    DoubleFloat c=f16;
    c=add(mul(c,w),neg(f14));c=add(mul(c,w),f12);c=add(mul(c,w),neg(f10));c=add(mul(c,w),f8);
    c=add(mul(c,w),neg(f6));c=add(mul(c,w),f4);c=add(mul(c,w),neg(f2));cosine=add(mul(c,w),{1.0f,0.0f});
}
}

}
#ifndef TH10_DF_TRIG_TABLE
#define TH10_DF_TRIG_TABLE 1
#endif
#if TH10_DF_TRIG_TABLE
#include "DfTrigTable.hpp"
namespace touhou::numeric::df {
namespace detail {
// sin(r), cos(r) for |r| <= 0.86 as r = a + h with a = k/256 the nearest node
// (|h| <= 2^-9 + 2^-24): sin(a+h) = sin a cos h + cos a sin h. Series to h^5
// (sin) and h^4 (cos); about 2^-44 relative, inside the 2^-38 acceptance
// bound, so accepted results are unchanged -- only cheaper (TH08 r073L:
// 432 instead of 756 instructions).
inline bool sincos_reduced_table(DoubleFloat r,DoubleFloat& sine,DoubleFloat& cosine){
    const float kf=r.hi*256.0f;const int k=static_cast<int>(kf+(kf>=0.0f?0.5f:-0.5f)),index=k<0?-k:k;
    if(index>node_max)return false;
    const float a=static_cast<float>(k)*0.00390625f;             // exact
    const DoubleFloat h=add_f(r,-a),w=mul(h,h);
    const float w2=w.hi*w.hi;                                     // w^2 terms are below 2^-40 of the result
    DoubleFloat sp=neg(mul(w,f3));sp.lo+=w2*f5.hi;sp=add_f(sp,1.0f);  // sin h = h (1 - w/6 + w^2/120)
    const DoubleFloat sh=mul(h,sp);
    DoubleFloat cm{-0.5f*w.hi,-0.5f*w.lo};cm.lo+=w2*f4.hi;       // cos h - 1 = -w/2 + w^2/24
    const DoubleFloat ch=add_f(cm,1.0f);
    const Node& node=nodes[index];
    DoubleFloat sa{node.sin_hi,node.sin_lo};if(k<0)sa=neg(sa);const DoubleFloat ca{node.cos_hi,node.cos_lo};
    sine=add(mul(sa,ch),mul(ca,sh));
    DoubleFloat c=add(ca,neg(mul(sa,sh)));c=quick_two_sum(c.hi,c.lo+ca.hi*(cm.hi+cm.lo));cosine=c;
    return true;
}
#if TH10_DF_LEAN
// TH08 r266 (item_sincos_fastpath_math.hpp DfSinCosReducedTableLean): h^2 by
// one two_prod plus the 2 h.hi h.lo cross term (h.lo^2 dropped), and
// sin h = h - h g with g = w/6 - w^2/120 in binary32 (|g| <= 2^-20.6, so its
// rounding is below 2^-43 of sin h). The rest is sincos_reduced_table.
inline bool sincos_reduced_table_lean(DoubleFloat r,DoubleFloat& sine,DoubleFloat& cosine){
    const float kf=r.hi*256.0f;const int k=static_cast<int>(kf+(kf>=0.0f?0.5f:-0.5f)),index=k<0?-k:k;
    if(index>node_max)return false;
    const float a=static_cast<float>(k)*0.00390625f;             // exact
    const DoubleFloat h=add_f(r,-a);
    DoubleFloat w=two_prod(h.hi,h.hi);w.lo+=2.0f*h.hi*h.lo;
    const float w2=w.hi*w.hi;
    const float g=w.hi*f3.hi-w2*f5.hi;
    const DoubleFloat sh=quick_two_sum(h.hi,h.lo-h.hi*g);
    DoubleFloat cm{-0.5f*w.hi,-0.5f*w.lo};cm.lo+=w2*f4.hi;
    const DoubleFloat ch=add_f(cm,1.0f);
    const Node& node=nodes[index];
    DoubleFloat sa{node.sin_hi,node.sin_lo};if(k<0)sa=neg(sa);const DoubleFloat ca{node.cos_hi,node.cos_lo};
    sine=add(mul(sa,ch),mul(ca,sh));
    DoubleFloat c=add(ca,neg(mul(sa,sh)));c=quick_two_sum(c.hi,c.lo+ca.hi*(cm.hi+cm.lo));cosine=c;
    return true;
}
#endif
}
#endif
#if TH10_DF_LEAN&&!TH10_DF_TRIG_TABLE
#error "TH10_DF_LEAN needs TH10_DF_TRIG_TABLE"
#endif
#if !TH10_DF_TRIG_TABLE
namespace touhou::numeric::df {
#endif
inline bool sincos_scaled(float angle,float length,float& x,float& y){
    using namespace detail;
    const std::uint32_t ab=float_bits(angle),lb=float_bits(length),ae=(ab>>23)&0xffu,le=(lb>>23)&0xffu;
    if(ae==0xffu||le==0xffu)return false;
    // cos(+-0) = 1 and sin(+-0) = +-0 exactly: the products are exact.
    if(!(ab&0x7fffffffu)){x=length;y=angle*length;return true;}
    if(!(lb&0x7fffffffu)&&(ae==0u))return false;   // denormal angle with zero length
    if((lb&0x7fffffffu)&&(le<min_length_exponent||le>max_length_exponent))return false;
    if(bits_float(ab&0x7fffffffu)>max_angle)return false;
    // k = nearest quarter turn, r = angle - k*pi/2 with a three-part pi/2.
    // Products k*hi and k*mid are exact double-floats (two_prod), so the only
    // reduction error is the three-part pi/2 residual (about |k| 2^-78) plus
    // double-float rounding. For |k| <= 2 this is the TH08 computation.
    const float kf=angle*two_over_pi;const int k=static_cast<int>(kf+(kf>=0.0f?0.5f:-0.5f));
    const float kk=static_cast<float>(k);
    const DoubleFloat p1=two_prod(kk,half_pi_hi),p2=two_prod(kk,half_pi_mid);
    DoubleFloat r=two_sum(angle,-p1.hi);r=add_f(r,-p1.lo);r=add_f(r,-p2.hi);r=add_f(r,-p2.lo);r=add_f(r,-(kk*half_pi_lo));
    // The reduction error is about |k| 2^-78 absolute (pi/2 residual), so a
    // reduced argument down to (|k|+1) 2^-34 keeps it below 2^-44 relative;
    // this admits the float angles next to multiples of pi/2 (e.g. 0.5f*pi).
    const float ar=bits_float(float_bits(r.hi)&0x7fffffffu);
    if(ar<static_cast<float>((k<0?-k:k)+1)*5.820766091346741e-11f)return false; // 2^-34
    DoubleFloat s,c;
#if TH10_DF_LEAN
    if(!sincos_reduced_table_lean(r,s,c))sincos_reduced(r,s,c);
#elif TH10_DF_TRIG_TABLE
    if(!sincos_reduced_table(r,s,c))sincos_reduced(r,s,c);
#else
    sincos_reduced(r,s,c);
#endif
    DoubleFloat sine,cosine;
    switch(k&3){case 0:sine=s;cosine=c;break;case 1:sine=c;cosine=neg(s);break;case 2:sine=neg(s);cosine=neg(c);break;default:sine=neg(c);cosine=s;break;}
    if(!(lb&0x7fffffffu)){x=cosine.hi*length;y=sine.hi*length;return true;} // +-0 with the sign of cos/sin
    float fx,fy;
#if TH10_DF_LEAN
    if(!accept_product_int(cosine,length,fx)||!accept_product_int(sine,length,fy))return false;
#else
    if(!accept_binary32(mul_f(cosine,length),relative_bound,fx)||!accept_binary32(mul_f(sine,length),relative_bound,fy))return false;
#endif
    x=fx;y=fy;return true;
}
#if defined(TH10_TRIG_MEMO) && TH10_TRIG_MEMO
// th10_port (TH08 psp/trig_df_memo.hpp sha d27cce0d, r259): sincos_scaled in
// two halves so a caller can keep the angle half for an object whose angle
// stays the same (a bullet accelerating along one direction, a player shot):
// sincos_angle does everything that depends on the angle only, sincos_length
// the length checks and the product tail. For every (angle, length) the pair
// returns what sincos_scaled returns (tools: test_trig_memo sweep).
struct SinCosAngle {DoubleFloat sine,cosine;std::uint8_t state,denormal;};   // state 0 declined, 1 ready, 2 angle +-0
inline SinCosAngle sincos_angle(float angle){
    using namespace detail;SinCosAngle out{{0,0},{0,0},0,0};
    const std::uint32_t ab=float_bits(angle),ae=(ab>>23)&0xffu;
    if(ae==0xffu)return out;
    if(!(ab&0x7fffffffu)){out.state=2;return out;}
    if(bits_float(ab&0x7fffffffu)>max_angle)return out;
    const float kf=angle*two_over_pi;const int k=static_cast<int>(kf+(kf>=0.0f?0.5f:-0.5f));
    const float kk=static_cast<float>(k);
    const DoubleFloat p1=two_prod(kk,half_pi_hi),p2=two_prod(kk,half_pi_mid);
    DoubleFloat r=two_sum(angle,-p1.hi);r=add_f(r,-p1.lo);r=add_f(r,-p2.hi);r=add_f(r,-p2.lo);r=add_f(r,-(kk*half_pi_lo));
    const float ar=bits_float(float_bits(r.hi)&0x7fffffffu);
    if(ar<static_cast<float>((k<0?-k:k)+1)*5.820766091346741e-11f)return out;
    DoubleFloat s,c;
#if TH10_DF_LEAN
    if(!sincos_reduced_table_lean(r,s,c))sincos_reduced(r,s,c);
#elif TH10_DF_TRIG_TABLE
    if(!sincos_reduced_table(r,s,c))sincos_reduced(r,s,c);
#else
    sincos_reduced(r,s,c);
#endif
    switch(k&3){case 0:out.sine=s;out.cosine=c;break;case 1:out.sine=c;out.cosine=neg(s);break;case 2:out.sine=neg(s);out.cosine=neg(c);break;default:out.sine=neg(c);out.cosine=s;break;}
    out.state=1;out.denormal=ae==0u;return out;
}
inline bool sincos_length(const SinCosAngle& a,float angle,float length,float& x,float& y){
    using namespace detail;
    const std::uint32_t lb=float_bits(length),le=(lb>>23)&0xffu;
    if(le==0xffu)return false;
    if(a.state==2){x=length;y=angle*length;return true;}
    if(a.state!=1)return false;
    if(!(lb&0x7fffffffu)&&a.denormal)return false;
    if((lb&0x7fffffffu)&&(le<min_length_exponent||le>max_length_exponent))return false;
    if(!(lb&0x7fffffffu)){x=a.cosine.hi*length;y=a.sine.hi*length;return true;}
    float fx,fy;
#if TH10_DF_LEAN
    if(!accept_product_int(a.cosine,length,fx)||!accept_product_int(a.sine,length,fy))return false;
#else
    if(!accept_binary32(mul_f(a.cosine,length),relative_bound,fx)||!accept_binary32(mul_f(a.sine,length),relative_bound,fy))return false;
#endif
    x=fx;y=fy;return true;
}
#endif
}
