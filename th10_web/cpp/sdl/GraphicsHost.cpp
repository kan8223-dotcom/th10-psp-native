#include "../platform/Graphics.hpp"
#include "Renderer.hpp"
#include "AssetPixelFormat.hpp"
#ifndef TH_NATIVE_PLATFORM
#include "LegacyGraphics.hpp"
#endif
#include <memory>
#include <cstring>
#include <cstdlib>
#include <algorithm>
using namespace touhou::sdl;
namespace {
u32 addr(const void* p){return reinterpret_cast<uintptr_t>(p);}template<class T=u32>T* ptr(u32 p){return reinterpret_cast<T*>(uintptr_t(p));}
u32 pixel_bytes(u32 f){return f==20?3:(f>=23&&f<=26)||f==51?2:f==28||f==50?1:4;}
struct Resource {enum Kind{Device,Texture,Image,Buffer}kind;u32 refs=1,parent=0,child=0,usage=0,pool=0;
#ifndef TH_NATIVE_PLATFORM
u32 fvf=0;
#endif
VertexLayout layout=VertexLayout::WorldUv;Surface s;
#ifdef TH10_SURFACE_ALLOCATOR
// th10_port: the PSP GE samples surface bytes in place (aligned base).
std::vector<u8,TH10_SURFACE_ALLOCATOR> bytes;
#else
std::vector<u8> bytes;
#endif
};
struct Host {
 std::map<u32,std::unique_ptr<Resource>> resources;u32 next=1,device=0,back=0,depth=0,target=0,depthTarget=0,bound=0,stream=0,streamOffset=0,streamStride=0,index=0;
 std::unique_ptr<Renderer> gpu;std::vector<u8> expanded;
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
 // th10_port (TH10_RENDER_FAST_GE_STATE, PSP-1000 lane): every draw looks up
 // its vertex stream, texture, image and target (about five walks of this
 // map). The last ids found are kept; ids are never reused (create: next++),
 // so an entry holds until its resource is erased (forget, shutdown).
 struct Found {u32 id=0;Resource* resource=nullptr;} found[16];
 Resource& get(u32 h){auto& f=found[h&15u];if(h&&f.id==h)return *f.resource;auto it=resources.find(h);if(it==resources.end()){std::fprintf(stderr,"SDL TH10 invalid resource %u\n",h);std::abort();}f.id=h;f.resource=it->second.get();return *f.resource;}
 void forget(u32 h){auto& f=found[h&15u];if(f.id==h)f=Found{};}
#else
 Resource& get(u32 h){auto it=resources.find(h);if(it==resources.end()){std::fprintf(stderr,"SDL TH10 invalid resource %u\n",h);std::abort();}return *it->second;}
#endif
 u32 create(Resource::Kind k){u32 id=next++;auto r=std::make_unique<Resource>();r->kind=k;r->s.handle=id;resources[id]=std::move(r);return id;}
#if defined(TH10_GE_NO_BACKBUFFER_BYTES) && TH10_GE_NO_BACKBUFFER_BYTES
 u32 image(u32 w,u32 h,u32 f,u32 pool=1,u32 usage=0,bool storage=true){u32 id=create(Resource::Image);auto& r=get(id);r.pool=pool;r.usage=usage;r.s.width=w;r.s.height=h;r.s.format=asset_pixel_format(f);r.s.pitch=w*pixel_bytes(f);r.s.size=r.s.pitch*h;if(storage){r.bytes.resize(r.s.size);r.s.data=r.bytes.data();}else r.s.data=nullptr;return id;}
#else
 u32 image(u32 w,u32 h,u32 f,u32 pool=1,u32 usage=0){u32 id=create(Resource::Image);auto& r=get(id);r.pool=pool;r.usage=usage;r.s.width=w;r.s.height=h;r.s.format=asset_pixel_format(f);r.s.pitch=w*pixel_bytes(f);r.s.size=r.s.pitch*h;r.bytes.resize(r.s.size);r.s.data=r.bytes.data();return id;}
#endif
 u32 add(u32 id){if(!id)return 0;auto& r=get(id);return r.parent?add(r.parent):++r.refs;}
 u32 release(u32 id){if(!id)return 0;auto& r=get(id);if(r.parent)return release(r.parent);if(--r.refs)return r.refs;
  if(r.kind==Resource::Device){gpu->flush();release(bound);release(stream);release(index);release(target);release(depthTarget);release(back);release(depth);}
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
  else if(r.kind==Resource::Texture){gpu->release(r.child);forget_indexed(get(r.child));
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
   forget(r.child);
#endif
   resources.erase(r.child);}else if(r.kind==Resource::Image){gpu->release(id);forget_indexed(r);}
#else
  else if(r.kind==Resource::Texture){gpu->release(r.child);
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
   forget(r.child);
#endif
   resources.erase(r.child);}else if(r.kind==Resource::Image)gpu->release(id);
#endif

#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
  forget(id);
#endif
  resources.erase(id);return 0;
 }
 static Surface resolve(void* p,u32 id){auto& h=*static_cast<Host*>(p);auto& r=h.get(id);return r.kind==Resource::Texture?h.get(r.child).s:r.s;}
 u32 make_texture(u32 w,u32 h,u32 f,u32 pool,u32 usage){u32 s=image(w,h,f,pool,usage),t=create(Resource::Texture);get(t).child=s;get(s).parent=t;return t;}
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
 // th10_port (TH10_TEXTURE_CLUT8): a texture whose image keeps 8-bit indices
 // (pitch = width) and, after them, a 256-entry table of 16-bit `f` values. Its
 // ids come in make_texture's order and the game sees the 16-bit format. Any
 // access to its bytes (lock, copy, rectangle write) first turns it back into
 // the 16-bit image the table describes (expand, counted; none expected).
 u32 indexed_live=0,indexed_bytes=0,indexed_saved=0,indexed_expanded=0;
 u32 make_indexed_texture(u32 w,u32 h,u32 f,u8*& indices,u16*& palette){
  u32 s=create(Resource::Image);auto& r=get(s);r.pool=1;r.usage=0;r.s.width=w;r.s.height=h;r.s.format=asset_pixel_format(f);r.s.pitch=w;r.s.size=w*h;
  r.bytes.resize(r.s.size+512);r.s.data=r.bytes.data();r.s.palette=reinterpret_cast<u16*>(r.s.data+r.s.size);r.s.version=1;
  u32 t=create(Resource::Texture);get(t).child=s;get(s).parent=t;indices=r.s.data;palette=r.s.palette;
  ++indexed_live;indexed_bytes+=r.s.size+512;indexed_saved+=w*h*2-(r.s.size+512);return t;
 }
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
 // th10_port (TH10_TEXTURE_LAZY_EMPTY): '@' textures start without bytes of
 // their own; all 0, they point at a shared block of zero indices (2 KiB: what
 // a renderer would copy) and a zero table. The first access allocates them.
 u32 lazy_live=0,lazy_bytes=0,lazy_materialized=0;
 u32 make_lazy_texture(u32 w,u32 h,u32 f){
  static u8 zero_indices[2048] __attribute__((aligned(64)));static u16 zero_table[256] __attribute__((aligned(64)));
  u32 s=create(Resource::Image);auto& r=get(s);r.pool=1;r.usage=0;r.s.width=w;r.s.height=h;r.s.format=asset_pixel_format(f);r.s.pitch=w*pixel_bytes(f);r.s.size=r.s.pitch*h;
  r.s.data=zero_indices;r.s.palette=zero_table;r.s.uniform=true;
  u32 t=create(Resource::Texture);get(t).child=s;get(s).parent=t;++lazy_live;lazy_bytes+=r.s.size;return t;
 }
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
 // th10_port (TH10_TEXTURE_LAZY_PADDED): a texture whose bytes are `rows` rows of
 // `columns` pixels at its top left (pixel_bytes 1: indices into `palette`, as
 // make_indexed_texture holds them; 2: 16-bit values) and `fill` everywhere
 // else. It starts as an '@' texture without bytes plus those rows (its seed,
 // by image id) and gets the whole texture on its first draw (draw), lock, copy
 // or rectangle write (expand); the renderer never samples it before.
 struct PaddedSeed {std::vector<u8> image;std::vector<u16> palette;u32 columns,rows,pixel_bytes,fill,bytes;};
 std::map<u32,PaddedSeed> padded;u32 padded_live=0,padded_bytes=0,padded_materialized=0;
 u32 make_padded_texture(u32 w,u32 h,u32 f,const u8* image,u32 columns,u32 rows,u32 pixel_bytes,const u16* palette,u32 fill){
  const u32 t=make_lazy_texture(w,h,f),s=get(t).child;auto& r=get(s);--lazy_live;lazy_bytes-=r.s.size;
  PaddedSeed seed;seed.image.assign(image,image+size_t(columns)*rows*pixel_bytes);if(palette)seed.palette.assign(palette,palette+256);
  seed.columns=columns;seed.rows=rows;seed.pixel_bytes=pixel_bytes;seed.fill=fill;seed.bytes=pixel_bytes==1?w*h+512:r.s.size;
  ++padded_live;padded_bytes+=seed.bytes;padded.emplace(s,std::move(seed));return t;
 }
 void materialize(u32 id){
  auto it=padded.find(id);if(it==padded.end())return;auto& r=get(id);const PaddedSeed& seed=it->second;
  gpu->release(id);--padded_live;padded_bytes-=seed.bytes;++padded_materialized;
  if(seed.pixel_bytes==1){   // make_indexed_texture's layout, then create_indexed_texture's indices and table
   r.s.pitch=r.s.width;r.s.size=r.s.width*r.s.height;r.bytes.resize(r.s.size+512);r.s.data=r.bytes.data();r.s.palette=reinterpret_cast<u16*>(r.s.data+r.s.size);
   if(seed.fill)std::memset(r.s.data,int(seed.fill),r.s.size);
   for(u32 y=0;y<seed.rows;++y)std::memcpy(r.s.data+y*r.s.pitch,seed.image.data()+size_t(y)*seed.columns,seed.columns);
   std::memcpy(r.s.palette,seed.palette.data(),512);
   ++indexed_live;indexed_bytes+=r.s.size+512;indexed_saved+=r.s.width*r.s.height*2-(r.s.size+512);
  }else{   // create_surface_texture's zeros, then upload_surface's rows
   r.bytes.resize(r.s.size);r.s.data=r.bytes.data();r.s.palette=nullptr;
   for(u32 y=0;y<seed.rows;++y)std::memcpy(r.s.data+y*r.s.pitch,seed.image.data()+size_t(y)*seed.columns*2,size_t(seed.columns)*2);
  }
  r.s.uniform=false;r.s.version++;padded.erase(it);
 }
#endif
#endif
 void forget_indexed(const Resource& r){
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
  if(r.s.uniform){auto it=padded.find(r.s.handle);if(it!=padded.end()){--padded_live;padded_bytes-=it->second.bytes;padded.erase(it);return;}}
#endif
  if(r.s.uniform){--lazy_live;lazy_bytes-=r.s.size;return;}
#endif
  if(r.s.palette){--indexed_live;indexed_bytes-=r.s.size+512;indexed_saved-=r.s.width*r.s.height*2-(r.s.size+512);}}
 void expand(u32 id){
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
  materialize(id);   // a padded texture first gets the bytes create_embedded would have made
#endif
  auto& r=get(id);
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
  if(r.s.uniform){gpu->release(id);forget_indexed(r);r.bytes.resize(r.s.size);r.s.data=r.bytes.data();r.s.palette=nullptr;r.s.uniform=false;r.s.version++;++lazy_materialized;return;}
#endif
  if(!r.s.palette)return;
  gpu->read(id);   // the renderer hands the bytes back in D3D order (indices unswizzled, table swapped back)
  const u32 n=r.s.width*r.s.height;decltype(r.bytes) wide(n*2);
  for(u32 i=0;i<n;++i)std::memcpy(wide.data()+i*2,r.s.palette+r.s.data[i],2);
  gpu->release(id);forget_indexed(r);   // the renderer forgets the 8-bit form (it waits for the GE first)
  r.bytes.swap(wide);r.s.data=r.bytes.data();r.s.pitch=r.s.width*2;r.s.size=n*2;r.s.palette=nullptr;r.s.version++;++indexed_expanded;
 }
#endif
 void desc(u32 id,u32* out){auto& r=get(id);u32 values[]{asset_pixel_code(r.s.format),1,r.usage,r.pool,0,0,r.s.width,r.s.height};std::memcpy(out,values,sizeof(values));}
 i32 copy(u32 a,const i32* from,u32 b,const i32* dest){auto& x=get(a).s;auto& y=get(b).s;if(a==b||x.format!=y.format)return i32(0x8876086c);
  i32 box[]{0,0,i32(x.width),i32(x.height)};if(!from)from=box;i32 point[]{from[0],from[1]};if(!dest)dest=point;
  if(from[0]<0||from[1]<0||from[2]<=from[0]||from[3]<=from[1]||from[2]>i32(x.width)||from[3]>i32(x.height)||dest[0]<0||dest[1]<0||dest[0]+from[2]-from[0]>i32(y.width)||dest[1]+from[3]-from[1]>i32(y.height))return i32(0x8876086c);
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
  expand(a);expand(b);
#endif
  gpu->copy(a,from,b,dest);y.version++;return 0;
 }
 void draw(TopologyParameter p,u32 n,const u8* data,u32 stride,const void* idx=nullptr,u32 fmt=0,u32 minimum=0,u32 vertexCount=0){
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
  const u32 child=bound?get(bound).child:0;if(child&&!padded.empty())materialize(child);   // a padded texture gets its bytes before its first draw
  gpu->state.texture=child;gpu->state.target=target;gpu->state.depth=depthTarget;
#else
  gpu->state.texture=bound?get(bound).child:0;gpu->state.target=target;gpu->state.depth=depthTarget;
#endif
  if(idx){
#ifdef TH_NATIVE_PLATFORM
  u32 count=vertex_count(p,n);
#else
  u32 count=vertex_count(legacy::topology(p),n);
#endif
expanded.resize(count*stride);for(u32 i=0;i<count;i++){u32 k=fmt==101?static_cast<const uint16_t*>(idx)[i]:static_cast<const u32*>(idx)[i];if(k<minimum||k>=minimum+vertexCount)std::abort();std::memcpy(expanded.data()+i*stride,data+k*stride,stride);}data=expanded.data();}
#ifdef TH_NATIVE_PLATFORM
  if(p==Topology::Triangles)gpu->draw_batch(n,data,stride);else gpu->draw(p,n,data,stride);
#else
  gpu->draw(legacy::topology(p),n,data,stride);
#endif
 }
}host;
}
extern "C" {
#ifdef TH_NATIVE_PLATFORM
PipelineState& sdl_pipeline(){return host.gpu->pipeline();}
#else
void sdl_texture_stage(u32 stage,u32 key,u32 value){if(!stage)legacy::stage(host.gpu->pipeline(),key,value);}
void sdl_sampler(u32 stage,u32 key,u32 value){const u32 keys[]{0,13,14,25,15,16,17,18,19,20,21};if(!stage&&key<11)legacy::stage(host.gpu->pipeline(),keys[key],value);}
void sdl_render_state(u32 key,u32 value){legacy::render(host.gpu->pipeline(),key,value);}
#endif
void sdl_bind_texture(u32 texture){if(host.bound!=texture){host.add(texture);host.release(host.bound);host.bound=texture;}}
void sdl_vertex_format(LayoutParameter format){
#ifdef TH_NATIVE_PLATFORM
 host.gpu->state.layout=attributes(format);
#else
 host.gpu->state.layout=legacy::vertices(format);
#endif
}
void sdl_draw(TopologyParameter primitive,u32 count,const void* vertices,u32 stride){host.draw(primitive,count,static_cast<const u8*>(vertices),stride);}
void sdl_transform(MatrixParameter kind,const th10::Matrix4* matrix){
#ifdef TH_NATIVE_PLATFORM
 host.gpu->transform(kind,matrix);
#else
 host.gpu->transform(legacy::matrix(kind),matrix);
#endif
}
void sdl_viewport(const th10::CameraViewport* view){host.gpu->viewport(*reinterpret_cast<const Viewport*>(view));}
u32 graphics_host_create(const th10::browser::GraphicsPresentation* p,u32 flags){
 if(!(flags&2))th10::arithmetic_mode(th10::Precision::Single,th10::Rounding::NearestEven);
 host.gpu=std::make_unique<Renderer>(10,Host::resolve,&host);if(!host.gpu->initialize())return 0;
#if defined(TH10_GE_NO_BACKBUFFER_BYTES) && TH10_GE_NO_BACKBUFFER_BYTES
 // th10_port (TH08 BACKBUFFER_SHADOW_LAZY, psp/backbuffer_shadow.cpp sha c7cff606):
 // the GE draws into VRAM and never reads or writes these two images' bytes
 // (copy/read of the back buffer return early), so they keep only their
 // descriptions: 2 x 1,228,800 bytes. Captures of the screen get no pixels
 // and stay black, as they already were; screenshots are skipped.
 host.back=host.image(p->width?p->width:640,p->height?p->height:480,p->format?p->format:22,0,0,false);host.depth=host.image(host.get(host.back).s.width,host.get(host.back).s.height,80,0,0,false);host.device=host.create(Resource::Device);
#else
 host.back=host.image(p->width?p->width:640,p->height?p->height:480,p->format?p->format:22,0);host.depth=host.image(host.get(host.back).s.width,host.get(host.back).s.height,80,0);host.device=host.create(Resource::Device);
#endif
 host.target=host.back;host.depthTarget=host.depth;host.add(host.back);host.add(host.depth);host.gpu->state.target=host.back;host.gpu->state.depth=host.depth;return host.device;
}
#ifdef TH_NATIVE_PLATFORM
i32 native_graphics_create_surface(u32 w,u32 h,u32 f,u32 pool,bool target,void*& out){out=ptr<void>(host.image(w,h,f,target?0:pool,target?1:0));return 0;}
i32 native_graphics_copy_surface(void* source,const th10::TextureRect* region,void* destination,const th10::TextureRect* target,u32 filter){auto& s=host.get(addr(source)).s;auto& d=host.get(addr(destination)).s;
    th10::TextureRect from=region?*region:th10::TextureRect{0,0,i32(s.width),i32(s.height)},to=target?*target:th10::TextureRect{0,0,i32(d.width),i32(d.height)};
    if(filter>2||from.right-from.left!=to.right-to.left||from.bottom-from.top!=to.bottom-to.top)return i32(0x8876086c);
    return host.copy(addr(source),&from.left,addr(destination),&to.left);
}
i32 native_graphics_update_surface(void* source,const th10::TextureRect& region,void* destination,i32 x,i32 y){i32 point[]{x,y};return host.copy(addr(source),&region.left,addr(destination),point);}
u32 native_graphics_retain_resource(void* value){return host.add(addr(value));}
u32 native_graphics_resource_revision(void* value){auto& r=host.get(addr(value));return host.get(r.kind==Resource::Texture?r.child:addr(value)).s.version;}
u32 native_graphics_release_resource(void* value){return host.release(addr(value));}
i32 native_graphics_create_texture(u32 width,u32 height,u32 format,void*& out){out=ptr<void>(host.make_texture(width,height,format,1,0));return 0;}
void* native_graphics_texture_surface(void* texture){const auto child=host.get(addr(texture)).child;host.add(child);return ptr<void>(child);}
th10::TextureDescription native_graphics_describe_surface(void* surface){auto& r=host.get(addr(surface));const auto& s=host.get(r.kind==Resource::Texture?r.child:addr(surface)).s;return {asset_pixel_code(s.format),s.width,s.height};}
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
th10::TextureLock native_graphics_map_surface(void* surface){auto& r=host.get(addr(surface));const auto id=r.kind==Resource::Texture?r.child:addr(surface);host.expand(id);host.gpu->read(id);auto& s=host.get(id).s;return {i32(s.pitch),s.data};}
// th10_port (TH10_TEXTURE_CLUT8): textures/Textures.cpp create_indexed_texture.
i32 native_graphics_create_indexed_texture(u32 width,u32 height,u32 format,void*& out,u8*& indices,uint16_t*& palette){out=ptr<void>(host.make_indexed_texture(width,height,format,indices,palette));return 0;}
// live indexed textures, their bytes, the bytes they save against 16 bit, expansions back to 16 bit
void native_graphics_indexed_stats(u32* out){out[0]=host.indexed_live;out[1]=host.indexed_bytes;out[2]=host.indexed_saved;out[3]=host.indexed_expanded;}
int native_graphics_surface_indexed(void* surface){auto& r=host.get(addr(surface));return host.get(r.kind==Resource::Texture?r.child:addr(surface)).s.palette!=nullptr;}
#if defined(TH10_TEXTURE_LAZY_EMPTY) && TH10_TEXTURE_LAZY_EMPTY
i32 native_graphics_create_lazy_texture(u32 width,u32 height,u32 format,void*& out){out=ptr<void>(host.make_lazy_texture(width,height,format));return 0;}
// '@' textures still without bytes, the bytes they would hold, how many got them since
void native_graphics_lazy_stats(u32* out){out[0]=host.lazy_live;out[1]=host.lazy_bytes;out[2]=host.lazy_materialized;}
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
i32 native_graphics_create_padded_texture(u32 width,u32 height,u32 format,void*& out,const u8* image,u32 columns,u32 rows,u32 pixel_bytes,const uint16_t* palette,u32 fill){
    if(!width||!height||!image||!columns||!rows||columns>width||rows>height||(pixel_bytes!=1&&pixel_bytes!=2)||(pixel_bytes==1&&(!palette||fill>255)))return -1;
    out=ptr<void>(host.make_padded_texture(width,height,format,image,columns,rows,pixel_bytes,palette,fill));return 0;}
// padded textures still without bytes, the bytes they will have, how many got them since
void native_graphics_padded_stats(u32* out){out[0]=host.padded_live;out[1]=host.padded_bytes;out[2]=host.padded_materialized;}
#endif
#endif
#else
th10::TextureLock native_graphics_map_surface(void* surface){auto& r=host.get(addr(surface));const auto id=r.kind==Resource::Texture?r.child:addr(surface);host.gpu->read(id);auto& s=host.get(id).s;return {i32(s.pitch),s.data};}
#endif
void native_graphics_unmap_surface(void* surface){auto& r=host.get(addr(surface));host.get(r.kind==Resource::Texture?r.child:addr(surface)).s.version++;}
#if defined(TH10_FAST_TEXT_UPLOAD) && TH10_FAST_TEXT_UPLOAD
// th10_port: the renderer writes the rectangle (on the PSP straight into the GE form), then the surface version moves as after an unmap.
bool native_graphics_write_surface(void* surface,const th10::TextureRect& rectangle,const u8* pixels,i32 pitch){
    auto& r=host.get(addr(surface));const auto id=r.kind==Resource::Texture?r.child:addr(surface);
#if defined(TH10_TEXTURE_CLUT8) && TH10_TEXTURE_CLUT8
    host.expand(id);
#endif
    if(pitch<=0||!host.gpu->write_rect(id,&rectangle.left,pixels,u32(pitch)))return false;
    host.get(id).s.version++;return true;}
#endif
void native_graphics_resource_priority(void*,u32){}
void native_graphics_preload_resource(void* value){if(value){auto& r=host.get(addr(value));if(r.kind==Resource::Texture)host.gpu->prepare(r.child);else if(r.kind==Resource::Image)host.gpu->prepare(addr(value));}}
i32 native_graphics_create_vertices(u32 bytes,LayoutParameter format,void*& out){const auto id=host.create(Resource::Buffer);auto& r=host.get(id);r.layout=format;r.pool=1;r.bytes.resize(bytes);out=ptr<void>(id);return 0;}
void* native_graphics_map_vertices(void* buffer){return host.get(addr(buffer)).bytes.data();}
void native_graphics_unmap_vertices(void*){}
void native_graphics_vertex_buffer(void* buffer,u32 stride){const auto id=addr(buffer);host.add(id);host.release(host.stream);host.stream=id;host.streamOffset=0;host.streamStride=stride;}
void native_graphics_draw_vertices(TopologyParameter primitive,u32 first,u32 count){host.draw(primitive,count,host.get(host.stream).bytes.data()+first*host.streamStride,host.streamStride);}
void native_graphics_clear_target(u32 flags,u32 color,float depth,u32 stencil,const i32* rectangles,u32 count){host.gpu->state.target=host.target;host.gpu->state.depth=host.depthTarget;host.gpu->clear(flags,color,depth,stencil,rectangles,count);}
i32 native_graphics_present_frame(){host.gpu->present(host.back);return 0;}
void* native_graphics_back_surface(){host.add(host.back);return ptr<void>(host.back);}
i32 native_graphics_reset_presentation(void*){return 0;}
void native_graphics_clear_shader(){}
#else
i32 graphics_host_device(u32 handle,u32 op,const u32* a){auto& h=host;auto& g=*h.gpu;
 switch(op){
 case 0:if(a[0]==0)legacy::stage(g.pipeline(),a[1],a[2]);break;
 case 1:{const u32 keys[]{0,13,14,25,15,16,17,18,19,20,21};if(a[0]==0&&a[1]<11)legacy::stage(g.pipeline(),keys[a[1]],a[2]);break;}
 case 2:legacy::render(g.pipeline(),a[0],a[1]);break;
 case 3:if(a[0]==0&&h.bound!=a[1]){h.add(a[1]);h.release(h.bound);h.bound=a[1];}break;
 case 4:g.state.layout=legacy::vertices(a[0]);break;
 case 5:h.draw(a[0],a[1],ptr<u8>(a[2]),a[3]);break;
 case 6:g.transform(legacy::matrix(a[0]),ptr(a[1]));break;
 case 7:if(a[0]==0){h.add(a[1]);h.release(h.stream);h.stream=a[1];h.streamOffset=a[2];h.streamStride=a[3];}break;
 case 8:h.draw(a[0],a[2],h.get(h.stream).bytes.data()+h.streamOffset+a[1]*h.streamStride,h.streamStride);break;
 case 9:g.viewport(*ptr<Viewport>(a[0]));break;
 case 10:g.state.target=h.target;g.state.depth=h.depthTarget;g.clear(a[2],a[3],*ptr<float>(addr(a+4)),a[5],a[0]?ptr<i32>(a[1]):nullptr,a[0]);break;
 case 11:case 12:break;
 case 13:g.present(h.back);break;
 case 14:h.add(h.back);*ptr(a[3])=h.back;break;
 case 15:h.add(h.target);*ptr(a[1])=h.target;break;
 case 16:if(a[0])return i32(0x8876086c);g.flush();h.add(a[1]);h.release(h.target);h.target=a[1];g.state.target=h.target;{auto& s=h.get(h.target).s;g.viewport({0,0,s.width,s.height,0,1});}break;
 case 17:h.add(h.depthTarget);*ptr(a[0])=h.depthTarget;return h.depthTarget?0:i32(0x88760866);
 case 18:g.flush();h.add(a[0]);h.release(h.depthTarget);h.depthTarget=a[0];g.state.depth=a[0];break;
 case 19:*ptr(a[6])=h.make_texture(a[0],a[1],a[4],a[5],a[3]);break;
 case 20:*ptr(a[4])=h.image(a[0],a[1],a[2],a[3]);break;
 case 21:*ptr(a[6])=h.image(a[0],a[1],a[2],0,1);break;
 case 22:case 23:{u32 id=h.create(Resource::Buffer);auto& r=h.get(id);r.usage=a[1];r.fvf=a[2];r.pool=a[3];r.bytes.resize(a[0]);*ptr(a[4])=id;break;}
 case 24:h.add(a[0]);h.release(h.index);h.index=a[0];break;
 case 25:{auto& idx=h.get(h.index);h.draw(a[0],a[5],h.get(h.stream).bytes.data()+h.streamOffset+i32(a[1])*h.streamStride,h.streamStride,idx.bytes.data()+a[4]*(idx.fvf==101?2:4),idx.fvf,a[2],a[3]);break;}
 case 26:h.draw(a[0],a[3],ptr<u8>(a[6]),a[7],ptr(a[4]),a[5],a[1],a[2]);break;
 case 27:{auto& s=h.get(a[0]).s;auto& d=h.get(a[2]).s;i32 from[]{0,0,i32(s.width),i32(s.height)},to[]{0,0,i32(d.width),i32(d.height)};if(a[1])std::memcpy(from,ptr(a[1]),16);if(a[3])std::memcpy(to,ptr(a[3]),16);if(a[4]>2||from[2]-from[0]!=to[2]-to[0]||from[3]-from[1]!=to[3]-to[1])return i32(0x8876086c);return h.copy(a[0],from,a[2],to);}
 case 28:return h.copy(a[0],ptr<i32>(a[1]),a[2],ptr<i32>(a[3]));
 case 29:break;
 case 30:{auto* out=ptr(a[0]);std::fill(out,out+76,0);std::fill(out+2,out+22,0xffffffff);out[0]=1;out[15]=5;out[22]=out[23]=4096;out[24]=256;out[25]=8192;out[26]=4096;out[27]=16;*reinterpret_cast<float*>(out+28)=1e10f;out[35]=0x10008;out[36]=0x3ffffff;out[37]=out[38]=8;out[39]=0x7f;out[40]=8;out[41]=6;out[42]=4;out[43]=255;*reinterpret_cast<float*>(out+44)=64;out[45]=out[46]=0xfffff;out[47]=16;out[48]=255;out[49]=0xfffe0101;out[50]=96;out[51]=0xffff0104;*reinterpret_cast<float*>(out+52)=8;break;}
 case 31:break;case 32:{u32 mode[]{640,480,60,22};std::memcpy(ptr(a[1]),mode,16);break;}case 33:break;
 default:std::fprintf(stderr,"SDL TH10 device operation %u\n",op);std::abort();
 }return 0;
}
i32 graphics_host_resource(u32 id,u32 op,const u32* a){auto& h=host;if(op==0)return h.add(id);if(op==1)return h.release(id);auto& r=h.get(id);u32 s=r.kind==Resource::Texture?r.child:id;
 switch(op){
 case 2:h.desc(s,ptr(a[0]));break;
 case 3:h.add(r.child);*ptr(a[1])=r.child;break;
 case 4:{auto& surface=h.get(s).s;h.gpu->read(s);u32 output=r.kind==Resource::Texture?a[1]:a[0],rect=r.kind==Resource::Texture?a[2]:a[1];auto* result=ptr(output);const auto* box=ptr<i32>(rect);result[0]=surface.pitch;result[1]=addr(surface.data)+(rect?box[1]*surface.pitch+box[0]*pixel_bytes(surface.format):0);break;}
 case 5:h.get(s).s.version++;break;
 case 6:*ptr(a[2])=addr(r.bytes.data())+a[0];break;
 case 7:case 8:case 9:break;
 default:std::fprintf(stderr,"SDL TH10 resource operation %u\n",op);std::abort();
 }return 0;
}
#endif
__attribute__((export_name("sdl_read_back"))) u32 sdl_read_back(){host.gpu->read(host.back);return addr(host.get(host.back).s.data);}
__attribute__((export_name("sdl_shutdown"))) void sdl_shutdown(){host.gpu.reset();host.resources.clear();host.next=1;
#if defined(TH10_RENDER_FAST_GE_STATE) && TH10_RENDER_FAST_GE_STATE
 for(auto& f:host.found)f=Host::Found{};   // ids start again at 1
#endif
 host.device=host.back=host.depth=host.target=host.depthTarget=host.bound=host.stream=host.index=0;}
}
