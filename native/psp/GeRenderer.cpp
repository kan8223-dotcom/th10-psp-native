// PSP GE renderer (native/psp_ge build): the semantic commands of the SDL
// renderer drawn with sceGu display lists. No PSPGL, SDL or GL layer.
// Conventions follow the TH08 PSP port's hardware-accepted native_ge.cpp:
// RGB565 framebuffers with stride 512, reversed 16-bit depth (near = 65535),
// pretransformed (screen) vertices in GE through mode scaled from the
// 640x480 back buffer to 480x272, D3D matrices used as they are.
// Textures are sampled in place from the game's surface bytes: the R/B swap
// between D3D and GE channel order is its own inverse, so a surface is
// swapped when drawn and swapped back before the game reads it.
#include "Renderer.hpp"
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifndef TH10_GUARD_CLIP
#define TH10_GUARD_CLIP 0
#endif
#if TH10_GUARD_CLIP
#include "GeGuardClip.hpp"
#endif
#if TH10_REC
#include "Recorder.hpp"
#endif

namespace touhou::sdl {
namespace {
constexpr u32 frame_bytes=512*272*2,depth_offset=2*frame_bytes;
#ifdef TH10_LIST_KB
// PSP-1000 lane: smaller lists. A frame that fills one runs it and opens the
// other mid-frame (reserve: restarts), so a short list costs a wait, never a
// wrong picture; list_peak says how full the largest list got.
constexpr u32 list_bytes=TH10_LIST_KB*1024u;
u32 list_peak=0;
#define TH10_NOTE_LIST() do{const u32 used_=u32(sceGuCheckList());if(used_>list_peak)list_peak=used_;}while(0)
#else
constexpr u32 list_bytes=512*1024;
#define TH10_NOTE_LIST() do{}while(0)
#endif
alignas(64) u32 lists[2][list_bytes/4];
struct GeVertex {float u,v;u32 color;float x,y,z;};
static_assert(sizeof(GeVertex)==24,"GE vertex layout");
// list: serial of the last list that used the texture; target: the FINISH
// count after which that list has run (settle waits for it).
struct GeTexture {u32 version=~0u,list=~0u,target=0;bool swapped=false,swizzled=false;int psm=GU_PSM_8888;u32 tbw=0,log_w=0,log_h=0,bytes=0;
    u32 vram=0,vram_version=~0u,last_frame=0;   // vram: offset of the VRAM copy (0 = none)
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    u32 vram_fail_gen=~0u,vram_fail_frame=0;   // vram_alloc found no room: tried again once VRAM is freed or 30 frames later
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    int cpsm=-1;   // a CLUT8 texture (GU_PSM_T8): the format of its 256-entry table, loaded when it is bound
#endif
};
std::map<u32,GeTexture> textures;
Renderer* active=nullptr;bool hashing=true;
constexpr uint64_t fnv_offset=1469598103934665603ull,fnv_prime=1099511628211ull;
u32 list_index=0,list_serial=1;int draw_frame=1;bool list_open=false;   // draw_frame -1: picked by the next list
u32 back_id=0,back_width=640,back_height=480;
// Applied GE state; `valid=false` resends everything.
struct Applied {
    bool valid=false,through=false,blend=false,alpha=false,depth=false,depth_write=true,fog=false,textured=false,filter_linear_min=true,filter_linear_mag=true;
    int blend_src=-1,blend_dst=-1;u32 blend_fix_src=0,blend_fix_dst=0;int alpha_func=-1;u32 alpha_ref=0;int depth_func=-1;
    u32 fog_color=0;float fog_near=0,fog_far=0;int wrap_u=-1,wrap_v=-1,tfx=-1;u32 texture=0,texture_version=~0u;
    Viewport viewport{};bool has_viewport=false;float matrix[3][16]{};bool has_matrix[3]{};
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    u32 matrix_gen[3]{};   // view_gen / projection_gen when that matrix was last checked
#endif
} applied;
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
// th10_port (TH10_RENDER_FAST_GE_STATE, PSP-1000 lane): per-draw state work
// that does not change what the GE gets. Generations count every write of the
// view and projection matrices and of the viewport (Renderer::transform and
// ::viewport are their only writers), so a world draw stops comparing 128+
// bytes of camera that did not move (apply, guard_draw). vram_free_gen counts
// every release of VRAM space; a texture whose vram_alloc failed waits for it
// (or 30 frames, the eviction age) instead of scanning every texture again on
// each bind: the stage 4 waterfall on the 1000 failed 47 times a frame.
u32 view_gen=1,projection_gen=1,viewport_gen=1,vram_free_gen=0;
#endif
u32 unsupported_combiner=0,readbacks=0,copies=0,restarts=0,conversions=0,swizzles=0,vram_uploads=0,vram_evictions=0,vram_refreshes=0;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
u32 clut_textures=0,clut_loads=0;   // CLUT8 textures first bound, palette loads (one per bind of one)
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
u32 uniform_textures=0;   // '@' textures bound while still without bytes (sampled as 0)
#endif
// Per presented frame: GE wait at the end of the frame and CPU time inside
// draw()/clear() (1 ms histogram buckets), for the device result file.
u64 frame_cpu_us=0,frame_ge_us=0,sum_cpu_us=0,sum_ge_us=0;u32 frames=0,hist_cpu[128]{},hist_ge[128]{};u32 frame_counter=0;
u64 now_us(){return sceKernelGetSystemTimeWide();}
// Diagnostics (--ge-debug): 1 no depth test, 2 no fog, 4 count 3D vertices
// the GE drops (behind the eye or outside the +-2048 pixel guard band).
u32 debug_flags=0,guard_draws=0,guard_vertices=0,behind_vertices=0,world_draws=0,z_below=0,z_above=0,z_inside=0;
float z_min=1e30f,z_max=-1e30f,w_min=1e30f,w_max=-1e30f;

void* vram(u32 offset){return reinterpret_cast<void*>(0x04000000u+offset);}
u32 rgba(u32 argb){return (argb&0xff00ff00u)|((argb&255u)<<16)|((argb>>16)&255u);}
u32 log2_ceil(u32 v){u32 l=0;while((1u<<l)<v)++l;return l;}
void enable(int cap,bool on){if(on)sceGuEnable(cap);else sceGuDisable(cap);}

// ---- present pipeline (TH08 native_ge.cpp, sha256 e2de6183: frame[3],
// Poll/WaitDisplay/PickDrawBuffer) ----
// Present closes the frame's list without waiting for the GE. The frame waits
// in the queue until the GE has run its list's FINISH (the callback counts
// them; lists complete in order), then its flip is requested (NEXTFRAME, main
// thread, one per VBlank) and it leaves the queue once the display shows it
// (sceDisplayGetFrameBuf IMMEDIATE, never the vcount: PV). The next frame is
// drawn into the image neither shown nor queued, so drawing does not wait
// for a VBlank while the GE keeps up. TH08 puts the third image in the GE4
// upper 2 MiB; here it follows the depth buffer: the CPU can clear it, no
// upper scanout is needed and PPSSPP (no GE4) runs the same three images.
// The texture area shrinks by one image either way. TH10_GE_FRAMES=2 keeps
// two images (the same queue with depth 1, TH08's accepted pending-1 path).
#ifndef TH10_GE_FRAMES
#define TH10_GE_FRAMES 3
#endif
static_assert(TH10_GE_FRAMES==2||TH10_GE_FRAMES==3,"two or three images");
constexpr int frame_count=TH10_GE_FRAMES;
constexpr u32 frame_offset[3]{0,frame_bytes,3*frame_bytes};
volatile u32 ge_finished=0;   // FINISH commands the GE has executed (interrupt context)
u32 ge_submitted=0;           // lists closed with sceGuFinish
u32 list_target[2]{};         // FINISH count that frees each list memory (TH08 fences[listIndex])
int displayed_frame=0;
struct PendingFrame {int frame;u32 target;bool requested;u64 committed_us,done_us;};
PendingFrame pending_queue[2]{};int pending_count=0;
// One presented frame waits for its flip at a time: TH08's pending depth 1
// (r243-r271, run on the Go with three images). r272's two-deep queue never
// ran on hardware (docs/CHECKLIST.md R.6d), so it stays out.
constexpr int max_pending=1;
// Per presented frame, for the result file: the wait for a free image in
// open_list or at the commit (1 ms histogram), commit -> GE done as seen by a
// poll (1 ms histogram), GE done -> shown; commits that waited for the previous
// flip; waits given up after 2 s (counted, never a hang).
u64 frame_flip_us=0,sum_flip_us=0,sum_tail_us=0,sum_latch_us=0;
#if defined(TH10_WINDOW_BREAKDOWN) && TH10_WINDOW_BREAKDOWN
u32 last_frame_cpu_us=0;   // renderer CPU time of the last presented frame (main.cpp heavy windows)
#endif
u32 hist_flip[128]{},hist_tail[128]{},tails=0,latches=0,commit_waits=0,wait_timeouts=0;
void ge_finish_callback(int){ge_finished=ge_finished+1;}
bool ge_done(u32 target){return i32(ge_finished-target)>=0;}
bool shows(int frame){void* top=nullptr;int width=0,format=0;
    return sceDisplayGetFrameBuf(&top,&width,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)>=0&&(reinterpret_cast<u32>(top)&0x1fffffffu)==0x04000000u+frame_offset[frame];}
void pop_pending(){
    if(pending_queue[0].done_us){sum_latch_us+=now_us()-pending_queue[0].done_us;++latches;}
    displayed_frame=pending_queue[0].frame;pending_queue[0]=pending_queue[1];--pending_count;
}
// TH08 Poll. After a pop the next frame's flip is requested at once.
void poll_present(){
    for(int i=0;i<pending_count;i++){auto& p=pending_queue[i];
        if(!p.done_us&&ge_done(p.target)){p.done_us=now_us();const u64 tail=p.done_us-p.committed_us;sum_tail_us+=tail;++tails;++hist_tail[std::min<u64>(tail/1000,127)];}}
    while(pending_count){
        auto& head=pending_queue[0];
        if(!head.requested){
            if(!head.done_us)return;
            sceDisplaySetFrameBuf(vram(frame_offset[head.frame]),512,GU_PSM_5650,PSP_DISPLAY_SETBUF_NEXTFRAME);head.requested=true;
        }
        if(!shows(head.frame))return;
        pop_pending();
    }
}
// Wait for FINISH number `target`. A GU_DIRECT list of ours may stay open and
// stalled behind it; the count still advances. Never waits on the open list.
void wait_ge(u32 target){
    if(ge_done(target))return;
    const u64 t=now_us();
    while(!ge_done(target)){sceKernelDelayThread(50);if(now_us()-t>2000000){++wait_timeouts;break;}}
    frame_ge_us+=now_us()-t;poll_present();
}
// TH08 WaitDisplay: until the oldest queued frame is shown. NEXTFRAME can
// latch shortly after the VBlank interrupt, so poll 20 x 100 us before waiting
// for another VBlank (TH08 r124 without it lost a whole VBlank per flip).
void wait_display(){
    if(!pending_count)return;
    const int target=pending_count;const u64 start=now_us();
    while(pending_count>=target){
        if(!pending_queue[0].requested)wait_ge(pending_queue[0].target);
        poll_present();
        for(int i=0;i<20&&pending_count>=target;i++){sceKernelDelayThread(100);poll_present();}
        if(pending_count>=target)sceDisplayWaitVblankStart();
        if(pending_count>=target&&now_us()-start>2000000){++wait_timeouts;pop_pending();}
    }
}
// TH08 PickDrawBuffer: an image neither shown nor queued.
bool pick_draw_frame(){
    for(int i=0;i<frame_count;i++){
        bool busy=i==displayed_frame;for(int q=0;q<pending_count;q++)if(pending_queue[q].frame==i)busy=true;
        if(!busy){draw_frame=i;return true;}
    }
    return false;
}

#if TH10_REC
// Recording (psp/Recorder.cpp, SELECT). At the end of a presented frame's
// list: the GE copy of the finished picture into a capture buffer (TH08
// native_ge.cpp ReadFramebuffer, sha256 e2de6183 :803-820: 5650 480x272 to
// Main RAM, stride 480, hardware-proven) when the recorder wants this frame,
// then the red "recording" dot drawn over the HUD's top-right corner. The copy
// comes first and sceGuTexSync holds the dot until the copy has landed, so the
// dot is never in the video. The list's FINISH says the copy is done
// (th10_rec_ge_done). The dot's state changes are re-sent by the next list
// (open_list clears `applied`).
void rec_indicator(){
    if(u32(sceGuCheckList())+1024>list_bytes)return;
    struct V {u32 color;float x,y,z;};
    constexpr int n=16;   // segments
    static const float c16[n+1]={1.f,.92388f,.70711f,.38268f,0.f,-.38268f,-.70711f,-.92388f,-1.f,-.92388f,-.70711f,-.38268f,0.f,.38268f,.70711f,.92388f,1.f};
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_FOG);
    sceGuDepthMask(1);sceGuScissor(0,0,480,272);
    const float cx=466.0f,cy=13.0f;
    const struct {float r;u32 color;} discs[2]={{7.0f,0xffe0e0e0u},{5.8f,0xff1010f0u}};   // ABGR: light rim, then red
    for(const auto& d:discs){
        auto* v=static_cast<V*>(sceGuGetMemory(int(sizeof(V)*(n+2))));
        v[0]={d.color,cx,cy,0.0f};
        for(int i=0;i<=n;i++)v[i+1]={d.color,cx+d.r*c16[i],cy+d.r*c16[(i+3*n/4)%n],0.0f};   // sin(a) = cos(a - 90 deg)
        sceGuDrawArray(GU_TRIANGLE_FAN,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,n+2,nullptr,v);
    }
    applied.valid=false;
}
void rec_present(int frame){
    if(frame<0)return;
    if(void* buffer=th10_rec_capture_want()){
        if(u32(sceGuCheckList())+1024>list_bytes)th10_rec_capture_skipped();
        else{sceGuCopyImage(GU_PSM_5650,0,0,480,272,512,vram(frame_offset[frame]),0,0,480,buffer);sceGuTexSync();
            th10_rec_capture_emitted(ge_submitted+1);}   // this list closes as FINISH number ge_submitted+1
    }
    if(th10_rec_indicator())rec_indicator();
}
#endif
// ---- display lists ----
void open_list(){
    if(list_open)return;
    if(draw_frame<0){   // TH08 drawBufferDeferred: the frame's first list picks its image
        poll_present();const u64 t=now_us();
        while(!pick_draw_frame())wait_display();
        frame_flip_us+=now_us()-t;
    }
    wait_ge(list_target[list_index]);
    sceGuStart(GU_DIRECT,lists[list_index]);
    sceGuDrawBufferList(GU_PSM_5650,reinterpret_cast<void*>(frame_offset[draw_frame]),512);
    list_open=true;applied.valid=false;
}
// Run everything queued so far; the next command opens a fresh list.
void drain(){if(!list_open)return;TH10_NOTE_LIST();sceGuFinish();list_target[list_index]=++ge_submitted;const u64 t=now_us();sceGuSync(0,0);frame_ge_us+=now_us()-t;list_open=false;list_index^=1;++list_serial;poll_present();}
void reserve(u32 bytes){
    open_list();
    if(u32(sceGuCheckList())+bytes+1024>list_bytes){drain();++restarts;open_list();}
}

// ---- textures ----
bool texture_format(PixelFormat f,int& psm,u32& bpp){
    switch(f){
    case PixelFormat::Bgra8:case PixelFormat::Bgrx8:psm=GU_PSM_8888;bpp=4;return true;
    case PixelFormat::Argb4444:psm=GU_PSM_4444;bpp=2;return true;
    case PixelFormat::Rgb565:psm=GU_PSM_5650;bpp=2;return true;
    case PixelFormat::Argb1555:case PixelFormat::Xrgb1555:psm=GU_PSM_5551;bpp=2;return true;
    default:return false;
    }
}
// D3D <-> GE channel order, in place. Swapping R and B is an involution;
// only the undefined X channel of the X formats is forced opaque.
void swap_channels(const Surface& s,bool to_ge){
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    // A CLUT8 texture: its colours are the 256 table entries (16-bit, the
    // surface format); the indices stay as they are. The GE reads both from
    // memory, so the table and the indices (written by the CPU when the
    // texture was made) are written back.
    if(s.palette){
        auto* p=s.palette;
        if(s.format==PixelFormat::Argb4444)for(u32 i=0;i<256;i++){const u32 n=p[i];p[i]=uint16_t((n&0xf0f0u)|((n>>8)&15u)|((n&15u)<<8));}
        else if(s.format==PixelFormat::Rgb565)for(u32 i=0;i<256;i++){const u32 n=p[i];p[i]=uint16_t((n&0x07e0u)|(n>>11)|((n&31u)<<11));}
        else{const u32 fill=to_ge&&s.format==PixelFormat::Xrgb1555?0x8000u:0;for(u32 i=0;i<256;i++){const u32 n=p[i];p[i]=uint16_t((n&0x83e0u)|((n>>10)&31u)|((n&31u)<<10)|fill);}}
        sceKernelDcacheWritebackRange(p,512);sceKernelDcacheWritebackRange(s.data,s.size);return;
    }
#endif
    const u32 pixels=s.size/(s.format==PixelFormat::Bgra8||s.format==PixelFormat::Bgrx8?4:2);
    if(s.format==PixelFormat::Bgra8||s.format==PixelFormat::Bgrx8){
        auto* p=reinterpret_cast<u32*>(s.data);const u32 fill=to_ge&&s.format==PixelFormat::Bgrx8?0xff000000u:0;
        for(u32 i=0;i<pixels;i++){const u32 v=p[i];p[i]=(v&0xff00ff00u)|((v&255u)<<16)|((v>>16)&255u)|fill;}
    }else{
        auto* p=reinterpret_cast<uint16_t*>(s.data);
        if(s.format==PixelFormat::Argb4444)for(u32 i=0;i<pixels;i++){const u32 n=p[i];p[i]=uint16_t((n&0xf0f0u)|((n>>8)&15u)|((n&15u)<<8));}
        else if(s.format==PixelFormat::Rgb565)for(u32 i=0;i<pixels;i++){const u32 n=p[i];p[i]=uint16_t((n&0x07e0u)|(n>>11)|((n&31u)<<11));}
        else{const u32 fill=to_ge&&s.format==PixelFormat::Xrgb1555?0x8000u:0;for(u32 i=0;i<pixels;i++){const u32 n=p[i];p[i]=uint16_t((n&0x83e0u)|((n>>10)&31u)|((n&31u)<<10)|fill);}}
    }
    sceKernelDcacheWritebackRange(s.data,s.size);
}
// GE swizzled layout (TH08 native_ge.cpp TexturePixel): 16-byte x 8-row
// blocks, blocks row-major. Needs whole blocks: row >= 16 bytes, height % 8.
u8* scratch=nullptr;u32 scratch_bytes=0;
bool swizzle_ok(u32 row_bytes,u32 height){return row_bytes>=16&&!(row_bytes&15u)&&height>=8&&!(height&7u);}
// One 8-row band at a time: a band's blocks occupy exactly the band's own
// bytes, so the scratch copy is 8 rows (16 KiB at 512 x 32 bit), not the whole
// surface (it grew to 1 MiB and was never released).
bool swizzle(u8* data,u32 row_bytes,u32 height,bool forward){
    const u32 band=row_bytes*8;
    if(scratch_bytes<band){std::free(scratch);scratch=static_cast<u8*>(memalign(64,band));scratch_bytes=scratch?band:0;if(!scratch)return false;}
    const u32 blocks=row_bytes/16;
    for(u32 by=0;by<height/8;by++){
        u8* base=data+by*band;std::memcpy(scratch,base,band);
        auto* linear=reinterpret_cast<u32*>(forward?scratch:base);auto* packed=reinterpret_cast<u32*>(forward?base:scratch);u32 k=0;
        for(u32 bx=0;bx<blocks;bx++)for(u32 r=0;r<8;r++){
            u32* line=linear+(r*row_bytes+bx*16)/4;u32* block=packed+k;k+=4;
            if(forward){block[0]=line[0];block[1]=line[1];block[2]=line[2];block[3]=line[3];}else{line[0]=block[0];line[1]=block[1];line[2]=block[2];line[3]=block[3];}}
    }
    return true;
}
// VRAM after the three 5650 frames and the depth buffer: 2 MiB, or 4 MiB with
// GE4 (native/psp/Ge4.cpp). Uploads go through the GE (sceGuCopyImage) since
// the upper 2 MiB is GE-only; blocks are whole 2 KiB rows of a 512-wide copy.
extern "C" bool th10_ge4_enable(void);extern "C" void th10_ge4_shutdown(void);extern "C" const char* th10_ge4_state(void);
constexpr u32 vram_begin=(frame_count==3?4:3)*frame_bytes;u32 vram_end=0x200000;
bool vram_alloc(u32 id,GeTexture& t){
    for(int pass=0;pass<2;pass++){
        u32 candidate=vram_begin;
        for(;;){u32 next=candidate;for(auto& e:textures){const auto& o=e.second;if(o.vram&&candidate<o.vram+o.bytes&&candidate+t.bytes>o.vram&&o.vram+o.bytes>next)next=(o.vram+o.bytes+63u)&~63u;}
            if(next==candidate)break;candidate=next;}
        if(candidate+t.bytes<=vram_end){t.vram=candidate;t.vram_version=~0u;return true;}
        // Full: evict textures unused for 30 frames, then try once more.
        bool evicted=false;for(auto& e:textures){auto& o=e.second;if(e.first!=id&&o.vram&&frame_counter-o.last_frame>30){if(o.list==list_serial&&list_open)continue;o.vram=0;o.vram_version=~0u;++vram_evictions;evicted=true;if(applied.texture==e.first)applied.texture=0;}}
        if(!evicted)break;
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
        ++vram_free_gen;
#endif
    }
    return false;
}
// Before the CPU or a conversion touches a surface a list may still sample,
// let the GE finish (TH08 PrepareTextureWrite): the open list is drained, a
// closed one (up to two presented frames) is waited for by its FINISH count.
void settle(const GeTexture& t){if(list_open&&t.list==list_serial)drain();else wait_ge(t.target);}
void to_d3d(u32 id,const Surface& s){
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    if(s.uniform)return;   // shared zeros, never swapped or swizzled; the host gives it bytes before any access
#endif
    auto it=textures.find(id);if(it==textures.end()||!it->second.swapped)return;
    auto& t=it->second;settle(t);
    if(t.swizzled&&swizzle(s.data,s.pitch,s.height,false))t.swizzled=false;
    swap_channels(s,false);t.swapped=false;t.version=~0u;t.vram_version=~0u;
    if(applied.texture==id)applied.texture=0;
}
GeTexture* bind(u32 id,const Surface& s){
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    // An '@' texture nothing has written yet is all 0 (sdl/GraphicsHost.cpp
    // make_lazy_texture): a 16x16 block of index 0 through an all-0 table
    // samples the same 0 at any UV, wrap mode or filter. No VRAM copy.
    if(s.uniform){
        int cpsm;u32 entry;if(!s.data||!s.palette||!texture_format(s.format,cpsm,entry)||entry!=2)return nullptr;
        auto& t=textures[id];t.last_frame=frame_counter;
        if(t.version!=s.version||!t.swapped||t.cpsm!=cpsm){settle(t);sceKernelDcacheWritebackRange(s.data,2048);sceKernelDcacheWritebackRange(s.palette,512);
            t.swapped=true;t.swizzled=false;t.version=s.version;t.psm=GU_PSM_T8;t.tbw=16;t.log_w=4;t.log_h=4;t.bytes=2048;
            if(t.cpsm<0)++uniform_textures;t.cpsm=cpsm;if(applied.texture==id)applied.texture=0;}
        return &t;
    }
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    // A CLUT8 texture samples 1-byte indices (GU_PSM_T8, rows of 16 bytes) through its table.
    int psm,cpsm=-1;u32 bpp;
    if(s.palette){u32 entry;if(!s.data||!s.width||!s.height||s.width>512||s.height>512||!texture_format(s.format,cpsm,entry)||entry!=2||(reinterpret_cast<u32>(s.palette)&15u))return nullptr;psm=GU_PSM_T8;bpp=1;}
    else if(!s.data||!s.width||!s.height||s.width>512||s.height>512||!texture_format(s.format,psm,bpp))return nullptr;
    const u32 tbw=s.pitch/bpp;if(s.pitch%bpp||tbw<s.width||(tbw&(bpp==4?3u:bpp==2?7u:15u))||(reinterpret_cast<u32>(s.data)&15u))return nullptr;
#else
    int psm;u32 bpp;if(!s.data||!s.width||!s.height||s.width>512||s.height>512||!texture_format(s.format,psm,bpp))return nullptr;
    const u32 tbw=s.pitch/bpp;if(s.pitch%bpp||tbw<s.width||(tbw&(bpp==4?3u:7u))||(reinterpret_cast<u32>(s.data)&15u))return nullptr;
#endif
    auto& t=textures[id];
    // A copy in the upper (GE4) 2 MiB left unused for more than a second is
    // uploaded again rather than trusted: the TH07 port measured Slim eDRAM
    // outside Sony's default range losing bits after ~2.2 s without use.
    if(t.vram>=0x200000u&&t.vram_version==t.version&&frame_counter-t.last_frame>30){t.vram_version=~0u;++vram_refreshes;}
    t.last_frame=frame_counter;
    if(t.version!=s.version||!t.swapped){
        settle(t);
        if(!t.swapped){swap_channels(s,true);t.swapped=true;++conversions;
            if(swizzle_ok(s.pitch,s.height)&&swizzle(s.data,s.pitch,s.height,true)){t.swizzled=true;++swizzles;sceKernelDcacheWritebackRange(s.data,s.size);}}
        t.version=s.version;t.psm=psm;t.tbw=tbw;t.log_w=log2_ceil(s.width);t.log_h=log2_ceil(s.height);t.bytes=(s.pitch*s.height+2047u)&~2047u;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
        if(cpsm>=0&&t.cpsm<0)++clut_textures;
        t.cpsm=cpsm;
#endif
        if(applied.texture==id)applied.texture=0;
    }
    // Hot textures live in VRAM: the GE reads them without the main bus.
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    bool in_vram=t.vram!=0;
    if(t.vram_version!=t.version&&!in_vram&&(t.vram_fail_gen!=vram_free_gen||frame_counter-t.vram_fail_frame>30)){
        in_vram=vram_alloc(id,t);if(!in_vram){t.vram_fail_gen=vram_free_gen;t.vram_fail_frame=frame_counter;}}
    if(t.vram_version!=t.version&&in_vram){
#else
    if(t.vram_version!=t.version&&(t.vram||vram_alloc(id,t))){
#endif
        settle(t);reserve(256);   // the copy runs in the list, before the draw that samples it
        const u32 width=t.bytes>=2048?512:t.bytes/4,height=t.bytes/(width*4);
        sceGuCopyImage(GU_PSM_8888,0,0,int(width),int(height),int(width),s.data,0,0,int(width),reinterpret_cast<void*>(0x04000000u+t.vram));
        sceGuTexSync();t.vram_version=t.version;t.list=list_serial;t.target=ge_submitted+1;++vram_uploads;
        if(applied.texture==id)applied.texture=0;
    }
    return &t;
}

// ---- state ----
int blend_factor(BlendFactor f,bool source,u32& fix){
    fix=0;
    switch(f){
    case BlendFactor::One:fix=0xffffff;return GU_FIX;
    case BlendFactor::SourceAlpha:return GU_SRC_ALPHA;
    case BlendFactor::InverseSourceAlpha:return GU_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:return GU_DST_ALPHA;
    case BlendFactor::InverseDestinationAlpha:return GU_ONE_MINUS_DST_ALPHA;
    // GE's "other colour" slot: destination colour as source factor, source colour as destination factor.
    case BlendFactor::SourceColor:return source?GU_FIX:GU_SRC_COLOR;
    case BlendFactor::InverseSourceColor:return source?GU_FIX:GU_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DestinationColor:return source?GU_DST_COLOR:GU_FIX;
    case BlendFactor::InverseDestinationColor:return source?GU_ONE_MINUS_DST_COLOR:GU_FIX;
    default:return GU_FIX;   // Zero (and unsupported: saturate)
    }
}
int compare(Compare c,bool reversed){
    static const int direct[]{GU_NEVER,GU_LESS,GU_EQUAL,GU_LEQUAL,GU_GREATER,GU_NOTEQUAL,GU_GEQUAL,GU_ALWAYS};
    static const int flipped[]{GU_NEVER,GU_GREATER,GU_EQUAL,GU_GEQUAL,GU_LESS,GU_NOTEQUAL,GU_LEQUAL,GU_ALWAYS};
    const u32 i=u32(c);return i<8?(reversed?flipped:direct)[i]:GU_ALWAYS;
}
// What the texture stage computes, reduced to what GE can do.
struct Combine {bool texture=false,replace=false,diffuse=true;u32 constant=0xffffffffu;};
Combine combine(const PipelineState& p,bool has_texture){
    Combine c;
    if(!has_texture||p.color.operation==ColorOperation::Disabled)return c;
    auto plain=[](const Argument& a){return !a.complement&&!a.alphaOnly;};
    const auto& first=p.color.first;const auto& second=p.color.second;
    auto single=[&](const Argument& a){
        if(a.source==ArgumentSource::Texture){c.texture=true;c.replace=true;c.diffuse=false;}
        else if(a.source==ArgumentSource::Factor){c.diffuse=false;c.constant=p.textureFactor;}
    };
    if(p.color.operation==ColorOperation::First&&plain(first))single(first);
    else if(p.color.operation==ColorOperation::Second&&plain(second))single(second);
    else if(p.color.operation==ColorOperation::Multiply&&plain(first)&&plain(second)&&
        (first.source==ArgumentSource::Texture)!=(second.source==ArgumentSource::Texture)){
        const auto other=first.source==ArgumentSource::Texture?second.source:first.source;
        c.texture=true;if(other==ArgumentSource::Factor){c.diffuse=false;c.constant=p.textureFactor;}
    }else{++unsupported_combiner;c.texture=true;}
    return c;
}
void apply_viewport(const Viewport& v,bool through){
    const float sx=480.0f/back_width,sy=272.0f/back_height;
    const int left=int(std::floor(v.x*sx)),top=int(std::floor(v.y*sy)),right=int(std::ceil((v.x+v.width)*sx)),bottom=int(std::ceil((v.y+v.height)*sy));
    sceGuScissor(left,top,right-left,bottom-top);
    if(!through){
        // D3D viewport, depth reversed: z_final = min + (z+1)/2 (max-min), GE depth 65535 (1 - z_final).
        sceGuSendCommandf(0x42,v.width*sx*0.5f);sceGuSendCommandf(0x43,-float(v.height)*sy*0.5f);   // height is u32: negate as float
        sceGuSendCommandf(0x45,2048-240+(v.x+v.width*0.5f)*sx);sceGuSendCommandf(0x46,2048-136+(v.y+v.height*0.5f)*sy);
        const float range=v.max-v.min;
        sceGuSendCommandf(0x44,-32767.5f*range);sceGuSendCommandf(0x47,65535.0f*(1.0f-v.min)-32767.5f*range);
    }
}
void apply(const State& st,bool through,const Combine& c,const GeTexture* t,u32 texture_id,const Surface* surface){
    const auto& p=st.pipeline;auto& a=applied;const bool all=!a.valid;
    if(all||a.blend!=p.blend){enable(GU_BLEND,p.blend);a.blend=p.blend;}
    if(p.blend){u32 fs,fd;const int s=blend_factor(p.sourceBlend,true,fs),d=blend_factor(p.destinationBlend,false,fd);
        if(all||s!=a.blend_src||d!=a.blend_dst||fs!=a.blend_fix_src||fd!=a.blend_fix_dst){
            static const int ops[]{GU_ADD,GU_SUBTRACT,GU_REVERSE_SUBTRACT,GU_MIN,GU_MAX};
            sceGuBlendFunc(ops[u32(p.blendEquation)<5?u32(p.blendEquation):0],s,d,fs,fd);a.blend_src=s;a.blend_dst=d;a.blend_fix_src=fs;a.blend_fix_dst=fd;}}
    if(all||a.alpha!=p.alphaTest){enable(GU_ALPHA_TEST,p.alphaTest);a.alpha=p.alphaTest;}
    if(p.alphaTest){const int f=compare(p.alphaCompare,false);if(all||f!=a.alpha_func||p.alphaReference!=a.alpha_ref){sceGuAlphaFunc(f,int(p.alphaReference&255u),255);a.alpha_func=f;a.alpha_ref=p.alphaReference;}}
    const bool depth=p.depthTest&&st.depth!=0&&!(debug_flags&1);
    if(all||a.depth!=depth){enable(GU_DEPTH_TEST,depth);a.depth=depth;}
    if(depth){const int f=compare(p.depthCompare,true);if(all||f!=a.depth_func){sceGuDepthFunc(f);a.depth_func=f;}}
    if(all||a.depth_write!=p.depthWrite){sceGuDepthMask(p.depthWrite?0:1);a.depth_write=p.depthWrite;}
    // Screen vertices take fog from specular alpha in the SDL shader; TH10
    // never supplies it, so they are unfogged.
    const bool fog=p.fog&&!through&&p.fogMode==FogMode::Linear&&p.fogFar!=p.fogNear&&!(debug_flags&2);
    if(all||a.fog!=fog){enable(GU_FOG,fog);a.fog=fog;}
    if(fog&&(all||p.fogColor!=a.fog_color||p.fogNear!=a.fog_near||p.fogFar!=a.fog_far)){sceGuFog(-p.fogNear,-p.fogFar,rgba(p.fogColor)&0xffffffu);a.fog_color=p.fogColor;a.fog_near=p.fogNear;a.fog_far=p.fogFar;}
    if(all||a.through!=through||!a.has_viewport||std::memcmp(&a.viewport,&st.viewport,sizeof(Viewport))){apply_viewport(st.viewport,through);a.viewport=st.viewport;a.has_viewport=true;a.through=through;}
    if(!through){
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
        // The same uploads in the same order; a view or projection not written
        // since its last check is equal to what the GE has (the world always moves).
        const u32 gens[3]{0,view_gen,projection_gen};float projection[16];
        const bool skip[3]{false,!all&&a.has_matrix[1]&&a.matrix_gen[1]==gens[1],!all&&a.has_matrix[2]&&a.matrix_gen[2]==gens[2]};
        if(!skip[2]){std::memcpy(projection,st.matrix[2].data(),64);
            for(int r=0;r<4;r++)projection[r*4+2]=projection[r*4+2]*2.0f-projection[r*4+3];}   // D3D clip z [0,w] -> [-w,w]
        const float* m[3]{st.matrix[0].data(),st.matrix[1].data(),projection};const int kinds[3]{GU_MODEL,GU_VIEW,GU_PROJECTION};
        for(int i=0;i<3;i++){if(skip[i])continue;a.matrix_gen[i]=gens[i];if(all||!a.has_matrix[i]||std::memcmp(a.matrix[i],m[i],64)){
#else
        float projection[16];std::memcpy(projection,st.matrix[2].data(),64);
        for(int r=0;r<4;r++)projection[r*4+2]=projection[r*4+2]*2.0f-projection[r*4+3];   // D3D clip z [0,w] -> [-w,w]
        const float* m[3]{st.matrix[0].data(),st.matrix[1].data(),projection};const int kinds[3]{GU_MODEL,GU_VIEW,GU_PROJECTION};
        for(int i=0;i<3;i++)if(all||!a.has_matrix[i]||std::memcmp(a.matrix[i],m[i],64)){
#endif
            sceGuSetMatrix(kinds[i],reinterpret_cast<const ScePspFMatrix4*>(m[i]));std::memcpy(a.matrix[i],m[i],64);a.has_matrix[i]=true;}
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
        }   // the generation loop's body
#endif
    }
    const bool textured=c.texture&&t;
    if(all||a.textured!=textured){enable(GU_TEXTURE_2D,textured);a.textured=textured;}
    if(textured){
        if(all||a.texture!=texture_id||a.texture_version!=t->version){
            const void* pixels=t->vram&&t->vram_version==t->version?reinterpret_cast<const void*>(0x04000000u+t->vram):surface->data;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
            // The whole table, 256 16-bit entries = 16 blocks of 32 bytes (TH08
            // ge_draw_direct: CLUT address, mode, then the load with the texture).
            if(t->cpsm>=0){sceGuClutMode(t->cpsm,0,0xff,0);sceGuClutLoad(16,surface->palette);++clut_loads;}
#endif
            sceGuTexMode(t->psm,0,0,t->swizzled?1:0);sceGuTexImage(0,1<<t->log_w,1<<t->log_h,int(t->tbw),pixels);
            sceGuTexScale(float(surface->width)/float(1u<<t->log_w),float(surface->height)/float(1u<<t->log_h));sceGuTexOffset(0,0);
            a.texture=texture_id;a.texture_version=t->version;}
        const int tfx=c.replace?GU_TFX_REPLACE:GU_TFX_MODULATE;if(all||tfx!=a.tfx){sceGuTexFunc(tfx,GU_TCC_RGBA);a.tfx=tfx;}
        const bool lmin=p.minFilter==Filter::Linear,lmag=p.magFilter==Filter::Linear;
        if(all||lmin!=a.filter_linear_min||lmag!=a.filter_linear_mag){sceGuTexFilter(lmin?GU_LINEAR:GU_NEAREST,lmag?GU_LINEAR:GU_NEAREST);a.filter_linear_min=lmin;a.filter_linear_mag=lmag;}
        const int wu=p.addressU==Address::Clamp?GU_CLAMP:GU_REPEAT,wv=p.addressV==Address::Clamp?GU_CLAMP:GU_REPEAT;
        if(all||wu!=a.wrap_u||wv!=a.wrap_v){sceGuTexWrap(wu,wv);a.wrap_u=wu;a.wrap_v=wv;}
    }
    a.valid=true;
}
u32 read32(const u8* p){u32 v;std::memcpy(&v,p,4);return v;}
float readf(const u8* p){float v;std::memcpy(&v,p,4);return v;}
#if TH10_GUARD_CLIP
// World draws with a vertex the GE would cull the triangle for (outside its
// guard band) are clipped on the CPU (GeGuardClip.hpp): the floors of stages
// 5 and 6, the waterfall of stage 4.
constexpr u32 guard_capacity=2048;   // vertices in and out; a bigger draw goes as it is
th10_guard::Vertex guard_out[guard_capacity];unsigned char guard_codes[guard_capacity];
u32 guard_clip_draws=0,guard_clip_triangles=0,guard_drop_triangles=0,guard_overflows=0;
// Camera planes (GeGuardClip.hpp camera_planes), rebuilt only when view,
// projection, viewport or back buffer change; a draw folds in its world.
struct GuardCamera {float view[16],projection[16],vp[16];Viewport viewport;u32 width=0,height=0;bool valid=false;th10_guard::CameraPlanes planes;
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    u32 gens[3]{};   // view_gen, projection_gen, viewport_gen of the planes
#endif
} guard_camera;
// Called by draw_timed for a world draw after its vertices are converted into
// the list (out, GeVertex = th10_guard::Vertex) and the state is applied, so
// draw_timed itself (hot layout, psp_hot2.ld) keeps its shape. When a vertex
// trips: draws the clipped triangle list itself and returns true. False: the
// draw goes as it is (nothing trips, or the clipped list does not fit).
__attribute__((noinline)) bool guard_draw(const State& state,Topology primitive,u32 n,const GeVertex* out){
    static_assert(sizeof(GeVertex)==sizeof(th10_guard::Vertex),"same GE vertex");
    if(u32(primitive)<u32(Topology::Triangles)||n<3||n>guard_capacity)return false;
    auto& g=guard_camera;const auto& v=state.viewport;const float* view=state.matrix[1].data();const float* projection=state.matrix[2].data();
    const float sx=480.0f/back_width,sy=272.0f/back_height;   // apply_viewport's numbers
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    if(!g.valid||g.width!=back_width||g.height!=back_height||g.gens[0]!=view_gen||g.gens[1]!=projection_gen||g.gens[2]!=viewport_gen){
        g.gens[0]=view_gen;g.gens[1]=projection_gen;g.gens[2]=viewport_gen;
#else
    if(!g.valid||g.width!=back_width||g.height!=back_height||std::memcmp(g.view,view,64)||std::memcmp(g.projection,projection,64)||std::memcmp(&g.viewport,&v,sizeof(Viewport))){
#endif
        float pr[16];std::memcpy(pr,projection,64);for(int r=0;r<4;r++)pr[r*4+2]=pr[r*4+2]*2.0f-pr[r*4+3];   // apply(): D3D clip z [0,w] -> [-w,w]
        for(int r=0;r<4;r++)for(int k=0;k<4;k++){float x=0;for(int j=0;j<4;j++)x+=view[r*4+j]*pr[j*4+k];g.vp[r*4+k]=x;}
        g.planes=th10_guard::camera_planes(g.vp,v.width*sx*0.5f,-float(v.height)*sy*0.5f,2048-240+(v.x+v.width*0.5f)*sx,2048-136+(v.y+v.height*0.5f)*sy);
        std::memcpy(g.view,view,64);std::memcpy(g.projection,projection,64);g.viewport=v;g.width=back_width;g.height=back_height;g.valid=true;
    }
    const th10_guard::Planes q=th10_guard::world_planes(state.matrix[0].data(),g.planes);
    bool trip=false;
    for(u32 i=0;i<n;i++){const u32 code=th10_guard::outcode(q,out[i].x,out[i].y,out[i].z);guard_codes[i]=static_cast<unsigned char>(code);
        if(th10_guard::trips(q,code,out[i].x,out[i].y,out[i].z))trip=true;}
    if(!trip)return false;
    const auto* in=reinterpret_cast<const th10_guard::Vertex*>(out);
    const u32 triangles=primitive==Topology::Triangles?n/3:n-2;u32 count=0;
    for(u32 k=0;k<triangles;k++){
        u32 a,b,d;   // strip: odd triangles swap their first two vertices (same facing as the GE's strip)
        if(primitive==Topology::Triangles){a=3*k;b=a+1;d=a+2;}else if(primitive==Topology::Strip){a=k+(k&1u);b=k+1-(k&1u);d=k+2;}else{a=0;b=k+1;d=k+2;}
        if(count+th10_guard::max_triangle_out>guard_capacity){++guard_overflows;return false;}
        const u32 ca=guard_codes[a],cb=guard_codes[b],cd=guard_codes[d];
        const u32 written=th10_guard::clip_triangle(q,in[a],in[b],in[d],ca,cb,cd,guard_out+count);
        if(ca|cb|cd){++guard_clip_triangles;if(!written)++guard_drop_triangles;}
        count+=written;
    }
    // draw_timed reserved n vertices plus 1280 bytes; a bigger list must not
    // drain here (the state is in this list), so it goes as it is.
    if(u32(sceGuCheckList())+count*sizeof(GeVertex)+256>list_bytes){++guard_overflows;return false;}
    ++guard_clip_draws;
    if(debug_flags&4){   // the --ge-debug 4 count on what the GE now gets
        float m[16];const float* world=state.matrix[0].data();
        for(int r=0;r<4;r++)for(int k=0;k<4;k++){float x=0;for(int j=0;j<4;j++)x+=world[r*4+j]*g.vp[j*4+k];m[r*4+k]=x;}
        bool any=false;++world_draws;
        for(u32 i=0;i<count;i++){const auto& o=guard_out[i];
            const float cx=o.x*m[0]+o.y*m[4]+o.z*m[8]+m[12],cy=o.x*m[1]+o.y*m[5]+o.z*m[9]+m[13],cw=o.x*m[3]+o.y*m[7]+o.z*m[11]+m[15];
            if(cw<=0){++behind_vertices;any=true;continue;}
            const float px=(v.x+v.width*0.5f)*sx+cx/cw*v.width*sx*0.5f,py=(v.y+v.height*0.5f)*sy-cy/cw*v.height*sy*0.5f;
            if(px<-1808.0f||px>2288.0f||py<-1912.0f||py>2184.0f){++guard_vertices;any=true;}}
        if(any)++guard_draws;
    }
    if(!count)return true;
    auto* clipped=static_cast<GeVertex*>(sceGuGetMemory(int(count*sizeof(GeVertex))));std::memcpy(clipped,guard_out,count*sizeof(GeVertex));
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,int(count),nullptr,clipped);
    return true;
}
#endif
#if defined(TH10_WORLD_BAKE) && TH10_WORLD_BAKE
// The GE keeps the top 24 bits of each matrix float (sceGuSetMatrix: 15 bits of
// mantissa). The floor tiles of stage 6 each come with their own world matrix,
// so an edge two tiles share lands up to ~3e-5 of its distance apart in the two
// draws, and the GE's 1/16-pixel raster leaves a row that neither covers: the
// red lines across the stage 6 floor on the Go and the PSP-1000 (2026-10-08;
// PPSSPP rasterizes in float and shows none). An affine world is applied to
// the vertices here in float32 instead and the GE gets an identity world; view
// and projection are shared by every draw of a frame, so their truncation is too.
constexpr float world_identity[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
bool world_affine(const float* w){return w[3]==0.0f&&w[7]==0.0f&&w[11]==0.0f&&w[15]==1.0f;}
#endif
#ifndef TH10_FIXED_VERTEX
#define TH10_FIXED_VERTEX 0
#endif
#ifndef TH10_FIXED_VERTEX_CHECK
#define TH10_FIXED_VERTEX_CHECK 0
#endif
#if TH10_FIXED_VERTEX || TH10_FIXED_VERTEX_CHECK
u32 fixed_checked_draws=0,fixed_mismatch_draws=0;
// Screen-space sprite vertices at fixed offsets (see draw_timed).
void encode_fixed(const void* vertices,GeVertex* out,u32 n,bool uv,float sx,float sy,float tw,float th){
    const u8* p=static_cast<const u8*>(__builtin_assume_aligned(vertices,4));
    for(u32 i=0;i<n;i++,p+=28){GeVertex& v=out[i];float x,y,z,u=0,w=0;u32 colour;
        std::memcpy(&x,p,4);std::memcpy(&y,p+4,4);std::memcpy(&z,p+8,4);std::memcpy(&colour,p+16,4);
        v.color=rgba(colour);if(uv){std::memcpy(&u,p+20,4);std::memcpy(&w,p+24,4);}
        v.x=(x+0.5f)*sx;v.y=(y+0.5f)*sy;const float depth=65535.0f-65535.0f*z;v.z=depth<0?0:depth>65535.0f?65535.0f:depth;v.u=u*tw;v.v=w*th;}
}
#endif
#if defined(TH10_QUAD_INDEX) && TH10_QUAD_INDEX
}}
extern "C" {volatile int th10_quad_batch=0;}   // set by AnmRenderer::flush just before its draw
namespace touhou::sdl { namespace {
// th10_port (TH08 TH08_PSP_EFFECT_INDEXED_QUADS / ITEM_NATURAL_QUADS: four unique
// vertices per quad and the canonical six indices): AnmRenderer::append writes
// every quad as q0 q1 q2 q1 q2 q3, so for its batches (flagged by flush) only
// positions 0, 1, 2 and 5 of each six are converted and one fixed index list
// draws the same two triangles in the same order. u16 indices: 16383 quads.
constexpr u32 quad_index_limit=16383;
alignas(16) u16 quad_indices[quad_index_limit*6];bool quad_indices_ready=false;u32 quad_batches=0,quad_rejects=0;
void build_quad_indices(){for(u32 q=0;q<quad_index_limit;q++){u16* i=quad_indices+q*6;const u16 b=u16(q*4);i[0]=b;i[1]=u16(b+1);i[2]=u16(b+2);i[3]=u16(b+1);i[4]=u16(b+2);i[5]=u16(b+3);}
    sceKernelDcacheWritebackRange(quad_indices,sizeof(quad_indices));quad_indices_ready=true;}
void encode_fixed_quads(const void* vertices,GeVertex* out,u32 quads,bool uv,float sx,float sy,float tw,float th){
    const u8* p=static_cast<const u8*>(__builtin_assume_aligned(vertices,4));
    for(u32 q=0;q<quads;q++,p+=6*28){
        static constexpr u32 unique[4]={0,1,2,5};
        for(u32 k=0;k<4;k++){const u8* s=p+unique[k]*28;GeVertex& v=out[q*4+k];float x,y,z,u=0,w=0;u32 colour;
            std::memcpy(&x,s,4);std::memcpy(&y,s+4,4);std::memcpy(&z,s+8,4);std::memcpy(&colour,s+16,4);
            v.color=rgba(colour);if(uv){std::memcpy(&u,s+20,4);std::memcpy(&w,s+24,4);}
            v.x=(x+0.5f)*sx;v.y=(y+0.5f)*sy;const float depth=65535.0f-65535.0f*z;v.z=depth<0?0:depth>65535.0f?65535.0f:depth;v.u=u*tw;v.v=w*th;}}
}
// q1 q2 of each six repeated at positions 3 and 4 (the TH10_QUAD_INDEX_CHECK audit).
bool quads_repeat(const void* vertices,u32 quads){const u8* p=static_cast<const u8*>(vertices);
    for(u32 q=0;q<quads;q++,p+=6*28)if(std::memcmp(p+28,p+3*28,28)||std::memcmp(p+2*28,p+4*28,28))return false;return true;}
#endif
}

Renderer* current(){return active;}
void set_current(Renderer* r){active=r;}

Renderer::Renderer(int v,Resolve r,void* p):version(v),resolve(r),owner(p){}
bool Renderer::initialize(){
    frame_hash=fnv_offset;set_current(this);
    if(sceGuInit()<0){failure="sceGuInit failed";return false;}
    sceGuSetCallback(GU_CALLBACK_FINISH,ge_finish_callback);
    sceKernelDcacheWritebackInvalidateRange(lists,sizeof(lists));
    // GE4 exactly where TH08 native_ge.cpp does it: after sceGuInit's idle
    // sync, before the display mode and any display list of ours.
    sceGuSync(0,0);
#if !defined(TH10_GE4) || TH10_GE4
    if(th10_ge4_enable())vram_end=0x400000;   // TH10_GE4=0 (the PSP-1000 lane): 2 MiB only, the wrapper is never loaded
#endif
    if(sceDisplaySetMode(0,480,272)<0){failure="sceDisplaySetMode failed";return false;}
    std::memset(reinterpret_cast<void*>(0x44000000u),0,2*frame_bytes);if(frame_count==3)std::memset(reinterpret_cast<void*>(0x44000000u+frame_offset[2]),0,frame_bytes);
    // A pixel format change latches only on NEXTFRAME (TH08 native_ge.cpp).
    sceDisplaySetFrameBuf(vram(0),512,GU_PSM_5650,PSP_DISPLAY_SETBUF_NEXTFRAME);
    for(int i=0;i<120;i++){void* top=nullptr;int width=0,format=0;
        if(sceDisplayGetFrameBuf(&top,&width,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)>=0&&(reinterpret_cast<u32>(top)&0x1fffffffu)==0x04000000u&&format==GU_PSM_5650)break;
        sceDisplayWaitVblankStart();}
    draw_frame=1;displayed_frame=0;pending_count=0;list_open=false;
    sceGuStart(GU_DIRECT,lists[list_index]);
    sceGuDrawBuffer(GU_PSM_5650,reinterpret_cast<void*>(frame_bytes),512);
    sceGuDepthBuffer(reinterpret_cast<void*>(depth_offset),512);
    sceGuDispBuffer(480,272,nullptr,512);
    sceGuOffset(2048-240,2048-136);
    sceGuSendCommandi(0x15,0);sceGuSendCommandi(0x16,(271<<10)|479);
    sceGuDepthRange(65535,0);
    // DepthRange rounds half units; reversed depth needs the exact scale.
    sceGuSendCommandf(0x44,-32767.5f);sceGuSendCommandf(0x47,32767.5f);
    sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_STENCIL_TEST);sceGuDisable(GU_COLOR_TEST);sceGuDisable(GU_COLOR_LOGIC_OP);
    alignas(16) static const int dither[16]{-4,0,-3,1, 2,-2,3,-1, -3,1,-4,0, 3,-1,2,-2};
    sceGuSetDither(reinterpret_cast<const ScePspIMatrix4*>(dither));sceGuEnable(GU_DITHER);
    sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_CLIP_PLANES);sceGuScissor(0,0,480,272);
    sceGuShadeModel(GU_SMOOTH);sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexOffset(0,0);
    sceGuColor(0xffffffff);
    sceGuClearColor(0);sceGuClearDepth(0);sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
    sceGuFinish();list_target[list_index]=++ge_submitted;sceGuSync(0,0);list_index^=1;++list_serial;applied=Applied{};
    return true;
}
Renderer::~Renderer(){
    drain();wait_ge(ge_submitted);while(pending_count)wait_display();for(auto& e:textures)e.second.vram=0;th10_ge4_shutdown();sceGuTerm();
    std::printf("GE: conversions=%u readbacks=%u copies=%u unsupported_combiner=%u list_restarts=%u\n",conversions,readbacks,copies,unsupported_combiner,restarts);
    if(active==this)active=nullptr;
}
void Renderer::mix(const void* bytes,size_t size){
    if(!hashing)return;
    const auto* p=static_cast<const u8*>(bytes);
    for(size_t i=0;i<size;i++){frame_hash^=p[i];frame_hash*=fnv_prime;}
}
void Renderer::flush(){}
void Renderer::discard(){}
void Renderer::transform(MatrixKind kind,const void* matrix){
    const auto k=static_cast<u32>(kind);if(k<state.matrix.size())std::memcpy(state.matrix[k].data(),matrix,sizeof(state.matrix[k]));
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    if(k==1)++view_gen;else if(k==2)++projection_gen;
#endif
}
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
void Renderer::viewport(const Viewport& v){state.viewport=v;++viewport_gen;}
#else
void Renderer::viewport(const Viewport& v){state.viewport=v;}
#endif
void Renderer::draw(Topology primitive,u32 count,const void* vertices,u32 stride,const void* indices,IndexType format){
    const u64 start=now_us();draw_timed(primitive,count,vertices,stride,indices,format);frame_cpu_us+=now_us()-start;
}
void Renderer::draw_timed(Topology primitive,u32 count,const void* vertices,u32 stride,const void*,IndexType){
    // Same frame hash as the headless renderer (ids are not hashed).
    Surface texture_surface{},target_surface{};
    u32 texture[3]{},target[3]{};
    if(state.texture){texture_surface=resolve(owner,state.texture);texture[0]=texture_surface.width;texture[1]=texture_surface.height;texture[2]=static_cast<u32>(texture_surface.format);}
    if(state.target){target_surface=resolve(owner,state.target);target[0]=target_surface.width;target[1]=target_surface.height;target[2]=static_cast<u32>(target_surface.format);
        if(!back_id){back_id=state.target;back_width=target_surface.width?target_surface.width:640;back_height=target_surface.height?target_surface.height:480;}}
    if(hashing){const u32 header[]{1,static_cast<u32>(primitive),count,stride};mix(header,sizeof(header));mix(texture,sizeof(texture));mix(target,sizeof(target));
        mix(&state.pipeline,sizeof(state.pipeline));mix(state.matrix.data(),sizeof(state.matrix));
        if(vertices&&stride)mix(vertices,size_t(vertex_count(primitive,count))*stride);}
    stats.calls++;frame_draws++;
    const u32 n=vertex_count(primitive,count);if(!vertices||!stride||!n||u32(primitive)>5)return;
    const bool through=state.layout.screen;
#if defined(TH10_WORLD_BAKE) && TH10_WORLD_BAKE
    // For this draw the state holds an identity world (apply, the guard clip
    // and the --ge-debug count read it) and the vertices carry the world.
    float world[16];
    const bool bake=!through&&world_affine(state.matrix[0].data());
    struct WorldRestore {std::array<float,16>& m;const float* saved;bool on;~WorldRestore(){if(on)std::memcpy(m.data(),saved,64);}} restore{state.matrix[0],world,bake};
    if(bake){std::memcpy(world,state.matrix[0].data(),64);std::memcpy(state.matrix[0].data(),world_identity,64);}
#endif
    Combine c=combine(state.pipeline,state.texture!=0);
    const bool flat=(debug_flags&8)&&!through;
    if(flat){c.texture=false;c.diffuse=false;c.constant=0xff00ff00u;}
    GeTexture* t=c.texture&&state.texture?bind(state.texture,texture_surface):nullptr;
    reserve(n*sizeof(GeVertex)+256);
    if(flat){State plain=state;auto& q=plain.pipeline;q.blend=false;q.alphaTest=false;q.depthTest=false;q.fog=false;apply(plain,through,c,t,state.texture,&texture_surface);}
    else apply(state,through,c,t,state.texture,&texture_surface);
    if(t){t->list=list_serial;t->target=ge_submitted+1;}   // the open list closes as FINISH number ge_submitted+1
#if defined(TH10_QUAD_INDEX) && TH10_QUAD_INDEX
    const bool quad_batch=th10_quad_batch!=0;th10_quad_batch=0;
    const bool quads=quad_batch&&primitive==Topology::Triangles&&n%6==0&&n/6<=quad_index_limit&&through&&stride==28&&state.layout.diffuse==16&&c.diffuse&&
        (!(state.layout.uv!=VertexAttributes::absent&&t)||state.layout.uv==20)&&
        !(reinterpret_cast<uintptr_t>(vertices)&3u)
#if TH10_QUAD_INDEX_CHECK
        &&(quads_repeat(vertices,n/6)||(++quad_rejects,false))
#endif
        ;
    if(quads){
        if(!quad_indices_ready)build_quad_indices();
        const bool has_uv=state.layout.uv!=VertexAttributes::absent&&t;
        auto* quad_out=static_cast<GeVertex*>(sceGuGetMemory(int((n/6)*4*sizeof(GeVertex))));
        const float qsx=480.0f/back_width,qsy=272.0f/back_height,qtw=t?float(texture_surface.width):0,qth=t?float(texture_surface.height):0;
        encode_fixed_quads(vertices,quad_out,n/6,has_uv,qsx,qsy,qtw,qth);++quad_batches;
        sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D|GU_INDEX_16BIT,int(n),quad_indices,quad_out);
        return;
    }
#endif
    auto* out=static_cast<GeVertex*>(sceGuGetMemory(int(n*sizeof(GeVertex))));
    const auto* in=static_cast<const u8*>(vertices);const auto& L=state.layout;
    const u32 constant=rgba(c.constant);const bool diffuse=c.diffuse&&L.diffuse!=VertexAttributes::absent;
    const bool uv=L.uv!=VertexAttributes::absent&&t;
    const bool transform_uv=uv&&!through&&state.pipeline.textureTransform;const float* tm=state.matrix[3].data();
    const float sx=480.0f/back_width,sy=272.0f/back_height,tw=t?float(texture_surface.width):0,th=t?float(texture_surface.height):0;
    // TH08 psp/native_fixed_vertex.hpp (sha b082e2be, r252): the sprite batches
    // (stride 28: xyz, rhw, colour at 16, uv at 20; 4-byte aligned) are read at
    // fixed offsets with aligned loads instead of the generic memcpy reads of
    // unknown alignment (lwl/lwr pairs). The same float operations in the same
    // order (-ffp-contract=off): TH10_FIXED_VERTEX_CHECK byte-compares both.
#if TH10_FIXED_VERTEX || TH10_FIXED_VERTEX_CHECK
    const bool fixed=TH10_FIXED_VERTEX&&through&&stride==28&&L.diffuse==16&&diffuse&&(!uv||L.uv==20)&&!(reinterpret_cast<uintptr_t>(vertices)&3u);
#endif
#if TH10_FIXED_VERTEX_CHECK
    if(fixed){static GeVertex check[4096];const u32 m=n<4096?n:4096;const u8* p=static_cast<const u8*>(vertices);
        for(u32 i=0;i<m;i++,p+=stride){GeVertex& v=check[i];v.color=rgba(read32(p+L.diffuse));float u=0,w=0;if(uv){u=readf(p+L.uv);w=readf(p+L.uv+4);}
            v.x=(readf(p)+0.5f)*sx;v.y=(readf(p+4)+0.5f)*sy;float z=65535.0f-65535.0f*readf(p+8);v.z=z<0?0:z>65535.0f?65535.0f:z;v.u=u*tw;v.v=w*th;}
        encode_fixed(vertices,out,n,uv,sx,sy,tw,th);++fixed_checked_draws;if(std::memcmp(check,out,m*sizeof(GeVertex)))++fixed_mismatch_draws;
    }else
#elif TH10_FIXED_VERTEX
    if(fixed)encode_fixed(vertices,out,n,uv,sx,sy,tw,th);else
#endif
    for(u32 i=0;i<n;i++,in+=stride){
        GeVertex& v=out[i];
        v.color=diffuse?rgba(read32(in+L.diffuse)):constant;
        float u=0,w=0;if(uv){u=readf(in+L.uv);w=readf(in+L.uv+4);}
        if(through){
            v.x=(readf(in)+0.5f)*sx;v.y=(readf(in+4)+0.5f)*sy;
            float z=65535.0f-65535.0f*readf(in+8);v.z=z<0?0:z>65535.0f?65535.0f:z;
            v.u=u*tw;v.v=w*th;
        }else{
#if defined(TH10_WORLD_BAKE) && TH10_WORLD_BAKE
            const float x=readf(in),y=readf(in+4),z=readf(in+8);   // row vector times world (D3D layout)
            if(bake){v.x=x*world[0]+y*world[4]+z*world[8]+world[12];v.y=x*world[1]+y*world[5]+z*world[9]+world[13];v.z=x*world[2]+y*world[6]+z*world[10]+world[14];}
            else{v.x=x;v.y=y;v.z=z;}
#else
            v.x=readf(in);v.y=readf(in+4);v.z=readf(in+8);
#endif
            if(transform_uv){v.u=u*tm[0]+w*tm[4]+tm[8];v.v=u*tm[1]+w*tm[5]+tm[9];}else{v.u=u;v.v=w;}
        }
    }
#if TH10_GUARD_CLIP
    if(!through&&guard_draw(state,primitive,n,out))return;
#endif
    if(!through&&(debug_flags&4)){
        // clip = v * world * view * projection' (row vectors, D3D layout)
        float m[16],t[16];const float* w=state.matrix[0].data();const float* vw=state.matrix[1].data();const float* pr=applied.matrix[2];
        for(int r=0;r<4;r++)for(int c=0;c<4;c++){float x=0;for(int k=0;k<4;k++)x+=w[r*4+k]*vw[k*4+c];t[r*4+c]=x;}
        for(int r=0;r<4;r++)for(int c=0;c<4;c++){float x=0;for(int k=0;k<4;k++)x+=t[r*4+k]*pr[k*4+c];m[r*4+c]=x;}
        const float sx=480.0f/back_width,sy=272.0f/back_height;const auto& v=state.viewport;bool any=false;++world_draws;
        for(u32 i=0;i<n;i++){const float x=out[i].x,y=out[i].y,z=out[i].z;
            const float cx=x*m[0]+y*m[4]+z*m[8]+m[12],cy=x*m[1]+y*m[5]+z*m[9]+m[13],cz=x*m[2]+y*m[6]+z*m[10]+m[14],cw=x*m[3]+y*m[7]+z*m[11]+m[15];
            if(cw>0){const float nz=cz/cw;if(nz<-1.0f)++z_below;else if(nz>1.0f)++z_above;else ++z_inside;if(nz<z_min)z_min=nz;if(nz>z_max)z_max=nz;if(cw<w_min)w_min=cw;if(cw>w_max)w_max=cw;}
            if(cw<=0){++behind_vertices;any=true;continue;}
            const float px=240.0f-240.0f+(v.x+v.width*0.5f)*sx+cx/cw*v.width*sx*0.5f,py=(v.y+v.height*0.5f)*sy-cy/cw*v.height*sy*0.5f;
            if(px<-1808.0f||px>2288.0f||py<-1912.0f||py>2184.0f){++guard_vertices;any=true;}}
        if(any)++guard_draws;
    }
    static const int prims[]{GU_POINTS,GU_LINES,GU_LINE_STRIP,GU_TRIANGLES,GU_TRIANGLE_STRIP,GU_TRIANGLE_FAN};
    sceGuDrawArray(prims[u32(primitive)],GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|(through?GU_TRANSFORM_2D:GU_TRANSFORM_3D),int(n),nullptr,out);
}
void Renderer::draw_batch(u32 count,const void* vertices,u32 stride){draw(Topology::Triangles,count,vertices,stride);stats.batches++;}
void Renderer::clear(u32 flags,u32 color,float depth,u32 stencil,const i32* rects,u32 count){
    u32 target[3]{};if(state.target){const auto s=resolve(owner,state.target);target[0]=s.width;target[1]=s.height;target[2]=static_cast<u32>(s.format);
        if(!back_id){back_id=state.target;back_width=s.width?s.width:640;back_height=s.height?s.height:480;}}
    if(hashing){const u32 header[]{2,flags,color,stencil,count};mix(header,sizeof(header));mix(target,sizeof(target));mix(&depth,sizeof(depth));
        if(rects&&count)mix(rects,size_t(count)*4*sizeof(i32));}
    int mask=(flags&1?GU_COLOR_BUFFER_BIT:0)|(flags&2?GU_DEPTH_BUFFER_BIT:0)|(flags&4?GU_STENCIL_BUFFER_BIT:0);if(!mask)return;
    reserve(256+count*64);
    const float sx=480.0f/back_width,sy=272.0f/back_height;const auto& v=state.viewport;
    const i32 box[]{i32(v.x),i32(v.y),i32(v.x+v.width),i32(v.y+v.height)};if(!rects||!count){rects=box;count=1;}
    if(depth<0)depth=0;else if(depth>1)depth=1;
    sceGuDepthMask(0);sceGuClearColor(rgba(color));sceGuClearDepth(unsigned(65535.0f-depth*65535.0f));sceGuClearStencil(stencil);
    for(u32 i=0;i<count;i++){const i32* r=rects+i*4;
        const int left=int(std::floor(r[0]*sx)),top=int(std::floor(r[1]*sy)),right=int(std::ceil(r[2]*sx)),bottom=int(std::ceil(r[3]*sy));
        if(right>left&&bottom>top){sceGuScissor(left,top,right-left,bottom-top);sceGuClear(mask);}}
    applied.valid=false;
}
void Renderer::copy(u32 source,const i32* rect,u32 target,const i32* point){
    const auto a=resolve(owner,source),b=resolve(owner,target);
    if(hashing){const u32 header[]{3,a.width,a.height,static_cast<u32>(a.format),b.width,b.height,static_cast<u32>(b.format)};mix(header,sizeof(header));
        if(rect)mix(rect,4*sizeof(i32));if(point)mix(point,2*sizeof(i32));}
    ++copies;
    if(source==back_id||!a.data||!b.data||!rect||!point)return;   // framebuffer copies: not needed so far (counted)
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    if(a.palette||b.palette)return;   // the host turns CLUT8 textures back into 16 bit before a copy
#endif
    to_d3d(source,a);to_d3d(target,b);
    u32 bpp=a.size/(a.height?a.height:1)/(a.width?a.width:1);if(!bpp)return;
    const i32 w=rect[2]-rect[0],h=rect[3]-rect[1];
    for(i32 y=0;y<h;y++)std::memmove(b.data+(point[1]+y)*b.pitch+point[0]*bpp,a.data+(rect[1]+y)*a.pitch+rect[0]*bpp,size_t(w)*bpp);
}
bool Renderer::resample(u32,const i32*,u32,const i32*,const float*,u32,u32){return false;}
void Renderer::read(u32 id){
    if(id==back_id){++readbacks;return;}   // framebuffer readback: counted, not implemented yet
    const auto s=resolve(owner,id);to_d3d(id,s);
}
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
// The text atlas upload (Fonts.cpp upload_content): the game writes one
// rectangle and never reads the texture there. When the 4444 texture is in GE
// form and current, the rectangle goes straight into it: R/B swapped (the
// swap is its own inverse) at its swizzled addresses (a permutation), the
// touched 8-row bands written back and copied to the VRAM copy. Before, the
// whole 512 KiB went back to D3D form and was converted and uploaded again
// for every dialogue line. The bytes equal what to_d3d, the write and bind
// leave; any other case takes that path (bind converts on the next draw).
u32 rect_writes=0,rect_fallbacks=0;
bool Renderer::write_rect(u32 id,const i32* r,const u8* pixels,u32 pitch){
    const auto s=resolve(owner,id);
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    if(s.palette)return false;   // the host turns a CLUT8 texture back into 16 bit before any write
#endif
    if(!s.data||!r||r[0]<0||r[1]<0||r[0]>=r[2]||r[1]>=r[3]||u32(r[2])>s.width||u32(r[3])>s.height||!s.height||!s.width)return false;
    const u32 bpp=s.size/s.height/s.width;if(!bpp)return false;
    const u32 x0=u32(r[0]),y0=u32(r[1]),x1=u32(r[2]),y1=u32(r[3]);
    auto it=textures.find(id);
    if(it==textures.end()||!it->second.swapped||it->second.version!=s.version||s.format!=PixelFormat::Argb4444){
        to_d3d(id,s);   // nothing to do unless the GE form is there
        for(u32 y=y0;y<y1;y++)std::memcpy(s.data+y*s.pitch+x0*bpp,pixels+(y-y0)*pitch,(x1-x0)*bpp);
        ++rect_fallbacks;return true;
    }
    auto& t=it->second;settle(t);
    const u32 band=s.pitch*8;
    for(u32 y=y0;y<y1;y++){const u8* row=pixels+(y-y0)*pitch;
        for(u32 x=x0;x<x1;x++){u16 n;std::memcpy(&n,row+(x-x0)*2,2);n=u16((n&0xf0f0u)|((n>>8)&15u)|((n&15u)<<8));   // swap_channels, 4444
            const u32 byte=x*2,offset=t.swizzled?(y/8)*band+((byte/16)*8+(y&7u))*16+(byte&15u):y*s.pitch+byte;
            std::memcpy(s.data+offset,&n,2);}}
    const u32 first=t.swizzled?(y0/8)*band:y0*s.pitch,last=t.swizzled?((y1+7)/8)*band:y1*s.pitch;
    sceKernelDcacheWritebackRange(s.data+first,last-first);
    const u32 version=s.version+1;   // the host moves the surface version after this call
    if(t.vram&&t.vram_version==t.version){
        reserve(256);   // the copy runs in the list, before the draws that sample it
        const u32 row0=first/2048,row1=(last+2047)/2048;   // the VRAM copy is a 512-wide 32-bit image (2 KiB rows)
        sceGuCopyImage(GU_PSM_8888,0,int(row0),512,int(row1-row0),512,s.data,0,int(row0),512,reinterpret_cast<void*>(0x04000000u+t.vram));
        sceGuTexSync();t.vram_version=version;t.list=list_serial;t.target=ge_submitted+1;
    }
    t.version=version;
    if(applied.texture==id)applied.texture=0;   // sceGuTexImage again: flushes the GE texture cache
    ++rect_writes;return true;
}
#endif
void Renderer::release(u32 id){auto it=textures.find(id);if(it==textures.end())return;settle(it->second);if(applied.texture==id)applied.texture=0;
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
    if(it->second.vram)++vram_free_gen;
#endif
    textures.erase(it);}
void Renderer::prepare(u32){}
void Renderer::present(u32 id){
    if(!back_id)back_id=id;
    pending=id;stats.frames++;presented_hash=frame_hash;presented_draws=frame_draws;
    frame_hash=fnv_offset;frame_draws=0;if(!defer)commit();
}
bool Renderer::commit(){
    if(!pending)return false;pending=0;stats.presentations++;
    if(list_open){
#if TH10_REC
        rec_present(draw_frame);
#endif
        // Close the frame without waiting for the GE: it waits in the queue for
        // its flip (poll_present) and the next list picks a free image (TH08 Present).
        TH10_NOTE_LIST();sceGuFinish();list_target[list_index]=++ge_submitted;list_open=false;list_index^=1;++list_serial;
        if(pending_count>=max_pending){const u64 t=now_us();wait_display();frame_flip_us+=now_us()-t;++commit_waits;}   // TH08 kPendingDepth
        ++frames;++frame_counter;sum_cpu_us+=frame_cpu_us;sum_ge_us+=frame_ge_us;sum_flip_us+=frame_flip_us;
#if defined(TH10_WINDOW_BREAKDOWN) && TH10_WINDOW_BREAKDOWN
        last_frame_cpu_us=u32(frame_cpu_us);
#endif
        ++hist_cpu[std::min<u64>(frame_cpu_us/1000,127)];++hist_ge[std::min<u64>(frame_ge_us/1000,127)];++hist_flip[std::min<u64>(frame_flip_us/1000,127)];
        frame_cpu_us=frame_ge_us=frame_flip_us=0;
        pending_queue[pending_count++]=PendingFrame{draw_frame,ge_submitted,false,now_us(),0};
        draw_frame=-1;poll_present();
    }
    return true;
}
}

extern "C" {
void headless_set_hash(bool enabled){touhou::sdl::hashing=enabled;}
const touhou::sdl::Statistics* sdl_stats(){static touhou::sdl::Statistics zero{};return touhou::sdl::current()?&touhou::sdl::current()->stats:&zero;}
void sdl_defer(int enabled){if(auto* r=touhou::sdl::current())r->defer=enabled;}
// Request the pending flip as soon as the GE has finished the presented list
// (main loop, every tick and while pacing).
void ge_poll(void){touhou::sdl::poll_present();}
#if defined(TH10_WINDOW_BREAKDOWN) && TH10_WINDOW_BREAKDOWN
u32 ge_last_frame_cpu_us(void){return touhou::sdl::last_frame_cpu_us;}
#endif
// The renderer's teardown without the renderer (main.cpp fail_stop): the GE
// idle, GE4 back to 2 MiB with its power lock released, before ExitGame.
void ge_shutdown_for_exit(void){using namespace touhou::sdl;static bool done=false;if(done)return;done=true;
    drain();wait_ge(ge_submitted);while(pending_count)wait_display();for(auto& e:textures)e.second.vram=0;th10_ge4_shutdown();sceGuTerm();}
int sdl_commit(){return touhou::sdl::current()&&touhou::sdl::current()->commit();}
const char* sdl_error(){return touhou::sdl::current()?touhou::sdl::current()->error():"no renderer";}
int sdl_version(){return 0;}
double sdl_ticks(){return double(sceKernelGetSystemTimeWide())/1000.0;}
void ge_renderer_counts(unsigned* out){out[0]=touhou::sdl::conversions;out[1]=touhou::sdl::readbacks;out[2]=touhou::sdl::copies;out[3]=touhou::sdl::unsupported_combiner;out[4]=touhou::sdl::restarts;
    out[5]=touhou::sdl::world_draws;out[6]=touhou::sdl::guard_draws;out[7]=touhou::sdl::guard_vertices;out[8]=touhou::sdl::behind_vertices;
    out[9]=touhou::sdl::z_below;out[10]=touhou::sdl::z_inside;out[11]=touhou::sdl::z_above;
    float f[4]{touhou::sdl::z_min,touhou::sdl::z_max,touhou::sdl::w_min,touhou::sdl::w_max};std::memcpy(out+12,f,16);}
void ge_set_debug(unsigned flags){touhou::sdl::debug_flags=flags;}
#ifdef TH10_LIST_KB
void ge_list_stats(unsigned* out){out[0]=touhou::sdl::list_bytes;out[1]=touhou::sdl::list_peak;out[2]=touhou::sdl::restarts;}
#endif
// "ge frames=N cpu avg/p50/p95 ms ge_wait avg/p50/p95 ms ... buffers=3 flip_wait ..." for the result file.
void ge_frame_stats(char* out,unsigned size){using namespace touhou::sdl;
    auto pct=[](const u32* h,u32 n,double p){if(!n)return 0u;u32 want=u32(p*n),acc=0;for(u32 i=0;i<128;i++){acc+=h[i];if(acc>want)return i;}return 127u;};
    u32 vram_used=0,vram_count=0;for(auto& e:textures)if(e.second.vram){vram_used+=e.second.bytes;++vram_count;}
    const int n=std::snprintf(out,size,"ge frames=%u cpu_draw avg=%.1f p50=%u p95=%u ms  ge_wait avg=%.1f p50=%u p95=%u ms  swizzled=%u vram %s textures=%u bytes=%u of %u uploads=%u evictions=%u refreshes=%u",
        frames,frames?sum_cpu_us/1000.0/frames:0.0,pct(hist_cpu,frames,.5),pct(hist_cpu,frames,.95),frames?sum_ge_us/1000.0/frames:0.0,pct(hist_ge,frames,.5),pct(hist_ge,frames,.95),
        swizzles,th10_ge4_state(),vram_count,vram_used,vram_end-vram_begin,vram_uploads,vram_evictions,vram_refreshes);
    if(n>0&&unsigned(n)<size)std::snprintf(out+n,size-unsigned(n),"  buffers=%d flip_wait avg=%.2f p95=%u ms ge_tail avg=%.1f p95=%u ms latch avg=%.1f ms commit_waits=%u timeouts=%u",
        frame_count,frames?sum_flip_us/1000.0/frames:0.0,pct(hist_flip,frames,.95),tails?sum_tail_us/1000.0/tails:0.0,pct(hist_tail,tails,.95),latches?sum_latch_us/1000.0/latches:0.0,commit_waits,wait_timeouts);
#if TH10_FIXED_VERTEX_CHECK
    {const size_t k=std::strlen(out);if(k<size)std::snprintf(out+k,size-k,"  fixed_vertex checked=%u mismatch=%u",fixed_checked_draws,fixed_mismatch_draws);}
#endif
#if defined(TH10_QUAD_INDEX) && TH10_QUAD_INDEX
    {const size_t k=std::strlen(out);if(k<size)std::snprintf(out+k,size-k,"  quad_batches=%u rejects=%u",quad_batches,quad_rejects);}
#endif
#if TH10_GUARD_CLIP
    {const size_t k=std::strlen(out);if(k<size)std::snprintf(out+k,size-k,"  guard_clip draws=%u triangles=%u dropped=%u overflows=%u",guard_clip_draws,guard_clip_triangles,guard_drop_triangles,guard_overflows);}
#endif
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
    {const size_t k=std::strlen(out);if(k<size)std::snprintf(out+k,size-k,"  rect_writes=%u fallbacks=%u",rect_writes,rect_fallbacks);}
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    {const size_t k=std::strlen(out);if(k<size)std::snprintf(out+k,size-k,"  clut8 textures=%u loads=%u",clut_textures,clut_loads);}
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    {const size_t k=std::strlen(out);if(k<size)std::snprintf(out+k,size-k," uniform=%u",uniform_textures);}
#endif
}
void ge_sample(char* out,unsigned size){if(size)out[0]=0;}
#if TH10_REC
int th10_rec_ge_done(unsigned fence){return touhou::sdl::ge_done(fence)?1:0;}
#endif
}
