#pragma once
#include "Types.hpp"
namespace th10 {
struct CodecMemory {
    virtual u8* allocate_bytes(u32 bytes)=0;
    virtual void release_bytes(void* bytes)=0;
};
// Resource/save/replay byte permutation (0x44b0d0, 0x44b220).
u8* transform_resource(u8* data,i32 length,u8 key,u8 step,i32 block,i32 limit,bool encrypt,CodecMemory& memory);
// The original dictionary survives decoding calls and is not zeroed here.
i32 decode_lzss(const u8* source,u32 length,u8* destination,u32 capacity,u8* dictionary) noexcept;
// th10_port: input handed over in pieces; pull() points `bytes` at the next
// piece and returns its length, 0 at the end (then the decoder reads zeros).
struct LzssInput {virtual u32 pull(const u8*& bytes) noexcept=0;};
i32 decode_lzss(LzssInput& input,u8* destination,u32 capacity,u8* dictionary) noexcept;
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// th10_port (TH10_ANM_STREAM_LOAD): decode_lzss over pulled input, resumed
// piece by piece so the caller never holds the whole entry. The same bits,
// dictionary updates (a match copies byte by byte, so it may overlap the bytes
// it writes) and capacity rule; a match cut by the caller's piece continues
// at its next dictionary position on the next read.
struct LzssStream {
    LzssInput* input=nullptr;const u8* window=nullptr;u32 cursor=0,size=0,mask=128,byte=0;
    u8* dictionary=nullptr;u32 head=1,match_at=0,match_left=0,written=0,capacity=0;bool ended=false,failed=false;
    void start(LzssInput& source,u8* dictionary_bytes,u32 decoded_capacity) noexcept {*this=LzssStream{};input=&source;dictionary=dictionary_bytes;capacity=decoded_capacity;}
    // Up to `count` decoded bytes; fewer once the stream has ended or failed (an
    // overflow past the capacity fails it, as decode_lzss returns -1).
    u32 read(u8* output,u32 count) noexcept;
};
#endif
struct LzssSearchNode {u32 parent,left,right;};
struct LzssSearch {
    LzssSearchNode* nodes;
    u8* dictionary;
    void initialize() noexcept;
    u32 insert(u32 index,u32& match) noexcept;
    void remove(u32 index) noexcept;
    void replace(u32 destination,u32 source) noexcept;
    void promote(u32 destination,u32 source) noexcept;
    u32 predecessor(u32 index) const noexcept;
};
u8* encode_lzss(const u8* source,i32 length,u32& packed_length,LzssSearch search,CodecMemory& memory);
}
