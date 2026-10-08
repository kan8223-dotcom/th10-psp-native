// Headless renderer: accepts the same semantic commands as the GLES renderer
// and folds them into a per-frame FNV-1a hash instead of drawing.
#include "Renderer.hpp"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace touhou::sdl {
namespace {
Renderer* active=nullptr;bool hashing=true;
// TH10_STATES=1 (PC): census of the pipeline/layout combinations actually
// drawn, to size the GE (PSP) and GLES2 (Android) state mapping.
bool census=false;std::map<std::string,uint64_t> combos;
Renderer::Resolve census_resolve=nullptr;void* census_owner=nullptr;
std::string describe(const State& st,Topology primitive){
    const auto& p=st.pipeline;char b[400];
    auto arg=[](const Argument& a){static const char* src[]={"diff","tex","tfac","spec"};static char t[8][24];static int i=0;i=(i+1)%8;std::snprintf(t[i],24,"%s%s%s",a.complement?"1-":"",src[unsigned(a.source)&3],a.alphaOnly?".a":"");return t[i];};
    std::snprintf(b,sizeof(b),"prim=%u layout=%s%s%s c=%u(%s,%s) a=%u(%s,%s) blend=%d(%u,%u,eq%u) atest=%d(%u,ref%u) ztest=%d(cmp%u) zwrite=%d fog=%d(mode%u) cull=%u tex=%d tt=%d filt=%u/%u addr=%u/%u",
        unsigned(primitive),st.layout.screen?"screen":"world",st.layout.diffuse!=VertexAttributes::absent?"+col":"",st.layout.uv!=VertexAttributes::absent?"+uv":"",
        unsigned(p.color.operation),arg(p.color.first),arg(p.color.second),unsigned(p.alpha.operation),arg(p.alpha.first),arg(p.alpha.second),
        p.blend,unsigned(p.sourceBlend),unsigned(p.destinationBlend),unsigned(p.blendEquation),p.alphaTest,unsigned(p.alphaCompare),p.alphaReference,p.depthTest,unsigned(p.depthCompare),p.depthWrite,p.fog,unsigned(p.fogMode),unsigned(p.cull),st.texture!=0,p.textureTransform,unsigned(p.minFilter),unsigned(p.magFilter),unsigned(p.addressU),unsigned(p.addressV));
    std::string out=b;
    if(census_resolve&&st.target){const auto t=census_resolve(census_owner,st.target);char tb[48];std::snprintf(tb,sizeof(tb)," target=%ux%u/%u",t.width,t.height,unsigned(t.format));out+=tb;}
    if(census_resolve&&st.texture){const auto t=census_resolve(census_owner,st.texture);char tb[48];std::snprintf(tb,sizeof(tb)," texfmt=%u",unsigned(t.format));out+=tb;}
    return out;
}
constexpr uint64_t fnv_offset=1469598103934665603ull,fnv_prime=1099511628211ull;
}
Renderer* current(){return active;}
void set_current(Renderer* r){active=r;}

Renderer::Renderer(int v,Resolve r,void* p):version(v),resolve(r),owner(p){}
bool Renderer::initialize(){frame_hash=fnv_offset;set_current(this);
#ifndef __PSP__
    census=std::getenv("TH10_STATES")!=nullptr;census_resolve=resolve;census_owner=owner;
#endif
    return true;}
Renderer::~Renderer(){
    if(census){if(FILE* f=std::fopen("th10_states.txt","w")){for(const auto& e:combos)std::fprintf(f,"%llu %s\n",(unsigned long long)e.second,e.first.c_str());std::fclose(f);}}
    if(active==this)active=nullptr;}
void Renderer::mix(const void* bytes,size_t size){
    if(!hashing)return;
    const auto* p=static_cast<const u8*>(bytes);
    for(size_t i=0;i<size;i++){frame_hash^=p[i];frame_hash*=fnv_prime;}
}
void Renderer::flush(){}
void Renderer::discard(){}
void Renderer::transform(MatrixKind kind,const void* matrix){
    const auto k=static_cast<u32>(kind);if(k<state.matrix.size())std::memcpy(state.matrix[k].data(),matrix,sizeof(state.matrix[k]));
}
void Renderer::viewport(const Viewport& v){state.viewport=v;}
void Renderer::draw(Topology primitive,u32 count,const void* vertices,u32 stride,const void*,IndexType){
    // Resource handles are renumbered when load order changes; hash what the
    // texture and target are (size, format), not their ids.
    u32 texture[3]{},target[3]{};
    if(state.texture){const auto s=resolve(owner,state.texture);texture[0]=s.width;texture[1]=s.height;texture[2]=static_cast<u32>(s.format);}
    if(state.target){const auto s=resolve(owner,state.target);target[0]=s.width;target[1]=s.height;target[2]=static_cast<u32>(s.format);}
    const u32 header[]{1,static_cast<u32>(primitive),count,stride};mix(header,sizeof(header));mix(texture,sizeof(texture));mix(target,sizeof(target));
    mix(&state.pipeline,sizeof(state.pipeline));mix(state.matrix.data(),sizeof(state.matrix));
    if(vertices&&stride)mix(vertices,size_t(vertex_count(primitive,count))*stride);
    stats.calls++;frame_draws++;
    if(census)++combos[describe(state,primitive)];
}
void Renderer::draw_batch(u32 count,const void* vertices,u32 stride){draw(Topology::Triangles,count,vertices,stride);stats.batches++;}
void Renderer::clear(u32 flags,u32 color,float depth,u32 stencil,const i32* rects,u32 count){
    u32 target[3]{};if(state.target){const auto s=resolve(owner,state.target);target[0]=s.width;target[1]=s.height;target[2]=static_cast<u32>(s.format);}
    const u32 header[]{2,flags,color,stencil,count};mix(header,sizeof(header));mix(target,sizeof(target));mix(&depth,sizeof(depth));
    if(rects&&count)mix(rects,size_t(count)*4*sizeof(i32));
}
void Renderer::copy(u32 source,const i32* rect,u32 target,const i32* point){
    const auto a=resolve(owner,source),b=resolve(owner,target);const u32 header[]{3,a.width,a.height,static_cast<u32>(a.format),b.width,b.height,static_cast<u32>(b.format)};mix(header,sizeof(header));
    if(rect)mix(rect,4*sizeof(i32));if(point)mix(point,2*sizeof(i32));
}
bool Renderer::resample(u32,const i32*,u32,const i32*,const float*,u32,u32){return false;}
void Renderer::read(u32){}
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
// The PC keeps surfaces in D3D form: the rows go straight in.
bool Renderer::write_rect(u32 id,const i32* r,const u8* pixels,u32 pitch){
    const auto s=resolve(owner,id);
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    if(s.palette)return false;   // the host turns a CLUT8 texture back into 16 bit before any write
#endif
    if(!s.data||!s.height||!s.width||!r||r[0]<0||r[1]<0||r[0]>=r[2]||r[1]>=r[3]||u32(r[2])>s.width||u32(r[3])>s.height)return false;
    const u32 bpp=s.size/s.height/s.width;if(!bpp)return false;
    for(i32 y=r[1];y<r[3];y++)std::memcpy(s.data+u32(y)*s.pitch+u32(r[0])*bpp,pixels+u32(y-r[1])*pitch,u32(r[2]-r[0])*bpp);
    return true;
}
#endif
void Renderer::release(u32){}
void Renderer::prepare(u32){}
void Renderer::present(u32 id){
    pending=id;stats.frames++;presented_hash=frame_hash;presented_draws=frame_draws;
    frame_hash=fnv_offset;frame_draws=0;if(!defer)commit();
}
bool Renderer::commit(){if(!pending)return false;pending=0;stats.presentations++;return true;}
}

extern "C" {
void headless_set_hash(bool enabled){touhou::sdl::hashing=enabled;}
const touhou::sdl::Statistics* sdl_stats(){static touhou::sdl::Statistics zero{};return touhou::sdl::current()?&touhou::sdl::current()->stats:&zero;}
void sdl_defer(int enabled){if(auto* r=touhou::sdl::current())r->defer=enabled;}
int sdl_commit(){return touhou::sdl::current()&&touhou::sdl::current()->commit();}
const char* sdl_error(){return touhou::sdl::current()?touhou::sdl::current()->error():"no renderer";}
int sdl_version(){return 0;}
double sdl_ticks(){using namespace std::chrono;return duration<double,std::milli>(steady_clock::now().time_since_epoch()).count();}
}
