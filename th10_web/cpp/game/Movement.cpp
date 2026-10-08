#include "Movement.hpp"
#include <cmath>
namespace th10 {
void Movement::update_velocity() noexcept {
    if(!(flags&1)){const auto next=TH10_POLAR_MEMO(this,angle,speed);velocity={next.x,next.y,0};}
    else{radius=Scalar::add(radial_velocity,radius);angle=normalize_angle(Scalar::add(speed,angle)).to_float();}
}
// 0x44c2a0. The original snaps x/y downward, including negative coordinates.
// The multiply is extended precision, then explicitly spilled to double before
// floor, then multiplied by the original *float* approximation of 0.01.
void Movement::update() noexcept {
    if(flags&1){
        const auto offset=polar(angle,radius);
        position.x=Scalar::add(offset.x,velocity.x);
        position.y=Scalar::add(offset.y,velocity.y);
        position.z=velocity.z;
    }else{
        position.x=Scalar::add(position.x,velocity.x);
        position.y=Scalar::add(velocity.y,position.y);
        position.z=Scalar::add(velocity.z,position.z);
    }
    const auto snap=[](float value){
#if TH10_FAST_FLOOR
        // th10_port: the same steps without double (software on the PSP). With
        // precision 32 nearest and a float operand, value*100 is the fast path's
        // float when accepted; floor of a float is an integer float (floorf
        // equals the double floor), from_double keeps it as that float, and the
        // last product is again the fast path's float when accepted.
        using namespace arithmetic;
        if(single_precision_nearest()&&representable(bits_of(value))){
            const float scaled=value*100.0f;const u32 scaled_bits=bits_of(scaled);
            if((scaled_bits&0x7fffffffu)?nonzero_accepted(scaled_bits):value==0){
                const float whole=std::floor(scaled),result=whole*0.01f;const u32 result_bits=bits_of(result);
                if((result_bits&0x7fffffffu)?nonzero_accepted(result_bits):whole==0)return result;
            }
        }
#endif
        return (Extended::from_double(std::floor((number(value)*number(100.0f)).to_double()))*number(0.01f)).to_float();
    };
    position.x=snap(position.x);position.y=snap(position.y);
}
}
