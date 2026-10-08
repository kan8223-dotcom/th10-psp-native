#include "BulletFrame.hpp"
#ifdef TH10_ANM_INDEX_CHECK
#include <cstdio>
#include <cstdlib>
#endif
#include "GameMath.hpp"
#include <cmath>
#if TH10_BULLET_GATE
#include "Player.hpp"
#endif
namespace th10 {
namespace {
#if TH10_BULLET_GATE
// th10_port: the two per-bullet virtual calls that almost never do anything,
// decided here first. The only BulletFrameEnvironment (platform/WorldBullets.cpp
// Bullets, final) forwards run_commands to EnemyBullet::process_commands and
// collide_player to World::collide_player -> Player::collide_rectangle (its
// Damage environment only stores pointers). process_commands returns at once
// unless this test holds (its loop condition and first exit, same order);
// collide_rectangle returns 0 without side effects when player_far holds (its
// first block verbatim, size = {cancel_size, hitbox_height}). A collision
// result of 0 changes nothing in update(), so skipping either call is exact.
inline bool commands_pending(const EnemyBullet& b){return b.command_index<18&&b.commands[b.command_index].type&&(b.commands[b.command_index].concurrent||!b.active_features);}
inline bool player_far(const Vec3& center,const Vec2& size,const PlayerBounds* bounds){
    if(!bounds)return false;
    const float hx=std::fabs(size.x)*.5f,hy=std::fabs(size.y)*.5f,rx=(hx>24.f?hx:24.f)+1.f,ry=(hy>24.f?hy:24.f)+1.f;
    return center.x+rx<bounds->minimum.x||bounds->maximum.x<center.x-rx||center.y+ry<bounds->minimum.y||bounds->maximum.y<center.y-ry;
}
#endif
void reset_timer(Timer& timer,u32& flags,const float* rate){if(!(flags&1)){flags|=1;timer.rate=rate;}timer.initialize(-1);}
#if TH10_HOT_MOVE
// The original operations for move() below when its float path declines, kept
// out of the per-bullet code (cold, not inlined).
__attribute__((noinline,cold)) void move_general(EnemyBulletMotion& motion,float rate,bool half,float zmul){
    auto x=number(rate)*number(motion.velocity.x),y=number(rate)*number(motion.velocity.y);
    auto z=number(zmul);
    if(half){x=number((x*number(.5f)).to_float());y=y*number(.5f);z=z*number(.5f);}
    motion.position.x=(x+number(motion.position.x)).to_float();motion.position.y=(y+number(motion.position.y)).to_float();motion.position.z=(z+number(motion.position.z)).to_float();
}
#endif
void move(EnemyBulletMotion& motion,float rate,bool half){
#if TH10_HOT_MOVE
    // th10_port: the per-bullet path in plain floats. Every operation is
    // accepted by the same test as its Extended fast path (Arithmetic.hpp:
    // single precision nearest, tagged operands, a result with biased exponent
    // 2..254 or an exact zero) and then gives that float; if any test fails,
    // move_general runs the original operations from the unchanged inputs.
    // Scalar::mul is evaluated once either way (it may count a SoftFloat call).
    const float zmul=Scalar::mul(rate,motion.velocity.z);
    if(single_precision_nearest()){
        using namespace arithmetic;
        const float vx=motion.velocity.x,vy=motion.velocity.y,px=motion.position.x,py=motion.position.y,pz=motion.position.z;
        const auto mul_ok=[](float a,float b,float r){const u32 bits=bits_of(r);return (bits&0x7fffffffu)?nonzero_accepted(bits):(a==0||b==0);};
        const auto add_ok=[](float a,float b,float r){const u32 bits=bits_of(r);return (bits&0x7fffffffu)?nonzero_accepted(bits):a==-b;};
        if(representable(bits_of(rate))&&representable(bits_of(vx))&&representable(bits_of(vy))&&representable(bits_of(zmul))&&representable(bits_of(px))&&representable(bits_of(py))&&representable(bits_of(pz))){
            float x=rate*vx,y=rate*vy,z=zmul;bool ok=mul_ok(rate,vx,x)&&mul_ok(rate,vy,y);
            if(half){const float hx=x*.5f,hy=y*.5f,hz=z*.5f;ok=ok&&mul_ok(x,.5f,hx)&&mul_ok(y,.5f,hy)&&mul_ok(z,.5f,hz);x=hx;y=hy;z=hz;}
            const float nx=x+px,ny=y+py,nz=z+pz;
            if(ok&&add_ok(x,px,nx)&&add_ok(y,py,ny)&&add_ok(z,pz,nz)){motion.position.x=nx;motion.position.y=ny;motion.position.z=nz;return;}
        }
    }
    move_general(motion,rate,half,zmul);
#else
    auto x=number(rate)*number(motion.velocity.x),y=number(rate)*number(motion.velocity.y);
    auto z=number(Scalar::mul(rate,motion.velocity.z));
    if(half){x=number((x*number(.5f)).to_float());y=y*number(.5f);z=z*number(.5f);}
    motion.position.x=(x+number(motion.position.x)).to_float();motion.position.y=(y+number(motion.position.y)).to_float();motion.position.z=(z+number(motion.position.z)).to_float();
#endif
}
void effect(EnemyBullet& bullet,i32 script,BulletFrameEnvironment& env){env.manager->create_at(*env.effect_file,script,bullet.motion.position,true,AnimationPlacement::WorldBack,*env.animations,*env.allocation);}
}
// 0x405d00 / 0x405de0. Construction preserves non-animation storage.
void EnemyBullet::initialize() noexcept {
    animation.clear();cancel_timer_flags&=~1u;secondary_timer_flags&=~1u;for(auto& modifier:modifiers)modifier.timer_flags&=~1u;
}
void EnemyBullet::release(AnmAllocationEnvironment& env){if(animation.geometry)env.release_memory(animation.geometry);animation.geometry=nullptr;}
// 0x405be0. Recycling only resets state and the two phase counters.
void EnemyBullet::reset(const float* rate) noexcept {state=0;reset_timer(cancel_timer,cancel_timer_flags,rate);reset_timer(secondary_timer,secondary_timer_flags,rate);}
// 0x406160 / 0x4061d0, with/without the extra 64 pixels above the playfield.
bool bullet_outside_playfield(const Vec3& point,float width,float height,bool extended_top) noexcept {
    // th10_port: exact early out. With a margin of 1 (float rounding here is
    // below 1e-4), a point this far inside cannot meet any test below.
    {   const float hx=std::fabs(width)*.5f,hy=std::fabs(height)*.5f;
        if(point.x-hx-1.f>-192.f&&point.x+hx+1.f<192.f&&point.y-hy-1.f>(extended_top?-64.f:0.f)&&point.y+hy+1.f<448.f)return false;}
#if TH10_FAST_OUTSIDE
    // th10_port: the general test in plain floats. With precision 32 nearest
    // and width, height, x, y in {0}u[2^-20,2^20], the halves are exact and the
    // sums and differences are 0 (exact cancellations) or multiples of 2^-44
    // below 2^21, all accepted by the Extended fast paths; "<||==" is "<=" on
    // non-NaN floats.
    {   const auto in_range=[](float v){const float a=std::fabs(v);return a==0||(a>=0x1p-20f&&a<=0x1p20f);};
        if(single_precision_nearest()&&in_range(width)&&in_range(height)&&in_range(point.x)&&in_range(point.y)){
            const float half_x=width*.5f,half_y=height*.5f,left=half_x+point.x,right=point.x-half_x,top=half_y+point.y,bottom=point.y-half_y,top_edge=extended_top?-64.f:0.f;
            return left<=-192.f||192.f<=right||top<=top_edge||448.f<=bottom;
        }
    }
#endif
    const auto half_x=number(width)*number(.5f),half_y=number(height)*number(.5f);
    const auto left=half_x+number(point.x),right=number(point.x)-half_x,top=half_y+number(point.y),bottom=number(point.y)-half_y;
    const auto top_edge=number(extended_top?-64.f:0.f);
    return left<number(-192.f)||left==number(-192.f)||number(192.f)<right||right==number(192.f)||top<top_edge||top==top_edge||number(448.f)<bottom||bottom==number(448.f);
}
// 0x406240. Spawn animations move at half speed; finishing that animation
// switches to the normal path during the same frame, including its movement.
i32 EnemyBullet::update(BulletFrameEnvironment& env){
    if(flags&8){reset(env.default_rate);return -1;}
    bool active=state==1;
    if(state==2){move(motion,*env.default_rate,true);if(animation.integer_variables[0]){state=1;active=true;}}
    else if(state==3)move(motion,*env.default_rate,true);
    if(active){
#if TH10_BULLET_GATE
        if(commands_pending(*this))
#endif
        env.run_commands(*this);
        if(active_features){
            constexpr u32 masks[]={1,0x10,0x20,0x40,0x100,0x80,0x8000c00,0x4000000};
            for(u32 i=0;i<8;i++)if(active_features&masks[i])env.update_feature(*this,static_cast<BulletFeature>(i));
            if(active_features&0x8000){if(modifiers[5].timer.current<=0)active_features^=0x8000;else TH10_TIMER_ADVANCE(modifiers[5].timer,-1.f);}
        }
        move(motion,*env.default_rate,false);
        if(flags&2
#if TH10_BULLET_GATE
           &&!player_far(motion.position,{cancel_size,hitbox_height},env.player_collision_bounds)
#endif
          ){
            const i32 collision=env.collide_player(motion.position,{cancel_size,hitbox_height});
            if(collision==1){state=3;animation.pending_interrupt=1;if(cancel_script>=0)effect(*this,cancel_script,env);}
            else if(collision==2&&!(flags&4)){flags|=4;effect(*this,0x1b2,env);env.play_sound(28,motion.position.x);}
        }
    }
    if(animation.sprite){
        if(active_features&0x100000)env.update_feature(*this,BulletFeature::HorizontalWrap);
        if(active_features&0x200000)env.update_feature(*this,BulletFeature::VerticalWrap);
        if(outside_delay<=0&&bullet_outside_playfield(motion.position,animation.sprite->width,animation.sprite->height,true)){reset(env.default_rate);return -1;}
    }
    if(cancel_protection)--cancel_protection;if(outside_delay>0)--outside_delay;
    if(animation.update(*env.animations)){reset(env.default_rate);return -1;}return 0;
}
// 0x4065c0. Rebuild six drawing lists in pool order and advance surviving
// bullets' phase timers, including the special update-suppression mode.
i32 EnemyBulletManager::update(BulletFrameEnvironment& env){
    active_count=0;for(auto& head:draw_heads)head=nullptr;for(auto& tail:draw_tails)tail=nullptr;
#ifdef TH10_ANM_INDEX_CHECK
    for(u32 i=0;i<2000;++i)if(pool[i].state&&!(live_slots[i>>5]&(1u<<(i&31)))){std::fprintf(stderr,"live_slots misses bullet %u (state %u)\n",i,unsigned(pool[i].state));std::abort();}
#endif
    // th10_port: slot order over live_slots (see BulletFrame.hpp). The word is
    // read again after each bullet, so a slot activated during an update is
    // still visited this tick when it lies ahead, as in the full scan.
    for(u32 word=0;word<63;++word)for(u32 bits=live_slots[word];bits;){
        const u32 bit=static_cast<u32>(__builtin_ctz(bits));auto& bullet=pool[word*32+bit];
        if(!bullet.state)live_slots[word]&=~(1u<<bit);
        else if((env.controller_flags&&(*env.controller_flags&0x402)==0x402)||bullet.update(env)==0){
            auto*& head=draw_heads[bullet.draw_layer];auto*& tail=draw_tails[bullet.draw_layer];
            if(head)tail->draw_next=&bullet;else head=&bullet;tail=&bullet;bullet.draw_next=nullptr;++active_count;TH10_TIMER_TICK(bullet.cancel_timer);
        }
        bits=live_slots[word]&~((2u<<bit)-1u);
    }
    return 1;
}
// 0x4066e0 / 0x4066c0.
i32 EnemyBulletManager::draw_layer(i32 layer,BulletFrameEnvironment& env){
    for(auto* bullet=draw_heads[layer];bullet;bullet=bullet->draw_next){
        auto& vm=bullet->animation;const auto& point=bullet->motion.position;
        vm.script_position={Scalar::add(point.x,224.f),Scalar::add(point.y,16.f),point.z};
        if(vm.flags&0x8000000){vm.rotation.z=TH10_ADD_ANGLE_FLOAT(bullet->motion.angle,1.57079637050628662109375f);vm.flags|=4;}
        env.submit(vm);
    }
    return 1;
}
i32 EnemyBulletManager::draw(BulletFrameEnvironment& env){for(i32 layer=0;layer<6;layer++)draw_layer(layer,env);return 1;}
i32 EnemyBulletManager::tick(BulletFrameEnvironment& env){return env.controller_flags&&(*env.controller_flags&5)?1:update(env);}
i32 EnemyBulletManager::render(BulletFrameEnvironment& env){return env.controller_flags&&(*env.controller_flags&4)?1:draw(env);}
}
