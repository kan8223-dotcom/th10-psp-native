#include "AnmFile.hpp"
#include "GameMath.hpp"
#if defined(TH10_FAST_RING) && TH10_FAST_RING
#if !(defined(TH10_TRIG_MEMO) && TH10_TRIG_MEMO)
#error TH10_FAST_RING uses the split DF polar (DfTrig.hpp sincos_angle/sincos_length): build it with TH10_TRIG_MEMO
#endif
#include "../../../portable/numeric/DfTrig.hpp"
#include "FastFloat.hpp"
#endif
namespace th10 {
// 0x43e5a0. Matrix dimensions and UV scale retain their original store order.
i32 AnmFile::bind_sprite(AnmVm& vm,i32 index) noexcept {
    if(!loaded||unavailable)return -1;
    vm.sprite_index=static_cast<std::int16_t>(index);vm.animation_file=this;vm.sprite=&sprites[index];
    vm.sprite_size={vm.sprite->width,vm.sprite->height};
    vm.sprite_matrix.identity();vm.uv_matrix.identity();
    vm.sprite_matrix.elements[0][0]=Scalar::mul(vm.sprite_size.x,0.00390625f);
    vm.sprite_matrix.elements[1][1]=Scalar::mul(vm.sprite_size.y,0.00390625f);
#if defined(TH10_FAST_BIND) && TH10_FAST_BIND
    // th10_port: the two UV scales in plain floats when every step is accepted
    // by its Extended fast path (precision 32 nearest, representable operands,
    // a quotient/product with biased exponent 2..254 or an exact zero: x/y
    // with x == 0, x*y with a zero factor); else the original expressions.
    // Items rebind their sprite on every draw (ItemDraw.cpp).
    if(single_precision_nearest()){
        using namespace arithmetic;
        const float sx=vm.sprite->scale_x,tw=vm.sprite->texture_width,w=vm.sprite_size.x,sy=vm.sprite->scale_y,th=vm.sprite->texture_height,h=vm.sprite_size.y;
        if(representable(bits_of(sx))&&representable(bits_of(tw))&&representable(bits_of(w))&&representable(bits_of(sy))&&representable(bits_of(th))&&representable(bits_of(h))){
            const float qx=sx/tw,qy=sy/th;const u32 qxb=bits_of(qx),qyb=bits_of(qy);
            if(((qxb&0x7fffffffu)?nonzero_accepted(qxb):sx==0)&&((qyb&0x7fffffffu)?nonzero_accepted(qyb):sy==0)){
                const float ux=qx*w,uy=qy*h;const u32 uxb=bits_of(ux),uyb=bits_of(uy);
                if(((uxb&0x7fffffffu)?nonzero_accepted(uxb):(qx==0||w==0))&&((uyb&0x7fffffffu)?nonzero_accepted(uyb):(qy==0||h==0))){
                    vm.uv_matrix.elements[0][0]=ux;vm.transform_matrix=vm.sprite_matrix;vm.uv_matrix.elements[1][1]=uy;return 0;
                }
            }
        }
    }
#endif
    vm.uv_matrix.elements[0][0]=(number(vm.sprite->scale_x)/number(vm.sprite->texture_width)*number(vm.sprite_size.x)).to_float();
    const auto scale_y=number(vm.sprite->scale_y)/number(vm.sprite->texture_height);
    vm.transform_matrix=vm.sprite_matrix;
    vm.uv_matrix.elements[1][1]=(scale_y*number(vm.sprite_size.y)).to_float();
    return 0;
}
// Ring mesh portion of 0x43ee30, 0x441169..0x44133e.
void AnmVm::update_ring_geometry() noexcept {
    auto* vertices=static_cast<AnmVertex*>(geometry);
    const i32 segments=wrapping_add(integer_variables[0],-1);
    const auto denominator=Extended::from_int(segments);
    const float angular_step=(number(6.283185482025146484375f)/denominator).to_float();
    const float texture_step=(Extended::from_int(integer_variables[1])/denominator).to_float();
    float angle=rotation.z,texture_v=0;
    auto* output=vertices;
#if defined(TH10_FAST_RING) && TH10_FAST_RING
    // th10_port (TH08 psp/radial_trig_reuse.hpp sha 3673af12: one angle, two
    // radii). The loop's invariant values are made once, by the same Extended
    // expressions; the angle half of polar's DF path (sincos_angle) once per
    // segment for both radii, sincos_length returning what sincos_scaled does
    // and a decline taking polar's own fallback; the position adds as plain
    // float where Extended's own fast path (both float, precision 32 nearest,
    // accepted result) would give the same bits. Vertices bit-identical
    // (tools/ring_check.cpp).
    const auto half_width=number(scale.x)*number(0.5f);
    const float radii[2]{(half_width+number(scale.y)).to_float(),(number(scale.y)-half_width).to_float()};
    const float u[2]{Scalar::add(sprite->u0,uv_offset.x),Scalar::add(sprite->u1,uv_offset.x)};
    const auto x=number(position.x)+number(script_position.x);
    const auto y=number(Scalar::add(position.y,script_position.y));
    const float z=Scalar::add(position.z,script_position.z);
    const bool float_xy=single_precision_nearest()&&x.tagged()&&y.tagged();const float xf=x.to_float(),yf=y.to_float();
    for(i32 segment=0;segment<segments;++segment){
        const touhou::numeric::df::SinCosAngle a=touhou::numeric::df::sincos_angle(angle);
        const float v=Scalar::add(texture_v,uv_offset.y);
        for(unsigned side=0;side<2;++side){
            auto& vertex=*output++;vertex.reciprocal_w=1;vertex.color=color;vertex.uv={u[side],v};
            Vec2 point;
            if(!touhou::numeric::df::sincos_length(a,angle,radii[side],point.x,point.y))
                point={(cosine(number(angle))*number(radii[side])).to_float(),(sine(number(angle))*number(radii[side])).to_float()};
            float px,py;
            if(float_xy&&fast_float::operand(point.x)&&fast_float::operand(point.y)&&fast_float::add(xf,point.x,px)&&fast_float::add(yf,point.y,py))vertex.position={px,py,z};
            else vertex.position={(x+number(point.x)).to_float(),(y+number(point.y)).to_float(),z};
        }
        texture_v=Scalar::add(texture_step,texture_v);
        angle=TH10_ADD_ANGLE_FLOAT(angle,angular_step);
    }
#else
    for(i32 segment=0;segment<segments;++segment){
        for(unsigned side=0;side<2;++side){
            auto& vertex=*output++;vertex.reciprocal_w=1;vertex.color=color;
            vertex.uv={Scalar::add(side?sprite->u1:sprite->u0,uv_offset.x),Scalar::add(texture_v,uv_offset.y)};
            const auto half_width=number(scale.x)*number(0.5f);
            const float radius=(side?number(scale.y)-half_width:half_width+number(scale.y)).to_float();
            const auto point=polar(angle,radius);
            const auto y=number(Scalar::add(position.y,script_position.y));
            vertex.position={
                (number(position.x)+number(script_position.x)+number(point.x)).to_float(),
                (y+number(point.y)).to_float(),
                Scalar::add(position.z,script_position.z)};
        }
        texture_v=Scalar::add(texture_step,texture_v);
        angle=TH10_ADD_ANGLE_FLOAT(angle,angular_step);
    }
#endif
    output[0]=vertices[0];output[0].uv.y=Scalar::add(texture_v,uv_offset.y);
    output[1]=vertices[1];output[1].uv.y=Scalar::add(texture_v,uv_offset.y);
}
}
