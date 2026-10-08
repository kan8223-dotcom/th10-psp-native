#include "ResourceArchive.hpp"
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
#include <new>
#endif
namespace th10 {
bool ResourceArchive::name_equal(const char* first,const char* second) noexcept {
    for(;;){u8 a=*first++,b=*second++;if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)return false;if(!a)return true;}
}
ArchiveEntry* ResourceArchive::find(const char* name) const noexcept {if(entries)for(i32 i=0;i<count;++i)if(name_equal(entries[i].name,name))return entries+i;return nullptr;}
u32 ResourceArchive::size(const char* name) const noexcept {const auto* entry=find(name);return entry?entry->size:0;}
#ifndef TH10_STREAM_ARCHIVE
#ifdef TH_NATIVE_PLATFORM
#define TH10_STREAM_ARCHIVE 1
#else
#define TH10_STREAM_ARCHIVE 0
#endif
#endif
#if TH10_STREAM_ARCHIVE
namespace {
// th10_port (TH08 src/pbg/Lzss.cpp LzssFileBitReader, sha256 05142297...): a
// packed entry is decoded straight from the archive
// through one shared 64 KiB window instead of a whole-entry allocation
// (stgenm04.anm: the 1,029,830 bytes that failed at the stage 3 -> 4 load on
// the Go). TH08 read 4 KiB pieces first and stalled the Go's internal storage.
alignas(64) u8 window[65536];
volatile int window_busy=0;
struct ArchiveInput final : LzssInput {
    ArchiveEnvironment& env;void* stream;u32 remaining;const ArchiveCipher& cipher;bool first=true,failed=false;
    ArchiveInput(ArchiveEnvironment& e,void* s,u32 length,const ArchiveCipher& c):env(e),stream(s),remaining(length),cipher(c){}
    u32 pull(const u8*& bytes) noexcept override {
        const u32 n=remaining<sizeof(window)?remaining:u32(sizeof(window));if(!n)return 0;
        if(env.read(stream,window,n)!=n){failed=true;remaining=0;return 0;}
        // The cipher covers at most the first 30,720 bytes (block multiples),
        // all inside the first piece: a piece of 64 KiB, or the whole entry,
        // is permuted exactly as the whole entry would be.
        if(first){env.decrypt(window,n,cipher);first=false;}
        remaining-=n;bytes=window;return n;
    }
};
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// TH10_ANM_STREAM_LOAD: ArchiveInput for a stream that stays open while its
// reader converts textures, so each piece seeks to its own offset first
// (another read of the archive in between cannot move it). The decoder runs
// on a copy of the shared dictionary, handed back on close: a nested
// whole-entry decode cannot disturb it, and later decodes start from the
// dictionary a whole-entry decode of this entry would have left.
struct StreamInput final : LzssInput {
    ArchiveEnvironment& env;void* stream;u32 position,remaining;const ArchiveCipher& cipher;bool first=true,failed=false;
    StreamInput(ArchiveEnvironment& e,void* s,u32 offset,u32 length,const ArchiveCipher& c):env(e),stream(s),position(offset),remaining(length),cipher(c){}
    u32 pull(const u8*& bytes) noexcept override {
        const u32 n=remaining<sizeof(window)?remaining:u32(sizeof(window));if(!n)return 0;
        if(!env.seek(stream,position)||env.read(stream,window,n)!=n){failed=true;remaining=0;return 0;}
        if(first){env.decrypt(window,n,cipher);first=false;}   // as ArchiveInput: the cipher stays inside the first piece
        position+=n;remaining-=n;bytes=window;return n;
    }
};
struct EntryStream final : ResourceStream {
    ArchiveEnvironment* env=nullptr;u8* shared=nullptr;u8* own=nullptr;u32 capacity=0,entry_offset=0;bool open=false;
    alignas(StreamInput) u8 input_bytes[sizeof(StreamInput)];LzssStream lzss;
    // mark(): the decoder and input state and a copy of the dictionary (8 KiB).
    struct Saved {LzssStream lzss;u32 position=0,remaining=0;bool first=false,failed=false;u8* dictionary=nullptr;} saved;bool marked=false;
    StreamInput& input(){return *reinterpret_cast<StreamInput*>(input_bytes);}
    u32 read(u8* output,u32 count) override {return open?lzss.read(output,count):0;}
    u32 size() const override {return capacity;}
    bool failed() const override {return lzss.failed||reinterpret_cast<const StreamInput*>(input_bytes)->failed;}
    bool mark() override {
        if(!open)return false;if(!saved.dictionary&&!(saved.dictionary=env->allocate_bytes(8192)))return false;
        auto& in=input();saved.lzss=lzss;saved.position=in.position;saved.remaining=in.remaining;saved.first=in.first;saved.failed=in.failed;
        std::memcpy(saved.dictionary,own,8192);marked=true;return true;
    }
    // The piece of input the decoder was reading at the mark is read into the
    // window again (and permuted again when it is the entry's first piece).
    bool rewind() override {
        if(!open||!marked)return false;
        auto& in=input();lzss=saved.lzss;in.position=saved.position;in.remaining=saved.remaining;in.first=saved.first;in.failed=saved.failed;
        std::memcpy(own,saved.dictionary,8192);
        if(lzss.cursor<lzss.size){const u32 start=in.position-lzss.size;
            if(!env->seek(in.stream,start)||env->read(in.stream,window,lzss.size)!=lzss.size){in.failed=true;return false;}
            if(start==entry_offset)env->decrypt(window,lzss.size,in.cipher);}
        return true;
    }
    void close() override {
        if(!open)return;
        u8 rest[256];while(!lzss.failed&&lzss.read(rest,sizeof(rest))){}
        std::memcpy(shared,own,8192);env->release_bytes(own);own=nullptr;open=false;
        if(saved.dictionary){env->release_bytes(saved.dictionary);saved.dictionary=nullptr;}marked=false;
        __atomic_store_n(&window_busy,0,__ATOMIC_RELEASE);
    }
} entry_stream;
#endif
}
#endif
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD && !TH10_STREAM_ARCHIVE
#error "TH10_ANM_STREAM_LOAD needs TH10_STREAM_ARCHIVE (its shared window)"
#endif
u8* ResourceArchive::read(const char* name,u8* output,ArchiveEnvironment& env){
    if(!stream)return nullptr;auto* entry=find(name);if(!entry)return nullptr;
    const u32 length=entry[1].offset-entry->offset,capacity=entry->size;
#if TH10_STREAM_ARCHIVE
    if(length!=capacity&&output&&__sync_bool_compare_and_swap(&window_busy,0,1)){
        u8 key=0;for(const char* p=entry->name;*p;++p)key=static_cast<u8>(key+static_cast<u8>(*p));
        i32 written=-1;bool failed=true;
        if(env.seek(stream,entry->offset)){ArchiveInput input{env,stream,length,env.ciphers[key&7]};written=env.decompress_input(input,output,capacity);failed=input.failed;}
        __atomic_store_n(&window_busy,0,__ATOMIC_RELEASE);
        if(written!=-2)return written>=0&&!failed?output:nullptr;
    }
#endif
    u8* bytes=length==capacity&&output?output:env.allocate_bytes(length);if(!bytes)return nullptr;
    if(!env.seek(stream,entry->offset)||!env.read(stream,bytes,length)){env.release_bytes(bytes);return nullptr;}
    u8 key=0;for(const char* p=entry->name;*p;++p)key=static_cast<u8>(key+static_cast<u8>(*p));
    env.decrypt(bytes,length,env.ciphers[key&7]);
    u8* result=length==capacity?bytes:env.decompress(bytes,length,output,capacity);
    if(bytes!=output)env.release_bytes(bytes);return result;
}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
ResourceStream* ResourceArchive::open_stream(const char* name,ArchiveEnvironment& env){
    if(!stream)return nullptr;auto* entry=find(name);if(!entry)return nullptr;
    const u32 length=entry[1].offset-entry->offset,capacity=entry->size;u8* shared=env.lzss_dictionary();
    if(length==capacity||!shared||!__sync_bool_compare_and_swap(&window_busy,0,1))return nullptr;
    u8* own=env.allocate_bytes(8192);
    if(!own){__atomic_store_n(&window_busy,0,__ATOMIC_RELEASE);return nullptr;}
    std::memcpy(own,shared,8192);
    u8 key=0;for(const char* p=entry->name;*p;++p)key=static_cast<u8>(key+static_cast<u8>(*p));
    auto& s=entry_stream;s.env=&env;s.shared=shared;s.own=own;s.capacity=capacity;s.entry_offset=entry->offset;s.marked=false;s.open=true;
    new(s.input_bytes) StreamInput{env,stream,entry->offset,length,env.ciphers[key&7]};
    s.lzss.start(s.input(),own,capacity);return &s;
}
#endif
}
