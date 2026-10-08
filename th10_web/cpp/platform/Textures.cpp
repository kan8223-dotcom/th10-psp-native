#include "Textures.hpp"
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
#include <vector>
#endif
namespace th10::browser {
namespace {
u32 address(const void* value){return static_cast<u32>(reinterpret_cast<uintptr_t>(value));}
struct Format {u32 bytes,red,green,blue,alpha,red_shift,green_shift,blue_shift,alpha_shift,unused;};
Format format(u32 value){switch(value){
    case 20:return {3,255,255,255,0,16,8,0,0,0};
    case 21:return {4,255,255,255,255,16,8,0,24,0};
    case 22:return {4,255,255,255,0,16,8,0,0,0};
    case 23:return {2,31,63,31,0,11,5,0,0,0};
    case 24:return {2,31,31,31,0,10,5,0,0,0};
    case 25:return {2,31,31,31,1,10,5,0,15,0};
    case 26:return {2,15,15,15,15,8,4,0,12,0};
    default:return {};
}}
u32 quantize(u32 pixel,u32 mask,u32 shift,u32 destination){if(!mask)return destination;return (((pixel>>shift)&mask)*destination+mask/2)/mask;}
}
i32 PixelCopy::copy(PixelSurface& output,const TextureRect& target,const u8* source,u32 source_format,i32 source_pitch,const TextureRect& region) noexcept {
    const auto in=format(source_format),out=format(output.format);constexpr i32 invalid=static_cast<i32>(0x8876086cu);
    if(!in.bytes||!out.bytes||!source||!output.pixels||target.left<0||target.top<0||target.right<=target.left||target.bottom<=target.top||static_cast<u32>(target.right)>output.width||static_cast<u32>(target.bottom)>output.height||region.left<0||region.top<0||region.right<=region.left||region.bottom<=region.top||region.right-region.left!=target.right-target.left||region.bottom-region.top!=target.bottom-target.top||source_pitch<=0||output.pitch<=0||static_cast<u64>(region.right)*in.bytes>static_cast<u32>(source_pitch)||static_cast<u64>(target.right)*out.bytes>static_cast<u32>(output.pitch))return invalid;
    const u32 width=target.right-target.left,height=target.bottom-target.top;
    for(u32 y=0;y<height;++y){const auto* row=source+(region.top+y)*source_pitch+region.left*in.bytes;auto* to=output.pixels+(target.top+y)*output.pitch+target.left*out.bytes;
        if(source_format==output.format){std::memmove(to,row,width*in.bytes);continue;}
        for(u32 x=0;x<width;++x){u32 pixel=0;std::memcpy(&pixel,row+x*in.bytes,in.bytes);
            const u32 value=out.unused|(quantize(pixel,in.red,in.red_shift,out.red)<<out.red_shift)|(quantize(pixel,in.green,in.green_shift,out.green)<<out.green_shift)|(quantize(pixel,in.blue,in.blue_shift,out.blue)<<out.blue_shift)|(quantize(pixel,in.alpha,in.alpha_shift,out.alpha)<<out.alpha_shift);
            std::memcpy(to+x*out.bytes,&value,out.bytes);
        }
    }
    return 0;
}
i32 Textures::create_surface_texture(AnmTexture& texture,u32 width,u32 height,u32 pixel_format){
    if(!width||!height||width>4096||height>4096)return static_cast<i32>(0x8876086cu);
    if(pixel_format==0)pixel_format=21;if(!format(pixel_format).bytes)return static_cast<i32>(0x8876086cu);
    return device.create_texture(width,height,pixel_format,texture.handle);
}
i32 Textures::decode_source_texture(AnmTexture&,u32,u32,u32,u32){
    // Every supplied ANM stores THTX pixels or declares an empty texture.
    // Fail explicitly for external encoded images until that API is supported.
    return static_cast<i32>(0x88760b59u);
}
void* Textures::get_surface(void* texture){return device.texture_surface(texture);}
TextureDescription Textures::describe_surface(void* surface){return device.describe_surface(surface);}
TextureLock Textures::lock_surface(void* surface){return device.map_surface(surface);}
void Textures::unlock_surface(void* surface){device.unmap_surface(surface);}
void Textures::release_surface(void* surface){device.release_resource(surface);}
i32 Textures::upload_surface(void* surface,const u8* bytes,u32 pixel_format,i32 pitch,const TextureRect& rectangle){const auto desc=describe_surface(surface);const auto lock=lock_surface(surface);PixelSurface output{desc.format,desc.width,desc.height,lock.pitch,lock.pixels};const auto result=PixelCopy::copy(output,rectangle,bytes,pixel_format,pitch,rectangle);unlock_surface(surface);return result;}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// th10_port: upload_surface's conversion (PixelCopy::copy) into a plain buffer of rows.
i32 Textures::convert_pixels(u8* output,u32 output_format,const u8* source,u32 source_format,u32 width,u32 rows){
    const auto in=format(source_format),out=format(output_format);if(!in.bytes||!out.bytes||!width||!rows)return static_cast<i32>(0x8876086cu);
    PixelSurface surface{output_format,width,rows,static_cast<i32>(width*out.bytes),output};const TextureRect rectangle{0,0,static_cast<i32>(width),static_cast<i32>(rows)};
    return PixelCopy::copy(surface,rectangle,source,source_format,static_cast<i32>(width*in.bytes),rectangle);
}
#endif
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
}
#ifdef TH_NATIVE_PLATFORM
extern "C" th10::i32 native_graphics_create_indexed_texture(th10::u32 width,th10::u32 height,th10::u32 format,void*& out,th10::u8*& indices,uint16_t*& palette);
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
extern "C" th10::i32 native_graphics_create_lazy_texture(th10::u32 width,th10::u32 height,th10::u32 format,void*& out);
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
extern "C" th10::i32 native_graphics_create_padded_texture(th10::u32 width,th10::u32 height,th10::u32 format,void*& out,const th10::u8* image,th10::u32 columns,th10::u32 rows,th10::u32 pixel_bytes,const uint16_t* palette,th10::u32 fill);
#endif
#endif
namespace {
// th10_port (TH10_TEXTURE_CLUT8, PSP-1000 lane; the lossless path of TH08's
// psp1000 psp/clut8_quantize.hpp, but the indices are written into their own
// 1-byte-per-pixel image instead of over a 16-bit one, so a texture is never
// allocated twice): created, refused for more than 256 colours, refused for
// its shape or format (the 16-bit path then runs as before).
th10::u32 indexed_created=0,indexed_too_many=0,indexed_unsuitable=0;bool indexed_enabled=true;
// The distinct 16-bit values in first-seen order, 256 at most. A slot holds
// value+1 (0 is empty), so the colour 0 counts like any other.
struct ColourSet {
    th10::u32 slots[1024];th10::u8 index_of[1024];uint16_t colours[256];th10::u32 count=0;
    ColourSet(){std::memset(slots,0,sizeof(slots));}
    // The index of `value`, added if new; -1 for a 257th colour.
    th10::i32 add(uint16_t value){
        th10::u32 h=((th10::u32(value)*40503u)>>6)&1023u;
        for(;;){if(!slots[h]){if(count==256)return -1;slots[h]=th10::u32(value)+1;index_of[h]=th10::u8(count);colours[count]=value;return th10::i32(count++);}
            if(slots[h]==th10::u32(value)+1)return index_of[h];h=(h+1)&1023u;}
    }
};
}
// PixelCopy::copy's values for the pairs TH10's ANM textures use - A8R8G8B8 to
// 4444/1555/565 and a 16-bit format to itself - from 256-entry tables of its
// rounding ((v*dst+127)/255) instead of four divisions per pixel; any other
// pair takes PixelCopy::copy itself. The PC audit compares every result with
// the 16-bit path.
struct RowConverter {
    th10::u32 source,output;bool fast;uint16_t a[256],r[256],g[256],b[256];
    RowConverter(th10::u32 source_format,th10::u32 output_format):source(source_format),output(output_format){
        fast=source==output?(output==23||output==25||output==26):source==21&&(output==23||output==25||output==26);
        if(!fast||source==output)return;
        for(th10::u32 v=0;v<256;++v){
            if(output==26){a[v]=uint16_t(((v*15+127)/255)<<12);r[v]=uint16_t(((v*15+127)/255)<<8);g[v]=uint16_t(((v*15+127)/255)<<4);b[v]=uint16_t((v*15+127)/255);}
            else if(output==25){a[v]=uint16_t(((v+127)/255)<<15);r[v]=uint16_t(((v*31+127)/255)<<10);g[v]=uint16_t(((v*31+127)/255)<<5);b[v]=uint16_t((v*31+127)/255);}
            else{a[v]=0;r[v]=uint16_t(((v*31+127)/255)<<11);g[v]=uint16_t(((v*63+127)/255)<<5);b[v]=uint16_t((v*31+127)/255);}
        }
    }
};
extern "C" void th10_indexed_counts(th10::u32* out){out[0]=indexed_created;out[1]=indexed_too_many;out[2]=indexed_unsuitable;}
extern "C" void th10_indexed_enable(int enabled){indexed_enabled=enabled!=0;}   // the PC audit compares against the 16-bit path
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
namespace {th10::u32 padded_indexed=0,padded_wide=0,padded_refused=0;bool padded_enabled=true;}
extern "C" void th10_padded_counts(th10::u32* out){out[0]=padded_indexed;out[1]=padded_wide;out[2]=padded_refused;}
extern "C" void th10_padded_enable(int enabled){padded_enabled=enabled!=0;}   // the PC audit's reference load makes them at once
#endif
namespace th10::browser {
// The pixels are the 16-bit values upload_surface writes (PixelCopy::copy,
// row by row); the part of the canvas outside the upload stays 0 there, so 0
// is one more colour then. GE limits: rows of 16-byte multiples, 512 square.
i32 Textures::create_indexed_texture(AnmTexture& texture,u32 width,u32 height,u32 output_format,const u8* bytes,u32 source_format,i32 pitch,const TextureRect& r){
#ifdef TH_NATIVE_PLATFORM
    const auto in=format(source_format),out=format(output_format);
    if(!indexed_enabled||!in.bytes||out.bytes!=2||(output_format!=23&&output_format!=25&&output_format!=26)||width<16||(width&15u)||width>512||!height||height>512||width*height<2048){++indexed_unsuitable;return -1;}
    // What PixelCopy::copy refuses for this upload (the surface then stays 0) keeps the 16-bit path.
    if(!bytes||r.left||r.top||r.right<=0||r.bottom<=0||u32(r.right)>width||u32(r.bottom)>height||pitch<=0||u64(u32(r.right))*in.bytes>u32(pitch)){++indexed_unsuitable;return -1;}
    const u32 sw=u32(r.right),sh=u32(r.bottom);uint16_t row[512];ColourSet colours;const RowConverter fast(source_format,output_format);
    auto convert=[&](u32 y){
        const u8* line_in=bytes+size_t(y)*u32(pitch);
        if(fast.fast&&fast.source==fast.output){std::memcpy(row,line_in,sw*2);return true;}
        if(fast.fast){for(u32 x=0;x<sw;++x){const u8* p=line_in+x*4;row[x]=uint16_t(fast.a[p[3]]|fast.r[p[2]]|fast.g[p[1]]|fast.b[p[0]]);}return true;}
        PixelSurface line{output_format,sw,1,i32(sw*2),reinterpret_cast<u8*>(row)};const TextureRect to{0,0,i32(sw),1},from{0,i32(y),i32(sw),i32(y)+1};return PixelCopy::copy(line,to,bytes,source_format,pitch,from)==0;};
    for(u32 y=0;y<sh;++y){if(!convert(y)){++indexed_unsuitable;return -1;}for(u32 x=0;x<sw;++x)if(colours.add(row[x])<0){++indexed_too_many;return -1;}}
    const bool margin=sw<width||sh<height;if(margin&&colours.add(0)<0){++indexed_too_many;return -1;}
    u8* indices=nullptr;uint16_t* palette=nullptr;if(native_graphics_create_indexed_texture(width,height,output_format,texture.handle,indices,palette))return -1;
    const u8 zero=margin?u8(colours.add(0)):0;
    for(u32 y=0;y<sh;++y){convert(y);u8* line=indices+y*width;for(u32 x=0;x<sw;++x)line[x]=u8(colours.add(row[x]));if(sw<width)std::memset(line+sw,zero,width-sw);}
    if(sh<height)std::memset(indices+sh*width,zero,(height-sh)*width);
    for(u32 i=0;i<256;++i)palette[i]=i<colours.count?colours.colours[i]:0;
    ++indexed_created;return 0;
#else
    (void)texture;(void)width;(void)height;(void)output_format;(void)bytes;(void)source_format;(void)pitch;(void)r;return -1;
#endif
}
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
#if !TH10_TEXTURE_CLUT8
#error "TH10_TEXTURE_LAZY_EMPTY needs TH10_TEXTURE_CLUT8 (it samples through the CLUT8 path)"
#endif
// th10_port (TH10_TEXTURE_LAZY_EMPTY): a 16-bit '@' texture (the text atlas,
// the capture target) starts with no pixel bytes; until the first lock, copy
// or rectangle write it is all 0, which the GE samples from a shared block of
// zeros (sdl/GraphicsHost.cpp make_lazy_texture). Same checks and ids as
// create_surface_texture; 32-bit ones are created as before.
i32 Textures::create_empty_texture(AnmTexture& texture,u32 width,u32 height,u32 pixel_format){
#ifdef TH_NATIVE_PLATFORM
    if(width&&height&&width<=4096&&height<=4096&&(pixel_format==23||pixel_format==25||pixel_format==26))return native_graphics_create_lazy_texture(width,height,pixel_format,texture.handle);
#endif
    return create_surface_texture(texture,width,height,pixel_format);
}
#endif
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
#if !TH10_TEXTURE_LAZY_EMPTY
#error "TH10_TEXTURE_LAZY_PADDED needs TH10_TEXTURE_LAZY_EMPTY (the host's textures without bytes)"
#endif
// th10_port (TH10_TEXTURE_LAZY_PADDED): what create_indexed_texture would make -
// its checks, its colours in first-seen order, 0 for the canvas outside the
// image - or else what create_surface_texture + upload_surface would make,
// handed to the host as the image rows only (indices + table + the index of 0,
// or 16-bit rows): sdl/GraphicsHost.cpp builds the whole texture from them on
// the first draw, lock, copy or write (an 8x8 face/dummy.png: 64 bytes instead
// of 64-128 KiB). Nonzero: nothing created.
i32 Textures::create_padded_texture(AnmTexture& texture,u32 width,u32 height,u32 output_format,const u8* bytes,u32 source_format,i32 pitch,const TextureRect& r){
#ifdef TH_NATIVE_PLATFORM
    const auto in=format(source_format),out=format(output_format);
    if(!padded_enabled||!in.bytes||out.bytes!=2||(output_format!=23&&output_format!=25&&output_format!=26)||!width||!height||width>4096||height>4096||
       !bytes||r.left||r.top||r.right<=0||r.bottom<=0||u32(r.right)>width||u32(r.bottom)>height||u32(r.right)>512||pitch<=0||u64(u32(r.right))*in.bytes>u32(pitch)){++padded_refused;return -1;}
    const u32 sw=u32(r.right),sh=u32(r.bottom);uint16_t row[512];const RowConverter fast(source_format,output_format);
    auto convert=[&](u32 y){
        const u8* line_in=bytes+size_t(y)*u32(pitch);
        if(fast.fast&&fast.source==fast.output){std::memcpy(row,line_in,sw*2);return true;}
        if(fast.fast){for(u32 x=0;x<sw;++x){const u8* p=line_in+x*4;row[x]=uint16_t(fast.a[p[3]]|fast.r[p[2]]|fast.g[p[1]]|fast.b[p[0]]);}return true;}
        PixelSurface line{output_format,sw,1,i32(sw*2),reinterpret_cast<u8*>(row)};const TextureRect to{0,0,i32(sw),1},from{0,i32(y),i32(sw),i32(y)+1};return PixelCopy::copy(line,to,bytes,source_format,pitch,from)==0;};
    std::vector<u8> image;
    if(indexed_enabled&&width>=16&&!(width&15u)&&width<=512&&height<=512&&width*height>=2048){
        ColourSet colours;bool fits=true;image.resize(size_t(sw)*sh);
        for(u32 y=0;y<sh&&fits;++y){if(!convert(y)){++padded_refused;return -1;}for(u32 x=0;x<sw;++x){const i32 k=colours.add(row[x]);if(k<0){fits=false;break;}image[size_t(y)*sw+x]=u8(k);}}
        const bool margin=sw<width||sh<height;if(fits&&margin&&colours.add(0)<0)fits=false;
        if(fits){uint16_t palette[256];for(u32 i=0;i<256;++i)palette[i]=i<colours.count?colours.colours[i]:0;
            if(native_graphics_create_padded_texture(width,height,output_format,texture.handle,image.data(),sw,sh,1,palette,margin?u32(colours.add(0)):0u))return -1;
            ++padded_indexed;return 0;}
    }
    image.resize(size_t(sw)*sh*2);
    for(u32 y=0;y<sh;++y){if(!convert(y)){++padded_refused;return -1;}std::memcpy(image.data()+size_t(y)*sw*2,row,sw*2);}
    if(native_graphics_create_padded_texture(width,height,output_format,texture.handle,image.data(),sw,sh,2,nullptr,0))return -1;
    ++padded_wide;return 0;
#else
    (void)texture;(void)width;(void)height;(void)output_format;(void)bytes;(void)source_format;(void)pitch;(void)r;return -1;
#endif
}
#endif
#endif
}
