#pragma once
// TH10 recording: a small integer baseline JPEG encoder (G0b timing phase
// P2d/P2e, and the MJPEG AVI path chosen on
// 2026-10-01). On the Go (G0b, 10-01) the ME, the SC and the PC gave the same
// bytes for every self-test frame and 0 audit mismatches; 480x272 q50 took
// 48.8/50.7/53.5 ms (p50/p95/max) per frame on the ME.
// One source for the places that run it:
//   - the Media Engine (psp/RecMeKernel.inl, job kind JobJpeg),
//   - the SC when there is no ME (psp/Recorder.cpp sc_encode: PPSSPP),
//   - the PC (native/tools/rec_check.cpp, rec_sim.cpp; the G0b tools in
//     rec_jpeg_test.cpp (not published): decode/PSNR checks).
//
// Encoder core rules (it runs on the ME, see RecMeKernel.inl): integer only,
// no division, no switch, no static or global data (every table comes through
// `Tables`, which the ME reads through the cached kseg0 alias), no struct
// copies, no library calls. Checked by disassembly (native/tools/rec_me_check.py).
//
// Bitstream (what the PSP writes into every AVI '00dc' chunk): SOI, APP0 JFIF,
// DQT x2, SOF0 (baseline, 8 bit, Y Cb Cr, 4:2:0; 4:2:2 only for the PC test
// set), DHT x4 (ITU-T T.81 Annex K tables, in every frame; the PC test set
// also makes frames without them), SOS, entropy-coded data, EOI. No restart
// markers: the encoder state (bit buffer, DC predictors, output position) is
// carried between jobs in `State`, so a frame may be cut into any MCU ranges
// and the bytes stay the same (the PC test checks one-shot == job split).
// Algorithms after IJG libjpeg / libjpeg-turbo, rewritten: the ifast AAN DCT
// (jfdctfst.c), the 16-bit reciprocal quantization (jcdctmgr.c
// compute_reciprocal), the quality scaling (jcparam.c) and the JFIF YCbCr
// constants (jccolor.c).
#include <cstdint>

#define TH10_JPEG_CORE __attribute__((optimize("no-jump-tables","no-tree-loop-distribute-patterns")))

namespace th10::rec::jpeg {
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using i32=std::int32_t;

constexpr u32 header_capacity=768;
// Worst case of one MCU (6 blocks): (16+11) + 63 x (16+10) bits per block,
// every byte 0xFF-stuffed: 2 x 209 x 6 = 2508 bytes.
constexpr u32 mcu_worst_bytes=2560;

struct Tables {
    i32 y_r[32],y_g[64],y_b[32];      // Q16 luma terms of the RGB565 fields (bit-replicated to 8 bit); -128 and rounding folded into y_b
    u16 recip[2][64],corr[2][64],shift[2][64];   // zigzag order, [0] luma [1] chroma: q = ((|x| + corr) * recip) >> shift
    u8 natural[64];                    // zigzag index -> natural (row-major) index
    u32 dc_code[2][16];                // (size << 16) | code, by magnitude category
    u32 ac_code[2][256];               // (size << 16) | code, by (run << 4) | size
    u32 width,height,mcus_x,mcus_y,vsamp,mcu_h;  // vsamp 2: 4:2:0 (MCU 16x16), 1: 4:2:2 (MCU 16x8)
    u32 src_w,src_h,scale_top,scale_active;      // 3:2 path: 480x272 source -> lines [scale_top, +scale_active) of the output
    u32 quality,dht,header_bytes,pad;
    u8 header[header_capacity];
};

// Carried from one job to the next (on the PSP it lives in RecG0.hpp JpegCtx,
// touched by the ME only through the uncached alias at job entry and exit).
struct State {u32 pos,acc,nbits,overflow;i32 dc0,dc1,dc2;u32 blocks,flat;};

// ----------------------------------------------------------- encoder core --
struct Writer {u8* p;u8* end;u32 acc,n;};
// size 1..16 and code < 2^size; n <= 7 on entry, so the accumulator never holds more than 23 live bits.
TH10_JPEG_CORE inline void put_bits(Writer& w,u32 code,u32 size){
    w.acc=(w.acc<<size)|code;w.n+=size;
    while(w.n>=8){w.n-=8;const u32 b=(w.acc>>w.n)&0xffu;*w.p++=u8(b);if(b==0xffu)*w.p++=0;}
}
TH10_JPEG_CORE inline void put_code(Writer& w,u32 entry){put_bits(w,entry&0xffffu,entry>>16);}
TH10_JPEG_CORE inline u32 avg565(u32 a,u32 b){return (a&b)+(((a^b)&0xf7deu)>>1);}   // per-channel floor average of two RGB565 pixels
TH10_JPEG_CORE inline u32 bit_count(u32 a){return a?32u-u32(__builtin_clz(a)):0u;}

// Forward DCT in place, AAN "ifast" (IJG jfdctfst.c, 8-bit constants, plain
// right shift). The outputs carry the AAN scale factors; the divisors fold them in.
TH10_JPEG_CORE inline i32 mul8(i32 v,i32 c){return (v*c)>>8;}
TH10_JPEG_CORE inline void fdct_ifast(i32* d){
    for(u32 pass=0;pass<2;pass++){
        const u32 step=pass?8u:1u,next=pass?1u:8u;   // rows, then columns
        i32* p=d;
        for(u32 i=0;i<8;i++,p+=next){
            const i32 tmp0=p[0]+p[7*step],tmp7=p[0]-p[7*step],tmp1=p[step]+p[6*step],tmp6=p[step]-p[6*step];
            const i32 tmp2=p[2*step]+p[5*step],tmp5=p[2*step]-p[5*step],tmp3=p[3*step]+p[4*step],tmp4=p[3*step]-p[4*step];
            i32 tmp10=tmp0+tmp3,tmp13=tmp0-tmp3,tmp11=tmp1+tmp2,tmp12=tmp1-tmp2;
            p[0]=tmp10+tmp11;p[4*step]=tmp10-tmp11;
            const i32 z1=mul8(tmp12+tmp13,181);      // c4
            p[2*step]=tmp13+z1;p[6*step]=tmp13-z1;
            tmp10=tmp4+tmp5;tmp11=tmp5+tmp6;tmp12=tmp6+tmp7;
            const i32 z5=mul8(tmp10-tmp12,98);       // c6
            const i32 z2=mul8(tmp10,139)+z5;         // c2-c6
            const i32 z4=mul8(tmp12,334)+z5;         // c2+c6
            const i32 z3=mul8(tmp11,181);            // c4
            const i32 z11=tmp7+z3,z13=tmp7-z3;
            p[5*step]=z13+z2;p[3*step]=z13-z2;p[step]=z11+z4;p[7*step]=z11-z4;
        }
    }
}
// One 8x8 block of level-shifted samples -> Huffman bits. A block whose 64
// samples are equal skips the DCT: the ifast DCT of a constant c is exactly
// {64c, 0, ...} (every odd/even difference term is 0), so the bits are the same.
TH10_JPEG_CORE inline void encode_block(Writer& w,i32* blk,const Tables& t,u32 cls,i32& dc_pred,u32& flat){
    i32 zz[64];u32 last=0;
    const i32 v0=blk[0];u32 i=1;
    while(i<64&&blk[i]==v0)++i;
    if(i==64){
        ++flat;
        const i32 x=v0*64;u32 a=u32(x<0?-x:x);
        a=((a+t.corr[cls][0])*t.recip[cls][0])>>t.shift[cls][0];
        zz[0]=x<0?-i32(a):i32(a);
    }else{
        fdct_ifast(blk);
        for(u32 k=0;k<64;k++){
            const i32 x=blk[t.natural[k]];u32 a=u32(x<0?-x:x);
            a=((a+t.corr[cls][k])*t.recip[cls][k])>>t.shift[cls][k];
            const i32 q=x<0?-i32(a):i32(a);
            zz[k]=q;if(q)last=k;
        }
    }
    {   // DC difference
        i32 v=zz[0]-dc_pred;dc_pred=zz[0];
        u32 a=u32(v);if(v<0){a=u32(-v);v-=1;}
        const u32 nb=bit_count(a);
        put_code(w,t.dc_code[cls][nb]);
        if(nb)put_bits(w,u32(v)&((1u<<nb)-1u),nb);
    }
    u32 run=0;
    for(u32 k=1;k<=last;k++){
        i32 v=zz[k];
        if(!v){++run;continue;}
        while(run>15){put_code(w,t.ac_code[cls][0xf0]);run-=16;}
        u32 a=u32(v);if(v<0){a=u32(-v);v-=1;}
        const u32 nb=bit_count(a);
        put_code(w,t.ac_code[cls][(run<<4)|nb]);
        put_bits(w,u32(v)&((1u<<nb)-1u),nb);
        run=0;
    }
    if(last<63)put_code(w,t.ac_code[cls][0]);   // EOB
}
// Pixels are the GE's GU_PSM_5650: red in bits 0-4, green 5-10, blue 11-15 (the
// first device recording, 2026-10-01, had red and blue swapped with the other order).
TH10_JPEG_CORE inline u32 red565(u32 p){return p&31u;}
TH10_JPEG_CORE inline u32 green565(u32 p){return (p>>5)&63u;}
TH10_JPEG_CORE inline u32 blue565(u32 p){return p>>11;}
TH10_JPEG_CORE inline u16 pack565(u32 r,u32 g,u32 b){return u16((b<<11)|(g<<5)|r);}
TH10_JPEG_CORE inline i32 luma(const Tables& t,u32 p){return (t.y_r[red565(p)]+t.y_g[green565(p)]+t.y_b[blue565(p)])>>16;}
// Chroma from the sums of the raw fields of 4 pixels (R and B 0..124, G 0..252):
// JFIF constants x 255/31 (or 255/63) / 4 in Q16, rounded, upper end clamped to 127.
TH10_JPEG_CORE inline i32 chroma_cb(i32 sr,i32 sg,i32 sb){const i32 v=(-22741*sr-21968*sg+67386*sb+32768)>>16;return v>127?127:v;}
TH10_JPEG_CORE inline i32 chroma_cr(i32 sr,i32 sg,i32 sb){const i32 v=(67386*sr-27766*sg-10958*sb+32768)>>16;return v>127?127:v;}
// 16x16 RGB565 (rows past rows_valid repeat the last valid row) -> 4 Y blocks + Cb + Cr (4:2:0).
TH10_JPEG_CORE inline void fetch_420(const Tables& t,const u16* src,u32 stride,u32 rows_valid,i32* y4,i32* cb,i32* cr){
    for(u32 gr=0;gr<8;gr++){
        u32 r0=2u*gr,r1=r0+1u;if(r0>=rows_valid)r0=rows_valid-1u;if(r1>=rows_valid)r1=rows_valid-1u;
        const u16* s0=src+r0*stride;const u16* s1=src+r1*stride;
        i32* ybase=y4+(gr>>2)*128u+((2u*gr)&7u)*8u;
        for(u32 gc=0;gc<8;gc++){
            const u32 c=2u*gc,p00=s0[c],p01=s0[c+1],p10=s1[c],p11=s1[c+1];
            i32* yb=ybase+(gc>>2)*64u+(c&7u);
            yb[0]=luma(t,p00);yb[1]=luma(t,p01);yb[8]=luma(t,p10);yb[9]=luma(t,p11);
            const i32 sr=i32(red565(p00)+red565(p01)+red565(p10)+red565(p11));
            const i32 sg=i32(green565(p00)+green565(p01)+green565(p10)+green565(p11));
            const i32 sb=i32(blue565(p00)+blue565(p01)+blue565(p10)+blue565(p11));
            cb[gr*8u+gc]=chroma_cb(sr,sg,sb);cr[gr*8u+gc]=chroma_cr(sr,sg,sb);
        }
    }
}
// 16x8 RGB565 -> 2 Y blocks + Cb + Cr (4:2:2, PC test set only).
TH10_JPEG_CORE inline void fetch_422(const Tables& t,const u16* src,u32 stride,u32 rows_valid,i32* y2,i32* cb,i32* cr){
    for(u32 r=0;r<8;r++){
        u32 rs=r;if(rs>=rows_valid)rs=rows_valid-1u;
        const u16* s=src+rs*stride;
        for(u32 gc=0;gc<8;gc++){
            const u32 c=2u*gc,p0=s[c],p1=s[c+1];
            i32* yb=y2+(gc>>2)*64u+r*8u+(c&7u);
            yb[0]=luma(t,p0);yb[1]=luma(t,p1);
            const i32 sr=i32(2u*(red565(p0)+red565(p1))),sg=i32(2u*(green565(p0)+green565(p1))),sb=i32(2u*(blue565(p0)+blue565(p1)));
            cb[r*8u+gc]=chroma_cb(sr,sg,sb);cr[r*8u+gc]=chroma_cr(sr,sg,sb);
        }
    }
}

// Start a frame: header bytes, empty bit buffer, DC predictors 0.
TH10_JPEG_CORE inline void frame_begin(State& s,const Tables& t,u8* out,u32 cap){
    s.acc=0;s.nbits=0;s.dc0=s.dc1=s.dc2=0;s.blocks=0;s.flat=0;s.pos=0;s.overflow=0;
    if(cap<t.header_bytes+mcu_worst_bytes){s.overflow=1;return;}
    for(u32 i=0;i<t.header_bytes;i++)out[i]=t.header[i];
    s.pos=t.header_bytes;
}
// MCUs [0, ncols) of one MCU row: `src` is the top-left pixel of the first one
// (RGB565, `stride` pixels per line), `rows_valid` lines of the MCU row exist.
TH10_JPEG_CORE inline void encode_range(State& s,const Tables& t,u8* out,u32 cap,const u16* src,u32 stride,u32 ncols,u32 rows_valid){
    if(s.overflow)return;
    Writer w;w.p=out+s.pos;w.end=out+cap;w.acc=s.acc;w.n=s.nbits;
    i32 dc0=s.dc0,dc1=s.dc1,dc2=s.dc2;u32 flat=s.flat,blocks=s.blocks;
    i32 y4[256],cb[64],cr[64];
    for(u32 m=0;m<ncols;m++){
        if(u32(w.end-w.p)<mcu_worst_bytes){s.overflow=1;break;}
        const u16* mcu=src+m*16u;
        if(t.vsamp==2u){
            fetch_420(t,mcu,stride,rows_valid,y4,cb,cr);
            encode_block(w,y4,t,0,dc0,flat);encode_block(w,y4+64,t,0,dc0,flat);encode_block(w,y4+128,t,0,dc0,flat);encode_block(w,y4+192,t,0,dc0,flat);
            blocks+=6;
        }else{
            fetch_422(t,mcu,stride,rows_valid,y4,cb,cr);
            encode_block(w,y4,t,0,dc0,flat);encode_block(w,y4+64,t,0,dc0,flat);
            blocks+=4;
        }
        encode_block(w,cb,t,1,dc1,flat);encode_block(w,cr,t,1,dc2,flat);
    }
    s.pos=u32(w.p-out);s.acc=w.acc;s.nbits=w.n;s.dc0=dc0;s.dc1=dc1;s.dc2=dc2;s.flat=flat;s.blocks=blocks;
}
// End a frame: pad the last byte with 1 bits, EOI.
TH10_JPEG_CORE inline void frame_end(State& s,u8* out,u32 cap){
    if(s.overflow)return;
    if(cap-s.pos<4u){s.overflow=1;return;}
    Writer w;w.p=out+s.pos;w.end=out+cap;w.acc=s.acc;w.n=s.nbits;
    if(w.n)put_bits(w,(1u<<(8u-w.n))-1u,8u-w.n);
    *w.p++=0xffu;*w.p++=0xd9u;
    s.pos=u32(w.p-out);s.acc=0;s.nbits=0;
}
// 3:2 downscale (the 320x240 path): output lines [y0, y0+16), columns
// [x0, x0+w) of the 320-wide letterboxed picture. Active lines
// [scale_top, scale_top+scale_active) come from the 480x272 source, two taps
// per axis with overlap (output pair 2k,2k+1 <- source 3k..3k+2), packed
// RGB565 floor averages; other lines are black.
TH10_JPEG_CORE inline void scale32_strip(const Tables& t,const u16* src,u32 y0,u32 x0,u32 w,u16* dst,u32 dst_stride){
    for(u32 r=0;r<16;r++){
        u16* d=dst+r*dst_stride;const u32 y=y0+r;
        if(y<t.scale_top||y>=t.scale_top+t.scale_active){for(u32 i=0;i<w;i++)d[i]=0;continue;}
        const u32 j=y-t.scale_top,sy0=3u*(j>>1)+(j&1u);u32 sy1=sy0+1u;if(sy1>=t.src_h)sy1=t.src_h-1u;
        const u16* a=src+sy0*t.src_w;const u16* b=src+sy1*t.src_w;
        for(u32 i=0;i<w;i++){
            const u32 x=x0+i,sx0=3u*(x>>1)+(x&1u),sx1=sx0+1u;
            d[i]=u16(avg565(avg565(a[sx0],a[sx1]),avg565(b[sx0],b[sx1])));
        }
    }
}
// Source lines that scale32_strip(y0) reads: [first, first+count) (count 0: none).
TH10_JPEG_CORE inline void scale32_source_lines(const Tables& t,u32 y0,u32& first,u32& count){
    u32 lo=y0,hi=y0+16u;
    if(lo<t.scale_top)lo=t.scale_top;
    if(hi>t.scale_top+t.scale_active)hi=t.scale_top+t.scale_active;
    if(lo>=hi){first=0;count=0;return;}
    const u32 j0=lo-t.scale_top,j1=hi-1u-t.scale_top;
    first=3u*(j0>>1)+(j0&1u);u32 last=3u*(j1>>1)+(j1&1u)+1u;if(last>=t.src_h)last=t.src_h-1u;
    count=last-first+1u;
}

// ------------------------------------------------- tables (SC and PC only) --
// Annex K.1 (natural order) and K.3, IJG jcparam.c / jstdhuff.c.
struct Std {
    static const u8* luma_q(){static const u8 v[64]={16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
        18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};return v;}
    static const u8* chroma_q(){static const u8 v[64]={17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
        99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};return v;}
    static const u8* zigzag(){static const u8 v[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
        35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};return v;}
    static const u16* aan(){static const u16 v[64]={16384,22725,21407,19266,16384,12873,8867,4520,22725,31521,29692,26722,22725,17855,12299,6270,
        21407,29692,27969,25172,21407,16819,11585,5906,19266,26722,25172,22654,19266,15137,10426,5315,16384,22725,21407,19266,16384,12873,8867,4520,
        12873,17855,16819,15137,12873,10114,6967,3552,8867,12299,11585,10426,8867,6967,4799,2446,4520,6270,5906,5315,4520,3552,2446,1247};return v;}
    // bits[16] then values
    static const u8* dc_luma(){static const u8 v[16+12]={0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,11};return v;}
    static const u8* dc_chroma(){static const u8 v[16+12]={0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,11};return v;}
    static const u8* ac_luma(){static const u8 v[16+162]={0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d,
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
        0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
        0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
        0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
        0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};return v;}
    static const u8* ac_chroma(){static const u8 v[16+162]={0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77,
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
        0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
        0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
        0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};return v;}
};
// Annex C: code lengths -> codes. Fills table[value] = (size << 16) | code.
inline void build_huffman(const u8* spec,u32 count,u32* table,u32 entries){
    for(u32 i=0;i<entries;i++)table[i]=0;
    u32 code=0,k=16;
    for(u32 len=1;len<=16;len++){for(u32 n=0;n<spec[len-1];n++){table[spec[k++]]=(len<<16)|code;++code;}code<<=1;}
    (void)count;
}
// libjpeg-turbo jcdctmgr.c compute_reciprocal, with the total shift stored.
inline void reciprocal(u32 divisor,u16& recip,u16& corr,u16& shift){
    if(divisor<=1u){recip=1;corr=0;shift=0;return;}
    u32 b=0;while((divisor>>(b+1u))!=0u)++b;   // floor(log2)
    u32 r=16u+b;u32 fq=(1u<<r)/divisor;const u32 fr=(1u<<r)%divisor;u32 c=divisor/2u;
    if(fr==0u){fq>>=1;--r;}else if(fr<=divisor/2u)++c;else ++fq;
    recip=u16(fq);corr=u16(c);shift=u16(r);
}
// Quality 1..100 -> table (jcparam.c jpeg_quality_scaling, force_baseline).
inline void scaled_quant(const u8* base,u32 quality,u8* out){
    if(quality<1u)quality=1u;
    if(quality>100u)quality=100u;
    const u32 scale=quality<50u?5000u/quality:200u-quality*2u;
    for(u32 i=0;i<64;i++){u32 v=(base[i]*scale+50u)/100u;if(v<1u)v=1u;if(v>255u)v=255u;out[i]=u8(v);}
}
struct HeaderOut {u8* p;void b(u32 v){*p++=u8(v);} void w(u32 v){*p++=u8(v>>8);*p++=u8(v);}};
// Build every table and the header. vsamp 2 = 4:2:0, 1 = 4:2:2. dht=false
// leaves out the DHT segments (decoders then use the Annex K tables).
// src_w/src_h/scale_top/scale_active: the 3:2 path (0 when unused).
inline void build_tables(Tables& t,u32 quality,u32 width,u32 height,u32 vsamp,bool dht,u32 src_w=0,u32 src_h=0,u32 scale_top=0,u32 scale_active=0){
    for(u32 v=0;v<32;v++){const i32 r8=i32((v<<3)|(v>>2));t.y_r[v]=19595*r8;t.y_b[v]=7471*r8+32768-(128<<16);}
    for(u32 v=0;v<64;v++){const i32 g8=i32((v<<2)|(v>>4));t.y_g[v]=38470*g8;}
    u8 q[2][64];scaled_quant(Std::luma_q(),quality,q[0]);scaled_quant(Std::chroma_q(),quality,q[1]);
    const u8* zz=Std::zigzag();const u16* aan=Std::aan();
    for(u32 k=0;k<64;k++){
        t.natural[k]=zz[k];
        for(u32 c=0;c<2;c++){const u32 n=zz[k],divisor=(u32(q[c][n])*aan[n]+1024u)>>11;reciprocal(divisor,t.recip[c][k],t.corr[c][k],t.shift[c][k]);}
    }
    build_huffman(Std::dc_luma(),12,t.dc_code[0],16);build_huffman(Std::dc_chroma(),12,t.dc_code[1],16);
    build_huffman(Std::ac_luma(),162,t.ac_code[0],256);build_huffman(Std::ac_chroma(),162,t.ac_code[1],256);
    t.width=width;t.height=height;t.vsamp=vsamp;t.mcu_h=8u*vsamp;t.mcus_x=(width+15u)/16u;t.mcus_y=(height+t.mcu_h-1u)/t.mcu_h;
    t.src_w=src_w;t.src_h=src_h;t.scale_top=scale_top;t.scale_active=scale_active;t.quality=quality;t.dht=dht?1u:0u;t.pad=0;
    HeaderOut h{t.header};
    h.w(0xffd8);
    h.w(0xffe0);h.w(16);h.b('J');h.b('F');h.b('I');h.b('F');h.b(0);h.b(1);h.b(1);h.b(0);h.w(1);h.w(1);h.b(0);h.b(0);   // JFIF 1.01, aspect 1:1
    for(u32 c=0;c<2;c++){h.w(0xffdb);h.w(67);h.b(c);for(u32 k=0;k<64;k++)h.b(q[c][zz[k]]);}
    h.w(0xffc0);h.w(17);h.b(8);h.w(height);h.w(width);h.b(3);
    h.b(1);h.b(vsamp==2u?0x22:0x21);h.b(0);h.b(2);h.b(0x11);h.b(1);h.b(3);h.b(0x11);h.b(1);
    if(dht){
        const u8* specs[4]={Std::dc_luma(),Std::ac_luma(),Std::dc_chroma(),Std::ac_chroma()};const u32 counts[4]={12,162,12,162},ids[4]={0x00,0x10,0x01,0x11};
        for(u32 s=0;s<4;s++){h.w(0xffc4);h.w(3u+16u+counts[s]);h.b(ids[s]);for(u32 i=0;i<16u+counts[s];i++)h.b(specs[s][i]);}
    }
    h.w(0xffda);h.w(12);h.b(3);h.b(1);h.b(0x00);h.b(2);h.b(0x11);h.b(3);h.b(0x11);h.b(0);h.b(63);h.b(0);
    t.header_bytes=u32(h.p-t.header);
}

// ------------------------------------------------- self-test pattern (SC/PC) --
// Deterministic RGB565 picture with flat tiles (flat-block path, EOB),
// gradients, hash noise (long AC codes), 1-pixel stripes every 3rd column and
// a checkerboard (high frequencies), saturated colours (chroma clamp).
// Shifts, multiplies and counters only (no division): the SC fills it a few
// rows per game tick (test_pattern_rows) without stalling the game thread.
inline u32 pattern_hash(u32 x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline void test_pattern_rows(u16* out,u32 w,u32 h,u32 stride,u32 y0,u32 y1){
    const u32 hw=w>>1,hh=h>>1;
    for(u32 y=y0;y<y1&&y<h;y++){
        u32 x3=0;   // x mod 3
        u32 y5=y;while(y5>=5u)y5-=5u;   // y mod 5 (at most h/5 steps per row)
        for(u32 x=0;x<w;x++){
            u32 r,g,b;
            if(y<hh&&x<hw){r=(x>>3)&31u;g=(y>>1)&63u;b=((x+y)>>2)&31u;}                                  // gradients
            else if(y<hh){const u32 v=pattern_hash(y*4099u+x);r=v&31u;g=(v>>5)&63u;b=(v>>11)&31u;if((x&63u)<16u){r=31;g=0;b=0;}}   // noise + red bars
            else if(x<hw){const u32 tile=((y>>5)*7u+(x>>5)*3u)&7u;r=(tile&1u)?31u:(tile*3u);g=(tile&2u)?63u:(tile*7u);b=(tile&4u)?31u:0u;}   // flat tiles
            else{r=x3==0u?31u:0u;g=(((x>>1)^(y>>1))&1u)?63u:8u;b=y5==0u?31u:2u;}                          // stripes / checkerboard
            out[y*stride+x]=pack565(r,g,b);
            if(++x3==3u)x3=0;
        }
    }
}
inline void test_pattern(u16* out,u32 w,u32 h,u32 stride){test_pattern_rows(out,w,h,stride,0,h);}
inline u32 fnv_bytes(const u8* p,u32 n){u32 h=2166136261u;for(u32 i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}

// One-shot encode of a whole frame through the job API with `mcus_per_job`
// MCUs per call (the split the ME uses). For the SC/PC. `scaled` is a
// 16 x width scratch when t.scale_active != 0 (src is then the 480x272 frame).
inline u32 encode_frame(const Tables& t,const u16* src,u32 stride,u8* out,u32 cap,u32 mcus_per_job,u16* scaled,State& s){
    frame_begin(s,t,out,cap);
    for(u32 row=0;row<t.mcus_y;row++){
        const u32 rows_valid=t.height-row*t.mcu_h<t.mcu_h?t.height-row*t.mcu_h:t.mcu_h;
        for(u32 c0=0;c0<t.mcus_x;c0+=mcus_per_job){
            const u32 n=t.mcus_x-c0<mcus_per_job?t.mcus_x-c0:mcus_per_job;
            if(t.scale_active){scale32_strip(t,src,row*16u,c0*16u,n*16u,scaled,n*16u);encode_range(s,t,out,cap,scaled,n*16u,n,rows_valid);}
            else encode_range(s,t,out,cap,src+row*t.mcu_h*stride+c0*16u,stride,n,rows_valid);
        }
    }
    frame_end(s,out,cap);
    return s.overflow?0u:s.pos;
}
}
