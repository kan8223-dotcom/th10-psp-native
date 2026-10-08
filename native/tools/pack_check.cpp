// Checks the TH10_FAST_PACK quantizer (game/Argb4444.cpp pack): first derives,
// from the old double formula, the smallest float whose old result is at least
// k (k = 1..15) and asserts that the new threshold table holds exactly those
// floats; then compares old and new on every float in [-1,1] (pack rejects
// everything else, NaN included), -0 included. The old formula runs in IEEE
// double here (-msse2 -mfpmath=sse), as libgcc's software double does on PSP.
//   cmake --build <dir> --target th10_pack_check && <dir>/th10_pack_check
#include <cstdint>
#include <cstdio>
#include <cstring>
using i32=std::int32_t;using u32=std::uint32_t;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// The original (Argb4444.cpp, the #else branch), verbatim.
static i32 quantize_original(float value){const auto quantize=[](float value){const i32 n=static_cast<i32>(static_cast<double>(value)*15+0.5);return n<0?0:n>15?15:n;};return quantize(value);}
// The TH10_FAST_PACK branch, verbatim.
static constexpr float thresholds[15]={0x1.111112p-5f,0x1.99999ap-4f,0x1.555556p-3f,0x1.dddddep-3f,0x1.333334p-2f,0x1.777778p-2f,0x1.bbbbbcp-2f,0x1p-1f,
    0x1.222224p-1f,0x1.444446p-1f,0x1.666668p-1f,0x1.88888ap-1f,0x1.aaaaacp-1f,0x1.cccccep-1f,0x1.eeeefp-1f};
static i32 quantize_fast(float value){const auto quantize=[](float value){i32 n=0;for(i32 step=8;step;step>>=1)if(n+step<=15&&value>=thresholds[n+step-1])n+=step;return n;};return quantize(value);}
int main(){
    // 1. the table from the old formula (monotone on [0,1]; binary search over float bits)
    int table_errors=0;
    for(i32 k=1;k<=15;++k){
        u32 lo=0,hi=0x3f800000u;
        while(lo<hi){const u32 mid=lo+(hi-lo)/2;if(quantize_original(fbits(mid))>=k)hi=mid;else lo=mid+1;}
        if(lo!=bits(thresholds[k-1])){std::printf("TABLE k=%d derived %08x table %08x\n",k,lo,bits(thresholds[k-1]));++table_errors;}
    }
    // 2. every float that pack accepts
    unsigned long long checked=0,mismatches=0;
    for(unsigned long long b=0;b<=0xffffffffull;++b){
        const float v=fbits(u32(b));if(!(v>=-1&&v<=1))continue;++checked;
        if(quantize_original(v)!=quantize_fast(v)){if(mismatches<8)std::printf("MISMATCH v=%08x old=%d new=%d\n",u32(b),quantize_original(v),quantize_fast(v));++mismatches;}
    }
    std::printf("pack_check: table %s, %llu floats in [-1,1], %llu mismatches\n",table_errors?"WRONG":"= old formula",checked,mismatches);
    return table_errors||mismatches?1:0;
}
