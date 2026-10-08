#include "Player.hpp"
#if TH10_FAST_SHOT
#include <cmath>
#endif
namespace th10 {
namespace {
struct Rectangle {float left,top,right,bottom;};
#if TH10_FAST_SHOT
// th10_port: the float path of rectangle() and shot_overlaps(). With precision
// 32 nearest and every operand in {0}u[2^-20,2^20], the halves are exact, the
// sums and differences are 0 (exact cancellations) or multiples of 2^-44 below
// 2^21, so the Extended fast paths accept every step and the comparisons are
// exact: the same floats and the same answers as the Extended operations.
inline bool damage_operand(float v){const float a=std::fabs(v);return a==0||(a>=0x1p-20f&&a<=0x1p20f);}
#endif
Rectangle rectangle(const Vec3& point,const Vec2& size){
#if TH10_FAST_SHOT
    if(single_precision_nearest()&&damage_operand(point.x)&&damage_operand(point.y)&&damage_operand(size.x)&&damage_operand(size.y)){
        const float half_x=size.x*.5f,half_y=size.y*.5f;
        return {point.x-half_x,point.y-half_y,half_x+point.x,half_y+point.y};
    }
#endif
    const auto half_x=number(size.x)*number(.5f),half_y=number(size.y)*number(.5f);
    return {(number(point.x)-half_x).to_float(),(number(point.y)-half_y).to_float(),
            (half_x+number(point.x)).to_float(),(half_y+number(point.y)).to_float()};
}
bool shot_overlaps(const PlayerShot& shot,const Rectangle& target){
#if TH10_FAST_SHOT
    {   const float hx=shot.definition->hitbox.x,hy=shot.definition->hitbox.y,px=shot.motion.position.x,py=shot.motion.position.y;
        if(single_precision_nearest()&&damage_operand(hx)&&damage_operand(hy)&&damage_operand(px)&&damage_operand(py)&&
           damage_operand(target.left)&&damage_operand(target.top)&&damage_operand(target.right)&&damage_operand(target.bottom)){
            const float half_x=hx*.5f,half_y=hy*.5f,left=px-half_x,top=py-half_y,right=half_x+px,bottom=half_y+py;
            if(target.right<left||target.bottom<top||bottom<target.top||right<target.left)return false;
            return !((shot.definition->type==3?target.bottom:top)<0.0f);
        }
    }
#endif
    const auto half_x=number(shot.definition->hitbox.x)*number(.5f),half_y=number(shot.definition->hitbox.y)*number(.5f);
    const float left=(number(shot.motion.position.x)-half_x).to_float(),top=(number(shot.motion.position.y)-half_y).to_float();
    const float right=(half_x+number(shot.motion.position.x)).to_float();const auto bottom=half_y+number(shot.motion.position.y);
    if(number(target.right)<number(left)||number(target.bottom)<number(top)||bottom<number(target.top)||number(right)<number(target.left))return false;
    return !(number(shot.definition->type==3?target.bottom:top)<number(0.0f));
}
bool area_overlaps(const DamageArea& area,const Vec3& center,const Vec2& size,const Rectangle& target){
    const auto x=number(center.x)-number(area.motion.position.x),y=number(center.y)-number(area.motion.position.y);
    if(area.flags&2)return !(number(area.radius)*number(area.radius)<x*x+y*y);
    if(number(area.angle)==number(0.0f)){
        const auto half_x=number(area.size.x)*number(.5f),half_y=number(area.size.y)*number(.5f);
        return !(number(target.right)<number(area.motion.position.x)-half_x||half_x+number(area.motion.position.x)<number(target.left)||
                 number(target.bottom)<number(area.motion.position.y)-half_y||half_y+number(area.motion.position.y)<number(target.top));
    }
    const auto sin=number(sine(-number(area.angle)).to_float()),cos=cosine(-number(area.angle));
    const float rotated_x=(cos*x-sin*y).to_float(),rotated_y=(cos*y+sin*x).to_float();
    const auto half_x=number(size.x)*number(.5f),half_y=number(size.y)*number(.5f);
    return !(half_x+number(rotated_x)<number(area.size.x)*number(-.5f)||number(area.size.x)*number(.5f)<number(rotated_x)-half_x||
             half_y+number(rotated_y)<number(area.size.y)*number(-.5f)||number(area.size.y)*number(.5f)<number(rotated_y)-half_y);
}
}
// 0x428630. Collision callbacks may change a shot's definition; fields are
// deliberately reloaded after that call. Area damage is applied after all shots.
i32 Player::damage(const Vec3& center,const Vec2& size,PlayerDamageEnvironment& env,u32* hit_count){
    if(state_timer.current==state_timer.previous)return 0;
    const auto target_bounds=rectangle(center,size);if(hit_count)*hit_count=0;i32 total=0;
    for(auto& shot:shots){
        if(shot.state==0||shot.state==2||!shot_overlaps(shot,target_bounds))continue;
        if(shot.definition->on_hit&&env.shot_hit(*this,shot,center))continue;
        if(!shot.first_collision){env.registry->interrupt(shot.animation,2);shot.first_collision=1;}
        shot.collided=1;
        const auto type=shot.definition->type;
        if(type!=3||(shot.timer.current!=shot.timer.previous&&shot.timer.current%4==0))total=wrapping_add(total,shot.definition->damage);
        if(type!=3){
            const float rotation=env.registry->find_and_clear(shot.animation)->rotation.z;
            env.registry->request_delete(shot.animation);shot.animation=0;
            shot.animation=env.create_animation(*animation_file,shot.definition->hit_animation+5,15);
            auto& animation=*env.registry->find_and_clear(shot.animation);animation.rotation.z=rotation;animation.flags|=4;
            shot.motion.position.z=.1f;shot.state=2;shot.motion.speed=Scalar::mul(shot.motion.speed,.125f);
        }
        if(shot.definition->type==2)emit_circle(shot.motion.position,32.0f,1.4f,13,shot.definition->damage/3,env.default_rate);
    }
    total=wrapping_add(total,env.bomb_damage(center));
    for(auto& area:damage_areas){
        if(!(area.flags&1)||(area.timer.current!=area.timer.previous&&area.timer.current%area.interval==0))continue;
        if(!area_overlaps(area,center,size,target_bounds))continue;
        total=wrapping_add(total,area.damage);area.total_damage=wrapping_add(area.total_damage,area.damage);
        if(area.total_damage>=area.damage_limit)area.damage=0;
    }
    if(total)env.economy->add_score(wrapping_add(total/10,10));return total;
}
}
