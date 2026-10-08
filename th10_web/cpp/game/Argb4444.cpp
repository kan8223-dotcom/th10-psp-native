#include "Argb4444.hpp"
namespace th10 {
void Argb4444::unpack(const u8* source,float* rgba,u32 width) noexcept {
    float levels[16];for(i32 i=0;i<16;++i)levels[i]=(Extended::from_int(i)*number(0x1.111112p-4f)).to_float();
    for(u32 i=0;i<width;++i){u16 value;std::memcpy(&value,source+i*2,2);rgba[i*4]=levels[(value>>8)&15];rgba[i*4+1]=levels[(value>>4)&15];rgba[i*4+2]=levels[value&15];rgba[i*4+3]=levels[value>>12];}
}
#if TH10_FAST_TEXT
void Argb4444::unpack_levels(float levels[16]) noexcept {for(i32 i=0;i<16;++i)levels[i]=(Extended::from_int(i)*number(0x1.111112p-4f)).to_float();}
void Argb4444::unpack(const float* levels,const u8* source,float* rgba,u32 width) noexcept {
    for(u32 i=0;i<width;++i){u16 value;std::memcpy(&value,source+i*2,2);rgba[i*4]=levels[(value>>8)&15];rgba[i*4+1]=levels[(value>>4)&15];rgba[i*4+2]=levels[value&15];rgba[i*4+3]=levels[value>>12];}
}
#endif
bool Argb4444::pack(const float* rgba,u8* destination,u32 width) noexcept {
    // With an ordinary float32 input in this interval, the half-step threshold
    // cannot cross an integer boundary through the DLL's intermediate stores.
    // Exceptional values, diffusion and nonuniform dithering use their own path.
    for(u32 i=0;i<width*4;++i)if(!(rgba[i]>=-1&&rgba[i]<=1))return false;
#if TH10_FAST_PACK
    // th10_port: the same quantization without double arithmetic (software
    // double on the PSP: 16 libgcc calls per pixel). For a float in [-1,1] the
    // old result below is the number of thresholds T_k <= value, T_k being the
    // smallest float whose old result is at least k (native/tools/pack_check.cpp
    // derives them from the old formula and compares every float in [-1,1]).
    static constexpr float thresholds[15]={0x1.111112p-5f,0x1.99999ap-4f,0x1.555556p-3f,0x1.dddddep-3f,0x1.333334p-2f,0x1.777778p-2f,0x1.bbbbbcp-2f,0x1p-1f,
        0x1.222224p-1f,0x1.444446p-1f,0x1.666668p-1f,0x1.88888ap-1f,0x1.aaaaacp-1f,0x1.cccccep-1f,0x1.eeeefp-1f};
    const auto quantize=[](float value){i32 n=0;for(i32 step=8;step;step>>=1)if(n+step<=15&&value>=thresholds[n+step-1])n+=step;return n;};
#else
    const auto quantize=[](float value){const i32 n=static_cast<i32>(static_cast<double>(value)*15+0.5);return n<0?0:n>15?15:n;};
#endif
    for(u32 i=0;i<width;++i){const auto* p=rgba+i*4;const u16 n=(quantize(p[3])<<12)|(quantize(p[0])<<8)|(quantize(p[1])<<4)|quantize(p[2]);std::memcpy(destination+i*2,&n,2);}return true;
}
}
