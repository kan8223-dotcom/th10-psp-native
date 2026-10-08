// Checks TH10_FAST_RING (game/AnmFile.cpp AnmVm::update_ring_geometry): the
// fast loop must write the same vertex bytes as the original loop, on random
// rings (segments, texture repeat, angle, scale, position, uv offsets, colour)
// including zeros, negative and huge values, angles near multiples of pi/2,
// subnormals, infinities and NaNs, in every precision/rounding mode the game uses.
//   cmake --build <dir> --target th10_ring_check && <dir>/th10_ring_check [thousands]
#include "../../th10_web/cpp/game/GameMath.hpp"
#include "../../th10_web/cpp/game/FastFloat.hpp"
#include "../../portable/numeric/DfTrig.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
#if defined(TH10_STATIC_I386_FMOD)
// glibc 2.39's static i386 libm exports only __ieee754_fmod (main.cpp does the same).
extern "C" double __ieee754_fmod(double,double);
extern "C" double fmod(double x,double y){return __ieee754_fmod(x,y);}
#endif
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
struct Vertex {Vec3 position;float reciprocal_w;u32 color;Vec2 uv;};
struct Ring {i32 variables[2];float rotation_z;Vec2 scale;Vec3 position,script_position;Vec2 uv_offset;float u0,u1;u32 color;};
static unsigned long long fast_vertices=0;
// ---- original (verbatim from the #else branch, members passed in) ----
static u32 ring_original(const Ring& r,Vertex* vertices){
    const i32 segments=wrapping_add(r.variables[0],-1);
    const auto denominator=Extended::from_int(segments);
    const float angular_step=(number(6.283185482025146484375f)/denominator).to_float();
    const float texture_step=(Extended::from_int(r.variables[1])/denominator).to_float();
    float angle=r.rotation_z,texture_v=0;
    auto* output=vertices;
    for(i32 segment=0;segment<segments;++segment){
        for(unsigned side=0;side<2;++side){
            auto& vertex=*output++;vertex.reciprocal_w=1;vertex.color=r.color;
            vertex.uv={Scalar::add(side?r.u1:r.u0,r.uv_offset.x),Scalar::add(texture_v,r.uv_offset.y)};
            const auto half_width=number(r.scale.x)*number(0.5f);
            const float radius=(side?number(r.scale.y)-half_width:half_width+number(r.scale.y)).to_float();
            const auto point=polar(angle,radius);
            const auto y=number(Scalar::add(r.position.y,r.script_position.y));
            vertex.position={
                (number(r.position.x)+number(r.script_position.x)+number(point.x)).to_float(),
                (y+number(point.y)).to_float(),
                Scalar::add(r.position.z,r.script_position.z)};
        }
        texture_v=Scalar::add(texture_step,texture_v);
        angle=add_angle(angle,angular_step).to_float();
    }
    return u32(output-vertices);
}
// ---- TH10_FAST_RING (verbatim apart from the members and counting) ----
static u32 ring_fast(const Ring& r,Vertex* vertices){
    const i32 segments=wrapping_add(r.variables[0],-1);
    const auto denominator=Extended::from_int(segments);
    const float angular_step=(number(6.283185482025146484375f)/denominator).to_float();
    const float texture_step=(Extended::from_int(r.variables[1])/denominator).to_float();
    float angle=r.rotation_z,texture_v=0;
    auto* output=vertices;
    const auto half_width=number(r.scale.x)*number(0.5f);
    const float radii[2]{(half_width+number(r.scale.y)).to_float(),(number(r.scale.y)-half_width).to_float()};
    const float u[2]{Scalar::add(r.u0,r.uv_offset.x),Scalar::add(r.u1,r.uv_offset.x)};
    const auto x=number(r.position.x)+number(r.script_position.x);
    const auto y=number(Scalar::add(r.position.y,r.script_position.y));
    const float z=Scalar::add(r.position.z,r.script_position.z);
    const bool float_xy=single_precision_nearest()&&x.tagged()&&y.tagged();const float xf=x.to_float(),yf=y.to_float();
    for(i32 segment=0;segment<segments;++segment){
        const touhou::numeric::df::SinCosAngle a=touhou::numeric::df::sincos_angle(angle);
        const float v=Scalar::add(texture_v,r.uv_offset.y);
        for(unsigned side=0;side<2;++side){
            auto& vertex=*output++;vertex.reciprocal_w=1;vertex.color=r.color;vertex.uv={u[side],v};
            Vec2 point;
            if(!touhou::numeric::df::sincos_length(a,angle,radii[side],point.x,point.y))
                point={(cosine(number(angle))*number(radii[side])).to_float(),(sine(number(angle))*number(radii[side])).to_float()};
            float px,py;
            if(float_xy&&fast_float::operand(point.x)&&fast_float::operand(point.y)&&fast_float::add(xf,point.x,px)&&fast_float::add(yf,point.y,py)){vertex.position={px,py,z};++fast_vertices;}
            else vertex.position={(x+number(point.x)).to_float(),(y+number(point.y)).to_float(),z};
        }
        texture_v=Scalar::add(texture_step,texture_v);
        angle=add_angle(angle,angular_step).to_float();
    }
    return u32(output-vertices);
}
static u64 state=0x6a09e667f3bcc909ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x80000000u,0x00000001u,0x007fffffu,0x00800000u,0x3f800000u,0x3fc90fdbu,0x40490fdbu,0x40c90fdbu,
    0xbfc90fdbu,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc00000u,0x49800000u,0x35800000u};
static float value(int kind){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(kind?2+r%4:r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%2400)-1200)/4.0f));                   // playfield, quarter units
    case 3:return fbits(bits(float((r>>8)%4096)/64.0f)|sign);                         // 0..64: ring widths, radii
    case 4:return fbits(bits(float(i32((r>>8)%1000)-500)*0.0062831855f));            // angles around +-pi
    default:return fbits(((u32(110+(r>>8)%30)<<23)|u32((r>>16)&0x7fffffu))|sign);   // 2^-17..2^12
    }
}
int main(int argc,char** argv){
    const u64 thousands=argc>1?std::strtoull(argv[1],nullptr,0):200;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    static Vertex a[1100],b[1100];unsigned long long rings=0,vertices=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        const u64 n=(m.p==Precision::Single&&m.r==Rounding::NearestEven?thousands:thousands/10+1)*1000ull;
        for(u64 k=0;k<n;++k){const int in=k&3?1:0;
            Ring r{};const u64 s=next();
            r.variables[0]=(s&15)==0?i32(s>>8)%3:2+i32((s>>8)%(k&7?64:512));r.variables[1]=i32((s>>20)%9)-1;
            r.rotation_z=value(in);if((s>>40)%4==0)r.rotation_z=fbits(((s>>44)&1?0x40490fdbu:0x3fc90fdbu)+u32(i32((s>>46)%5)-2));   // a few ulps around pi/2, pi
            r.scale={value(in),value(in)};r.position={value(in),value(in),value(in)};r.script_position={value(in),value(in),value(in)};
            r.uv_offset={value(in),value(in)};r.u0=value(in);r.u1=value(in);r.color=u32(next());
            std::memset(a,0xa5,sizeof a);std::memset(b,0xa5,sizeof b);
            const u32 na=ring_original(r,a),nb=ring_fast(r,b);++rings;vertices+=na;
            if(na!=nb||std::memcmp(a,b,na*sizeof(Vertex))){
                bool nan_only=na==nb;for(u32 i=0;i<na&&nan_only;i++)for(u32 w=0;w<7;w++){u32 x,y;std::memcpy(&x,reinterpret_cast<const char*>(a+i)+4*w,4);std::memcpy(&y,reinterpret_cast<const char*>(b+i)+4*w,4);
                    if(x!=y&&!(std::isnan(fbits(x))&&std::isnan(fbits(y))))nan_only=false;}
                if(!nan_only){if(mismatches<8)std::printf("MISMATCH mode=%d/%d segments=%d angle=%08x scale=%08x,%08x pos=%08x\n",int(m.p),int(m.r),r.variables[0],bits(r.rotation_z),bits(r.scale.x),bits(r.scale.y),bits(r.position.x));++mismatches;}
            }
        }}
    std::printf("ring_check: %llu rings, %llu vertices (%llu on the float path), %llu mismatches\n",rings,vertices,fast_vertices,mismatches);
    return mismatches?1:0;
}
