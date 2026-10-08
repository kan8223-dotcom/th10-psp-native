// Checks TH10_FAST_FLOOR (game/Movement.cpp snap and the float-path pixel floor
// in game/AnmRenderer.cpp axis_geometry) against the original double floor on
// every float bit pattern, in every precision/rounding mode the game uses.
//   cmake --build <dir> --target th10_floor_check && <dir>/th10_floor_check [step]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// Movement.cpp snap: the original, verbatim.
static float snap_original(float value){return (Extended::from_double(std::floor((number(value)*number(100.0f)).to_double()))*number(0.01f)).to_float();}
// The TH10_FAST_FLOOR branch, verbatim apart from counting.
static unsigned long long fast_hits=0;
static float snap_fast(float value){
        using namespace arithmetic;
        if(single_precision_nearest()&&representable(bits_of(value))){
            const float scaled=value*100.0f;const u32 scaled_bits=bits_of(scaled);
            if((scaled_bits&0x7fffffffu)?nonzero_accepted(scaled_bits):value==0){
                const float whole=std::floor(scaled),result=whole*0.01f;const u32 result_bits=bits_of(result);
                if((result_bits&0x7fffffffu)?nonzero_accepted(result_bits):whole==0){++fast_hits;return result;}
            }
        }
        return (Extended::from_double(std::floor((number(value)*number(100.0f)).to_double()))*number(0.01f)).to_float();
}
// axis_geometry float path: SpriteNumber::from_double(floor(to_double())) vs floorf.
static float pixel_original(float v){return float(std::floor(double(v)));}
static float pixel_fast(float v){return std::floor(v);}
int main(int argc,char** argv){
    const unsigned long long step=argc>1?std::strtoull(argv[1],nullptr,0):1;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::Down},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    unsigned long long checked=0,mismatches=0,pixel_checked=0,pixel_mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);const bool main_mode=m.p==Precision::Single&&m.r==Rounding::NearestEven;
        for(unsigned long long b=0;b<=0xffffffffull;b+=main_mode?step:65537){const float v=fbits(u32(b));const float a=snap_original(v),c=snap_fast(v);++checked;
            if(bits(a)!=bits(c)&&!(std::isnan(a)&&std::isnan(c))){if(mismatches<8)std::printf("SNAP MISMATCH mode=%d/%d v=%08x %08x/%08x\n",int(m.p),int(m.r),u32(b),bits(a),bits(c));++mismatches;}}}
    for(unsigned long long b=0;b<=0xffffffffull;b+=step){const float v=fbits(u32(b));if(std::isnan(v))continue;++pixel_checked;if(bits(pixel_original(v))!=bits(pixel_fast(v))){if(pixel_mismatches<8)std::printf("PIXEL MISMATCH v=%08x\n",u32(b));++pixel_mismatches;}}
    std::printf("floor_check: snap %llu cases (%llu on the float path), %llu mismatches; pixel floor %llu non-NaN floats, %llu mismatches\n",checked,fast_hits,mismatches,pixel_checked,pixel_mismatches);
    return mismatches||pixel_mismatches?1:0;
}
