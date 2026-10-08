// Checks TH10_FAST_TEXT (game/AnmText.cpp TextRaster::draw_marked, RasterImage
// fill_transparent_edges_box, TextureResample::triangle_content, the marking
// and divide255 in native/HeadlessFonts.cpp) against TextRaster::draw_reference:
// for each case both paths start from the same garbage bitmap and the same
// random 512x512 A4R4G4B4 surface, and the whole surface and the whole host
// bitmap (padding rows included) must be byte-identical afterwards.
// Cases: the dialogue lines of a TH10_TEXT_LOG (optional argument), then random
// strings from the glyph file (SJIS, ASCII, spaces, missing glyphs), sizes
// 0/9..31, offsets -60..420, flat, dst rectangles near and over the edges,
// and hosts without marking (the original path must run).
// Also compares the host blend (divide255) with the old divisions on every input.
//   cmake --build <dir> --target th10_text_raster_check
//   TH10_GAME_DIR=<dir with th10_font32.bin> <dir>/th10_text_raster_check [text.log] [random cases]
#include "../../th10_web/cpp/game/AnmText.hpp"
#include "../../th10_web/cpp/game/FontResources.hpp"
#include "../../th10_web/cpp/platform/TextureResample.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace th10;
extern "C" {
u32 fonts_bitmap(const BitmapDescription*,u8**);u32 fonts_context();u32 fonts_select(u32,u32);u32 fonts_font(i32,const char*,u32);
void fonts_background(u32,u32);void fonts_color(u32,u32);void fonts_text(u32,i32,i32,const char*,u32);
u32 fonts_text_mark(i32);u32 fonts_text_marked(i32*);u32 headless_font_text_calls();
unsigned th10_current_tick(void){return 0;}
}
// TextureResample's equal-size path (never taken by text: source width = 2*dst+22).
namespace th10::browser { u32 pixel_copies=0;i32 PixelCopy::copy(PixelSurface&,const TextureRect&,const u8*,u32,i32,const TextureRect&) noexcept {++pixel_copies;return 0;} }
namespace {
u64 state=0x9e3779b97f4a7c15ull;u32 next(){state^=state<<13;state^=state>>7;state^=state<<17;return u32(state>>16);}
u32 below(u32 n){return n?next()%n:0;}
struct Env final:TextRasterEnvironment {
    bool marking=true;u8* surface=nullptr;
    u32 select_font(u32 c,u32 f) override{return fonts_select(c,f);}
    void transparent_background(u32 c) override{fonts_background(c,1);}
    void text_color(u32 c,u32 v) override{fonts_color(c,v);}
    void text_out(u32 c,i32 x,i32 y,const char* t,u32 n) override{fonts_text(c,x,y,t,n);}
    void* get_surface(void* t) override{return t;}
    void release_surface(void*) override{}
    PixelSurface out(){return PixelSurface{26,512,512,1024,surface};}
    PixelSurface in(const RasterImage& b){return PixelSurface{b.format,u32(b.width),u32(b.height),b.pitch,b.pixels};}
    void upload(void*,const TextureRect& d,const RasterImage& b,const TextureRect& s) override{auto o=out();browser::TextureResample::triangle(o,d,in(b),s);}
    bool begin_marked_text(i32 rows) override{return marking&&fonts_text_mark(rows);}
    u32 end_marked_text(TextureRect& box) override{i32 b[4];const u32 n=fonts_text_marked(b);box=TextureRect{b[0],b[1],b[2],b[3]};return n;}
    void upload_content(void*,const TextureRect& d,const RasterImage& b,const TextureRect& s,const TextureRect& c) override{++content_uploads;auto o=out();browser::TextureResample::triangle_content(o,d,in(b),s,c);}
    u32 content_uploads=0;
};
std::vector<u16> glyph_codes(){
    std::vector<u16> codes;const char* dir=std::getenv("TH10_GAME_DIR");const std::string path=std::string(dir&&*dir?dir:"game")+"/th10_font32.bin";
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return codes;std::vector<u8> head(24);if(std::fread(head.data(),1,24,f)==24){u32 count;std::memcpy(&count,head.data()+16,4);
        std::vector<u8> table(size_t(count)*12);if(std::fread(table.data(),1,table.size(),f)==table.size())for(u32 i=0;i<count;++i){u16 c;std::memcpy(&c,table.data()+size_t(i)*12,2);codes.push_back(c);}}
    std::fclose(f);return codes;
}
u32 old_blend(u32 before,u32 target,u32 coverage){const u32 source=(target*15+127)/255;return (before*(255-coverage)+source*coverage+127)/255;}   // HeadlessFonts.cpp, verbatim
u32 new_blend(u32 before,u32 target,u32 coverage){const u32 level=(target*15+127)/255;const u32 n=before*(255-coverage)+level*coverage+127;return (n+1+(n>>8))>>8;}  // TH10_FAST_TEXT
}
int main(int argc,char** argv){
    u64 blend_bad=0;for(u32 b=0;b<16;++b)for(u32 t=0;t<256;++t)for(u32 c=0;c<256;++c)if(old_blend(b,t,c)!=new_blend(b,t,c))++blend_bad;
    std::printf("blend: %u inputs, %llu mismatches\n",16u*256u*256u,(unsigned long long)blend_bad);
    const auto codes=glyph_codes();if(codes.empty()){std::printf("no glyph file (TH10_GAME_DIR): random strings draw nothing\n");}
    // bitmap like FontResources (1024x64, A4R4G4B4, top-down), fonts 32..60
    BitmapDescription d{};d.size=sizeof(d);d.width=1024;d.height=~63;d.planes=1;d.bits=16;d.byte_size=2048*64;u8* storage=nullptr;
    const u32 bitmap=fonts_bitmap(&d,&storage);const u32 context=fonts_context();fonts_select(context,bitmap);
    RasterImage image{};image.format=26;image.width=1024;image.height=64;image.pitch=2048;image.byte_size=2048*64;image.device_context=context;image.bitmap_handle=bitmap;image.pixels=storage;
    const size_t bitmap_bytes=size_t(2048)*(64+4);   // HeadlessFonts allocates 4 extra rows
    u32 fonts[15];for(u32 i=0;i<15;++i)fonts[i]=fonts_font(32+i*2,"",128);
    Env env;env.bitmap=&image;env.fonts=fonts;
    std::vector<u8> s_ref(512*1024),s_new(512*1024),b_ref(bitmap_bytes),start_surface(512*1024),start_bitmap(bitmap_bytes);
    struct Case {std::string text;i32 size,offset;u32 color;bool flat,marking;TextureRect dst;};
    std::vector<Case> cases;
    if(argc>1)if(FILE* f=std::fopen(argv[1],"r")){char line[4096];while(std::fgets(line,sizeof line,f)){
        int size=0,flat=0,off=0,l=0,t=0,r=0,b=0;const char* p=std::strstr(line," size=");const char* q=std::strstr(line," text=");const char* dd=std::strstr(line," dst=");
        if(!p||!q||!dd||std::sscanf(p," size=%d flat=%d off=%d",&size,&flat,&off)!=3||std::sscanf(dd," dst=%d,%d,%d,%d",&l,&t,&r,&b)!=4)continue;
        std::string text;for(const char* h=q+6;h[0]&&h[1]&&h[0]!='\n';h+=2){unsigned v;std::sscanf(h,"%2x",&v);text+=char(v);}
        cases.push_back({text,size,off,0xf8f08fu,flat!=0,true,TextureRect{l,t,r,b}});}std::fclose(f);}
    const u32 logged=u32(cases.size());const u32 random_cases=argc>2?u32(std::atoi(argv[2])):20000;
    // size >= 32 is left out: rows = 2*size+6 > 68 makes the ORIGINAL invert_alpha
    // write past the host bitmap (64+4 rows); the fast path falls back above 64 rows.
    static const i32 sizes[]={0,9,12,16,17,18,20,24,28,29,30,31};
    for(u32 n=0;n<random_cases;++n){
        Case c;const u32 len=below(5)==0?1:below(48);
        for(u32 i=0;i<len;++i){const u32 kind=below(10);
            if(kind<6&&!codes.empty()){const u16 code=codes[below(u32(codes.size()))];if(code>255)c.text+=char(code>>8);c.text+=char(code&255);}
            else if(kind<8)c.text+=char(below(2)?' ':(0x21+below(94)));else if(kind<9){c.text+=char(0x81);c.text+=char(0x40);}   // ideographic space
            else{c.text+=char(0x88);c.text+=char(0x40+below(60));}}                                                              // maybe missing
        c.size=sizes[below(12)];c.offset=below(4)==0?i32(below(480))-60:0;c.color=next()&0xffffff;if(below(8)==0)c.color=0;c.flat=below(10)==0;c.marking=below(10)!=0;
        const i32 w=1+i32(below(5)==0?below(600):below(360)),h=1+i32(below(40)),l=i32(below(5)==0?below(520):below(160)),t=i32(below(5)==0?below(520):below(480));
        c.dst=TextureRect{l,t,l+w,t+h};if(below(20)==0)c.dst=TextureRect{0,1,354,18};
        cases.push_back(c);
    }
    u64 surface_bad=0,bitmap_bad=0,content_uploads=0,fallbacks=0;
    for(u32 n=0;n<cases.size();++n){const auto& c=cases[n];
        for(auto& v:start_surface)v=u8(next());for(auto& v:start_bitmap)v=u8(next());
        // reference
        std::memcpy(s_ref.data(),start_surface.data(),s_ref.size());std::memcpy(storage,start_bitmap.data(),bitmap_bytes);env.surface=s_ref.data();env.marking=false;
        TextRaster::draw_reference(c.dst,c.offset,c.size,c.color,c.text.c_str(),nullptr,c.flat,env);std::memcpy(b_ref.data(),storage,bitmap_bytes);
        // TH10_FAST_TEXT dispatch (fast path, or the reference when it does not apply)
        std::memcpy(s_new.data(),start_surface.data(),s_new.size());std::memcpy(storage,start_bitmap.data(),bitmap_bytes);env.surface=s_new.data();env.marking=c.marking;
        const u32 before=env.content_uploads;TextRaster::draw(c.dst,c.offset,c.size,c.color,c.text.c_str(),nullptr,c.flat,env);
        if(env.content_uploads!=before)++content_uploads;else ++fallbacks;
        const bool sb=std::memcmp(s_ref.data(),s_new.data(),s_ref.size())!=0,bb=std::memcmp(b_ref.data(),storage,bitmap_bytes)!=0;
        if(sb||bb){if(surface_bad+bitmap_bad<10)std::printf("MISMATCH case %u (%s) size=%d off=%d flat=%d marking=%d dst=%d,%d,%d,%d surface=%d bitmap=%d len=%zu\n",n,n<logged?"log":"random",c.size,c.offset,c.flat,c.marking,c.dst.left,c.dst.top,c.dst.right,c.dst.bottom,sb,bb,c.text.size());surface_bad+=sb;bitmap_bad+=bb;}
    }
    std::printf("cases: %zu (%u from the log, %u random); fast path %llu, original path %llu; text_out calls %u; mismatches: surface %llu, bitmap %llu; equal-size copies %u\n",
        cases.size(),logged,random_cases,(unsigned long long)content_uploads,(unsigned long long)fallbacks,headless_font_text_calls(),(unsigned long long)surface_bad,(unsigned long long)bitmap_bad,browser::pixel_copies);
    const bool ok=!blend_bad&&!surface_bad&&!bitmap_bad&&content_uploads>0;std::printf("%s\n",ok?"OK":"FAIL");return ok?0:1;
}
