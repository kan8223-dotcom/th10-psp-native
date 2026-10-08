// Checks the TH10_FAST_BILLBOARD stages (game/AnmProjection.cpp
// billboard_geometry): with precision 32 nearest and inputs inside each
// stage's range, the SpriteNumber (plain float) instantiation must give the
// same floats as the Extended one. Inputs are drawn log-uniformly over the
// whole allowed range, with both signs, zeros and the exact range edges.
//   cmake --build <dir> --target th10_billboard_check && <dir>/th10_billboard_check [millions]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include "../../portable/numeric/SpriteNumber.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
using touhou::numeric::SpriteNumber;
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
// Verbatim from AnmProjection.cpp (TH10_FAST_BILLBOARD helpers).
inline SpriteNumber root(SpriteNumber v){return SpriteNumber(std::sqrt(v.value));}
inline Extended root(const Extended& v){return v.square_root();}
template<class Number,class N> void anchor_as(Number number,u32 mode,N size,N& first,N& last){switch(mode){case 0:first=number((size*number(-.5f)).to_float());last=size*number(.5f);break;case 1:first=number(0);last=size;break;case 2:first=number((-size).to_float());last=number(0);break;default:__builtin_trap();}}
struct V3 {float x,y,z;};struct V2 {float x,y;};
struct Vm {V3 child_position,position,script_position;V2 sprite_size,scale;u32 flags;};
// The three stage lambdas of billboard_geometry, verbatim apart from taking
// their inputs as arguments.
template<class Number> void translate(Number number,const Vm& vm,float out[3]){
    const auto sum=[&](float a,float b,float c){return number(a)+number(b)+number(c);};
    out[0]=sum(vm.child_position.x,vm.position.x,vm.script_position.x).to_float();out[1]=sum(vm.child_position.y,vm.position.y,vm.script_position.y).to_float();out[2]=sum(vm.child_position.z,vm.position.z,vm.script_position.z).to_float();}
template<class Number> void scale(Number number,const Vm& vm,float dx,float dy,float dz,float& width_value,float& height_value){
    const auto ratio=root(number(dz)*number(dz)+number(dy)*number(dy)+number(dx)*number(dx))*number(.5f);
    width_value=(number(vm.sprite_size.x)*number(vm.scale.x)*ratio).to_float();height_value=(number(vm.sprite_size.y)*number(vm.scale.y)*ratio).to_float();}
template<class Number> void corners(Number number,const Vm& vm,float width_value,float height_value,float cosine_value,float sine_value,V2 center,float q[8]){
    using N=decltype(number(0.f));
    const auto width=number(width_value),height=number(height_value);
    const auto c=number(cosine_value),s=number(sine_value),x=number(center.x),y=number(center.y);
    N left,right,top,bottom;anchor_as(number,(vm.flags>>18)&3,width,left,right);anchor_as(number,(vm.flags>>20)&3,height,top,bottom);const auto r=number(right.to_float()),b=number(bottom.to_float());
    q[0]=(left*c-top*s+x).to_float();q[1]=(left*s+top*c+y).to_float();q[2]=(right*c-top*s+x).to_float();q[3]=(top*c+right*s+y).to_float();q[4]=(left*c-bottom*s+x).to_float();q[5]=(left*s+c*bottom+y).to_float();q[6]=(r*c-b*s+x).to_float();q[7]=(r*s+b*c+y).to_float();}
static u64 state=0xbb67ae8584caa73bull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
// 0, an edge of [2^lo,2^hi], or a log-uniform magnitude inside it; either sign.
static float in_range(int lo,int hi){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%16){
    case 0:return fbits(sign);
    case 1:return fbits(u32(127+lo)<<23|sign);
    case 2:return fbits(u32(127+hi)<<23|sign);
    case 3:return fbits(bits(1.0f)|sign);
    default:{const u32 e=u32(127+lo)+u32((r>>8)%u64(hi-lo));return fbits(e<<23|u32((r>>16)&0x7fffffu)|sign);}
    }
}
static bool same(const float* a,const float* b,int n){for(int i=0;i<n;++i)if(bits(a[i])!=bits(b[i]))return false;return true;}
int main(int argc,char** argv){
    const u64 n=(argc>1?std::strtoull(argv[1],nullptr,0):20)*1000000ull;
    arithmetic_mode(Precision::Single,Rounding::NearestEven);
    const auto as_float=[](float v){return SpriteNumber(v);};const auto as_extended=[](float v){return th10::number(v);};
    unsigned long long checked=0,mismatches=0;
    for(u64 k=0;k<n;++k){
        Vm vm{{in_range(-20,20),in_range(-20,20),in_range(-20,20)},{in_range(-20,20),in_range(-20,20),in_range(-20,20)},{in_range(-20,20),in_range(-20,20),in_range(-20,20)},
              {in_range(-20,20),in_range(-20,20)},{in_range(-20,20),in_range(-20,20)},u32(next()%3)<<18|u32(next()%3)<<20};
        float a[8],b[8];
        translate(as_float,vm,a);translate(as_extended,vm,b);++checked;
        if(!same(a,b,3)){if(mismatches<8)std::printf("TRANSLATE MISMATCH\n");++mismatches;}
        const float dx=in_range(-60,40),dy=in_range(-60,40),dz=in_range(-60,40);
        scale(as_float,vm,dx,dy,dz,a[0],a[1]);scale(as_extended,vm,dx,dy,dz,b[0],b[1]);++checked;
        if(!same(a,b,2)){if(mismatches<8)std::printf("SCALE MISMATCH d=%08x %08x %08x\n",bits(dx),bits(dy),bits(dz));++mismatches;}
        const float w=in_range(-40,40),h=in_range(-40,40),c=in_range(-40,20),s=in_range(-40,20);const V2 center{in_range(-20,20),in_range(-20,20)};
        corners(as_float,vm,w,h,c,s,center,a);corners(as_extended,vm,w,h,c,s,center,b);++checked;
        if(!same(a,b,8)){if(mismatches<8)std::printf("CORNERS MISMATCH w=%08x h=%08x c=%08x s=%08x\n",bits(w),bits(h),bits(c),bits(s));++mismatches;}
    }
    std::printf("billboard_check: %llu stage cases, %llu mismatches\n",checked,mismatches);
    return mismatches?1:0;
}
