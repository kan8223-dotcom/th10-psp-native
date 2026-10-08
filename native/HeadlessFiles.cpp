// stdio file host with the same contract as th10_web/cpp/sdl/FileHost.cpp.
// Read-only game files come from TH10_GAME_DIR (default ./game); saves and
// replays go to TH10_SAVE_DIR (default ./save)/jp or /chs.
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../th10_web/cpp/game/Types.hpp"
#ifdef __PSP__
#include <pspiofilemgr.h>
#if TH10_REC
#include <pspkernel.h>
extern "C" void th10_rec_game_read(unsigned bytes,unsigned us);   // psp/Recorder.cpp: its writer keeps clear of the game's reads
#endif
#if TH10_BGM_PREFETCH
#include <malloc.h>
#include <pspkernel.h>
#if TH10_REC
extern "C" void th10_rec_bgm_read(void);   // psp/Recorder.cpp: the writer starts right after a read-ahead read
#endif
#endif
#endif
using th10::u32;using th10::i32;using th10::u8;using th10::u64;
namespace {
std::string env(const char* name,const char* fallback){const char* v=std::getenv(name);return v&&*v?v:fallback;}
std::string game_root(){static const std::string root=env("TH10_GAME_DIR","game");return root;}
std::string save_base(){static const std::string root=env("TH10_SAVE_DIR","save");return root;}
std::string save_root=save_base()+"/jp";u32 next=1;
#ifdef __PSP__
std::map<u32,SceUID> handles;
#else
std::map<u32,FILE*> handles;
#endif
std::set<u32> writers;
std::string normalize(const char* value){
    if(!value||!*value)return {};
    std::string out,part;auto flush=[&](){if(part=="..")return false;if(!part.empty()&&part!="."){if(!out.empty())out+='/';out+=part;}part.clear();return true;};
    for(const auto* p=value;*p;p++){char c=*p;if(c==':')return {};if(c=='/'||c=='\\'){if(!flush())return {};}else part+=c>='A'&&c<='Z'?char(c+32):c;}
    if(!flush())return {};return out;
}
void parents(const std::string& path){for(size_t i=1;i<path.size();i++)if(path[i]=='/')mkdir(path.substr(0,i).c_str(),0777);}
bool match(const char* pat,const char* s){
    if(std::strcmp(pat,"*.*")==0)pat="*";
    const char* star=nullptr;const char* retry=nullptr;
    while(*s){if(*pat=='?'||*pat==*s){++pat;++s;}else if(*pat=='*'){star=pat++;retry=s;}else if(star){pat=star+1;s=++retry;}else return false;}
    while(*pat=='*')++pat;return !*pat;
}
#ifndef __PSP__
FILE* find(u32 id){auto it=handles.find(id);return it==handles.end()?nullptr:it->second;}
#endif
// Read-pattern census (reported at exit): PSP Go internal storage stalls on
// bursts of tiny reads, so know the sizes the game asks for.
struct ReadStats {u64 reads=0,bytes=0,seeks=0,opens=0,buckets[8]{};~ReadStats(){
    if(!reads)return;static const char* names[]={"<512","<4K","<16K","<64K","<256K","<1M","<4M",">=4M"};
    std::fprintf(stderr,"file reads=%llu bytes=%llu seeks=%llu opens=%llu sizes:",(unsigned long long)reads,(unsigned long long)bytes,(unsigned long long)seeks,(unsigned long long)opens);
    for(int i=0;i<8;i++)std::fprintf(stderr," %s=%llu",names[i],(unsigned long long)buckets[i]);std::fprintf(stderr,"\n");}}read_stats;
void count_read(u32 size){++read_stats.reads;read_stats.bytes+=size;const u32 limits[]={512,4096,16384,65536,262144,1048576,4194304};int b=0;while(b<7&&size>=limits[b])++b;++read_stats.buckets[b];}
#if defined(__PSP__) && TH10_BGM_PREFETCH
// th10_port (TH10_BGM_PREFETCH): read-ahead for thbgm.dat. The game's music
// worker refills its 4 s stream with one 44,100-byte read every 0.25 s on
// the game thread; while the recorder writes to the M2 such a read waited up
// to 0.5 s (28_rec/28b_rec on the Go) and the per-tick audio jobs ran dry. A
// thread below the game (0x2c, above the recorder's writer 0x30) reads the
// file ahead into a 1 MiB ring (5.9 s of 44.1 kHz stereo; 256 KiB ran dry
// under 1.5 s M2 writes in a 5-minute recording, 30_play) with its own
// handle; the game thread copies from the ring. Same bytes as before: the
// ring holds the file's bytes from the game's position on; a seek outside the
// buffered window (loop point, new track) drops the ring and the reads start
// again there; after 2 s without data the game thread reads by itself.
namespace bgm {
constexpr u32 ring_bytes=1024u*1024u,piece=32u*1024u;
u8* ring=nullptr;SceUID file=-1,thread=-1,lock=-1;
u32 id=0;                          // browser handle being served (0: none)
u32 gen=0,head=0,filled=0,state=0; // under `lock`; state bit 1: read error
u64 pos=0,size=0;                  // pos: file offset of ring[head]
u32 hits=0,waits=0,fallbacks=0,flushes=0,reads=0,wait_max_us=0,read_max_us=0,low_water=ring_bytes;u64 wait_us=0;
void take(){sceKernelWaitSema(lock,1,nullptr);}
void give(){sceKernelSignalSema(lock,1);}
int run(SceSize,void*){
    for(;;){
        take();const u32 g=gen,f=filled,h=head;const u64 at=pos+f;const bool active=id!=0&&!(state&2u)&&f+piece<=ring_bytes&&at<size;give();
        if(!active){sceKernelDelayThread(2000);continue;}
        const u32 tail=(h+f)%ring_bytes;u32 n=piece;if(n>ring_bytes-tail)n=ring_bytes-tail;if(u64(n)>size-at)n=u32(size-at);
        const u64 t0=sceKernelGetSystemTimeWide();int r=-1;
        if(sceIoLseek(file,SceOff(at),PSP_SEEK_SET)==SceOff(at))r=sceIoRead(file,ring+tail,n);
        const u32 us=u32(sceKernelGetSystemTimeWide()-t0);
        take();if(g==gen){if(r>0){filled+=u32(r);++reads;if(us>read_max_us)read_max_us=us;}else state|=2u;}give();
#if TH10_REC
        if(r>0)th10_rec_bgm_read();
#endif
    }
    return 0;
}
// browser_open of thbgm.dat (read-only): serve this handle if none is served.
void attach(u32 handle,const std::string& path){
    if(id)return;
    if(lock<0){lock=sceKernelCreateSema("th10_bgm",0,1,1,nullptr);if(lock<0)return;}
    if(!ring){ring=static_cast<u8*>(memalign(64,ring_bytes));if(!ring)return;}
    if(file<0){file=sceIoOpen(path.c_str(),PSP_O_RDONLY,0);if(file<0)return;}
    const SceOff end=sceIoLseek(file,0,PSP_SEEK_END);if(end<=0)return;
    take();id=handle;size=u64(end);pos=0;head=0;filled=0;state=0;++gen;give();
    if(thread<0){thread=sceKernelCreateThread("th10_bgm_read",run,0x2c,0x2000,PSP_THREAD_ATTR_USER,nullptr);if(thread>=0)sceKernelStartThread(thread,0,nullptr);}
}
void detach(u32 handle){if(!id||handle!=id)return;take();id=0;++gen;head=0;filled=0;give();}
u32 seek(i32 offset,u32 origin){
    take();
    const std::int64_t base=origin==0?0:origin==1?std::int64_t(pos):std::int64_t(size);const std::int64_t t=base+offset;
    if(t<0){give();return ~0u;}
    const u64 target=u64(t);
    if(target>=pos&&target<=pos+filled){const u32 skip=u32(target-pos);head=(head+skip)%ring_bytes;filled-=skip;pos=target;}
    else{++gen;head=0;filled=0;pos=target;state=0;++flushes;}
    give();return u32(target);
}
u32 read(SceUID game_file,u8* out,u32 want){
    u32 done=0;bool waited=false;const u64 t0=sceKernelGetSystemTimeWide();
    take();if(filled<low_water)low_water=filled;give();
    for(;;){
        take();u32 n=want-done;if(n>filled)n=filled;if(n>ring_bytes-head)n=ring_bytes-head;const u32 h=head;
        const bool end=pos>=size,bad=(state&2u)!=0;give();
        if(n){std::memcpy(out+done,ring+h,n);take();head=(head+n)%ring_bytes;filled-=n;pos+=n;give();done+=n;if(done==want)break;continue;}
        if(end)break;
        if(bad||sceKernelGetSystemTimeWide()-t0>2000000u){   // the read-ahead is stuck: read the rest here, then start again after it
            take();const u64 at=pos;give();int r=-1;
            if(sceIoLseek(game_file,SceOff(at),PSP_SEEK_SET)==SceOff(at))r=sceIoRead(game_file,out+done,want-done);
            take();++gen;head=0;filled=0;pos=at+u64(r>0?r:0);state=0;give();if(r>0)done+=u32(r);++fallbacks;break;
        }
        waited=true;sceKernelDelayThread(500);
    }
    if(waited){++waits;const u32 us=u32(sceKernelGetSystemTimeWide()-t0);wait_us+=us;if(us>wait_max_us)wait_max_us=us;}else ++hits;
    return done;
}
}
#endif
}
extern "C" {
void headless_read_stats(u64* out){out[0]=read_stats.reads;out[1]=read_stats.bytes;out[2]=read_stats.seeks;for(int i=0;i<8;i++)out[3+i]=read_stats.buckets[i];}
#if defined(__PSP__) && TH10_BGM_PREFETCH
void headless_bgm_stats(u32* out){out[0]=bgm::hits;out[1]=bgm::waits;out[2]=bgm::wait_max_us;out[3]=u32(bgm::wait_us/1000u);out[4]=bgm::fallbacks;out[5]=bgm::flushes;out[6]=bgm::reads;out[7]=bgm::read_max_us;out[8]=bgm::low_water;out[9]=bgm::ring_bytes;}
#endif
#ifdef __PSP__
// PSP: sceIo directly. Each game request becomes one sceIoRead of the same
// size (no stdio buffer splitting): the Go's internal storage stalls on
// bursts of tiny reads (TH08 ef0 30 s stall).
u32 browser_open(const char* raw,u32 write){const auto name=normalize(raw);if(name.empty())return ~0u;SceUID f=-1;
    if(write){const auto path=save_root+"/"+name;parents(path);f=sceIoOpen(path.c_str(),PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);}
    else {f=sceIoOpen((save_root+"/"+name).c_str(),PSP_O_RDONLY,0);if(f<0)f=sceIoOpen((game_root()+"/"+name).c_str(),PSP_O_RDONLY,0);}
    if(f<0)return ~0u;++read_stats.opens;const auto id=next++;handles[id]=f;if(write)writers.insert(id);
#if TH10_BGM_PREFETCH
    if(!write&&name=="thbgm.dat"){SceUID probe=sceIoOpen((save_root+"/"+name).c_str(),PSP_O_RDONLY,0);std::string path=save_root+"/"+name;
        if(probe>=0)sceIoClose(probe);else path=game_root()+"/"+name;bgm::attach(id,path);}
#endif
    return id;
}
void browser_close(u32 id){
#if TH10_BGM_PREFETCH
    bgm::detach(id);
#endif
    auto it=handles.find(id);if(it!=handles.end()){sceIoClose(it->second);handles.erase(it);writers.erase(id);}}
u32 browser_size(u32 id){auto it=handles.find(id);if(it==handles.end())return ~0u;
#if TH10_BGM_PREFETCH
    if(bgm::id&&id==bgm::id)return u32(bgm::size);
#endif
const SceOff here=sceIoLseek(it->second,0,PSP_SEEK_CUR),end=sceIoLseek(it->second,0,PSP_SEEK_END);sceIoLseek(it->second,here,PSP_SEEK_SET);return end<0?~0u:u32(end);}
u32 browser_seek(u32 id,i32 offset,u32 origin){auto it=handles.find(id);if(it==handles.end()||origin>2)return ~0u;
    ++read_stats.seeks;
#if TH10_BGM_PREFETCH
    if(bgm::id&&id==bgm::id)return bgm::seek(offset,origin);
#endif
    const SceOff r=sceIoLseek(it->second,offset,origin==0?PSP_SEEK_SET:origin==1?PSP_SEEK_CUR:PSP_SEEK_END);return r<0?~0u:u32(r);}
#if TH10_REC
u32 browser_read(u32 id,u8* out,u32 size){auto it=handles.find(id);if(it==handles.end())return 0;count_read(size);
#if TH10_BGM_PREFETCH
    if(bgm::id&&id==bgm::id)return bgm::read(it->second,out,size);   // no M2 access on the game thread: no hook for the writer
#endif
    const u64 t0=sceKernelGetSystemTimeWide();const int r=sceIoRead(it->second,out,size);th10_rec_game_read(size,unsigned(sceKernelGetSystemTimeWide()-t0));return r<0?0:u32(r);}
#else
u32 browser_read(u32 id,u8* out,u32 size){auto it=handles.find(id);if(it==handles.end())return 0;count_read(size);
#if TH10_BGM_PREFETCH
    if(bgm::id&&id==bgm::id)return bgm::read(it->second,out,size);
#endif
    const int r=sceIoRead(it->second,out,size);return r<0?0:u32(r);}
#endif
u32 browser_write(u32 id,const u8* in,u32 size){auto it=handles.find(id);if(it==handles.end())return 0;const int r=sceIoWrite(it->second,in,size);return r<0?0:u32(r);}
#else
u32 browser_open(const char* raw,u32 write){const auto name=normalize(raw);if(name.empty())return ~0u;FILE* f=nullptr;
    if(write){const auto path=save_root+"/"+name;parents(path);f=std::fopen(path.c_str(),"wb");}
    else {f=std::fopen((save_root+"/"+name).c_str(),"rb");if(!f)f=std::fopen((game_root()+"/"+name).c_str(),"rb");}
    if(!f)return ~0u;++read_stats.opens;const auto id=next++;handles[id]=f;if(write)writers.insert(id);return id;
}
void browser_close(u32 id){auto it=handles.find(id);if(it!=handles.end()){std::fclose(it->second);handles.erase(it);writers.erase(id);}}
u32 browser_size(u32 id){auto* f=find(id);if(!f)return ~0u;struct stat st{};if(fstat(fileno(f),&st))return ~0u;return u32(st.st_size);}
u32 browser_seek(u32 id,i32 offset,u32 origin){auto* f=find(id);if(!f||origin>2)return ~0u;
    ++read_stats.seeks;const int whence=origin==0?SEEK_SET:origin==1?SEEK_CUR:SEEK_END;if(std::fseek(f,offset,whence))return ~0u;return u32(std::ftell(f));}
u32 browser_read(u32 id,u8* out,u32 size){auto* f=find(id);if(!f)return 0;count_read(size);return u32(std::fread(out,1,size,f));}
u32 browser_write(u32 id,const u8* in,u32 size){auto* f=find(id);return f?u32(std::fwrite(in,1,size,f)):0;}
#endif
u32 browser_list(const char* directory,const char* pattern,u32 index,char* out,u32 capacity){
    const auto dir=normalize(directory);std::vector<std::string> names;
    for(const auto& root:{game_root(),save_root})if(auto* d=opendir((root+"/"+dir).c_str())){
        while(auto* e=readdir(d)){const std::string name=e->d_name;if(name=="."||name==".."||!match(pattern,name.c_str()))continue;
            struct stat st{};if(!stat((root+"/"+dir+"/"+name).c_str(),&st)&&S_ISREG(st.st_mode))names.push_back(name);
        }closedir(d);
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    if(index>=names.size()||names[index].size()+1>capacity)return 0;std::memcpy(out,names[index].c_str(),names[index].size()+1);return 1;
}
void sdl_files_root(u32 chinese){save_root=save_base()+(chinese?"/chs":"/jp");parents(save_root+"/replay/");}
u32 sdl_file_handles(){return u32(handles.size());}
}
