#pragma once
// TH10 recording: the portable core of the PSP-side MJPEG AVI writer. No PSP
// headers: the PSP writer thread (psp/Recorder.cpp) runs it with sceIo, the
// PC check (native/tools/rec_check.cpp) runs the same code with stdio, so the
// test exercises what the device writes.
//
// File layout = the XMB-proven "B0" file (deploy/avitest 01 and avitest2 05,
// 2026-10-01 on the Go; the G0 tool make_avitest.py (not published) write_avi,
// reproduced field for field, byte-compared by native/tools/rec_check.py):
//   RIFF 'AVI ' { LIST 'hdrl' { avih, LIST 'strl' {strh vids MJPG, strf
//   BITMAPINFOHEADER}, LIST 'strl' {strh auds, strf WAVEFORMATEX(18)} },
//   JUNK up to file offset 2048, LIST 'movi' { per video frame '00dc' JPEG
//   (odd sizes padded) then '01wb' PCM }, 'idx1' (AVIIF_KEYFRAME 0x10 on every
//   entry, offsets from the 'movi' FourCC, unpadded sizes) }.
// avih flags 0x110 (HASINDEX | ISINTERLEAVED). Audio: 16-bit LPCM 22.05 kHz
// mono (8-bit fails in the camera layout, avitest2 C group). idx1 is required
// by the XMB (avitest 07), so a file is finished on stop, and an unfinished
// one (crash, power loss) is repaired on the next start (Repair below).
//
// A/V clock: video slot k <-> audio mono samples [k*spf, (k+1)*spf), spf =
// 22050 / fps (1470 at 15 fps). The audio is what the audio thread played,
// so the slots run on real (audio) time even when the game slows down; a slot
// without a new frame repeats the previous JPEG (TH10_REC_DROP_EMPTY=1: a
// zero-length '00dc' instead, untested on the XMB).
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace th10::rec::avi {
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;using i16=std::int16_t;using i32=std::int32_t;

constexpr u32 header_bytes=2048;            // RIFF + hdrl + JUNK; 'LIST' <size> 'movi' starts here
constexpr u32 movi_fourcc=header_bytes+8;   // idx1 offsets count from here
constexpr u32 movi_start=header_bytes+12;   // first chunk
constexpr u32 audio_rate=22050,audio_block=2,audio_bytes_per_sec=audio_rate*audio_block;
constexpr u32 index_entry_bytes=16;
constexpr u32 flag_keyframe=0x10;

inline void put16(u8* p,u32 v){p[0]=u8(v);p[1]=u8(v>>8);}
inline void put32(u8* p,u32 v){p[0]=u8(v);p[1]=u8(v>>8);p[2]=u8(v>>16);p[3]=u8(v>>24);}
inline u32 get32(const u8* p){return u32(p[0])|u32(p[1])<<8|u32(p[2])<<16|u32(p[3])<<24;}
inline void fourcc(u8* p,const char* s){p[0]=u8(s[0]);p[1]=u8(s[1]);p[2]=u8(s[2]);p[3]=u8(s[3]);}
inline bool is_fourcc(const u8* p,const char* s){return p[0]==u8(s[0])&&p[1]==u8(s[1])&&p[2]==u8(s[2])&&p[3]==u8(s[3]);}

struct Params {u32 width=480,height=272,fps=15;};
// What the header needs at the end. movi_bytes: the chunks after the 'movi'
// FourCC (pads included); entries: idx1 entries.
struct Totals {u32 frames=0,audio_samples=0,video_max=0,audio_max=0;u64 movi_bytes=0;u32 entries=0;};

// Bytes [0, movi_start) of the file. finished=false: the placeholder written
// when the file is opened (RIFF and movi sizes 0 = "unfinished", Repair's mark).
inline void header(u8* out,const Params& p,const Totals& t,bool finished){
    std::memset(out,0,movi_start);
    const u32 fps=p.fps?p.fps:15,vmax=t.video_max,amax=t.audio_max;
    u8* h=out;
    fourcc(h,"RIFF");fourcc(h+8,"AVI ");
    fourcc(h+12,"LIST");put32(h+16,294);fourcc(h+20,"hdrl");
    u8* a=h+24;fourcc(a,"avih");put32(a+4,56);a+=8;   // MainAVIHeader, 14 dwords
    put32(a,(1000000u+fps/2u)/fps);                    // make_avitest: round(1e6 / fps)
    put32(a+4,vmax*fps+audio_bytes_per_sec+1u);        // int(vmax * fps + abytes) + 1
    put32(a+8,0);put32(a+12,0x110);put32(a+16,t.frames);put32(a+20,0);put32(a+24,2);put32(a+28,vmax+8u);put32(a+32,p.width);put32(a+36,p.height);
    u8* s=h+88;fourcc(s,"LIST");put32(s+4,116);fourcc(s+8,"strl");
    u8* v=s+12;fourcc(v,"strh");put32(v+4,56);v+=8;     // video AVIStreamHeader
    fourcc(v,"vids");fourcc(v+4,"MJPG");put32(v+8,0);put16(v+12,0);put16(v+14,0);put32(v+16,0);put32(v+20,1);put32(v+24,fps);put32(v+28,0);
    put32(v+32,t.frames);put32(v+36,vmax);put32(v+40,0xffffffffu);put32(v+44,0);put16(v+48,0);put16(v+50,0);put16(v+52,p.width);put16(v+54,p.height);
    u8* b=s+76;fourcc(b,"strf");put32(b+4,40);b+=8;      // BITMAPINFOHEADER
    put32(b,40);put32(b+4,p.width);put32(b+8,p.height);put16(b+12,1);put16(b+14,24);fourcc(b+16,"MJPG");put32(b+20,p.width*p.height*3u);
    u8* s2=h+212;fourcc(s2,"LIST");put32(s2+4,94);fourcc(s2+8,"strl");
    u8* q=s2+12;fourcc(q,"strh");put32(q+4,56);q+=8;     // audio AVIStreamHeader
    fourcc(q,"auds");put32(q+4,0);put32(q+8,0);put16(q+12,0);put16(q+14,0);put32(q+16,0);put32(q+20,audio_block);put32(q+24,audio_bytes_per_sec);put32(q+28,0);
    put32(q+32,t.audio_samples);put32(q+36,amax);put32(q+40,0xffffffffu);put32(q+44,audio_block);
    u8* w=s2+76;fourcc(w,"strf");put32(w+4,18);w+=8;      // WAVEFORMATEX, cbSize 0
    put16(w,1);put16(w+2,1);put32(w+4,audio_rate);put32(w+8,audio_bytes_per_sec);put16(w+12,audio_block);put16(w+14,16);put16(w+16,0);
    u8* j=h+314;fourcc(j,"JUNK");put32(j+4,header_bytes-314u-8u);   // 1726 zero bytes: 'LIST' 'movi' at 2048
    u8* m=h+header_bytes;fourcc(m,"LIST");fourcc(m+8,"movi");
    if(finished){
        const u64 movi_list=4u+t.movi_bytes;
        const u64 file=u64(movi_start)+t.movi_bytes+8u+u64(t.entries)*index_entry_bytes;
        put32(h+4,u32(file-8u));put32(m+4,u32(movi_list));
    }
}
// Repair's test: exactly our placeholder (header(..., false)) for the
// width/height/fps it names, byte for byte; anything else is left alone.
inline bool parse_header(const u8* in,Params& p){
    if(!is_fourcc(in,"RIFF")||!is_fourcc(in+8,"AVI ")||!is_fourcc(in+header_bytes,"LIST")||!is_fourcc(in+header_bytes+8,"movi"))return false;
    p.width=get32(in+64);p.height=get32(in+68);p.fps=get32(in+132);   // avih dwWidth/dwHeight, video strh dwRate
    if(!p.width||!p.height||!p.fps||p.width>1024||p.height>1024||p.fps>60)return false;
    u8 expect[movi_start];header(expect,p,Totals{},false);
    return std::memcmp(expect,in,movi_start)==0;
}
inline bool unfinished(const u8* in){return is_fourcc(in,"RIFF")&&get32(in+4)==0;}

// The played audio (1024 frames of 44.1 kHz stereo s16, MeAudio's block) ->
// 22.05 kHz mono s16: each output sample is the rounded mean of the four
// input samples of a frame pair, m[i] = (L[2i] + R[2i] + L[2i+1] + R[2i+1] + 2) >> 2
// (arithmetic shift = floor; range -32768..32767, no clamp needed).
inline void downmix(const i16* stereo,u32 frames,i16* mono){
    for(u32 i=0;i<frames/2u;i++){
        const i32 s=i32(stereo[4*i])+i32(stereo[4*i+1])+i32(stereo[4*i+2])+i32(stereo[4*i+3])+2;
        mono[i]=i16(s>>2);
    }
}

// ---------------------------------------------------------------- writer --
// One AVI file at a time. The byte stream is put into two `piece`-byte write
// buffers in turn (Main RAM); a full buffer is written whole, so with piece =
// 64 KiB every write is whole FAT clusters on a 64 KiB boundary of the file.
// Short writes are completed in a loop (TH08 psp/fileio.cpp:1001-1015). When
// to write is the caller's policy (write_one). JPEG bytes reach the buffers
// through the source's copy() (on the PSP an ME copy job out of the JPEG
// ring, which may be in ME eDRAM), everything else through put(). idx1
// entries go to a RAM buffer, appended to a spill file when the caller asks
// (spill_index) or, as a last resort, when it is full; finalize copies the
// spill back (a 1 GiB file at 3 Mbps is ~47 min = 1.35 MB of idx1). No sync
// calls (TH08 sceIoSync stalled 1.93 s).
//
// Fs: static int open_write(path) (create/truncate), open_rw(path),
// open_read(path), int write(f,p,n) (may be short, <0 error), int read(f,p,n),
// long long seek(f,pos) (absolute), long long size(f), int close(f),
// int remove(path), u64 now_us(), void before_write(const void* p,u32 n)
// (PSP: the SC's dirty lines of a write buffer to RAM before sceIoWrite).
struct WriteStats {u64 bytes=0,write_us=0;u32 writes=0,shorts=0,errors=0,max_us=0,over50=0,over100=0,over500=0,over1000=0,files=0,finalize_max_us=0,spills=0,forced_spills=0;int first_error=0;
    u32 hist[256]{};   // write times, 2 ms buckets (the last one: 510 ms and more)
    u32 p99_us() const {u32 n=0;for(u32 b:hist)n+=b;if(!n)return 0;const u32 want=n-n/100u;u32 acc=0;for(u32 i=0;i<256;i++){acc+=hist[i];if(acc>=want)return i<255?(i+1)*2000u:max_us;}return max_us;}};
template<class Fs> struct Writer {
    Params params;
    u8* bufs[2]{};u32 piece=65536u,fill[2]{},head=0,full_n=0,oldest=0;   // head: the buffer being filled; oldest: the first full one
    u64 stream=0;                                                      // file offset of the next byte put
    u8* index=nullptr;u32 index_cap=0,index_n=0;                       // RAM idx1 entries
    int spill=-1;u32 spilled=0;char spill_path[200]{};
    int file=-1;char path[200]{};
    Totals t;bool error=false;
    WriteStats* stats=nullptr;
    void setup(u8* buffer0,u8* buffer1,u32 piece_bytes,u8* index_buf,u32 index_bytes,WriteStats* s){bufs[0]=buffer0;bufs[1]=buffer1;piece=piece_bytes;index=index_buf;index_cap=index_bytes/index_entry_bytes;stats=s;}
    void fail(int code){if(!error&&stats){++stats->errors;if(!stats->first_error)stats->first_error=code?code:-1;}error=true;}
    bool write_all(const u8* p,u32 n){
        Fs::before_write(p,n);
        const u64 t0=Fs::now_us();u32 off=0;
        while(off<n){const int w=Fs::write(file,p+off,n-off);if(w<=0){fail(w);return false;}if(u32(w)<n-off&&stats)++stats->shorts;off+=u32(w);}
        if(stats){const u64 dt=Fs::now_us()-t0;stats->bytes+=n;stats->write_us+=dt;++stats->writes;if(dt>stats->max_us)stats->max_us=u32(dt);
            if(dt>50000){++stats->over50;}if(dt>100000){++stats->over100;}if(dt>500000){++stats->over500;}if(dt>1000000){++stats->over1000;}
            const u64 b=dt/2000u;++stats->hist[b<255?b:255];}
        return true;
    }
    // Empty idx1 (RAM and spill) for a new file or a repair walk.
    void reset_index(const char* spill_file){
        if(spill>=0){Fs::close(spill);spill=-1;}
        index_n=0;spilled=0;t=Totals{};error=false;std::snprintf(spill_path,sizeof(spill_path),"%s",spill_file);
    }
    void reset_buffers(){fill[0]=fill[1]=0;head=0;full_n=0;oldest=0;}
    // A new file: the placeholder header is the stream's first bytes.
    bool open(const char* file_path,const char* spill_file,const Params& p){
        std::snprintf(path,sizeof(path),"%s",file_path);reset_index(spill_file);reset_buffers();
        params=p;stream=0;
        file=Fs::open_write(path);if(file<0){fail(file);return false;}
        if(stats)++stats->files;
        u8 h[movi_start];header(h,params,t,false);return put(h,movi_start);
    }
    // Repair: the existing file, its chunks ending at `end`; the index and
    // the totals were collected by the walk and are kept.
    bool reopen(const char* file_path,const Params& p,u64 end){
        std::snprintf(path,sizeof(path),"%s",file_path);reset_buffers();
        params=p;stream=end;
        file=Fs::open_rw(path);if(file<0){fail(file);return false;}
        return true;
    }
    // Bytes that can be put before a buffer has to be written.
    u32 room() const {return full_n>=2?0u:(piece-fill[head])+(full_n==0?piece:0u);}
    u32 full() const {return full_n;}
    u64 pending() const {u64 n=u64(full_n)*piece;if(full_n<2)n+=fill[head];return n;}
    void advance(u32 k){
        fill[head]+=k;stream+=k;
        if(fill[head]==piece){if(!full_n)oldest=head;++full_n;head^=1u;}
    }
    bool put(const void* data,u32 n){
        const u8* p=static_cast<const u8*>(data);
        while(n){if(full_n>=2||error)return false;const u32 k=n<piece-fill[head]?n:piece-fill[head];std::memcpy(bufs[head]+fill[head],p,k);p+=k;n-=k;advance(k);}
        return true;
    }
    // `n` bytes of a JPEG held by the source: src.copy(handle, offset, dst, k) per buffer piece.
    template<class Src> bool put_copy(Src& src,u32 handle,u32 n){
        u32 off=0;
        while(off<n){if(full_n>=2||error)return false;const u32 k=n-off<piece-fill[head]?n-off:piece-fill[head];
            if(!src.copy(handle,off,bufs[head]+fill[head],k)){fail(-4);return false;}off+=k;advance(k);}
        return true;
    }
    // The oldest full buffer to the file.
    bool write_one(){
        if(!full_n||error)return false;
        if(!write_all(bufs[oldest],piece))return false;
        fill[oldest]=0;--full_n;if(full_n)oldest^=1u;return true;
    }
    // The RAM idx1 entries to the spill file (appended; created on first use).
    bool spill_index(){
        if(!index_n)return true;
        if(spill<0){spill=Fs::open_write(spill_path);if(spill<0){fail(spill);return false;}}
        const u32 n=index_n*index_entry_bytes;u32 off=0;
        while(off<n){const int w=Fs::write(spill,index+off,n-off);if(w<=0){fail(w);return false;}off+=u32(w);}
        spilled+=index_n;index_n=0;if(stats)++stats->spills;return true;
    }
    bool want_spill() const {return index_n>=index_cap-index_cap/4u;}   // 3/4 full: the caller spills at a good moment
    bool add_index(const char* id,u32 flags,u64 chunk_pos,u32 bytes){
        if(index_n==index_cap){if(stats)++stats->forced_spills;if(!spill_index())return false;}   // full: now
        u8* e=index+index_n*index_entry_bytes;fourcc(e,id);put32(e+4,flags);put32(e+8,u32(chunk_pos-movi_fourcc));put32(e+12,bytes);++index_n;++t.entries;
        return true;
    }
    // A chunk header and its idx1 entry; the caller puts the `bytes` of
    // payload next, then pad(bytes).
    bool chunk_header(const char* id,u32 bytes,u32 flags){
        if(error)return false;
        u8 h[8];fourcc(h,id);put32(h+4,bytes);
        if(!add_index(id,flags,stream,bytes))return false;
        t.movi_bytes+=8u+bytes+(bytes&1u);
        return put(h,8);
    }
    bool pad(u32 bytes){static const u8 zero=0;return (bytes&1u)?put(&zero,1):true;}
    // File size if `bytes` more chunk bytes and `entries` more idx1 entries were added now.
    u64 size_if(u64 bytes,u32 entries) const {return stream+bytes+8u+u64(spilled+index_n+entries)*index_entry_bytes;}
    // The rest of the stream, idx1 (spill copied back, then the RAM entries),
    // the header rewritten, close. Returns false on any error; a file
    // without frames is removed.
    bool finalize(){
        const u64 t0=Fs::now_us();bool ok=!error;
        while(ok&&full_n)ok=write_one();
        if(ok&&full_n<2&&fill[head]){ok=write_all(bufs[head],fill[head]);fill[head]=0;}
        if(file<0)return false;
        if(ok&&!t.frames){Fs::close(file);file=-1;Fs::remove(path);if(spill>=0){Fs::close(spill);spill=-1;Fs::remove(spill_path);}return true;}
        if(ok&&Fs::seek(file,stream)<0){fail(-2);ok=false;}
        if(ok){u8 h[8];fourcc(h,"idx1");put32(h+4,(spilled+index_n)*index_entry_bytes);ok=write_all(h,8);}
        if(ok&&spill>=0){
            Fs::close(spill);spill=Fs::open_read(spill_path);if(spill<0){fail(spill);ok=false;}
            u32 left=spilled*index_entry_bytes;
            while(ok&&left){const u32 want=left<piece?left:piece;u32 got=0;
                while(got<want){const int r=Fs::read(spill,bufs[0]+got,want-got);if(r<=0)break;got+=u32(r);}
                if(got!=want){fail(-3);ok=false;break;}
                ok=write_all(bufs[0],want);left-=want;}
        }
        if(ok&&index_n)ok=write_all(index,index_n*index_entry_bytes);
        if(ok){header(bufs[0],params,t,true);ok=Fs::seek(file,0)>=0&&write_all(bufs[0],movi_start);}
        Fs::close(file);file=-1;
        if(spill>=0){Fs::close(spill);spill=-1;}
        if(spilled)Fs::remove(spill_path);
        reset_buffers();
        if(stats){const u64 dt=Fs::now_us()-t0;if(dt>stats->finalize_max_us)stats->finalize_max_us=u32(dt);}
        return ok&&!error;
    }
    // Close without finishing (abandoned repair).
    void abandon(){if(file>=0){Fs::close(file);file=-1;}if(spill>=0){Fs::close(spill);spill=-1;Fs::remove(spill_path);}reset_buffers();}
};

// ------------------------------------------------------------------ muxer --
// Slots in order. Src (the platform side):
//   u64 audio_written()                    mono samples captured since the session started
//   void audio_read(u64 at,i16* out,u32 n) samples [at, at+n); overwritten ones as 0
//   u32 records()                          unconsumed frame records, oldest first
//   u32 record_slot(u32 i), u32 record_state(u32 i), u32 record_bytes(u32 i), u32 record_handle(u32 i)  (i: 0 = oldest)
//   u32 record_tick(u32 i)                 the game tick the picture was captured at
//   void note_frame(u64 frame,u32 tick)   frame `frame` of the session (0 = first in the file) shows the picture of `tick`
//   bool copy(u32 handle,u32 offset,u8* dst,u32 n)  bytes of a done record's JPEG (valid until released)
//   void pop(), void release(u32 handle)   pictures are released in the order they were encoded
// Record states: Pending (being captured/encoded: the slot waits for it),
// Done (JPEG ready), Failed (no picture: superseded, overflow, error).
enum RecordState : u32 {RecPending=1,RecDone=2,RecFailed=3};
enum Step : u32 {StepWait=0,StepEmitted,StepSkipped,StepRoom,StepSplit,StepFull,StepDone,StepError};
struct MuxStats {u64 slots=0;u32 real=0,repeats=0,empty=0,superseded=0,failed=0,lead=0;u64 audio_samples=0;};
constexpr u32 max_spf=1470;   // 15 fps
template<class Fs,class Src> struct Mux {
    Writer<Fs>* w=nullptr;Src* src=nullptr;
    u32 spf=1470;bool drop_empty=false;
    u64 next_slot=0;
    bool have_last=false;u32 last_handle=0,last_bytes=0,last_tick=0;   // the picture a slot without a new one repeats
    u64 split_bytes=1000ull<<20,budget=~0ull,used=0;       // file split size; bytes this session may still take on the card
    MuxStats st;
    void begin(Writer<Fs>* writer,Src* source,u32 samples_per_frame,bool empty_drops){w=writer;src=source;spf=samples_per_frame>max_spf?max_spf:samples_per_frame;drop_empty=empty_drops;next_slot=0;have_last=false;last_bytes=0;last_tick=0;used=0;st=MuxStats{};}
    // Emit slot next_slot if everything for it is there. `limit`: slots >= limit are not recorded (stop).
    Step step(u64 limit){
        const u64 k=next_slot;
        if(k>=limit)return StepDone;
        if(src->audio_written()<(k+1u)*spf)return StepWait;
        // Frames for this slot: the oldest records with slot <= k; a pending one is waited for.
        const u32 n=src->records();u32 take=0;int best=-1;
        for(;take<n;take++){
            if(src->record_slot(take)>k)break;
            const u32 s=src->record_state(take);
            if(s==RecPending)return StepWait;
            if(s==RecDone)best=int(take);
        }
        if(best<0&&!have_last){   // the recording starts at its first picture: lead-in slots are skipped
            for(u32 i=0;i<take;i++){++st.failed;src->pop();}
            ++next_slot;++st.lead;return StepSkipped;
        }
        const u32 vbytes=best>=0?src->record_bytes(u32(best)):drop_empty?0u:last_bytes;
        const u32 abytes=spf*2u,need=8u+vbytes+(vbytes&1u)+8u+abytes;
        if(need>w->room())return StepRoom;   // a buffer has to be written first
        if(w->t.frames&&w->size_if(need,2)>split_bytes)return StepSplit;
        if(used+need+32u>budget)return StepFull;
        const u32 tick=best>=0?src->record_tick(u32(best)):last_tick;
        // Consume, releasing pictures in the order they were encoded: the
        // previous kept picture first, then older pictures of this slot
        // (superseded by the newest), then the newest is kept.
        if(best>=0&&have_last){src->release(last_handle);have_last=false;}
        u32 handle=last_handle;
        for(u32 i=0;i<take;i++){
            const u32 s=src->record_state(0),h=src->record_handle(0);
            if(int(i)==best)handle=h;
            else if(s==RecDone){++st.superseded;src->release(h);}
            else if(s==RecFailed)++st.failed;
            src->pop();
        }
        if(best>=0){have_last=true;last_handle=handle;last_bytes=vbytes;last_tick=tick;}
        if(!w->chunk_header("00dc",vbytes,(vbytes||!drop_empty)?flag_keyframe:0u))return StepError;
        if(vbytes&&!w->put_copy(*src,handle,vbytes))return StepError;
        if(!w->pad(vbytes))return StepError;
        if(best>=0)++st.real;else if(vbytes)++st.repeats;else ++st.empty;
        if(!w->chunk_header("01wb",abytes,flag_keyframe))return StepError;
        i16 pcm[max_spf];src->audio_read(k*spf,pcm,spf);
        if(!w->put(pcm,abytes))return StepError;
        ++w->t.frames;w->t.audio_samples+=spf;if(vbytes>w->t.video_max)w->t.video_max=vbytes;if(abytes>w->t.audio_max)w->t.audio_max=abytes;
        src->note_frame(st.slots,tick);
        used+=need+32u;st.audio_samples+=spf;++st.slots;++next_slot;
        return StepEmitted;
    }
    // After the session: the kept picture is no longer needed.
    void end(){if(have_last)src->release(last_handle);have_last=false;}
};

// ----------------------------------------------------------------- repair --
// An unfinished file (RIFF size 0: the game stopped before finalize): walk
// its chunks from movi_start, keep whole '00dc'+'01wb' pairs, then write idx1
// right after the last whole pair and the finished header (Writer::finalize).
// Bytes after that are left beyond the RIFF size. Step-wise (the writer
// thread interleaves it with its other work, and drops it when a recording
// starts: nothing is written before the walk ends).
template<class Fs> struct Repair {
    int file=-1;u64 size=0,at=0,pair_end=0;Params params;Totals t;Writer<Fs>* w=nullptr;
    u32 pend_bytes=0;u64 pend_pos=0;bool pend=false;   // a '00dc' waiting for its '01wb'
    u32 chunks=0;bool walking=false;char path[200]{},spill[200]{};
    // Starts on `file_path`: false when it is not an unfinished file of ours.
    // `scratch`: movi_start bytes (the writer's first buffer).
    bool begin(const char* file_path,const char* spill_path,Writer<Fs>* writer,u8* scratch){
        std::snprintf(path,sizeof(path),"%s",file_path);std::snprintf(spill,sizeof(spill),"%s",spill_path);w=writer;
        file=Fs::open_read(path);if(file<0)return false;
        const long long n=Fs::size(file);size=n>0?u64(n):0;
        u32 got=0;if(size>=movi_start){Fs::seek(file,0);while(got<movi_start){const int r=Fs::read(file,scratch+got,movi_start-got);if(r<=0)break;got+=u32(r);}}
        if(got!=movi_start||!unfinished(scratch)||!parse_header(scratch,params)){Fs::close(file);file=-1;return false;}
        at=movi_start;pair_end=movi_start;t=Totals{};pend=false;chunks=0;walking=true;
        w->reset_index(spill);   // the idx1 entries are collected in the writer's RAM index (and spill) as pairs complete
        return true;
    }
    // Up to `max_chunks` chunk headers. Returns true while walking.
    bool walk(u32 max_chunks){
        if(!walking)return false;
        for(u32 c=0;c<max_chunks;c++){
            u8 h[8];u32 got=0;
            if(at+8u>size||Fs::seek(file,at)<0){walking=false;break;}
            while(got<8){const int r=Fs::read(file,h+got,8-got);if(r<=0)break;got+=u32(r);}
            if(got!=8){walking=false;break;}
            const u32 bytes=get32(h+4);const u64 next=at+8u+bytes+(bytes&1u);
            const bool video=is_fourcc(h,"00dc"),audio=is_fourcc(h,"01wb");
            if((!video&&!audio)||next>size||bytes>(64u<<20)){walking=false;break;}
            if(video){pend=true;pend_pos=at;pend_bytes=bytes;}
            else if(pend&&!(bytes&1u)){   // a whole pair
                if(!w->add_index("00dc",pend_bytes?flag_keyframe:0u,pend_pos,pend_bytes)||!w->add_index("01wb",flag_keyframe,at,bytes)){walking=false;break;}
                ++t.frames;t.audio_samples+=bytes/audio_block;if(pend_bytes>t.video_max)t.video_max=pend_bytes;if(bytes>t.audio_max)t.audio_max=bytes;
                pair_end=next;pend=false;
            }else{walking=false;break;}   // audio without its picture: not our order
            at=next;++chunks;
        }
        return walking;
    }
    // After the walk: idx1 at pair_end, the header, close. False: nothing to do (no whole pair) or an error.
    bool finish(){
        Fs::close(file);file=-1;
        if(!t.frames){w->abandon();return false;}
        if(!w->reopen(path,params,pair_end))return false;
        w->t=t;w->t.entries=2u*t.frames;w->t.movi_bytes=pair_end-movi_start;   // the walk's counts (its entries are in the index/spill)
        return w->finalize();
    }
    void abandon(){if(file>=0){Fs::close(file);file=-1;}walking=false;w->abandon();}
};
}
