// Checks TH10_FAST_ANIMATE (game/AnmInterpreter.cpp animate: rotation step,
// scale velocity, UV scroll) against the original Extended expressions on
// random and edge operands, in every precision/rounding mode the game uses.
//   cmake --build <dir> --target th10_animate_check && <dir>/th10_animate_check [millions]
#include "../../th10_web/cpp/game/FastFloat.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// ---- originals (verbatim from the #else branch, rate passed in) ----
static float step_original(float r,float v){const auto rate=number(r);return (rate*number(v)).to_float();}
static float scaled_original(float r,float velocity,float value){const auto rate=number(r);return (rate*number(velocity)+number(value)).to_float();}
static float scroll_original(float r,float value,float velocity){
    const auto rate=number(r);
    auto next=rate*number(velocity)+number(value);
    if(number(1.0f)<next||number(1.0f)==next)next=next-number(1.0f);
    else if(next<number(0.0f))next=next+number(1.0f);
    return next.to_float();
}
// ---- TH10_FAST_ANIMATE (verbatim apart from the arguments and counting; the
//      scroll skip block is outside both and unchanged) ----
static unsigned long long hits=0;
namespace ff=fast_float;
static float step_fast(float rate_value,float v){
    const bool float_mode=single_precision_nearest()&&ff::operand(rate_value);float step;
    if(!(float_mode&&ff::operand(v)&&ff::mul(rate_value,v,step)))step=(number(rate_value)*number(v)).to_float();else ++hits;
    return step;
}
static float scaled_fast(float rate_value,float velocity,float value){
    const bool float_mode=single_precision_nearest()&&ff::operand(rate_value);float product,sum;
    if(float_mode&&ff::operand(velocity)&&ff::operand(value)&&ff::mul(rate_value,velocity,product)&&ff::add(product,value,sum)){++hits;return sum;}
    return (number(rate_value)*number(velocity)+number(value)).to_float();
}
static float scroll_fast(float rate_value,float value,float velocity){
    const bool float_mode=single_precision_nearest()&&ff::operand(rate_value);
    {   float product,next,wrapped;
        if(float_mode&&ff::operand(velocity)&&ff::operand(value)&&ff::mul(rate_value,velocity,product)&&ff::add(product,value,next)){
            if(1.0f<=next){if(ff::sub(next,1.0f,wrapped)){++hits;return wrapped;}}
            else if(next<0.0f){if(ff::add(next,1.0f,wrapped)){++hits;return wrapped;}}
            else {++hits;return next;}
        }
    }
    auto next=number(rate_value)*number(velocity)+number(value);
    if(number(1.0f)<next||number(1.0f)==next)next=next-number(1.0f);
    else if(next<number(0.0f))next=next+number(1.0f);
    return next.to_float();
}
static u64 state=0xa54ff53a5f1d36f1ull;
static u64 next_random(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x3f800000u,0x3f7fffffu,0x3f800001u,0x3f000000u,
    0xbf800000u,0x33800000u,0x7f7fffffu,0x7f800000u,0x7fc00000u,0x3c23d70au,0x3e800000u};
static float draw_value(bool in){
    const u64 r=next_random();const u32 sign=u32(r>>63)<<31;
    switch(in?2+r%3:r%5){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float((r>>8)%4096)/4096.0f)|sign);                    // UV offsets and small steps
    case 3:return fbits(((u32(100+(r>>8)%40)<<23)|u32((r>>16)&0x7fffffu))|sign);   // 2^-27..2^12
    default:return fbits(bits(float(i32((r>>8)%2001)-1000)/256.0f));
    }
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        const u64 n=(m.p==Precision::Single&&m.r==Rounding::NearestEven?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){const bool in=k&1;
            const float rate=(next_random()&3)?1.0f:draw_value(in),a=draw_value(in),b=draw_value(in);
            const float x1=step_original(rate,a),x2=step_fast(rate,a);
            const float y1=scaled_original(rate,a,b),y2=scaled_fast(rate,a,b);
            float value=draw_value(in);if((next_random()&7)==0)value=fbits(bits(1.0f-rate*a));
            const float z1=scroll_original(rate,value,a),z2=scroll_fast(rate,value,a);checked+=3;
            auto bad=[](float p,float q){return bits(p)!=bits(q)&&!(std::isnan(p)&&std::isnan(q));};
            if(bad(x1,x2)||bad(y1,y2)||bad(z1,z2)){if(mismatches<8)std::printf("MISMATCH mode=%d/%d rate=%08x a=%08x b=%08x value=%08x\n",int(m.p),int(m.r),bits(rate),bits(a),bits(b),bits(value));++mismatches;}
        }}
    std::printf("animate_check: %llu cases (%llu on the float path), %llu mismatches\n",checked,hits,mismatches);
    return mismatches?1:0;
}
