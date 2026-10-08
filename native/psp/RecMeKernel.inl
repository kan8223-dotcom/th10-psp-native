// TH10_REC only: the Media Engine side of the recording jobs. Included by
// psp/MeAudio.cpp after me-core.h (meLibDcache* are the ME's own cache
// routines from me-lib.c). Ported from the G0b measurement build
// (RecG0MeKernel.inl, not published: run_jpeg and
// run_one unchanged, the eDRAM probe and band kinds removed).
// Everything here runs on the ME: no syscalls, no SC cache calls, no floating
// point, no jump tables, no static data (tools: native/tools/rec_me_check.py).
// Job records and the ring counters are touched only through the uncached
// alias; frame data through the cached kseg0 alias with explicit
// invalidate/writeback (the MeAudio.cpp rule).
#include "RecMe.hpp"

namespace th10::rec::me {
inline volatile Box* ring(){return reinterpret_cast<volatile Box*>(TH10_REC_UNCACHED|reinterpret_cast<u32>(&th10_rec_box));}
inline volatile Job* slot(u32 index){return reinterpret_cast<volatile Job*>(TH10_REC_UNCACHED|(reinterpret_cast<u32>(th10_rec_jobs)+(index%job_slots)*u32(sizeof(Job))));}
inline bool pending(){volatile Box* r=ring();return r->completed!=r->submitted;}
inline volatile Box* wring(){return reinterpret_cast<volatile Box*>(TH10_REC_UNCACHED|reinterpret_cast<u32>(&th10_rec_wbox));}
inline volatile Job* wslot(u32 index){return reinterpret_cast<volatile Job*>(TH10_REC_UNCACHED|(reinterpret_cast<u32>(th10_rec_wjobs)+(index%wjob_slots)*u32(sizeof(Job))));}
inline bool wpending(){volatile Box* r=wring();return r->completed!=r->submitted;}
inline u32 count(){u32 v;TH10_REC_ME_COUNT(v);return v;}
// Main RAM that the SC hands over (64 MiB user range), 64-byte aligned.
inline bool main_ram_ok(u32 address,u32 bytes){return !(address&63u)&&address>=TH10_REC_RAM_LO&&address<TH10_REC_RAM_HI&&bytes<=TH10_REC_RAM_HI-address;}
inline bool main_range_ok(u32 address,u32 bytes){return address>=TH10_REC_RAM_LO&&address<TH10_REC_RAM_HI&&bytes<=TH10_REC_RAM_HI-address;}
// The JPEG ring: the recorder's eDRAM region (psp/MeEdramMap.hpp; never the
// BGM cache or the stack guards), or Main RAM (th10rec.txt ring_edram=0).
inline bool ring_ok(u32 address,u32 bytes){
    const u32 lo=TH10_REC_EDRAM_BASE+edram::rec_ring.offset,hi=lo+edram::rec_ring.bytes;
    return (address>=lo&&address<hi&&bytes<=hi-address)||main_range_ok(address,bytes);
}
// meLib's range loops step from `addr` by 64 and so miss the last line when
// `addr` is not line-aligned: align both ends here.
inline void inval_lines(u32 address,u32 bytes){const u32 a=address&~63u;meLibDcacheInvalidateRange(a,((address+bytes+63u)&~63u)-a);}
inline void writeback_lines(u32 address,u32 bytes){const u32 a=address&~63u;meLibDcacheWritebackRange(a,((address+bytes+63u)&~63u)-a);}
// r0 = CP0 count (the SC calibrates the count rate from two of these).
inline void run_clock(volatile Job* j){j->r[0]=count();j->status=StatusOk;}
// JPEG (RecJpeg.hpp): MCUs [col0, col0+n) of MCU row `row`.
// a0 source frame (480x272 RGB565, stride 480: the capture buffer), a1 tables
// (jpeg::Tables, written by the SC, invalidated here on the frame's first job),
// a2 JpegCtx (uncached only), a3 out, a4 out capacity, a5 (row << 16) | col0,
// a6 (flags << 16) | n, a7 scratch (16 x 16n RGB565, JpegScale: the 3:2 strip
// is made here, then encoded).
// r0 bytes after this job, r1 count units spent scaling, r2 overflow, r3 blocks, r4 flat blocks.
TH10_JPEG_CORE inline void run_jpeg(volatile Job* j){
    const u32 src=j->a[0],tables=j->a[1],ctx=j->a[2],out=j->a[3],cap=j->a[4],row=j->a[5]>>16,col0=j->a[5]&0xffffu,flags=j->a[6]>>16,n=j->a[6]&0xffffu,scratch=j->a[7];
    if(!main_ram_ok(tables,u32(sizeof(jpeg::Tables)))||!main_ram_ok(ctx,u32(sizeof(JpegCtx)))||!cap||(out&63u)||!ring_ok(out,cap)||!main_ram_ok(src,frame_words*2u)){j->status=StatusBadArgs;return;}
    if(flags&JpegFirst)inval_lines(TH10_REC_KSEG0|tables,u32(sizeof(jpeg::Tables)));
    const jpeg::Tables& t=*reinterpret_cast<const jpeg::Tables*>(TH10_REC_KSEG0|tables);
    const bool scale=flags&JpegScale;
    if(!n||n>20u||col0+n>t.mcus_x||row>=t.mcus_y||t.vsamp!=2u||t.mcu_h!=16u||(scale&&(!t.scale_active||!main_ram_ok(scratch,n*16u*16u*2u)))||(!scale&&(t.width!=width||t.height!=height))){j->status=StatusBadArgs;return;}
    volatile JpegCtx* c=reinterpret_cast<volatile JpegCtx*>(TH10_REC_UNCACHED|ctx);
    jpeg::State s;
    s.pos=c->pos;s.acc=c->acc;s.nbits=c->nbits;s.overflow=c->overflow;s.dc0=c->dc0;s.dc1=c->dc1;s.dc2=c->dc2;s.blocks=c->blocks;s.flat=c->flat;
    jpeg::u8* o=reinterpret_cast<jpeg::u8*>(TH10_REC_KSEG0|out);
    const u32 from=(flags&JpegFirst)?0u:s.pos;
    if(flags&JpegFirst)jpeg::frame_begin(s,t,o,cap);
    u32 scale_counts=0;
    if(scale){
        const u32 t0=count();u32 first=0,lines_n=0;jpeg::scale32_source_lines(t,row*16u,first,lines_n);
        if(lines_n)inval_lines(TH10_REC_KSEG0|(src+first*width*2u),lines_n*width*2u);
        jpeg::u16* strip=reinterpret_cast<jpeg::u16*>(TH10_REC_KSEG0|scratch);
        jpeg::scale32_strip(t,reinterpret_cast<const jpeg::u16*>(TH10_REC_KSEG0|src),row*16u,col0*16u,n*16u,strip,n*16u);
        scale_counts=count()-t0;
        jpeg::encode_range(s,t,o,cap,strip,n*16u,n,16u);
    }else{
        inval_lines(TH10_REC_KSEG0|(src+row*16u*width*2u),16u*width*2u);
        jpeg::encode_range(s,t,o,cap,reinterpret_cast<const jpeg::u16*>(TH10_REC_KSEG0|src)+row*16u*width+col0*16u,width,n,16u);
    }
    if(flags&JpegLast)jpeg::frame_end(s,o,cap);
    if(s.pos>from)writeback_lines(TH10_REC_KSEG0|(out+from),s.pos-from);
    c->pos=s.pos;c->acc=s.acc;c->nbits=s.nbits;c->overflow=s.overflow;c->dc0=s.dc0;c->dc1=s.dc1;c->dc2=s.dc2;c->blocks=s.blocks;c->flat=s.flat;
    j->r[0]=s.pos;j->r[1]=scale_counts;j->r[2]=s.overflow;j->r[3]=s.blocks;j->r[4]=s.flat;j->status=StatusOk;
}
// a0 source (JPEG ring), a1 destination (a Main RAM write buffer), a2 bytes
// (<= 64 KiB, any alignment). The SC wrote back and invalidated the
// destination's lines before submitting, so the partial lines at both ends
// load its bytes (the chunk header before, nothing after) and go back whole.
// r0 bytes copied.
TH10_JPEG_CORE inline void run_copy(volatile Job* j){
    const u32 src=j->a[0],dst=j->a[1],n=j->a[2];
    if(!n||n>copy_max||!ring_ok(src,n)||!main_range_ok(dst,n)){j->status=StatusBadArgs;return;}
    inval_lines(TH10_REC_KSEG0|dst,n);
    const jpeg::u8* s=reinterpret_cast<const jpeg::u8*>(TH10_REC_KSEG0|src);jpeg::u8* d=reinterpret_cast<jpeg::u8*>(TH10_REC_KSEG0|dst);
    u32 i=0;
    if(!((src^dst)&3u)){   // same word phase: bytes up to a word boundary, then words
        while(i<n&&((dst+i)&3u)){d[i]=s[i];++i;}
        for(;i+4u<=n;i+=4u)*reinterpret_cast<u32*>(d+i)=*reinterpret_cast<const u32*>(s+i);
    }
    for(;i<n;i++)d[i]=s[i];
    writeback_lines(TH10_REC_KSEG0|dst,n);
    j->r[0]=n;j->status=StatusOk;
}
// One copy job from the writer's ring (served before the encode ring).
TH10_JPEG_CORE inline void run_copy_one(const volatile u32* audio_submitted){
    volatile Box* r=wring();const u32 index=r->completed;volatile Job* j=wslot(index);
    const u32 audio_before=*audio_submitted,start=count();
    j->count_start=start;
    if(j->kind==JobCopy)run_copy(j);
    else j->status=StatusBadKind;
    const u32 end=count(),spent=end-start;
    j->count_end=end;r->busy_total=r->busy_total+spent;
    if(*audio_submitted!=audio_before){r->audio_waits=r->audio_waits+1;if(spent>r->audio_wait_max)r->audio_wait_max=spent;}
    TH10_REC_ME_SYNC();
    j->done=j->serial;TH10_REC_ME_SYNC();
    r->completed=index+1;TH10_REC_ME_SYNC();
}
// One job, between two audio checks. `audio_submitted` is the audio mailbox's
// submit counter: if it moved while this job ran, an audio job waited for it.
// Dispatch by compare-and-branch, never a jump table: a table holds link-time
// (0x08...) addresses, so `jr` would leave the kseg0 (0x88...) alias the ME
// code has always run from; MeAudio's proven ME code has no indirect jumps.
TH10_JPEG_CORE inline void run_one(const volatile u32* audio_submitted){
    volatile Box* r=ring();const u32 index=r->completed;volatile Job* j=slot(index);
    const u32 audio_before=*audio_submitted,start=count();
    j->count_start=start;
    const u32 kind=j->kind;
    if(kind==JobJpeg)run_jpeg(j);
    else if(kind==JobClock)run_clock(j);
    else j->status=StatusBadKind;   // never left pending (TH08 r183-r189)
    const u32 end=count(),spent=end-start;
    j->count_end=end;r->busy_total=r->busy_total+spent;
    if(*audio_submitted!=audio_before){r->audio_waits=r->audio_waits+1;if(spent>r->audio_wait_max)r->audio_wait_max=spent;}
    TH10_REC_ME_SYNC();
    j->done=j->serial;TH10_REC_ME_SYNC();
    r->completed=index+1;TH10_REC_ME_SYNC();
}
}
