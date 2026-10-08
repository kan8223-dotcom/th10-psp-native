// Font host with the GDI contract of th10_web/cpp/sdl/FontHost.cpp. TextOut
// draws pre-rendered glyphs (th10_font32.bin next to th10.dat: the release's
// Noto table, or one made from the user's own MS Gothic with tools/fonts/)
// with the same A4R4G4B4 blending as the SDL host. Without the file it draws
// nothing, like the headless host always did.
#include "../th10_web/cpp/platform/Fonts.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <memory>
#include <vector>
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
#include "psp/VolatileArena.hpp"
#endif
using namespace th10;
namespace {
struct Object {
    enum Kind{Bitmap,Context,Font} kind;u32 font=0,bitmap=0,color=0,mode=2,charset=0;
    int width=0,height=0,pitch=0,bpp=0;std::vector<u8> pixels;
};
std::map<u32,std::unique_ptr<Object>> objects;u32 next=1,text_calls=0,missing_glyphs=0;
// th10_font32.bin: "T10F", u32 version, height, ascent, count, data bytes;
// count x {u16 sjis, u8 w, u8 h, i8 left, i8 top (from the ascender line),
// u8 advance, u8 pad, u32 offset}; then 8-bit coverage.
#if defined(__PSP__) && defined(TH10_VOLATILE_ARENA) && TH10_VOLATILE_ARENA
using GlyphBytes=std::vector<u8,VolatileAllocator<u8>>;   // PSP-1000 lane: the ~1 MB file outside the heap (heap when it does not fit)
#else
using GlyphBytes=std::vector<u8>;
#endif
struct Glyphs {GlyphBytes file;u32 count=0;const u8* table=nullptr;const u8* data=nullptr;bool tried=false;} glyphs;
void load_glyphs(){
    if(glyphs.tried)return;glyphs.tried=true;
    const char* dir=std::getenv("TH10_GAME_DIR");const std::string path=std::string(dir&&*dir?dir:"game")+"/th10_font32.bin";
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return;std::fseek(f,0,SEEK_END);const long size=std::ftell(f);std::fseek(f,0,SEEK_SET);
    if(size>24){glyphs.file.resize(size_t(size));if(std::fread(glyphs.file.data(),1,size_t(size),f)!=size_t(size))glyphs.file.clear();}
    std::fclose(f);
    if(glyphs.file.size()<24||std::memcmp(glyphs.file.data(),"T10F",4)){glyphs.file.clear();return;}
    u32 count,bytes;std::memcpy(&count,glyphs.file.data()+16,4);std::memcpy(&bytes,glyphs.file.data()+20,4);
    if(24+size_t(count)*12+bytes>glyphs.file.size()){glyphs.file.clear();return;}
    glyphs.count=count;glyphs.table=glyphs.file.data()+24;glyphs.data=glyphs.table+size_t(count)*12;
}
const u8* find_glyph(u32 code){
    u32 lo=0,hi=glyphs.count;
    while(lo<hi){const u32 mid=(lo+hi)/2;u16 c;std::memcpy(&c,glyphs.table+size_t(mid)*12,2);if(c==code)return glyphs.table+size_t(mid)*12;if(c<code)lo=mid+1;else hi=mid;}
    return nullptr;
}
// Same arithmetic as FontHost.cpp: coverage blends each 4-bit channel and the
// alpha nibble is cleared (TextRaster::draw() inverts it afterwards).
u32 blend_channel_4444(u32 before,u32 target,u32 coverage){const u32 source=(target*15+127)/255;return (before*(255-coverage)+source*coverage+127)/255;}
#if TH10_FAST_TEXT
// th10_port: TH08 psp/text_mask_blit.hpp TextDivide255 (exact floor(n/255) for
// n <= 65025; here n <= 15*255+127) instead of two divisions per channel, with
// the per-call source level hoisted. text_raster_check.cpp compares all inputs.
inline u32 divide255(u32 n){return (n+1+(n>>8))>>8;}
inline u32 blend_level_4444(u32 before,u32 source,u32 coverage){return divide255(before*(255-coverage)+source*coverage+127);}
// Marking (TextRaster::draw_marked): covered pixels of rows < mark_rows also
// get alpha 0xF; their count and half-open box are reported.
i32 mark_rows=-1,mark_box[4];u32 mark_count=0;
#endif
Object& get(u32 id){auto it=objects.find(id);if(it==objects.end())std::abort();return *it->second;}
u32 add(Object::Kind kind){const auto id=next++;auto value=std::make_unique<Object>();value->kind=kind;objects[id]=std::move(value);return id;}
}
extern "C" {
u32 fonts_bitmap(const BitmapDescription* d,u8** out){const auto* bytes=reinterpret_cast<const u8*>(d);i32 width,height;u16 bpp;std::memcpy(&width,bytes+4,4);std::memcpy(&height,bytes+8,4);std::memcpy(&bpp,bytes+14,2);height=std::abs(height);if(width<=0||height<=0||bpp!=16||uint64_t(width)*height>16777216)return 0;
    const auto id=add(Object::Bitmap);auto& b=get(id);b.width=width;b.height=height;b.bpp=bpp;b.pitch=((width*bpp+31)>>5)*4;b.pixels.resize(size_t(b.pitch)*(height+4));*out=b.pixels.data();return id;
}
u32 fonts_context(){return add(Object::Context);}
u32 fonts_select(u32 context,u32 id){auto& dc=get(context);auto it=objects.find(id);if(it==objects.end())return 0;u32& target=it->second->kind==Object::Font?dc.font:dc.bitmap;const auto old=target;target=id;return old;}
void fonts_delete_context(u32 id){objects.erase(id);}
void fonts_delete_object(u32 id){objects.erase(id);}
u32 fonts_font(i32 height,const char*,u32 charset){const auto id=add(Object::Font);auto& f=get(id);f.charset=charset;f.height=height;return id;}
void fonts_background(u32 id,u32 mode){get(id).mode=mode;}
void fonts_color(u32 id,u32 color){get(id).color=color;}
extern "C" unsigned th10_current_tick(void);
unsigned th10_current_tick_for_log(){return th10_current_tick();}
// PC inventory (TH10_FONT_LOG=<file>): one line per TextOut, "height x y hex-bytes".
void fonts_text(u32 id,i32 x,i32 y,const char* bytes,u32 length){text_calls++;
    {auto& dc=get(id);load_glyphs();
    if(glyphs.count&&dc.bitmap&&objects.count(dc.bitmap)&&get(dc.bitmap).bpp==16){
        auto& b=get(dc.bitmap);const u32 r=dc.color&255,g=(dc.color>>8)&255,blue=(dc.color>>16)&255;i32 pen=x;
#if TH10_FAST_TEXT
        const u32 level_r=(r*15+127)/255,level_g=(g*15+127)/255,level_b=(blue*15+127)/255;
#endif
        for(u32 i=0;i<length;){
            const u32 c=u8(bytes[i]);const bool lead=(c>=0x81&&c<=0x9f)||(c>=0xe0&&c<=0xfc);u32 code=c;
            if(lead&&i+1<length){code=(c<<8)|u8(bytes[i+1]);i+=2;}else i+=1;
            const u8* e=find_glyph(code);if(!e){++missing_glyphs;pen+=lead?32:16;continue;}
            const u32 w=e[2],h=e[3];const i32 left=static_cast<std::int8_t>(e[4]),top=static_cast<std::int8_t>(e[5]);u32 offset;std::memcpy(&offset,e+8,4);const u8* cov=glyphs.data+offset;
            for(u32 j=0;j<h;j++){const i32 row=y+top+i32(j);if(row<0||row>=b.height)continue;
                for(u32 k=0;k<w;k++){const i32 column=pen+left+i32(k);if(column<0||column>=b.width)continue;const u32 coverage=cov[j*w+k];if(!coverage)continue;
                    u8* target=b.pixels.data()+size_t(row)*b.pitch+size_t(column)*2;const u32 before=target[0]|(u32(target[1])<<8);
#if TH10_FAST_TEXT
                    u16 result=u16((blend_level_4444((before>>8)&15,level_r,coverage)<<8)|(blend_level_4444((before>>4)&15,level_g,coverage)<<4)|blend_level_4444(before&15,level_b,coverage));
                    if(row<mark_rows){result|=0xf000u;++mark_count;if(column<mark_box[0])mark_box[0]=column;if(row<mark_box[1])mark_box[1]=row;if(column>=mark_box[2])mark_box[2]=column+1;if(row>=mark_box[3])mark_box[3]=row+1;}
#else
                    const u16 result=u16((blend_channel_4444((before>>8)&15,r,coverage)<<8)|(blend_channel_4444((before>>4)&15,g,coverage)<<4)|blend_channel_4444(before&15,blue,coverage));
#endif
                    target[0]=u8(result);target[1]=u8(result>>8);}}
            pen+=e[6];
        }
#ifndef __PSP__
        // TH10_FONT_DUMP=<dir>: raw A4R4G4B4 image after the first few foreground runs.
        static int dumps=0;if(const char* dir=std::getenv("TH10_FONT_DUMP"))if(dc.color&&length>2&&dumps<4){char path[512];std::snprintf(path,sizeof(path),"%s/dib_%d.raw",dir,dumps++);
            if(FILE* f=std::fopen(path,"wb")){const i32 header[3]{b.width,b.height,b.pitch};std::fwrite(header,4,3,f);std::fwrite(b.pixels.data(),1,size_t(b.pitch)*b.height,f);std::fclose(f);}}
#endif
    }}
#ifndef __PSP__
    static FILE* log=nullptr;static bool tried=false;if(!tried){tried=true;if(const char* path=std::getenv("TH10_FONT_LOG"))log=std::fopen(path,"w");}
    if(log){auto& dc=get(id);const int height=dc.font&&objects.count(dc.font)?get(dc.font).height:0;std::fprintf(log,"%d %d %d ",height,x,y);
        extern unsigned th10_current_tick_for_log();std::fprintf(log,"t%u ",th10_current_tick_for_log());
        for(u32 i=0;i<length;i++)std::fprintf(log,"%02x",unsigned(u8(bytes[i])));std::fprintf(log,"\n");std::fflush(log);}
#else
    (void)id;(void)x;(void)y;(void)bytes;(void)length;
#endif
}
#if TH10_FAST_TEXT
u32 fonts_text_mark(i32 rows){mark_rows=rows;mark_box[0]=mark_box[1]=0x7fffffff;mark_box[2]=mark_box[3]=-1;mark_count=0;return 1;}
u32 fonts_text_marked(i32* box){if(mark_count){for(int i=0;i<4;i++)box[i]=mark_box[i];}else{box[0]=box[1]=box[2]=box[3]=0;}mark_rows=-1;return mark_count;}
#endif
u32 sdl_fonts_errors(){return 0;}
void sdl_fonts_shutdown(){objects.clear();}
u32 headless_font_text_calls(){return text_calls;}
u32 headless_font_missing_glyphs(){return missing_glyphs;}
}
