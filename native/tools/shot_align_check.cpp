// Checks two float paths against the original Extended operations:
//  - TH10_FAST_ALIGN (game/AnmRenderer.cpp submit, the pixel-alignment lambda):
//    every float bit pattern in precision 32 nearest (sampled in other modes);
//  - TH10_FAST_SHOT (game/PlayerDamage.cpp rectangle and shot_overlaps):
//    random and edge operands (zeros, the 2^-20/2^20 range edges, exact
//    cancellations, touching edges, subnormals, infinities, NaNs).
//   cmake --build <dir> --target th10_shot_align_check && <dir>/th10_shot_align_check [millions] [align_step]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
struct V2 {float x,y;};struct V3 {float x,y,z;};
struct Rectangle {float left,top,right,bottom;};
struct Definition {V2 hitbox;i32 type;};
struct Shot {const Definition* definition;struct {V3 position;} motion;};
// ---- originals (verbatim) ----
static float aligned_original(float x){return (number(x).round_to_integer()-number(0.5f)).to_float();}
static Rectangle rectangle_original(const V3& point,const V2& size){
    const auto half_x=number(size.x)*number(.5f),half_y=number(size.y)*number(.5f);
    return {(number(point.x)-half_x).to_float(),(number(point.y)-half_y).to_float(),
            (half_x+number(point.x)).to_float(),(half_y+number(point.y)).to_float()};
}
static bool shot_overlaps_original(const Shot& shot,const Rectangle& target){
    const auto half_x=number(shot.definition->hitbox.x)*number(.5f),half_y=number(shot.definition->hitbox.y)*number(.5f);
    const float left=(number(shot.motion.position.x)-half_x).to_float(),top=(number(shot.motion.position.y)-half_y).to_float();
    const float right=(half_x+number(shot.motion.position.x)).to_float();const auto bottom=half_y+number(shot.motion.position.y);
    if(number(target.right)<number(left)||number(target.bottom)<number(top)||bottom<number(target.top)||number(right)<number(target.left))return false;
    return !(number(shot.definition->type==3?target.bottom:top)<number(0.0f));
}
// ---- float paths (verbatim apart from counting) ----
static unsigned long long fast_align=0,fast_rect=0,fast_shot=0;
static float aligned_fast(float x){
        if(single_precision_nearest()&&arithmetic::representable(arithmetic::bits_of(x))){
            const float magnitude=std::fabs(x),r=magnitude>=8388608.f?x:std::copysign((magnitude+8388608.f)-8388608.f,x);++fast_align;return r-0.5f;}
        return (number(x).round_to_integer()-number(0.5f)).to_float();}
inline bool damage_operand(float v){const float a=std::fabs(v);return a==0||(a>=0x1p-20f&&a<=0x1p20f);}
static Rectangle rectangle_fast(const V3& point,const V2& size){
    if(single_precision_nearest()&&damage_operand(point.x)&&damage_operand(point.y)&&damage_operand(size.x)&&damage_operand(size.y)){
        const float half_x=size.x*.5f,half_y=size.y*.5f;++fast_rect;
        return {point.x-half_x,point.y-half_y,half_x+point.x,half_y+point.y};
    }
    return rectangle_original(point,size);
}
static bool shot_overlaps_fast(const Shot& shot,const Rectangle& target){
    {   const float hx=shot.definition->hitbox.x,hy=shot.definition->hitbox.y,px=shot.motion.position.x,py=shot.motion.position.y;
        if(single_precision_nearest()&&damage_operand(hx)&&damage_operand(hy)&&damage_operand(px)&&damage_operand(py)&&
           damage_operand(target.left)&&damage_operand(target.top)&&damage_operand(target.right)&&damage_operand(target.bottom)){
            const float half_x=hx*.5f,half_y=hy*.5f,left=px-half_x,top=py-half_y,right=half_x+px,bottom=half_y+py;++fast_shot;
            if(target.right<left||target.bottom<top||bottom<target.top||right<target.left)return false;
            return !((shot.definition->type==3?target.bottom:top)<0.0f);
        }
    }
    return shot_overlaps_original(shot,target);
}
static u64 state=0x853c49e6748fea9bull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x35800000u,0x357fffffu,0x49800000u,0x49800001u,0x3f000000u,
    0x3f800000u,0x43c00000u,0x44000000u,0x7f7fffffu,0x7f800000u,0x7fc00000u,0x41000000u,0x40800000u};
static bool in_range_only=false;   // half the cases draw only in-range kinds (2..4)
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(in_range_only?2+r%3:r%5){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%1600)-400)/4.0f));                 // playfield quarter pixels
    case 3:return fbits(bits(float((r>>8)%64)*0.5f)|sign);                        // hitbox sizes
    default:return fbits(((u32(105+(r>>8)%6)<<23)|u32((r>>16)&0x7fffffu))|sign);  // near 2^-20
    }
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;const u64 step=argc>2?std::strtoull(argv[2],nullptr,0):1;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Up},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);const bool main_mode=m.p==Precision::Single&&m.r==Rounding::NearestEven;
        // aligned: every float in the game's mode (step 1), sampled in the others
        const u64 s=main_mode?step:4099;
        for(u64 b=0;b<=0xffffffffull;b+=s){const float x=fbits(u32(b));const float a=aligned_original(x),c=aligned_fast(x);++checked;
            if(bits(a)!=bits(c)){if(mismatches<8)std::printf("ALIGN MISMATCH mode=%d/%d x=%08x %08x/%08x\n",int(m.p),int(m.r),u32(b),bits(a),bits(c));++mismatches;}}
        const u64 n=(main_mode?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            in_range_only=k&1;
            const V3 point{draw_value(),draw_value(),0};const V2 size{draw_value(),draw_value()};
            const Rectangle r1=rectangle_original(point,size),r2=rectangle_fast(point,size);++checked;
            if(bits(r1.left)!=bits(r2.left)||bits(r1.top)!=bits(r2.top)||bits(r1.right)!=bits(r2.right)||bits(r1.bottom)!=bits(r2.bottom)){if(mismatches<8)std::printf("RECT MISMATCH\n");++mismatches;}
            Definition def{{draw_value(),draw_value()},i32(next()%4)};Shot shot{&def,{{draw_value(),draw_value(),0}}};
            Rectangle target=r1;
            if((next()&3)==0){target.left=shot.motion.position.x+def.hitbox.x*.5f;}   // touching edges
            else if((next()&3)==0){target={draw_value(),draw_value(),draw_value(),draw_value()};}
            const bool o1=shot_overlaps_original(shot,target),o2=shot_overlaps_fast(shot,target);++checked;
            if(o1!=o2){if(mismatches<8)std::printf("SHOT MISMATCH mode=%d/%d\n",int(m.p),int(m.r));++mismatches;}
        }}
    std::printf("shot_align_check: %llu cases (float path: align %llu, rectangle %llu, shot %llu), %llu mismatches\n",checked,fast_align,fast_rect,fast_shot,mismatches);
    return mismatches?1:0;
}
