#pragma once
#include "AnmFile.hpp"
namespace th10 {
struct PixelSurface {
    u32 format,width,height;
    i32 pitch;
    u8* pixels;
    void repair_edges() noexcept;
};
struct TextureDescription {u32 format,width,height;};
struct TextureLock {i32 pitch;u8* pixels;};
struct TextureRect {i32 left,top,right,bottom;};
struct TexturePlatform {
    const u32* display_flags;
    virtual i32 create_surface_texture(AnmTexture& texture,u32 width,u32 height,u32 format)=0;
    virtual i32 decode_source_texture(AnmTexture& texture,u32 width,u32 height,u32 format,u32 color_key)=0;
    virtual void* get_surface(void* texture)=0;
    virtual TextureDescription describe_surface(void* surface)=0;
    virtual TextureLock lock_surface(void* surface)=0;
    virtual void unlock_surface(void* surface)=0;
    virtual void release_surface(void* surface)=0;
    virtual i32 upload_surface(void* surface,const u8* bytes,u32 format,i32 pitch,const TextureRect& rectangle)=0;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    // th10_port (TH10_TEXTURE_CLUT8): create the texture create_surface_texture +
    // upload_surface would make, kept as 8-bit indices into a table of its
    // colours, when it has 256 or fewer; nonzero (nothing created) otherwise.
    virtual i32 create_indexed_texture(AnmTexture&,u32 width,u32 height,u32 format,const u8* bytes,u32 source_format,i32 pitch,const TextureRect& rectangle){(void)width;(void)height;(void)format;(void)bytes;(void)source_format;(void)pitch;(void)rectangle;return -1;}
#endif
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    // th10_port (TH10_ANM_STREAM_LOAD): `rows` rows of `width` pixels converted as
    // upload_surface converts them (nonzero: refused).
    virtual i32 convert_pixels(u8* output,u32 output_format,const u8* source,u32 source_format,u32 width,u32 rows){(void)output;(void)output_format;(void)source;(void)source_format;(void)width;(void)rows;return -1;}
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    // th10_port (TH10_TEXTURE_LAZY_PADDED): the texture create_indexed_texture, or
    // else create_surface_texture + upload_surface, would make (same arguments),
    // created without bytes: they are made, exactly so, when it is first drawn
    // or touched. Nonzero (nothing created): make it now.
    virtual i32 create_padded_texture(AnmTexture&,u32 width,u32 height,u32 format,const u8* bytes,u32 source_format,i32 pitch,const TextureRect& rectangle){(void)width;(void)height;(void)format;(void)bytes;(void)source_format;(void)pitch;(void)rectangle;return -1;}
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    // th10_port (TH10_TEXTURE_LAZY_EMPTY): an '@' texture (create_empty) whose
    // pixels are allocated when something first writes them.
    virtual i32 create_empty_texture(AnmTexture& texture,u32 width,u32 height,u32 format){return create_surface_texture(texture,width,height,format);}
#endif
};
}
