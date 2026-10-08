#include "BulletFrame.hpp"
#include "GameMath.hpp"
#include <cmath>
#if TH10_FAST_TURN
#include "FastFloat.hpp"
#endif
namespace th10 {
namespace {
#ifdef TH10_TRIG_MEMO_PROBE
}
// th10_port measurement (PC, checklist A-4): how often a bullet asks polar()
// for the angle it asked last time (the TH08 r259 per-bullet memo's hit rate).
extern "C" {unsigned long long th10_memo_calls=0,th10_memo_repeats=0;}
namespace {
#endif
void velocity(EnemyBulletMotion& motion,float speed){
#ifdef TH10_TRIG_MEMO_PROBE
    {static const EnemyBulletMotion* last_motion[4096];static u32 last_bits[4096];const u32 slot=u32(reinterpret_cast<uintptr_t>(&motion)/sizeof(void*))&4095u;u32 bits;__builtin_memcpy(&bits,&motion.angle,4);
        ++th10_memo_calls;if(last_motion[slot]==&motion&&last_bits[slot]==bits)++th10_memo_repeats;last_motion[slot]=&motion;last_bits[slot]=bits;}
#endif
    const auto xy=TH10_POLAR_MEMO(&motion,motion.angle,speed);motion.velocity.x=xy.x;motion.velocity.y=xy.y;}
void reset_modifier(ProjectileModifier& modifier,const float* rate){if(!(modifier.timer_flags&1)){modifier.timer_flags|=1;modifier.timer.rate=rate;}modifier.timer.initialize(-1);}
float aim(const Vec3& from,const Vec3& to){const auto x=number(to.x)-number(from.x),y=number(to.y)-number(from.y);return x==number(0.f)&&y==number(0.f)?1.57079637050628662109375f:angle_to_float(y,x);}
#if TH10_FAST_FEATURES
// th10_port: (number(a)*number(b)+number(c)).to_float() and the spawn speed
// (5 - f*0.3125 + speed) in plain floats when every Extended fast path accepts
// (Arithmetic.hpp: precision 32 nearest, float operands, a result with biased
// exponent 2..254 or an exact zero); otherwise the original operations.
inline bool feature_operand(float v){return arithmetic::representable(arithmetic::bits_of(v));}
inline bool accepted_mul(float x,float y,float r){const u32 b=arithmetic::bits_of(r);return (b&0x7fffffffu)?arithmetic::nonzero_accepted(b):(x==0||y==0);}
inline bool accepted_add(float x,float y,float r){const u32 b=arithmetic::bits_of(r);return (b&0x7fffffffu)?arithmetic::nonzero_accepted(b):x==-y;}
inline bool accepted_sub(float x,float y,float r){const u32 b=arithmetic::bits_of(r);return (b&0x7fffffffu)?arithmetic::nonzero_accepted(b):x==y;}
float mul_add(float a,float b,float c){
    if(single_precision_nearest()&&feature_operand(a)&&feature_operand(b)&&feature_operand(c)){
        const float product=a*b;if(accepted_mul(a,b,product)){const float sum=product+c;if(accepted_add(product,c,sum))return sum;}}
    return (number(a)*number(b)+number(c)).to_float();
}
float spawn_speed(float fractional,float speed){
    if(single_precision_nearest()&&feature_operand(fractional)&&feature_operand(speed)){
        const float step=fractional*.3125f;
        if(accepted_mul(fractional,.3125f,step)){const float rest=5.f-step;
            if(accepted_sub(5.f,step,rest)){const float sum=rest+speed;if(accepted_add(rest,speed,sum))return sum;}}}
    return (number(5.f)-number(fractional)*number(.3125f)+number(speed)).to_float();
}
#endif
#if TH10_FAST_TURN
// th10_port: a turning bullet's deceleration,
// (number(s)-number(s)*number(f)/Extended::from_int(d)).to_float(), in plain
// floats when every step is accepted by its Extended fast path (FastFloat.hpp,
// Arithmetic.hpp: precision 32 nearest, float operands, from_int_fast's
// |d| <= 2^24, a result with biased exponent 2..254 or an exact zero: a zero
// product only from a zero factor, a zero quotient only from a zero dividend,
// a zero difference only from equal operands); otherwise the original
// expression from the same inputs. tools/turn_check.cpp.
inline float turn_speed(float s,float f,i32 d){
    if(single_precision_nearest()&&fast_float::operand(s)&&fast_float::operand(f)&&d>=-16777216&&d<=16777216){
        float product,difference;
        if(fast_float::mul(s,f,product)){
            const float quotient=product/static_cast<float>(d);const u32 b=arithmetic::bits_of(quotient);
            if(((b&0x7fffffffu)?arithmetic::nonzero_accepted(b):product==0)&&fast_float::sub(s,quotient,difference))return difference;
        }
    }
    return (number(s)-number(s)*number(f)/Extended::from_int(d)).to_float();
}
#endif
}
// 0x4074b0..0x40802f. Modifier timers and phase transitions retain the order
// used by the original instruction stream, including completion-frame ticks.
void EnemyBullet::update_feature(BulletFeature feature,BulletFrameEnvironment& env){
    if(feature==BulletFeature::SpawnAcceleration){
#if TH10_FAST_FEATURES
        auto& m=modifiers[0];if(m.timer.current<=16)velocity(motion,spawn_speed(m.timer.fractional,motion.speed));else active_features^=1;TH10_TIMER_TICK(m.timer);
#else
        auto& m=modifiers[0];if(m.timer.current<=16)velocity(motion,(number(5.f)-number(m.timer.fractional)*number(.3125f)+number(motion.speed)).to_float());else active_features^=1;TH10_TIMER_TICK(m.timer);
#endif
    }else if(feature==BulletFeature::VectorAcceleration){
        auto& m=modifiers[1];
        if(m.timer.current>=m.duration)active_features&=~0x10u;
        else{
#if TH10_FAST_FEATURES
            motion.speed=mul_add(*env.default_rate,m.first,motion.speed);
            const float z=Scalar::mul(*env.default_rate,m.vector.z);
            motion.velocity.x=mul_add(*env.default_rate,m.vector.x,motion.velocity.x);motion.velocity.y=mul_add(*env.default_rate,m.vector.y,motion.velocity.y);motion.velocity.z=Scalar::add(z,motion.velocity.z);
#else
            motion.speed=(number(*env.default_rate)*number(m.first)+number(motion.speed)).to_float();
            const auto x=number(*env.default_rate)*number(m.vector.x),y=number(*env.default_rate)*number(m.vector.y);const float z=Scalar::mul(*env.default_rate,m.vector.z);
            motion.velocity.x=(x+number(motion.velocity.x)).to_float();motion.velocity.y=(y+number(motion.velocity.y)).to_float();motion.velocity.z=Scalar::add(z,motion.velocity.z);
#endif
            if(std::fabs(motion.velocity.x)>.0001f||std::fabs(motion.velocity.y)>.0001f)motion.angle=angle_to_float(number(motion.velocity.y),number(motion.velocity.x));
        }
        TH10_TIMER_TICK(m.timer);
    }else if(feature==BulletFeature::AngularAcceleration){
        auto& m=modifiers[2];if(m.timer.current>=m.duration)active_features&=~0x20u;
#if TH10_FAST_FEATURES
        else{motion.angle=TH10_ADD_ANGLE_FLOAT(motion.angle,Scalar::mul(*env.default_rate,m.second));motion.speed=mul_add(*env.default_rate,m.first,motion.speed);velocity(motion,motion.speed);}TH10_TIMER_TICK(m.timer);
#else
        else{motion.angle=TH10_ADD_ANGLE_FLOAT(motion.angle,Scalar::mul(*env.default_rate,m.second));motion.speed=(number(*env.default_rate)*number(m.first)+number(motion.speed)).to_float();velocity(motion,motion.speed);}TH10_TIMER_TICK(m.timer);
#endif
    }else if(feature==BulletFeature::Turn||feature==BulletFeature::TurnToAngle||feature==BulletFeature::TurnAimed){
        auto& m=modifiers[3];float speed;
        if(m.timer.current>=m.duration){
            if(turn_sound>=0)env.play_turn_sound(turn_sound);
            m.iteration=wrapping_add(m.iteration,1);if(m.iteration>=m.count)active_features&=~(feature==BulletFeature::Turn?0x40u:feature==BulletFeature::TurnToAngle?0x100u:0x80u);
            if(feature==BulletFeature::Turn)motion.angle=Scalar::add(m.second,motion.angle);
            else if(feature==BulletFeature::TurnToAngle)motion.angle=m.second;
            else motion.angle=TH10_ADD_ANGLE_FLOAT(aim(motion.position,*env.player_position),m.second);
            speed=motion.speed=m.first;reset_modifier(m,env.default_rate);
        }else
#if TH10_FAST_TURN
            speed=turn_speed(motion.speed,m.timer.fractional,m.duration);
#else
            speed=(number(motion.speed)-number(motion.speed)*number(m.timer.fractional)/Extended::from_int(m.duration)).to_float();
#endif
        velocity(motion,speed);TH10_TIMER_TICK(m.timer);
    }else if(feature==BulletFeature::Reflect){
        auto& m=modifiers[4];
        if(!bullet_outside_playfield(motion.position,0,0,false))return;
        if(turn_sound>=0)env.play_turn_sound(turn_sound);bool reflected=false;
        if(motion.position.x< -192.f||motion.position.x>=192.f){
            motion.angle=TH10_ADD_ANGLE_FLOAT((-number(motion.angle)-number(3.1415927410125732421875f)).to_float(),0);reflected=true;
            motion.position.x=Scalar::sub(motion.position.x< -192.f?-384.f:384.f,motion.position.x);
        }
        if(!(active_features&0x8000000)&&(motion.position.y<0.f||(motion.position.y>=448.f&&(active_features&0x400)))){
            motion.angle=(-number(motion.angle)).to_float();reflected=true;
            motion.position.y=(motion.position.y<0.f?-number(motion.position.y):number(448.f)-number(motion.position.y)+number(448.f)).to_float();
        }
        if(m.first> -990.f)motion.speed=m.first;velocity(motion,motion.speed);
        if(reflected)m.duration=wrapping_add(m.duration,1);if(m.duration>=m.count)active_features&=~0x8000c00u;
    }else if(feature==BulletFeature::Homing){
        auto& m=modifiers[8];
        if(m.timer.current>=m.duration)active_features&=~0x4000000u;
        else{
            const float desired=TH10_ADD_ANGLE_FLOAT(m.second,aim(motion.position,*env.player_position));
            const float delta=(angle_difference(desired,motion.angle)*number(m.first)*number(*env.default_rate)).to_float();
            motion.angle=TH10_ADD_ANGLE_FLOAT(motion.angle,delta);velocity(motion,motion.speed);
        }
        TH10_TIMER_TICK(m.timer);
    }else{
        const bool horizontal=feature==BulletFeature::HorizontalWrap;auto& m=modifiers[horizontal?6:7];
        if(!bullet_outside_playfield(motion.position,animation.sprite->width,animation.sprite->height,false))return;
        float& coordinate=horizontal?motion.position.x:motion.position.y;const float low=horizontal?-192.f:0.f,high=horizontal?192.f:448.f;
        const float dimension=horizontal?animation.sprite->width:animation.sprite->height,span=horizontal?384.f:448.f;bool wrapped=false;
        if(coordinate<low){coordinate=(number(dimension)+number(coordinate)+number(span)).to_float();wrapped=true;}
        else if(coordinate>high){coordinate=(number(coordinate)-(number(dimension)+number(span))).to_float();wrapped=true;}
        if(wrapped){m.timer.advance(-1);if(turn_sound>=0)env.play_turn_sound(turn_sound);}
        if(m.timer.current<=0)active_features^=horizontal?0x100000u:0x200000u;
    }
}
}
