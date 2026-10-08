#pragma once
#include "Graphics.hpp"
#include "../game/TexturePlatform.hpp"
namespace th10::browser {
struct PixelCopy {
    static i32 copy(PixelSurface& destination,const TextureRect& target,const u8* source,u32 source_format,i32 source_pitch,const TextureRect& region) noexcept;
};
struct Textures final:TexturePlatform {
    GraphicsDevice& device;
    explicit Textures(GraphicsDevice& device,const u32& flags):device(device){display_flags=&flags;}
    i32 create_surface_texture(AnmTexture& texture,u32 width,u32 height,u32 format) override;
    i32 decode_source_texture(AnmTexture&,u32,u32,u32,u32) override;
    void* get_surface(void* texture) override;
    TextureDescription describe_surface(void* surface) override;
    TextureLock lock_surface(void* surface) override;
    void unlock_surface(void* surface) override;
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
    bool write_surface(void* surface,const TextureRect& rectangle,const u8* pixels,i32 pitch){return device.write_surface(surface,rectangle,pixels,pitch);}
#endif
    void release_surface(void* surface) override;
    i32 upload_surface(void* surface,const u8* bytes,u32 format,i32 pitch,const TextureRect& rectangle) override;
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    i32 convert_pixels(u8* output,u32 output_format,const u8* source,u32 source_format,u32 width,u32 rows) override;
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    i32 create_indexed_texture(AnmTexture& texture,u32 width,u32 height,u32 format,const u8* bytes,u32 source_format,i32 pitch,const TextureRect& rectangle) override;
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    i32 create_empty_texture(AnmTexture& texture,u32 width,u32 height,u32 format) override;
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    i32 create_padded_texture(AnmTexture& texture,u32 width,u32 height,u32 format,const u8* bytes,u32 source_format,i32 pitch,const TextureRect& rectangle) override;
#endif
};
}
