#pragma once
// TH10 recording (TH10_REC=1 only; the default build never includes this
// file): the typed Media Engine job ring shared by the SC side
// (psp/Recorder.cpp) and the ME loop (psp/MeAudio.cpp, which includes
// psp/RecMeKernel.inl). Trimmed from the G0b measurement build
// (RecG0.hpp, not published): only the JPEG job and the
// clock job remain; the eDRAM probes and band codec are G0 measurements.
//
// A second ring next to the audio FIFO, with its own counters. The ME loop
// serves audio first and runs at most one of these jobs between two audio
// checks, so an audio job waits at most one job (one job = up to 20 MCUs of
// one MCU row, about 1/34 of a 480x272 frame). TH08 me_worker.cpp
// (sha256 3ecc71f1...) and TH07 audio_me.c (sha256 7f7d73e9...) typed their ME
// commands the same way; an unknown kind is completed with an error status,
// never left pending (TH08 r183-r189 spun idle on an unconverted kind).
#include "MeEdramMap.hpp"
#include "RecJpeg.hpp"

// Hardware primitives (the PC simulation, native/tools/rec_sim.cpp, defines
// them before including; the PSP build always gets these values):
#ifndef TH10_REC_UNCACHED
#define TH10_REC_UNCACHED 0x40000000u   // uncached alias of Main RAM (SC and ME)
#define TH10_REC_KSEG0 0x80000000u      // the ME's cached kseg0 alias
#define TH10_REC_RAM_LO 0x08800000u     // user Main RAM the SC hands to the ME
#define TH10_REC_RAM_HI 0x0c000000u
#define TH10_REC_SYNC() asm volatile("sync":::"memory")   // SC side
#define TH10_REC_ME_SYNC() asm volatile("sync")             // ME side (as the G0b kernel)
#define TH10_REC_ME_COUNT(v) asm volatile("mfc0 %0, $9":"=r"(v))
#endif
#ifndef TH10_REC_EDRAM_BASE
#define TH10_REC_EDRAM_BASE 0x00000000u  // physical address of the ME eDRAM (the ME reads it through TH10_REC_KSEG0|...)
#endif

namespace th10::rec {
using u32=std::uint32_t;
// The captured frame: RGB565, 480 x 272, stride 480 (GE copy of the 512-wide framebuffer).
constexpr u32 width=480,height=272,frame_words=width*height,frame_bytes=frame_words*2u;
enum JobKind : u32 {JobNone=0,JobClock=1,JobJpeg=6,JobCopy=7};   // 6 = G0b's JobJpeg (same ME code)
enum JobStatus : u32 {StatusPending=0,StatusOk=1,StatusBadKind=2,StatusBadArgs=3};
constexpr u32 job_slots=32;
// The writer thread's own ring (copy jobs: JPEG ring -> Main RAM write buffer).
// One producer per ring (the game thread feeds th10_rec_box, the writer thread
// th10_rec_wbox); the ME serves this one before the encode ring (audio first).
constexpr u32 wjob_slots=4,copy_max=0x10000u;
// JPEG job flags (RecMeKernel.inl run_jpeg). One job = a run of MCUs of one
// MCU row; the encoder state is carried in a JpegCtx between the jobs of a
// frame (the ring runs in order on one ME), so no restart markers.
constexpr u32 JpegFirst=1u,JpegLast=2u,JpegScale=4u;
// Carried between the JPEG jobs of a frame. One cache line, only ever
// touched through the uncached alias (SC and ME), like the job records.
struct alignas(64) JpegCtx {
    u32 pos,acc,nbits,overflow;       // = jpeg::State
    std::int32_t dc0,dc1,dc2;
    u32 blocks,flat;
    u32 pad[7];
};
static_assert(sizeof(JpegCtx)==64,"jpeg ctx is one cache line");
// One job: 64 words (256 bytes, four cache lines), uncached alias only.
struct alignas(64) Job {
    u32 kind,serial,a[8];                    // inputs
    u32 status,done,count_start,count_end;   // done = serial when finished
    u32 r[12];                               // results (per kind, see RecMeKernel.inl)
    u32 detail[8];
    u32 pad[30];
};
static_assert(sizeof(Job)==256,"rec job layout");
struct alignas(64) Box {
    u32 submitted,completed;          // like the audio mailbox
    u32 audio_waits;                  // audio jobs that arrived while a rec job ran
    u32 audio_wait_max;               // longest such rec job, ME count units
    u32 busy_total;                   // ME count units spent in rec jobs
    u32 jobs_base;                    // address of the job ring (SC view)
    u32 audio_busy;                   // ME count units spent in audio jobs (MeAudio.cpp)
    u32 pad[9];
};
static_assert(sizeof(Box)==64,"rec box is one cache line");
}

extern "C" {
extern th10::rec::Box th10_rec_box;                          // psp/Recorder.cpp
extern th10::rec::Job th10_rec_jobs[th10::rec::job_slots];
extern th10::rec::Box th10_rec_wbox;
extern th10::rec::Job th10_rec_wjobs[th10::rec::wjob_slots];
}
