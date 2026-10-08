#ifdef TH10_SPRITE_RANGE_PROBE
#include <cstdio>
#endif
#include "AnmRenderer.hpp"
#include "GameMath.hpp"
#include <cmath>
#if defined(TH10_RENDER_FAST_BULLETS) && TH10_RENDER_FAST_BULLETS
#include <cstring>
#endif
#include "../../../portable/numeric/SpriteNumber.hpp"
#include "../../../portable/numeric/RenderTrig.hpp"
#if defined(TH10_QUAD_INDEX) && TH10_QUAD_INDEX
extern "C" volatile int th10_quad_batch;
#endif
namespace th10 {
namespace {
Extended sum(float a,float b,float c){return number(a)+number(b)+number(c);}
Extended unsigned_number(u32 bits){auto result=Extended::from_int(static_cast<i32>(bits));if(bits&0x80000000)result=result+number(4294967296.0f);return result;}
void depth(AnmVertex* quad,Extended value){const auto z=value.to_float();for(u32 i=0;i<4;++i)quad[i].position.z=z;}
}
// 0x443080 / 0x443290. Keep their different store points and coordinate order.
#ifdef TH10_SPRITE_RANGE_PROBE
// th10_port measurement (PC): why sprite geometry takes the Extended path.
namespace { struct SpriteRangeProbe { unsigned long long calls[2]{},fallback[2]{},fail_at[2][16]{},mode_off=0; float min_bad[2][16];
    SpriteRangeProbe(){for(auto& r:min_bad)for(auto& v:r)v=1e30f;}
    ~SpriteRangeProbe(){for(int k=0;k<2;++k){std::fprintf(stderr,"sprite_range %s calls=%llu fallback=%llu\n",k?"rotated":"axis",calls[k],fallback[k]);for(int i=0;i<16;++i)if(fail_at[k][i])std::fprintf(stderr,"  operand %d fails=%llu min_abs=%g\n",i,fail_at[k][i],min_bad[k][i]);}std::fprintf(stderr,"sprite_range mode_off=%llu\n",mode_off);}
    void check(int k,std::initializer_list<float> v){++calls[k];if(!th10::single_precision_nearest()){++mode_off;++fallback[k];return;}int i=0;bool bad=false;for(float f:v){const float a=std::fabs(f);if(a!=0&&!(a>=0x1p-20f&&a<=0x1p20f)){++fail_at[k][i];if(a<min_bad[k][i])min_bad[k][i]=a;bad=true;}++i;}if(bad)++fallback[k];}
} sprite_range_probe; }
#define TH10_SR_PROBE(k,...) sprite_range_probe.check(k,{__VA_ARGS__})
#else
#define TH10_SR_PROBE(k,...) ((void)0)
#endif
#if TH10_FAST_FLOOR
// th10_port: pixel snapping in axis_geometry. On the float path floorf equals
// the double floor of a float (an integer float, or the float itself), without
// the PSP's software double; the Extended path is unchanged.
#if defined(TH10_RENDER_FAST_BULLETS) && TH10_RENDER_FAST_BULLETS
// th10_port (TH10_RENDER_FAST_BULLETS): floorf inline, without newlib's call:
// truncation is exact below 2^23 and one step down fixes negatives; integers,
// infinities, NaN and signed zeros come back as floorf returns them
// (tools/floor_inline_check.cpp compares every float bit pattern).
inline float floor_inline(float x){if(!(std::fabs(x)<8388608.f)||x==0)return x;const float t=static_cast<float>(static_cast<i32>(x));return t>x?t-1.0f:t;}
inline touhou::numeric::SpriteNumber floor_value(touhou::numeric::SpriteNumber v){return touhou::numeric::SpriteNumber(floor_inline(v.value));}
#else
inline touhou::numeric::SpriteNumber floor_value(touhou::numeric::SpriteNumber v){return touhou::numeric::SpriteNumber(std::floor(v.value));}
#endif
inline Extended floor_value(const Extended& v){return Extended::from_double(std::floor(v.to_double()));}
#define TH10_PIXEL_FLOOR(x) floor_value(x)
#else
#define TH10_PIXEL_FLOOR(x) N::from_double(std::floor((x).to_double()))
#endif
u32 AnmRenderer::axis_geometry(const AnmVm& vm,AnmVertex* q,bool pixel) noexcept {
    auto evaluate=[&](auto number)->u32{
        using N=decltype(number(0));
        auto sum_values=[&](float a,float b,float c){return number(a)+number(b)+number(c);};
        auto write_depth=[](auto* q,N z){for(unsigned i=0;i<4;++i)q[i].position.z=z.to_float();};

    auto width=number(vm.sprite_size.x)*number(vm.scale.x);if(pixel)width=number(width.to_float());
    const auto height_product=number(vm.sprite_size.y)*number(vm.scale.y);
    const auto height=number(height_product.to_float()),half_height=number((height_product*number(0.5f)).to_float());
    switch((vm.flags>>18)&3){
    case 0:{auto left=sum_values(vm.child_position.x,vm.position.x,vm.script_position.x)-width*number(0.5f);if(pixel)left=TH10_PIXEL_FLOOR(left);
        q[0].position.x=q[2].position.x=left.to_float();q[1].position.x=q[3].position.x=(number(q[0].position.x)+width).to_float();break;}
    case 1:q[0].position.x=q[2].position.x=sum_values(vm.script_position.x,vm.position.x,vm.child_position.x).to_float();q[1].position.x=q[3].position.x=(number(vm.script_position.x)+number(vm.position.x)+width+number(vm.child_position.x)).to_float();break;
    case 2:q[0].position.x=q[2].position.x=(sum_values(vm.script_position.x,vm.position.x,vm.child_position.x)-width).to_float();q[1].position.x=q[3].position.x=sum_values(vm.script_position.x,vm.position.x,vm.child_position.x).to_float();break;
    }
    switch((vm.flags>>20)&3){
    case 0:{auto top=sum_values(vm.child_position.y,vm.position.y,vm.script_position.y)-half_height;if(pixel)top=TH10_PIXEL_FLOOR(top);q[0].position.y=q[1].position.y=top.to_float();q[2].position.y=q[3].position.y=(number(q[0].position.y)+height).to_float();break;}
    case 1:if(pixel){const auto top=sum_values(vm.position.y,vm.child_position.y,vm.script_position.y);q[0].position.y=q[1].position.y=top.to_float();q[2].position.y=q[3].position.y=(top+height).to_float();}
        else{q[0].position.y=q[1].position.y=sum_values(vm.child_position.y,vm.script_position.y,vm.position.y).to_float();q[2].position.y=q[3].position.y=(number(vm.child_position.y)+number(vm.script_position.y)+height+number(vm.position.y)).to_float();}break;
    case 2:{const auto bottom=pixel?sum_values(vm.position.y,vm.child_position.y,vm.script_position.y):sum_values(vm.child_position.y,vm.script_position.y,vm.position.y);q[0].position.y=q[1].position.y=(bottom-height).to_float();q[2].position.y=q[3].position.y=bottom.to_float();break;}
    }
    // The non-pixel original reads child Y here, including when rotation is zero.
    write_depth(q,sum_values(pixel?vm.child_position.z:vm.child_position.y,vm.position.z,vm.script_position.z));return pixel?1:0;

    };
    TH10_SR_PROBE(0,vm.sprite_size.x,vm.sprite_size.y,vm.scale.x,vm.scale.y,vm.position.x,vm.position.y,vm.position.z,vm.child_position.x,vm.child_position.y,vm.child_position.z,vm.script_position.x,vm.script_position.y,vm.script_position.z);
    if(single_precision_nearest()&&touhou::numeric::sprite_range({vm.sprite_size.x,vm.sprite_size.y,vm.scale.x,vm.scale.y,vm.position.x,vm.position.y,vm.position.z,vm.child_position.x,vm.child_position.y,vm.child_position.z,vm.script_position.x,vm.script_position.y,vm.script_position.z}))
        return evaluate([](float v){return touhou::numeric::SpriteNumber(v);});
    return evaluate([](float v){return th10::number(v);});
}
// 0x4436c0 / 0x443910 are identical. The upper-right X offset remains extended
// while its lower-right copy is stored to float before multiplication.
u32 AnmRenderer::rotated_geometry(const AnmVm& vm,AnmVertex* q) noexcept {
    if(vm.rotation.z==0)return axis_geometry(vm,q,false);
    float rotation_sin,rotation_cos;
#if TH10_RENDER_FAST_TRIG
    // th10_port: draw-only geometry; ~2 ulp instead of the exact DfTrig path.
    if(!touhou::numeric::render_sincos(vm.rotation.z,rotation_sin,rotation_cos))
#endif
    sincos_float(vm.rotation.z,rotation_sin,rotation_cos);

    auto evaluate=[&](auto number)->u32{
        using N=decltype(number(0));
        auto sum_values=[&](float a,float b,float c){return number(a)+number(b)+number(c);};
        auto write_depth=[](auto* q,N z){for(unsigned i=0;i<4;++i)q[i].position.z=z.to_float();};

    if(vm.rotation.z==0)return axis_geometry(vm,q,false);
    const auto c=number(rotation_cos),s=number(rotation_sin);
    const auto x=sum_values(vm.child_position.x,vm.position.x,vm.script_position.x),y=sum_values(vm.child_position.y,vm.position.y,vm.script_position.y);
    const auto width=number(vm.sprite_size.x)*number(vm.scale.x),height=number(Scalar::mul(vm.sprite_size.y,vm.scale.y));
    N left,right,top,bottom;
    switch((vm.flags>>18)&3){case 0:left=number((width*number(-0.5f)).to_float());right=width*number(0.5f);break;case 1:left=number(0);right=width;break;case 2:left=number((-width).to_float());right=number(0);break;default:__builtin_trap();}
    switch((vm.flags>>20)&3){case 0:top=number((height*number(-0.5f)).to_float());bottom=height*number(0.5f);break;case 1:top=number(0);bottom=height;break;case 2:top=-height;bottom=number(0);break;default:__builtin_trap();}
    const auto right_copy=number(right.to_float()),bottom_copy=number(bottom.to_float());
    q[0].position.x=(left*c-top*s+x).to_float();q[0].position.y=(left*s+top*c+y).to_float();
    q[1].position.x=(right*c-top*s+x).to_float();q[1].position.y=(top*c+right*s+y).to_float();
    q[2].position.x=(left*c-bottom*s+x).to_float();q[2].position.y=(left*s+c*bottom+y).to_float();
    q[3].position.x=(right_copy*c-bottom_copy*s+x).to_float();q[3].position.y=(right_copy*s+bottom_copy*c+y).to_float();
    write_depth(q,sum_values(vm.child_position.z,vm.position.z,vm.script_position.z));return 0;

    };
    TH10_SR_PROBE(1,vm.sprite_size.x,vm.sprite_size.y,vm.scale.x,vm.scale.y,vm.position.x,vm.position.y,vm.position.z,vm.child_position.x,vm.child_position.y,vm.child_position.z,vm.script_position.x,vm.script_position.y,vm.script_position.z,rotation_cos,rotation_sin);
    if(single_precision_nearest()&&touhou::numeric::sprite_range({vm.sprite_size.x,vm.sprite_size.y,vm.scale.x,vm.scale.y,vm.position.x,vm.position.y,vm.position.z,vm.child_position.x,vm.child_position.y,vm.child_position.z,vm.script_position.x,vm.script_position.y,vm.script_position.z})
#if TH10_SPRITE_ROT_RANGE
        &&touhou::numeric::sprite_rotation_range(rotation_cos,rotation_sin))
#else
        &&touhou::numeric::sprite_range({rotation_cos,rotation_sin}))
#endif
        return evaluate([](float v){return touhou::numeric::SpriteNumber(v);});
    return evaluate([](float v){return th10::number(v);});
}
u32 AnmRenderer::modulate_channel(u32 color,u32 tint) noexcept {const auto result=((color&255)*(tint&255))>>7;return result>255?255:result;}
// 0x442f50. Flushing advances the start of the next batch; the frame owns the
// much larger vertex arena and resets its write pointer separately.
#ifdef TH10_VERTEX_PEAK
// th10_port measurement (PC): the largest batch and the largest frame total of
// the sprite vertex buffer (131072 vertices, 3.67 MB), to size a smaller one.
extern "C" {u32 th10_vertex_peak_batch=0,th10_vertex_peak_frame=0;}
#endif
void AnmRenderer::flush(){if(!manager.batch_quads)return;

#ifdef TH10_VERTEX_PEAK
    if(manager.batch_quads*6>th10_vertex_peak_batch)th10_vertex_peak_batch=manager.batch_quads*6;
#endif
#if defined(TH10_QUAD_INDEX) && TH10_QUAD_INDEX
    th10_quad_batch=1;   // this batch is append()'s q0 q1 q2 q1 q2 q3 quads (psp/GeRenderer.cpp)
#endif
    RenderCommands(environment).SetDiffuseArg(TextureArg::Diffuse);environment.vertex_format(Layouts::Screen);environment.draw_triangles(Primitives::Triangles,manager.batch_quads*2,manager.batch_start,28);manager.batch_start=manager.vertex_write;manager.batch_quads=0;++manager.flushed_batches;
#if TH10_ANM_VERTEX_CAP<131072
    manager.vertex_write=manager.batch_start=manager.vertex_buffer;   // the drawn batch was copied (TH08 RESET_STAGING)
#endif
}
// 0x4425a0. Read VM flags again after a flush, preserving callback side effects.
void AnmRenderer::apply_state(const AnmVm& vm){
    if(manager.cached_draw_state[0]!=((vm.flags>>4)&3)){flush();const auto blend=(vm.flags>>4)&3;manager.cached_draw_state[0]=blend;if(blend<2)RenderCommands(environment).SetDestinationBlend(blend?BlendMode::One:BlendMode::InverseSourceAlpha);}
    if(manager.cached_draw_state[6]!=(vm.flags>>31)){flush();const auto point=vm.flags>>31;manager.cached_draw_state[6]=point;environment.texture_filter(point);}++manager.submitted_draws;
}
// 0x4423e0. Matrix-transformed sprites use a texture factor and support the
// additional blend selector 2 that the pretransformed path leaves untouched.
void AnmRenderer::apply_material(const AnmVm& vm){
    if(manager.cached_draw_state[0]!=((vm.flags>>4)&3)){flush();const auto blend=(vm.flags>>4)&3;manager.cached_draw_state[0]=blend;if(blend<3)RenderCommands(environment).SetDestinationBlend(blend?BlendMode::One:BlendMode::InverseSourceAlpha);}
    u32 color=vm.flags&0x8000?vm.secondary_color:vm.color;if(manager.tint_enabled){u32 result=0;for(u32 shift=0;shift<32;shift+=8)result|=modulate_channel(color>>shift,manager.tint>>shift)<<shift;color=result;}
    if(manager.current_material_color!=color){flush();manager.current_material_color=color;RenderCommands(environment).SetTextureFactor(color);}
    if(manager.cached_draw_state[6]!=(vm.flags>>31)){flush();const auto point=vm.flags>>31;manager.cached_draw_state[6]=point;environment.texture_filter(point);}++manager.submitted_draws;
}
// 0x442fe0. Two triangles retain the original 0,1,2 / 1,2,3 winding.
i32 AnmRenderer::append(const AnmVertex* q) noexcept {
#if TH10_ANM_VERTEX_CAP<131072
    if(manager.vertex_write+6>manager.vertex_buffer+TH10_ANM_VERTEX_CAP)flush();   // never on the measured scenes
#endif
    constexpr u32 indices[]={0,1,2,1,2,3};for(auto index:indices){*manager.vertex_write=q[index];++manager.vertex_write;}++manager.batch_quads;return 0;}
// 0x442670. Offset, pixel alignment, UVs, bounds, material state, tint, batching.
i32 AnmRenderer::submit(const AnmVm& vm,u32 flags,bool flip_u){
    auto* q=environment.quad;for(u32 i=0;i<4;++i){q[i].position.x=Scalar::add(q[i].position.x,manager.draw_offset.x);q[i].position.y=Scalar::add(q[i].position.y,manager.draw_offset.y);}
    if(flags&1){const auto aligned=[](float x){
#if TH10_FAST_ALIGN
        // th10_port: round_to_integer's own fast path (Arithmetic.cpp, round to
        // nearest and a float operand) gives an integer r, and r-0.5 is then
        // always accepted by the single-precision subtraction fast path.
        if(single_precision_nearest()&&arithmetic::representable(arithmetic::bits_of(x))){
            const float magnitude=std::fabs(x),r=magnitude>=8388608.f?x:std::copysign((magnitude+8388608.f)-8388608.f,x);return r-0.5f;}
#endif
        return (number(x).round_to_integer()-number(0.5f)).to_float();};q[0].position.x=q[2].position.x=aligned(q[0].position.x);q[1].position.x=q[3].position.x=aligned(q[1].position.x);q[0].position.y=q[1].position.y=aligned(q[0].position.y);q[2].position.y=q[3].position.y=aligned(q[2].position.y);}
    q[0].uv.x=q[2].uv.x=Scalar::add(flip_u?vm.sprite->u1:vm.sprite->u0,vm.uv_offset.x);q[1].uv.x=q[3].uv.x=Scalar::add(flip_u?vm.sprite->u0:vm.sprite->u1,vm.uv_offset.x);q[0].uv.y=q[1].uv.y=Scalar::add(vm.sprite->v0,vm.uv_offset.y);q[2].uv.y=q[3].uv.y=Scalar::add(vm.sprite->v1,vm.uv_offset.y);
    float max_x=q[0].position.x>q[1].position.x?q[0].position.x:q[1].position.x,max_y=q[0].position.y>q[1].position.y?q[0].position.y:q[1].position.y;
    float min_x=q[0].position.x<q[1].position.x?q[0].position.x:q[1].position.x,min_y=q[0].position.y<q[1].position.y?q[0].position.y:q[1].position.y;
    for(u32 i=2;i<4;++i){if(max_x<q[i].position.x)max_x=q[i].position.x;if(max_y<q[i].position.y)max_y=q[i].position.y;if(q[i].position.x<min_x)min_x=q[i].position.x;if(q[i].position.y<min_y)min_y=q[i].position.y;}
    const auto& viewport=*environment.viewport;
    // th10_port: bounds below 2^24 are exact floats, and the comparison is exact either way.
    if(viewport.x+viewport.width<16777216u&&viewport.y+viewport.height<16777216u&&viewport.x+viewport.width>=viewport.x&&viewport.y+viewport.height>=viewport.y){
        if(max_x<float(viewport.x)||max_y<float(viewport.y)||float(viewport.x+viewport.width)<min_x||float(viewport.y+viewport.height)<min_y)return 0;
    }else if(number(max_x)<unsigned_number(viewport.x)||number(max_y)<unsigned_number(viewport.y)||unsigned_number(viewport.x+viewport.width)<number(min_x)||unsigned_number(viewport.y+viewport.height)<number(min_y))return 0;
    if(manager.current_texture!=vm.sprite->texture){manager.current_texture=vm.sprite->texture;flush();environment.set_texture(manager.current_texture);}
    if(manager.cached_draw_state[2]!=1){flush();manager.cached_draw_state[2]=1;}
    if(!(flags&2)){u32 color=vm.flags&0x8000?vm.secondary_color:vm.color;if(manager.tint_enabled){u32 result=0;for(u32 shift=0;shift<32;shift+=8)result|=modulate_channel(color>>shift,manager.tint>>shift)<<shift;color=result;}for(u32 i=0;i<4;++i)q[i].color=color;}
    apply_state(vm);return append(q);
}
// 0x4451c0. The custom mesh and matrix modes have their own render paths.
i32 AnmRenderer::draw(AnmVm& vm){if((vm.flags&3)!=3||!(vm.color>>24))return -1;
#if defined(TH10_TRANSITION_LOWMEM) && TH10_TRANSITION_LOWMEM
    if(anm_released_textures&&vm.animation_file==anm_released_textures)return -1;   // the fading old stage's released pixels
#endif
const auto mode=(vm.flags>>22)&15;if(mode==0)return submit(vm,axis_geometry(vm,environment.quad,true));if(mode==2)return submit(vm,axis_geometry(vm,environment.quad,false));if(mode==1||mode==3)return submit(vm,rotated_geometry(vm,environment.quad));if(mode<=9)return environment.special_draw(manager,vm,mode);return 0;}
// 0x442f30. Called after the previous frame's remaining batch has been flushed.
void AnmRenderer::begin_frame() noexcept {
#ifdef TH10_VERTEX_PEAK
    {const u32 used=u32(manager.vertex_write-manager.vertex_buffer);if(manager.vertex_write&&used>th10_vertex_peak_frame&&used<=131072)th10_vertex_peak_frame=used;}
#endif
    manager.batch_quads=0;manager.vertex_write=manager.batch_start=manager.vertex_buffer;}
// 0x443480 uses half extents with distinct addition and storage order. Its
// submission path, 0x442ad0, reverses U while retaining the vertex winding.
void AnmRenderer::flipped_geometry(const AnmVm& vm,AnmVertex* q) noexcept {
    const auto half_width=number(vm.sprite_size.x)*number(vm.scale.x)*number(.5f);
    const auto half_height=number((number(vm.sprite_size.y)*number(vm.scale.y)*number(.5f)).to_float());
    const auto x=sum(vm.position.x,vm.child_position.x,vm.script_position.x);
    switch((vm.flags>>18)&3){
    case 0:q[0].position.x=q[2].position.x=(x-half_width).to_float();q[1].position.x=q[3].position.x=(x+half_width).to_float();break;
    case 1:q[0].position.x=q[2].position.x=x.to_float();q[1].position.x=q[3].position.x=(half_width+half_width+number(vm.position.x)+number(vm.child_position.x)+number(vm.script_position.x)).to_float();break;
    case 2:q[0].position.x=q[2].position.x=(x-half_width-half_width).to_float();q[1].position.x=q[3].position.x=x.to_float();break;
    }
    const auto y=sum(vm.position.y,vm.child_position.y,vm.script_position.y);
    switch((vm.flags>>20)&3){
    case 0:q[0].position.y=q[1].position.y=(y-half_height).to_float();q[2].position.y=q[3].position.y=(number(vm.position.y)+number(vm.child_position.y)+half_height+number(vm.script_position.y)).to_float();break;
    case 1:q[0].position.y=q[1].position.y=y.to_float();q[2].position.y=q[3].position.y=(half_height+half_height+number(vm.position.y)+number(vm.child_position.y)+number(vm.script_position.y)).to_float();break;
    case 2:q[0].position.y=q[1].position.y=(y-half_height-half_height).to_float();q[2].position.y=q[3].position.y=y.to_float();break;
    }
    depth(q,sum(vm.child_position.z,vm.position.z,vm.script_position.z));
}
i32 AnmRenderer::draw_flipped(const AnmVm& vm){if((vm.flags&3)!=3||!(vm.color>>24))return -1;flipped_geometry(vm,environment.quad);return submit(vm,1,true);}
#if defined(TH10_RENDER_FAST_BULLETS) && TH10_RENDER_FAST_BULLETS
// th10_port (TH10_RENDER_FAST_BULLETS): submit(vm,1,false) for axis_geometry's
// pixel quads, with the same results in the same order. axis_geometry stores
// pairs (q0.x=q2.x, q1.x=q3.x, q0.y=q1.y, q2.y=q3.y), so the draw offset and
// the pixel alignment run once per distinct value; the UVs are a function of
// the sprite's u0/u1/v0/v1, the VM's uv_offset and the arithmetic mode, kept
// from the previous quad when those and the arithmetic mode are bit-identical
// (bullet lists repeat a few sprites). The rest is submit() as written.
namespace {
struct UvMemo {u32 key[6]{},mode=0;bool valid=false;float u0=0,u1=0,v0=0,v1=0;};
UvMemo uv_memo;
inline u32 fbits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
}
i32 AnmRenderer::submit_pixel(const AnmVm& vm,u32 flags){
    if(flags!=1)return submit(vm,flags,false);
    auto* q=environment.quad;
    const float dx=manager.draw_offset.x,dy=manager.draw_offset.y;
    const float ax=Scalar::add(q[0].position.x,dx),bx=Scalar::add(q[1].position.x,dx),ay=Scalar::add(q[0].position.y,dy),by=Scalar::add(q[2].position.y,dy);
    const auto aligned=[](float x){
#if TH10_FAST_ALIGN
        if(single_precision_nearest()&&arithmetic::representable(arithmetic::bits_of(x))){
            const float magnitude=std::fabs(x),r=magnitude>=8388608.f?x:std::copysign((magnitude+8388608.f)-8388608.f,x);return r-0.5f;}
#endif
        return (number(x).round_to_integer()-number(0.5f)).to_float();};
    const float x0=aligned(ax),x1=aligned(bx),y0=aligned(ay),y2=aligned(by);
    q[0].position.x=q[2].position.x=x0;q[1].position.x=q[3].position.x=x1;q[0].position.y=q[1].position.y=y0;q[2].position.y=q[3].position.y=y2;
    {const auto& s=*vm.sprite;const u32 key[6]={fbits(s.u0),fbits(s.u1),fbits(s.v0),fbits(s.v1),fbits(vm.uv_offset.x),fbits(vm.uv_offset.y)};const u32 mode=arithmetic_mode_key();
        if(!uv_memo.valid||mode!=uv_memo.mode||std::memcmp(key,uv_memo.key,sizeof(key))){
            uv_memo.u0=Scalar::add(s.u0,vm.uv_offset.x);uv_memo.u1=Scalar::add(s.u1,vm.uv_offset.x);uv_memo.v0=Scalar::add(s.v0,vm.uv_offset.y);uv_memo.v1=Scalar::add(s.v1,vm.uv_offset.y);
            std::memcpy(uv_memo.key,key,sizeof(key));uv_memo.mode=mode;uv_memo.valid=true;}
        q[0].uv.x=q[2].uv.x=uv_memo.u0;q[1].uv.x=q[3].uv.x=uv_memo.u1;q[0].uv.y=q[1].uv.y=uv_memo.v0;q[2].uv.y=q[3].uv.y=uv_memo.v1;}
    // Bounds exactly as submit() takes them (its order matters for NaN and signed zeros).
    float max_x=q[0].position.x>q[1].position.x?q[0].position.x:q[1].position.x,max_y=q[0].position.y>q[1].position.y?q[0].position.y:q[1].position.y;
    float min_x=q[0].position.x<q[1].position.x?q[0].position.x:q[1].position.x,min_y=q[0].position.y<q[1].position.y?q[0].position.y:q[1].position.y;
    for(u32 i=2;i<4;++i){if(max_x<q[i].position.x)max_x=q[i].position.x;if(max_y<q[i].position.y)max_y=q[i].position.y;if(q[i].position.x<min_x)min_x=q[i].position.x;if(q[i].position.y<min_y)min_y=q[i].position.y;}
    const auto& viewport=*environment.viewport;
    if(viewport.x+viewport.width<16777216u&&viewport.y+viewport.height<16777216u&&viewport.x+viewport.width>=viewport.x&&viewport.y+viewport.height>=viewport.y){
        if(max_x<float(viewport.x)||max_y<float(viewport.y)||float(viewport.x+viewport.width)<min_x||float(viewport.y+viewport.height)<min_y)return 0;
    }else if(number(max_x)<unsigned_number(viewport.x)||number(max_y)<unsigned_number(viewport.y)||unsigned_number(viewport.x+viewport.width)<number(min_x)||unsigned_number(viewport.y+viewport.height)<number(min_y))return 0;
    if(manager.current_texture!=vm.sprite->texture){manager.current_texture=vm.sprite->texture;flush();environment.set_texture(manager.current_texture);}
    if(manager.cached_draw_state[2]!=1){flush();manager.cached_draw_state[2]=1;}
    {u32 color=vm.flags&0x8000?vm.secondary_color:vm.color;if(manager.tint_enabled){u32 result=0;for(u32 shift=0;shift<32;shift+=8)result|=modulate_channel(color>>shift,manager.tint>>shift)<<shift;color=result;}for(u32 i=0;i<4;++i)q[i].color=color;}
    apply_state(vm);return append(q);
}
i32 AnmRenderer::draw_fast(AnmVm& vm){
    if((vm.flags&3)!=3||!(vm.color>>24))return -1;
#if defined(TH10_TRANSITION_LOWMEM) && TH10_TRANSITION_LOWMEM
    if(anm_released_textures&&vm.animation_file==anm_released_textures)return -1;
#endif
    if(((vm.flags>>22)&15)!=0)return draw(vm);
    return submit_pixel(vm,axis_geometry(vm,environment.quad,true));
}
#endif
}
