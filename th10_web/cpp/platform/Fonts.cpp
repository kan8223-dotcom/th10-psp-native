#include "Fonts.hpp"
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
#include <cstdlib>
#include <cstring>
#endif
#if TH10_TEXT_AUDIT
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif
namespace th10::browser {
namespace {
constexpr RasterFormat formats[]={
    {22,32,0,0xff0000,0xff00,0xff},{21,32,0xff000000,0xff0000,0xff00,0xff},
    {24,16,0,0x7c00,0x3e0,0x1f},{23,16,0,0xf800,0x7e0,0x1f},
    {25,16,0x8000,0x7c00,0x3e0,0x1f},{26,16,0xf000,0xf00,0xf0,0xf},{0xffffffff,0,0,0,0,0}
};
}
Fonts::Fonts(FontHost& h,GraphicsDevice& d,Rng& rng,bool chinese):host(h),device(d),random(rng),textures(d,display){RasterEnvironment::formats=browser::formats;face=chinese?"SimHei":u8"ＭＳ ゴシック";charset=chinese?134:128;TextRasterEnvironment::bitmap=&image;TextRasterEnvironment::fonts=draw_handles;image.initialize();}
void Fonts::initialize(){FontResources{image,random,handles}.initialize(*this);for(u32 i=0;i<15;++i)draw_handles[i]=handles[14-i];}
void Fonts::release(){FontResources{image,random,handles}.release(*this);}
#if TH10_TEXT_AUDIT
// th10_port audit (PC): TH10_TEXT_LOG=<file> writes one line per text upload:
// tick, size, flat, rows, dst rect, surface w/h/format, the work counts, and
// FNV-1a hashes of the destination rectangle, the whole surface and the bitmap.
namespace { struct AuditDraw {const char* text=nullptr;i32 offset=0,size=0;bool flat=false;} audit_draw; FILE* audit_log=nullptr;bool audit_tried=false;
u32 fnv(const u8* p,size_t n,u32 h=2166136261u){for(size_t i=0;i<n;++i){h^=p[i];h*=16777619u;}return h;}
}
extern "C" unsigned th10_current_tick(void);
static void audit_upload(const TextureDescription& desc,const TextureLock& lock,const TextureRect& d,const RasterImage& bitmap,const TextureRect& source,const TextureRect* content,i32 result){
    if(!audit_tried){audit_tried=true;if(const char* path=std::getenv("TH10_TEXT_LOG"))audit_log=std::fopen(path,"w");}
    if(!audit_log)return;
    u32 hd=2166136261u;if(lock.pixels&&d.right>d.left&&d.bottom>d.top&&d.left>=0&&d.top>=0&&u32(d.bottom)<=desc.height)for(i32 y=d.top;y<d.bottom;++y)hd=fnv(lock.pixels+y*lock.pitch+d.left*2,size_t(d.right-d.left)*2,hd);
    const u32 hs=lock.pixels?fnv(lock.pixels,size_t(lock.pitch)*desc.height):0,hb=fnv(bitmap.pixels,size_t(bitmap.byte_size));
    // covered (opaque) and nonzero bitmap pixels inside the source rectangle, and the opaque box over all rows
    u32 opaque=0,nonzero=0;i32 bl=1<<30,bt=1<<30,br=-1,bb=-1;
    for(i32 y=0;y<bitmap.height;++y)for(i32 x=0;x<bitmap.width;++x){u16 v;std::memcpy(&v,bitmap.pixels+y*bitmap.pitch+x*2,2);
        if(v&0xf000){if(x<bl)bl=x;if(y<bt)bt=y;if(x+1>br)br=x+1;if(y+1>bb)bb=y+1;if(y<source.bottom&&x<source.right)++opaque;}
        if(v&&y<source.bottom&&x<source.right)++nonzero;}
    std::fprintf(audit_log,"t%u size=%d flat=%d off=%d len=%u dst=%d,%d,%d,%d src=%d,%d surf=%ux%u/f%u/p%d res=%d opaque_src=%u nonzero_src=%u obox=%d,%d,%d,%d content=%s%d,%d,%d,%d unpacked=%llu nzpx=%llu mac=%llu packed=%llu zerostored=%llu hd=%08x hs=%08x hb=%08x text=",
        th10_current_tick(),audit_draw.size,audit_draw.flat,audit_draw.offset,unsigned(audit_draw.text?std::strlen(audit_draw.text):0),d.left,d.top,d.right,d.bottom,source.right,source.bottom,desc.width,desc.height,desc.format,lock.pitch,result,opaque,nonzero,
        br<0?0:bl,br<0?0:bt,br<0?0:br,br<0?0:bb,content?"":"-",content?content->left:0,content?content->top:0,content?content->right:0,content?content->bottom:0,
        (unsigned long long)text_audit_counts[0],(unsigned long long)text_audit_counts[1],(unsigned long long)text_audit_counts[2],(unsigned long long)text_audit_counts[3],(unsigned long long)text_audit_counts[4],hd,hs,hb);
    if(audit_draw.text)for(const char* c=audit_draw.text;*c;++c)std::fprintf(audit_log,"%02x",unsigned(u8(*c)));
    std::fprintf(audit_log,"\n");std::fflush(audit_log);
}
#endif
#if TH10_TEXT_AUDIT
void Fonts::rasterize(const TextureRect& rectangle,i32 offset,i32 size,u32 color,const char* text,void* texture,bool flat){
    audit_draw={text,offset,size,flat};
    TextRaster::draw(rectangle,offset,size,color,text,texture,flat,*this);
    audit_draw={};
}
#endif
void Fonts::upload(void* surface,const TextureRect& destination,const RasterImage& bitmap,const TextureRect& source){
    const auto desc=textures.describe_surface(surface);const auto lock=textures.lock_surface(surface);
    PixelSurface output{desc.format,desc.width,desc.height,lock.pitch,lock.pixels},input{bitmap.format,static_cast<u32>(bitmap.width),static_cast<u32>(bitmap.height),bitmap.pitch,bitmap.pixels};
#if TH10_TEXT_AUDIT
    for(auto& c:text_audit_counts)c=0;
    const i32 result=TextureResample::triangle(output,destination,input,source);audit_upload(desc,lock,destination,bitmap,source,nullptr,result);
#else
    TextureResample::triangle(output,destination,input,source);
#endif
    textures.unlock_surface(surface);
}
#if TH10_FAST_TEXT
void Fonts::upload_content(void* surface,const TextureRect& destination,const RasterImage& bitmap,const TextureRect& source,const TextureRect& content){
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD && !TH10_TEXT_AUDIT
    // th10_port (TH10_FAST_TEXT_UPLOAD): the resample writes only the destination
    // rectangle and never reads it, so it goes to a buffer of that size and the
    // platform writes the rectangle (on the PSP straight into the texture's GE
    // form, GeRenderer.cpp write_rect); a platform without that takes a lock.
    {   const auto desc=textures.describe_surface(surface);const i32 w=destination.right-destination.left,h=destination.bottom-destination.top;
        u8* buffer=desc.format==26&&w>0&&h>0&&w<=1024&&h<=256?static_cast<u8*>(std::malloc(static_cast<size_t>(w)*static_cast<size_t>(h)*2)):nullptr;
        if(buffer){
            PixelSurface output{desc.format,static_cast<u32>(w),static_cast<u32>(h),w*2,buffer},input{bitmap.format,static_cast<u32>(bitmap.width),static_cast<u32>(bitmap.height),bitmap.pitch,bitmap.pixels};
            const TextureRect local{0,0,w,h};
            if(TextureResample::triangle_content(output,local,input,source,content)==0&&!textures.write_surface(surface,destination,buffer,w*2)){
                const auto lock=textures.lock_surface(surface);
                for(i32 y=0;y<h;y++)std::memcpy(lock.pixels+(destination.top+y)*lock.pitch+destination.left*2,buffer+static_cast<size_t>(y)*static_cast<size_t>(w)*2,static_cast<size_t>(w)*2);
                textures.unlock_surface(surface);
            }
            std::free(buffer);return;
        }
    }
#endif
    const auto desc=textures.describe_surface(surface);const auto lock=textures.lock_surface(surface);
    PixelSurface output{desc.format,desc.width,desc.height,lock.pitch,lock.pixels},input{bitmap.format,static_cast<u32>(bitmap.width),static_cast<u32>(bitmap.height),bitmap.pitch,bitmap.pixels};
#if TH10_TEXT_AUDIT
    for(auto& c:text_audit_counts)c=0;
    const i32 result=TextureResample::triangle_content(output,destination,input,source,content);audit_upload(desc,lock,destination,bitmap,source,&content,result);
#else
    TextureResample::triangle_content(output,destination,input,source,content);
#endif
    textures.unlock_surface(surface);
}
#endif
}
