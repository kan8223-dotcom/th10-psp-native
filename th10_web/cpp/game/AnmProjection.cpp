#include "AnmProjection.hpp"
#include <cstring>
#if TH10_TRANSFORM_MEMO
#include <cstdint>
#endif
#include "GameMath.hpp"
#include "../../../portable/numeric/RenderTrig.hpp"
#if TH10_FAST_BILLBOARD || TH10_FAST_MODEL
#include "../../../portable/numeric/SpriteNumber.hpp"
#include <cmath>
#endif
namespace th10 {
namespace {
Extended sum(float a,float b,float c){return number(a)+number(b)+number(c);}
Extended distance(Extended x,Extended y,Extended z){return (z*z+y*y+x*x).square_root();}
u32 selected_color(const AnmVm& vm){return vm.flags&0x8000?vm.secondary_color:vm.color;}
u32 tint(u32 color,const AnmManager& manager){if(!manager.tint_enabled)return color;u32 result=0;for(u32 shift=0;shift<32;shift+=8)result|=AnmRenderer::modulate_channel(color>>shift,manager.tint>>shift)<<shift;return result;}
void anchor(u32 mode,Extended size,Extended& first,Extended& last){switch(mode){case 0:first=number((size*number(-.5f)).to_float());last=size*number(.5f);break;case 1:first=number(0);last=size;break;case 2:first=number((-size).to_float());last=number(0);break;default:__builtin_trap();}}
#if TH10_FAST_BILLBOARD
// th10_port (TH10_FAST_BILLBOARD): billboard_geometry's stages on either number
// type; anchor_as is anchor() with the number conversion passed in.
inline bool billboard_operand(float v,float low,float high) noexcept {const float a=std::fabs(v);return a==0||(a>=low&&a<=high);}
inline touhou::numeric::SpriteNumber root(touhou::numeric::SpriteNumber v){return touhou::numeric::SpriteNumber(std::sqrt(v.value));}
inline Extended root(const Extended& v){return v.square_root();}
template<class Number,class N> void anchor_as(Number number,u32 mode,N size,N& first,N& last){switch(mode){case 0:first=number((size*number(-.5f)).to_float());last=size*number(.5f);break;case 1:first=number(0);last=size;break;case 2:first=number((-size).to_float());last=number(0);break;default:__builtin_trap();}}
#endif
}
#if TH10_TRANSFORM_MEMO
// th10_port (TH10_TRANSFORM_MEMO): update_transform's result is a pure
// function of sprite_matrix, scale, rotation (bits) and the arithmetic mode
// (Scalar::mul and sincos_float's fallback depend on it; the rotation and
// product are plain float). STD objects set the scale-dirty bit on every
// drawn frame (StageRenderer draw_objects), so the same matrix was rebuilt
// each frame, with a DF sincos and a 4x4 product per rotated axis. A slot
// per VM address keeps the inputs and the result; equal inputs give the
// stored result (identity of the VM is not relied on). Only VMs with a
// rotation use it: without one the rebuild is a copy and two products.
namespace {
struct TransformMemo {const AnmVm* vm;u32 mode;Vec2 scale;Vec3 rotation;Matrix4 sprite,result;};
TransformMemo transform_memo[256];
}
#endif
// Shared matrix preparation in 0x444240 and 0x444760.
void AnmProjection::update_transform(AnmVm& vm){
    if((vm.flags&0x4000)||!(vm.flags&12))return;
#if TH10_TRANSFORM_MEMO
    TransformMemo* memo=nullptr;u32 mode=0;
    if(vm.rotation.x!=0||vm.rotation.y!=0||vm.rotation.z!=0){
        memo=&transform_memo[(reinterpret_cast<uintptr_t>(&vm)>>4)&255u];mode=arithmetic_mode_key();
        if(memo->vm==&vm&&memo->mode==mode&&!std::memcmp(&memo->rotation,&vm.rotation,sizeof(Vec3))&&!std::memcmp(&memo->scale,&vm.scale,sizeof(Vec2))&&!std::memcmp(&memo->sprite,&vm.sprite_matrix,sizeof(Matrix4))){
            vm.transform_matrix=memo->result;vm.flags&=~12u;return;}
        
    }
#endif
    vm.transform_matrix=vm.sprite_matrix;
    vm.transform_matrix.elements[0][0]=Scalar::mul(vm.scale.x,vm.transform_matrix.elements[0][0]);
    vm.transform_matrix.elements[1][1]=Scalar::mul(vm.scale.y,vm.transform_matrix.elements[1][1]);vm.flags&=~8u;
    const float* angles[]={&vm.rotation.x,&vm.rotation.y,&vm.rotation.z};
    for(u32 axis=0;axis<3;++axis)if(*angles[axis]!=0){Matrix4 rotation;environment.rotation(rotation,axis,*angles[axis]);environment.multiply(vm.transform_matrix,vm.transform_matrix,rotation);}
    vm.flags&=~4u;
#if TH10_TRANSFORM_MEMO
    if(memo){memo->vm=&vm;memo->mode=mode;memo->scale=vm.scale;memo->rotation=vm.rotation;memo->sprite=vm.sprite_matrix;memo->result=vm.transform_matrix;}
#endif
}
// 0x444240. The model is a 256-unit square; its size was placed in the VM's
// sprite matrix. X/Y translation includes matrix translation, Z replaces it.
i32 AnmProjection::project_quad(AnmVm& vm){
    update_transform(vm);auto world=vm.transform_matrix;
    world.elements[3][0]=(sum(vm.child_position.x,vm.position.x,vm.script_position.x)+number(world.elements[3][0])).to_float();
    world.elements[3][1]=(sum(vm.child_position.y,vm.position.y,vm.script_position.y)+number(world.elements[3][1])).to_float();
    world.elements[3][2]=sum(vm.child_position.z,vm.position.z,vm.script_position.z).to_float();
    constexpr float first[]={-128,0,-256},last[]={128,256,0};const auto horizontal=(vm.flags>>18)&3,vertical=(vm.flags>>20)&3;if(horizontal==3||vertical==3)__builtin_trap();
    const Vec3 points[]={{first[horizontal],first[vertical],0},{last[horizontal],first[vertical],0},{first[horizontal],last[vertical],0},{last[horizontal],last[vertical],0}};
    for(u32 i=0;i<4;++i)environment.project(renderer.environment.quad[i].position,points[i],world);renderer.manager.render_world_matrix=world;return 0;
}
// 0x443b60. Project a point and a camera reference vector to obtain pixel scale,
// then rotate a billboard in screen space. A depth outside [0,1] rejects it.
i32 AnmProjection::billboard_geometry(const AnmVm& vm){
#if TH10_FAST_BILLBOARD
    // th10_port: the operations of the #else branch in three stages that each
    // start and end with floats. A stage runs on plain floats (SpriteNumber)
    // when precision 32 nearest holds and its inputs are in range (0 or a
    // magnitude in the range), otherwise on Extended exactly as before:
    //  translation: positions in [2^-20,2^20]; sums are multiples of 2^-43.
    //  scale: d in [2^-60,2^40], size and scale in [2^-20,2^20]; squares and
    //    their (never cancelling) sum lie in [2^-120,2^82], the root in
    //    [2^-60,2^41], width and height in [2^-101,2^81].
    //  corners: width/height in [2^-40,2^40], cos/sin in [2^-40,2^20], the
    //    center in [2^-20,2^20]; halves >= 2^-41, products >= 2^-81, every
    //    nonzero result a multiple of 2^-104.
    // Every result then has a biased exponent of at least 7 and every zero is
    // an exact cancellation or a zero factor, so the Extended fast paths
    // (Arithmetic.hpp, square_root in Arithmetic.cpp) give the same floats.
    using touhou::numeric::SpriteNumber;
    const bool nearest=single_precision_nearest();
    const auto as_float=[](float v){return SpriteNumber(v);};const auto as_extended=[](float v){return th10::number(v);};
    Matrix4 world;world.identity();
    const auto translate=[&](auto number){
        const auto sum=[&](float a,float b,float c){return number(a)+number(b)+number(c);};
        world.elements[3][0]=sum(vm.child_position.x,vm.position.x,vm.script_position.x).to_float();world.elements[3][1]=sum(vm.child_position.y,vm.position.y,vm.script_position.y).to_float();world.elements[3][2]=sum(vm.child_position.z,vm.position.z,vm.script_position.z).to_float();};
    if(nearest&&touhou::numeric::sprite_range({vm.child_position.x,vm.position.x,vm.script_position.x,vm.child_position.y,vm.position.y,vm.script_position.y,vm.child_position.z,vm.position.z,vm.script_position.z}))translate(as_float);else translate(as_extended);
    Vec3 center,reference;const Vec3 origin{};environment.project(center,origin,world);if(center.z<0||center.z>1)return -1;environment.project(reference,*environment.camera_unit,world);
    const float dx=Scalar::sub(reference.x,center.x),dy=Scalar::sub(reference.y,center.y),dz=Scalar::sub(reference.z,center.z);
    float width_value,height_value;
    const auto scale=[&](auto number){
        const auto ratio=root(number(dz)*number(dz)+number(dy)*number(dy)+number(dx)*number(dx))*number(.5f);
        width_value=(number(vm.sprite_size.x)*number(vm.scale.x)*ratio).to_float();height_value=(number(vm.sprite_size.y)*number(vm.scale.y)*ratio).to_float();};
    if(nearest&&billboard_operand(dx,0x1p-60f,0x1p40f)&&billboard_operand(dy,0x1p-60f,0x1p40f)&&billboard_operand(dz,0x1p-60f,0x1p40f)&&touhou::numeric::sprite_range({vm.sprite_size.x,vm.sprite_size.y,vm.scale.x,vm.scale.y}))scale(as_float);else scale(as_extended);
    auto* q=renderer.environment.quad;for(u32 i=0;i<4;++i)q[i].position.z=center.z;
    float sine_value,cosine_value;
#if TH10_RENDER_FAST_TRIG
    // th10_port: draw-only geometry; ~2 ulp instead of the exact DfTrig path.
    if(!touhou::numeric::render_sincos(vm.rotation.z,sine_value,cosine_value))
#endif
    sincos_float(vm.rotation.z,sine_value,cosine_value);
    const auto corners=[&](auto number){
        using N=decltype(number(0.f));
        const auto width=number(width_value),height=number(height_value);
        const auto c=number(cosine_value),s=number(sine_value),x=number(center.x),y=number(center.y);
        N left,right,top,bottom;anchor_as(number,(vm.flags>>18)&3,width,left,right);anchor_as(number,(vm.flags>>20)&3,height,top,bottom);const auto r=number(right.to_float()),b=number(bottom.to_float());
        q[0].position.x=(left*c-top*s+x).to_float();q[0].position.y=(left*s+top*c+y).to_float();q[1].position.x=(right*c-top*s+x).to_float();q[1].position.y=(top*c+right*s+y).to_float();q[2].position.x=(left*c-bottom*s+x).to_float();q[2].position.y=(left*s+c*bottom+y).to_float();q[3].position.x=(r*c-b*s+x).to_float();q[3].position.y=(r*s+b*c+y).to_float();};
    if(nearest&&billboard_operand(width_value,0x1p-40f,0x1p40f)&&billboard_operand(height_value,0x1p-40f,0x1p40f)&&billboard_operand(cosine_value,0x1p-40f,0x1p20f)&&billboard_operand(sine_value,0x1p-40f,0x1p20f)&&touhou::numeric::sprite_range({center.x,center.y}))corners(as_float);else corners(as_extended);
    return 0;
#else
    Matrix4 world;world.identity();world.elements[3][0]=sum(vm.child_position.x,vm.position.x,vm.script_position.x).to_float();world.elements[3][1]=sum(vm.child_position.y,vm.position.y,vm.script_position.y).to_float();world.elements[3][2]=sum(vm.child_position.z,vm.position.z,vm.script_position.z).to_float();
    Vec3 center,reference;const Vec3 origin{};environment.project(center,origin,world);if(center.z<0||center.z>1)return -1;environment.project(reference,*environment.camera_unit,world);
    const auto dx=number(Scalar::sub(reference.x,center.x)),dy=number(Scalar::sub(reference.y,center.y)),dz=number(Scalar::sub(reference.z,center.z));
    const auto ratio=distance(dx,dy,dz)*number(.5f);const auto width=number((number(vm.sprite_size.x)*number(vm.scale.x)*ratio).to_float()),height=number((number(vm.sprite_size.y)*number(vm.scale.y)*ratio).to_float());
    auto* q=renderer.environment.quad;for(u32 i=0;i<4;++i)q[i].position.z=center.z;
    float sine_value,cosine_value;
#if TH10_RENDER_FAST_TRIG
    // th10_port: draw-only geometry; ~2 ulp instead of the exact DfTrig path.
    if(!touhou::numeric::render_sincos(vm.rotation.z,sine_value,cosine_value))
#endif
    sincos_float(vm.rotation.z,sine_value,cosine_value);
    const auto c=number(cosine_value),s=number(sine_value),x=number(center.x),y=number(center.y);
    Extended left,right,top,bottom;anchor((vm.flags>>18)&3,width,left,right);anchor((vm.flags>>20)&3,height,top,bottom);const auto r=number(right.to_float()),b=number(bottom.to_float());
    q[0].position.x=(left*c-top*s+x).to_float();q[0].position.y=(left*s+top*c+y).to_float();q[1].position.x=(right*c-top*s+x).to_float();q[1].position.y=(top*c+right*s+y).to_float();q[2].position.x=(left*c-bottom*s+x).to_float();q[2].position.y=(left*s+c*bottom+y).to_float();q[3].position.x=(r*c-b*s+x).to_float();q[3].position.y=(r*s+b*c+y).to_float();return 0;

#endif
}
// 0x443f80 / 0x443fb0. Billboard fog uses one distance, integer fog channels
// and fading alpha. Projected polygon fog below uses four distances and keeps A.
i32 AnmProjection::draw_billboard(AnmVm& vm,bool fog_enabled){
    if(billboard_geometry(vm))return -1;if(!fog_enabled)return renderer.submit(vm,0);
    const auto& fog=*environment.fog;const auto denominator=number(fog.near_distance)-number(fog.far_distance);auto color=selected_color(vm);
    const auto px=number(sum(vm.position.x,vm.script_position.x,vm.child_position.x).to_float());
    const auto py=sum(vm.position.y,vm.script_position.y,vm.child_position.y),pz=number(Scalar::add(vm.position.z,vm.script_position.z))+number(vm.child_position.z);
    const auto dx=number((px-number(environment.camera_position->x)).to_float()),dy=number((py-number(environment.camera_position->y)).to_float()),dz=number((pz-number(environment.camera_position->z)).to_float());
    const auto length=distance(dx,dy,dz);color=tint(color,renderer.manager);
    if(number(fog.near_distance)<length){const auto fraction=(number(fog.near_distance)-length)/denominator;if(!(fraction<number(1)))return -1;u32 result=0;
        for(u32 channel=0;channel<3;++channel){const auto value=(color>>(channel*8))&255;const auto delta=wrapping_add(value,static_cast<i32>(0u-static_cast<u32>(Scalar::truncate(fog.color[channel]))));const auto amount=(Extended::from_int(delta)*fraction).truncate_int();result|=static_cast<u8>(value-static_cast<u32>(amount))<<(channel*8);}
        const auto alpha=((number(1)-fraction)*Extended::from_int(color>>24)).truncate_int();color=result|(static_cast<u32>(static_cast<u8>(alpha))<<24);
    }
    for(u32 i=0;i<4;++i)renderer.environment.quad[i].color=color;return renderer.submit(vm,2);
}
// 0x444580 / 0x4445c0.
i32 AnmProjection::draw_projected(AnmVm& vm,bool fog_enabled){
    project_quad(vm);auto* q=renderer.environment.quad;
    if(fog_enabled){const auto& fog=*environment.fog;const auto denominator=number(Scalar::sub(fog.near_distance,fog.far_distance));const auto color=selected_color(vm);
        for(u32 i=0;i<4;++i){float world[4];environment.transform(world,renderer.manager.model_vertices[i].position,renderer.manager.render_world_matrix);
            const auto length=distance(number(world[0])-number(environment.camera_position->x),number(world[1])-number(environment.camera_position->y),number(world[2])-number(environment.camera_position->z));
            auto result=color;if(number(fog.near_distance)<length){const auto fraction=(number(fog.near_distance)-length)/denominator;if(!(fraction<number(1)))result=(fog.packed_color&0xffffff)|(color&0xff000000);else{result=color&0xff000000;for(u32 channel=0;channel<3;++channel){const auto value=(color>>(channel*8))&255;const auto amount=((Extended::from_int(value)-number(fog.color[channel]))*fraction).truncate_int();result|=static_cast<u32>(static_cast<u8>(value-static_cast<u32>(amount)))<<(channel*8);}}}q[i].color=result;
        }
    }
    const auto result=renderer.submit(vm,fog_enabled?2:0);for(u32 i=0;i<4;++i)q[i].reciprocal_w=1;return result;
}
// 0x444ce0. A pretransformed triangle strip bypasses the quad arena.
i32 AnmProjection::draw_strip(AnmVm& vm,const void* vertices,u32 count){
    if((vm.flags&3)!=3||!(vm.color>>24))return -1;auto& manager=renderer.manager;auto& platform=renderer.environment;
    renderer.flush();if(manager.current_texture!=vm.sprite->texture){manager.current_texture=vm.sprite->texture;platform.set_texture(manager.current_texture);}if(manager.cached_draw_state[2]!=3){platform.vertex_format(Layouts::Screen);manager.cached_draw_state[2]=3;}renderer.apply_state(vm);RenderCommands(platform).SetDiffuseArg(TextureArg::Diffuse);platform.draw_triangles(Primitives::Strip,count-2,vertices,28);return 0;
}
// 0x444760. Draw the model-space vertex buffer with world and UV transforms.
i32 AnmProjection::draw_model(AnmVm& vm){
    if((vm.flags&3)!=3||!(vm.color>>24))return -1;renderer.flush();update_transform(vm);auto world=vm.transform_matrix;
#if TH10_FAST_MODEL
    // th10_port (TH10_FAST_MODEL): the translation below on plain floats when
    // precision 32 nearest holds and size, scale and the three positions are 0
    // or a magnitude in [2^-20,2^20] (as TH10_FAST_BILLBOARD's translate and
    // scale stages): sums are multiples of 2^-43 below 2^22, half is 0 or in
    // [2^-41,2^39] and a multiple of 2^-64, so every Extended fast path
    // accepts and gives the same floats. half (no side effects) is formed only
    // by the anchors that read it. Otherwise the Extended lambda as before.
    if(single_precision_nearest()&&touhou::numeric::sprite_range({vm.sprite_size.x,vm.sprite_size.y,vm.scale.x,vm.scale.y,vm.child_position.x,vm.child_position.y,vm.child_position.z,
        vm.position.x,vm.position.y,vm.position.z,vm.script_position.x,vm.script_position.y,vm.script_position.z})){
        const auto translated=[](u32 anchor,float size,float scale,float child,float position,float script,float previous){
            switch(anchor){case 0:return (child+position)+script;case 1:return ((child+position)+script)-std::fabs((size*scale)*.5f);case 2:return ((std::fabs((size*scale)*.5f)+child)+position)+script;default:return previous;}};
        world.elements[3][0]=translated((vm.flags>>18)&3,vm.sprite_size.x,vm.scale.x,vm.child_position.x,vm.position.x,vm.script_position.x,world.elements[3][0]);
        world.elements[3][1]=translated((vm.flags>>20)&3,vm.sprite_size.y,vm.scale.y,vm.child_position.y,vm.position.y,vm.script_position.y,world.elements[3][1]);
        renderer.apply_material(vm);world.elements[3][2]=(vm.child_position.z+vm.script_position.z)+vm.position.z;
    }else{
    const auto translated=[](u32 anchor,float size,float scale,float child,float position,float script,float previous){auto half=(number(size)*number(scale)*number(.5f)).magnitude();switch(anchor){case 0:return sum(child,position,script).to_float();case 1:return (sum(child,position,script)-half).to_float();case 2:return (half+number(child)+number(position)+number(script)).to_float();default:return previous;}};
    world.elements[3][0]=translated((vm.flags>>18)&3,vm.sprite_size.x,vm.scale.x,vm.child_position.x,vm.position.x,vm.script_position.x,world.elements[3][0]);
    world.elements[3][1]=translated((vm.flags>>20)&3,vm.sprite_size.y,vm.scale.y,vm.child_position.y,vm.position.y,vm.script_position.y,world.elements[3][1]);
    renderer.apply_material(vm);world.elements[3][2]=sum(vm.child_position.z,vm.script_position.z,vm.position.z).to_float();
    }
    auto& platform=renderer.environment;auto& manager=renderer.manager;platform.set_transform(Matrices::World,world);
#else
    const auto translated=[](u32 anchor,float size,float scale,float child,float position,float script,float previous){auto half=(number(size)*number(scale)*number(.5f)).magnitude();switch(anchor){case 0:return sum(child,position,script).to_float();case 1:return (sum(child,position,script)-half).to_float();case 2:return (half+number(child)+number(position)+number(script)).to_float();default:return previous;}};
    world.elements[3][0]=translated((vm.flags>>18)&3,vm.sprite_size.x,vm.scale.x,vm.child_position.x,vm.position.x,vm.script_position.x,world.elements[3][0]);
    world.elements[3][1]=translated((vm.flags>>20)&3,vm.sprite_size.y,vm.scale.y,vm.child_position.y,vm.position.y,vm.script_position.y,world.elements[3][1]);
    renderer.apply_material(vm);world.elements[3][2]=sum(vm.child_position.z,vm.script_position.z,vm.position.z).to_float();auto& platform=renderer.environment;auto& manager=renderer.manager;platform.set_transform(Matrices::World,world);
#endif
    if(manager.current_texture!=vm.sprite->texture){manager.current_texture=vm.sprite->texture;platform.set_texture(manager.current_texture);}
    // Both original scroll checks read U. A V-only scroll does not invalidate
    // an unchanged sprite's texture transform, which is preserved here.
    if(manager.current_uv_sprite!=vm.sprite||vm.uv_offset.x!=0){manager.current_uv_sprite=vm.sprite;auto uv=vm.uv_matrix;uv.elements[2][0]=Scalar::add(vm.sprite->u0,vm.uv_offset.x);uv.elements[2][1]=Scalar::add(vm.sprite->v0,vm.uv_offset.y);platform.set_transform(Matrices::Texture,uv);}
    if(manager.cached_draw_state[2]!=2){platform.stream_source(manager.model_vertex_buffer,20);platform.vertex_format(Layouts::World);RenderCommands(platform).SetDiffuseArg(TextureArg::Factor);manager.cached_draw_state[2]=2;}platform.draw_buffer(Primitives::Strip,0,2);return 0;
}
}
