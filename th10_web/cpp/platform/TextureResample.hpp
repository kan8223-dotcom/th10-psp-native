#pragma once
#include "Textures.hpp"
namespace th10::browser {
struct TextureResample {
    static i32 point(PixelSurface& output,const TextureRect& destination,const PixelSurface& input,const TextureRect& source) noexcept;
    static i32 triangle(PixelSurface& output,const TextureRect& destination,const PixelSurface& input,const TextureRect& source,bool wrap_x=true,bool wrap_y=true,bool dither=false) noexcept;
#if TH10_FAST_TEXT
    // th10_port (TH10_FAST_TEXT): triangle() with the defaults (wrap, no
    // dither) for an input whose pixels outside `content` are all zero.
    static i32 triangle_content(PixelSurface& output,const TextureRect& destination,const PixelSurface& input,const TextureRect& source,const TextureRect& content) noexcept;
#endif
};
#if TH10_TEXT_AUDIT
extern u64 text_audit_counts[8]; // unpacked, nonzero, multiply-adds, packed nonzero, packed zero
#endif
}
