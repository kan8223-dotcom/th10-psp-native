#pragma once
#include "ResourceCodec.hpp"
namespace th10 {
struct ArchiveLifecycleEnvironment;
struct ArchiveEntry {char* name;u32 offset,size,reserved;};
struct ArchiveCipher {u8 key,step;u16 reserved;i32 block,limit;};
static_assert(sizeof(ArchiveEntry)==16&&sizeof(ArchiveCipher)==12);
struct ArchiveEnvironment : CodecMemory {
    const ArchiveCipher* ciphers;
    virtual bool seek(void* stream,u32 offset)=0;
    virtual u32 read(void* stream,u8* output,u32 length)=0;
    virtual void decrypt(u8* bytes,u32 length,const ArchiveCipher& cipher)=0;
    virtual u8* decompress(u8* bytes,u32 length,u8* output,u32 capacity)=0;
    // th10_port: decode an entry pulled in pieces with the environment's own
    // dictionary; -2 when the environment only decodes whole entries.
    virtual i32 decompress_input(LzssInput&,u8*,u32){return -2;}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    // th10_port: the dictionary decompress/decompress_input use (nullptr: no
    // piece-by-piece entry streams, ResourceArchive::open_stream).
    virtual u8* lzss_dictionary(){return nullptr;}
#endif
};
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// th10_port (TH10_ANM_STREAM_LOAD): one packed entry decoded piece by piece,
// so a loader converts what it reads and never holds the whole entry.
struct ResourceStream {
    virtual u32 read(u8* output,u32 count)=0;   // decoded bytes, fewer at the end or after a failure
    virtual u32 size() const=0;                  // the entry's decoded size
    virtual bool failed() const=0;               // a short archive read or a decode error
    virtual void close()=0;                      // decodes the rest, hands back the shared dictionary and window
    // Remember this point, and go back to it: the bytes read after the mark come
    // again, identical (one mark at a time; a reader that needs two looks at
    // the same bytes, without holding them).
    virtual bool mark()=0;
    virtual bool rewind()=0;
};
#endif
struct ResourceArchive {
    ArchiveEntry* entries;i32 count;char* filename;void* stream;
    static bool name_equal(const char* first,const char* second) noexcept;
    ArchiveEntry* find(const char* name) const noexcept;
    u32 size(const char* name) const noexcept;
    u8* read(const char* name,u8* output,ArchiveEnvironment& environment);
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    // nullptr when the entry is stored unpacked, the shared window is in use
    // or the environment has no dictionary: the caller then reads it whole.
    ResourceStream* open_stream(const char* name,ArchiveEnvironment& environment);
#endif
    void initialize() noexcept {entries=nullptr;count=0;filename=nullptr;stream=nullptr;}
    void release(ArchiveLifecycleEnvironment& environment);
    bool open(const char* name,bool memory,ArchiveLifecycleEnvironment& environment);
    bool load_index(const char* name,ArchiveLifecycleEnvironment& environment);
    static char* duplicate_name(const char* name,CodecMemory& memory);
    static ArchiveEntry* parse_index(const u8* bytes,i32 count,u32 table_offset,ArchiveLifecycleEnvironment& environment);
    static void release_entry(ArchiveEntry& entry,CodecMemory& memory);
    static void release_entries(ArchiveEntry* entries,ArchiveLifecycleEnvironment& environment);
};
static_assert(sizeof(ResourceArchive)==16);
struct ArchiveLifecycleEnvironment : ArchiveEnvironment {
    const char* const* open_mode;
    virtual void* allocate_object(u32 bytes)=0;
    virtual void free_object(void* object)=0;
    virtual void* create_stream(bool memory)=0;
    virtual bool open_stream(void* stream,const char* name,const char* mode)=0;
    virtual u32 stream_length(void* stream)=0;
    virtual void destroy_stream(void* stream)=0;
    virtual ArchiveEntry* build_index(const u8* data,i32 count,u32 table_offset){return ResourceArchive::parse_index(data,count,table_offset,*this);}
};
}
