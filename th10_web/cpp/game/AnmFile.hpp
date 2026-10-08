#pragma once
#include "AnmVm.hpp"
namespace th10 {
struct AnmResourceEnvironment;
struct AnmTextureEnvironment;
struct TexturePlatform;
struct AnmChunk;
struct AnmTexture {
    void* handle;u8* source;u32 source_size,bytes_per_pixel;
    i32 create_empty(i32 width,i32 height,i32 format,TexturePlatform& platform);
    i32 create_encoded(i32 width,i32 height,i32 format,u32 color_key,TexturePlatform& platform);
    i32 create_embedded(const u8* data,i32 width,i32 height,i32 format,TexturePlatform& platform);
    void repair_edges(TexturePlatform& platform);
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    // th10_port (TH10_ANM_STREAM_LOAD): create_embedded in steps, for an image
    // the stream loader converts band by band: the format index it picks
    // (`binary`: every alpha byte of the A8R8G8B8 image is 0 or 255), rows
    // converted to that format, and the creation from such rows (`data`: the
    // THTX header with that index as its format, then the converted pixels).
    static u32 embedded_index(i32 format,const u8* header,bool binary,TexturePlatform& platform);
    static i32 convert_embedded_rows(u8* output,u32 index,const u8* source,const u8* header,u32 rows,TexturePlatform& platform);
    i32 create_embedded_as(const u8* data,i32 width,i32 height,u32 index,TexturePlatform& platform);
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    // th10_port (TH10_TEXTURE_LAZY_PADDED): create_embedded's texture, its bytes
    // made when it is first drawn or touched (TexturePlatform::create_padded_texture);
    // create_embedded itself when the platform refuses.
    i32 create_embedded_padded(const u8* data,i32 width,i32 height,i32 format,TexturePlatform& platform);
#endif
};
static_assert(sizeof(AnmTexture)==16);
struct AnmSprite {
    u32 file_index;
    void* texture;
    float left,top,right,bottom;
    float texture_height,texture_width;
    float u0,v0,u1,v1;
    float height,width;
    float scale_x,scale_y;
    u32 reserved_40;
};
static_assert(sizeof(AnmSprite)==0x44);
struct AnmFile {
    i32 file_index;
    char name[260];
    u8* loaded;
    i32 texture_count,script_count,sprite_count;
    AnmSprite* sprites;
    AnmInstruction** scripts;
    AnmTexture* textures;
    u32 unavailable;
    u32 discard_request;
    u8* extra_data;
#if TH10_ANM_COMPACT
    u32 loaded_size;   // th10_port: bytes of `loaded`, for compact()
    void compact(AnmResourceEnvironment& environment);
#endif
    i32 prepare_chunk(i32 index,const AnmChunk* chunk,AnmResourceEnvironment& environment);
    bool complete_next(AnmResourceEnvironment& environment);
    void release(AnmResourceEnvironment& environment);
    void set_sprite(i32 index,const AnmSprite& sprite) noexcept;
    i32 materialize(i32 texture,i32 first_sprite,i32 first_script,const AnmChunk* chunk,AnmTextureEnvironment& environment);
    i32 bind_sprite(AnmVm& vm,i32 index) noexcept;
    void start_script(AnmVm& vm,i32 index,AnmEnvironment& environment,u32& started_scripts);
    void initialize_script(AnmVm& vm,i32 index,AnmEnvironment& environment,u32& started_scripts);
    void bind_script(AnmVm& vm,i32 index,AnmEnvironment& environment,u32& started_scripts);
    void prepare_script(AnmVm& vm,i32 index,AnmEnvironment& environment,u32& started_scripts);
#if defined(TH10_TRANSITION_LOWMEM) && TH10_TRANSITION_LOWMEM
    void release_textures(AnmResourceEnvironment& environment);
#endif
};
static_assert(offsetof(AnmFile,sprites)==0x118);
static_assert(offsetof(AnmFile,unavailable)==0x124);
#if TH10_ANM_COMPACT
static_assert(sizeof(AnmFile)==0x134);   // th10_port: + loaded_size (AnmFile is only used through pointers)
#else
static_assert(sizeof(AnmFile)==0x130);
#endif
void initialize_embedded_animation(AnmFile& file,AnmVm& vm,i32 script,AnmEnvironment& environment,u32& started);
#if defined(TH10_TRANSITION_LOWMEM) && TH10_TRANSITION_LOWMEM
// th10_port (TH10_TRANSITION_LOWMEM, game/AnmResources.cpp): the fading old
// stage's ANM file after its textures were released at the stage transition.
// Its VMs keep updating; AnmRenderer::draw and draw_textured_fan skip them.
extern AnmFile* anm_released_textures;
#endif
struct AnmVertex {Vec3 position;float reciprocal_w;u32 color;Vec2 uv;};
static_assert(sizeof(AnmVertex)==28);
}
