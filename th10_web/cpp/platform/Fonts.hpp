#pragma once
#include "TextureResample.hpp"
#include "../game/FontResources.hpp"
#include "../game/AnmText.hpp"
namespace th10::browser {
struct FontHost {
    virtual u32 bitmap(const BitmapDescription& description,u8** pixels)=0;
    virtual u32 context()=0;
    virtual u32 select(u32 context,u32 object)=0;
    virtual void delete_context(u32 context)=0;
    virtual void delete_object(u32 object)=0;
    virtual u32 font(i32 height,const char* utf8_face,u32 charset)=0;
    virtual void background(u32 context,u32 mode)=0;
    virtual void color(u32 context,u32 value)=0;
    virtual void text(u32 context,i32 x,i32 y,const char* bytes,u32 length)=0;
#if TH10_FAST_TEXT
    virtual bool mark_text(i32 rows){(void)rows;return false;}
    virtual u32 marked_text(TextureRect& box){box=TextureRect{0,0,0,0};return 0;}
#endif
};
struct Fonts final:FontEnvironment,TextRasterEnvironment,AnmTextEnvironment {
    FontHost& host;GraphicsDevice& device;Rng& random;RasterImage image{};u32 handles[15]{},draw_handles[15]{},display=0;Textures textures;
    Fonts(FontHost& host,GraphicsDevice& device,Rng& random,bool chinese);
    void initialize();void release();
    u32 select_object(u32 context,u32 object) override {return host.select(context,object);}
    void delete_context(u32 context) override {host.delete_context(context);}
    void delete_object(u32 object) override {host.delete_object(object);}
    u32 create_bitmap(const BitmapDescription& description,u8** pixels) override {return host.bitmap(description,pixels);}
    u32 create_context() override {return host.context();}
    u32 create_font(i32 height,const char* face,u32 charset) override {return host.font(height,face,charset);}
    u32 select_font(u32 context,u32 font) override {return host.select(context,font);}
    void transparent_background(u32 context) override {host.background(context,1);}
    void text_color(u32 context,u32 color) override {host.color(context,color);}
    void text_out(u32 context,i32 x,i32 y,const char* text,u32 length) override {host.text(context,x,y,text,length);}
    void* get_surface(void* texture) override {return textures.get_surface(texture);}
    void upload(void* surface,const TextureRect& destination,const RasterImage& bitmap,const TextureRect& source) override;
    void release_surface(void* surface) override {textures.release_surface(surface);}
#if TH10_TEXT_AUDIT
    void rasterize(const TextureRect& rectangle,i32 offset,i32 size,u32 color,const char* text,void* texture,bool flat) override;
#else
    void rasterize(const TextureRect& rectangle,i32 offset,i32 size,u32 color,const char* text,void* texture,bool flat) override {TextRaster::draw(rectangle,offset,size,color,text,texture,flat,*this);}
#endif
#if TH10_FAST_TEXT
    bool begin_marked_text(i32 rows) override {return host.mark_text(rows);}
    u32 end_marked_text(TextureRect& box) override {return host.marked_text(box);}
    void upload_content(void* surface,const TextureRect& destination,const RasterImage& bitmap,const TextureRect& source,const TextureRect& content) override;
#endif
};
}
