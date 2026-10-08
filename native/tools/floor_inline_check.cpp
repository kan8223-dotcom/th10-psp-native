// th10_port: TH10_RENDER_FAST_BULLETS floor_inline against std::floor for
// every float bit pattern in [begin, end) (NaNs: both must be NaN).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
using i32 = std::int32_t;
inline float floor_inline(float x){if(!(std::fabs(x)<8388608.f)||x==0)return x;const float t=static_cast<float>(static_cast<i32>(x));return t>x?t-1.0f:t;}
int main(int argc,char** argv){
    const unsigned long long begin=argc>1?std::strtoull(argv[1],nullptr,0):0,end=argc>2?std::strtoull(argv[2],nullptr,0):0x100000000ull;
    unsigned long long bad=0,checked=0;
    for(unsigned long long v=begin;v<end;++v){const std::uint32_t b=static_cast<std::uint32_t>(v);float x;std::memcpy(&x,&b,4);
        const float a=std::floor(x),c=floor_inline(x);std::uint32_t ab,cb;std::memcpy(&ab,&a,4);std::memcpy(&cb,&c,4);++checked;
        if(std::isnan(a)?!std::isnan(c):ab!=cb){if(bad++<10)std::printf("MISMATCH %08x floor=%08x inline=%08x\n",b,ab,cb);}}
    std::printf("floor_inline_check [%llx,%llx) checked=%llu mismatches=%llu\n",begin,end,checked,bad);return bad?1:0;
}
