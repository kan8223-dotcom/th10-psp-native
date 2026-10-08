#pragma once
#include "Types.hpp"
namespace th10 {
struct RasterEnvironment;
struct RasterImage {
    u8 resource_fields[0x100];
    u32 format;
    i32 width,height;
    i32 byte_size;
    i32 pitch;
    u32 device_context;
    u32 previous_bitmap,bitmap_handle;
    u8* pixels;
    void initialize() noexcept;
    bool release(RasterEnvironment& environment);
    bool create(i32 width,i32 height,u32 format,RasterEnvironment& environment);
    bool create_compatible(i32 width,i32 height,u32 format,RasterEnvironment& environment);
    bool invert_alpha(i32 rows) noexcept;
    bool fill_transparent_edges(u32 rows) noexcept;
    void fill_text_background(u32 color) noexcept;
#if TH10_FAST_TEXT
    // th10_port (TH10_FAST_TEXT): fill_transparent_edges(rows) for A4R4G4B4
    // without row padding, visiting only box +-1 (box: the covered pixels of
    // rows < `rows`, half-open; every other pixel there is transparent with
    // RGB 0 and no opaque neighbour, which the full pass rewrites as 0).
    void fill_transparent_edges_box(u32 rows,i32 left,i32 top,i32 right,i32 bottom) noexcept;
#endif
};
static_assert(offsetof(RasterImage,pixels)==0x120);
}
