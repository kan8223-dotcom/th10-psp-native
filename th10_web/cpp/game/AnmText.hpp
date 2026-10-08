#pragma once
#include "AnmFile.hpp"
#include "RasterImage.hpp"
#include "TexturePlatform.hpp"
namespace th10 {
enum class TextAlignment { Left,Right,Center };
struct TextRasterEnvironment {
    RasterImage* bitmap;
    const u32* fonts; // Increasing sizes 17..31; platform font handles.
    virtual u32 select_font(u32 context,u32 font)=0;
    virtual void transparent_background(u32 context)=0;
    virtual void text_color(u32 context,u32 color)=0;
    virtual void text_out(u32 context,i32 x,i32 y,const char* text,u32 length)=0;
    virtual void* get_surface(void* texture)=0;
    virtual void upload(void* surface,const TextureRect& destination,const RasterImage& bitmap,const TextureRect& source)=0;
    virtual void release_surface(void* surface)=0;
#if TH10_FAST_TEXT
    // th10_port: optional host support. While marked, TextOut also sets alpha
    // 0xF on the covered pixels of rows < `rows` (what the two invert_alpha
    // passes around it produce) and end_marked_text() returns their count and
    // half-open box. Hosts without it return false: the original path runs.
    virtual bool begin_marked_text(i32 rows){(void)rows;return false;}
    virtual u32 end_marked_text(TextureRect& box){box=TextureRect{0,0,0,0};return 0;}
    // `content`: bitmap pixels outside it are zero (a hint; a platform may ignore it).
    virtual void upload_content(void* surface,const TextureRect& destination,const RasterImage& bitmap,const TextureRect& source,const TextureRect& content){(void)content;upload(surface,destination,bitmap,source);}
#endif
};
struct TextRaster {
    static void draw(const TextureRect& rectangle,i32 offset,i32 font_size,u32 color,const char* text,void* texture,bool flat,TextRasterEnvironment& environment);
#if TH10_FAST_TEXT
    static void draw_reference(const TextureRect& rectangle,i32 offset,i32 font_size,u32 color,const char* text,void* texture,bool flat,TextRasterEnvironment& environment);
    static bool draw_marked(const TextureRect& rectangle,i32 offset,i32 font_size,u32 color,const char* text,void* texture,TextRasterEnvironment& environment);
#endif
};
struct AnmTextEnvironment {
    virtual void rasterize(const TextureRect& rectangle,i32 offset,i32 font_size,u32 color,const char* text,void* texture,bool flat)=0;
};
struct AnmText {
    static void draw(AnmVm& animation,u32 color,const char* text,TextAlignment alignment,AnmTextEnvironment& environment);
    static void draw_sprite(const AnmSprite& sprite,void* texture,i32 offset,i32 font_size,u32 color,const char* text,bool flat,AnmTextEnvironment& environment);
};
}
