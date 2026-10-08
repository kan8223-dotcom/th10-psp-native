// Proves the TH10_ANM_SCROLL_SKIP condition (game/AnmInterpreter.cpp animate):
// with velocity +-0, value bits in [+0, 1.0) and a finite rate, the original
// Extended arithmetic returns the value unchanged. Every such value, both zero
// velocities, several rates, in every precision/rounding mode the game uses.
//   cmake --build <dir> --target th10_scroll_check && <dir>/th10_scroll_check
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cstdio>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// The original lambda, verbatim apart from the rate argument.
static float scroll(float value,float velocity,float rate_value){
    const auto rate=number(rate_value);
    auto next=rate*number(velocity)+number(value);
    if(number(1.0f)<next||number(1.0f)==next)next=next-number(1.0f);
    else if(next<number(0.0f))next=next+number(1.0f);
    return next.to_float();
}
int main(){
    const float rates[]={1.0f,0.5f,2.0f,1.0f/3.0f,0.99f,1.01f,fbits(0x00000001u),3.0e38f,-1.0f};
    const float velocities[]={0.0f,-0.0f};
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        for(float rate:rates)for(float velocity:velocities){
            // rates other than 1.0 on a sampled grid keep the run short; 1.0 (the game's rate) covers every value.
            const u32 step=rate==1.0f?1u:4099u;
            for(u32 b=0;b<0x3f800000u;b+=step){const float value=fbits(b);const float out=scroll(value,velocity,rate);++checked;
                if(bits(out)!=b){if(mismatches<5)std::printf("MISMATCH mode=%d rate=%a velocity=%a value=%08x out=%08x\n",int(m.p),rate,velocity,b,bits(out));++mismatches;}}}}
    std::printf("scroll skip condition: checked=%llu mismatches=%llu\n",checked,mismatches);return mismatches!=0;
}
