// PC check of the TH10 recorder core (TH10_REC): native/psp/RecCore.hpp (AVI
// layout, downmix, writer with idx1 spill and split, slot muxer, repair) and
// native/psp/RecJpeg.hpp (the encoder the ME runs), through the same template
// code the PSP writer thread runs, with a stdio file system that can inject
// short writes. Writes AVI files that native/tools/rec_check.py validates
// (ffprobe/ffmpeg decode, RIFF/idx1 walk, byte comparison with the XMB-proven
// B0 writer of th10_rec/tools/make_avitest.py, A/V marker sync).
//   cmake --build <dir> --target th10_rec_check && <dir>/th10_rec_check <outdir> [frames_565.raw]
//   python3 native/tools/rec_check.py <outdir>
// frames_565.raw: 480x272 RGB565 frames (the PPSSPP dump of the G0 study);
// without it the pictures are synthetic.
#include "../psp/RecCore.hpp"
#include "../psp/RecJpeg.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
using namespace th10::rec;
using avi::u8;using avi::u16;using avi::u32;using avi::u64;using avi::i16;using avi::i32;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

// ---- stdio file system (PSP: sceIo in psp/Recorder.cpp) ----
static unsigned rng_state=12345;static unsigned rnd(){rng_state=rng_state*1103515245u+12345u;return rng_state>>8;}
static bool short_writes=false;static u32 short_count=0;
struct PosixFs {
    static int open_write(const char* p){return ::open(p,O_WRONLY|O_CREAT|O_TRUNC,0644);}
    static int open_rw(const char* p){return ::open(p,O_RDWR);}
    static int open_read(const char* p){return ::open(p,O_RDONLY);}
    static int write(int f,const void* d,u32 n){u32 m=n;if(short_writes&&n>16&&rnd()%4==0){m=1+rnd()%(n-1);++short_count;}const ssize_t r=::write(f,d,m);return int(r);}
    static int read(int f,void* d,u32 n){return int(::read(f,d,n));}
    static long long seek(int f,u64 pos){return ::lseek(f,off_t(pos),SEEK_SET);}
    static long long size(int f){struct stat s;return fstat(f,&s)==0?s.st_size:-1;}
    static int close(int f){return ::close(f);}
    static int remove(const char* p){return ::unlink(p);}
    static u64 now_us(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return u64(t.tv_sec)*1000000u+u64(t.tv_nsec)/1000u;}
    static void before_write(const void*,u32){}
};

// ---- pictures ----
constexpr u32 W=480,H=272,FW=W*H;
static std::vector<u16> real_frames;static u32 real_count=0;
static void picture(u32 k,bool marker,u16* out){
    if(real_count){std::memcpy(out,real_frames.data()+size_t((834u+2u*k)%real_count)*FW,FW*2);}   // stage-4 demo from dump frame 834, every 2nd (30 -> 15 fps)
    else for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++){const u32 r=((x+4*k)>>4)&31u,g=((y+2*k)>>3)&63u,b=((x^y)>>4)&31u;out[y*W+x]=u16((b<<11)|(g<<5)|r);}   // GE 5650: red in the low bits
    // marker frames (A/V sync check): a white box over the picture's top-left quarter; others dark there
    for(u32 y=0;y<64;y++)for(u32 x=0;x<64;x++)out[y*W+x]=marker?0xffffu:0x0000u;
}
static jpeg::Tables tables;
static std::vector<u8> encode(const u16* frame){
    std::vector<u8> out(128u*1024u);std::vector<u16> scratch(16*320);jpeg::State s;
    const u32 n=jpeg::encode_frame(tables,frame,W,out.data(),u32(out.size()),15,scratch.data(),s);
    CHECK(n>0);out.resize(n);return out;
}

// ---- audio: 44.1 kHz stereo blocks of 1024 frames (MeAudio's block) -> downmix ----
// A 1 kHz beep of 441 mono samples starts at mono sample spf*k for every
// marker slot k; quiet noise elsewhere (so silence is never mistaken for it).
static std::vector<i16> make_audio(u32 slots,u32 spf,u32 marker_every){
    const u32 mono_total=slots*spf+2048,stereo_frames=(mono_total*2+1023)/1024*1024;
    std::vector<i16> stereo(size_t(stereo_frames)*2);
    for(u32 f=0;f<stereo_frames;f++){
        const u32 m=f/2,k=m/spf;const bool beep=(k%marker_every==0)&&(m-k*spf)<441u;
        const double t=f/44100.0;
        const int v=beep?int(16000*std::sin(2*M_PI*1000*t)):int(rnd()%201)-100;
        stereo[2*f]=i16(v);stereo[2*f+1]=i16(beep?v:-v/2);
    }
    std::vector<i16> mono(stereo_frames/2);
    for(u32 b=0;b<stereo_frames/1024;b++)avi::downmix(stereo.data()+b*2048,1024,mono.data()+b*512);
    // the formula, independently
    for(u32 i=0;i<mono.size();i++){const i32 s=i32(stereo[4*i])+stereo[4*i+1]+stereo[4*i+2]+stereo[4*i+3];CHECK(mono[i]==i16(std::floor((s+2)/4.0)));}
    return mono;
}

// ---- the record source (PSP: the game thread's record ring + audio ring) ----
struct Rec {u32 slot,state,handle;int ready_after;};   // ready_after: steps until a pending record is done (-1: stays)
struct TestSrc {
    std::vector<i16> audio;u64 written=0;
    std::deque<Rec> recs;std::map<u32,std::vector<u8>> jpegs;std::map<u32,int> released;u32 next_handle=1;
    std::deque<u32> encoded;   // pictures in capture order (the PSP encodes one at a time, in that order, into a FIFO ring): releases must come in this order
    u64 audio_written(){return written;}
    void audio_read(u64 at,i16* out,u32 n){for(u32 i=0;i<n;i++)out[i]=at+i<audio.size()?audio[size_t(at+i)]:0;}
    u32 records(){return u32(recs.size());}
    u32 record_slot(u32 i){return recs[i].slot;}
    u32 record_state(u32 i){return recs[i].state;}
    u32 record_handle(u32 i){return recs[i].handle;}
    u32 record_tick(u32){return 0;}void note_frame(u64,u32){}
    u32 record_bytes(u32 i){return u32(jpegs[recs[i].handle].size());}
    bool copy(u32 h,u32 off,u8* dst,u32 n){auto it=jpegs.find(h);CHECK(it!=jpegs.end()&&off+n<=it->second.size());if(it==jpegs.end())return false;std::memcpy(dst,it->second.data()+off,n);return true;}
    void pop(){recs.pop_front();}
    void release(u32 h){released[h]++;CHECK(released[h]==1);const bool fifo=!encoded.empty()&&encoded.front()==h;CHECK(fifo);if(fifo){encoded.pop_front();}if(released[h]==1){jpegs.erase(h);}}
    u32 add(u32 slot,u32 state,std::vector<u8> j,int ready_after=0){const u32 h=next_handle++;recs.push_back({slot,state,h,ready_after});if(!j.empty()){jpegs[h]=std::move(j);}
        if(state!=avi::RecFailed){encoded.push_back(h);}return h;}
    void age(){for(auto& r:recs){if(r.state==avi::RecPending&&r.ready_after>0&&--r.ready_after==0)r.state=avi::RecDone;}}
};
using Writer=avi::Writer<PosixFs>;using Mux=avi::Mux<PosixFs,TestSrc>;

static bool save(const std::string& path,const void* p,size_t n){FILE* f=std::fopen(path.c_str(),"wb");if(!f)return false;std::fwrite(p,1,n,f);std::fclose(f);return true;}
struct Session {std::vector<std::string> files;avi::MuxStats st;avi::WriteStats ws;};

// Runs a scripted session: `plan(slot)` says what the capture side produced
// for each slot. The audio grows by 512 samples per call (one played block).
enum Plan {Normal,NoFrame,Failed,Twice,Late,Pending};
template<class F> static Session run(const std::string& out,const char* name,u32 slots,u32 fps,bool empty,u64 split,u32 index_entries,F plan,u32 marker_every,bool crash=false,u32 crash_slot=0){
    Session s;const u32 spf=22050/fps;
    static std::vector<u8> buf0(65536),buf1(65536),index;index.assign(size_t(index_entries)*16u,0);
    Writer w;w.setup(buf0.data(),buf1.data(),65536,index.data(),u32(index.size()),&s.ws);
    TestSrc src;src.audio=make_audio(slots+4,spf,marker_every);
    Mux m;m.begin(&w,&src,spf,empty);m.split_bytes=split;
    u32 part=1;auto open_next=[&](){char p[256],sp[256];std::snprintf(p,sizeof(p),"%s/%s_%u.avi",out.c_str(),name,part);std::snprintf(sp,sizeof(sp),"%s/%s.idx.tmp",out.c_str(),name);
        CHECK(w.open(p,sp,avi::Params{W,H,fps}));s.files.push_back(p);++part;};
    open_next();
    std::vector<u16> frame(FW);u32 made=0;
    const u64 audio_end=u64(slots)*spf;
    for(u32 iter=0;iter<200000;iter++){
        // capture side: records for slots whose time has come (one slot ahead of the audio, like a present)
        while(made<slots&&u64(made)*spf<=src.written){
            const Plan p=plan(made);picture(made,made%marker_every==0,frame.data());
            if(p==Normal)src.add(made,avi::RecDone,encode(frame.data()));
            else if(p==Failed)src.add(made,avi::RecFailed,{});
            else if(p==Twice){std::vector<u16> other(FW);picture(made+1000,false,other.data());src.add(made,avi::RecDone,encode(other.data()));src.add(made,avi::RecDone,encode(frame.data()));}
            else if(p==Late&&made>0){src.add(made-1,avi::RecDone,encode(frame.data()));}   // stamped one slot early (the race in Recorder.cpp)
            else if(p==Pending)src.add(made,avi::RecPending,encode(frame.data()),3);
            ++made;
        }
        src.written=std::min<u64>(src.written+512,audio_end);
        src.age();
        for(;;){
            const avi::Step st=m.step(slots);
            if(st==avi::StepEmitted||st==avi::StepSkipped){if(crash&&m.next_slot==crash_slot)break;continue;}
            if(st==avi::StepRoom){CHECK(w.write_one());continue;}
            if(st==avi::StepSplit){CHECK(w.finalize());open_next();continue;}
            break;
        }
        while(w.full())CHECK(w.write_one());
        if(crash&&m.next_slot>=crash_slot)break;
        if(m.next_slot>=slots)break;
    }
    if(crash){while(w.full())CHECK(w.write_one());CHECK(w.write_all(w.bufs[w.head],w.fill[w.head]));PosixFs::close(w.file);w.file=-1;}   // power lost: no idx1, placeholder header
    else{CHECK(w.finalize());m.end();
        for(auto& kv:src.jpegs)if(!src.released.count(kv.first)){
            bool queued=false;for(auto& r:src.recs)if(r.handle==kv.first)queued=true;
            CHECK(queued);}   // every picture the muxer took was released exactly once (release() checks "once")
    }
    s.st=m.st;
    save(out+"/"+name+".pcm",src.audio.data(),src.audio.size()*2);   // the mono track the slots took their audio from
    std::printf("%s: files=%zu slots=%llu real=%u repeats=%u empty=%u superseded=%u failed=%u lead=%u audio=%llu | writes=%u bytes=%llu short_writes(injected)=%u spills=%u errors=%u\n",
        name,s.files.size(),(unsigned long long)m.st.slots,m.st.real,m.st.repeats,m.st.empty,m.st.superseded,m.st.failed,m.st.lead,(unsigned long long)m.st.audio_samples,
        s.ws.writes,(unsigned long long)s.ws.bytes,short_count,s.ws.spills,s.ws.errors);
    // expectations file for rec_check.py
    std::string e;char line[256];
    for(auto& f:s.files){std::snprintf(line,sizeof(line),"file %s\n",f.c_str());e+=line;}
    std::snprintf(line,sizeof(line),"slots %llu real %u repeats %u empty %u lead %u fps %u spf %u marker %u crash %d\n",(unsigned long long)m.st.slots,m.st.real,m.st.repeats,m.st.empty,m.st.lead,fps,spf,marker_every,crash?1:0);e+=line;
    save(out+"/"+name+".expect",e.data(),e.size());
    return s;
}

int main(int argc,char** argv){
    if(argc<2){std::printf("usage: th10_rec_check <outdir> [frames_565.raw]\n");return 64;}
    const std::string out=argv[1];
    if(argc>2){FILE* f=std::fopen(argv[2],"rb");if(f){std::fseek(f,0,SEEK_END);const long n=std::ftell(f);std::fseek(f,0,SEEK_SET);real_count=u32(n/(FW*2));real_frames.resize(size_t(real_count)*FW);
        if(std::fread(real_frames.data(),FW*2,real_count,f)!=real_count){real_count=0;}std::fclose(f);}}
    std::printf("pictures: %s\n",real_count?"real game frames (PPSSPP dump)":"synthetic");
    jpeg::build_tables(tables,50,W,H,2,true);

    // header layout: placeholder parses back, finished header does not
    {u8 h[avi::movi_start];avi::Params p{W,H,15},q;avi::header(h,p,avi::Totals{},false);CHECK(avi::unfinished(h));CHECK(avi::parse_header(h,q)&&q.width==W&&q.height==H&&q.fps==15);
     avi::Totals t;t.frames=10;t.audio_samples=14700;t.video_max=30000;t.audio_max=2940;t.movi_bytes=320000;t.entries=20;avi::header(h,p,t,true);CHECK(!avi::unfinished(h));CHECK(!avi::parse_header(h,q));}

    // A: every slot has its picture (the B0 comparison file), real-time stamps
    {   const u32 slots=60,spf=1470;std::vector<u8> jl;
        Session s=run(out,"a_b0",slots,15,false,1000ull<<20,4096,[](u32){return Normal;},15);
        CHECK(s.files.size()==1&&s.st.real==slots&&s.st.repeats==0&&s.st.lead==0);
        // the chunks rec_check.py feeds to the Python B0 writer
        std::vector<u16> frame(FW);for(u32 k=0;k<slots;k++){picture(k,k%15==0,frame.data());auto j=encode(frame.data());u32 n=u32(j.size());jl.insert(jl.end(),reinterpret_cast<u8*>(&n),reinterpret_cast<u8*>(&n)+4);jl.insert(jl.end(),j.begin(),j.end());}
        save(out+"/a_b0.jpegs",jl.data(),jl.size());(void)spf;
    }
    // B: drops, failures, two pictures in one slot, late stamps, slow encodes, file split, idx1 spill (64 entries in RAM), short writes
    {   short_writes=true;rng_state=777;
        Session s=run(out,"b_drops",420,15,false,3ull<<20,64,[](u32 k){
            if(k==0||k==1)return NoFrame;                 // lead-in: the file starts at the first picture
            if(k%10==3){return NoFrame;}if(k%17==5){return Failed;}if(k%23==7){return Twice;}if(k%29==11){return Late;}if(k%7==2){return Pending;}return Normal;},15);
        short_writes=false;
        CHECK(s.files.size()>=2);CHECK(s.st.lead==2);CHECK(s.st.repeats>0&&s.st.superseded>0&&s.ws.spills>0);
    }
    // C: TH10_REC_DROP_EMPTY=1 variant (zero-length '00dc' for a slot without a new picture)
    {   Session s=run(out,"c_empty",120,15,true,1000ull<<20,4096,[](u32 k){return (k%4==1)?NoFrame:Normal;},15);
        CHECK(s.st.empty==30&&s.st.repeats==0);
    }
    // D: 30 fps (spf 735)
    {   Session s=run(out,"d_30fps",90,30,false,1000ull<<20,4096,[](u32 k){return (k%3==2)?NoFrame:Normal;},30);
        CHECK(s.st.slots==90);
    }
    // E: crash (no finalize) then repair: whole pairs kept, idx1 written after them
    {   Session s=run(out,"e_crash",150,15,false,1000ull<<20,40,[](u32){return Normal;},15,true,97);
        CHECK(s.files.size()==1);
        const std::string f=s.files[0];std::vector<std::string> variants;
        // e1: as crashed; e2: cut inside a picture chunk; e3: cut after a '00dc' (its '01wb' missing); e4: a finished file (refused)
        auto copy=[&](const std::string& to,long long cut){FILE* a=std::fopen(f.c_str(),"rb");std::fseek(a,0,SEEK_END);long long n=std::ftell(a);std::fseek(a,0,SEEK_SET);
            std::vector<u8> d(static_cast<size_t>(n));if(std::fread(d.data(),1,d.size(),a)!=d.size()){}std::fclose(a);if(cut>0&&cut<n)d.resize(size_t(cut));save(to,d.data(),d.size());return d;};
        auto d=copy(out+"/e_crash_cut0.avi",0);
        // walk to find chunk positions in the crashed file
        std::vector<std::pair<u64,bool>> chunks;for(u64 at=avi::movi_start;at+8<=d.size();){const u32 n=avi::get32(&d[at+4]);chunks.push_back({at,avi::is_fourcc(&d[at],"00dc")});at+=8+n+(n&1);}
        CHECK(chunks.size()>=40);
        const u64 mid=chunks[chunks.size()-6].first+8+100;   // inside a chunk near the end
        u64 after_video=0;for(size_t i=chunks.size()-8;i<chunks.size();i++)if(chunks[i].second){after_video=chunks[i].first+8+avi::get32(&d[chunks[i].first+4]);after_video+=after_video&1;break;}
        copy(out+"/e_crash_cut1.avi",(long long)mid);copy(out+"/e_crash_cut2.avi",(long long)after_video);
        std::vector<u8> b0(65536),b1(65536),index(40*16);   // a small RAM index: the walk spills too
        avi::WriteStats ws;Writer w;w.setup(b0.data(),b1.data(),65536,index.data(),u32(index.size()),&ws);
        for(int v=0;v<3;v++){
            char p[256];std::snprintf(p,sizeof(p),"%s/e_crash_cut%d.avi",out.c_str(),v);
            avi::Repair<PosixFs> r;CHECK(r.begin(p,(out+"/e_repair.idx.tmp").c_str(),&w,b0.data()));
            u32 calls=0;while(r.walk(7))++calls;
            const bool ok=r.finish();CHECK(ok);
            std::printf("repair %s: frames=%u audio=%u pair_end=%llu walk_calls=%u spills=%u\n",p,r.t.frames,r.t.audio_samples,(unsigned long long)r.pair_end,calls,ws.spills);
            // a second pass refuses the now finished file
            avi::Repair<PosixFs> again;CHECK(!again.begin(p,(out+"/e_repair.idx.tmp").c_str(),&w,b0.data()));
        }
        // a foreign RIFF (the B0 file of A, finished) is refused too
        avi::Repair<PosixFs> foreign;CHECK(!foreign.begin((out+"/a_b0_1.avi").c_str(),(out+"/e_repair.idx.tmp").c_str(),&w,b0.data()));
        std::remove(f.c_str());
    }
    if(failures)std::printf("REC_CHECK FAILED %d\n",failures);else std::printf("REC_CHECK OK (now run rec_check.py on %s)\n",out.c_str());
    return failures?1:0;
}
