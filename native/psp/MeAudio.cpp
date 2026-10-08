// TH10 audio output on the PSP: per-tick mix jobs (native/AudioMix.hpp) are
// mixed on the Media Engine and played by one sceAudio thread.
// The ME is taken over with m-c/d's me-custom-core (MECC, MIT, third_party/)
// as in the TH07/TH08 ports: Main RAM only (no ME eDRAM buffers), the job
// ring is written back by the SC before submission, the ME invalidates what
// it reads and writes the mixed block back. Without an ME (PPSSPP, takeover
// failure) the same kernel runs on the SC when the job is submitted.
#include "../AudioMix.hpp"
#include "../psp_cfw/cfw_imports.h"
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
extern "C" {
#include "me-core.h"
}
#if TH10_REC
extern "C" void th10_rec_audio_block(const int16_t* stereo);   // psp/Recorder.cpp: one played 1024-frame block
#endif
#if TH10_SE_EDRAM
// PSP-1000 lane: sound effects in the ME's local eDRAM (psp/SeEdram.hpp).
// Not with the recorder: the recorder's eDRAM plan (MeEdramMap.hpp) is the Go's.
#if TH10_REC
#error "TH10_SE_EDRAM and TH10_REC both use the ME eDRAM"
#endif
#include "SeEdram.hpp"
#endif

namespace {
using th10::mix::Job;using th10::mix::u32;
constexpr u32 slots=8,block_frames=1024;
alignas(64) Job jobs[slots];
// SC <-> ME mailbox, only ever accessed through the uncached alias.
struct alignas(64) Mailbox {
    volatile u32 submitted,completed,state,stop,heartbeat,jobs_base,job_bytes,last_cycles;
#if TH10_SE_EDRAM||TH10_ME_ALLOW_FAT
    // se_ctl: th10::se::Ctl address (0 = none); the ME reports its $sp and the
    // ME image witness word (0x88300018) once it runs: MECC put the stack top
    // at 0x80200000 (the 1000's image 0x279c1d44) or 0x80400000 (Slim+ 0x279c637c).
    volatile u32 se_ctl,me_sp,me_witness,pad[5];
#else
    volatile u32 pad[8];
#endif
};
static_assert(sizeof(Mailbox)==64,"mailbox is one cache line");
alignas(64) Mailbox mailbox;
enum : u32 {Booting=0,Ready=1,Stopped=2};
volatile Mailbox* box(){return reinterpret_cast<volatile Mailbox*>(0x40000000u|reinterpret_cast<u32>(&mailbox));}

// ---- SC state ----
bool started=false,me_ready=false,power_locked=false,running=false;
const char* me_state="off";
volatile u32 write_index=0;        // next job the game thread fills (game thread)
volatile u32 read_index=0;         // next job the audio thread plays (audio thread)
u32 serial=0,underruns=0,stretched=0,dropped_jobs=0,me_jobs=0,sc_jobs=0;
SceUID thread=-1;int channel=-1;
#if TH10_REC
volatile u32 rec_level_min=~0u;   // lowest queued frames since the recorder's last reset
#endif

bool under_ppsspp(){SceIoStat st;return sceIoGetstat("ms0:/PSP/SYSTEM/ppsspp.ini",&st)>=0;}
#if TH10_SE_EDRAM||TH10_ME_ALLOW_FAT
int me_model=-9,me_mapper=0;   // kuKernelGetModel() (0 on the 1000), meLibDefaultInit() (the ME core table: 3 on the 1000, 2 on Slim+)
#endif
}

// ---- ME side: runs on the Media Engine after meLibDefaultInit() ----
namespace {
void me_invalidate(u32 address,u32 bytes){meLibDcacheInvalidateRange(address,bytes);}
inline u32 me_count(){u32 v;asm volatile("mfc0 %0, $9":"=r"(v));return v;}
#if TH10_SE_EDRAM
// The ME's view for psp/SeEdram.hpp: eDRAM through the uncached kseg1 alias,
// Main RAM (user range and the volatile partition) through kseg0 after an
// invalidate or through the uncached alias, the block table uncached.
struct SeMe {
    static volatile u32* edram(u32 offset){return reinterpret_cast<volatile u32*>(0xA0000000u|offset);}
    static void edram_drop(u32 offset,u32 bytes){meLibDcacheInvalidateRange(0x80000000u|offset,bytes);}
    // Drops every line the copy reads, including a line that straddles either
    // end (start rounded down to 64; end + 3 for the tail word, rounded up),
    // so no clean line left over from an earlier source can be served.
    static const u32* ram_words(u32 address,u32 bytes){const u32 a=(0x80000000u|address)&~63u;meLibDcacheInvalidateRange(a,(((0x80000000u|address)+bytes+3u+63u)&~63u)-a);return reinterpret_cast<const u32*>(0x80000000u|address);}
    static volatile u32* ram_uc(u32 address){return reinterpret_cast<volatile u32*>(0x40000000u|address);}
    static bool ram_ok(u32 address,u32 bytes){return address>=0x08400000u&&address<0x0c000000u&&bytes<=0x0c000000u-address;}
    static volatile th10::se::Block* table(u32 base){return reinterpret_cast<volatile th10::se::Block*>(0x40000000u|base);}
    static u32 count(){return me_count();}
    static void sync(){asm volatile("sync");}
};
#endif
}
#if TH10_REC
// Recording (psp/RecMe.hpp): two typed job rings next to the audio FIFO.
// Audio stays first: one recording job (a writer copy before an encode job)
// runs only when no audio job is waiting, then audio is checked again (G0b's
// ME loop, plus the copy ring).
#include "RecMeKernel.inl"
#endif
extern "C" void meLibOnProcess(void){
    volatile Mailbox* b=box();
    asm volatile("mtc0 $0, $9\n\tsync\n\tnop\n\tnop":::"memory");   // start CP0 Count
    meLibDcacheWritebackInvalidateAll();meLibIcacheInvalidateAll();
#if TH10_SE_EDRAM||TH10_ME_ALLOW_FAT
    {u32 sp;asm volatile("move %0, $sp":"=r"(sp));b->me_sp=sp;b->me_witness=*reinterpret_cast<volatile u32*>(0x88300018u);}
#endif
    b->state=Ready;asm volatile("sync");
    for(;;){
#if TH10_REC
        while(b->completed==b->submitted&&!b->stop&&!th10::rec::me::pending()&&!th10::rec::me::wpending()){b->heartbeat=b->heartbeat+1;asm volatile("nop; nop; nop; nop; nop; nop; nop; nop;");}
#elif TH10_SE_EDRAM
        // Idle: an effect copy job runs only while no audio job waits; the
        // optional read-refresh takes a slice when now_ms has moved on.
        // Looked at every 64th spin, so the idle loop's uncached traffic stays
        // about what it was (an effect job waits some 10 us more).
        for(u32 spin=0;b->completed==b->submitted&&!b->stop;){
            b->heartbeat=b->heartbeat+1;
            if(!(++spin&63u))if(const u32 ctl=b->se_ctl){volatile th10::se::Ctl* c=reinterpret_cast<volatile th10::se::Ctl*>(0x40000000u|ctl);
                if(c->submitted!=c->completed){th10::se::run_job<SeMe>(c);continue;}
                th10::se::refresh_step<SeMe>(c);}
            asm volatile("nop; nop; nop; nop; nop; nop; nop; nop;");
        }
#else
        while(b->completed==b->submitted&&!b->stop){b->heartbeat=b->heartbeat+1;asm volatile("nop; nop; nop; nop; nop; nop; nop; nop;");}
#endif
        if(b->stop){b->state=Stopped;asm volatile("sync");while(b->stop)asm volatile("nop; nop; nop; nop;");b->state=Ready;asm volatile("sync");continue;}
#if TH10_REC
        if(b->completed==b->submitted){if(th10::rec::me::wpending())th10::rec::me::run_copy_one(&b->submitted);else th10::rec::me::run_one(&b->submitted);continue;}
#endif
        const u32 index=b->completed,address=b->jobs_base+(index%slots)*b->job_bytes;
        meLibDcacheInvalidateRange(0x80000000u|address,b->job_bytes);
        auto& job=*reinterpret_cast<Job*>(0x80000000u|address);
        const u32 start=me_count();
#if TH10_SE_EDRAM
        bool se_wrote=false;   // due block re-checks of eDRAM voices
        if(const u32 ctl=b->se_ctl)se_wrote=th10::se::prepass<SeMe>(job,reinterpret_cast<volatile th10::se::Ctl*>(0x40000000u|ctl));
#endif
        th10::mix::mix_job(job,0x80000000u,me_invalidate);
        meLibDcacheWritebackRange(0x80000000u|reinterpret_cast<u32>(job.out),sizeof(job.out));
#if TH10_SE_EDRAM
        if(se_wrote)meLibDcacheWritebackInvalidateRange(0x80000000u|reinterpret_cast<u32>(job.voice),sizeof(job.voice));   // a silenced voice: the record's lines go back, none stays dirty
#endif
        auto* done=reinterpret_cast<volatile Job*>(0x40000000u|address);
        const u32 cycles=me_count()-start;done->cycles=cycles;done->on_me=1;done->done=job.serial;b->last_cycles=cycles;
#if TH10_REC
        {volatile th10::rec::Box* r=th10::rec::me::ring();r->audio_busy=r->audio_busy+cycles;}   // audio's share of the ME
#endif
        asm volatile("sync");
        b->completed=index+1;asm volatile("sync");
    }
}
extern "C" void meLibOnSleep(void){box()->stop=1;asm volatile("sync");}
extern "C" __attribute__((noinline,aligned(4))) void meLibOnWake(void){}

// ---- SC side ----
namespace {
bool job_done(const Job& j){return reinterpret_cast<const volatile Job*>(0x40000000u|reinterpret_cast<u32>(&j))->done==j.serial;}
#if TH10_SE_EDRAM
// ---- sound effects in the ME eDRAM (psp/SeEdram.hpp) ----
alignas(64) th10::se::Ctl se_ctl;
alignas(64) th10::se::Block se_blocks[th10::se::max_blocks];   // 15.5 KiB: one checksum record per 2 KiB of eDRAM
volatile th10::se::Ctl* se_uc(){return reinterpret_cast<volatile th10::se::Ctl*>(0x40000000u|reinterpret_cast<u32>(&se_ctl));}
struct SePsp {
    // The ME runs the job when no audio job waits (a copy of the largest
    // effect is a few ms); 1 s means the ME stopped answering.
    static bool run(volatile th10::se::Ctl* c){
        asm volatile("sync":::"memory");const u32 index=c->submitted+1u;c->submitted=index;asm volatile("sync":::"memory");
        for(int i=0;i<4000&&c->completed!=index;i++)sceKernelDelayThread(250);
        return c->completed==index;
    }
    static void writeback(const void* p,u32 n){sceKernelDcacheWritebackRange(p,n);}
    static void wbinv(void* p,u32 n){sceKernelDcacheWritebackInvalidateRange(p,n);}
    static u32 now_ms(){return u32(sceKernelGetSystemTimeWide()/1000u);}
    static u32 now_us(){return sceKernelGetSystemTimeLow();}
};
th10::se::Host<SePsp> se_host;
u32 se_sp=0,se_witness=0;const char* se_state="off";
// After the ME is up: effects go to eDRAM only if MECC's stack sits in the
// 64 KiB guard at the top of a 2 MiB half (0x80200000 on the 1000,
// 0x80400000 on Slim+/Go); the area 0x000000..0x1EFFFF is below it either way.
void se_start(){
    if(!me_ready){se_state=under_ppsspp()?"off (no ME: PPSSPP)":"off (no ME)";return;}
    se_sp=box()->me_sp;se_witness=box()->me_witness;const u32 at=se_sp&0x003fffffu;
    if(!((at>=0x001f0000u&&at<0x00200000u)||(at>=0x003f0000u&&at<0x00400000u))){se_state="off (ME stack outside the guard windows)";return;}
    se_host.enable(reinterpret_cast<u32>(se_blocks),TH10_SE_EDRAM_CHECK_MS,TH10_SE_EDRAM_REFRESH_MS);
    se_uc()->now_ms=SePsp::now_ms();se_state="eDRAM";
}
#endif
// Frames of job `index` already handed to sceAudio.
u32 consumed=0;
// Copy up to `want` mixed frames, in job order, waiting a little for the ME
// or the game thread; returns the frames copied.
u32 take(int16_t* out,u32 want,u32 wait_us){
    u32 got=0,waited=0;
    while(got<want){
        if(read_index==write_index||!job_done(jobs[read_index%slots])){
            if(waited>=wait_us)break;sceKernelDelayThread(500);waited+=500;continue;}
        // The SC holds no line of j.out: submit wrote back and invalidated it.
        Job& j=jobs[read_index%slots];
        const u32 n=std::min(want-got,j.frames-consumed);
        std::memcpy(out+got*2,j.out+consumed*2,n*4);got+=n;consumed+=n;
        if(consumed>=j.frames){consumed=0;read_index=read_index+1;}
    }
    return got;
}
u32 queued_frames(){u32 n=0;for(u32 i=read_index;i!=write_index;++i){const Job& j=jobs[i%slots];if(!job_done(j))break;n+=j.frames;}return n>consumed?n-consumed:0;}
int audio_thread(SceSize,void*){
    alignas(64) static int16_t block[2][block_frames*2];alignas(64) static int16_t input[(block_frames+64)*2];
    u32 which=0;bool primed=false;int average=int(2*block_frames)*16;   // queue level x16
    while(running){
        int16_t* out=block[which];which^=1;
        const u32 level=queued_frames();
#if TH10_REC
        if(primed&&level<rec_level_min)rec_level_min=level;
#endif
        if(!primed&&level>=2*block_frames)primed=true;
        if(!primed)std::memset(out,0,sizeof(block[0]));   // (re)start with two blocks queued
        else{
            // Game ticks come at the display rate (59.94 Hz, 0.1% short of
            // 44.1 kHz): consume a few frames more or less per block to hold
            // the queue near two blocks (a 1-frame change is 0.1%, 2 cents).
            // The level jumps by a job (~735 frames) as ticks arrive; steer on
            // its running average so the ratio changes rarely and by 1 frame.
            average+=(int(level)*16-average)/16;
            const int error=(average/16-int(2*block_frames))/256;
            const u32 want=u32(int(block_frames)+(error<-4?-4:error>4?4:error));
            const u32 got=take(input,want,30000);
            if(!got){++underruns;primed=false;std::memset(out,0,sizeof(block[0]));}
            else if(got==block_frames)std::memcpy(out,input,sizeof(block[0]));
            else{if(got<want)++underruns;else ++stretched;
                // Linear resample of `got` frames onto the block.
                const u32 step=(got<<16)/block_frames;u32 pos=0;
                for(u32 i=0;i<block_frames;i++,pos+=step){const u32 k=pos>>16,f=pos&0xffffu,k1=k+1<got?k+1:k;
                    out[2*i]=int16_t(input[2*k]+((int(input[2*k1])-int(input[2*k]))*int(f)>>16));
                    out[2*i+1]=int16_t(input[2*k+1]+((int(input[2*k1+1])-int(input[2*k+1]))*int(f)>>16));}}
        }
#if TH10_REC
        th10_rec_audio_block(out);   // what is played (silence, stretches and all) is what is recorded
#endif
#if TH10_SE_EDRAM
        if(se_host.active)se_uc()->now_ms=SePsp::now_ms();   // the ME's clock for block re-checks and the refresh, also while the game is paused
#endif
        sceAudioOutputPannedBlocking(channel,PSP_AUDIO_VOLUME_MAX,PSP_AUDIO_VOLUME_MAX,out);
    }
    return 0;
}
// One job through the ME and the same job on the SC; the blocks must match
// bit for bit (in-place stereo 16-bit loop + staged mono 8-bit voice).
alignas(64) int16_t selftest_pcm[4096*2];
bool selftest(){
    for(u32 i=0;i<4096;i++){const int v=int((i*2654435761u)>>17)-16384;selftest_pcm[2*i]=int16_t(v);selftest_pcm[2*i+1]=int16_t(-v/2);}
    sceKernelDcacheWritebackRange(selftest_pcm,sizeof(selftest_pcm));
    Job& j=jobs[0];std::memset(&j,0,sizeof(Job));j.frames=block_frames;j.voices=2;
    auto& a=j.voice[0];a.source=reinterpret_cast<u32>(selftest_pcm);a.frames=4096;a.loop=1;a.channels=2;a.bits=16;a.index=4000;a.frac=0x1234;a.step=0x10000*22050/44100+77;a.gain_l=20000;a.gain_r=32768;a.invalidate=1;
    for(u32 i=0;i<2048;i++)j.staging[i]=u8(i*37);
    auto& b=j.voice[1];b.source=reinterpret_cast<u32>(j.staging);b.frames=2048;b.channels=1;b.bits=8;b.step=0x10000;b.gain_l=b.gain_r=30000;
    static Job reference;std::memcpy(&reference,&j,sizeof(Job));th10::mix::mix_job(reference,0,nullptr);
    th10_audio_submit(&j);
    for(int i=0;i<200&&!job_done(j);i++)sceKernelDelayThread(500);
    const bool ok=job_done(j)&&!std::memcmp(j.out,reference.out,sizeof(j.out));
    // Retire the test job from the ring before real jobs start.
    read_index=write_index;return ok;
}
void start(){
    if(started)return;started=true;
    std::memset(&mailbox,0,sizeof(mailbox));std::memset(jobs,0,sizeof(jobs));
#if TH10_SE_EDRAM
    std::memset(&se_ctl,0,sizeof(se_ctl));std::memset(se_blocks,0,sizeof(se_blocks));
#endif
    sceKernelDcacheWritebackInvalidateAll();
    box()->jobs_base=reinterpret_cast<u32>(jobs);box()->job_bytes=sizeof(Job);
#if TH10_SE_EDRAM
    box()->se_ctl=reinterpret_cast<u32>(&se_ctl);   // the ME reads effect jobs here; nblocks stays 0 (off) until se_start()
    se_host.attach(se_uc(),reinterpret_cast<volatile th10::se::Block*>(0x40000000u|reinterpret_cast<u32>(se_blocks)));
#endif
#if TH10_SE_EDRAM||TH10_ME_ALLOW_FAT
    me_model=kuKernelGetModel();
#endif
    if(under_ppsspp())me_state="skipped (PPSSPP)";
#if !TH10_ME_ALLOW_FAT
    else if(kuKernelGetModel()<1)me_state="skipped (model)";
#endif
    // TH10_ME_ALLOW_FAT (PSP-1000 lane): no model gate. Every PSP has the ME;
    // MECC picks its core table from the ME image (the 1000's witness
    // 0x279c1d44 -> table 3, stack top 0x80200000). TH08's 1000 lane took
    // the ME over this way on the device (r061ME 2026-09-19,
    // TH08_PSP_ME_ALLOW_FAT); any failure leaves the mixing on the SC.
    else{
        power_locked=scePowerLock(0)>=0;   // no suspend while the ME runs our code (GE4 may hold one too)
        const int mapper=meLibDefaultInit();
#if TH10_SE_EDRAM||TH10_ME_ALLOW_FAT
        me_mapper=mapper;
#endif
        if(mapper<0)me_state="takeover failed";
        else{for(int i=0;i<2000&&box()->state!=Ready;i++)sceKernelDelayThread(1000);
            me_ready=box()->state==Ready;me_state=me_ready?"ME":"no ready";
            if(me_ready&&!selftest()){me_ready=false;me_state="ME selftest NG";}}
    }
#if TH10_SE_EDRAM
    se_start();
#endif
    channel=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,block_frames,PSP_AUDIO_FORMAT_STEREO);
    if(channel<0)return;
    running=true;
    thread=sceKernelCreateThread("th10_audio",audio_thread,0x12,0x4000,PSP_THREAD_ATTR_USER,nullptr);
    if(thread>=0)sceKernelStartThread(thread,0,nullptr);else running=false;
}
}

extern "C" Job* th10_audio_acquire(void){
    start();
    if(!running||write_index-read_index>=slots){++dropped_jobs;return nullptr;}
    Job& j=jobs[write_index%slots];
    // The slot's previous output has been played; the ME may still hold
    // nothing of it (completed <= write_index).
    j.done=0;j.on_me=0;j.cycles=0;
    return &j;
}
extern "C" void th10_audio_submit(Job* job){
    job->serial=++serial;
    if(me_ready){
        // No SC cache line may cover the output while the ME owns it.
        sceKernelDcacheWritebackInvalidateRange(job->out,sizeof(job->out));
        sceKernelDcacheWritebackRange(job,u32(reinterpret_cast<char*>(job->out)-reinterpret_cast<char*>(job)));
        sceKernelDcacheWritebackRange(job->staging,sizeof(job->staging));
        write_index=write_index+1;
#if TH10_SE_EDRAM
        if(se_host.active)se_uc()->now_ms=SePsp::now_ms();
#endif
        asm volatile("sync");
        box()->submitted=write_index;asm volatile("sync");
        ++me_jobs;
    }else{
#if TH10_SE_EDRAM
        // The SC cannot read the ME eDRAM; nothing is stored there without an ME.
        for(u32 v=0;v<job->voices&&v<th10::mix::max_voices;v++)if(job->voice[v].reserved&th10::se::VoiceEdram)job->voice[v].frames=0;
#endif
        th10::mix::mix_job(*job,0,nullptr);job->done=job->serial;job->on_me=0;
        write_index=write_index+1;++sc_jobs;
    }
}
extern "C" void th10_audio_written(const void* bytes,std::uint32_t size){if(bytes&&size)sceKernelDcacheWritebackRange(bytes,size);}
extern "C" void th10_audio_shutdown(void){
    if(!started)return;
    running=false;if(thread>=0){sceKernelWaitThreadEnd(thread,nullptr);sceKernelDeleteThread(thread);thread=-1;}
    if(channel>=0){sceAudioChRelease(channel);channel=-1;}
    if(me_ready){box()->stop=1;asm volatile("sync");for(int i=0;i<200&&box()->state!=Stopped;i++)sceKernelDelayThread(1000);}
    if(power_locked){scePowerUnlock(0);power_locked=false;}
}
extern "C" void th10_audio_stats(char* out,unsigned size){
#if TH10_SE_EDRAM||TH10_ME_ALLOW_FAT
    // + model (0 = PSP-1000), mapper (MECC core table: 3 = the 1000's image,
    // 2 = Slim+), the ME $sp and witness word, and the eDRAM that implies.
    const u32 sp=box()->me_sp,at=sp&0x003fffffu;
    std::snprintf(out,size,"audio %s me_jobs=%u sc_jobs=%u dropped=%u underruns=%u stretched=%u last_me_cycles=%u heartbeat=%u model=%d mapper=%d sp=0x%08x witness=0x%08x edram=%s",
        me_state,me_jobs,sc_jobs,dropped_jobs,underruns,stretched,box()->last_cycles,box()->heartbeat,me_model,me_mapper,sp,box()->me_witness,
        !me_ready?"-":at>=0x003f0000u?"4MiB":at>=0x001f0000u&&at<0x00200000u?"2MiB(1984KiB below the stack guard)":"?");
#else
    std::snprintf(out,size,"audio %s me_jobs=%u sc_jobs=%u dropped=%u underruns=%u stretched=%u last_me_cycles=%u heartbeat=%u",
        me_state,me_jobs,sc_jobs,dropped_jobs,underruns,stretched,box()->last_cycles,box()->heartbeat);
#endif
}
#if TH10_SE_EDRAM
extern "C" unsigned th10_se_store(const void* pcm,unsigned bytes,unsigned fill){start();return se_host.store(pcm,bytes,fill);}   // effects load before the first mix job: start the ME here
extern "C" int th10_se_voice(unsigned handle,th10::mix::Voice* voice){return se_host.voice(handle,*voice)?1:0;}
extern "C" int th10_se_restore(unsigned handle,void* dst,unsigned bytes){return se_host.restore(handle,dst,bytes)?1:0;}
extern "C" void th10_se_release(unsigned handle){se_host.release(handle);}
extern "C" void th10_se_stats(char* out,unsigned size){
    volatile th10::se::Ctl* c=se_uc();const auto& h=se_host;
    std::snprintf(out,size,"se_edram %s area=%uKiB stored=%u live=%u bytes=%u peak=%u blocks=%u peak=%u/%u load_ms=%u store_us_max=%u fail space=%u copy=%u args=%u timeout=%u would_store=%u (%u bytes) | ME checks=%u mismatches=%u muted=%u verify_max=%u counts refresh=%ums slices=%u restores=%u releases=%u",
        se_state,th10::se::area_bytes/1024u,h.stored,h.live,h.live_bytes,h.peak_bytes,h.live_blocks,h.peak_blocks,th10::se::max_blocks,h.store_us/1000u,h.store_us_max,h.fail_space,h.fail_copy,h.fail_args,h.fail_timeout,h.dry,h.dry_bytes,
        u32(c->checks),u32(c->mismatches),u32(c->muted),u32(c->verify_max),u32(c->refresh_period_ms),u32(c->refreshes),h.restores,h.releases);
}
#endif
#if TH10_REC
extern "C" int th10_rec_me_ready(void){return me_ready?1:0;}
extern "C" int th10_rec_audio_running(void){return running?1:0;}
extern "C" const char* th10_rec_me_state(void){return me_state;}
// underruns, stretched, dropped jobs, me_jobs, sc_jobs, lowest stock (frames) since the last reset, audio thread started
extern "C" void th10_rec_audio_counters(unsigned* out){
    out[0]=underruns;out[1]=stretched;out[2]=dropped_jobs;out[3]=me_jobs;out[4]=sc_jobs;out[5]=rec_level_min;out[6]=started?1u:0u;out[7]=0;}
extern "C" void th10_rec_audio_level_reset(void){rec_level_min=~0u;}
#endif
