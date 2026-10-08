#pragma once
// TH10_SE_EDRAM (the PSP-1000 lane): sound-effect PCM in the Media Engine's
// local eDRAM instead of Main RAM (37 effect sources, about 1.82 MiB on the
// PC count). Only the ME mixer reads effects and the SC cannot address the ME
// eDRAM, so:
//  - load (HeadlessAudio O::Unlock of a finished effect source): the SC puts a
//    checksum per 2 KiB block in the table and hands the PCM, still in Main
//    RAM, to an ME copy job. The ME copies it into eDRAM, reads every block
//    back against the SC's checksums, and only then does the SC free the
//    Main-RAM copy (heap or volatile arena). On a mismatch, a full eDRAM or
//    no ME, the buffer stays where it is.
//  - mixing: an eDRAM voice carries VoiceEdram in Voice::reserved and the
//    eDRAM offset as `source`; mix_job (AudioMix.hpp, unchanged) maps it with
//    its kseg0 alias (0x80000000|offset) like any Main-RAM source. eDRAM only
//    changes through this file's jobs, which drop the cached lines they touch,
//    so eDRAM voices need no per-job invalidate.
//  - safety: before mixing a job the ME re-reads (uncached) each block a voice
//    is about to read whose last check is older than check_ms (10 s). A
//    mismatch rewrites that block with silence (no Main-RAM copy is left) and
//    is counted.
//  - read-refresh (refresh_period_ms, 0 = off): only if the PSP-1000
//    retention test shows rot in VME mode 2.
//    The ME reads one uncached word per 64 bytes of the stored area, a slice
//    per audio-thread block (now_ms advances ~23 ms), so it keeps running
//    while the game is paused.
// One header for the ME loop and the SC host (psp/MeAudio.cpp) and the PC
// simulation (HeadlessAudio.cpp, TH10_SE_EDRAM_SIM: a host array stands in
// for the eDRAM). ME code follows native/tools/rec_me_check.py: no FPU, no
// indirect jumps, no library calls, no static data (the control block comes
// through the audio mailbox), no variable divisors, 32-bit arithmetic only.
// TH07 lesson (memory psp-edram-upper-retention): its PSP-3000 eDRAM rings
// rotted after ~2.2 s of dwell with 0xBCC00040=1 (TH07_PSP_MECC_AUDIO_4M);
// TH10's MECC writes 2 and this code never writes that register.
#include "../AudioMix.hpp"
#include <cstdint>
#include <cstring>
#ifndef TH10_SE_EDRAM_CHECK_MS
#define TH10_SE_EDRAM_CHECK_MS 10000   // a block is re-read against its checksum at its first use after this long
#endif
#ifndef TH10_SE_EDRAM_REFRESH_MS
#define TH10_SE_EDRAM_REFRESH_MS 0     // ME read-refresh period; set only if the 1000 retention test shows mode-2 rot
#endif

namespace th10::se {
using u32=std::uint32_t;using u8=std::uint8_t;
constexpr u32 block_bytes=2048u,block_shift=11u,block_words=block_bytes/4u;
// eDRAM 0x000000..0x1EFFFF: the PSP-1000's 2 MiB minus the top 64 KiB, below
// MECC's ME stack (top 0x80200000 for the 1000's image, witness 0x279c1d44;
// the G0/RecG0 guard). On Slim+/Go the stack tops 4 MiB and this is the
// lower half.
constexpr u32 area_bytes=0x001F0000u,max_blocks=area_bytes/block_bytes;   // 992
constexpr u32 VoiceEdram=1u;          // mix::Voice::reserved bit: `source` is an eDRAM offset
constexpr u32 max_stores=64;          // TH10 loads 37 effect sources
constexpr u32 max_step=0x00200000u;   // 32x the output rate; keeps the span arithmetic in 32 bits
constexpr u32 refresh_tick_ms=20u;    // at most one refresh slice per 20 ms of now_ms
enum : u32 {BlockFree=0,BlockOk=1,BlockMuted=2};
enum : u32 {JobCopyIn=1,JobCopyOut=2};
enum : u32 {StatusPending=0,StatusOk=1,StatusBadArgs=2,StatusMismatch=3,StatusBadKind=4};
// One per 2 KiB block, in Main RAM; both sides use the uncached alias.
struct Block {u32 sum,checked_ms,state,fill;};
static_assert(sizeof(Block)==16,"block record");
// The control block (Main RAM, uncached alias on both sides). Its address
// reaches the ME through the audio mailbox. One job at a time: the SC fills
// the job words and bumps `submitted`; the ME runs it when no audio job is
// waiting and sets `completed` to the value it ran.
struct alignas(64) Ctl {
    u32 submitted,completed,kind,status;
    u32 src,dst,bytes,fill;              // CopyIn: src Main RAM, dst eDRAM offset; CopyOut: the reverse
    u32 result,blocks_base,nblocks,now_ms;   // result: blocks failing the read-back; nblocks 0 = off
    u32 check_ms,checks,mismatches,muted;    // re-check age and counters of the mixing prepass
    u32 verify_counts,verify_max,refresh_period_ms,refresh_slice;   // ME count units spent re-checking; refresh setup
    u32 refresh_used,refresh_cursor,refresh_last_ms,refreshes;
    u32 refresh_xor,pad[7];
};
static_assert(sizeof(Ctl)==128,"ctl is two cache lines");
constexpr u32 fnv_init=2166136261u;
inline u32 fnv(u32 h,u32 w){return (h^w)*16777619u;}   // FNV-1a over words (TH07 HashBgmBlock)

// ================================================================ ME side ==
// E is the ME's view (psp/MeAudio.cpp SeMe) or the PC simulation's:
//   edram(offset)        volatile u32*: uncached eDRAM (ME 0xA0000000|offset)
//   edram_drop(off,n)    drop cached eDRAM lines (ME kseg0 invalidate)
//   ram_words(addr,n)    const u32*: cached Main RAM after dropping its lines
//   ram_uc(addr)         volatile u32*: uncached Main RAM (0x40000000|addr)
//   ram_ok(addr,n)       Main RAM the SC may hand over (heap or volatile arena)
//   table(base)          volatile Block*: the block table, uncached
//   count()              CP0 count (PC: 0); sync()
template<class E> inline u32 block_sum(u32 offset){
    const volatile u32* p=E::edram(offset);u32 h=fnv_init;
    for(u32 w=0;w<block_words;w++)h=fnv(h,p[w]);
    return h;
}
// src: Main RAM, word aligned, `bytes` long; dst: a whole number of free
// blocks. The last block is padded with the silence byte. Every block is read
// back against the SC's checksum before it counts as stored.
template<class E> inline void copy_in(volatile Ctl* c){
    const u32 src=c->src,dst=c->dst,n=c->bytes,fill=c->fill&0xffu,nb=(n+block_bytes-1u)>>block_shift,first=dst>>block_shift;
    if(!n||(src&3u)||(dst&(block_bytes-1u))||dst>=area_bytes||nb>((area_bytes-dst)>>block_shift)||first+nb>c->nblocks||!E::ram_ok(src,n)){c->status=StatusBadArgs;return;}
    const u32* s=E::ram_words(src,n);volatile u32* d=E::edram(dst);
    const u32 words=n>>2,tail=n&3u,fw=fill*0x01010101u;
    for(u32 i=0;i<words;i++)d[i]=s[i];
    u32 i=words;
    if(tail){const u32 keep=(1u<<(tail*8u))-1u;d[i]=(s[i]&keep)|(fw&~keep);++i;}   // little endian: the low `tail` bytes are PCM
    for(const u32 end=nb*block_words;i<end;i++)d[i]=fw;
    E::edram_drop(dst,nb*block_bytes);
    volatile Block* t=E::table(c->blocks_base)+first;u32 bad=0;
    for(u32 k=0;k<nb;k++){
        if(block_sum<E>(dst+k*block_bytes)==t[k].sum){t[k].state=BlockOk;t[k].checked_ms=c->now_ms;}
        else{t[k].state=BlockFree;++bad;}
    }
    c->result=bad;c->status=bad?StatusMismatch:StatusOk;
}
// src: eDRAM offset of a stored buffer, dst: Main RAM (word aligned; the SC
// wrote back and invalidated it), uncached stores.
template<class E> inline void copy_out(volatile Ctl* c){
    const u32 src=c->src,dst=c->dst,n=c->bytes;
    if(!n||(dst&3u)||(src&(block_bytes-1u))||src>=area_bytes||n>area_bytes-src||!E::ram_ok(dst,n)){c->status=StatusBadArgs;return;}
    const volatile u32* s=E::edram(src);volatile u32* d=E::ram_uc(dst);
    const u32 words=n>>2;
    for(u32 i=0;i<words;i++)d[i]=s[i];
    if(n&3u){volatile u8* tail=reinterpret_cast<volatile u8*>(d+words);const u32 w=s[words];for(u32 k=0;k<(n&3u);k++)tail[k]=u8(w>>(k*8u));}
    c->status=StatusOk;
}
// The pending job (called only when no audio job is waiting).
template<class E> inline void run_job(volatile Ctl* c){
    const u32 index=c->submitted;E::sync();
    const u32 kind=c->kind;
    if(kind==JobCopyIn)copy_in<E>(c);
    else if(kind==JobCopyOut)copy_out<E>(c);
    else c->status=StatusBadKind;
    E::sync();c->completed=index;E::sync();
}
// Re-check the stored blocks covering [off, off+bytes) that are due.
template<class E> inline void check_range(volatile Ctl* c,volatile Block* t,u32 nblocks,u32 off,u32 bytes,u32 now,u32 age){
    if(!bytes)return;
    const u32 last=(off+bytes-1u)>>block_shift;
    for(u32 k=off>>block_shift;k<=last&&k<nblocks;k++){
        volatile Block& b=t[k];
        if(b.state!=BlockOk||now-b.checked_ms<age)continue;
        const u32 t0=E::count(),at=k<<block_shift;
        if(block_sum<E>(at)==b.sum){b.checked_ms=now;c->checks=c->checks+1;}
        else{   // rotted: silence this block for good (nothing to restore from)
            volatile u32* p=E::edram(at);const u32 f=(b.fill&0xffu)*0x01010101u;
            for(u32 w=0;w<block_words;w++)p[w]=f;
            E::edram_drop(at,block_bytes);
            b.state=BlockMuted;c->mismatches=c->mismatches+1;c->muted=c->muted+1;
        }
        const u32 dt=E::count()-t0;c->verify_counts=c->verify_counts+dt;if(dt>c->verify_max)c->verify_max=dt;
    }
}
// Before mix_job: the blocks each eDRAM voice reads in this job (the frames
// mix_job reads: [index, index+span), wrapping for loops). A malformed eDRAM
// voice is silenced (frames=0), never mixed outside the stored area; then the
// job record was written and true is returned (the ME writes those lines
// back and drops them after mixing, so no dirty line outlives the job).
template<class E> inline bool prepass(mix::Job& job,volatile Ctl* c){
    const u32 nblocks=c->nblocks;if(!nblocks)return false;
    bool wrote=false;
    volatile Block* t=E::table(c->blocks_base);
    const u32 now=c->now_ms,age=c->check_ms;
    const u32 frames=job.frames<mix::max_frames?job.frames:mix::max_frames,voices=job.voices<mix::max_voices?job.voices:mix::max_voices;
    for(u32 v=0;v<voices;v++){
        mix::Voice& s=job.voice[v];
        if(!(s.reserved&VoiceEdram))continue;
        const u32 fb=s.channels*(s.bits>>3),sh=fb==4u?2u:fb==2u?1u:fb==1u?0u:9u;
        if(sh>2u||s.source>=area_bytes||(s.source&(block_bytes-1u))||s.frames>((area_bytes-s.source)>>sh)||s.step>max_step){s.frames=0;wrote=true;continue;}
        if(!s.frames||!s.step||s.index>=s.frames)continue;
        const u32 span=((s.frac+frames*s.step)>>16)+2u,first=s.frames-s.index<span?s.frames-s.index:span;
        check_range<E>(c,t,nblocks,s.source+(s.index<<sh),first<<sh,now,age);
        if(s.loop&&span>first){const u32 rest=span-first<s.frames?span-first:s.frames;check_range<E>(c,t,nblocks,s.source,rest<<sh,now,age);}
    }
    return wrote;
}
// One read-refresh slice when now_ms has moved on (refresh off: period 0).
template<class E> inline void refresh_step(volatile Ctl* c){
    if(!c->refresh_period_ms)return;
    const u32 now=c->now_ms;if(now-c->refresh_last_ms<refresh_tick_ms)return;
    c->refresh_last_ms=now;
    const u32 used=c->refresh_used,slice=c->refresh_slice;if(!used||!slice)return;
    u32 at=c->refresh_cursor,x=0;const volatile u32* p=E::edram(0);
    for(u32 n=0;n<slice;n+=64u){x^=p[at>>2];at+=64u;if(at>=used)at=0;}
    c->refresh_cursor=at;c->refreshes=c->refreshes+1;c->refresh_xor=x;
}

// ================================================================ SC side ==
// P is the SC platform (psp/MeAudio.cpp SePsp or the PC simulation):
//   run(ctl)      submit the filled job and wait for it; false on timeout
//   writeback(p,n) / wbinv(p,n)   SC dcache maintenance
//   now_ms()      the ME's clock source for check ages; now_us() times the stores
template<class P> struct Host {
    volatile Ctl* c=nullptr;volatile Block* t=nullptr;
    bool active=false,dead=false;
    u32 used[(max_blocks+31u)/32u]{};
    struct Store {u32 first,nblocks,bytes,fill;};
    Store stores[max_stores]{};
    // stats: stored now / ever, bytes now / peak, blocks now / peak, fallbacks,
    // the dry run (no ME: what would have been stored), restores and releases
    u32 live=0,stored=0,live_bytes=0,peak_bytes=0,live_blocks=0,peak_blocks=0,fail_space=0,fail_copy=0,fail_args=0,fail_timeout=0,dry=0,dry_bytes=0,restores=0,releases=0;
    u32 store_us=0,store_us_max=0;   // SC wall time of the stores (checksums + ME copy and read-back): what the title load gains
    // ctl/table: the SC's uncached views; table_base: the table address the
    // ME is given (E::table maps it). The eDRAM path stays off until enable().
    void attach(volatile Ctl* ctl,volatile Block* table){c=ctl;t=table;}
    void enable(u32 table_base,u32 check_ms,u32 refresh_period_ms){
        c->check_ms=check_ms;c->blocks_base=table_base;c->refresh_period_ms=refresh_period_ms;c->refresh_slice=0;c->refresh_used=0;
        c->nblocks=max_blocks;active=true;
    }
    bool block_used(u32 k)const{return (used[k>>5]>>(k&31u))&1u;}
    void mark(u32 first,u32 nb,bool on){for(u32 k=first;k<first+nb;k++){if(on)used[k>>5]|=1u<<(k&31u);else used[k>>5]&=~(1u<<(k&31u));}}
    int alloc(u32 nb){   // first fit
        for(u32 k=0;k+nb<=max_blocks;){u32 run=0;while(run<nb&&!block_used(k+run))++run;if(run==nb)return int(k);k+=run+1u;}
        return -1;
    }
    void refresh_extent(){   // the read-refresh covers [0, end of the highest stored block)
        u32 end=0;for(const auto& s:stores)if(s.nblocks&&(s.first+s.nblocks)*block_bytes>end)end=(s.first+s.nblocks)*block_bytes;
        if(!c->refresh_period_ms){c->refresh_used=end;return;}
        // a slice per ~23 ms audio-thread block, sized for 25 ms so a pass takes at most one period
        u32 slice=u32((uint64_t(end)*25u+c->refresh_period_ms-1u)/c->refresh_period_ms);slice=(slice+63u)&~63u;
        c->refresh_slice=0;c->refresh_used=end;if(c->refresh_cursor>=end)c->refresh_cursor=0;c->refresh_slice=slice;
    }
    // The block checksum as the ME will read it back: little-endian words of
    // the n PCM bytes (word aligned), then the silence byte up to 2 KiB.
    static u32 sum_of(const u8* p,u32 n,u32 fill){
        u32 h=fnv_init,w=0;
        for(;w<n/4u;w++){u32 v;std::memcpy(&v,p+w*4u,4);h=fnv(h,v);}
        if(n&3u){u32 v=0;for(u32 b=0;b<4u;b++)v|=u32(b<(n&3u)?p[w*4u+b]:fill)<<(b*8u);h=fnv(h,v);++w;}
        for(const u32 fw=fill*0x01010101u;w<block_words;w++)h=fnv(h,fw);
        return h;
    }
    // Returns a handle (1..max_stores) when the PCM now lives in eDRAM and
    // the caller may free its copy; 0 when it must stay where it is.
    u32 store(const void* pcm,u32 n,u32 fill){
        const u32 nb=(n+block_bytes-1u)>>block_shift;
        if(!pcm||!n||(uintptr_t(pcm)&3u)||nb>max_blocks){++fail_args;return 0;}
        u32 slot=0;while(slot<max_stores&&stores[slot].nblocks)++slot;
        const int first=slot<max_stores?alloc(nb):-1;
        if(first<0){++fail_space;return 0;}
        if(!active){   // no ME (PPSSPP, takeover failure): the accounting of what would be stored
            mark(u32(first),nb,true);stores[slot]=Store{u32(first),nb,n,fill};++dry;dry_bytes+=n;
            live_blocks+=nb;if(live_blocks>peak_blocks)peak_blocks=live_blocks;return 0;}
        const u32 t0=P::now_us();
        const u8* bytes=static_cast<const u8*>(pcm);
        for(u32 k=0;k<nb;k++){volatile Block& b=t[u32(first)+k];const u32 off=k*block_bytes;
            b.sum=sum_of(bytes+off,n-off<block_bytes?n-off:block_bytes,fill&0xffu);b.state=BlockFree;b.checked_ms=0;b.fill=fill&0xffu;}
        P::writeback(pcm,(n+3u)&~3u);
        c->kind=JobCopyIn;c->src=u32(uintptr_t(pcm));c->dst=u32(first)*block_bytes;c->bytes=n;c->fill=fill&0xffu;c->status=StatusPending;c->result=0;c->now_ms=P::now_ms();
        const bool done=P::run(c);
        {const u32 dt=P::now_us()-t0;store_us+=dt;if(dt>store_us_max)store_us_max=dt;}
        if(!done){dead=true;active=false;++fail_timeout;mark(u32(first),nb,true);return 0;}   // the blocks stay taken: the ME may still write them
        if(c->status!=StatusOk){if(c->status==StatusMismatch)++fail_copy;else ++fail_args;for(u32 k=0;k<nb;k++)t[u32(first)+k].state=BlockFree;return 0;}
        mark(u32(first),nb,true);stores[slot]=Store{u32(first),nb,n,fill};
        ++live;++stored;live_bytes+=n;if(live_bytes>peak_bytes)peak_bytes=live_bytes;live_blocks+=nb;if(live_blocks>peak_blocks)peak_blocks=live_blocks;
        refresh_extent();
        return slot+1u;
    }
    const Store* get(u32 h)const{return h&&h<=max_stores&&stores[h-1u].nblocks?&stores[h-1u]:nullptr;}
    bool voice(u32 h,mix::Voice& v)const{
        const Store* s=get(h);if(!s)return false;
        v.source=s->first*block_bytes;v.invalidate=0;v.reserved|=VoiceEdram;return true;
    }
    void release(u32 h){
        const Store* s=get(h);if(!s)return;
        for(u32 k=0;k<s->nblocks;k++)t[s->first+k].state=BlockFree;
        mark(s->first,s->nblocks,false);live_bytes-=s->bytes;live_blocks-=s->nblocks;--live;++releases;
        stores[h-1u]=Store{};refresh_extent();
    }
    // Back to Main RAM (dst: bytes long, word aligned); then released.
    bool restore(u32 h,void* dst,u32 n){
        const Store* s=get(h);if(!s)return false;
        bool ok=false;
        if(active&&!dead&&n<=s->bytes&&!(uintptr_t(dst)&3u)){
            P::wbinv(dst,(n+3u)&~3u);
            c->kind=JobCopyOut;c->src=s->first*block_bytes;c->dst=u32(uintptr_t(dst));c->bytes=n;c->status=StatusPending;c->now_ms=P::now_ms();
            if(!P::run(c)){dead=true;active=false;++fail_timeout;return false;}
            ok=c->status==StatusOk;
        }
        ++restores;release(h);return ok;
    }
};
}

// SC entry points (psp/MeAudio.cpp on the PSP, HeadlessAudio.cpp in the PC
// simulation), used by HeadlessAudio.cpp.
extern "C" {
unsigned th10_se_store(const void* pcm,unsigned bytes,unsigned fill);   // handle, 0 = keep the Main-RAM copy
int th10_se_voice(unsigned handle,th10::mix::Voice* voice);            // 1: the voice now reads eDRAM
int th10_se_restore(unsigned handle,void* dst,unsigned bytes);          // back to Main RAM, then released
void th10_se_release(unsigned handle);
void th10_se_stats(char* out,unsigned size);
}
