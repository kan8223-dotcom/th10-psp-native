// TH10 recording, SC side (TH10_REC=1 only: psp_ge/Makefile adds this file
// when EXTRA_CXXFLAGS has -DTH10_REC=1; the default build has none of it).
// Design (2026-10-01): SELECT starts/stops recording, a red dot is shown
// while recording. Design: the ME serves audio first and
// encodes in its spare time, built into TH10 behind a flag, frames that cannot
// be encoded in time are dropped, audio is recorded, the file is a Motion JPEG
// AVI the XMB plays (made on the PSP).
//
// Threads and who does what:
//   game thread  (th10_rec_tick, every tick after the pacing wait; and the
//                capture hooks inside GeRenderer's commit): SELECT, captures
//                (GE copy of the presented frame into one of 3 RAM buffers,
//                at most one per video slot), the encode pipeline (newest
//                captured frame -> ME JPEG jobs writing into the JPEG ring; an
//                older waiting frame is superseded), ME clock calibration.
//                Never touches files.
//   audio thread (MeAudio, 0x12): th10_rec_audio_block downmixes the block it
//                is about to play into the audio ring.
//   ME           (MeAudio loop): audio jobs first, then one recording job: a
//                writer copy (JPEG ring -> write buffer) before a JPEG job
//                (1/34 of a frame); then audio is checked again.
//   writer       (this file's thread, below the game thread by default,
//                th10rec.txt writer_prio): the AVI muxer (RecCore.hpp: slots
//                on the audio clock, a slot without a new picture repeats the
//                previous one) into two 64 KiB Main RAM write buffers, the
//                file writes (whole 64 KiB buffers, started right after a game
//                file read or paced, never while paused by the BGM side), idx1
//                + header at stop, file split, the log, scePowerTick, the SC
//                encoder when there is no ME (PPSSPP), and the repair of an
//                unfinished file left by a crash.
// JPEG ring: ME eDRAM (psp/MeEdramMap.hpp rec_ring, 1000 KiB; th10rec.txt
// ring_edram=1) or Main RAM (ring_edram=0, ring_kib, default 256 KiB): the
// same jobs, only the base address differs; the SC never touches an eDRAM
// ring (the ME copies out of it). Buffers are allocated once at init, before
// the game's own allocations (the 3->4 stage load ran out of memory on 10-01;
// nothing here is freed or reallocated later). Output:
// ms0:/VIDEO/TH10/TH10_<date>_<time>.AVI (only when the game folder is on
// ms0:, the Go's M2; never ef0:). Log: <game dir>/th10_rec_log.txt (first
// line TH10_BUILD_ID=). Settings: <game dir>/th10rec.txt, key=value:
// fps=15|30 quality=1..100 size=480|320 mcus=5..20 ring_edram=0|1
// ring_kib=128..2048 writer_prio=0x30 write_policy=1 split_mb=1000
// min_free_mb=64 auto_start=<tick> auto_stop=<tick> progress_s=30 repair=1.
// BGM side: th10_rec_writer_pause(1) before its big M2 read, (0) after;
// th10_rec_writer_paused() says when no write of ours is in flight.
#include "Recorder.hpp"
#include "RecCore.hpp"
#include "RecMe.hpp"
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspsdk.h>
#include <malloc.h>
#include <strings.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "build_id.h"

#ifndef TH10_REC_DROP_EMPTY
// 0 (default): a slot without a new picture repeats the previous JPEG (every
// '00dc' chunk is a whole JPEG; the bitrate never exceeds the all-frames
// case). 1: a zero-length '00dc' chunk instead (idx1 flags 0) - smaller, but
// untested on the XMB player.
#define TH10_REC_DROP_EMPTY 0
#endif
#ifndef TH10_REC_RING_EDRAM
// The JPEG ring's default place (th10rec.txt ring_edram overrides it): 0 Main
// RAM (ring_kib), 1 ME eDRAM (MeEdramMap.hpp rec_ring; only after the
// 1/5/10-minute eDRAM retention run passed).
#define TH10_REC_RING_EDRAM 0
#endif

extern "C" {
th10::rec::Box th10_rec_box;                         // encode ring (game thread)
th10::rec::Job th10_rec_jobs[th10::rec::job_slots];
th10::rec::Box th10_rec_wbox;                        // copy ring (writer thread)
th10::rec::Job th10_rec_wjobs[th10::rec::wjob_slots];
th10::rec::JpegCtx th10_rec_jctx;   // the JPEG state between the jobs of a frame (uncached alias only)
}

namespace {
using namespace th10::rec;
namespace jp=th10::rec::jpeg;
using avi::u8;using avi::u64;using avi::i16;using avi::i32;
u64 now(){return sceKernelGetSystemTimeWide();}
inline void barrier(){asm volatile("":::"memory");}
inline void sync(){TH10_REC_SYNC();}

// ------------------------------------------------------------- config --
struct Config {u32 fps=15,quality=50,size=480,mcus=0,ring_edram=TH10_REC_RING_EDRAM,ring_kib=256,writer_prio=0x30,write_policy=1,split_mb=1000,min_free_mb=64,auto_start=0,auto_stop=0,progress_s=30,repair=1;} cfg;
char game_dir[160],log_path[200],spill_path[200];bool under_ppsspp=false,enabled=false;const char* disabled_reason="";
void read_config(){   // whitespace-separated key=value words (sceIo, no FILE*)
    char path[200];std::snprintf(path,sizeof(path),"%s/th10rec.txt",game_dir);
    char text[2048];int n=0;
    {const SceUID f=sceIoOpen(path,PSP_O_RDONLY,0777);if(f>=0){n=sceIoRead(f,text,sizeof(text)-1);sceIoClose(f);}}
    text[n>0?n:0]=0;
    for(char* w=std::strtok(text," \t\r\n");w;w=std::strtok(nullptr," \t\r\n")){
        char* eq=std::strchr(w,'=');if(!eq)continue;*eq=0;const u32 v=u32(std::strtoul(eq+1,nullptr,0));
        struct {const char* k;u32* p;} keys[]{{"fps",&cfg.fps},{"quality",&cfg.quality},{"size",&cfg.size},{"mcus",&cfg.mcus},{"ring_edram",&cfg.ring_edram},{"ring_kib",&cfg.ring_kib},
            {"writer_prio",&cfg.writer_prio},{"write_policy",&cfg.write_policy},{"split_mb",&cfg.split_mb},{"min_free_mb",&cfg.min_free_mb},{"auto_start",&cfg.auto_start},{"auto_stop",&cfg.auto_stop},
            {"progress_s",&cfg.progress_s},{"repair",&cfg.repair}};
        for(auto& k:keys)if(!std::strcmp(w,k.k))*k.p=v;
    }
    cfg.fps=cfg.fps>=30?30u:15u;   // the XMB-tested rates (G0b: the ME holds 480x272 at 15, not 30)
    cfg.quality=std::min<u32>(std::max<u32>(cfg.quality,1u),100u);
    cfg.size=cfg.size==320?320u:480u;
    if(!cfg.mcus)cfg.mcus=cfg.size==480?15u:10u;
    cfg.mcus=std::min<u32>(std::max<u32>(cfg.mcus,5u),20u);   // the ME job checks n <= 20; >= 5 keeps a frame under 128 jobs
    cfg.ring_edram=cfg.ring_edram?1u:0u;
    cfg.ring_kib=std::min<u32>(std::max<u32>(cfg.ring_kib,128u),2048u)&~3u;
    cfg.writer_prio=std::min<u32>(std::max<u32>(cfg.writer_prio,0x13u),0x7fu);   // always below the audio thread (0x12)
    cfg.split_mb=std::min<u32>(std::max<u32>(cfg.split_mb,16u),1800u);
    if(!cfg.progress_s)cfg.progress_s=30;
}
u32 spf(){return 22050u/cfg.fps;}

// ------------------------------------------------------------- the log --
SceUID log_sema=-1;char log_buf[16384];u32 log_len=0,log_lost=0;volatile u32 log_flush_req=0;
void rlog(const char* fmt,...){
    char line[700];va_list a;va_start(a,fmt);int n=std::vsnprintf(line,sizeof(line),fmt,a);va_end(a);
    if(n<=0){return;}if(n>=int(sizeof(line))){n=int(sizeof(line))-1;}
    if(log_sema>=0)sceKernelWaitSema(log_sema,1,nullptr);
    if(log_len+u32(n)<sizeof(log_buf)){std::memcpy(log_buf+log_len,line,size_t(n));log_len+=u32(n);}else log_lost+=u32(n);
    if(log_sema>=0)sceKernelSignalSema(log_sema,1);
}
void log_write_out(){   // writer thread (or the game thread at init/after the writer ended)
    static char out[sizeof(log_buf)];u32 n=0;
    if(log_sema>=0)sceKernelWaitSema(log_sema,1,nullptr);
    n=log_len;std::memcpy(out,log_buf,n);log_len=0;
    if(log_sema>=0)sceKernelSignalSema(log_sema,1);
    if(!n||std::strncmp(log_path,"ms0:",4))return;   // nothing is written outside ms0: (the Go's internal ef0: stays untouched)
    const SceUID f=sceIoOpen(log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    if(f>=0){u32 off=0;while(off<n){const int w=sceIoWrite(f,out+off,n-off);if(w<=0)break;off+=u32(w);}sceIoClose(f);}
}

// --------------------------------------------------- percentiles (no heap) --
struct Hist {u32 width=1,count=0,max=0;u64 sum=0;u32 bins[256];
    void reset(u32 w){width=w;count=0;max=0;sum=0;std::memset(bins,0,sizeof(bins));}
    void add(u32 v){++count;sum+=v;if(v>max)max=v;u32 b=v/width;if(b>255)b=255;++bins[b];}
    u32 pct(double p) const {if(!count)return 0;const u32 want=u32(p*count);u32 acc=0;for(u32 i=0;i<255;i++){acc+=bins[i];if(acc>want)return std::min((i+1)*width,max);}return max;}};

// ------------------------------------------------------------- buffers --
constexpr u32 cap_count=3,rec_count=32,frame_reserve=96u*1024u;   // a frame's room in the JPEG ring (G0b q50 max 39.6 KB; more = overflow, the slot repeats)
constexpr u32 aring_size=1u<<17,aring_mask=aring_size-1u;          // 5.9 s of 22.05 kHz mono
constexpr u32 piece=64u*1024u,index_bytes=64u*1024u,scratch_bytes=20u*16u*16u*2u;
constexpr u32 tables_bytes=(u32(sizeof(jp::Tables))+63u)&~63u;
u8* cap_buf[cap_count]{};jp::Tables* tables=nullptr;jp::u16* scratch_me=nullptr;jp::u16* scratch_sc=nullptr;
i16* aring=nullptr;u8* wbuf[2]{};u8* index_buf=nullptr;u8* ring_main=nullptr;
u32 ring_base=0,ring_size=0;bool ring_in_edram=false;   // the JPEG ring: an address the ME reads through its kseg0 alias (eDRAM or Main RAM)
bool alloc_all(){
    for(u32 i=0;i<cap_count;i++)cap_buf[i]=static_cast<u8*>(memalign(64,frame_bytes));
    tables=static_cast<jp::Tables*>(memalign(64,tables_bytes));scratch_me=static_cast<jp::u16*>(memalign(64,scratch_bytes));scratch_sc=static_cast<jp::u16*>(memalign(64,scratch_bytes));
    aring=static_cast<i16*>(memalign(64,aring_size*2u));wbuf[0]=static_cast<u8*>(memalign(64,piece));wbuf[1]=static_cast<u8*>(memalign(64,piece));index_buf=static_cast<u8*>(memalign(64,index_bytes));
    ring_in_edram=cfg.ring_edram&&!under_ppsspp;   // PPSSPP has no ME: the SC encodes and copies, so the ring must be in Main RAM
    if(!ring_in_edram)ring_main=static_cast<u8*>(memalign(64,cfg.ring_kib*1024u));
    bool ok=tables&&scratch_me&&scratch_sc&&aring&&wbuf[0]&&wbuf[1]&&index_buf&&(ring_in_edram||ring_main);for(auto* p:cap_buf)ok=ok&&p;
    if(!ok)return false;
    for(auto* p:cap_buf){std::memset(p,0,frame_bytes);}
    std::memset(scratch_me,0,scratch_bytes);std::memset(aring,0,aring_size*2u);std::memset(tables,0,tables_bytes);
    if(ring_in_edram){ring_base=TH10_REC_EDRAM_BASE+th10::edram::rec_ring.offset;ring_size=th10::edram::rec_ring.bytes;}
    else{std::memset(ring_main,0,cfg.ring_kib*1024u);ring_base=reinterpret_cast<u32>(ring_main);ring_size=cfg.ring_kib*1024u;}
    if(cfg.size==480)jp::build_tables(*tables,cfg.quality,width,height,2,true);
    else jp::build_tables(*tables,cfg.quality,320,240,2,true,width,height,30,181);   // 3:2 on the ME, active lines 30..210 (G0b P2e)
    // GE/ME-owned from here (capture buffers, Main RAM ring, ME scratch): no
    // SC line may stay dirty over them; the ME invalidates the tables per frame.
    sceKernelDcacheWritebackInvalidateAll();
    return true;
}
avi::Params params(){avi::Params p;p.width=cfg.size;p.height=cfg.size==480?272u:240u;p.fps=cfg.fps;return p;}

// -------------------------------------------------------------- ME rings --
// Single producer each: the encode ring (th10_rec_box) only from the game
// thread, the copy ring (th10_rec_wbox) only from the writer thread.
volatile Box* ring(){return reinterpret_cast<volatile Box*>(TH10_REC_UNCACHED|reinterpret_cast<u32>(&th10_rec_box));}
volatile Job* slot(u32 index){return reinterpret_cast<volatile Job*>(TH10_REC_UNCACHED|(reinterpret_cast<u32>(th10_rec_jobs)+(index%job_slots)*u32(sizeof(Job))));}
volatile Box* wring(){return reinterpret_cast<volatile Box*>(TH10_REC_UNCACHED|reinterpret_cast<u32>(&th10_rec_wbox));}
volatile Job* wslot(u32 index){return reinterpret_cast<volatile Job*>(TH10_REC_UNCACHED|(reinterpret_cast<u32>(th10_rec_wjobs)+(index%wjob_slots)*u32(sizeof(Job))));}
volatile JpegCtx* jctx(){return reinterpret_cast<volatile JpegCtx*>(TH10_REC_UNCACHED|reinterpret_cast<u32>(&th10_rec_jctx));}
u32 ring_free(){volatile Box* r=ring();return job_slots-(r->submitted-r->completed);}
u32 submit_to(volatile Box* r,volatile Job* (*at)(u32),u32 slots,u32 kind,u32 a0=0,u32 a1=0,u32 a2=0,u32 a3=0,u32 a4=0,u32 a5=0,u32 a6=0,u32 a7=0){
    if(r->submitted-r->completed>=slots)return ~0u;
    const u32 index=r->submitted;volatile Job* j=at(index);
    j->kind=kind;j->serial=index+1;j->a[0]=a0;j->a[1]=a1;j->a[2]=a2;j->a[3]=a3;j->a[4]=a4;j->a[5]=a5;j->a[6]=a6;j->a[7]=a7;
    j->status=StatusPending;j->done=0;j->count_start=j->count_end=0;
    sync();r->submitted=index+1;sync();return index;
}
u32 submit(u32 kind,u32 a0=0,u32 a1=0,u32 a2=0,u32 a3=0,u32 a4=0,u32 a5=0,u32 a6=0,u32 a7=0){return submit_to(ring(),slot,job_slots,kind,a0,a1,a2,a3,a4,a5,a6,a7);}
bool finished(u32 index){return slot(index)->done==index+1;}
double counts_per_us=0;   // ME CP0 count rate (calibrated in the first recording)
double us_of(u64 counts){return counts_per_us>0?double(counts)/counts_per_us:0.0;}
u64 busy64=0,audio_busy64=0,wbusy64=0;u32 busy_last=0,audio_busy_last=0,wbusy_last=0;   // widened every tick by the game thread only (the count wraps in 10.2 s)
void widen_busy(){volatile Box* r=ring();volatile Box* w=wring();const u32 b=r->busy_total,a=r->audio_busy,c=w->busy_total;
    busy64+=u32(b-busy_last);audio_busy64+=u32(a-audio_busy_last);wbusy64+=u32(c-wbusy_last);busy_last=b;audio_busy_last=a;wbusy_last=c;}

// ------------------------------------------------------- audio capture --
volatile u32 audio_on=0,a_written=0,a_seq=0,a_blocks=0;volatile u64 a_last_us=0;
bool wall_mode=false;u64 t_start=0;   // wall_mode: no audio thread (silence on the wall clock)
u32 audio_lost=0;
// The game's own file reads (HeadlessFiles.cpp browser_read, game thread):
// thbgm.dat refills are synchronous 0.25 s (43 KiB) reads every 0.25 s, so a
// read that waits behind one of our writes stalls the game thread, and with it
// the per-tick audio jobs (MeAudio keeps ~46 ms). G0b P3 (10-01, full-speed
// 64 KiB writes on the Go's M2): +51 underruns per 40 s. The writer therefore
// writes right after a game read (the next one is >= 100 ms away) or, with
// no reads for 0.4 s, paced 0.1 s apart (writer_window below).
volatile u32 read_end_low=0,reads_seen=0;   // sceKernelGetSystemTimeLow at the end of the last read (u32: no torn reads)
struct ReadStats {u32 n=0,over50=0,over200=0,max_us=0;u64 us=0,bytes=0;void add(u32 bytes_,u32 us_){++n;bytes+=bytes_;us+=us_;if(us_>max_us)max_us=us_;if(us_>50000)++over50;if(us_>200000)++over200;}};
ReadStats reads_session,reads_boot,reads_before;
// Pause (the BGM side's big M2 read): no write, spill, log or repair I/O is
// started while pause_req is set; paused() also waits for one in flight.
volatile u32 pause_req=0,in_io=0;volatile u64 pause_t0=0;
struct PauseStats {u32 count=0,longest_us=0;u64 total_us=0;} pauses_session;
}
extern "C" void th10_rec_game_read(unsigned bytes,unsigned us){
    read_end_low=sceKernelGetSystemTimeLow();reads_seen=1;reads_boot.add(bytes,us);reads_session.add(bytes,us);
}
// HeadlessFiles.cpp TH10_BGM_PREFETCH: the BGM read-ahead thread's reads open the same window (no
// game-read statistics: the game thread no longer waits for them).
extern "C" void th10_rec_bgm_read(void){read_end_low=sceKernelGetSystemTimeLow();reads_seen=1;}
extern "C" void th10_rec_writer_pause(int on){
    if(on){if(!pause_req){pause_t0=now();barrier();pause_req=1;}}
    else if(pause_req){const u64 d=now()-pause_t0;pause_req=0;auto& p=pauses_session;++p.count;p.total_us+=d;if(d>p.longest_us)p.longest_us=u32(d);}
}
extern "C" int th10_rec_writer_paused(void){return pause_req&&!in_io?1:0;}

// The audio thread (0x12) runs this for every block it plays; the game and
// writer threads cannot preempt it, so its updates are atomic for them.
extern "C" void th10_rec_audio_block(const int16_t* stereo){
    if(!audio_on)return;
    const u32 w=a_written;   // a multiple of 512, and the ring of 512s: a block never wraps
    avi::downmix(stereo,1024,aring+(w&aring_mask));
    a_last_us=now();a_written=w+512u;a_seq=a_seq+1;a_blocks=a_blocks+1;
}

namespace {
// Mono samples since the session started, now (game thread): the last
// block plus the time since it (at most one block).
u64 audio_pos_now(){
    const u64 t=now();
    if(wall_mode)return (t-t_start)*22050ull/1000000ull;
    u32 s,w;u64 last;
    do{s=a_seq;w=a_written;last=a_last_us;barrier();}while(s!=a_seq);
    u64 extra=t>last?(t-last)*22050ull/1000000ull:0;if(extra>512)extra=512;
    return u64(w)+extra;
}

// ---------------------------------------------------------- JPEG ring --
// A FIFO of JPEGs in encode order (one frame is encoded at a time, in capture
// order). The game thread reserves frame_reserve bytes at the head when an
// encode starts (skipping the ring's tail end if it is too short), commits
// the JPEG's real size when it is done (nothing when it failed), and
// publishes {offset, length, skip}; the writer releases the oldest in the
// same order (RecCore.hpp Mux keeps that order). allocated/released are byte
// counters (skips included), so in use = allocated - released.
struct Alloc {u32 off,len,skip;};
Alloc allocs[64];volatile u32 alloc_head=0,alloc_tail=0;   // head: game thread, tail: writer
u32 ring_pos=0;volatile u32 ring_allocated=0,ring_released=0;u32 ring_hw=0,ring_release_mismatch=0;
bool ring_reserve(u32& off,u32& skip){
    const u32 in_use=ring_allocated-ring_released;
    if(alloc_head-alloc_tail>=64u)return false;
    if(ring_pos+frame_reserve<=ring_size){if(in_use+frame_reserve>ring_size)return false;off=ring_pos;skip=0;return true;}
    skip=ring_size-ring_pos;if(in_use+skip+frame_reserve>ring_size)return false;off=0;return true;
}
void ring_commit(u32 off,u32 skip,u32 bytes){
    const u32 len=(bytes+63u)&~63u;Alloc& a=allocs[alloc_head%64u];a.off=off;a.len=len;a.skip=skip;
    ring_pos=off+len;ring_allocated=ring_allocated+skip+len;barrier();alloc_head=alloc_head+1;
    const u32 in_use=ring_allocated-ring_released;if(in_use>ring_hw)ring_hw=in_use;
}
void ring_release(u32 off){   // writer thread
    if(alloc_tail==alloc_head){++ring_release_mismatch;return;}
    const Alloc& a=allocs[alloc_tail%64u];if(a.off!=off)++ring_release_mismatch;
    ring_released=ring_released+a.skip+a.len;barrier();alloc_tail=alloc_tail+1;
}
u32 ring_in_use(){return ring_allocated-ring_released;}

// ------------------------------------------------------------ records --
// One per captured frame, in capture order; the game thread fills them and
// moves them to Done/Failed, the writer consumes them in order (RecCore.hpp Mux).
enum Reason : u32 {ReasonNone=0,ReasonSuperseded,ReasonOverflow,ReasonMeError,ReasonTimeout,ReasonDrained};
struct Record {volatile u32 state;u32 slot,fence,bytes,reason,ring_off,ring_blocked;int cap;u64 t_emit,t_ready;u32 tick;};
Record recs[rec_count];volatile u32 rec_head=0,rec_tail=0;   // head: game thread, tail: writer
Record& rec(u32 id){return recs[id%rec_count];}
bool cap_busy[cap_count]{};             // game thread only
u32 capq[cap_count];u32 capq_n=0;       // records whose GE copy is in flight, oldest first
int ready=-1;                           // captured, waiting for the encoder (newest only)

// ------------------------------------------------------------ sessions --
volatile u32 session=0,done_session=0;            // game: session; writer: done_session
volatile u32 stop_session=0,drain_session=0;       // game: stop_session; writer: drain_session (ended without a stop)
volatile u64 stop_slot=~0ull;                      // exclusive end of the stopping session
volatile u32 want=0;                               // the user wants recording (the dot)
bool start_pending=false;
bool me_ok=false,me_dead=false;bool inited=false;
u32 cur_tick=0,cur_late=0,cur_presents=0;
u32 last_cap_slot=0;bool have_cap_slot=false;int want_cap=-1;u32 want_slot=0;
bool capturing(){return want&&session!=done_session&&stop_session!=session&&drain_session!=session;}
bool dropping(){return drain_session==session;}

// --------------------------------------------- per-session measurements --
struct Stats {
    u32 captures=0,list_full=0,no_buffer_slots=0,superseded=0,encoded=0,overflow=0,me_errors=0,timeouts=0,drained=0,ring_full_ticks=0,ring_full_drops=0,sc_frames=0;
    Hist me_busy,me_span,sc_us,enc_wall,jpeg_kb,cap_ms;   // ME counts (converted at log time), SC us, us, bytes, us
    u64 jpeg_bytes=0;u32 last_nobuf_slot=~0u;
    unsigned audio0[8]{};u32 waits0=0,wwaits0=0;u64 busy0=0,abusy0=0,wbusy0=0;u32 late0=0,presents0=0,tick0=0;u64 t0=0;
    u32 late_before=0,ticks_before=0;
    void reset(){*this=Stats{};me_busy.reset(1u<<17);me_span.reset(1u<<17);sc_us.reset(1000);enc_wall.reset(1000);jpeg_kb.reset(1024);cap_ms.reset(1000);}
} st;
u32 boot_late=0,boot_ticks=0;

// ------------------------------------------------------- encode pipeline --
struct Enc {bool active=false;int rec=-1;u32 off=0,skip=0,total=0,submitted=0,collected=0,bad=0,first_start=0,last_end=0;u64 busy=0,start_us=0;u32 ring[128];} enc;   // <= 102 jobs (480 wide, 5 MCUs/job)
u32 jobs_per_row(){return (tables->mcus_x+cfg.mcus-1u)/cfg.mcus;}
u32 jobs_total(){return jobs_per_row()*tables->mcus_y;}
void free_cap(Record& r){if(r.cap>=0)cap_busy[r.cap]=false;r.cap=-1;}
void fail(Record& r,Reason why){free_cap(r);r.reason=why;if(r.ring_blocked&&(why==ReasonSuperseded||why==ReasonDrained))++st.ring_full_drops;barrier();r.state=avi::RecFailed;if(why==ReasonDrained)++st.drained;}
void finish_ok(Record& r,u32 bytes,u64 wall_us){   // the JPEG is in the ring at enc.off
    ring_commit(enc.off,enc.skip,bytes);
    free_cap(r);r.ring_off=enc.off;r.bytes=bytes;barrier();r.state=avi::RecDone;
    ++st.encoded;st.jpeg_bytes+=bytes;st.jpeg_kb.add(bytes);st.enc_wall.add(u32(wall_us));
}
// --- ME ---
void me_feed(){
    while(enc.submitted<enc.total&&enc.submitted-enc.collected<job_slots-1u&&ring_free()>1){   // never reuse an uncollected slot (G0b fix)
        const u32 k=enc.submitted,jpr=jobs_per_row(),row=k/jpr,col0=(k%jpr)*cfg.mcus,n=std::min(cfg.mcus,tables->mcus_x-col0);
        u32 flags=cfg.size==320?JpegScale:0u;if(k==0)flags|=JpegFirst;if(k+1==enc.total)flags|=JpegLast;
        const Record& r=rec(u32(enc.rec));
        const u32 index=submit(JobJpeg,reinterpret_cast<u32>(cap_buf[r.cap]),reinterpret_cast<u32>(tables),reinterpret_cast<u32>(&th10_rec_jctx),
            ring_base+enc.off,frame_reserve,(row<<16)|col0,(flags<<16)|n,reinterpret_cast<u32>(scratch_me));
        if(index==~0u)break;
        enc.ring[enc.submitted++]=index;
    }
}
void me_start(int id,u32 off,u32 skip){
    enc=Enc{};enc.active=true;enc.rec=id;enc.off=off;enc.skip=skip;enc.total=jobs_total();enc.start_us=now();
    volatile JpegCtx* c=jctx();c->pos=0;c->acc=0;c->nbits=0;c->overflow=0;c->dc0=c->dc1=c->dc2=0;c->blocks=0;c->flat=0;sync();
    me_feed();
}
void me_poll(u64 t){
    while(enc.collected<enc.submitted&&finished(enc.ring[enc.collected])){
        volatile Job* j=slot(enc.ring[enc.collected]);
        if(j->status!=StatusOk)++enc.bad;
        const u32 s0=j->count_start,e0=j->count_end;enc.busy+=u32(e0-s0);
        if(enc.collected==0)enc.first_start=s0;
        if(enc.collected+1==enc.total)enc.last_end=e0;
        ++enc.collected;
    }
    Record& r=rec(u32(enc.rec));
    if(enc.collected==enc.total){
        volatile JpegCtx* c=jctx();const u32 bytes=c->pos,over=c->overflow;
        st.me_busy.add(u32(enc.busy));st.me_span.add(u32(enc.last_end-enc.first_start));   // ME counts
        if(!enc.bad&&!over&&bytes)finish_ok(r,bytes,t-enc.start_us);
        else if(over){++st.overflow;fail(r,ReasonOverflow);}else{++st.me_errors;fail(r,ReasonMeError);}   // nothing committed: the reservation is simply reused
        enc.active=false;return;
    }
    me_feed();
    if(t-enc.start_us>3000000){   // the ME stopped answering: keep its capture buffer (a late job may still read it); the ring reservation is not committed
        ++st.timeouts;me_dead=true;me_ok=false;r.cap=-1;fail(r,ReasonTimeout);enc.active=false;
        rlog("rec ME TIMEOUT: frame jobs %u/%u collected after 3 s; the ME is no longer used for recording\n",enc.collected,enc.total);
    }
}
// --- SC (no ME: PPSSPP): the writer thread encodes into the Main RAM ring ---
volatile u32 sc_state=0;volatile u32 sc_bytes=0,sc_us=0;   // 1 requested (game), 2 done (writer)
void sc_start(int id,u32 off,u32 skip){enc=Enc{};enc.active=true;enc.rec=id;enc.off=off;enc.skip=skip;enc.start_us=now();barrier();sc_state=1;}
void sc_poll(u64 t){
    if(sc_state!=2)return;
    Record& r=rec(u32(enc.rec));const u32 bytes=sc_bytes;
    ++st.sc_frames;st.sc_us.add(sc_us);
    if(bytes)finish_ok(r,bytes,t-enc.start_us);else{++st.overflow;fail(r,ReasonOverflow);}
    enc.active=false;barrier();sc_state=0;
}
// --- ME clock rate: two clock jobs 5-8 s apart, submitted with the ring idle ---
struct Clock {int state=0;u32 index=0,count_a=0;u64 t_submit=0,t_a=0;} clk;
void clock_poll(u64 t){
    if(!me_ok||clk.state==4)return;
    if(clk.state==1||clk.state==3){
        if(finished(clk.index)){const u32 c=slot(clk.index)->r[0];
            if(clk.state==1){clk.count_a=c;clk.t_a=clk.t_submit;clk.state=2;}
            else{counts_per_us=double(u32(c-clk.count_a))/double(clk.t_submit-clk.t_a);clk.state=4;
                rlog("rec ME count rate %.3f counts/us (%.2f s between the clock jobs)\n",counts_per_us,(clk.t_submit-clk.t_a)/1e6);}}
        else if(t-clk.t_submit>500000){clk.state=4;rlog("rec ME clock job unanswered; ME times stay in counts\n");}
        return;
    }
    if(enc.active)return;
    if(clk.state==2&&t-clk.t_a>8000000)clk.state=0;   // the 32-bit count wraps in 10.2 s (419.68 counts/us, G0b on the Go): start over
    if(clk.state==0||(clk.state==2&&t-clk.t_a>=5000000)){
        volatile Box* r=ring();if(r->submitted!=r->completed)return;
        const u32 index=submit(JobClock);if(index==~0u)return;clk.index=index;clk.t_submit=now();clk.state=clk.state==0?1:3;
    }
}
bool clock_busy(){return clk.state==1||clk.state==3;}
void encoder_poll(){
    const u64 t=now();
    widen_busy();
    while(capq_n){   // GE copies done (lists finish in order) -> ready; a newer frame supersedes a waiting one
        const u32 id=capq[0];Record& r=rec(id);
        if(!th10_rec_ge_done(r.fence))break;
        for(u32 i=1;i<capq_n;i++){capq[i-1]=capq[i];}--capq_n;
        r.t_ready=t;st.cap_ms.add(u32(t-r.t_emit));
        if(dropping()){fail(r,ReasonDrained);continue;}
        if(ready>=0){++st.superseded;fail(rec(u32(ready)),ReasonSuperseded);}
        ready=int(id);
    }
    if(ready>=0&&dropping()){fail(rec(u32(ready)),ReasonDrained);ready=-1;}
    if(enc.active){if(me_ok)me_poll(t);else sc_poll(t);}
    clock_poll(t);
    if(!enc.active&&ready>=0&&!clock_busy()&&(me_ok||!ring_in_edram)){
        u32 off=0,skip=0;
        if(!ring_reserve(off,skip)){++st.ring_full_ticks;rec(u32(ready)).ring_blocked=1;}   // the writer is behind (or paused): the frame waits, a newer one replaces it
        else{const int id=ready;ready=-1;if(me_ok)me_start(id,off,skip);else sc_start(id,off,skip);}
    }
}

// ---------------------------------------------------------- control --
bool select_was_down=false;u64 last_toggle_us=0;
bool select_down(){SceCtrlData pad{};return sceCtrlPeekBufferPositive(&pad,1)>0&&(pad.Buttons&PSP_CTRL_SELECT);}
void request_start(){
    if(!enabled){rlog("SELECT at tick %u: recording is off (%s)\n",cur_tick,disabled_reason);return;}
    if(session!=done_session){start_pending=true;rlog("SELECT at tick %u: start queued (the previous file is still being finished)\n",cur_tick);return;}
    if(!me_dead)me_ok=th10_rec_me_ready()!=0;
    if(ring_in_edram&&!me_ok){rlog("SELECT at tick %u: no ME, and the JPEG ring is in ME eDRAM (ring_edram=1): not recording\n",cur_tick);return;}
    // The writer is idle: the record ring and the JPEG ring are empty and nothing of an old session is in flight.
    rec_tail=rec_head;capq_n=0;ready=-1;have_cap_slot=false;
    alloc_tail=alloc_head;ring_released=ring_allocated;ring_pos=0;ring_hw=0;ring_release_mismatch=0;
    st.reset();st.t0=now();st.tick0=cur_tick;st.late0=cur_late;st.presents0=cur_presents;st.late_before=cur_late-boot_late;st.ticks_before=cur_tick-boot_ticks;
    th10_rec_audio_counters(st.audio0);th10_rec_audio_level_reset();
    {volatile Box* r=ring();st.waits0=r->audio_waits;r->audio_wait_max=0;volatile Box* w=wring();st.wwaits0=w->audio_waits;w->audio_wait_max=0;}
    widen_busy();st.busy0=busy64;st.abusy0=audio_busy64;st.wbusy0=wbusy64;
    audio_on=0;a_written=0;a_seq=a_seq+1;a_last_us=now();t_start=now();audio_lost=0;a_blocks=0;reads_session=ReadStats{};reads_before=reads_boot;pauses_session=PauseStats{};
    wall_mode=!th10_rec_audio_running();
    if(!wall_mode)audio_on=1;
    stop_slot=~0ull;want=1;barrier();
    session=session+1;
    rlog("SELECT at tick %u: start session %u (encoder %s, audio %s, JPEG ring %s %u KiB)\n",cur_tick,session,me_ok?"ME":"SC",wall_mode?"none (silence on the wall clock)":"played blocks",
        ring_in_edram?"ME eDRAM":"Main RAM",ring_size/1024u);
}
void request_stop(){
    want=0;start_pending=false;
    if(session==done_session)return;
    const u64 s=audio_pos_now()/spf()+1u;   // the current slot is the last one
    stop_slot=s;barrier();stop_session=session;
    rlog("stop at tick %u: session %u ends with slot %llu\n",cur_tick,session,s);
}
}

// =========================================================== GE hooks ======
extern "C" void* th10_rec_capture_want(void){
    if(!inited||!capturing())return nullptr;
    const u32 s=u32(audio_pos_now()/spf());
    if(have_cap_slot&&s<=last_cap_slot)return nullptr;   // one picture per video slot
    int b=-1;for(u32 i=0;i<cap_count;i++)if(!cap_busy[i]){b=int(i);break;}
    if(b<0||rec_head-rec_tail>=rec_count||capq_n>=cap_count){if(st.last_nobuf_slot!=s){st.last_nobuf_slot=s;++st.no_buffer_slots;}return nullptr;}
    want_cap=b;want_slot=s;return cap_buf[b];
}
extern "C" void th10_rec_capture_emitted(unsigned fence){
    const u32 id=rec_head;Record& r=rec(id);
    r.slot=want_slot;r.fence=fence;r.bytes=0;r.reason=0;r.ring_off=0;r.ring_blocked=0;r.cap=want_cap;r.t_emit=now();r.t_ready=0;r.tick=cur_tick;r.state=avi::RecPending;
    cap_busy[want_cap]=true;capq[capq_n++]=id;last_cap_slot=want_slot;have_cap_slot=true;++st.captures;
    barrier();rec_head=id+1;
}
extern "C" void th10_rec_capture_skipped(void){++st.list_full;}
extern "C" int th10_rec_indicator(void){return inited&&want&&drain_session!=session?1:0;}

// ======================================================== writer thread ======
namespace {
struct PspFs {
    static int open_write(const char* p){return sceIoOpen(p,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);}
    static int open_rw(const char* p){return sceIoOpen(p,PSP_O_RDWR,0777);}
    static int open_read(const char* p){return sceIoOpen(p,PSP_O_RDONLY,0777);}
    static int write(int f,const void* d,u32 n){in_io=1;const int r=sceIoWrite(f,d,n);in_io=0;return r;}
    static int read(int f,void* d,u32 n){in_io=1;const int r=sceIoRead(f,d,n);in_io=0;return r;}
    static long long seek(int f,u64 pos){return sceIoLseek(f,SceOff(pos),PSP_SEEK_SET);}
    static long long size(int f){const SceOff cur=sceIoLseek(f,0,PSP_SEEK_CUR),end=sceIoLseek(f,0,PSP_SEEK_END);sceIoLseek(f,cur,PSP_SEEK_SET);return end;}
    static int close(int f){return sceIoClose(f);}
    static int remove(const char* p){return sceIoRemove(p);}
    static u64 now_us(){return now();}
    static void before_write(const void* p,u32 n){sceKernelDcacheWritebackRange(p,n);}   // the SC's bytes of a write buffer in RAM (the ME's are already there)
};
u32 copy_jobs=0,copy_timeouts=0;Hist copy_wait;
// Frame -> game tick of a session (debugging: line a device frame up with a
// PC/PPSSPP render of the same tick). Written next to the AVI as <base>.TCK:
// "TH10" "TCK1", u32 frames, u32 fps, u32 truncated, u32 0, then one u32 tick
// per frame, every part of the session in order. 16,384 frames = 18 min at 15 fps.
constexpr u32 tck_cap=16384u;u32 tck[tck_cap];u32 tck_n=0;bool tck_trunc=false;
struct PspSrc {
    u64 audio_written(){if(wall_mode)return (now()-t_start)*22050ull/1000000ull;return a_written;}
    void audio_read(u64 at,i16* out,u32 n){
        if(wall_mode){std::memset(out,0,n*2u);return;}
        for(u32 i=0;i<n;i++)out[i]=aring[u32(at+i)&aring_mask];
        // The audio thread may have lapped the ring meanwhile: samples older
        // than one block short of a full ring are silence (counted).
        const u64 w=a_written;const u64 oldest=w>aring_size-512u?w-(aring_size-512u):0;
        for(u32 i=0;i<n;i++)if(at+i<oldest){out[i]=0;++audio_lost;}
    }
    u32 records(){return rec_head-rec_tail;}
    u32 record_slot(u32 i){return rec(rec_tail+i).slot;}
    u32 record_state(u32 i){return rec(rec_tail+i).state;}
    u32 record_bytes(u32 i){return rec(rec_tail+i).bytes;}
    u32 record_handle(u32 i){return rec(rec_tail+i).ring_off;}
    u32 record_tick(u32 i){return rec(rec_tail+i).tick;}
    void note_frame(u64 f,u32 tick){if(f<tck_cap){tck[f]=tick;tck_n=u32(f+1u);}else tck_trunc=true;}
    // `n` JPEG bytes from the ring (record at `handle`, from `off`) into a
    // write buffer. ME: a copy job on the writer's ring (served before the
    // encode jobs); the SC first writes back and invalidates the destination's
    // lines (its chunk header shares the first one), the ME then loads, fills
    // and writes back whole lines. No ME (Main RAM ring, SC encoder): memcpy.
    bool copy(u32 handle,u32 off,u8* dst,u32 n){
        const u32 src=ring_base+handle+off;
        if(!me_ok){std::memcpy(dst,reinterpret_cast<const void*>(src),n);return true;}
        const u32 a=reinterpret_cast<u32>(dst)&~63u,b=(reinterpret_cast<u32>(dst)+n+63u)&~63u;
        sceKernelDcacheWritebackInvalidateRange(reinterpret_cast<const void*>(a),b-a);
        const u32 index=submit_to(wring(),wslot,wjob_slots,JobCopy,src,reinterpret_cast<u32>(dst),n);
        if(index==~0u)return false;
        ++copy_jobs;const u64 t0=now();
        while(wslot(index)->done!=index+1){if(now()-t0>1000000){++copy_timeouts;return false;}sceKernelDelayThread(50);}
        copy_wait.add(u32(now()-t0));
        return wslot(index)->status==StatusOk;
    }
    void pop(){barrier();rec_tail=rec_tail+1;}
    void release(u32 handle){ring_release(handle);}
};
avi::WriteStats wst;avi::Writer<PspFs> wr;avi::Mux<PspFs,PspSrc> mux;PspSrc src;
SceUID io_thread=-1;volatile bool io_run=false;
bool ws_active=false;int ws_phase=0;u32 ws_session=0,ws_part=0;u64 ws_t0=0,ws_last_progress=0,budget=0;char ws_base[160],ws_file[200];const char* ws_why="";
// When to start a write (see read_end_low): 0 no, 1 just after a game read,
// 2 no reads lately (paced), 3 forced (the JPEG ring is 3/4 full, or
// write_policy=0). Never while the BGM side has us paused.
constexpr u32 window_us=120000,quiet_us=400000,gap_us=100000;
u32 last_write_low=0;struct WriteKinds {u32 window=0,quiet=0,forced=0,log_writes=0;} wk;
int writer_window(){
    if(pause_req)return 0;
    const u32 t=sceKernelGetSystemTimeLow(),since_read=t-read_end_low,since_write=t-last_write_low;
    if(!cfg.write_policy||ring_in_use()*4u>=ring_size*3u)return 3;
    if(reads_seen&&since_read<=window_us)return 1;
    if((!reads_seen||since_read>=quiet_us)&&since_write>=gap_us)return 2;
    return 0;
}
bool pump_writes(){   // full 64 KiB buffers to the file while a window is open
    bool did=false;
    while(wr.full()){
        const int k=writer_window();if(!k)break;
        if(!wr.write_one())return did;
        last_write_low=sceKernelGetSystemTimeLow();did=true;
        if(k==1)++wk.window;else if(k==2)++wk.quiet;else ++wk.forced;
    }
    if(wr.want_spill()){const int k=writer_window();if(k==1||k==2){wr.spill_index();last_write_low=sceKernelGetSystemTimeLow();did=true;}}
    return did;
}

bool free_space(u64& bytes){
    SceDevInf inf{};SceDevctlCmd cmd{&inf};
    if(sceIoDevctl("ms0:",SCE_PR_GETDEV,&cmd,sizeof(cmd),nullptr,0)<0)return false;
    bytes=u64(inf.freeClusters)*u64(u32(inf.sectorCount))*u64(u32(inf.sectorSize));return true;
}
bool exists(const char* p){SceIoStat s;return sceIoGetstat(p,&s)>=0;}
void make_base(){
    ScePspDateTime d{};char stem[64];
    if(sceRtcGetCurrentClockLocalTime(&d)>=0)std::snprintf(stem,sizeof(stem),"TH10_%04u%02u%02u_%02u%02u%02u",unsigned(d.year),unsigned(d.month),unsigned(d.day),unsigned(d.hour),unsigned(d.minute),unsigned(d.second));
    else std::snprintf(stem,sizeof(stem),"TH10_%08x",unsigned(sceKernelGetSystemTimeLow()));
    std::snprintf(ws_base,sizeof(ws_base),"ms0:/VIDEO/TH10/%s",stem);
    for(char c='b';c<='z';c++){char p[200];std::snprintf(p,sizeof(p),"%s.AVI",ws_base);if(!exists(p))break;std::snprintf(ws_base,sizeof(ws_base),"ms0:/VIDEO/TH10/%s%c",stem,c);}
}
void part_name(u32 part){if(part<=1)std::snprintf(ws_file,sizeof(ws_file),"%s.AVI",ws_base);else std::snprintf(ws_file,sizeof(ws_file),"%s_%u.AVI",ws_base,part);}

// ---- repair of an unfinished file (a crash before the stop) ----
struct RepairRun {int phase=0;char names[16][64];u32 count=0,at=0;u64 t0=0;avi::Repair<PspFs> r;} rp;
void repair_abandon(){if(rp.phase==2){rp.r.abandon();rlog("repair of %s left for the next start (a recording began)\n",rp.names[rp.at]);}if(rp.phase)rp.phase=3;}
bool repair_step(){
    if(rp.phase==3)return false;
    if(!writer_window())return false;   // its reads, like our writes, keep clear of the game's reads and of a pause
    if(rp.phase==0){
        const SceUID d=sceIoDopen("ms0:/VIDEO/TH10");
        if(d>=0){SceIoDirent e;for(;;){std::memset(&e,0,sizeof(e));if(sceIoDread(d,&e)<=0)break;const size_t n=std::strlen(e.d_name);
            if(n>4&&n<60&&rp.count<16&&!strcasecmp(e.d_name+n-4,".AVI"))std::snprintf(rp.names[rp.count++],64,"%s",e.d_name);}sceIoDclose(d);}
        rp.phase=1;return true;
    }
    if(rp.phase==1){
        while(rp.at<rp.count){char p[200];std::snprintf(p,sizeof(p),"ms0:/VIDEO/TH10/%s",rp.names[rp.at]);
            if(rp.r.begin(p,spill_path,&wr,wbuf[0])){rp.phase=2;rp.t0=now();rlog("repair %s: unfinished (no idx1), walking its chunks\n",p);return true;}
            ++rp.at;}
        rp.phase=3;return true;
    }
    if(rp.r.walk(32))return true;
    const bool ok=rp.r.finish();
    rlog("repair ms0:/VIDEO/TH10/%s: %s frames=%u (%.1f s) kept up to byte %llu of %llu, %.1f s\n",rp.names[rp.at],ok?"OK":"FAILED",rp.r.t.frames,rp.r.t.frames/double(rp.r.params.fps?rp.r.params.fps:15),
        rp.r.pair_end,rp.r.size,(now()-rp.t0)/1e6);
    log_flush_req=1;++rp.at;rp.phase=1;return true;
}

// ---- one recording session ----
void session_log(const char* what){
    const u64 t=now();const double wall=(t-ws_t0)/1e6;unsigned a[8];th10_rec_audio_counters(a);volatile Box* r=ring();volatile Box* w=wring();
    const double slots=double(mux.st.slots);
    rlog("rec %s session=%u file=%s parts=%u wall=%.1fs video=%.1fs slots=%llu real=%u repeats=%u empty=%u lead_skipped=%u superseded_in_slot=%u failed=%u\n",
        what,ws_session,ws_file,ws_part,wall,slots/cfg.fps,(unsigned long long)mux.st.slots,mux.st.real,mux.st.repeats,mux.st.empty,mux.st.lead,mux.st.superseded,mux.st.failed);
    rlog("rec %s capture: emitted=%u list_full=%u no_buffer_slots=%u waiting_frame_superseded=%u drained=%u ge_copy+poll ms p50/p95/max=%u/%u/%u\n",
        what,st.captures,st.list_full,st.no_buffer_slots,st.superseded,st.drained,st.cap_ms.pct(.5)/1000,st.cap_ms.pct(.95)/1000,st.cap_ms.max/1000);
    rlog("rec %s ring: %s %uKiB high_water=%uKiB (%u%%) frames_dropped_ring_full=%u ring_full_ticks=%u release_mismatch=%u | pauses=%u longest=%.0fms total=%.1fs\n",
        what,ring_in_edram?"ME eDRAM":"Main RAM",ring_size/1024u,ring_hw/1024u,ring_size?ring_hw*100u/ring_size:0u,st.ring_full_drops,st.ring_full_ticks,ring_release_mismatch,
        pauses_session.count,pauses_session.longest_us/1000.0,pauses_session.total_us/1e6);
    rlog("rec %s encode(%s%s): ok=%u overflow=%u errors=%u timeouts=%u sc_frames=%u | ME per frame busy ms p50/p95/max=%.1f/%.1f/%.1f span ms p50/p95/max=%.1f/%.1f/%.1f (counts p50 %u, rate %.3f/us%s) | SC encode ms p50/p95/max=%u/%u/%u | start->done ms p50/p95/max=%u/%u/%u | jpeg KiB p50/p95/max=%u/%u/%u avg=%.1f -> %.0f kbps at %u fps (%.2f MB/s)\n",
        what,me_ok?"ME":"SC",me_dead?", ME timed out":"",st.encoded,st.overflow,st.me_errors,st.timeouts,st.sc_frames,
        us_of(st.me_busy.pct(.5))/1000.0,us_of(st.me_busy.pct(.95))/1000.0,us_of(st.me_busy.max)/1000.0,us_of(st.me_span.pct(.5))/1000.0,us_of(st.me_span.pct(.95))/1000.0,us_of(st.me_span.max)/1000.0,
        st.me_busy.pct(.5),counts_per_us,counts_per_us>0?"":" NOT CALIBRATED: ms are 0",st.sc_us.pct(.5)/1000,st.sc_us.pct(.95)/1000,st.sc_us.max/1000,
        st.enc_wall.pct(.5)/1000,st.enc_wall.pct(.95)/1000,st.enc_wall.max/1000,st.jpeg_kb.pct(.5)/1024,st.jpeg_kb.pct(.95)/1024,st.jpeg_kb.max/1024,
        st.encoded?st.jpeg_bytes/1024.0/st.encoded:0.0,st.encoded?st.jpeg_bytes*8.0*cfg.fps/st.encoded/1000.0:0.0,cfg.fps,st.encoded?st.jpeg_bytes*double(cfg.fps)/st.encoded/1e6:0.0);
    rlog("rec %s audio: mode=%s samples=%llu lost=%u blocks=%u | underruns+%u stretched+%u dropped_jobs+%u level_min=%d frames | rec jobs delaying audio=%u+%u (longest %.0f/%.0f us encode/copy) | ME busy audio=%.1f ms/s encode=%.1f ms/s copy=%.1f ms/s\n",
        what,wall_mode?"silence":"played",(unsigned long long)mux.st.audio_samples,audio_lost,a_blocks,a[0]-st.audio0[0],a[1]-st.audio0[1],a[2]-st.audio0[2],a[5]==~0u?-1:int(a[5]),
        r->audio_waits-st.waits0,w->audio_waits-st.wwaits0,us_of(r->audio_wait_max),us_of(w->audio_wait_max),
        wall>0?us_of(audio_busy64-st.abusy0)/1000.0/wall:0.0,wall>0?us_of(busy64-st.busy0)/1000.0/wall:0.0,wall>0?us_of(wbusy64-st.wbusy0)/1000.0/wall:0.0);
    rlog("rec %s write: prio=0x%x piece=64KiB bytes=%llu writes=%u MB/s in_write=%.2f overall=%.2f p99=%.0fms max=%.1fms >50ms=%u >100ms=%u >500ms=%u >1s=%u short=%u errors=%u first_error=0x%08x | started: after_game_read=%u quiet_paced=%u forced=%u log_appends=%u idx_spills=%u (forced %u) | ME copies=%u wait us p50/p95/max=%u/%u/%u timeouts=%u | finalize_max=%.1fms files=%u budget=%.0fMiB\n",
        what,cfg.writer_prio,(unsigned long long)wst.bytes,wst.writes,wst.write_us?wst.bytes/double(wst.write_us):0.0,wall>0?wst.bytes/wall/1e6:0.0,wst.p99_us()/1000.0,wst.max_us/1000.0,wst.over50,wst.over100,wst.over500,wst.over1000,wst.shorts,wst.errors,unsigned(wst.first_error),
        wk.window,wk.quiet,wk.forced,wk.log_writes,wst.spills,wst.forced_spills,copy_jobs,copy_wait.pct(.5),copy_wait.pct(.95),copy_wait.max,copy_timeouts,wst.finalize_max_us/1000.0,wst.files,budget/1048576.0);
    {const ReadStats& g=reads_session;const ReadStats& b=reads_before;
     rlog("rec %s game reads (do our writes delay them?): n=%u %.1fMiB avg=%.1fms max=%.1fms >50ms=%u >200ms=%u | before this recording (since start): n=%u avg=%.1fms max=%.1fms >50ms=%u >200ms=%u\n",
        what,g.n,g.bytes/1048576.0,g.n?g.us/1000.0/g.n:0.0,g.max_us/1000.0,g.over50,g.over200,b.n,b.n?b.us/1000.0/b.n:0.0,b.max_us/1000.0,b.over50,b.over200);}
    rlog("rec %s game: ticks+%u late+%u presents+%u (before this session: late %u of %u ticks)\n",what,cur_tick-st.tick0,cur_late-st.late0,cur_presents-st.presents0,st.late_before,st.ticks_before);
    log_flush_req=1;
}
void begin_session(u32 s){
    repair_abandon();
    ws_active=true;ws_session=s;ws_phase=1;ws_part=1;ws_t0=now();ws_last_progress=ws_t0;ws_why="stop";wst=avi::WriteStats{};wk=WriteKinds{};ws_file[0]=0;copy_jobs=copy_timeouts=0;copy_wait.reset(100);
    const char* fail=nullptr;u64 room=0;
    if(!free_space(room))fail="free space query failed";
    else if(room<(u64(cfg.min_free_mb)<<20)+(16u<<20))fail="not enough free space on ms0:";
    if(!fail){
        budget=room-(u64(cfg.min_free_mb)<<20);
        sceIoMkdir("ms0:/VIDEO",0777);sceIoMkdir("ms0:/VIDEO/TH10",0777);
        make_base();part_name(1);
        if(!wr.open(ws_file,spill_path,params()))fail="cannot create the file";
    }
    if(fail){rlog("rec start FAILED session=%u: %s (free %.1f MiB, need %u MiB + 16)\n",s,fail,room/1048576.0,cfg.min_free_mb);drain_session=s;ws_phase=2;log_flush_req=1;return;}
    tck_n=0;tck_trunc=false;
    mux.begin(&wr,&src,spf(),TH10_REC_DROP_EMPTY!=0);mux.split_bytes=u64(cfg.split_mb)<<20;mux.budget=budget;
    rlog("rec start session=%u file=%s %ux%u %u fps q%u spf=%u free=%.1fMiB budget=%.1fMiB ring=%s %uKiB\n",s,ws_file,params().width,params().height,cfg.fps,cfg.quality,spf(),room/1048576.0,budget/1048576.0,
        ring_in_edram?"eDRAM":"Main RAM",ring_size/1024u);
    log_flush_req=1;
}
void write_ticks(){
    if(!tck_n)return;char p[240];std::snprintf(p,sizeof(p),"%s.TCK",ws_base);
    const SceUID f=sceIoOpen(p,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(f<0){rlog("rec ticks: cannot create %s\n",p);return;}
    const u32 h[6]{0x30314854u,0x314b4354u,tck_n,cfg.fps,tck_trunc?1u:0u,0u};
    const bool ok=sceIoWrite(f,h,sizeof(h))==int(sizeof(h))&&sceIoWrite(f,tck,tck_n*4u)==int(tck_n*4u);sceIoClose(f);
    rlog("rec ticks: %s frames=%u%s %s\n",p,tck_n,tck_trunc?" (truncated)":"",ok?"written":"WRITE FAILED");
}
void end_recording(bool normal,const char* why){
    ws_why=why;
    if(!normal)drain_session=ws_session;   // the game thread stops capturing and fails what it holds
    const bool ok=wr.file>=0?wr.finalize():false;
    mux.end();
    write_ticks();
    rlog("rec %s session=%u: %s, file %s %s\n",normal?"stop":"END",ws_session,why,ws_file,ok?"finished (idx1 + header)":wr.t.frames?"NOT finished (repaired on the next start)":"removed (no frames)");
    session_log("end");
    ws_phase=2;
}
bool drain(){   // every record of the session consumed, pictures released (in encode order)
    while(rec_tail!=rec_head){Record& r=rec(rec_tail);if(r.state==avi::RecPending)return false;if(r.state==avi::RecDone)ring_release(r.ring_off);barrier();rec_tail=rec_tail+1;}
    audio_on=0;ws_active=false;ws_phase=0;barrier();done_session=ws_session;log_flush_req=1;return true;
}
bool run_session(){
    bool did=false;
    if(ws_phase==1){
        const u64 limit=stop_session==ws_session?u64(stop_slot):~0ull;
        for(int i=0;i<64&&ws_phase==1;i++){
            const avi::Step s=mux.step(limit);
            if(s==avi::StepEmitted||s==avi::StepSkipped){did=true;continue;}
            if(s==avi::StepRoom){if(pump_writes()){did=true;continue;}break;}   // both buffers full: wait for a write window (the ring holds the frames meanwhile)
            if(pause_req)break;   // split, stop, card full and errors finish a file (writes): not while paused
            if(s==avi::StepSplit){did=true;
                const bool ok=wr.finalize();rlog("rec split: %s %s (%u frames)\n",ws_file,ok?"finished":"NOT finished",wr.t.frames);
                part_name(++ws_part);if(!wr.open(ws_file,spill_path,params()))end_recording(false,"cannot create the next file");continue;}
            if(s==avi::StepFull){end_recording(false,"card full");break;}
            if(s==avi::StepError){end_recording(false,wr.error?"write error":"ME copy failed");break;}
            if(s==avi::StepDone){end_recording(true,"stopped");break;}
            break;   // StepWait
        }
        if(ws_phase==1){did|=pump_writes();if(wr.error)end_recording(false,"write error");}
        if(ws_phase==1&&now()-ws_last_progress>=u64(cfg.progress_s)*1000000u){ws_last_progress=now();session_log("progress");}
    }
    if(ws_phase==2)did|=drain();
    return did;
}
// The SC encoder (no ME), in this thread so the game thread never waits for it.
void sc_encode(){
    const Record& r=rec(u32(enc.rec));const u64 t0=now();
    sceKernelDcacheInvalidateRange(cap_buf[r.cap],frame_bytes);   // the GE wrote it
    jp::State s;const u32 n=jp::encode_frame(*tables,reinterpret_cast<const jp::u16*>(cap_buf[r.cap]),width,reinterpret_cast<u8*>(ring_base+enc.off),frame_reserve,cfg.mcus,scratch_sc,s);
    sc_us=u32(now()-t0);sc_bytes=n;barrier();sc_state=2;
}
int io_main(SceSize,void*){
    pspSdkDisableFPUExceptions();   // checklist A-6: every thread starts with FPU traps enabled
    u64 last_power=now();const u64 repair_after=now()+10000000ull;
    while(io_run){
        bool did=false;
        if(!ws_active&&session!=done_session&&!pause_req){begin_session(session);did=true;}
        if(ws_active)did|=run_session();
        else if(cfg.repair&&enabled&&now()>repair_after)did|=repair_step();
        if(sc_state==1){sc_encode();did=true;}
        const u64 t=now();
        if(ws_active&&t-last_power>=1000000){scePowerTick(PSP_POWER_TICK_ALL);last_power=t;}   // TH08 platform.cpp PowerKeepAliveThread
        if(log_flush_req&&!pause_req){const int k=ws_active&&ws_phase==1?writer_window():1;   // the log is file I/O on the M2 too (G0b: one append took 627 ms)
            if(k==1||k==2||!cfg.write_policy){log_flush_req=0;in_io=1;log_write_out();in_io=0;if(ws_active)++wk.log_writes;}}
        if(!did)sceKernelDelayThread(ws_active?4000:20000);
    }
    log_write_out();
    return 0;
}
}

// ================================================================ API ======
extern "C" void th10_rec_init(const char* dir){
    if(inited)return;
    std::snprintf(game_dir,sizeof(game_dir),"%s",dir?dir:"ms0:/PSP/GAME/TH10GE");
    std::snprintf(log_path,sizeof(log_path),"%s/th10_rec_log.txt",game_dir);
    std::snprintf(spill_path,sizeof(spill_path),"%s/th10_rec_idx.tmp",game_dir);
    {SceIoStat s;under_ppsspp=sceIoGetstat("ms0:/PSP/SYSTEM/ppsspp.ini",&s)>=0;}
    read_config();
    log_sema=sceKernelCreateSema("th10_rec_log",0,1,1,nullptr);
    if(!std::strncmp(log_path,"ms0:",4)){const SceUID f=sceIoOpen(log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(f>=0)sceIoClose(f);}
    rlog("TH10_BUILD_ID=" TH10_BUILD_ID "\n");
    const struct mallinfo m0=mallinfo();
    enabled=!std::strncmp(game_dir,"ms0:",4);
    if(!enabled)disabled_reason="the game folder is not on ms0: (the Go's M2); nothing is written to internal storage";
    else if(!alloc_all()){enabled=false;disabled_reason="buffer allocation failed";}
    const struct mallinfo m1=mallinfo();
    rlog("rec init: platform=%s dir=%s enabled=%d%s%s | %ux%u %u fps q%u mcus/job=%u jobs/frame=%u | JPEG ring %s %uKiB (eDRAM 0x%06x..0x%06x per MeEdramMap.hpp) | writer_prio=0x%x policy=%u split=%uMiB min_free=%uMiB auto_start=%u auto_stop=%u drop=%s | heap in_use %u -> %u (+%u) free=%u\n",
        under_ppsspp?"ppsspp":"device",game_dir,enabled?1:0,enabled?"":" reason=",enabled?"":disabled_reason,params().width,params().height,cfg.fps,cfg.quality,cfg.mcus,tables?jobs_total():0u,
        ring_in_edram?"ME eDRAM":"Main RAM",ring_size/1024u,th10::edram::rec_ring.offset,th10::edram::rec_ring.offset+th10::edram::rec_ring.bytes,
        cfg.writer_prio,cfg.write_policy,cfg.split_mb,cfg.min_free_mb,cfg.auto_start,cfg.auto_stop,TH10_REC_DROP_EMPTY?"zero-length chunk":"repeat previous JPEG",
        u32(m0.uordblks),u32(m1.uordblks),u32(m1.uordblks-m0.uordblks),u32(m1.fordblks));
    std::memset(&th10_rec_box,0,sizeof(th10_rec_box));std::memset(th10_rec_jobs,0,sizeof(th10_rec_jobs));std::memset(&th10_rec_jctx,0,sizeof(th10_rec_jctx));
    std::memset(&th10_rec_wbox,0,sizeof(th10_rec_wbox));std::memset(th10_rec_wjobs,0,sizeof(th10_rec_wjobs));
    th10_rec_box.jobs_base=reinterpret_cast<u32>(th10_rec_jobs);th10_rec_wbox.jobs_base=reinterpret_cast<u32>(th10_rec_wjobs);
    sceKernelDcacheWritebackInvalidateRange(&th10_rec_box,sizeof(th10_rec_box));sceKernelDcacheWritebackInvalidateRange(th10_rec_jobs,sizeof(th10_rec_jobs));
    sceKernelDcacheWritebackInvalidateRange(&th10_rec_wbox,sizeof(th10_rec_wbox));sceKernelDcacheWritebackInvalidateRange(th10_rec_wjobs,sizeof(th10_rec_wjobs));
    sceKernelDcacheWritebackInvalidateRange(&th10_rec_jctx,sizeof(th10_rec_jctx));
    if(enabled){wr.setup(wbuf[0],wbuf[1],piece,index_buf,index_bytes,&wst);st.reset();}
    copy_wait.reset(100);
    select_was_down=select_down();   // a SELECT held at start is not a press (TH08 render_cadence.cpp Reset)
    inited=true;
    io_run=true;io_thread=sceKernelCreateThread("th10_rec_writer",io_main,int(cfg.writer_prio),0x8000,PSP_THREAD_ATTR_USER,nullptr);
    if(io_thread>=0)sceKernelStartThread(io_thread,0,nullptr);
    else{io_run=false;enabled=false;disabled_reason="writer thread create failed";rlog("rec writer thread create failed 0x%08x\n",unsigned(io_thread));}
    log_write_out();
}

extern "C" void th10_rec_tick(unsigned tick,unsigned late,unsigned presents){
    if(!inited)return;
    if(!boot_ticks&&!boot_late){boot_ticks=tick;boot_late=late;}
    cur_tick=tick;cur_late=late;cur_presents=presents;
    // SELECT: toggle on the press edge (TH08 render_cadence.cpp), 0.3 s apart
    const bool down=select_down(),edge=down&&!select_was_down;select_was_down=down;
    bool toggle=edge&&now()-last_toggle_us>=300000;
    if(cfg.auto_start&&tick==cfg.auto_start&&!want)toggle=true;   // unattended runs (PPSSPP: no keyboard automation)
    if(cfg.auto_stop&&tick==cfg.auto_stop&&want)toggle=true;
    if(toggle){last_toggle_us=now();if(want||start_pending)request_stop();else request_start();}
    if(want&&session==done_session&&stop_session!=session){want=0;rlog("tick %u: session %u ended by the writer (%s)\n",tick,session,ws_why);}
    if(want&&drain_session==session){want=0;rlog("tick %u: session %u ended by the writer (%s)\n",tick,session,ws_why);}
    if(start_pending&&session==done_session){start_pending=false;request_start();}
    if(enabled)encoder_poll();
}

extern "C" void th10_rec_shutdown(void){
    static bool done=false;if(!inited||done)return;done=true;
    start_pending=false;
    if(want)request_stop();
    const u64 t0=now();
    while(session!=done_session&&now()-t0<15000000ull){   // the writer finishes the file (idx1 + header)
        if(pause_req&&now()-t0>2000000)th10_rec_writer_pause(0);   // exiting: a pause left on does not keep the file unfinished
        encoder_poll();sceKernelDelayThread(2000);}
    if(session!=done_session)rlog("shutdown: the file was not finished within 15 s (it is repaired on the next start)\n");
    rlog("rec shutdown at tick %u after %.1f s wait; log_lost=%u\n",cur_tick,(now()-t0)/1e6,log_lost);
    io_run=false;bool ended=true;
    if(io_thread>=0){SceUInt timeout=3000000;if(sceKernelWaitThreadEnd(io_thread,&timeout)<0)ended=false;else{sceKernelDeleteThread(io_thread);io_thread=-1;}}
    if(ended)log_write_out();
}
