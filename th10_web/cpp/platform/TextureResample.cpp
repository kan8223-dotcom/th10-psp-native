#include "TextureResample.hpp"
#include "../game/TriangleCoefficients.hpp"
#include "../game/Argb4444.hpp"
#include <array>
#include <cstdlib>
namespace th10::browser {
#if TH10_TEXT_AUDIT
u64 text_audit_counts[8];
#define TEXT_AUDIT_COUNT(i,n) (text_audit_counts[i]+=(n))
#else
#define TEXT_AUDIT_COUNT(i,n) ((void)0)
#endif
namespace {
struct PixelFormat {u32 bytes,masks[4],shifts[4];};
PixelFormat layout(u32 format){switch(format){
    case 20:return {3,{255,255,255,0},{16,8,0,0}};
    case 21:return {4,{255,255,255,255},{16,8,0,24}};
    case 22:return {4,{255,255,255,0},{16,8,0,0}};
    case 23:return {2,{31,63,31,0},{11,5,0,0}};
    case 24:return {2,{31,31,31,0},{10,5,0,0}};
    case 25:return {2,{31,31,31,1},{10,5,0,15}};
    case 26:return {2,{15,15,15,15},{8,4,0,12}};
    default:return {};
}}
u32 word(const u8* bytes){u32 value;std::memcpy(&value,bytes,4);return value;}
bool rectangle(const PixelSurface& image,const TextureRect& r,const PixelFormat& format){return format.bytes&&image.pixels&&image.pitch>0&&r.left>=0&&r.top>=0&&r.right>r.left&&r.bottom>r.top&&static_cast<u32>(r.right)<=image.width&&static_cast<u32>(r.bottom)<=image.height&&static_cast<u64>(r.right)*format.bytes<=static_cast<u32>(image.pitch);}
struct CachedFilter {
    u32 source=0,destination=0;bool wrap=false;u64 used=0;u8* bytes=nullptr;
    ~CachedFilter(){std::free(bytes);}
};
const u8* filter(u32 source,u32 destination,bool wrap,bool native_float,u8*& owned){
    // Text repeatedly uses the same small dimensions. Cache coefficients only
    // in single/nearest mode; directed and extended rounding remain isolated.
    // 32 tables with dimensions <=1024 bound this cache to about 2.2 MiB.
    if(!native_float||source>1024||destination>1024)return owned=TriangleCoefficients::create(source,destination,wrap);
    static std::array<CachedFilter,32> cache;static u64 clock=0;
    auto* oldest=&cache[0];
    for(auto& entry:cache){
        if(entry.bytes&&entry.source==source&&entry.destination==destination&&entry.wrap==wrap){entry.used=++clock;return entry.bytes;}
        if(entry.used<oldest->used)oldest=&entry;
    }
    auto* bytes=TriangleCoefficients::create(source,destination,wrap);if(!bytes)return nullptr;
    std::free(oldest->bytes);oldest->bytes=bytes;oldest->source=source;oldest->destination=destination;oldest->wrap=wrap;oldest->used=++clock;return bytes;
}
}
i32 TextureResample::point(PixelSurface& output,const TextureRect& destination,const PixelSurface& input,const TextureRect& source) noexcept {
    constexpr i32 invalid=static_cast<i32>(0x8876086cu);const auto in=layout(input.format),out=layout(output.format);
    if(!rectangle(input,source,in)||!rectangle(output,destination,out))return invalid;
    const u32 width=destination.right-destination.left,height=destination.bottom-destination.top,source_width=source.right-source.left,source_height=source.bottom-source.top;
    for(u32 y=0;y<height;++y){const i32 sy=source.top+static_cast<u64>(y)*source_height/height;
        for(u32 x=0;x<width;++x){const i32 sx=source.left+static_cast<u64>(x)*source_width/width;const TextureRect from{sx,sy,sx+1,sy+1},to{destination.left+static_cast<i32>(x),destination.top+static_cast<i32>(y),destination.left+static_cast<i32>(x)+1,destination.top+static_cast<i32>(y)+1};const i32 result=PixelCopy::copy(output,to,input.pixels,input.format,input.pitch,from);if(result)return result;
            if(width!=source_width||height!=source_height){u32 mask=0,value=0;for(u32 c=0;c<4;++c)mask|=out.masks[c]<<out.shifts[c];auto* pixel=output.pixels+to.top*output.pitch+to.left*out.bytes;std::memcpy(&value,pixel,out.bytes);value&=mask;std::memcpy(pixel,&value,out.bytes);}
        }
    }return 0;
}
i32 TextureResample::triangle(PixelSurface& output,const TextureRect& destination,const PixelSurface& input,const TextureRect& source,bool wrap_x,bool wrap_y,bool dither) noexcept {
    constexpr i32 invalid=static_cast<i32>(0x8876086cu),no_memory=static_cast<i32>(0x8007000eu);
    const auto in=layout(input.format),out=layout(output.format);if(!rectangle(input,source,in)||!rectangle(output,destination,out))return invalid;
    const u32 width=destination.right-destination.left,height=destination.bottom-destination.top,source_width=source.right-source.left,source_height=source.bottom-source.top;
    if(width==source_width&&height==source_height&&(!dither||input.format==output.format))return PixelCopy::copy(output,destination,input.pixels,input.format,input.pitch,source);
    if(static_cast<u64>(width)*height>0x1000000u)return no_memory;
    const bool native_float=single_precision_nearest();
    u8* owned_horizontal=nullptr;u8* owned_vertical=nullptr;
    const u8* horizontal=filter(source_width,width,wrap_x,native_float,owned_horizontal),*vertical=filter(source_height,height,wrap_y,native_float,owned_vertical);
    auto* pixels=static_cast<float*>(std::calloc(static_cast<size_t>(width)*height*4,sizeof(float)));auto* row=static_cast<float*>(std::malloc(source_width*4*sizeof(float)));
    const auto release=[&](){std::free(owned_horizontal);std::free(owned_vertical);std::free(pixels);std::free(row);};
    if(!horizontal||!vertical||!pixels||!row){release();return no_memory;}
    float levels[4][256];for(u32 c=0;c<4;++c){const auto mask=in.masks[c];if(!mask){levels[c][0]=1;continue;}const float unit=1.0f/mask;for(u32 n=0;n<=mask;++n)levels[c][n]=(Extended::from_int(n)*number(unit)).to_float();}
    // These coefficients are positive (create discards weights <= 1e-5), and
    // decoded channels are in [0,1]. Their products and sums cannot approach
    // float32's exponent limits. In the game's single/nearest mode each x87
    // operation therefore matches an ordinary float operation exactly. Keep
    // the original multiply/add order and -ffp-contract=off; other modes still
    // use Extended, including the higher precision D3DX compatibility tests.
    const auto* y_block=vertical+4;
    for(u32 sy=0;sy<source_height;++sy){
        const auto* source_row=input.pixels+(source.top+sy)*input.pitch+source.left*in.bytes;
        TEXT_AUDIT_COUNT(0,source_width);
        if(input.format==26)Argb4444::unpack(source_row,row,source_width);
        else for(u32 sx=0;sx<source_width;++sx){u32 packed=0;std::memcpy(&packed,source_row+sx*in.bytes,in.bytes);for(u32 c=0;c<4;++c)row[sx*4+c]=levels[c][(packed>>in.shifts[c])&in.masks[c]];}
        const auto* y_end=y_block+word(y_block);const auto* x_block=horizontal+4;
        for(u32 sx=0;sx<source_width;++sx){const auto* x_end=x_block+word(x_block);
            if(width==source_width&&height==source_height){std::memcpy(pixels+(sy*width+sx)*4,row+sx*4,16);x_block=x_end;continue;}
            // Empty atlas padding contributes only +0 to nonnegative sums.
            if(native_float&&row[sx*4]==0&&row[sx*4+1]==0&&row[sx*4+2]==0&&row[sx*4+3]==0){x_block=x_end;continue;}
            TEXT_AUDIT_COUNT(1,1);TEXT_AUDIT_COUNT(2,((y_end-y_block-4)/sizeof(FilterWeight))*((x_end-x_block-4)/sizeof(FilterWeight)));
            for(const auto* y=reinterpret_cast<const FilterWeight*>(y_block+4);reinterpret_cast<const u8*>(y)<y_end;++y)
                for(const auto* x=reinterpret_cast<const FilterWeight*>(x_block+4);reinterpret_cast<const u8*>(x)<x_end;++x){
                    auto* pixel=pixels+(y->index*width+x->index)*4;
                    if(native_float){
                        const float weight=x->weight*y->weight;
                        for(u32 c=0;c<4;++c){const float scaled=weight*row[sx*4+c];pixel[c]=scaled+pixel[c];}
                    }else{
                        const auto weight=number(x->weight)*number(y->weight);
                        for(u32 c=0;c<4;++c)pixel[c]=(weight*number(row[sx*4+c])+number(pixel[c])).to_float();
                    }
                }
            x_block=x_end;
        }
        y_block=y_end;
    }
    for(u32 y=0;y<height;++y){auto* values=pixels+y*width*4;for(u32 i=0;i<width*4;++i)values[i]=values[i]<0?0:values[i]<1?values[i]:1;
        auto* destination_row=output.pixels+(destination.top+y)*output.pitch+destination.left*out.bytes;
        if(output.format==26&&!dither){Argb4444::pack(values,destination_row,width);TEXT_AUDIT_COUNT(3,width);}
        else for(u32 x=0;x<width;++x){
            // Original D3DX ordered thresholds, indexed within the destination
            // rectangle (not the containing surface). Alpha uses the same cell.
            static constexpr u8 thresholds[4][4]={{31,15,27,11},{7,23,3,19},{25,9,29,13},{1,17,5,21}};
            const u32 scan_x=(y&1)?width-1-x:x;
            const double bias=dither?thresholds[y&3][scan_x&3]/32.0:.5;u32 packed=0;
            for(u32 c=0;c<4;++c){const auto max=out.masks[c];const i32 quantized=static_cast<i32>(static_cast<double>(values[x*4+c])*max+bias);packed|=static_cast<u32>(quantized<0?0:quantized>static_cast<i32>(max)?max:quantized)<<out.shifts[c];}std::memcpy(destination_row+x*out.bytes,&packed,out.bytes);
        }
    }
    release();return 0;
}
#if TH10_FAST_TEXT
// th10_port (TH10_FAST_TEXT): the text atlas upload. Pixels outside
// `content` are zero, so in single/nearest mode (where triangle() skips zero
// pixels) the same multiply-adds happen in the same order when the loops are
// limited to content x source. The coefficient walk, the 16-entry level table
// (hoisted out of the row loop), the clamp and Argb4444::pack are unchanged;
// all-zero sums pack to 0x0000 and are stored directly. Empty content: every
// sum stays +0, so the destination is cleared without filtering.
i32 TextureResample::triangle_content(PixelSurface& output,const TextureRect& destination,const PixelSurface& input,const TextureRect& source,const TextureRect& content) noexcept {
    constexpr i32 invalid=static_cast<i32>(0x8876086cu),no_memory=static_cast<i32>(0x8007000eu);
    const auto in=layout(input.format),out=layout(output.format);if(!rectangle(input,source,in)||!rectangle(output,destination,out))return invalid;
    const u32 width=destination.right-destination.left,height=destination.bottom-destination.top,source_width=source.right-source.left,source_height=source.bottom-source.top;
    if(input.format!=26||output.format!=26||(width==source_width&&height==source_height)||!single_precision_nearest())return triangle(output,destination,input,source);
    if(static_cast<u64>(width)*height>0x1000000u)return no_memory;
    const i32 cl=(content.left>source.left?content.left:source.left)-source.left,cr=(content.right<source.right?content.right:source.right)-source.left;
    const i32 ct=(content.top>source.top?content.top:source.top)-source.top,cb=(content.bottom<source.bottom?content.bottom:source.bottom)-source.top;
    if(cl>=cr||ct>=cb){
        for(u32 y=0;y<height;++y)std::memset(output.pixels+(destination.top+y)*output.pitch+destination.left*2,0,width*2);
        TEXT_AUDIT_COUNT(4,u64(width)*height);return 0;
    }
    u8* owned_horizontal=nullptr;u8* owned_vertical=nullptr;
    const u8* horizontal=filter(source_width,width,true,true,owned_horizontal),*vertical=filter(source_height,height,true,true,owned_vertical);
    auto* pixels=static_cast<float*>(std::calloc(static_cast<size_t>(width)*height*4,sizeof(float)));auto* row=static_cast<float*>(std::malloc(source_width*4*sizeof(float)));
    const auto release=[&](){std::free(owned_horizontal);std::free(owned_vertical);std::free(pixels);std::free(row);};
    if(!horizontal||!vertical||!pixels||!row){release();return no_memory;}
    float levels[16];Argb4444::unpack_levels(levels);
    const u8* x_first=horizontal+4;for(i32 sx=0;sx<cl;++sx)x_first+=word(x_first);
    const u8* y_block=vertical+4;
    for(u32 sy=0;sy<source_height;++sy){
        const u8* y_end=y_block+word(y_block);
        if(static_cast<i32>(sy)>=ct&&static_cast<i32>(sy)<cb){
            Argb4444::unpack(levels,input.pixels+(source.top+sy)*input.pitch+(source.left+cl)*2,row+cl*4,static_cast<u32>(cr-cl));TEXT_AUDIT_COUNT(0,cr-cl);
            const u8* x_block=x_first;
            for(i32 sx=cl;sx<cr;++sx){const u8* x_end=x_block+word(x_block);const float* value=row+sx*4;
                if(value[0]==0&&value[1]==0&&value[2]==0&&value[3]==0){x_block=x_end;continue;}
                TEXT_AUDIT_COUNT(1,1);TEXT_AUDIT_COUNT(2,((y_end-y_block-4)/sizeof(FilterWeight))*((x_end-x_block-4)/sizeof(FilterWeight)));
                for(const auto* y=reinterpret_cast<const FilterWeight*>(y_block+4);reinterpret_cast<const u8*>(y)<y_end;++y)
                    for(const auto* x=reinterpret_cast<const FilterWeight*>(x_block+4);reinterpret_cast<const u8*>(x)<x_end;++x){
                        auto* pixel=pixels+(y->index*width+x->index)*4;const float weight=x->weight*y->weight;
                        for(u32 c=0;c<4;++c){const float scaled=weight*value[c];pixel[c]=scaled+pixel[c];}
                    }
                x_block=x_end;
            }
        }
        y_block=y_end;
    }
    for(u32 y=0;y<height;++y){auto* values=pixels+y*width*4;auto* destination_row=output.pixels+(destination.top+y)*output.pitch+destination.left*2;
        for(u32 x=0;x<width;++x){float* v=values+x*4;
            if(v[0]==0&&v[1]==0&&v[2]==0&&v[3]==0){destination_row[x*2]=0;destination_row[x*2+1]=0;TEXT_AUDIT_COUNT(4,1);continue;}
            for(u32 c=0;c<4;++c)v[c]=v[c]<0?0:v[c]<1?v[c]:1;
            Argb4444::pack(v,destination_row+x*2,1);TEXT_AUDIT_COUNT(3,1);
        }
    }
    release();return 0;
}
#endif
}
