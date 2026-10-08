// DirectSound-shaped audio host. Buffer ownership, formats, play cursors
// (from the host millisecond clock) and position notifications follow
// th10_web/cpp/sdl/AudioHost.cpp, so the game's BGM refill worker and sound
// bookkeeping run the same code paths on every platform.
// Output (TH10_AUDIO_OUT): each clock advance renders the slice just played,
// as one mix job of all playing buffers (AudioMix.hpp); the mixer keeps its
// own cursors and never changes what the game observes.
#include "../th10_web/cpp/platform/Audio.hpp"
#include "AudioMix.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
#include "psp/VolatileArena.hpp"   // PSP-1000 lane: sound buffers live in the 4 MiB volatile partition when it is locked
#endif
// Sound effects in the ME eDRAM (PSP-1000 lane, psp/SeEdram.hpp): on the PSP
// with TH10_SE_EDRAM (psp/MeAudio.cpp), on the PC with TH10_SE_EDRAM_SIM (a
// host array stands in for the eDRAM; the same SeEdram.hpp code, below).
#if (defined(__PSP__) && defined(TH10_SE_EDRAM) && TH10_SE_EDRAM) || (!defined(__PSP__) && defined(TH10_SE_EDRAM_SIM) && TH10_SE_EDRAM_SIM)
#define TH10_SE_HOOKS 1
#include "psp/SeEdram.hpp"
#else
#define TH10_SE_HOOKS 0
#endif
namespace {
using th10::u32;using th10::i32;using th10::u8;
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
using PcmBytes=std::vector<u8,VolatileAllocator<u8>>;
#else
using PcmBytes=std::vector<u8>;
#endif
template<class T=u32>T* ptr(u32 p){return reinterpret_cast<T*>(uintptr_t(p));}
u32 address(const void* p){return u32(reinterpret_cast<uintptr_t>(p));}
u32 read32(const u8* p){u32 v;std::memcpy(&v,p,4);return v;}
unsigned read16(const u8* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
struct Buffer {
    u32 refs=1,flags=0,length=0,cursor=0,last_cursor=0,frequency=44100;
    i32 volume=0,pan=0;bool device=false,playing=false,loop=false;
    uint64_t started=0;std::array<u8,18> format{{1,0,2,0,68,172,0,0,16,177,2,0,4,0,16,0,0,0}};
    std::shared_ptr<PcmBytes> pcm;
    std::vector<std::pair<u32,u32>> notifications;
    u32 mix_index=0,mix_frac=0;bool mix_on=false;   // output mixer cursor (frames)
};
struct Host {
    std::map<u32,std::unique_ptr<Buffer>> objects;std::map<u32,bool> events;
    std::vector<th10::browser::Audio*> owners;
    u32 next=1,next_event=1,pumps=0,signals=0,error=0;uint64_t millis=1000;bool paused=false;
    Buffer* get(u32 id){auto it=objects.find(id);return it==objects.end()?nullptr:it->second.get();}
    void destroy(){objects.clear();events.clear();
#if TH10_SE_HOOKS
        se_resident.clear();
#endif
    }
#if TH10_SE_HOOKS
    // ---- sound effects in the ME eDRAM ----
    // The PCM object of a stored effect source (shared with its duplicates) -> handle.
    std::map<const void*,u32> se_resident;
    u32 se_freed=0;   // Main-RAM bytes given back (vector capacities)
    // An effect source: a secondary buffer with volume control and no position
    // notifications (the game's 37 sources, flags 0x80c8; not the primary
    // 0x8008 nor the BGM streams 0x18188|...).
    static bool se_candidate(const Buffer& b){return !b.device&&!(b.flags&1u)&&(b.flags&0x80u)&&!(b.flags&0x100u)&&b.notifications.empty();}
    void se_store(Buffer& b){
        if(!se_candidate(b)||!b.pcm||b.pcm->empty()||b.pcm.use_count()!=1||se_resident.count(b.pcm.get())||!sound(b))return;
        const u32 h=th10_se_store(b.pcm->data(),u32(b.pcm->size()),read16(b.format.data()+14)==8?128u:0u);
        if(!h)return;   // no ME, no room or a failed read-back: it stays in Main RAM
        se_resident[b.pcm.get()]=h;se_freed+=u32(b.pcm->capacity());
        PcmBytes().swap(*b.pcm);   // the Main-RAM copy goes (heap or volatile arena); b.length keeps its size
    }
    void se_voice(const Buffer& b,th10::mix::Voice& v){
        if(se_resident.empty())return;
        const auto it=se_resident.find(b.pcm.get());if(it!=se_resident.end())th10_se_voice(it->second,&v);
    }
    void se_restore(Buffer& b){   // never expected: effect sources are locked once, while loading
        if(!b.pcm||se_resident.empty())return;
        const auto it=se_resident.find(b.pcm.get());if(it==se_resident.end())return;
        b.pcm->resize(b.length);
        if(!th10_se_restore(it->second,b.pcm->data(),b.length))std::fill(b.pcm->begin(),b.pcm->end(),u8(read16(b.format.data()+14)==8?128:0));
        se_resident.erase(it);th10_audio_written(b.pcm->data(),u32(b.pcm->size()));
    }
    void se_release(Buffer& b){   // the last reference to stored PCM: its eDRAM blocks are free again
        if(!b.pcm||b.pcm.use_count()!=1||se_resident.empty())return;
        const auto it=se_resident.find(b.pcm.get());if(it==se_resident.end())return;
        th10_se_release(it->second);se_resident.erase(it);
    }
#endif
    u32 position(Buffer& b){
        if(!b.playing)return b.cursor;
        const auto align=std::max(1u,read16(b.format.data()+12));
        const uint64_t pos=b.cursor+((millis-b.started)*b.frequency/1000)*align;
        if(b.loop)return u32(pos%b.length);
        if(pos>=b.length){b.playing=false;return b.cursor=0;}return u32(pos);
    }
    bool sound(Buffer& b){
        if(b.flags&1)return true;
        const auto channels=read16(b.format.data()+2),align=read16(b.format.data()+12),bits=read16(b.format.data()+14);
        return read16(b.format.data())==1&&align&&channels&&(bits==8||bits==16);
    }
    u32 buffer(u32 flags,u32 length,const u8* format,std::shared_ptr<PcmBytes> shared={}){
        auto b=std::make_unique<Buffer>();b->flags=flags;b->length=length?length:4096;
        if(format)std::memcpy(b->format.data(),format,18);b->frequency=read32(b->format.data()+4);
        b->pcm=shared?shared:std::make_shared<PcmBytes>(b->length,read16(b->format.data()+14)==8?128:0);
        th10_audio_written(b->pcm->data(),u32(b->pcm->size()));
        const auto id=next++;objects[id]=std::move(b);return id;
    }
    bool notify(Buffer& b){
        const auto pos=position(b),last=b.last_cursor;bool signaled=false;
        for(const auto& note:b.notifications)if((pos>=last&&note.first>=last&&note.first<pos)||(pos<last&&(note.first>=last||note.first<pos))){
            auto it=events.find(note.second);if(it!=events.end()){it->second=true;signaled=true;}
        }
        b.last_cursor=pos;return signaled;
    }
    // ---- output mixer ----
    u32 mix_skipped=0,mix_resyncs=0,mix_jobs=0;
    static u32 align_of(const Buffer& b){return std::max(1u,read16(b.format.data()+12));}
    // Model position in bytes without the end-of-buffer side effect of position().
    u32 model_bytes(const Buffer& b)const{
        if(!b.playing)return b.cursor;
        const uint64_t pos=b.cursor+((millis-b.started)*b.frequency/1000)*align_of(b);
        return b.loop&&b.length?u32(pos%b.length):u32(std::min<uint64_t>(pos,b.length));
    }
    void mix_sync(Buffer& b){b.mix_index=model_bytes(b)/align_of(b);b.mix_frac=0;b.mix_on=true;}
    static u32 gain_q15(i32 hundredths_db){
        if(hundredths_db>=0)return 32768;if(hundredths_db<=-10000)return 0;
        return u32(32768.0f*std::pow(10.0f,float(hundredths_db)/2000.0f));
    }
    // Render [millis, millis + ms): what the buffers play under the model
    // before the game's next step changes them.
    void render(u32 ms){
#ifdef TH10_AUDIO_OUT
        const uint64_t f0=millis*th10::mix::rate/1000,f1=(millis+ms)*th10::mix::rate/1000;
        if(paused)return;
        // Long slices (real-time audio clock on slow ticks) go out as several jobs.
        for(uint64_t left=f1-f0,chunk=0;left;chunk++){const u32 frames=u32(std::min<uint64_t>(left,th10::mix::max_frames));left-=frames;render_chunk(ms,frames,chunk==0);}
#else
        (void)ms;
#endif
    }
    void render_chunk(u32 ms,u32 frames,bool first){
#ifdef TH10_AUDIO_OUT
#ifndef __PSP__
        static const bool debug=std::getenv("TH10_AUDIO_DEBUG")!=nullptr;
        if(debug&&millis/1000!=(millis+ms)/1000){std::fprintf(stderr,"t=%llu ms:",(unsigned long long)millis);
            for(auto& e:objects){const auto& b=*e.second;if(b.device||!b.playing)continue;u32 energy=0;if(b.pcm)for(size_t k=0;k<b.pcm->size();k+=64)energy+=(*b.pcm)[k];
                std::fprintf(stderr," [%u%s%s len=%u f=%u vol=%d pan=%d n=%zu fl=%x mix=%u on=%d energy=%u fmt=%u/%u/%u]",e.first,b.playing?" P":"",b.loop?"L":"",b.length,b.frequency,b.volume,b.pan,b.notifications.size(),b.flags,b.mix_index,b.mix_on,energy,
                    read16(b.format.data()),read16(b.format.data()+2),read16(b.format.data()+14));}
            std::fprintf(stderr,"\n");}
#endif
        auto* job=th10_audio_acquire();if(!job){++mix_skipped;for(auto& e:objects)e.second->mix_on=false;return;}
        job->frames=frames;job->voices=0;u32 staged=0;
        for(auto& entry:objects){auto& b=*entry.second;
            if(!b.playing||b.device||(b.flags&1)||!b.pcm||!sound(b)){b.mix_on=false;continue;}
            const u32 align=align_of(b),channels=read16(b.format.data()+2),bits=read16(b.format.data()+14),total=b.length/align;
            if(!total||align!=channels*(bits/8))continue;
            if(!b.mix_on)mix_sync(b);
            else if(first){const u32 model=model_bytes(b)/align,d=model>b.mix_index?model-b.mix_index:b.mix_index-model;
                if(std::min(d,total-std::min(d,total))>4096){mix_sync(b);++mix_resyncs;}}   // drift guard
            if(job->voices>=th10::mix::max_voices)continue;
            const u32 step=u32((uint64_t(b.frequency)<<16)/th10::mix::rate);
            auto& v=job->voice[job->voices];v.channels=channels;v.bits=bits;v.step=step;v.frac=b.mix_frac;v.reserved=0;
            i32 left=b.volume,right=b.volume;if(b.pan>0)left-=b.pan;else if(b.pan<0)right+=b.pan;
            v.gain_l=gain_q15(left);v.gain_r=gain_q15(right);
            const u32 span=u32((uint64_t(b.mix_frac)+uint64_t(frames)*step)>>16);
            if(!b.notifications.empty()){
                // Streams are refilled by the game: mix a private copy of the frames to play.
                const u32 need=span+2,bytes=need*align;
                if(staged+bytes>th10::mix::staging_bytes){++mix_skipped;continue;}
                u8* dst=job->staging+staged;u32 at=b.mix_index;
                for(u32 k=0;k<need;k++){if(at>=total){if(!b.loop){std::memset(dst+k*align,bits==8?128:0,size_t(need-k)*align);break;}at=0;}
                    std::memcpy(dst+k*align,b.pcm->data()+size_t(at)*align,align);++at;}
                v.source=address(dst);v.frames=need;v.loop=0;v.index=0;v.invalidate=0;staged+=(bytes+63u)&~63u;
            }else{v.source=address(b.pcm->data());v.frames=total;v.loop=b.loop;v.index=b.mix_index;v.invalidate=1;
#if TH10_SE_HOOKS
                se_voice(b,v);   // stored in eDRAM: source = its eDRAM offset
#endif
            }
            ++job->voices;
            const uint64_t next=uint64_t(b.mix_index)+span;b.mix_frac=u32((uint64_t(b.mix_frac)+uint64_t(frames)*step)&0xffffu);
            if(next>=total){if(b.loop)b.mix_index=u32(next%total);else{b.mix_index=total;b.mix_on=false;}}else b.mix_index=u32(next);
        }
        ++mix_jobs;th10_audio_submit(job);
#else
        (void)ms;(void)frames;(void)first;
#endif
    }
    // One notification pass per host frame stands in for the mixer slices of
    // the SDL host: positions come from the same millisecond clock.
    void pump(){
        if(paused)return;++pumps;bool signaled=false;
        for(auto& entry:objects){auto& b=*entry.second;if(b.playing&&!b.notifications.empty())signaled=notify(b)||signaled;}
        if(signaled){++signals;for(auto* owner:owners)owner->pump();}
    }
}host;
}
#if TH10_SE_HOOKS && !defined(__PSP__)
// ---- PC simulation of the ME eDRAM path (TH10_SE_EDRAM_SIM) ----
// The SeEdram.hpp code of the PSP: Host for the SC side, run_job/prepass for
// the ME side, with a host array as the eDRAM and the ME's work done in
// line (copy jobs at once, the prepass in th10_audio_submit just before
// mix_job, after which eDRAM offsets become stand-in addresses: what the ME's
// kseg0 alias does). Game time (host.millis) is the clock.
// TH10_SE_SIM_FLIP=<ms>:<offset>:<bit> flips one stand-in bit once that time
// has passed (the safety-net test: that block must be muted at its next due check).
namespace {
alignas(64) unsigned char sim_edram[th10::se::area_bytes];
th10::se::Ctl sim_ctl;th10::se::Block sim_blocks[th10::se::max_blocks];
struct SimMe {
    static volatile u32* edram(u32 offset){return reinterpret_cast<volatile u32*>(sim_edram+offset);}
    static void edram_drop(u32,u32){}
    static const u32* ram_words(u32 address,u32){return reinterpret_cast<const u32*>(uintptr_t(address));}
    static volatile u32* ram_uc(u32 address){return reinterpret_cast<volatile u32*>(uintptr_t(address));}
    static bool ram_ok(u32 address,u32){return address!=0;}
    static volatile th10::se::Block* table(u32 base){return reinterpret_cast<volatile th10::se::Block*>(uintptr_t(base));}
    static u32 count(){return 0;}
    static void sync(){__sync_synchronize();}
};
struct SimPc {
    static bool run(volatile th10::se::Ctl* c){c->submitted=c->submitted+1u;th10::se::run_job<SimMe>(c);return c->completed==c->submitted;}
    static void writeback(const void*,u32){}
    static void wbinv(void*,u32){}
    static u32 now_ms(){return u32(host.millis);}
    static u32 now_us(){return 0;}
};
th10::se::Host<SimPc> sim_host;bool sim_ready=false,sim_flipped=false;
void sim_init(){if(sim_ready)return;sim_ready=true;sim_host.attach(&sim_ctl,sim_blocks);sim_host.enable(u32(uintptr_t(sim_blocks)),TH10_SE_EDRAM_CHECK_MS,TH10_SE_EDRAM_REFRESH_MS);}
void sim_mix_prepare(th10::mix::Job& job){
    sim_init();sim_ctl.now_ms=u32(host.millis);
    if(!sim_flipped)if(const char* f=std::getenv("TH10_SE_SIM_FLIP")){unsigned ms=0,off=0,bit=0;
        if(std::sscanf(f,"%u:%u:%u",&ms,&off,&bit)==3&&host.millis>=ms&&off<th10::se::area_bytes){sim_edram[off]^=u8(1u<<(bit&7u));sim_flipped=true;
            std::fprintf(stderr,"SE_EDRAM_SIM flip t=%llu ms offset=0x%06x bit=%u (block %u)\n",(unsigned long long)host.millis,off,bit&7u,off>>th10::se::block_shift);}}
    (void)th10::se::prepass<SimMe>(job,&sim_ctl);
    for(u32 v=0;v<job.voices&&v<th10::mix::max_voices;v++){auto& s=job.voice[v];if(s.reserved&th10::se::VoiceEdram)s.source=u32(uintptr_t(sim_edram))+s.source;}
}
struct SimOut {~SimOut(){if(!sim_ready)return;const auto& h=sim_host;
    std::fprintf(stderr,"SE_EDRAM_SIM stored=%u live=%u bytes=%u peak=%u blocks=%u peak=%u/%u main_ram_freed=%u fail space=%u copy=%u args=%u checks=%u mismatches=%u muted=%u restores=%u releases=%u\n",
        h.stored,h.live,h.live_bytes,h.peak_bytes,h.live_blocks,h.peak_blocks,th10::se::max_blocks,host.se_freed,h.fail_space,h.fail_copy,h.fail_args,u32(sim_ctl.checks),u32(sim_ctl.mismatches),u32(sim_ctl.muted),h.restores,h.releases);}} sim_out;
}
extern "C" unsigned th10_se_store(const void* pcm,unsigned bytes,unsigned fill){sim_init();return sim_host.store(pcm,bytes,fill);}
extern "C" int th10_se_voice(unsigned handle,th10::mix::Voice* voice){return sim_host.voice(handle,*voice)?1:0;}
extern "C" int th10_se_restore(unsigned handle,void* dst,unsigned bytes){return sim_host.restore(handle,dst,bytes)?1:0;}
extern "C" void th10_se_release(unsigned handle){sim_host.release(handle);}
extern "C" void th10_se_stats(char* out,unsigned size){std::snprintf(out,size,"se_edram sim stored=%u",sim_host.stored);}
#endif
// ---- PC output: TH10_AUDIO_WAV=<file> writes the mixed stream (44.1 kHz s16 stereo) ----
#if defined(TH10_AUDIO_OUT)&&!defined(__PSP__)
namespace {
struct WavOut {FILE* f=nullptr;u32 bytes=0;bool tried=false;th10::mix::Job job{};
    bool open(){if(!tried){tried=true;if(const char* path=std::getenv("TH10_AUDIO_WAV"))if((f=std::fopen(path,"wb"))){u8 header[44]{};std::fwrite(header,1,44,f);}}return f;}
    void close(){if(!f)return;const u32 rate=th10::mix::rate;u8 h[44];std::memcpy(h,"RIFF",4);const u32 riff=36+bytes;std::memcpy(h+4,&riff,4);std::memcpy(h+8,"WAVEfmt ",8);
        const u32 fmt=16;const uint16_t pcm=1,channels=2,align=4,bits=16;const u32 avg=rate*4;std::memcpy(h+16,&fmt,4);std::memcpy(h+20,&pcm,2);std::memcpy(h+22,&channels,2);
        std::memcpy(h+24,&rate,4);std::memcpy(h+28,&avg,4);std::memcpy(h+32,&align,2);std::memcpy(h+34,&bits,2);std::memcpy(h+36,"data",4);std::memcpy(h+40,&bytes,4);
        std::fseek(f,0,SEEK_SET);std::fwrite(h,1,44,f);std::fclose(f);f=nullptr;}
    ~WavOut(){close();}
}wav;
}
extern "C" th10::mix::Job* th10_audio_acquire(void){return wav.open()?&wav.job:nullptr;}
extern "C" void th10_audio_submit(th10::mix::Job* job){
#if TH10_SE_HOOKS
    sim_mix_prepare(*job);
#endif
    th10::mix::mix_job(*job,0,nullptr);job->done=job->serial;
    const u32 n=job->frames*4;std::fwrite(job->out,1,n,wav.f);wav.bytes+=n;}
extern "C" void th10_audio_written(const void*,std::uint32_t){}
#elif !defined(TH10_AUDIO_OUT)
extern "C" void th10_audio_written(const void*,std::uint32_t){}
#endif
extern "C" {
u32 audio_host_create(){auto b=std::make_unique<Buffer>();b->device=true;const auto id=host.next++;host.objects[id]=std::move(b);return id;}
i32 audio_host_call(u32 id,u32 op,const u32* a){
    auto* p=host.get(id);if(!p)return i32(0x80070057);auto& b=*p;
    using O=th10::browser::AudioOperation;
    switch(static_cast<O>(op)){
    case O::AddRef:return ++b.refs;
    case O::Release:if(--b.refs)return b.refs;
#if TH10_SE_HOOKS
        host.se_release(b);
#endif
        host.objects.erase(id);return 0;
    case O::CreateBuffer:{auto* desc=ptr(a[0]);*ptr(a[1])=host.buffer(desc[1],desc[2],ptr<u8>(desc[4]));break;}
    case O::Duplicate:{auto* src=host.get(a[0]);if(!src)return -1;const auto copy=host.buffer(src->flags,src->length,src->format.data(),src->pcm);auto& dest=*host.get(copy);dest.frequency=src->frequency;dest.volume=src->volume;dest.pan=src->pan;*ptr(a[1])=copy;break;}
    case O::Cooperative:case O::Restore:break;
    case O::Unlock:th10_audio_written(b.pcm?b.pcm->data():nullptr,b.pcm?u32(b.pcm->size()):0);
#if TH10_SE_HOOKS
        host.se_store(b);   // a finished effect source: its PCM moves to the ME eDRAM
#endif
        break;
    case O::Format:std::memcpy(b.format.data(),ptr(a[0]),18);b.frequency=read32(b.format.data()+4);break;
    case O::Status:host.position(b);*ptr(a[0])=b.playing?(b.loop?5:1):0;break;
    case O::Volume:b.volume=i32(a[0]);break;
    case O::Pan:b.pan=i32(a[0]);break;
    case O::Frequency:b.cursor=host.position(b);b.started=host.millis;b.frequency=a[0]?a[0]:read32(b.format.data()+4);b.mix_on=false;break;
    case O::GetVolume:*ptr(a[0])=u32(b.volume);break;
    case O::GetPan:*ptr(a[0])=u32(b.pan);break;
    case O::GetFrequency:*ptr(a[0])=b.frequency;break;
    case O::Play:
        if(!host.sound(b)){host.error=5;return -1;}
        if(b.playing)b.cursor=host.position(b);b.playing=true;b.loop=!!(a[2]&1);b.started=host.millis;b.last_cursor=b.cursor;b.mix_on=false;break;
    case O::Stop:b.cursor=host.position(b);b.playing=false;b.mix_on=false;break;
    case O::Position:b.cursor=a[0]%b.length;b.started=host.millis;b.mix_on=false;break;
    case O::CurrentPosition:{const auto pos=host.position(b);if(a[0])*ptr(a[0])=pos;if(a[1])*ptr(a[1])=(pos+2048)%b.length;break;}
    case O::Lock:{
#if TH10_SE_HOOKS
        host.se_restore(b);
#endif
        const auto offset=a[0]%b.length,len=a[6]&2?b.length:a[1],first=std::min(len,b.length-offset);if(len>b.length)return -1;
        *ptr(a[2])=address(b.pcm->data()+offset);*ptr(a[3])=first;if(a[4])*ptr(a[4])=len>first?address(b.pcm->data()):0;if(a[5])*ptr(a[5])=len-first;break;}
    case O::Query:++b.refs;*ptr(a[1])=id;break;
    case O::Notifications:{auto* notes=ptr(a[1]);b.notifications.clear();for(u32 i=0;i<a[0];i++)b.notifications.emplace_back(notes[i*2],notes[i*2+1]);break;}
    case O::GetFormat:if(a[2])*ptr(a[2])=18;if(a[0])std::memcpy(ptr(a[0]),b.format.data(),std::min(a[1],18u));break;
    case O::Caps:{u32 caps[]{20,b.flags,b.length,0,0};std::memcpy(ptr(a[0]),caps,sizeof(caps));break;}
    default:return -1;
    }return 0;
}
u32 audio_host_event(u32 op,u32 id){
    if(op==0){const auto next=host.next_event++;host.events[next]=false;return next;}
    if(op==2){host.events.erase(id);return 0;}auto it=host.events.find(id);if(it==host.events.end())return ~0u;
    const bool signaled=it->second;it->second=false;return signaled?0:0x102;
}
void audio_host_advance(u32 ms){host.render(ms);host.millis+=ms;}
void audio_host_attach(th10::browser::Audio* owner,bool attach){
    if(attach)host.owners.push_back(owner);
    else host.owners.erase(std::remove(host.owners.begin(),host.owners.end(),owner),host.owners.end());
}
void sdl_audio_pump(){host.pump();}
void sdl_audio_pause(u32 paused){host.paused=paused!=0;}
void sdl_audio_shutdown(){host.destroy();}
u32 headless_audio_signals(){return host.signals;}
void headless_audio_mix_stats(u32* out){out[0]=host.mix_jobs;out[1]=host.mix_skipped;out[2]=host.mix_resyncs;}
}
