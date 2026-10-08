#pragma once
// The Media Engine eDRAM plan, one table for every user (2026-10-01; final only if the 1/5/10-minute eDRAM retention run passes).
// Offsets are physical offsets into the ME-local eDRAM: 4 MiB on the Go, two
// independent halves 0x000000-0x1FFFFF and 0x200000-0x3FFFFF (G0 P1 on the
// Go: no mirror, 0 flipped bits up to ~5 s dwell). The top 64 KiB of each half
// is never used (the ME stack descends from 0x003FFFA8, kseg0 0x803FFFA8; G0
// kept the same guards), so 3968 KiB are usable. Move a boundary here and every
// user follows; the static_asserts keep the regions apart.
#include <cstdint>

namespace th10::edram {
using u32=std::uint32_t;
struct Region {u32 offset,bytes;};
constexpr u32 KiB=1024u;
constexpr Region half_lower{0x000000u,0x200000u},half_upper{0x200000u,0x200000u};
constexpr Region guard_lower{0x1F0000u,0x10000u},guard_upper{0x3F0000u,0x10000u};   // top 64 KiB of each half: ME stack, never touched

// BGM cache (the BGM side's, not the recorder's): 17 s of 44.1 kHz stereo s16
// = 2929 KiB plus ~3 KiB of per-4 KiB checksums: all of the usable lower half,
// then the start of the upper half.
constexpr u32 bgm_bytes=2932u*KiB;
constexpr Region bgm_lower{0x000000u,0x1F0000u};
constexpr Region bgm_upper{0x200000u,bgm_bytes-0x1F0000u};                       // 948 KiB

// Recorder (psp/Recorder.cpp with th10rec.txt ring_edram=1): the JPEG ring
// (~2 s at 15 fps q50, G0b p95 29.6 KB per frame) and working memory (the
// JPEG tables and the 320x240 strip still live in Main RAM, as G0b ran them
// on the device; the region is reserved for them).
constexpr Region rec_ring{bgm_upper.offset+bgm_upper.bytes,1000u*KiB};         // 0x2ED000, 1000 KiB
constexpr Region rec_work{rec_ring.offset+rec_ring.bytes,guard_upper.offset-(rec_ring.offset+rec_ring.bytes)};   // 0x3E7000, 36 KiB

constexpr bool inside(Region r,Region in){return r.offset>=in.offset&&r.offset+r.bytes<=in.offset+in.bytes;}
constexpr bool apart(Region a,Region b){return a.offset+a.bytes<=b.offset||b.offset+b.bytes<=a.offset;}
static_assert(inside(bgm_lower,half_lower)&&apart(bgm_lower,guard_lower),"BGM lower half");
static_assert(inside(bgm_upper,half_upper)&&apart(bgm_upper,guard_upper),"BGM upper part");
static_assert(inside(rec_ring,half_upper)&&apart(rec_ring,guard_upper)&&apart(rec_ring,bgm_upper),"recorder ring");
static_assert(inside(rec_work,half_upper)&&apart(rec_work,guard_upper)&&apart(rec_work,rec_ring)&&apart(rec_work,bgm_upper),"recorder work");
static_assert(!(rec_ring.offset&63u)&&!(rec_ring.bytes&63u),"the ring is whole cache lines");
}
