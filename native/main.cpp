// Headless TH10 runner. Builds the same Application as
// th10_web/cpp/sdl/GameHost.cpp and drives it with the deterministic
// sdl_loop_tick() semantics of ApplicationHost.cpp: every tick adds 1/60 s,
// advances the audio clock, samples input and runs one original 60 Hz step.
// Writes a per-tick sync trace and a summary (SoftFloat fallbacks, heap).
#include "../th10_web/cpp/platform/Application.hpp"
#include "Renderer.hpp"
#include <malloc.h>
#include <algorithm>
#include <cstdarg>
#ifdef __PSP__
#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>
#include <pspsdk.h>
#include <pspiofilemgr.h>
#include <pspsysmem.h>
#include <unistd.h>
#ifdef TH10_GE
#include <pspctrl.h>
#include <pspdisplay.h>
#endif
#define TH10_HAS_BACKTRACE 0
#else
#include <chrono>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <xmmintrin.h>
#define TH10_HAS_BACKTRACE 1
#endif
// BUILD_ID (psp_ge/Makefile TH10_BUILD_ID): `strings EBOOT.PBP | grep TH10_BUILD_ID=` and the
// first line of th10_result.txt say which build produced a log.
#if __has_include("build_id.h")
#include "build_id.h"
#endif
#ifndef TH10_BUILD_ID
#define TH10_BUILD_ID "unset"
#endif
extern "C" const char th10_build_tag[] __attribute__((used))="TH10_BUILD_ID=" TH10_BUILD_ID;
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <string>
#include <vector>
#if TH10_REC
#include "psp/Recorder.hpp"   // SELECT recording (psp/Recorder.cpp)
#endif
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
#include "psp/VolatileArena.hpp"   // PSP-1000 lane: the 4 MiB volatile partition
#endif
using namespace th10;using namespace th10::browser;
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
extern "C" void native_graphics_indexed_stats(u32*);extern "C" int native_graphics_surface_indexed(void*);   // sdl/GraphicsHost.cpp
extern "C" void th10_indexed_counts(u32*);extern "C" void th10_indexed_enable(int);                          // platform/Textures.cpp
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
extern "C" void native_graphics_lazy_stats(u32*);   // sdl/GraphicsHost.cpp
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
extern "C" void native_graphics_padded_stats(u32*);   // sdl/GraphicsHost.cpp
extern "C" void th10_padded_counts(u32*);extern "C" void th10_padded_enable(int);   // platform/Textures.cpp
#endif

extern "C" {
FileSystem* files_create();void files_destroy(FileSystem*);u32 files_attach(FileSystem*,const char*);
Input* input_create();void input_destroy(Input*);GameState* game_state_create(Input*,u32);void game_state_destroy(GameState*);
GraphicsDevice* graphics_create(const GraphicsPresentation*,u32);void graphics_destroy(GraphicsDevice*);
AnimationEngine* animation_engine_create(FileSystem*,GraphicsDevice*,Rng*,Rng*,float*);void animation_engine_destroy(AnimationEngine*);
Fonts* fonts_create(GraphicsDevice*,Rng*,u32);void fonts_destroy(Fonts*);Audio* audio_create(FileSystem*);void audio_destroy(Audio*);
ScreenEffects* effects_create(AnimationEngine*,const u32*,const u32*);void effects_destroy(ScreenEffects*);
Application* application_create(FileSystem*,Input*,GameState*,AnimationEngine*,Fonts*,Audio*,ScreenEffects*);void application_destroy(Application*);
void sdl_files_root(u32);void sdl_audio_pump();void sdl_audio_shutdown();void sdl_fonts_shutdown();void sdl_shutdown();
u32 headless_font_text_calls();u32 headless_audio_signals();void headless_read_stats(th10::u64*);
#if defined(__PSP__) && TH10_BGM_PREFETCH
void headless_bgm_stats(th10::u32*);
#endif
}

// ---- time host (th10_web/cpp/sdl/TimeHost.cpp contract, deterministic) ----
namespace {double elapsed=0;}
// Gameplay ticks are scheduled by the loop below (Application::step(true));
// this clock only feeds FPS statistics and frame-cost measurements. The PSP
// GE build reports real time so the in-game FPS meter shows the device's pace.
extern "C" double monotonic(){
#ifdef TH10_GE
    return double(sceKernelGetSystemTimeWide())*1e-6;
#else
    return elapsed;
#endif
}
extern "C" i32 current_timestamp(){return 1190000000;}
extern "C" void local_date(i32 stamp,void* out){const std::time_t value=stamp;std::tm t{};gmtime_r(&value,&t);
    const i32 fields[9]{t.tm_sec,t.tm_min,t.tm_hour,t.tm_mday,t.tm_mon,t.tm_year,t.tm_wday,t.tm_yday,0};std::memcpy(out,fields,sizeof(fields));}

// ---- platform helpers ----
namespace {
u64 now_us(){
#ifdef __PSP__
    return sceKernelGetSystemTimeWide();
#else
    return u64(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
size_t heap_in_use(){
#ifdef __PSP__
    const auto m=mallinfo();return size_t(m.uordblks)+size_t(m.hblkhd);
#else
    const auto m=mallinfo2();return m.uordblks+m.hblkhd;
#endif
}
void status(const char* fmt,...){
#if defined(__PSP__)&&!defined(TH10_GE)
    char line[160];va_list a;va_start(a,fmt);std::vsnprintf(line,sizeof(line),fmt,a);va_end(a);pspDebugScreenPrintf("%s",line);
#else
    (void)fmt;
#endif
}
// Fixed debug-screen rows (68 columns): 0-1 header/clock, 2 tick, 3 file,
// 4 large allocation, 5 FPU flags, 6 heap, 7 messages.
void screen_row(int row,const char* fmt,...){
#if defined(__PSP__)&&!defined(TH10_GE)
    char text[80];va_list a;va_start(a,fmt);std::vsnprintf(text,sizeof(text),fmt,a);va_end(a);
    pspDebugScreenSetXY(0,row);pspDebugScreenPrintf("%-67.67s",text);
#else
    (void)row;(void)fmt;
#endif
}
}

// ---- HOME menu exit (PSP) ----
#ifdef __PSP__
namespace {
volatile bool exit_requested=false;
int exit_callback(int,int,void*){exit_requested=true;return 0;}
int callback_thread(SceSize,void*){const int id=sceKernelCreateCallback("th10_exit",exit_callback,nullptr);sceKernelRegisterExitCallback(id);sceKernelSleepThreadCB();return 0;}
void start_callbacks(){const SceUID t=sceKernelCreateThread("th10_callbacks",callback_thread,0x11,0x1000,PSP_THREAD_ATTR_USER,nullptr);if(t>=0)sceKernelStartThread(t,0,nullptr);}
}
#endif

// ---- device probe: what the Go was doing when it stopped ----
// The PSP thread default FCR31 enables overflow, divide-by-zero and invalid
// traps (pspSdkDisableFPUExceptions() clears them, as TH07/TH08 did at
// start); PPSSPP never raises them. TH10 hit one in ScorePopups::draw
// (8/0 on a popup's first frame, tick 2533 of the demo run). Exceptions are
// disabled like the original's masked x87 and the sticky flags are logged.
namespace {
struct Probe {u32 tick=0,fpu_first[3]{~0u,~0u,~0u},fpu_ticks=0,fpu_events=0,mhz=0,big_alloc=0,big_tick=0,fails=0,fail_bytes=0,file_bytes=0,heap_room_start=0;
    u32 event_tick[16]{},event_flags[16]{};char file[40]{};char path[160]{};size_t heap_peak=0;u32 heap_peak_tick=0;int screen=0,stage=0;
    // The last allocations of 64 KiB or more (tick, bytes, file being read), written on failure.
    struct Big {u32 tick,bytes;char file[20];} recent[12]{};u32 recent_count=0;
#if defined(TH10_HEAP_HWM) && TH10_HEAP_HWM
    // th10_port (TH10_HEAP_HWM, PSP-1000 lane): the heap in use sampled after
    // every noted allocation (archive reads, surfaces, the ANM stream's
    // buffers) as well as after every tick, so a load's transient is seen;
    // the whole run, and since the previous th10_stages.txt line.
    size_t hwm=0,window_hwm=0;u32 hwm_tick=0,window_tick=0;char hwm_file[24]{},window_file[24]{};
    void note_heap(size_t heap,u32 tick,const char* file){
        if(heap>hwm){hwm=heap;hwm_tick=tick;std::snprintf(hwm_file,sizeof(hwm_file),"%s",file);}
        if(heap>window_hwm){window_hwm=heap;window_tick=tick;std::snprintf(window_file,sizeof(window_file),"%s",file);}}
#endif
} probe;
#ifdef __PSP__
u32 read_fcr31(){u32 v;asm volatile("cfc1 %0,$31":"=r"(v));return v;}
void write_fcr31(u32 v){asm volatile("ctc1 %0,$31"::"r"(v));}
// ARK's overclock is invisible to scePowerGet*ClockFrequencyInt (TH08
// ledger R-051: the API said 333 while the Go ran near 443), so time 64
// extra dependent ALU instructions per iteration; loop overhead cancels.
template<int N> __attribute__((noinline)) u32 spin(u32 iterations){u32 acc=0;
    asm volatile(".set push\n.set noreorder\n1:\n.rept %2\naddu %0,%0,%1\n.endr\naddiu %1,%1,-1\nbnez %1,1b\nnop\n.set pop\n":"+r"(acc),"+r"(iterations):"i"(N));return acc;}
u32 measure_mhz(){const u32 n=400000;spin<64>(1000);const u64 t0=now_us();spin<64>(n);const u64 t1=now_us();spin<128>(n);const u64 t2=now_us();
    const u64 a=t1-t0,b=t2-t1;return b>a?u32(64ull*n/(b-a)):0;}
// Remaining sbrk room of the newlib heap (binary search; the break is
// restored after every successful probe, so malloc never sees it move).
u32 heap_room(){u32 lo=0,hi=64u<<20;while(lo<hi){const u32 mid=lo+(hi-lo+1)/2;void* p=sbrk(ptrdiff_t(mid));if(p!=reinterpret_cast<void*>(-1)){sbrk(-ptrdiff_t(mid));lo=mid;}else hi=mid-1;}return lo;}
// Largest block malloc can return now (binary search to 4 KiB, each probe
// freed at once): the contiguous space a big load actually gets. The asm
// keeps GCC from eliding the malloc/free pair (it did: every probe
// "succeeded" and the first stage log read 64 MiB).
u32 max_block(){u32 lo=0,hi=64u<<20;while(hi-lo>4096){const u32 mid=lo+(hi-lo)/2;void* p=std::malloc(mid);asm volatile("":"+r"(p)::"memory");if(p){std::free(p);lo=mid;}else hi=mid;}return lo;}
#else
u32 heap_room(){return 0;}
u32 max_block(){return 0;}
#endif
const char* fpu_text(char* out,size_t size){
    auto at=[](u32 t){return t==~0u?-1:int(t);};
    int n=std::snprintf(out,size,"first O=%d Z=%d V=%d ticks=%u:",at(probe.fpu_first[0]),at(probe.fpu_first[1]),at(probe.fpu_first[2]),probe.fpu_ticks);
    for(u32 i=0;i<probe.fpu_events&&i<16&&n>0&&size_t(n)<size;i++){const u32 f=probe.event_flags[i];
        n+=std::snprintf(out+n,size-size_t(n)," t%u:%s%s%s",probe.event_tick[i],f&4?"O":"",f&8?"Z":"",f&16?"V":"");}
    return out;}
// Progress file, rewritten every 600 ticks and on events, so a frozen run
// still leaves its last state (no heap use: it also runs out of memory).
void write_progress(const char* why){
    if(!probe.path[0])return;static char text[2048];char fpu[512];
#ifdef __PSP__
    const auto m=mallinfo();const u32 in_use=u32(m.uordblks),arena=u32(m.arena),free_bytes=u32(m.fordblks);
    const int n=std::snprintf(text,sizeof(text),"build=" TH10_BUILD_ID " why=%s tick=%u screen=%d stage=%d\nreal_mhz=%u api=%d/%d\nheap in_use=%u arena=%u free=%u room=%u room_start=%u peak=%u (tick %u)\n"
        "kernel max_free=%u total_free=%u\nfpu %s\nfile=%s bytes=%u\nalloc big=%u (tick %u) fails=%u fail_bytes=%u max_block=%u free_chunks=%u top=%u\nrecent big allocs:",
        why,probe.tick,probe.screen,probe.stage,probe.mhz,scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt(),in_use,arena,free_bytes,heap_room(),probe.heap_room_start,
        u32(probe.heap_peak),probe.heap_peak_tick,u32(sceKernelMaxFreeMemSize()),u32(sceKernelTotalFreeMemSize()),fpu_text(fpu,sizeof(fpu)),probe.file,probe.file_bytes,
        probe.big_alloc,probe.big_tick,probe.fails,probe.fail_bytes,probe.fails?max_block():0u,u32(m.ordblks),u32(m.keepcost));
    int k=n>0?n:0;
    for(u32 i=0;i<12&&i<probe.recent_count&&size_t(k)<sizeof(text);i++){const auto& b=probe.recent[(probe.recent_count-1-i)%12];
        const int w=std::snprintf(text+k,sizeof(text)-size_t(k)," t%u:%u(%s)",b.tick,b.bytes,b.file);if(w>0)k+=w;}
    if(size_t(k)<sizeof(text)-1)text[k++]='\n';
#if defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    {unsigned v[8]{};th10_volatile_stats(v);const int w=std::snprintf(text+k,sizeof(text)-size_t(k),"volatile locked=%u bytes=%u in_use=%u peak=%u allocations=%u heap_fallbacks=%u live=%u\n",v[0],v[2],v[3],v[4],v[5],v[6],v[7]);
     if(w>0&&size_t(k+w)<sizeof(text))k+=w;}
#endif
    const SceUID f=sceIoOpen(probe.path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(f>=0){sceIoWrite(f,text,size_t(k));sceIoClose(f);}
    screen_row(6,"heap %.1f/%.1f MiB room %.1f MiB  (%s t%u)",in_use/1048576.0,arena/1048576.0,heap_room()/1048576.0,why,probe.tick);
#else
    (void)why;(void)text;(void)fpu;
#endif
}
// One appended line per stage change and again 120 ticks later, when the
// stage's loads are done (th10_stages.txt): the heap at every stage boundary
// of a real play, where the Go ran out (checklist C-3: writes only there).
void write_stage_line(){
#ifdef __PSP__
    if(!probe.path[0])return;char path[168];std::snprintf(path,sizeof(path),"%s",probe.path);
    char* slash=std::strrchr(path,'/');if(!slash)return;std::snprintf(slash+1,sizeof(path)-size_t(slash+1-path),"th10_stages.txt");
#if defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    const auto m=mallinfo();char line[320];unsigned v[8]{};th10_volatile_stats(v);
    const int n=std::snprintf(line,sizeof(line),"build=" TH10_BUILD_ID " tick=%u screen=%d stage=%d in_use=%u arena=%u free=%u room=%u max_block=%u free_chunks=%u peak=%u (tick %u) volatile_in_use=%u\n",
        probe.tick,probe.screen,probe.stage,u32(m.uordblks),u32(m.arena),u32(m.fordblks),heap_room(),max_block(),u32(m.ordblks),u32(probe.heap_peak),probe.heap_peak_tick,v[3]);
#else
    const auto m=mallinfo();char line[288];
    const int n=std::snprintf(line,sizeof(line),"build=" TH10_BUILD_ID " tick=%u screen=%d stage=%d in_use=%u arena=%u free=%u room=%u max_block=%u free_chunks=%u peak=%u (tick %u)\n",
        probe.tick,probe.screen,probe.stage,u32(m.uordblks),u32(m.arena),u32(m.fordblks),heap_room(),max_block(),u32(m.ordblks),u32(probe.heap_peak),probe.heap_peak_tick);
#endif
    const SceUID f=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);if(f>=0){sceIoWrite(f,line,size_t(n>0?n:0));sceIoClose(f);}
#if (defined(TH10_HEAP_HWM) && TH10_HEAP_HWM) || (defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8)
    // th10_port (PSP-1000 lane): a second line with the heap's high-water mark
    // (TH10_HEAP_HWM) and the CLUT8 textures alive now (TH10_TEXTURE_CLUT8).
    {char more[288];int k=std::snprintf(more,sizeof(more),"textures tick=%u stage=%d",probe.tick,probe.stage);
#if defined(TH10_HEAP_HWM) && TH10_HEAP_HWM
     k+=std::snprintf(more+k,sizeof(more)-size_t(k)," window_hwm=%u (tick %u %s) hwm=%u (tick %u)",u32(probe.window_hwm),probe.window_tick,probe.window_file,u32(probe.hwm),probe.hwm_tick);
     probe.window_hwm=0;
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
     {u32 h[4]{};native_graphics_indexed_stats(h);k+=std::snprintf(more+k,sizeof(more)-size_t(k)," clut8 live=%u bytes=%u saved=%u",h[0],h[1],h[2]);}
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
     {u32 z[3]{};native_graphics_lazy_stats(z);k+=std::snprintf(more+k,sizeof(more)-size_t(k)," lazy live=%u bytes=%u",z[0],z[1]);}
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
     {u32 z[3]{};native_graphics_padded_stats(z);k+=std::snprintf(more+k,sizeof(more)-size_t(k)," padded live=%u bytes=%u drawn=%u",z[0],z[1],z[2]);}
#endif
     if(k>0&&size_t(k)<sizeof(more)-1){more[k++]='\n';const SceUID g=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);if(g>=0){sceIoWrite(g,more,size_t(k));sceIoClose(g);}}}
#endif
#endif
}
// An allocation failure ends the run (GE build): continuing with a null froze
// the Go at the stage 3 -> 4 load until it slept (2026-10-01). The progress
// file keeps the heap state; the headless build keeps its screen instead.
extern "C" void ge_shutdown_for_exit(void);extern "C" void th10_audio_shutdown(void);
#if defined(__PSP__) && defined(TH10_STACK_PROBE) && TH10_STACK_PROBE
// PSP-1000 lane: how deep does the main thread's stack get? The unused part
// below the stack pointer is painted at start; the result counts how much of
// the paint the run overwrote (decides whether TH10_MAIN_STACK_KB can shrink).
namespace stack_probe {
constexpr u32 paint=0x5ca1ab1eu;u32* bottom=nullptr;u32* top=nullptr;u32 size=0;
void start(){SceKernelThreadInfo info{};info.size=sizeof(info);if(sceKernelReferThreadStatus(sceKernelGetThreadId(),&info)<0)return;
    bottom=static_cast<u32*>(info.stack);size=u32(info.stackSize);top=bottom+size/4;u32 sp;asm volatile("move %0,$sp":"=r"(sp));
    u32* end=reinterpret_cast<u32*>(sp-4096u);for(u32* p=bottom+16;p<end;++p)*p=paint;}
u32 used(){if(!bottom)return 0;const u32* p=bottom+16;while(p<top&&*p==paint)++p;return u32(reinterpret_cast<const unsigned char*>(top)-reinterpret_cast<const unsigned char*>(p));}
}
#endif
void fail_stop(){
#if defined(__PSP__) && defined(TH10_GE)
#if TH10_REC
    th10_rec_shutdown();   // finish the recording (idx1 + header) before the GE and the ME stop
#endif
    // The normal exit's order (renderer, then the ME audio; proven on the Go):
    // leaving with GE4 in 4 MiB mode and its power lock held could keep the
    // XMB from sleeping.
    ge_shutdown_for_exit();th10_audio_shutdown();
#if defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    th10_volatile_shutdown();
#endif
    sceKernelExitGame();
#elif defined(__PSP__)
    sceKernelSleepThread();
#endif
}
void out_of_memory(){screen_row(7,"ALLOC FAIL (operator new) tick %u",probe.tick);write_progress("operator new failed");fail_stop();std::abort();}
}
// Called from platform/FileSystem.cpp and the large allocators (TH_NATIVE_PLATFORM).
extern "C" void th10_note_file(const char* name,u32 bytes){
    std::snprintf(probe.file,sizeof(probe.file),"%s",name?name:"?");probe.file_bytes=bytes;screen_row(3,"file %s %u B (tick %u)",probe.file,bytes,probe.tick);}
extern "C" void th10_note_alloc(u32 bytes,const void* result){
    if(!result){++probe.fails;probe.fail_bytes=bytes;screen_row(7,"ALLOC FAIL %u B at tick %u",bytes,probe.tick);write_progress("allocation failed");fail_stop();return;}
    if(bytes>=64u*1024u){auto& b=probe.recent[probe.recent_count++%12];b.tick=probe.tick;b.bytes=bytes;std::snprintf(b.file,sizeof(b.file),"%s",probe.file);}
#if defined(TH10_HEAP_HWM) && TH10_HEAP_HWM
    probe.note_heap(heap_in_use(),probe.tick,probe.file);
#endif
    if(bytes>=256u*1024u){probe.big_alloc=bytes;probe.big_tick=probe.tick;screen_row(4,"alloc %u KiB (tick %u)",bytes>>10,probe.tick);}}

// ---- SoftFloat fallback / libm trig counters (linker --wrap, CMakeLists.txt) ----
// Per-function totals, plus optional call-site sampling (TH10_PROFILE=1):
// the backtrace above the wrapper is folded into a histogram so hot callers
// can be resolved with addr2line afterwards.
#include <map>
#include <array>
namespace {
enum Counter {F32_TO_EXT,F64_TO_EXT,EXT_TO_F32,EXT_TO_F64,EXT_TO_I64,EXT_ADD,EXT_SUB,EXT_MUL,EXT_DIV,EXT_ROUND,EXT_SQRT,EXT_LT,EXT_EQ,
    SOFT_COUNT,SIN=SOFT_COUNT,COS,TAN,ACOS,ATAN2,FMOD,ALL_COUNT};
const char* counter_names[ALL_COUNT]{"f32_to_extF80","f64_to_extF80","extF80_to_f32","extF80_to_f64","extF80_to_i64","add","sub","mul","div","roundToInt","sqrt","lt","eq",
    "sin","cos","tan","acos","atan2","fmod"};
u64 counts[ALL_COUNT]{},soft_calls=0,trig_calls=0;bool profiling=false;
using Frames=std::array<void*,6>;std::map<std::pair<int,Frames>,u64> sites;
inline void count(Counter c){++counts[c];if(c<SOFT_COUNT)++soft_calls;else ++trig_calls;
#if TH10_HAS_BACKTRACE
    if(profiling){void* raw[8]{};const int n=backtrace(raw,8);Frames f{};for(int i=2;i<n&&i<8;i++)f[i-2]=raw[i];++sites[{int(c),f}];}
#endif
}
}
// TH10_CALL_COUNTERS=0 (PSP timing builds): no --wrap layer at all, the
// game calls SoftFloat/libm directly. Counts come from the PC build.
#ifndef TH10_CALL_COUNTERS
#define TH10_CALL_COUNTERS 1
#endif
// PC float-exception census (TH10_FPE, below): libm runs with the SSE
// exceptions masked and its flags discarded, because doubles are soft-float
// on PSP and cannot raise FPU exceptions there.
#if TH10_HAS_BACKTRACE
namespace {struct FpeQuiet {u32 csr;FpeQuiet():csr(_mm_getcsr()){_mm_setcsr(csr|0x1f80u);}~FpeQuiet(){_mm_setcsr(csr);}};}
#define TH10_FPE_QUIET FpeQuiet quiet_fpe;
#else
#define TH10_FPE_QUIET
#endif
#if TH10_CALL_COUNTERS
#define TH10_WRAP(id,ret,name,params,args) extern "C" ret __real_##name params; extern "C" ret __wrap_##name params {count(id);TH10_FPE_QUIET return __real_##name args;}
#else
#define TH10_WRAP(id,ret,name,params,args)
#endif
struct SoftExtended {u64 significand;u16 exponent,reserved16;u32 reserved32;};
struct SoftBits32 {u32 value;};struct SoftBits64 {u64 value;};
TH10_WRAP(F32_TO_EXT,void,f32_to_extF80M,(SoftBits32 a,SoftExtended* r),(a,r))
TH10_WRAP(F64_TO_EXT,void,f64_to_extF80M,(SoftBits64 a,SoftExtended* r),(a,r))
TH10_WRAP(EXT_TO_F32,SoftBits32,extF80M_to_f32,(const SoftExtended* a),(a))
TH10_WRAP(EXT_TO_F64,SoftBits64,extF80M_to_f64,(const SoftExtended* a),(a))
TH10_WRAP(EXT_TO_I64,i64,extF80M_to_i64,(const SoftExtended* a,u8 m,bool e),(a,m,e))
TH10_WRAP(EXT_ADD,void,extF80M_add,(const SoftExtended* a,const SoftExtended* b,SoftExtended* r),(a,b,r))
TH10_WRAP(EXT_SUB,void,extF80M_sub,(const SoftExtended* a,const SoftExtended* b,SoftExtended* r),(a,b,r))
TH10_WRAP(EXT_MUL,void,extF80M_mul,(const SoftExtended* a,const SoftExtended* b,SoftExtended* r),(a,b,r))
TH10_WRAP(EXT_DIV,void,extF80M_div,(const SoftExtended* a,const SoftExtended* b,SoftExtended* r),(a,b,r))
TH10_WRAP(EXT_ROUND,void,extF80M_roundToInt,(const SoftExtended* a,u8 m,bool e,SoftExtended* r),(a,m,e,r))
TH10_WRAP(EXT_SQRT,void,extF80M_sqrt,(const SoftExtended* a,SoftExtended* r),(a,r))
TH10_WRAP(EXT_LT,bool,extF80M_lt_quiet,(const SoftExtended* a,const SoftExtended* b),(a,b))
TH10_WRAP(EXT_EQ,bool,extF80M_eq,(const SoftExtended* a,const SoftExtended* b),(a,b))
// game/GameMath.cpp takes sine/cosine/tangent/arccosine/atan2/fmod from libm
// in double precision. On PSP (no double FPU) each one is soft-double.
TH10_WRAP(SIN,double,sin,(double x),(x))
TH10_WRAP(COS,double,cos,(double x),(x))
TH10_WRAP(TAN,double,tan,(double x),(x))
TH10_WRAP(ACOS,double,acos,(double x),(x))
TH10_WRAP(ATAN2,double,atan2,(double y,double x),(y,x))
#if TH10_CALL_COUNTERS && defined(TH10_STATIC_I386_FMOD)
// glibc 2.39's static i386 libm exports only __ieee754_fmod.
extern "C" double __ieee754_fmod(double,double);
extern "C" double __wrap_fmod(double x,double y){count(FMOD);TH10_FPE_QUIET return __ieee754_fmod(x,y);}
#else
TH10_WRAP(FMOD,double,fmod,(double x,double y),(x,y))
#endif

// ---- heap attribution (TH10_HEAP=1): malloc family is wrapped at link time;
// live bytes are attributed to the call site above operator new/malloc, and
// the per-site table is snapshotted whenever the live total sets a new peak
// (in 1 MiB steps). Internal allocations of the tracker pass through.
#include <unordered_map>
namespace {
bool heap_tracking=false,heap_guard=false;
#if TH10_HAS_BACKTRACE
struct Block {size_t size;u32 site;};
std::unordered_map<void*,Block>* blocks=nullptr;std::unordered_map<u32,Frames>* site_frames=nullptr;
std::unordered_map<u32,size_t>* site_live=nullptr;std::unordered_map<u32,size_t> peak_sites;
size_t live_total=0,live_peak=0,snapshot_peak=0;u32 live_peak_tick=0,current_tick=0;
u32 site_of(){void* raw[9]{};const int n=backtrace(raw,9);Frames f{};for(int i=2;i<n&&i<8;i++)f[i-2]=raw[i];
    u32 h=2166136261u;for(auto* p:f){auto v=reinterpret_cast<uintptr_t>(p);for(int b=0;b<4;b++){h^=(v>>(8*b))&255;h*=16777619u;}}
    (*site_frames)[h]=f;return h;}
void track_alloc(void* p,size_t size){if(!heap_tracking||heap_guard||!p)return;heap_guard=true;
    const u32 site=site_of();(*blocks)[p]={size,site};(*site_live)[site]+=size;live_total+=size;
    if(live_total>live_peak){live_peak=live_total;live_peak_tick=current_tick;if(live_peak>=snapshot_peak+(1u<<20)){peak_sites=*site_live;snapshot_peak=live_peak;}}
    heap_guard=false;}
void track_free(void* p){if(!heap_tracking||heap_guard||!p)return;heap_guard=true;
    auto it=blocks->find(p);if(it!=blocks->end()){(*site_live)[it->second.site]-=it->second.size;live_total-=it->second.size;blocks->erase(it);}
    heap_guard=false;}
}
extern "C" {
void* __real_malloc(size_t);void __real_free(void*);void* __real_calloc(size_t,size_t);void* __real_realloc(void*,size_t);
int __real_posix_memalign(void**,size_t,size_t);void* __real_aligned_alloc(size_t,size_t);void* __real_memalign(size_t,size_t);
void* __wrap_malloc(size_t n){void* p=__real_malloc(n);track_alloc(p,n);return p;}
void __wrap_free(void* p){track_free(p);__real_free(p);}
void* __wrap_calloc(size_t a,size_t b){void* p=__real_calloc(a,b);track_alloc(p,a*b);return p;}
void* __wrap_realloc(void* old,size_t n){track_free(old);void* p=__real_realloc(old,n);track_alloc(p,n);return p;}
int __wrap_posix_memalign(void** out,size_t a,size_t n){const int r=__real_posix_memalign(out,a,n);if(!r)track_alloc(*out,n);return r;}
void* __wrap_aligned_alloc(size_t a,size_t n){void* p=__real_aligned_alloc(a,n);track_alloc(p,n);return p;}
void* __wrap_memalign(size_t a,size_t n){void* p=__real_memalign(a,n);track_alloc(p,n);return p;}
}
#else
}
namespace {struct Block {size_t size;u32 site;};std::unordered_map<void*,Block>* blocks=nullptr;std::unordered_map<u32,Frames>* site_frames=nullptr;
std::unordered_map<u32,size_t>* site_live=nullptr;std::unordered_map<u32,size_t> peak_sites;size_t live_peak=0,snapshot_peak=0;u32 live_peak_tick=0,current_tick=0;}
#endif

// ---- float exceptions (PC, TH10_FPE=count|trap) ----
// The real PSP FPU traps on overflow, divide-by-zero and invalid operation
// (thread default FCR31 enables them) unless pspSdkDisableFPUExceptions()
// runs; PPSSPP never raises them. count: per-tick SSE flags O/Z/V after each
// step; trap: unmask them and report the first faulting instruction.
#if TH10_HAS_BACKTRACE
namespace {
int fpe_mode=0;struct FpeEvent {u32 tick,flags;};std::vector<FpeEvent> fpe_events;u32 fpe_ticks=0;
void fpe_handler(int,siginfo_t* info,void* context){
    const auto* uc=static_cast<const ucontext_t*>(context);const auto eip=unsigned(uc->uc_mcontext.gregs[REG_EIP]);
    const auto* code=reinterpret_cast<const u8*>(uintptr_t(eip));char line[200];
    const int n=std::snprintf(line,sizeof(line),"SIGFPE tick %u si_code %d eip %#x bytes %02x %02x %02x %02x %02x %02x\n",current_tick,info->si_code,eip,code[0],code[1],code[2],code[3],code[4],code[5]);
    if(write(2,line,size_t(n))<0){}void* raw[16];const int m=backtrace(raw,16);backtrace_symbols_fd(raw,m,2);_exit(3);}
void fpe_start(){if(const char* v=std::getenv("TH10_FPE"))fpe_mode=!std::strcmp(v,"trap")?2:1;if(!fpe_mode)return;
    u32 csr=_mm_getcsr()&~0x3fu;
    if(fpe_mode==2){struct sigaction sa{};sa.sa_sigaction=fpe_handler;sa.sa_flags=SA_SIGINFO;sigaction(SIGFPE,&sa,nullptr);csr&=~((1u<<7)|(1u<<9)|(1u<<10));}
    _mm_setcsr(csr);}
void fpe_tick(u32 tick){if(fpe_mode!=1)return;const u32 csr=_mm_getcsr(),f=csr&0x0du;if(f){++fpe_ticks;if(fpe_events.size()<64)fpe_events.push_back({tick,f});}_mm_setcsr(csr&~0x3fu);}
}
#endif

// ---- scripted input (--input FILE): lines "<tick> <KeyCode> <ticks>" using
// the KeyboardMap.inc codes (KeyZ, KeyX, ArrowUp, Escape, ShiftLeft, ...).
namespace {
struct Key {const char* code;const char* sdl;u32 scan,vk;bool hosted=false;};
#include "../portable/input/KeyboardMap.inc"
struct Press {u32 begin,end,scan,vk;};std::vector<Press> presses;
bool load_script(const char* path){FILE* f=std::fopen(path,"r");if(!f)return false;char line[256];
    while(std::fgets(line,sizeof(line),f)){if(line[0]=='#'||line[0]=='\n')continue;unsigned tick=0,len=1;char name[64]{};
        if(std::sscanf(line,"%u %63s %u",&tick,name,&len)<2)continue;const Key* k=nullptr;for(const auto& e:keyboard_map)if(!std::strcmp(e.code,name))k=&e;
        if(!k){std::fprintf(stderr,"unknown key %s\n",name);std::fclose(f);return false;}presses.push_back({tick,tick+len,k->scan,k->vk});}
    std::fclose(f);return true;}
void apply_input(InputSnapshot& s,u32 tick){for(const auto& p:presses)if(tick>=p.begin&&tick<p.end){s.scan_keys[p.scan]=128;s.virtual_keys[p.vk]=128;if(p.vk>=160&&p.vk<=165)s.virtual_keys[16+(p.vk-160)/2]=128;}}
void press(InputSnapshot& s,const Key* k){if(!k)return;s.scan_keys[k->scan]=128;s.virtual_keys[k->vk]=128;if(k->vk>=160&&k->vk<=165)s.virtual_keys[16+(k->vk-160)/2]=128;}
const Key* key_named(const char* code){for(const auto& e:keyboard_map)if(!std::strcmp(e.code,code))return &e;return nullptr;}
#ifdef TH10_GE
// PSP pad -> the keyboard keys the game reads (no input script given).
struct PadKey {u32 mask;const char* code;const Key* key;};
// Same layout as the TH08 PSP port: Cross shot/OK, Circle bomb/cancel,
// Square/L/R focus, Triangle skip dialogue, Start pause.
PadKey pad_keys[]{{PSP_CTRL_UP,"ArrowUp",nullptr},{PSP_CTRL_DOWN,"ArrowDown",nullptr},{PSP_CTRL_LEFT,"ArrowLeft",nullptr},{PSP_CTRL_RIGHT,"ArrowRight",nullptr},
    {PSP_CTRL_CROSS,"KeyZ",nullptr},{PSP_CTRL_CIRCLE,"KeyX",nullptr},{PSP_CTRL_SQUARE|PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER,"ShiftLeft",nullptr},
    {PSP_CTRL_TRIANGLE,"ControlLeft",nullptr},{PSP_CTRL_START,"Escape",nullptr}};
void pad_start(){sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);for(auto& p:pad_keys)p.key=key_named(p.code);}
void pad_input(InputSnapshot& s){SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);u32 b=pad.Buttons;
    if(pad.Lx<48)b|=PSP_CTRL_LEFT;else if(pad.Lx>208)b|=PSP_CTRL_RIGHT;if(pad.Ly<48)b|=PSP_CTRL_UP;else if(pad.Ly>208)b|=PSP_CTRL_DOWN;
    for(const auto& p:pad_keys)if(b&p.mask)press(s,p.key);}
// One game tick per vblank (59.94 Hz). A tick that overruns its vblank is
// caught up by skipping waits; more than 8 behind drops the backlog.
// TH10_PACING_NO_CATCHUP (TH08 r262, src/main.cpp:189-236 sha256 56fe54ef:
// "a late frame never waits here" and nothing is made up): a late tick moves
// the schedule to now, so the game slows down like the original instead of
// running faster than 60 Hz to catch up (the TH08 r260 TICK_SPIKE report).
#ifndef TH10_PACING_NO_CATCHUP
#define TH10_PACING_NO_CATCHUP 0
#endif
extern "C" void ge_poll(void);   // psp/GeRenderer.cpp: request the flip once the GE is done
struct Pacer {u32 base=0,late=0,dropped=0,slipped=0;void start(u32 tick){base=sceDisplayGetVcount()-tick;}
    void wait(u32 tick){ge_poll();const u32 target=base+tick+1;const i32 behind=i32(sceDisplayGetVcount()-target);
#if TH10_PACING_NO_CATCHUP
        if(behind>0){++late;base+=u32(behind);slipped+=u32(behind);return;}
#else
        if(behind>0){++late;if(behind>8){base+=u32(behind);++dropped;}return;}
#endif
        while(i32(sceDisplayGetVcount()-target)<0){sceDisplayWaitVblankStart();ge_poll();}}} pacer;
#endif
}

namespace {
struct Options {u32 ticks=20000,seed=0,chinese=0,every=1,hash=1,progress=600,ge_debug=0,pad=1;std::string trace="th10_trace.txt",input,result="th10_result.txt",dump_entry,dump_to,data;int frameskip=-1;};
u64 fnv(u64 h,const void* bytes,size_t size){const auto* p=static_cast<const u8*>(bytes);for(size_t i=0;i<size;i++){h^=p[i];h*=1099511628211ull;}return h;}
bool parse(Options& o,const std::vector<std::string>& args){
    for(size_t i=0;i+1<args.size();i+=2){const auto& k=args[i];const char* v=args[i+1].c_str();
        if(k=="--ticks")o.ticks=u32(std::strtoul(v,nullptr,0));else if(k=="--seed")o.seed=u32(std::strtoul(v,nullptr,0));
        else if(k=="--chinese")o.chinese=u32(std::strtoul(v,nullptr,0));else if(k=="--every")o.every=std::max(1u,u32(std::strtoul(v,nullptr,0)));
        else if(k=="--trace")o.trace=v;else if(k=="--input")o.input=v;else if(k=="--result")o.result=v;
        else if(k=="--frameskip")o.frameskip=int(std::strtol(v,nullptr,0));else if(k=="--progress")o.progress=u32(std::strtoul(v,nullptr,0));else if(k=="--ge-debug")o.ge_debug=u32(std::strtoul(v,nullptr,0));
        else if(k=="--dump-entry"){o.dump_entry=v;if(i+2<args.size()){o.dump_to=args[i+2];++i;}}else if(k=="--hash")o.hash=u32(std::strtoul(v,nullptr,0));
        else if(k=="--data")o.data=v;   // game files (th10.dat, thbgm.dat, fonts) from another folder
        else if(k=="--pad")o.pad=u32(std::strtoul(v,nullptr,0));   // 0: ignore the pad (unattended demo runs; HOME still exits)
        else{std::fprintf(stderr,"unknown option %s\n",k.c_str());return false;}}
    return true;}
// Present-to-present windows: the ticks since the previous present, ending
// with the tick that drew. Draw 30 (frameskip 1) must fit every window in
// 2 x 16.667 ms; one overshoot costs a whole extra vsync (MISS).
struct Window {u32 tick,us,ticks;i32 screen,stage;u32 wall_us;};   // us = step (compute) time, wall_us = present to present
}
extern "C" void headless_set_hash(bool);
#ifdef TH10_VERTEX_PEAK
extern "C" {extern u32 th10_vertex_peak_batch,th10_vertex_peak_frame;}   // game/AnmRenderer.cpp (PC measurement)
#endif
#ifdef TH10_TRIG_MEMO_PROBE
extern "C" {extern unsigned long long th10_memo_calls,th10_memo_repeats;}   // game/BulletFeatures.cpp (PC measurement)
#if defined(TH10_TRIG_MEMO) && TH10_TRIG_MEMO
extern "C" {extern unsigned long long th10_polar_memo_calls,th10_polar_memo_hits;}   // game/GameMath.cpp
#endif
unsigned long long memo_tick_max=0,memo_tick_max_repeats=0;unsigned memo_tick_max_tick=0;
#endif
#if !defined(__PSP__) && defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// th10_port audit of TH10_ANM_STREAM_LOAD (PC, TH10_ANM_CHECK=1 in the
// environment): every .anm of the archive is loaded through the whole-file
// path and through the stream loader into slot 32. The file bytes left after
// compact(), every texture (description, bytes per pixel, all pixel rows as
// the game would lock them), the sprites (texture as an index) and the script
// offsets must be identical. Exit code: the number of files that differ.
namespace {
// With TH10_TEXTURE_CLUT8 the first load is also 16-bit only and the second
// keeps textures of 256 colours or fewer as CLUT8: locking one turns it back
// into 16 bit, which must equal the 16-bit texture (and is counted, expanded=).
// Per texture of the second load: CLUT8 or not, and its distinct 16-bit values.
struct AnmSnapshot {std::vector<u8> loaded,sprites;std::vector<u32> textures,scripts,indexed,colours,sizes;std::vector<std::vector<u8>> pixels;};
AnmSnapshot anm_snapshot(AnimationEngine& engine,AnmFile& file){
    AnmSnapshot s;auto& t=engine.resources.textures;s.loaded.assign(file.loaded,file.loaded+file.loaded_size);
    for(i32 i=0;i<file.texture_count;++i){const auto& texture=file.textures[i];s.textures.push_back(texture.bytes_per_pixel);s.textures.push_back(texture.handle?1:0);if(!texture.handle){s.pixels.emplace_back();s.indexed.push_back(0);s.colours.push_back(0);s.sizes.push_back(0);continue;}
        void* surface=t.get_surface(texture.handle);
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
        s.indexed.push_back(u32(native_graphics_surface_indexed(surface)));
#else
        s.indexed.push_back(0);
#endif
        const auto d=t.describe_surface(surface);const auto lock=t.lock_surface(surface);
        s.textures.push_back(d.format);s.textures.push_back(d.width);s.textures.push_back(d.height);s.textures.push_back(u32(lock.pitch));
        s.pixels.emplace_back(lock.pixels,lock.pixels+size_t(lock.pitch)*d.height);s.sizes.push_back(d.width|(d.height<<16));
        u32 distinct=0;if(u32(lock.pitch)==d.width*2){std::vector<bool> seen(65536);for(u32 k=0;k<d.width*d.height;++k){u16 v;std::memcpy(&v,lock.pixels+k*2,2);if(!seen[v]){seen[v]=true;++distinct;}}}
        s.colours.push_back(distinct);t.unlock_surface(surface);t.release_surface(surface);}
    for(i32 i=0;i<file.sprite_count;++i){AnmSprite sprite=file.sprites[i];u32 index=~0u;for(i32 k=0;k<file.texture_count;++k)if(file.textures[k].handle==sprite.texture)index=u32(k);
        sprite.texture=reinterpret_cast<void*>(uintptr_t(index));const auto* b=reinterpret_cast<const u8*>(&sprite);s.sprites.insert(s.sprites.end(),b,b+sizeof(sprite));}
    for(i32 i=0;i<file.script_count;++i)s.scripts.push_back(u32(reinterpret_cast<const u8*>(file.scripts[i])-file.loaded));
    return s;
}
int anm_stream_audit(AnimationEngine& engine,FileSystem& files){
    int differing=0,checked=0;u64 load_us[2]{};
    // Every packed entry: the whole-entry decode against the stream read in
    // random pieces (1..70000 bytes), from the same starting dictionary; the
    // bytes and the shared dictionary each leaves behind must be identical.
    {int entries=0,bad=0;u32 seed=12345u;auto random=[&seed](){seed=seed*1103515245u+12345u;return seed>>8;};
     for(i32 i=0;i<files.resources.count;++i){const auto& e=files.resources.entries[i];if(e.size==files.resources.entries[i+1].offset-e.offset)continue;
        std::vector<u8> start(files.archives.dictionary,files.archives.dictionary+8192),whole(e.size);
        const bool ok=files.resources.read(e.name,whole.data(),files.archives)!=nullptr;std::vector<u8> left(files.archives.dictionary,files.archives.dictionary+8192);
        std::memcpy(files.archives.dictionary,start.data(),8192);
        std::vector<u8> streamed;bool failed=true;
        if(auto* in=files.resources.open_stream(e.name,files.archives)){for(;;){std::vector<u8> piece(1+random()%70000);const u32 got=in->read(piece.data(),u32(piece.size()));streamed.insert(streamed.end(),piece.begin(),piece.begin()+got);if(got<piece.size())break;}failed=in->failed();in->close();}
        const bool same=ok&&!failed&&streamed==whole&&std::memcmp(files.archives.dictionary,left.data(),8192)==0;
        if(!same){++bad;std::printf("lzss-check %s DIFFERENT (whole ok=%d stream failed=%d bytes %zu/%zu)\n",e.name,ok,failed,streamed.size(),whole.size());}
        ++entries;}
     std::printf("lzss-check packed entries=%d differing=%d\n",entries,bad);differing+=bad;}
    for(i32 i=0;i<files.resources.count;++i){
        const char* name=files.resources.entries[i].name;const size_t n=std::strlen(name);if(n<4||std::strcmp(name+n-4,".anm"))continue;
        AnmSnapshot snap[2];bool loaded[2]{};const auto before=anm_stream_stats();
        for(int way=0;way<2;++way){anm_stream_force_whole(way==0);
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
            th10_indexed_enable(way==1);
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
            th10_padded_enable(way==1);   // the reference load makes every texture at once
#endif
            const u64 t0=now_us();auto* file=engine.manager.load(32,name,engine.resources);load_us[way]+=now_us()-t0;
            if(file){loaded[way]=true;snap[way]=anm_snapshot(engine,*file);}
            engine.manager.unload(32,engine.resources);}
        anm_stream_force_whole(false);const auto after=anm_stream_stats();
        for(size_t k=0;k<snap[1].indexed.size();++k)std::printf("anm-tex %s %zu %ux%u format=%u clut8=%u colours=%u\n",name,k,snap[1].sizes[k]&0xffff,snap[1].sizes[k]>>16,k*6+2<snap[1].textures.size()?snap[1].textures[k*6+2]:0,snap[1].indexed[k],snap[1].colours[k]);
        const bool same=loaded[0]&&loaded[1]&&snap[0].loaded==snap[1].loaded&&snap[0].textures==snap[1].textures&&snap[0].pixels==snap[1].pixels&&snap[0].sprites==snap[1].sprites&&snap[0].scripts==snap[1].scripts;
        size_t pixel_bytes=0;for(const auto& p:snap[1].pixels)pixel_bytes+=p.size();
        std::printf("anm-check %-16s %s streamed=%u aborted=%u kept=%zu textures=%zu pixel_bytes=%zu\n",name,same?"same":"DIFFERENT",after.streamed-before.streamed,after.aborted-before.aborted,snap[1].loaded.size(),snap[1].pixels.size(),pixel_bytes);
        if(!same)++differing;++checked;
    }
    // The abort path: title.anm stopped after 3 textures must come back from
    // the whole-file path exactly as it loads there.
    {AnmSnapshot snap[2];bool loaded[2]{};const auto before=anm_stream_stats();
     for(int way=0;way<2;++way){anm_stream_force_whole(way==0);if(way)anm_stream_fault_after(3);
        if(auto* file=engine.manager.load(32,"title.anm",engine.resources)){loaded[way]=true;snap[way]=anm_snapshot(engine,*file);}
        engine.manager.unload(32,engine.resources);}
     anm_stream_force_whole(false);anm_stream_fault_after(~0u);const auto after=anm_stream_stats();
     const bool same=loaded[0]&&loaded[1]&&snap[0].loaded==snap[1].loaded&&snap[0].textures==snap[1].textures&&snap[0].pixels==snap[1].pixels&&snap[0].sprites==snap[1].sprites&&snap[0].scripts==snap[1].scripts;
     std::printf("anm-check abort path title.anm %s aborted=%u streamed=%u\n",same?"same":"DIFFERENT",after.aborted-before.aborted,after.streamed-before.streamed);if(!same||after.aborted-before.aborted!=1)++differing;}
    const auto st=anm_stream_stats();std::printf("anm-check files=%d differing=%d streamed=%u whole=%u aborted=%u scratch_peak=%u two_pass=%u load_us whole=%llu stream=%llu\n",checked,differing,st.streamed,st.whole,st.aborted,st.scratch_peak,st.two_pass,(unsigned long long)load_us[0],(unsigned long long)load_us[1]);
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    {u32 c[3]{},z[3]{};th10_padded_counts(c);native_graphics_padded_stats(z);std::printf("anm-check padded created indexed=%u 16bit=%u refused=%u, given bytes at the lock=%u, left without=%u\n",c[0],c[1],c[2],z[2],z[0]);}
#endif
    return differing;
}
}
#endif
extern "C" unsigned th10_current_tick(void);
#if TH10_ANM_POOL_STATS
extern "C" void th10_anm_pool_stats(unsigned*);
#endif
#ifdef TH10_GE
extern "C" void ge_renderer_counts(unsigned*);
#ifdef TH10_LIST_KB
extern "C" void ge_list_stats(unsigned*);
#endif
extern "C" void ge_set_debug(unsigned);
extern "C" void ge_sample(char*,unsigned);
extern "C" void ge_frame_stats(char*,unsigned);
extern "C" u32 ge_last_frame_cpu_us(void);   // psp/GeRenderer.cpp: renderer CPU time of the last presented frame
extern "C" unsigned headless_font_missing_glyphs(void);
extern "C" void th10_audio_shutdown(void);
extern "C" void th10_audio_stats(char*,unsigned);
extern "C" void headless_audio_mix_stats(unsigned*);
#if TH10_SE_EDRAM
extern "C" void th10_se_stats(char*,unsigned);   // psp/MeAudio.cpp: sound effects in the ME eDRAM
#endif
#endif

namespace {volatile unsigned tick_now=0;}
// ---- per-callback CPU time (TH10_CHAIN_TIMING: PSP GE build, PC sample build) ----
// chain_token is the running callback (+128 while drawing, -1 outside); the
// PC sampler (TH10_SAMPLER) files every sample under it.
#ifdef TH10_CHAIN_TIMING
#include "CallbackNames.inc"
namespace {u64 chain_us[2][128];u32 chain_calls[2][128];volatile int chain_token=-1;int chain_stack[8];u32 chain_depth=0;
#ifndef TH10_WINDOW_BREAKDOWN
#define TH10_WINDOW_BREAKDOWN 0
#endif
#if TH10_WINDOW_BREAKDOWN
// Per-window callback times (TH08 per-window PERF lines, docs/CHECKLIST.md R.9):
// the five heaviest game windows keep their top callbacks for the result file.
u32 window_chain_us[2][128];
struct HeavyWindow {u32 tick,us,ticks,outside,draws,renderer_us;u16 token[8];u32 cost[8];};
HeavyWindow heavy[5];u32 heavy_count=0;
// The five heaviest game windows under 250 ms (play, not stage loading: a
// loading window takes 0.4-2.3 s on the Go and fills the list above).
HeavyWindow heavy_play[5];u32 heavy_play_count=0;
#endif
#ifdef __PSP__
u32 chain_clock(){return sceKernelGetSystemTimeLow();}
#else
u32 chain_clock(){return u32(now_us());}
#endif
}
#ifdef TH10_GE
extern "C" void ge_poll(void);
#endif
#if TH10_SOFTFLOAT_CENSUS
// psp/SoftfloatCensus.cpp (make CENSUS=1): the running chain callback for the counts.
extern "C" {volatile int th10_census_token=-1;void th10_census_report(void (*add)(const char*),const char* const* names,unsigned name_count);}
#endif
extern "C" u32 th10_chain_begin(bool drawing,u32 token){
#ifdef TH10_GE
    ge_poll();   // request the pending flip as soon as the GE is done (TH08 native_ge: Poll after every Kick/Submit)
#endif
    if(chain_depth<8)chain_stack[chain_depth]=chain_token;++chain_depth;chain_token=token<128?int(token)+(drawing?128:0):-1;
#if TH10_SOFTFLOAT_CENSUS
    th10_census_token=chain_token;
#endif
    return chain_clock();}
extern "C" void th10_chain_end(bool drawing,u32 token,u32 started){const u32 us=chain_clock()-started;if(chain_depth)--chain_depth;chain_token=chain_depth<8?chain_stack[chain_depth]:-1;
#if TH10_SOFTFLOAT_CENSUS
    th10_census_token=chain_token;
#endif
    if(token<128){chain_us[drawing?1:0][token]+=us;++chain_calls[drawing?1:0][token];
#if TH10_WINDOW_BREAKDOWN
        window_chain_us[drawing?1:0][token]+=us;
#endif
    }}
#if TH10_WINDOW_BREAKDOWN
// Called when a window closes: keep it if it is among the five heaviest game windows.
void keep_heavy(HeavyWindow* list,u32& count,u32 tick,u32 us,u32 ticks){
    {u32 slot=count<5?count:5;
        if(slot==5){u32 lightest=0;for(u32 i=1;i<5;i++)if(list[i].us<list[lightest].us)lightest=i;if(list[lightest].us<us)slot=lightest;}
        if(slot<5){HeavyWindow h{};h.tick=tick;h.us=us;h.ticks=ticks;u64 inside=0;
            if(const auto* r=touhou::sdl::current())h.draws=r->presented_draws;
#ifdef TH10_GE
            h.renderer_us=ge_last_frame_cpu_us();
#endif
            for(int k=0;k<8;k++){u32 best=0,best_t=0xffff;for(int d=0;d<2;d++)for(int t=0;t<128;t++){const u32 v=window_chain_us[d][t];bool used=false;for(int j=0;j<k;j++)if(h.token[j]==u32(t+d*128))used=true;if(!used&&v>best){best=v;best_t=u32(t+d*128);}}
                if(best_t==0xffff)break;h.token[k]=u16(best_t);h.cost[k]=best;}
            for(int d=0;d<2;d++)for(int t=0;t<128;t++)inside+=window_chain_us[d][t];h.outside=us>inside?u32(us-inside):0;
            list[slot]=h;if(count<5)++count;}}
}
#if TH10_MISS_BREAKDOWN
// Per stage, the callbacks' time summed over the MISS windows (a game window
// longer than its ticks x 16.667 ms, the "game windows ... miss=" count), so a
// live play shows what fills the MISS band; the heavy lists above keep only
// the five worst windows (stage loads, dialogue). Stage from the call site.
struct MissStage {u32 windows=0,misses=0;u64 us=0,miss_us=0,chain[2][128]{};};
MissStage miss_stages[8];u32 miss_stage=0;
#endif
void th10_window_close(u32 tick,u32 us,u32 ticks,bool game){
    if(game){keep_heavy(heavy,heavy_count,tick,us,ticks);if(us<250000)keep_heavy(heavy_play,heavy_play_count,tick,us,ticks);}
#if TH10_MISS_BREAKDOWN
    if(game){auto& m=miss_stages[miss_stage<8?miss_stage:7];++m.windows;m.us+=us;
        if(us>ticks*16667u){++m.misses;m.miss_us+=us;for(int d=0;d<2;d++)for(int t=0;t<128;t++)m.chain[d][t]+=window_chain_us[d][t];}}
#endif
    std::memset(window_chain_us,0,sizeof(window_chain_us));
}
#endif
#endif
// ---- PC sampling profiler (TH10_SAMPLER, needs TH10_CHAIN_TIMING) ----
// A 20 kHz CLOCK_MONOTONIC timer (the process is single-threaded and CPU
// bound) records the interrupted pc and the frame-pointer chain above it,
// filed under the running chain callback. native/tools/samples.py folds them
// into exclusive/inclusive function profiles per callback.
#if TH10_HAS_BACKTRACE && defined(TH10_SAMPLER) && defined(TH10_CHAIN_TIMING)
#include <ucontext.h>
#include <time.h>
namespace {
constexpr u32 sample_depth=30;
struct Sample {i32 token;u32 depth;u32 pc[sample_depth];};
Sample* samples=nullptr;volatile u32 sample_count=0;u32 sample_cap=0;uintptr_t stack_low=0,stack_high=0;timer_t sample_timer{};
void on_sample(int,siginfo_t*,void* context){
    if(!samples||sample_count>=sample_cap)return;
    const auto* uc=static_cast<const ucontext_t*>(context);Sample& s=samples[sample_count];s.token=chain_token;
    // Live frames lie between the interrupted stack pointer and the top of
    // the main stack; everything in that range is mapped.
    const uintptr_t low=uintptr_t(uc->uc_mcontext.gregs[REG_ESP]);
    uintptr_t fp=uintptr_t(uc->uc_mcontext.gregs[REG_EBP]);u32 n=0;s.pc[n++]=u32(uc->uc_mcontext.gregs[REG_EIP]);
    while(n<sample_depth&&fp>=low&&fp>=stack_low&&fp+8<=stack_high&&!(fp&3)){const auto* frame=reinterpret_cast<const uintptr_t*>(fp);
        const uintptr_t next=frame[0],ret=frame[1];if(!ret)break;s.pc[n++]=u32(ret);if(next<=fp)break;fp=next;}
    s.depth=n;sample_count=sample_count+1;
}
void sampler_start(){
    char here;stack_high=(uintptr_t(&here)+4096)&~uintptr_t(15);stack_low=stack_high-(64u<<20);
    sample_cap=1u<<20;samples=static_cast<Sample*>(std::calloc(sample_cap,sizeof(Sample)));if(!samples)return;
    struct sigaction sa{};sa.sa_sigaction=on_sample;sa.sa_flags=SA_SIGINFO|SA_RESTART;sigemptyset(&sa.sa_mask);sigaction(SIGPROF,&sa,nullptr);
    sigevent ev{};ev.sigev_notify=SIGEV_SIGNAL;ev.sigev_signo=SIGPROF;if(timer_create(CLOCK_MONOTONIC,&ev,&sample_timer)!=0){std::fprintf(stderr,"sampler: timer_create failed\n");return;}
    itimerspec its{};its.it_interval.tv_nsec=50000;its.it_value.tv_nsec=50000;timer_settime(sample_timer,0,&its,nullptr);
}
void sampler_stop(const char* path){
    if(!samples)return;itimerspec off{};timer_settime(sample_timer,0,&off,nullptr);timer_delete(sample_timer);
    if(FILE* f=std::fopen(path,"wb")){const u32 n=sample_count;std::fwrite(&n,4,1,f);std::fwrite(samples,sizeof(Sample),n,f);std::fclose(f);}
    std::fprintf(stderr,"sampler: %u samples -> %s\n",unsigned(sample_count),path);
}
}
#endif
extern "C" unsigned th10_current_tick(void){return tick_now;}
int main(int argc,char** argv){
    Options o;std::vector<std::string> args;
    std::set_new_handler(out_of_memory);
#ifdef __PSP__
    pspSdkDisableFPUExceptions();   // x87 semantics: masked, like TH07/TH08
    start_callbacks();
    scePowerSetClockFrequency(333,333,166);   // as TH07/TH08; ARK applies its overclock on top
#ifndef TH10_GE
    pspDebugScreenInit();
#endif
    {   // Game data and settings live next to EBOOT.PBP.
        std::string dir=argc>0&&argv[0]?argv[0]:"ms0:/PSP/GAME/TH10PSP/EBOOT.PBP";dir=dir.substr(0,dir.find_last_of('/'));
        // The unified launcher (psp_launcher/) starts this PBP as TH10RUNTIME.PBP through LoadExec:
        // "./ge4wrap_texv1.prx" (psp/Ge4.cpp) stays EBOOT-local, as TH07's psp/fileio.cpp does.
        sceIoChdir(dir.c_str());
        setenv("TH10_GAME_DIR",dir.c_str(),1);setenv("TH10_SAVE_DIR",(dir+"/save").c_str(),1);
        o.trace=dir+"/th10_trace.txt";o.result=dir+"/th10_result.txt";
        std::snprintf(probe.path,sizeof(probe.path),"%s/th10_progress.txt",dir.c_str());
#if TH10_RESULT_KEEP_PREV
        // The previous launch's files stay as *_prev.txt: two launches between
        // collections lost the first one's result (30_play, 2026-10-01).
        for(const char* name:{"th10_result","th10_progress","th10_rec_log"}){
            const std::string cur=dir+"/"+name+".txt",prev=dir+"/"+name+"_prev.txt";SceIoStat st;
            if(sceIoGetstat(cur.c_str(),&st)>=0){sceIoRemove(prev.c_str());sceIoRename(cur.c_str(),prev.c_str());}
        }
#endif
        if(FILE* f=std::fopen((dir+"/th10run.txt").c_str(),"r")){char word[256];while(std::fscanf(f,"%255s",word)==1)args.push_back(word);std::fclose(f);}
        // No th10run.txt (the distributed folder): a normal play start, the options build_queue.sh writes for the play slots.
        else args={"--ticks","0","--frameskip","1","--hash","0","--trace","none","--progress","0"};
        screen_row(0,"TH10 PSP headless  dir=%s",dir.c_str());
        probe.mhz=measure_mhz();probe.heap_room_start=heap_room();
#if defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
        th10_volatile_init();   // PSP-1000 lane: 4 MiB more RAM for the sound buffers (heap fallback when it is not lockable)
#endif
#if defined(TH10_STACK_PROBE) && TH10_STACK_PROBE
        stack_probe::start();
#endif
        screen_row(1,"api cpu=%d bus=%d MHz  real %u MHz (spin)",scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt(),probe.mhz);
        write_fcr31(read_fcr31()&~0x7cu);
#if TH10_REC
        th10_rec_init(dir.c_str());   // its buffers before the game's allocations
#endif
    }
#else
    for(int i=1;i<argc;i++)args.push_back(argv[i]);
#endif
    if(!parse(o,args))return 64;
    if(!o.data.empty())setenv("TH10_GAME_DIR",o.data.c_str(),1);
    if(!o.input.empty()&&!load_script(o.input.c_str())){std::fprintf(stderr,"cannot load input script %s\n",o.input.c_str());return 64;}
    headless_set_hash(o.hash!=0);
#if TH10_HAS_BACKTRACE
    profiling=std::getenv("TH10_PROFILE")!=nullptr;
    if(std::getenv("TH10_HEAP")){heap_guard=true;blocks=new std::unordered_map<void*,Block>();site_frames=new std::unordered_map<u32,Frames>();site_live=new std::unordered_map<u32,size_t>();blocks->reserve(1<<20);heap_guard=false;heap_tracking=true;}
#endif
#if TH10_HAS_BACKTRACE
    fpe_start();
#if defined(TH10_SAMPLER) && defined(TH10_CHAIN_TIMING)
    if(std::getenv("TH10_SAMPLE"))sampler_start();
#endif
#endif
    Rng random{},visual{};float rate=1;u32 quitting=0;
    {const u32 rng[]{o.seed,0};std::memcpy(&random,rng,8);std::memcpy(&visual,rng,8);}
    sdl_files_root(o.chinese);
    auto* files=files_create();if(!files||!files_attach(files,o.chinese?"th10c.dat":"th10.dat")){std::fprintf(stderr,"cannot open th10.dat (set TH10_GAME_DIR)\n");screen_row(7,"cannot open th10.dat");return 2;}
    // --dump-entry list -: print every th10.dat entry name and size.
    if(o.dump_entry=="list"){for(i32 i=0;i<files->resources.count;i++)std::printf("%s %u\n",files->resources.entries[i].name,files->resources.entries[i].size);return 0;}
    // --dump-entry NAME FILE: write one th10.dat entry (e.g. a demo replay) and stop.
    if(!o.dump_entry.empty()){const auto* entry=files->resources.find(o.dump_entry.c_str());if(!entry){std::fprintf(stderr,"no entry %s\n",o.dump_entry.c_str());return 3;}
        std::vector<u8> bytes(entry->size);files->read_archive(o.dump_entry.c_str(),bytes.data());
        FILE* f=std::fopen(o.dump_to.c_str(),"wb");if(!f)return 3;std::fwrite(bytes.data(),1,bytes.size(),f);std::fclose(f);std::printf("%s: %u bytes\n",o.dump_entry.c_str(),entry->size);return 0;}
    const u32 parameters[]{640,480,22,1,0,0,1,0,1,1,80,0,0,0};
    auto* input=input_create();auto* state=game_state_create(input,o.chinese);
    auto* device=graphics_create(reinterpret_cast<const GraphicsPresentation*>(parameters),0x40);if(!device){std::fprintf(stderr,"graphics_create failed\n");return 2;}
    auto* animation=animation_engine_create(files,device,&random,&visual,&rate);auto* fonts=fonts_create(device,&random,o.chinese);
    auto* audio=audio_create(files);auto* effects=effects_create(animation,&quitting,nullptr);
    auto* app=application_create(files,input,state,animation,fonts,audio,effects);if(!app){std::fprintf(stderr,"application_create failed\n");return 2;}
#if !defined(__PSP__) && defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    if(std::getenv("TH10_ANM_CHECK"))return anm_stream_audit(*animation,*files);
#endif

    // Trace lines are buffered and written in large blocks (PSP storage
    // dislikes small writes; see the TH08 ef0 stall).
    FILE* trace=o.trace=="none"?nullptr:std::fopen(o.trace.c_str(),"w");std::string tbuf;
#ifdef TH10_TRACE_FLUSH_KB
    // PSP-1000 lane: a 1 MiB buffer grew to 2 MiB of capacity in traced test
    // runs, heap a play (--trace none) never has.
    const size_t trace_flush=size_t(TH10_TRACE_FLUSH_KB)*1024u;if(trace)tbuf.reserve(trace_flush+1024);
#else
    const size_t trace_flush=size_t(1u<<20);
#endif
    if(trace)tbuf+="# tick screen stage score power lives rng rng_calls vis vis_calls econ_hash draw_hash draws soft trig step_us\n";
    double audio_remainder=0;u64 soft_max=0,soft_total=0,trig_max=0,trig_total=0,step_sum=0;u32 soft_max_tick=0,trig_max_tick=0,step_max=0,step_max_tick=0;
    size_t heap_peak=0;u32 heap_peak_tick=0;int result=0;u32 tick=0,window_us=0,window_ticks=0,last_presents=sdl_stats()->presentations;
    // --ticks 0 plays until the game quits; statistics keep the first 20 min.
#ifdef TH10_STAT_CAP
    // PSP-1000 lane: the full cap reserved 1,152,000 B for a --ticks 0 play and
    // the result copied it again at exit (operator new failed in PSPModel=0).
    const u32 stat_cap=TH10_STAT_CAP;std::vector<Window> windows;
#else
    const u32 stat_cap=36000;std::vector<Window> windows;
#endif
    windows.reserve(std::min(o.ticks?o.ticks/2+16:stat_cap,stat_cap));const u64 run_start=now_us();
    // Game-screen ticks split by whether they presented: calc-only ticks vs
    // calc+draw ticks (draw = the difference), for draw-30 budgeting.
    std::vector<u32> calc_ticks,draw_ticks;calc_ticks.reserve(std::min(o.ticks?o.ticks:stat_cap,stat_cap));draw_ticks.reserve(std::min(o.ticks?o.ticks:stat_cap,stat_cap));
    write_progress("start");
#ifdef TH10_GE
    pad_start();pacer.start(0);ge_set_debug(o.ge_debug);
#endif
    for(;!o.ticks||tick<o.ticks;++tick){
#ifdef __PSP__
        if(exit_requested)break;   // HOME menu -> Exit
#endif
        probe.tick=tick;tick_now=tick;
        elapsed+=1.0/60.0;
#ifdef TH10_GE
        // Audio runs on real time: when a tick overruns (slow screens, loads)
        // the music keeps its speed instead of stalling with the game. A
        // stall longer than 100 ms pauses the music rather than skipping it.
        {static u64 audio_last=now_us();const u64 t=now_us();audio_remainder+=std::min(100.0,double(t-audio_last)/1000.0);audio_last=t;}
#else
        audio_remainder+=1000.0/60.0;
#endif
        const auto ms=u32(std::floor(audio_remainder));audio_remainder-=ms;
        current_tick=tick;app->audio.advance(ms);
        auto& snapshot=app->input.snapshot;std::memset(&snapshot,0,sizeof(snapshot));snapshot.focused=1;apply_input(snapshot,tick);
#ifdef TH10_GE
        if(presses.empty()&&o.pad)pad_input(snapshot);
#endif
        // The original frame-skip option (th10.cfg options[4]): update every
        // tick, draw/present every frameskip+1 ticks. th10.cfg is read on the
        // first step, so enforce the override before every step.
        if(o.frameskip>=0)app->state.configuration.options[4]=u8(o.frameskip);
        const u64 soft_before=soft_calls,trig_before=trig_calls,t0=now_us();
        result=app->step(true);
        const u32 us=u32(now_us()-t0);
#if TH10_HAS_BACKTRACE
        fpe_tick(tick);
#else
        {   // FCR31 sticky flags (O bit 4, Z bit 5, V bit 6) raised by this tick.
            const u32 f=read_fcr31(),flags=(f>>2)&0x1cu;
            if(flags){const bool first=!probe.fpu_ticks;++probe.fpu_ticks;for(int b=0;b<3;b++)if(flags&(4u<<b)&&probe.fpu_first[b]==~0u)probe.fpu_first[b]=tick;
                if(probe.fpu_events<16){probe.event_tick[probe.fpu_events]=tick;probe.event_flags[probe.fpu_events]=flags;}++probe.fpu_events;
                char fpu[512];screen_row(5,"fpu %s",fpu_text(fpu,sizeof(fpu)));if(first)write_progress("first fpu flag");}
            write_fcr31(f&~0x7cu);
        }
#endif
        sdl_audio_pump();
#ifdef TH10_TRIG_MEMO_PROBE
        {static unsigned long long last_calls=0,last_repeats=0;const unsigned long long dc=th10_memo_calls-last_calls,dr=th10_memo_repeats-last_repeats;last_calls=th10_memo_calls;last_repeats=th10_memo_repeats;
            if(dc>memo_tick_max){memo_tick_max=dc;memo_tick_max_repeats=dr;memo_tick_max_tick=tick;}}
#endif
        const u64 soft=soft_calls-soft_before;soft_total+=soft;if(soft>soft_max){soft_max=soft;soft_max_tick=tick;}
        const u64 trig=trig_calls-trig_before;trig_total+=trig;if(trig>trig_max){trig_max=trig;trig_max_tick=tick;}
        step_sum+=us;if(us>step_max){step_max=us;step_max_tick=tick;}
        window_us+=us;++window_ticks;const u32 presents=sdl_stats()->presentations;
        const bool drew=presents!=last_presents;
        if(app->value.screen==7){auto& v=drew?draw_ticks:calc_ticks;if(v.size()<stat_cap)v.push_back(us);}
#if defined(TH10_CHAIN_TIMING) && TH10_WINDOW_BREAKDOWN
#if TH10_MISS_BREAKDOWN
        miss_stage=u32(app->state.game.stage);
#endif
        if(drew)th10_window_close(tick,window_us,window_ticks,app->value.screen==7);
#endif
        if(drew){static u64 last_present_wall=now_us();const u64 wall_now=now_us();
            if(windows.size()<stat_cap)windows.push_back({tick,window_us,window_ticks,app->value.screen,app->state.game.stage,u32(std::min<u64>(wall_now-last_present_wall,0xffffffffu))});
            last_present_wall=wall_now;window_us=0;window_ticks=0;last_presents=presents;}
        const size_t heap=heap_in_use();if(heap>heap_peak){heap_peak=heap;heap_peak_tick=tick;}
#if defined(TH10_HEAP_HWM) && TH10_HEAP_HWM
        probe.note_heap(heap,tick,"(tick end)");
#endif
        probe.screen=app->value.screen;probe.stage=app->state.game.stage;probe.heap_peak=heap_peak;probe.heap_peak_tick=heap_peak_tick;
        {   // stage boundaries and entering/leaving the game screen: see write_stage_line
            static int logged_stage=-1,logged_screen=-1;static u32 settle_tick=0;
            if(probe.stage!=logged_stage||(probe.screen!=logged_screen&&(probe.screen==7||logged_screen==7))){logged_stage=probe.stage;logged_screen=probe.screen;write_stage_line();settle_tick=tick+120;}
            else if(settle_tick&&tick==settle_tick){write_stage_line();settle_tick=0;}}
        if(trace&&tick%o.every==0){
            // Hash economy values, not addresses: Timer::rate is a pointer, so
            // replace it by the rate it points at.
            const auto& g=app->state.game;GameEconomy e=g;const float rate_value=g.faith_timer.rate?*g.faith_timer.rate:0.f;e.faith_timer.rate=nullptr;
            u64 econ=fnv(1469598103934665603ull,&e,sizeof(e));econ=fnv(econ,&rate_value,sizeof(rate_value));
            const auto* r=touhou::sdl::current();char line[256];
            std::snprintf(line,sizeof(line),"%u %d %d %d %d %d %u %u %u %u %016llx %016llx %u %llu %llu %u\n",tick,app->value.screen,g.stage,g.score,int(g.power),g.lives,
                random.seed,random.calls,visual.seed,visual.calls,(unsigned long long)econ,(unsigned long long)(r?r->presented_hash:0),r?r->presented_draws:0,(unsigned long long)soft,(unsigned long long)trig,us);
            tbuf+=line;if(tbuf.size()>trace_flush){std::fwrite(tbuf.data(),1,tbuf.size(),trace);tbuf.clear();}
        }
        if(tick%60==59)screen_row(2,"tick %6u  scr %d st %d  step %6u us  win %u  heap %.1f MiB",tick+1,app->value.screen,app->state.game.stage,us,unsigned(windows.size()),heap_peak/1048576.0);
        if(o.progress&&tick%o.progress==o.progress-1)write_progress("periodic");
#ifdef TH10_GE
        pacer.wait(tick);
        if(!(tick&63))scePowerTick(PSP_POWER_TICK_ALL);   // unattended runs (--pad 0): keep backlight and sleep timers off, as TH08
#if TH10_REC
        th10_rec_tick(tick,pacer.late,sdl_stats()->presentations);
#endif
#endif
        if(result)break;
    }
    const u64 run_us=now_us()-run_start;
#if TH10_REC && defined(TH10_GE)
    th10_rec_shutdown();   // HOME exit / run end: the file gets its idx1 and header now
#endif
    if(trace){std::fwrite(tbuf.data(),1,tbuf.size(),trace);std::fclose(trace);}
    // ---- summary ----
    std::string out;char line[512];auto add=[&](const char* fmt,...){va_list a;va_start(a,fmt);std::vsnprintf(line,sizeof(line),fmt,a);va_end(a);out+=line;};
    add("%s\n",th10_build_tag);   // TH10_BUILD_ID=<id> (checklist F-2: read the build before the numbers; also keeps the tag in the EBOOT)
    const auto* stats=sdl_stats();
#ifdef __PSP__
    add("platform=psp cpu=%d MHz bus=%d MHz (API) real=%u MHz (spin)\n",scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt(),probe.mhz);
    {char fpu[512];add("fpu flags (FCR31, exceptions disabled) %s\n",fpu_text(fpu,sizeof(fpu)));}
    add("heap room start=%u end=%u alloc fails=%u\n",probe.heap_room_start,heap_room(),probe.fails);
#ifdef TH10_GE
    {unsigned g[16]{};ge_renderer_counts(g);add("ge conversions=%u readbacks=%u copies=%u unsupported_combiner=%u list_restarts=%u  pacing late=%u dropped=%u slipped=%u catchup=%d\n",g[0],g[1],g[2],g[3],g[4],pacer.late,pacer.dropped,pacer.slipped,TH10_PACING_NO_CATCHUP?0:1);
#ifdef TH10_LIST_KB
    {unsigned l[3]{};ge_list_stats(l);add("ge list: bytes=%u peak=%u restarts=%u\n",l[0],l[1],l[2]);}
#endif
     if(o.ge_debug){float f[4];std::memcpy(f,g+12,16);add("ge debug=%u world_draws=%u guard_draws=%u guard_vertices=%u behind_vertices=%u z(ndc after 2z-w) below=%u inside=%u above=%u min=%g max=%g w=%g..%g\n",o.ge_debug,g[5],g[6],g[7],g[8],g[9],g[10],g[11],f[0],f[1],f[2],f[3]);
        char sample[1024];ge_sample(sample,sizeof(sample));out+=sample;}}
    {char g[512];ge_frame_stats(g,sizeof(g));add("%s  font_missing_glyphs=%u\n",g,headless_font_missing_glyphs());}
#ifdef TH10_CHAIN_TIMING
    for(int d=0;d<2;d++){   // top callbacks by total CPU time, per game tick
        int order[128];int n=0;for(int t=0;t<128;t++)if(chain_calls[d][t])order[n++]=t;
        std::sort(order,order+n,[&](int a,int b){return chain_us[d][a]>chain_us[d][b];});
        add("chain %s (ms per tick over %u ticks):",d?"draw":"update",tick);
        for(int i=0;i<n&&i<16;i++){const int t=order[i];if(t<int(sizeof(callback_names)/sizeof(*callback_names)))add(" %s=%.2f",callback_names[t],tick?chain_us[d][t]/1000.0/tick:0.0);else add(" #%d=%.2f",t,tick?chain_us[d][t]/1000.0/tick:0.0);}
        add("\n");}
#if TH10_WINDOW_BREAKDOWN
    for(int list=0;list<2;list++){const HeavyWindow* windows=list?heavy_play:heavy;const u32 count=list?heavy_play_count:heavy_count;
    for(u32 i=0;i<count;i++){const auto& h=windows[i];add("%s t%u %.1f ms/%ut outside_chain=%.1f draws=%u renderer_cpu=%.1f:",list?"heavy play window":"heavy window",h.tick,h.us/1000.0,h.ticks,h.outside/1000.0,h.draws,h.renderer_us/1000.0);
        for(int k=0;k<8&&h.cost[k];k++){const int t=h.token[k]&127;add(" %s%s=%.1f",h.token[k]>=128?"draw:":"",t<int(sizeof(callback_names)/sizeof(*callback_names))?callback_names[t]:"?",h.cost[k]/1000.0);}add("\n");}}
#if TH10_MISS_BREAKDOWN
    // "miss stage S windows=N misses=M (x%) window avg=..ms miss avg=..ms: callback=ms per MISS window ..." (top 10)
    for(u32 s=0;s<8;s++){const auto& m=miss_stages[s];if(!m.windows)continue;
        add("miss stage %u windows=%u misses=%u (%.2f%%) window avg=%.1f ms miss avg=%.1f ms:",s,m.windows,m.misses,100.0*m.misses/m.windows,m.us/1000.0/m.windows,m.misses?m.miss_us/1000.0/m.misses:0.0);
        u32 used[10]{};int n=0;
        for(;n<10&&m.misses;n++){u64 best=0;u32 best_t=0xffff;for(int d=0;d<2;d++)for(int t=0;t<128;t++){const u32 k=u32(t+d*128);bool taken=false;for(int j=0;j<n;j++)if(used[j]==k)taken=true;if(!taken&&m.chain[d][t]>best){best=m.chain[d][t];best_t=k;}}
            if(best_t==0xffff)break;used[n]=best_t;const int t=int(best_t&127);
            add(" %s%s=%.2f",best_t>=128?"draw:":"",t<int(sizeof(callback_names)/sizeof(*callback_names))?callback_names[t]:"?",best/1000.0/m.misses);}
        add("\n");}
#endif
#endif
#endif
    {char a[256];th10_audio_stats(a,sizeof(a));unsigned m[3]{};headless_audio_mix_stats(m);add("%s host_jobs=%u host_skipped=%u resyncs=%u\n",a,m[0],m[1],m[2]);}
#if TH10_SE_EDRAM
    {char a[500];th10_se_stats(a,sizeof(a));add("%s\n",a);}
#endif
#endif
#else
    add("platform=pc-i386\n");
#ifdef TH10_CHAIN_TIMING
    for(int d=0;d<2;d++){   // same table as the PSP GE build, in PC microseconds
        int order[128];int n=0;for(int t=0;t<128;t++)if(chain_calls[d][t])order[n++]=t;
        std::sort(order,order+n,[&](int a,int b){return chain_us[d][a]>chain_us[d][b];});
        add("chain %s (us per tick over %u ticks):",d?"draw":"update",tick);
        for(int i=0;i<n&&i<20;i++){const int t=order[i];add(" %s=%.1f",t<int(sizeof(callback_names)/sizeof(*callback_names))?callback_names[t]:"?",tick?double(chain_us[d][t])/tick:0.0);}
        add("\n");}
#endif
#endif
    add("ticks=%u result=%d error=%d screen=%d stage=%d frameskip=%d hash=%u run=%.1f s\n",tick,result,app->error,app->value.screen,app->state.game.stage,o.frameskip,o.hash,run_us/1e6);
    add("step_us avg=%.0f max=%u (tick %u)\n",tick?double(step_sum)/tick:0.0,step_max,step_max_tick);
    for(int game=1;game>=0;--game){   // game screen (7) and everything else
        std::vector<u32> w;u32 miss=0,worst=0,worst_tick=0;u64 wall_us=0,wall_ticks=0;
        for(const auto& x:windows)if((x.screen==7)==(game==1)){w.push_back(x.us);wall_us+=x.wall_us;wall_ticks+=x.ticks;const u32 limit=u32(x.ticks*16667);if(x.us>limit)++miss;if(x.us>worst){worst=x.us;worst_tick=x.tick;}}
        if(w.empty())continue;std::sort(w.begin(),w.end());auto q=[&](double p){return w[size_t(p*(w.size()-1))];};
        // real_hz: game ticks per wall second (present to present, pacing waits
        // included) over these windows (checklist F-5; 59.94 = full speed).
        add("%s windows=%zu miss=%u (%.2f%%) p50=%u p95=%u p99=%u worst=%u us (tick %u) real_hz=%.2f\n",game?"game":"other",w.size(),miss,100.0*miss/w.size(),q(.5),q(.95),q(.99),worst,worst_tick,wall_us?wall_ticks*1e6/double(wall_us):0.0);
#if TH10_MISS_BREAKDOWN
        // Per stage, the same real_hz over that stage's game windows (a replay's
        // playback FPS per stage, to set beside the FPS bytes the .rpy recorded).
        if(game){u64 sw[8]{},st[8]{};u32 sn[8]{};
            for(const auto& x:windows)if(x.screen==7){const u32 s=x.stage>=0&&x.stage<8?u32(x.stage):7u;sw[s]+=x.wall_us;st[s]+=x.ticks;++sn[s];}
            add("stage real_hz:");for(u32 s=0;s<8;s++)if(sn[s])add(" %u=%.2f",s,sw[s]?st[s]*1e6/double(sw[s]):0.0);add("\n");}
#endif
    }
    {   // TH08 tick_spike (r260/r262): 12 consecutive game ticks in under 160 ms
        // ran faster than 75 Hz (catch-up). Spans start at each game window.
        u32 spikes=0;double fastest=0;
        for(size_t i=0;i<windows.size();i++){u32 ticks=0;u64 wall=0;
            for(size_t j=i;j<windows.size()&&windows[j].screen==7&&ticks<12;j++){ticks+=windows[j].ticks;wall+=windows[j].wall_us;}
            if(ticks<12||!wall)continue;const double hz=ticks*1e6/double(wall);if(hz>fastest)fastest=hz;if(wall*12<160000ull*ticks)++spikes;}
        add("tick spikes=%u (12 game ticks under 160 ms) fastest=%.1f Hz\n",spikes,fastest);
    }
    for(int drawn=0;drawn<2;++drawn){auto& v=drawn?draw_ticks:calc_ticks;if(v.empty())continue;std::vector<u32> w=v;std::sort(w.begin(),w.end());u64 sum=0;for(auto x:w)sum+=x;
        auto q=[&](double p){return w[size_t(p*(w.size()-1))];};
        add("game %s ticks=%zu avg=%.0f p50=%u p95=%u p99=%u max=%u us\n",drawn?"calc+draw":"calc-only",w.size(),double(sum)/w.size(),q(.5),q(.95),q(.99),w.back());}
#ifdef TH10_STAT_CAP
    {   std::vector<Window> top(std::min<size_t>(10,windows.size()));std::partial_sort_copy(windows.begin(),windows.end(),top.begin(),top.end(),[](const Window& a,const Window& b){return a.us>b.us;});
#else
    {   std::vector<Window> top=windows;std::sort(top.begin(),top.end(),[](const Window& a,const Window& b){return a.us>b.us;});if(top.size()>10)top.resize(10);
#endif
        add("worst windows:");for(const auto& x:top)add(" t%u:%uus/%ut(s%d,st%d)",x.tick,x.us,x.ticks,x.screen,x.stage);add("\n");}
#ifdef TH10_VERTEX_PEAK
    add("vertex peak batch=%u frame=%u (buffer 131072)\n",th10_vertex_peak_batch,th10_vertex_peak_frame);
#endif
#if defined(TH10_TRIG_MEMO_PROBE) && defined(TH10_TRIG_MEMO) && TH10_TRIG_MEMO
    add("polar memo calls=%llu hits=%llu (%.1f%%)\n",th10_polar_memo_calls,th10_polar_memo_hits,th10_polar_memo_calls?100.0*th10_polar_memo_hits/th10_polar_memo_calls:0.0);
#endif
#ifdef TH10_TRIG_MEMO_PROBE
    add("bullet velocity polar calls=%llu same-angle repeats=%llu (%.1f%%) max/tick=%llu (repeats %llu, tick %u)\n",th10_memo_calls,th10_memo_repeats,th10_memo_calls?100.0*th10_memo_repeats/th10_memo_calls:0.0,memo_tick_max,memo_tick_max_repeats,memo_tick_max_tick);
#endif
    add("softfloat total=%llu max/tick=%llu (tick %u)\n",(unsigned long long)soft_total,(unsigned long long)soft_max,soft_max_tick);
    add("libm trig total=%llu max/tick=%llu (tick %u)\n",(unsigned long long)trig_total,(unsigned long long)trig_max,trig_max_tick);
#if TH10_SOFTFLOAT_CENSUS
    {static std::string* census_out;census_out=&out;th10_census_report([](const char* l){*census_out+=l;},callback_names,unsigned(sizeof(callback_names)/sizeof(*callback_names)));}
#endif
    add("heap peak=%.2f MiB (tick %u)\n",heap_peak/1048576.0,heap_peak_tick);
#if TH10_ANM_POOL_STATS
    {unsigned a[5]{};th10_anm_pool_stats(a);add("anm pool: cap=%u live_max=%u pool_allocs=%u heap_allocs=%u heap_live_max=%u\n",a[0],a[1],a[2],a[3],a[4]);}
#endif
#if defined(TH10_HEAP_HWM) && TH10_HEAP_HWM
    add("heap hwm=%.2f MiB (tick %u, %s): in use after every noted allocation and tick\n",probe.hwm/1048576.0,probe.hwm_tick,probe.hwm_file);
#endif
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    {const auto a=anm_stream_stats();add("anm stream: streamed=%u whole=%u aborted=%u scratch_peak=%u two_pass=%u\n",a.streamed,a.whole,a.aborted,a.scratch_peak,a.two_pass);}
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    {u32 c[3]{},h[4]{};th10_indexed_counts(c);native_graphics_indexed_stats(h);
     add("clut8 created=%u refused_colours=%u refused_shape=%u live=%u bytes=%u saved=%u expanded=%u\n",c[0],c[1],c[2],h[0],h[1],h[2],h[3]);}
#endif
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
    {u32 z[3]{};native_graphics_lazy_stats(z);add("lazy '@' textures: still without bytes=%u (%u bytes not allocated) given bytes on first write=%u\n",z[0],z[1],z[2]);}
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
    {u32 c[3]{},z[3]{};th10_padded_counts(c);native_graphics_padded_stats(z);
     add("padded textures (image smaller than the texture, a sprite past it): created indexed=%u 16bit=%u refused=%u; still without bytes=%u (%u bytes not allocated); given bytes when first drawn or touched=%u\n",c[0],c[1],c[2],z[0],z[1],z[2]);}
#endif
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    {unsigned v[8]{};th10_volatile_stats(v);add("volatile arena: locked=%u base=0x%08x bytes=%u in_use=%u peak=%u allocations=%u heap_fallbacks=%u live=%u\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);}
#endif
#if defined(__PSP__) && defined(TH10_STACK_PROBE) && TH10_STACK_PROBE
    add("main stack: size=%u used_max=%u bytes (painted at start, below the stack pointer)\n",stack_probe::size,stack_probe::used());
#endif
    add("per function:");for(int i=0;i<ALL_COUNT;i++)if(counts[i])add(" %s=%llu",counter_names[i],(unsigned long long)counts[i]);add("\n");
    {u64 r[11]{};headless_read_stats(r);add("file reads=%llu bytes=%llu seeks=%llu sizes <512=%llu <4K=%llu <16K=%llu <64K=%llu <256K=%llu <1M=%llu <4M=%llu >=4M=%llu\n",
        (unsigned long long)r[0],(unsigned long long)r[1],(unsigned long long)r[2],(unsigned long long)r[3],(unsigned long long)r[4],(unsigned long long)r[5],(unsigned long long)r[6],(unsigned long long)r[7],(unsigned long long)r[8],(unsigned long long)r[9],(unsigned long long)r[10]);}
#if defined(__PSP__) && TH10_BGM_PREFETCH
    {u32 b[10]{};headless_bgm_stats(b);add("bgm prefetch: hits=%u waits=%u wait_max=%.1fms wait_total=%ums fallbacks=%u flushes=%u reads=%u read_max=%.1fms low_water=%uKiB of %uKiB\n",b[0],b[1],b[2]/1000.0,b[3],b[4],b[5],b[6],b[7]/1000.0,b[8]/1024u,b[9]/1024u);}
#endif
    add("draw calls=%u presents=%u font_text=%u audio_signals=%u\n",stats->calls,stats->presentations,headless_font_text_calls(),headless_audio_signals());
#if TH10_HAS_BACKTRACE
    if(fpe_mode==1){add("fpe ticks=%u (V invalid, Z div0, O overflow):",fpe_ticks);
        for(const auto& e:fpe_events)add(" t%u:%s%s%s",e.tick,e.flags&1?"V":"",e.flags&4?"Z":"",e.flags&8?"O":"");add("\n");}
    if(heap_tracking){heap_tracking=false;add("tracked live peak=%.2f MiB (tick %u), sites snapshot at %.2f MiB\n",live_peak/1048576.0,live_peak_tick,snapshot_peak/1048576.0);
        FILE* f=std::fopen("th10_heap_sites.txt","w");if(f){for(const auto& e:peak_sites)if(e.second){std::fprintf(f,"%zu",e.second);for(auto* p:(*site_frames)[e.first])std::fprintf(f," %p",p);std::fprintf(f,"\n");}std::fclose(f);}}
    if(profiling){FILE* f=std::fopen("th10_sites.txt","w");if(f){for(const auto& e:sites){std::fprintf(f,"%llu %s",(unsigned long long)e.second,counter_names[e.first.first]);for(auto* p:e.first.second)std::fprintf(f," %p",p);std::fprintf(f,"\n");}std::fclose(f);}}
#endif
#if TH10_HAS_BACKTRACE && defined(TH10_SAMPLER) && defined(TH10_CHAIN_TIMING)
    sampler_stop("th10_samples.bin");
#endif
    std::fputs(out.c_str(),stdout);
    if(FILE* f=std::fopen(o.result.c_str(),"w")){std::fputs(out.c_str(),f);std::fclose(f);}
    write_progress("end");
    screen_row(7,"done. result: %s",o.result.c_str());
    application_destroy(app);effects_destroy(effects);audio_destroy(audio);sdl_audio_shutdown();fonts_destroy(fonts);sdl_fonts_shutdown();
    animation_engine_destroy(animation);graphics_destroy(device);game_state_destroy(state);input_destroy(input);files_destroy(files);sdl_shutdown();
#ifdef __PSP__
#ifdef TH10_GE
    th10_audio_shutdown();
#endif
#if defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
    th10_volatile_shutdown();
#endif
#ifndef TH10_GE
    sceKernelDelayThread(3*1000*1000);   // leave "done." on the debug screen
#endif
    sceKernelExitGame();
#endif
    return result==2?1:0;
}
