#pragma once
#include "AnmManager.hpp"
namespace th10 {
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
struct ResourceStream;
#endif
struct AnmChunk {
    i32 sprite_count,script_count;
    u32 reserved_008;
    i32 width,height,format;
    u32 color_key,name_offset,reserved_020,reserved_024,version,priority,texture_offset;
    u8 embedded_texture,reserved_035[3];
    u32 next_offset,reserved_03c;
    const AnmChunk* next() const noexcept {return reinterpret_cast<const AnmChunk*>(reinterpret_cast<const u8*>(this)+next_offset);}
    const char* texture_name() const noexcept {return reinterpret_cast<const char*>(this)+name_offset;}
};
static_assert(sizeof(AnmChunk)==0x40);
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
// th10_port (TH10_TEXTURE_LAZY_PADDED, PSP-1000 lane): true when the THTX image
// (16-byte header at `thtx`) of the chunk at `bytes` is smaller than the
// chunk's texture and one of the chunk's sprites reaches past the image. In
// TH10 these are the face/dummy.png slots of stgenm01-07.anm: an 8x8 white
// image in a 256x256, 256x64 or 512x256 texture whose one sprite is the whole
// texture, for expressions the stage's dialogue does not use. The padding
// cannot be cut off (the sprite samples it), so such a texture is created
// without bytes and gets them, exactly as create_embedded makes them, when it
// is first drawn or touched (TexturePlatform::create_padded_texture).
inline bool anm_sprites_past_image(const u8* bytes,const u8* thtx) noexcept {
    AnmChunk chunk;__builtin_memcpy(&chunk,bytes,sizeof(chunk));short w,h;__builtin_memcpy(&w,thtx+8,2);__builtin_memcpy(&h,thtx+10,2);
    if(w<=0||h<=0||(w>=chunk.width&&h>=chunk.height))return false;
    for(i32 i=0;i<chunk.sprite_count;++i){u32 offset;__builtin_memcpy(&offset,bytes+sizeof(AnmChunk)+4*u32(i),4);float r[4];__builtin_memcpy(r,bytes+offset+4,sizeof(r));
        if(r[0]+r[2]>float(w)||r[1]+r[3]>float(h))return true;}
    return false;
}
#endif
enum class AnmResourceError {InvalidSlot,MissingHeader,InvalidVersion,MissingTexture,EncodedTexture,EmbeddedTexture};
struct AnmTextureDimensions {u32 width,height;};
struct AnmTextureEnvironment {
    virtual i32 create_empty(AnmTexture& texture,i32 width,i32 height,i32 format)=0;
    virtual i32 create_encoded(AnmTexture& texture,i32 width,i32 height,i32 format,u32 color_key)=0;
    virtual i32 create_embedded(AnmTexture& texture,const u8* source,i32 width,i32 height,i32 format)=0;
    virtual void set_priority(void* texture,u32 priority)=0;
    virtual void preload(void* texture)=0;
    virtual AnmTextureDimensions dimensions(void* texture)=0;
    virtual void texture_error(AnmResourceError error,const char* name)=0;
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    // th10_port (TH10_ANM_STREAM_LOAD): create_embedded in steps (AnmTexture::embedded_index,
    // convert_embedded_rows, create_embedded_as) for images bigger than the stream loader's scratch.
    virtual u32 embedded_index(i32 format,const u8* header,bool binary){(void)format;(void)header;(void)binary;return ~0u;}
    virtual i32 convert_embedded_rows(u8* output,u32 index,const u8* source,const u8* header,u32 rows){(void)output;(void)index;(void)source;(void)header;(void)rows;return -1;}
    virtual i32 create_embedded_as(AnmTexture& texture,const u8* data,i32 width,i32 height,u32 index){(void)texture;(void)data;(void)width;(void)height;(void)index;return -1;}
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    // th10_port (TH10_TEXTURE_LAZY_PADDED): create_embedded for a chunk anm_sprites_past_image picks.
    virtual i32 create_embedded_padded(AnmTexture& texture,const u8* source,i32 width,i32 height,i32 format){return create_embedded(texture,source,width,height,format);}
#endif
};
struct AnmResourceEnvironment {
    AnmRegistry* registry;
    const u32* loader_flags;
    virtual AnmFile* allocate_file()=0;
    virtual void* allocate_bytes(u32 size)=0;
    virtual void release_file(AnmFile* file)=0;
    virtual void release_bytes(void* memory)=0;
    virtual u8* read_file(const char* name,bool external_texture,u32* size)=0;
    virtual void release_texture(void* texture)=0;
    virtual void report(AnmResourceError error)=0;
    virtual i32 materialize(AnmFile& file,i32 texture,i32 sprite,i32 script,const AnmChunk* chunk)=0;
    virtual void wait_for_loading(AnmManager& manager,AnmFile& file)=0;
#ifdef TH_NATIVE_PLATFORM
    virtual AnmFile* prepared_file(i32,const char*){return nullptr;}
#endif
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    // th10_port (TH10_ANM_STREAM_LOAD, AnmManager::open): the file's decoded
    // bytes piece by piece (nullptr: the whole-file path), a buffer for one
    // texture's THTX block (it may live outside the heap), a resize for the
    // growing header buffer, and the texture side of the environment.
    virtual ResourceStream* open_stream(const char*){return nullptr;}
    virtual void* allocate_scratch(u32 size){return allocate_bytes(size);}
    virtual void release_scratch(void* bytes){release_bytes(bytes);}
    virtual void* resize_bytes(void*,u32){return nullptr;}
    virtual AnmTextureEnvironment* texture_environment(){return nullptr;}
#endif
};
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// Streamed loads, and loads that took the whole-file path (no stream, or a
// file the stream loader does not take); AnmResources.cpp.
struct AnmStreamStats {u32 streamed,whole,aborted,scratch_peak,two_pass;};
AnmStreamStats anm_stream_stats() noexcept;
void anm_stream_force_whole(bool whole) noexcept;   // the PC audit (native/main.cpp, TH10_ANM_CHECK) loads each file both ways
void anm_stream_fault_after(u32 textures) noexcept;  // the audit: abort a stream after that many textures (~0u: never)
#endif
}
