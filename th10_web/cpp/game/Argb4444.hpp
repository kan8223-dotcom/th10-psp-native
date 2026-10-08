#pragma once
#include "Arithmetic.hpp"
namespace th10 {
// Pixel conversion used by the original font atlas. These operations accept
// ordinary image rows and have no dependency on a CPU or DLL object layout.
struct Argb4444 {
    static void unpack(const u8* source,float* rgba,u32 width) noexcept;
    static bool pack(const float* rgba,u8* destination,u32 width) noexcept;
#if TH10_FAST_TEXT
    // th10_port (TH10_FAST_TEXT): unpack() split into its level table, which
    // it recomputes per row with 16 Extended products, and the row loop.
    static void unpack_levels(float levels[16]) noexcept;
    static void unpack(const float* levels,const u8* source,float* rgba,u32 width) noexcept;
#endif
};
}
