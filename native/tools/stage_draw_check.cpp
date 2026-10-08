// Checks TH10_FAST_STAGE_CULL (game/StageRenderer.cpp culled, the distance
// test) and TH10_FAST_MODEL (game/AnmProjection.cpp draw_model, the world
// translation): with precision 32 nearest and inputs inside the gate's range,
// the plain-float form must give the same result as the Extended one (the
// cull decision and the squared distance's bits; the three translation
// floats). Inputs are drawn log-uniformly over the allowed range with both
// signs, zeros and the exact range edges; cull cases also place the camera so
// that differences cancel, and put limit on the distance and its neighbours.
//   cmake --build <dir> --target th10_stage_draw_check && <dir>/th10_stage_draw_check [millions] [wide]
// "wide" draws outside the gate (2^-75..2^60) to show the check can fail there.
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
struct V3 {float x,y,z;};
inline bool less_than(const Extended& a,const Extended& b){return a<b;}
inline bool less_than(SpriteNumber a,SpriteNumber b){return a.value<b.value;}
// StageRenderer.cpp culled(): the distance lambda, verbatim apart from taking
// its inputs as arguments and also returning the squared distance.
template<class Number> bool far(Number number,const V3& position,const V3& instance,const V3& eye,const V3& camera,float limit,float& square){
    const auto x=number(Scalar::add(position.x,instance.x))-(number(eye.x)+number(camera.x));
    const auto y=number(Scalar::add(position.y,instance.y))-(number(eye.y)+number(camera.y));
    const auto z=number(Scalar::add(position.z,instance.z))-number(Scalar::add(eye.z,camera.z));
    const auto dx=number(x.to_float()),dy=number(y.to_float()),dz=number(z.to_float());
    const auto sum=dz*dz+dy*dy+dx*dx;square=sum.to_float();
    return less_than(number(limit),sum);}
// AnmProjection.cpp draw_model(): the original Extended lambda and the float one.
static Extended sum(float a,float b,float c){return number(a)+number(b)+number(c);}
static float translated_extended(u32 anchor,float size,float scale,float child,float position,float script,float previous){auto half=(number(size)*number(scale)*number(.5f)).magnitude();switch(anchor){case 0:return sum(child,position,script).to_float();case 1:return (sum(child,position,script)-half).to_float();case 2:return (half+number(child)+number(position)+number(script)).to_float();default:return previous;}}
static float translated_float(u32 anchor,float size,float scale,float child,float position,float script,float previous){
    switch(anchor){case 0:return (child+position)+script;case 1:return ((child+position)+script)-std::fabs((size*scale)*.5f);case 2:return ((std::fabs((size*scale)*.5f)+child)+position)+script;default:return previous;}}
static u64 state=0x3c6ef372fe94f82bull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
// 0, an edge of [2^lo,2^hi], or a log-uniform magnitude inside it; either sign.
static int widen=0;   // argv[2]=wide: sensitivity test (outside the gate, mismatches expected)
static float in_range(int lo,int hi){if(widen){lo=lo<-20?lo:-75;hi=60;}
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%16){
    case 0:return fbits(sign);
    case 1:return fbits(u32(127+lo)<<23|sign);
    case 2:return fbits(u32(127+hi)<<23|sign);
    case 3:return fbits(bits(1.0f)|sign);
    default:{const u32 e=u32(127+lo)+u32((r>>8)%u64(hi-lo));return fbits(e<<23|u32((r>>16)&0x7fffffu)|sign);}
    }
}
static bool gate_ok(float v){const float a=std::fabs(v);return a==0||(a>=0x1p-20f&&a<=0x1p20f);}
int main(int argc,char** argv){
    const u64 n=(argc>1?std::strtoull(argv[1],nullptr,0):20)*1000000ull;
    widen=argc>2&&!std::strcmp(argv[2],"wide");
    arithmetic_mode(Precision::Single,Rounding::NearestEven);
    const auto as_float=[](float v){return SpriteNumber(v);};const auto as_extended=[](float v){return th10::number(v);};
    unsigned long long cull_cases=0,cull_far=0,cull_mismatch=0,cull_cancel=0,model_cases=0,model_mismatch=0;
    for(u64 k=0;k<n;++k){
        V3 p{in_range(-20,20),in_range(-20,20),in_range(-20,20)},i{in_range(-20,20),in_range(-20,20),in_range(-20,20)},e{in_range(-20,20),in_range(-20,20),in_range(-20,20)},c{in_range(-20,20),in_range(-20,20),in_range(-20,20)};
        if(next()%2){   // camera near the object: differences cancel (fully or partly)
            float* pc[3]={&c.x,&c.y,&c.z};const float* pe[3]={&e.x,&e.y,&e.z};const float s[3]={p.x+i.x,p.y+i.y,p.z+i.z};
            for(int a=0;a<3;++a){const float t=(s[a]-*pe[a])+(next()%3==0?0.f:in_range(-20,-5));if(gate_ok(t))*pc[a]=t;}
            ++cull_cancel;}
        float limit;switch(next()%4){case 0:limit=0;break;case 1:limit=fbits(u32(1+next()%253)<<23|u32(next()&0x7fffff));break;default:{float sq;far(as_float,p,i,e,c,1.f,sq);
            const u32 b=bits(sq)+u32(int(next()%5)-2);limit=fbits(b);break;}}
        if(!(bits(limit)>>23&255)||(bits(limit)>>23&255)==255)limit=0;   // the gate needs a normal float or zero
        float sa,sb;const bool fa=far(as_float,p,i,e,c,limit,sa),fb=far(as_extended,p,i,e,c,limit,sb);++cull_cases;cull_far+=fb;
        if(fa!=fb||bits(sa)!=bits(sb)){if(cull_mismatch<8)std::printf("CULL MISMATCH limit=%08x float=%d/%08x extended=%d/%08x\n",bits(limit),fa,bits(sa),fb,bits(sb));++cull_mismatch;}
        for(u32 anchor=0;anchor<4;++anchor){
            const float size=in_range(-20,20),scale=in_range(-20,20),child=in_range(-20,20),position=in_range(-20,20),script=in_range(-20,20),previous=in_range(-20,20);
            const float a=translated_float(anchor,size,scale,child,position,script,previous),b=translated_extended(anchor,size,scale,child,position,script,previous);++model_cases;
            if(bits(a)!=bits(b)){if(model_mismatch<8)std::printf("MODEL MISMATCH anchor=%u %08x vs %08x\n",anchor,bits(a),bits(b));++model_mismatch;}
        }
    }
    std::printf("stage_draw_check: cull %llu cases (%llu far, %llu with cancelling camera), %llu mismatches; model %llu cases, %llu mismatches\n",cull_cases,cull_far,cull_cancel,cull_mismatch,model_cases,model_mismatch);
    return cull_mismatch||model_mismatch?1:0;
}
