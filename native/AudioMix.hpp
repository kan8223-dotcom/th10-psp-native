#pragma once
// Per-tick PCM mix jobs (TH10 audio output). One job is one game tick of
// output, about 735 frames of 44.1 kHz stereo: every playing DirectSound-style
// buffer of the audio host becomes a voice. The same integer kernel runs on
// the PSP Media Engine, as the SC fallback (PPSSPP, no ME) and on the PC for
// the WAV check, so all three produce identical blocks.
#include <cstdint>

namespace th10::mix {
using u32=std::uint32_t;using i32=std::int32_t;
constexpr u32 rate=44100,max_frames=1024,max_voices=32,staging_bytes=8192;
struct Voice {
    u32 source;         // address of the PCM bytes (SC view; the ME uses its cached alias)
    u32 frames;         // source length in frames; looping voices wrap here
    u32 loop;           // 1: wrap at `frames`, 0: stop at the end
    u32 channels;       // 1 or 2
    u32 bits;           // 8 (unsigned) or 16 (signed)
    u32 index,frac;     // start: frame index and 16-bit fraction
    u32 step;           // source frames per output frame, 16.16
    u32 gain_l,gain_r;  // Q15, 32768 = unity
    u32 invalidate;     // 1: data read in place (the ME invalidates what it reads)
    u32 reserved;
};
static_assert(sizeof(Voice)==48,"voice layout");
struct alignas(64) Job {
    u32 frames,voices,serial,done;   // done = serial once the block is mixed
    u32 cycles,on_me,reserved[10];
    Voice voice[max_voices];
    int16_t out[max_frames*2] __attribute__((aligned(64)));
    unsigned char staging[staging_bytes] __attribute__((aligned(64)));
};
static_assert(sizeof(Job)%64==0,"job layout");

// Mixes job.voice[] into job.out. `alias` selects the address view of the
// sources (0 on the SC, 0x80000000 cached kseg0 on the ME); `invalidate`
// (ME only) drops stale cache lines of in-place data before it is read.
inline void mix_job(Job& job,u32 alias,void (*invalidate)(u32 address,u32 bytes)){
    const u32 frames=job.frames<max_frames?job.frames:max_frames;
    i32 acc[max_frames*2];
    for(u32 i=0;i<frames*2;i++)acc[i]=0;
    const u32 voices=job.voices<max_voices?job.voices:max_voices;
    for(u32 v=0;v<voices;v++){
        const Voice& s=job.voice[v];
        const u32 frame_bytes=s.channels*(s.bits>>3);
        if(!s.frames||!frame_bytes||!s.step||s.index>=s.frames)continue;
        const auto* base=reinterpret_cast<const unsigned char*>(uintptr_t(alias?((s.source&0x1fffffffu)|alias):s.source));
        if(invalidate&&s.invalidate){
            // Frames this voice reads: [index, index + span), wrapping for loops.
            const u32 span=u32((uint64_t(s.frac)+uint64_t(frames)*s.step)>>16)+2;
            const u32 first=s.frames-s.index<span?s.frames-s.index:span;
            const u32 a=u32(uintptr_t(base))+s.index*frame_bytes;
            invalidate(a&~63u,((a&63u)+first*frame_bytes+63u)&~63u);
            if(s.loop&&span>first){const u32 rest=span-first<s.frames?span-first:s.frames;
                invalidate(u32(uintptr_t(base))&~63u,((u32(uintptr_t(base))&63u)+rest*frame_bytes+63u)&~63u);}
        }
        u32 index=s.index,frac=s.frac;const i32 gl=i32(s.gain_l),gr=i32(s.gain_r);
        for(u32 i=0;i<frames;i++){
            if(index>=s.frames){if(!s.loop)break;index%=s.frames;}
            const unsigned char* p=base+index*frame_bytes;i32 l,r;
            if(s.bits==16){l=*reinterpret_cast<const int16_t*>(p);r=s.channels==2?*reinterpret_cast<const int16_t*>(p+2):l;}
            else{l=(i32(p[0])-128)<<8;r=s.channels==2?(i32(p[1])-128)<<8:l;}
            acc[2*i]+=(l*gl)>>15;acc[2*i+1]+=(r*gr)>>15;
            frac+=s.step;index+=frac>>16;frac&=0xffffu;
        }
    }
    for(u32 i=0;i<frames*2;i++){const i32 x=acc[i];job.out[i]=int16_t(x>32767?32767:x<-32768?-32768:x);}
}
}

// Platform side (PSP: native/psp/MeAudio.cpp; PC: native/HeadlessAudio.cpp).
extern "C" {
th10::mix::Job* th10_audio_acquire(void);     // a free job, or nullptr
void th10_audio_submit(th10::mix::Job* job);  // mixed on the ME (or SC) and played in order
void th10_audio_written(const void* bytes,std::uint32_t size);  // CPU wrote PCM read in place (PSP: dcache writeback)
}
