#include "AnimationResources.hpp"
#include <cstdlib>
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
#ifdef TH_NATIVE_PLATFORM
extern "C" void th10_note_alloc(th10::u32 bytes,const void* result);
#endif
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
#include <cstddef>
extern "C" void* th10_volatile_alloc(std::size_t bytes);extern "C" int th10_volatile_free(void* p);   // native/psp/VolatileArena.hpp
#endif
#endif
namespace th10::browser {
namespace {u32 address(const void* value){return static_cast<u32>(reinterpret_cast<uintptr_t>(value));}}
AnimationResources::AnimationResources(FileSystem& f,GraphicsDevice& d,AnmManager& m,u32 flags):files(f),device(d),manager(m),display(flags),textures(d,display){registry=&m.registry;loader_flags=&loading;}
AnimationResources::~AnimationResources(){
#ifdef TH_NATIVE_PLATFORM
    for(const auto& entry:prepared)for(const auto& texture:entry.textures)if(texture.resident)device.release_resource(texture.resident);
#endif
}
AnmFile* AnimationResources::allocate_file(){return static_cast<AnmFile*>(std::malloc(sizeof(AnmFile)));}
void* AnimationResources::allocate_bytes(u32 size){return std::malloc(size);}
void AnimationResources::release_file(AnmFile* file){std::free(file);}
void AnimationResources::release_bytes(void* bytes){std::free(bytes);}
u8* AnimationResources::read_file(const char* name,bool external,u32* size){
#ifdef TH_NATIVE_PLATFORM
    u32 n=0;auto* data=ResourceFiles{files}.load(name,&n,external);if(size)*size=n;if(!external)last_read_size=n;return data;
#else
    return ResourceFiles{files}.load(name,size,external);
#endif
}
void AnimationResources::release_texture(void* texture){device.release_resource(texture);}
i32 AnimationResources::materialize(AnmFile& file,i32 texture,i32 sprite,i32 script,const AnmChunk* chunk){return file.materialize(texture,sprite,script,chunk,*this);}
void AnimationResources::wait_for_loading(AnmManager& owner,AnmFile&){owner.process_loading(*this);}
void AnimationResources::set_priority(void* texture,u32 priority){device.resource_priority(texture,priority);}
void AnimationResources::preload(void* texture){device.preload_resource(texture);}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// th10_port (TH10_ANM_STREAM_LOAD): the archive name as ResourceFiles::load
// looks it up (path stripped), decoded piece by piece by the archive.
ResourceStream* AnimationResources::open_stream(const char* name){
    const char* last_backslash=std::strrchr(name,'\\');const char* slash=std::strrchr(last_backslash?last_backslash+1:name,'/');if(slash)name=slash+1;
    return files.open_stream(name);
}
// One texture's THTX block at a time (1 MiB at most): the 1000's volatile
// partition when it has room, the heap otherwise.
void* AnimationResources::allocate_scratch(u32 size){
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    if(void* bytes=th10_volatile_alloc(size))return bytes;
#endif
    void* bytes=std::malloc(size);
#ifdef TH_NATIVE_PLATFORM
    th10_note_alloc(size,bytes);
#endif
    return bytes;
}
void AnimationResources::release_scratch(void* bytes){
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    if(th10_volatile_free(bytes))return;
#endif
    std::free(bytes);
}
void* AnimationResources::resize_bytes(void* bytes,u32 size){void* resized=std::realloc(bytes,size);
#ifdef TH_NATIVE_PLATFORM
    th10_note_alloc(size,resized);
#endif
    return resized;}
#endif
AnmTextureDimensions AnimationResources::dimensions(void* texture){void* surface=textures.get_surface(texture);const auto desc=textures.describe_surface(surface);textures.release_surface(surface);return {desc.width,desc.height};}
}
