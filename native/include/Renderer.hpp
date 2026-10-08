#pragma once
// Headless stand-in for portable/sdl/Renderer.hpp. It keeps the public
// interface used by th10_web/cpp/sdl/GraphicsHost.cpp, but draws nothing:
// every draw call is folded into a per-frame hash so native builds can be
// compared frame by frame without a GPU. Put native/include before
// portable/sdl on the include path so this header shadows the GLES one.
#include "GraphicsState.hpp"
#include <malloc.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <new>
#include <string>
#include <vector>

extern "C" void th10_note_alloc(uint32_t bytes,const void* result);
namespace touhou::sdl {
using u32=uint32_t;using i32=int32_t;using u8=uint8_t;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
using u16=uint16_t;
#endif
using namespace touhou::graphics;
// Surface bytes (sdl/GraphicsHost.cpp Resource::bytes). The PSP GE samples
// textures in place, so the base is cache-line aligned (GE needs 16 bytes;
// 64 keeps each surface on its own dcache lines for writeback).
template<class T> struct SurfaceAllocator {
    using value_type=T;
    SurfaceAllocator()=default;template<class U> SurfaceAllocator(const SurfaceAllocator<U>&){}
    T* allocate(size_t n){void* p=memalign(64,n*sizeof(T));th10_note_alloc(u32(n*sizeof(T)),p);
        if(!p){if(auto handler=std::get_new_handler())handler();std::abort();}return static_cast<T*>(p);}
    void deallocate(T* p,size_t){std::free(p);}
    template<class U> bool operator==(const SurfaceAllocator<U>&)const{return true;}
    template<class U> bool operator!=(const SurfaceAllocator<U>&)const{return false;}
};
#define TH10_SURFACE_ALLOCATOR touhou::sdl::SurfaceAllocator<touhou::sdl::u8>
struct Surface {u32 handle=0,width=0,height=0;PixelFormat format=PixelFormat::Bgra8;u32 pitch=0;u8* data=nullptr;u32 size=0,version=0;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    // th10_port (TH10_TEXTURE_CLUT8): a texture of 256 colours or fewer is kept as
    // 8-bit indices (data, pitch = width) into this 256-entry table of `format`
    // values; the game still sees the 16-bit format (sdl/GraphicsHost.cpp).
    u16* palette=nullptr;
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    // th10_port (TH10_TEXTURE_LAZY_EMPTY): an '@' texture nothing has written
    // yet: every pixel is 0 and there are no bytes of its own (data/palette
    // point at shared zeros); the host allocates them on the first write.
    bool uniform=false;
#endif
};
struct Viewport {u32 x=0,y=0,width=640,height=480;float min=0,max=1;};
struct Statistics {u32 calls=0,batches=0,uploadBytes=0,readBytes=0,frames=0,presentations=0,bufferReplacements=0,bufferSubUpdates=0,vertexUploadBytes=0,directBytes=0,copiedBytes=0,layoutSetups=0,textureBinds=0,framebufferBinds=0,programCompiles=0,genericBatches=0,resamples=0;};
struct State {
    PipelineState pipeline{};
    std::array<std::array<float,16>,4> matrix{};
    Viewport viewport{};u32 texture=0,target=0,depth=0,stride=0;VertexAttributes layout{};
};
class Renderer : public StateCommands {
public:
    using Resolve=Surface(*)(void*,u32);
    Statistics stats{};State state{};bool defer=false;
    Renderer(int version,Resolve,void*);~Renderer();
    bool initialize();void flush();void discard();bool commit();
    PipelineState& pipeline() override { return state.pipeline; }
    void transform(MatrixKind,const void*);void viewport(const Viewport&);
    void draw(Topology primitive,u32 count,const void*,u32 stride,const void* indices=nullptr,IndexType indexFormat=IndexType::UInt16);
    void draw_timed(Topology primitive,u32 count,const void*,u32 stride,const void* indices,IndexType indexFormat);   // PSP GE renderer
    void draw_batch(u32 count,const void*,u32 stride);
    void clear(u32 flags,u32 color,float depth,u32 stencil,const i32* rects=nullptr,u32 count=0);
    void copy(u32 source,const i32* rect,u32 target,const i32* point);
    bool resample(u32,const i32*,u32,const i32*,const float*,u32,u32);
    void read(u32);void release(u32);void present(u32);void prepare(u32);
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
    // th10_port: write rows of a rectangle (left, top, right, bottom) that nothing reads back; the caller then moves the surface version.
    bool write_rect(u32 id,const i32* rectangle,const u8* pixels,u32 pitch);
#endif
    const char* error()const{return failure.c_str();}
    int version;Resolve resolve;void* owner;
    // Headless only: hash of everything drawn since the last present, and the
    // hash of the last presented frame.
    uint64_t frame_hash=0,presented_hash=0;u32 frame_draws=0,presented_draws=0;
private:
    void mix(const void*,size_t);
    u32 pending=0;std::string failure;
};
Renderer* current();void set_current(Renderer*);
}

extern "C" {
const touhou::sdl::Statistics* sdl_stats();
void sdl_defer(int);
int sdl_commit();
const char* sdl_error();
int sdl_version();
double sdl_ticks();
}
