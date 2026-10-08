// PC simulation of the TH10 recorder (TH10_REC): the real psp/Recorder.cpp and
// the real ME kernel (psp/RecMeKernel.inl) compiled for a 32-bit PC against
// native/tools/rec_sim/psp_sim.h, driven by threads standing in for the PSP:
//   game  (this main thread): 59.94 Hz ticks, a present every 2nd tick (every
//         3rd in a slow window, none during a stall), the GE copy done ~5 ms
//         after its list is closed, SELECT presses from a script;
//   audio: one 1024-frame 44.1 kHz stereo block per 23.2 ms into
//          th10_rec_audio_block, quiet noise plus a 1 kHz beep at every whole
//          second (the same instants the game marks its picture with a white box);
//   ME:    runs th10::rec::me::run_one on the job ring, with a set delay per job;
//          an audio job counter moves now and then (audio_waits accounting);
//   writer: Recorder.cpp's own thread.
// Not simulated: the PSP's single core and priorities, the cache (the aliases
// are 0, sync is a fence), GE timing, the XMB. It checks the plumbing (slots,
// records, buffers, ME jobs, muxer, files, stop/split/full/repair) end to end;
// native/tools/rec_sim_check.py checks the files (ffprobe/ffmpeg, idx1, marker
// pictures against beeps).
//   build: see native/tools/rec_sim_check.py (g++ -m32 ... -I native/tools/rec_sim)
//   env: REC_SIM_ROOT (dir), REC_SIM_SCALE (time x, 4), REC_SIM_TICKS, REC_SIM_ME (1/0),
//        REC_SIM_JOB_US (ME us per job), REC_SIM_PRESS ("120,1500,..." SELECT press ticks),
//        REC_SIM_STALL ("tick,ms"), REC_SIM_SLOW ("from,to"), REC_SIM_FREE_MB, REC_SIM_CRASH (tick),
//        REC_SIM_FRAMES (480x272 RGB565 raw file)
#define TH10_REC_UNCACHED 0u
#define TH10_REC_KSEG0 0u
#define TH10_REC_RAM_LO 0u
#define TH10_REC_RAM_HI 0xffffffffu
#define TH10_REC_SYNC() __sync_synchronize()
#define TH10_REC_ME_SYNC() __sync_synchronize()
#define TH10_REC_ME_COUNT(v) ((v)=sim_me_count())
#define TH10_REC_EDRAM_BASE sim_edram_base()   // a 4 MiB host array stands in for the ME eDRAM (only the ME side touches it)
#include <cstdint>
static uint32_t sim_me_count();
alignas(64) static unsigned char sim_edram[4u<<20];
static inline uint32_t sim_edram_base(){return uint32_t(reinterpret_cast<uintptr_t>(sim_edram));}
#include "../psp/Recorder.cpp"
extern "C" {void meLibDcacheInvalidateRange(unsigned,unsigned){}void meLibDcacheWritebackRange(unsigned,unsigned){}}
#include "../psp/RecMeKernel.inl"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <cmath>
#include <ctime>

// ------------------------------------------------------------ the clock --
static double scale=4.0;static const auto host0=std::chrono::steady_clock::now();
static uint64_t sim_us(){return uint64_t(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-host0).count()*scale)+1000000ull;}
static void sim_sleep(uint64_t us){std::this_thread::sleep_for(std::chrono::microseconds(uint64_t(us/scale)));}
static uint32_t sim_me_count(){return uint32_t(sim_us()*200ull);}   // a 200 MHz count
static std::string root;
static std::string host_path(const char* p){std::string s(p);if(!s.compare(0,4,"ms0:"))return root+"/ms0"+s.substr(4);return s;}
static std::atomic<unsigned> buttons{0};static std::atomic<uint64_t> free_bytes{2048ull<<20};
// The card: one request at a time (G0b P3 on the Go: 64 KiB writes p50 9.6 ms, p99 150 ms, max 360 ms).
static std::mutex card;static uint32_t card_rng=7;static bool card_model=true;
static void card_time(uint64_t bytes,bool write){if(!card_model)return;uint64_t us=bytes*1000000ull/(write?4800000ull:12000000ull);
    if(write){card_rng=card_rng*1103515245u+12345u;if((card_rng>>16)%100==0)us+=150000;}sim_sleep(us);}

extern "C" {
uint64_t sceKernelGetSystemTimeWide(void){return sim_us();}
unsigned int sceKernelGetSystemTimeLow(void){return unsigned(sim_us());}
struct Sema {std::mutex m;std::condition_variable cv;int count;};static std::map<int,Sema*> semas;static int next_id=100;static std::mutex ids;
SceUID sceKernelCreateSema(const char*,unsigned,int init,int,void*){std::lock_guard<std::mutex> l(ids);auto* s=new Sema;s->count=init;semas[++next_id]=s;return next_id;}
int sceKernelWaitSema(SceUID id,int n,SceUInt*){Sema* s;{std::lock_guard<std::mutex> l(ids);s=semas[id];}std::unique_lock<std::mutex> l(s->m);s->cv.wait(l,[&]{return s->count>=n;});s->count-=n;return 0;}
int sceKernelSignalSema(SceUID id,int n){Sema* s;{std::lock_guard<std::mutex> l(ids);s=semas[id];}{std::lock_guard<std::mutex> l(s->m);s->count+=n;}s->cv.notify_all();return 0;}
struct Thr {SceKernelThreadEntry f;std::thread t;};static std::map<int,Thr*> threads;
SceUID sceKernelCreateThread(const char*,SceKernelThreadEntry f,int,int,SceUInt,void*){std::lock_guard<std::mutex> l(ids);auto* t=new Thr;t->f=f;threads[++next_id]=t;return next_id;}
int sceKernelStartThread(SceUID id,SceSize n,void* a){Thr* t;{std::lock_guard<std::mutex> l(ids);t=threads[id];}t->t=std::thread([t,n,a]{t->f(n,a);});return 0;}
int sceKernelWaitThreadEnd(SceUID id,SceUInt*){Thr* t;{std::lock_guard<std::mutex> l(ids);t=threads[id];}if(t->t.joinable())t->t.join();return 0;}
int sceKernelDeleteThread(SceUID){return 0;}
int sceKernelDelayThread(SceUInt us){sim_sleep(us);return 0;}
void sceKernelDcacheWritebackInvalidateAll(void){}void sceKernelDcacheWritebackInvalidateRange(const void*,unsigned){}void sceKernelDcacheInvalidateRange(const void*,unsigned){}void sceKernelDcacheWritebackRange(const void*,unsigned){}
SceUID sceIoOpen(const char* p,int flags,int){int f=0;const int acc=flags&3;f|=acc==1?O_RDONLY:acc==2?O_WRONLY:O_RDWR;if(flags&PSP_O_APPEND)f|=O_APPEND;if(flags&PSP_O_CREAT)f|=O_CREAT;if(flags&PSP_O_TRUNC)f|=O_TRUNC;
    const int r=::open(host_path(p).c_str(),f,0644);return r<0?int(0x80010002):r;}
int sceIoWrite(SceUID f,const void* d,SceSize n){
    const uint64_t room=free_bytes.load();if(room<n){if(!room)return int(0x8001001c);n=SceSize(room);}   // a full card: short write, then an error
    std::lock_guard<std::mutex> l(card);card_time(n,true);const int r=int(::write(f,d,n));if(r>0)free_bytes-=uint64_t(r);return r;}
int sceIoRead(SceUID f,void* d,SceSize n){std::lock_guard<std::mutex> l(card);card_time(n,false);return int(::read(f,d,n));}
SceOff sceIoLseek(SceUID f,SceOff o,int w){return ::lseek(f,off_t(o),w==0?SEEK_SET:w==1?SEEK_CUR:SEEK_END);}
int sceIoClose(SceUID f){return ::close(f);}
int sceIoRemove(const char* p){struct stat s;if(::stat(host_path(p).c_str(),&s)==0)free_bytes+=uint64_t(s.st_size);return ::unlink(host_path(p).c_str());}
int sceIoGetstat(const char* p,SceIoStat* st){struct stat s;if(::stat(host_path(p).c_str(),&s))return -1;st->st_size=s.st_size;return 0;}
int sceIoMkdir(const char* p,int){return ::mkdir(host_path(p).c_str(),0755);}
int sceIoDevctl(const char*,unsigned cmd,void* in,int,void*,int){if(cmd!=SCE_PR_GETDEV)return -1;SceDevInf* d=static_cast<SceDevctlCmd*>(in)->pdevinf;d->sectorSize=512;d->sectorCount=64;d->freeClusters=unsigned(free_bytes.load()/32768u);d->maxClusters=d->freeClusters;return 0;}
static std::map<int,DIR*> dirs;
SceUID sceIoDopen(const char* p){DIR* d=::opendir(host_path(p).c_str());if(!d)return -1;std::lock_guard<std::mutex> l(ids);dirs[++next_id]=d;return next_id;}
int sceIoDread(SceUID id,SceIoDirent* e){DIR* d=dirs[id];dirent* x;while((x=::readdir(d))){if(x->d_name[0]=='.')continue;std::snprintf(e->d_name,sizeof(e->d_name),"%s",x->d_name);return 1;}return 0;}
int sceIoDclose(SceUID id){::closedir(dirs[id]);dirs.erase(id);return 0;}
int sceCtrlPeekBufferPositive(SceCtrlData* d,int){d->Buttons=buttons.load();return 1;}
int sceRtcGetCurrentClockLocalTime(ScePspDateTime* t){const time_t now=time(nullptr);tm x;localtime_r(&now,&x);t->year=uint16_t(x.tm_year+1900);t->month=uint16_t(x.tm_mon+1);t->day=uint16_t(x.tm_mday);t->hour=uint16_t(x.tm_hour);t->minute=uint16_t(x.tm_min);t->second=uint16_t(x.tm_sec);t->microsecond=0;return 0;}
static std::atomic<unsigned> power_ticks{0};
int scePowerTick(int){++power_ticks;return 0;}
void pspSdkDisableFPUExceptions(void){}
// GeRenderer.cpp / MeAudio.cpp side of the recorder API
static std::atomic<uint32_t> ge_closed{0};static std::mutex ge_m;static std::map<uint32_t,uint64_t> ge_done_at;
int th10_rec_ge_done(unsigned fence){std::lock_guard<std::mutex> l(ge_m);auto it=ge_done_at.find(fence);return it!=ge_done_at.end()&&it->second<=sim_us();}
static int me_on=1;
int th10_rec_me_ready(void){return me_on;}
int th10_rec_audio_running(void){return 1;}
const char* th10_rec_me_state(void){return me_on?"ME (sim)":"skipped (sim)";}
void th10_rec_audio_counters(unsigned* out){for(int i=0;i<8;i++)out[i]=0;out[5]=1500;out[6]=1;}
void th10_rec_audio_level_reset(void){}
}

// ------------------------------------------------------------- threads --
static std::atomic<bool> run{true};static volatile uint32_t audio_submitted=0;static uint32_t job_us=1500;
static void me_main(){   // the ME loop of MeAudio.cpp with the rec rings (audio jobs are only counted): copies before encode jobs
    while(run){
        if(th10::rec::me::wpending())th10::rec::me::run_copy_one(&audio_submitted);
        else if(th10::rec::me::pending()){sim_sleep(job_us);th10::rec::me::run_one(&audio_submitted);}
        else sim_sleep(200);
    }
}
// The BGM side's big read (every `period` ms: pause our writer, read `bytes` from the card, resume).
static std::atomic<unsigned> big_reads{0};static uint64_t pause_wait_max=0;
static void bgm_main(int period_ms,int read_ms){
    uint64_t next=sim_us()+uint64_t(period_ms)*1000u;
    while(run){
        while(run&&sim_us()<next)sim_sleep(10000);
        if(!run)break;
        th10_rec_writer_pause(1);const uint64_t t0=sim_us();
        while(!th10_rec_writer_paused()&&sim_us()-t0<2000000)sim_sleep(1000);
        if(sim_us()-t0>pause_wait_max)pause_wait_max=sim_us()-t0;
        {std::lock_guard<std::mutex> l(card);sim_sleep(uint64_t(read_ms)*1000u);}
        th10_rec_writer_pause(0);++big_reads;next+=uint64_t(period_ms)*1000u;
    }
}
static uint64_t audio_t0=0;static std::vector<uint64_t> beep_times;
static void audio_main(){
    int16_t block[2048];uint64_t k=0;audio_t0=sim_us();uint32_t rng=1;
    while(run){
        const uint64_t t=audio_t0+k*1024ull*1000000ull/44100ull;
        while(sim_us()<t&&run)sim_sleep(1000);
        for(int i=0;i<1024;i++){
            const uint64_t ts=t+uint64_t(i)*1000000ull/44100ull;   // this frame's time
            const bool beep=(ts%1000000ull)<20000ull;                // 20 ms from every whole second (sim clock)
            rng=rng*1103515245u+12345u;const int noise=int((rng>>16)%201)-100;
            const int v=beep?int(14000*std::sin(2*M_PI*1000.0*double(ts)/1e6)):noise;
            block[2*i]=int16_t(v);block[2*i+1]=int16_t(beep?v:-noise);
        }
        th10_rec_audio_block(block);
        if(k%5==0)audio_submitted=audio_submitted+1;   // an audio job now and then (rec jobs that delay it are counted)
        ++k;
    }
}
static std::vector<int> envlist(const char* name){std::vector<int> v;if(const char* s=std::getenv(name)){std::string x(s);size_t p=0;while(p<x.size()){v.push_back(std::atoi(x.c_str()+p));p=x.find(',',p);if(p==std::string::npos)break;++p;}}return v;}

int main(){
    root=std::getenv("REC_SIM_ROOT")?std::getenv("REC_SIM_ROOT"):"rec_sim_out";
    if(const char* s=std::getenv("REC_SIM_SCALE"))scale=std::atof(s);
    const int ticks=std::getenv("REC_SIM_TICKS")?std::atoi(std::getenv("REC_SIM_TICKS")):3000;
    if(const char* s=std::getenv("REC_SIM_ME"))me_on=std::atoi(s);
    if(const char* s=std::getenv("REC_SIM_JOB_US"))job_us=uint32_t(std::atoi(s));
    if(const char* s=std::getenv("REC_SIM_FREE_MB"))free_bytes=uint64_t(std::atoll(s))<<20;
    const std::vector<int> presses=envlist("REC_SIM_PRESS"),stall=envlist("REC_SIM_STALL"),slow=envlist("REC_SIM_SLOW");
    const int crash=std::getenv("REC_SIM_CRASH")?std::atoi(std::getenv("REC_SIM_CRASH")):-1;
    if(const char* s=std::getenv("REC_SIM_CARD"))card_model=std::atoi(s)!=0;
    const bool bgm=!std::getenv("REC_SIM_BGM")||std::atoi(std::getenv("REC_SIM_BGM"));
    std::vector<uint16_t> frames;uint32_t nframes=0;
    if(const char* f=std::getenv("REC_SIM_FRAMES")){FILE* fp=std::fopen(f,"rb");if(fp){std::fseek(fp,0,SEEK_END);const long n=std::ftell(fp);std::fseek(fp,0,SEEK_SET);nframes=uint32_t(std::min<long>(n/(480*272*2),600));frames.resize(size_t(nframes)*480*272);
        if(std::fread(frames.data(),480*272*2,nframes,fp)!=nframes)nframes=0;std::fclose(fp);}}
    ::mkdir(root.c_str(),0755);::mkdir((root+"/ms0").c_str(),0755);::mkdir((root+"/ms0/PSP").c_str(),0755);::mkdir((root+"/ms0/PSP/GAME").c_str(),0755);::mkdir((root+"/ms0/PSP/GAME/TH10T").c_str(),0755);
    th10_rec_init("ms0:/PSP/GAME/TH10T");
    std::thread me(me_main),au(audio_main);
    const std::vector<int> big=envlist("REC_SIM_BIGREAD");   // "period_ms,read_ms": the BGM side's 2.5 MB read
    std::thread bg;if(big.size()==2)bg=std::thread(bgm_main,big[0],big[1]);
    std::vector<uint16_t> pic(480*272);uint32_t late=0,presents=0,slow_reads=0;uint64_t max_read=0;uint64_t last_marker_sec=0;std::vector<uint64_t> markers;uint32_t indicator=0;
    const uint64_t game0=sim_us();
    for(int tick=0;tick<ticks;tick++){
        if(stall.size()==2&&tick==stall[0]){sim_sleep(uint64_t(stall[1])*1000u);}   // a stage load: no ticks, no presents
        const bool slow_now=slow.size()==2&&tick>=slow[0]&&tick<slow[1];
        if(slow_now)sim_sleep(25000);   // a heavy tick (slowdown)
        const bool present=slow_now?(tick%3==0):(tick%2==0);
        if(present){
            ++presents;const uint64_t t=sim_us();const bool marker=(t%1000000ull)<100000ull;(void)last_marker_sec;   // lit for 100 ms after every whole second
            if(void* b=th10_rec_capture_want()){
                if(nframes)std::memcpy(pic.data(),frames.data()+size_t((tick/2)%nframes)*480*272,480*272*2);
                else for(int y=0;y<272;y++)for(int x=0;x<480;x++)pic[size_t(y)*480+x]=uint16_t((((x^y)>>4)&31)<<11|(((y+tick)>>3)&63)<<5|(((x+tick)>>4)&31));   // GE 5650: red in the low bits
                for(int y=0;y<64;y++)for(int x=0;x<64;x++)pic[size_t(y)*480+x]=marker?0xffff:0;
                std::memcpy(b,pic.data(),480*272*2);
                const uint32_t fence=++ge_closed;{std::lock_guard<std::mutex> l(ge_m);ge_done_at[fence]=sim_us()+5000;}
                th10_rec_capture_emitted(fence);
                if(marker&&(markers.empty()||t-markers.back()>500000ull))markers.push_back(t);
            }
            if(th10_rec_indicator())++indicator;
        }
        if(bgm&&tick%15==7){   // thbgm.dat: one 0.25 s refill (44100 bytes) on the game thread, waiting for the card like a real read
            const uint64_t r0=sim_us();{std::lock_guard<std::mutex> l(card);card_time(44100,false);}th10_rec_game_read(44100,unsigned(sim_us()-r0));
            if(sim_us()-r0>50000)++slow_reads;if(sim_us()-r0>max_read)max_read=sim_us()-r0;}
        bool down=false;for(int p:presses)if(tick>=p&&tick<p+4)down=true;
        buttons=down?PSP_CTRL_SELECT:0u;
        const uint64_t due=game0+uint64_t(tick+1)*16683ull;
        if(sim_us()<due)sim_sleep(due-sim_us());else ++late;
        th10_rec_tick(unsigned(tick),late,presents);
        if(tick==crash){std::printf("SIM CRASH at tick %d (no shutdown)\n",tick);std::fflush(stdout);_exit(0);}
    }
    th10_rec_shutdown();
    run=false;me.join();au.join();if(bg.joinable())bg.join();
    std::printf("SIM DONE ticks=%d presents=%u late=%u indicator_frames=%u power_ticks=%u markers_captured=%zu free_left=%llu MiB bgm_reads>50ms=%u max=%.1fms big_reads=%u pause_wait_max=%.1fms\n",ticks,presents,late,indicator,power_ticks.load(),markers.size(),(unsigned long long)(free_bytes.load()>>20),slow_reads,max_read/1000.0,big_reads.load(),pause_wait_max/1000.0);
    return 0;
}
