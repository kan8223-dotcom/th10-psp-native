#pragma once
#include "Textures.hpp"
#include "FileSystem.hpp"
#include "../game/AnmResources.hpp"
#include <vector>
#include <string>
namespace th10::browser {
struct AnimationResources final:AnmResourceEnvironment,AnmTextureEnvironment {
    FileSystem& files;GraphicsDevice& device;AnmManager& manager;u32 loading=0,display=0;Textures textures;
    i32 last_error=-1;
    AnimationResources(FileSystem& files,GraphicsDevice& device,AnmManager& manager,u32 display_flags=0);
    ~AnimationResources();
    AnmFile* allocate_file() override;
    void* allocate_bytes(u32 size) override;
    void release_file(AnmFile* file) override;
    void release_bytes(void* bytes) override;
    u8* read_file(const char* name,bool external,u32* size) override;
    void release_texture(void* texture) override;
    void report(AnmResourceError error) override {last_error=static_cast<i32>(error);}
    i32 materialize(AnmFile& file,i32 texture,i32 sprite,i32 script,const AnmChunk* chunk) override;
    void wait_for_loading(AnmManager& manager,AnmFile& file) override;
    i32 create_empty(AnmTexture& texture,i32 width,i32 height,i32 format) override {return texture.create_empty(width,height,format,textures);}
    i32 create_encoded(AnmTexture& texture,i32 width,i32 height,i32 format,u32 key) override {return texture.create_encoded(width,height,format,key,textures);}
    i32 create_embedded(AnmTexture& texture,const u8* data,i32 width,i32 height,i32 format) override {return texture.create_embedded(data,width,height,format,textures);}
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    i32 create_embedded_padded(AnmTexture& texture,const u8* data,i32 width,i32 height,i32 format) override {return texture.create_embedded_padded(data,width,height,format,textures);}
#endif
    void set_priority(void* texture,u32 priority) override;
    void preload(void* texture) override;
    AnmTextureDimensions dimensions(void* texture) override;
    void texture_error(AnmResourceError error,const char*) override {report(error);}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    ResourceStream* open_stream(const char* name) override;
    void* allocate_scratch(u32 size) override;
    void release_scratch(void* bytes) override;
    void* resize_bytes(void* bytes,u32 size) override;
    AnmTextureEnvironment* texture_environment() override {return this;}
    u32 embedded_index(i32 format,const u8* header,bool binary) override {return AnmTexture::embedded_index(format,header,binary,textures);}
    i32 convert_embedded_rows(u8* output,u32 index,const u8* source,const u8* header,u32 rows) override {return AnmTexture::convert_embedded_rows(output,index,source,header,rows,textures);}
    i32 create_embedded_as(AnmTexture& texture,const u8* data,i32 width,i32 height,u32 index) override {return texture.create_embedded_as(data,width,height,index,textures);}
#endif
#ifdef TH_NATIVE_PLATFORM
    struct PreparedTexture {TextureDescription description;std::vector<u8> pixels;u32 pitch=0,bytes_per_pixel=0,revision=0;void* resident=nullptr;};
    struct PreparedAnimation {std::string name;u32 display=0;std::vector<u8> raw;std::vector<AnmSprite> sprites;std::vector<u32> sprite_textures,script_offsets;std::vector<PreparedTexture> textures;};
    std::vector<PreparedAnimation> prepared;u32 last_read_size=0,prepared_bytes=0,cache_hits=0,resident_hits=0;
    bool preload_transition(u32 index);
    AnmFile* prepared_file(i32,const char*)override;
#endif
};
}
