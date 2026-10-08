#include "Item.hpp"
#include "GameMath.hpp"
#if defined(TH10_FAST_ITEM) && TH10_FAST_ITEM
#include "FastFloat.hpp"
#include "../../../portable/numeric/DfAtan2.hpp"
#endif
#ifdef TH10_ANM_INDEX_CHECK
#include <cstdio>
#include <cstdlib>
#endif
namespace th10 {
// The original rectangle comparisons treat unordered coordinates as inside.
bool ItemRegion::contains(const Vec3& point) const noexcept {
    return !(number(point.x)<number(minimum.x)||number(point.y)<number(minimum.y)||
             number(maximum.x)<number(point.x)||number(maximum.y)<number(point.y));
}
namespace {
#if defined(TH10_FAST_ITEM) && TH10_FAST_ITEM
// th10_port: the item update's Extended expressions in plain floats. Each
// float step is accepted by the same test as its Extended fast path
// (FastFloat.hpp: precision 32 nearest, representable operands, a result with
// biased exponent 2..254 or an exact zero), so an accepted result is the
// tagged Extended's float; any declined step runs the original statement.
// Comparisons of representable floats are the tagged comparisons (any mode).
namespace ff=fast_float;
inline bool less(float a,float b) noexcept {return ff::operand(a)&&ff::operand(b)?a<b:number(a)<number(b);}
inline bool region_contains(const ItemRegion& r,const Vec3& p) noexcept {
    if(ff::operand(p.x)&&ff::operand(p.y)&&ff::operand(r.minimum.x)&&ff::operand(r.minimum.y)&&ff::operand(r.maximum.x)&&ff::operand(r.maximum.y))
        return !(p.x<r.minimum.x||p.y<r.minimum.y||r.maximum.x<p.x||r.maximum.y<p.y);
    return r.contains(p);
}
// number(rate)*number(v)+number(p) (advance_position) and number(rate)*number(.03f)+number(v) (gravity).
inline bool scaled_sum(float rate,float v,float p,float& out) noexcept {
    float product;return ff::operand(v)&&ff::operand(p)&&ff::mul(rate,v,product)&&ff::add(product,p,out);
}
inline bool float_rate(float rate) noexcept {return single_precision_nearest()&&ff::operand(rate);}
// The item timer is only ticked (Timer::tick's unscaled branch inline; the scaled one calls it).
inline void tick_item_timer(Timer& timer) noexcept {
    const float rate=*timer.rate;
    if(0.99f<rate&&rate<1.01f){timer.previous=timer.current;timer.current=wrapping_add(timer.current,1);timer.fractional=Scalar::add(timer.fractional,1.0f);}
    else timer.tick();
}
#endif
void advance_position(Item& item,float rate){
#if defined(TH10_FAST_ITEM) && TH10_FAST_ITEM
    {   float x,y;
        if(float_rate(rate)&&scaled_sum(rate,item.velocity.x,item.position.x,x)&&scaled_sum(rate,item.velocity.y,item.position.y,y)){
            const float z=Scalar::mul(rate,item.velocity.z);
            item.position.x=x;item.position.y=y;item.position.z=Scalar::add(z,item.position.z);return;
        }
    }
#endif
    const auto x=number(rate)*number(item.velocity.x),y=number(rate)*number(item.velocity.y);
    const float z=Scalar::mul(rate,item.velocity.z);
    item.position.x=(x+number(item.position.x)).to_float();item.position.y=(y+number(item.position.y)).to_float();
    item.position.z=Scalar::add(z,item.position.z);
}
void attract(Item& item,ItemFrameEnvironment& env){
#if defined(TH10_FAST_ITEM) && TH10_FAST_ITEM
    {   const float px=env.player_position->x,py=env.player_position->y,ix=item.position.x,iy=item.position.y;float dx,dy;
        if(single_precision_nearest()&&ff::operand(px)&&ff::operand(ix)&&ff::operand(py)&&ff::operand(iy)&&ff::sub(px,ix,dx)&&ff::sub(py,iy,dy)){
            // angle_to_float(y,x) on tagged operands: DfAtan2, else libm through angle_to.
            float angle=1.5707963705062866f;
            if(!(dx==0.0f&&dy==0.0f)&&!touhou::numeric::df::atan2_float(dy,dx,angle))angle=angle_to(number(dy),number(dx)).to_float();
#if defined(TH10_ITEM_POLAR_MEMO) && TH10_ITEM_POLAR_MEMO
            const auto velocity=TH10_POLAR_MEMO(&item,angle,item.attraction_speed);
#else
            const auto velocity=polar(angle,item.attraction_speed);
#endif
            item.velocity.x=velocity.x;item.velocity.y=velocity.y;
            advance_position(item,*env.default_rate);
            if(less(item.attraction_speed,12.0f))item.attraction_speed=Scalar::add(item.attraction_speed,.2f);
            if(*env.player_state==4){item.state=1;item.velocity.x=item.velocity.y=0;}
            return;
        }
    }
#endif
    const auto x=number(env.player_position->x)-number(item.position.x),y=number(env.player_position->y)-number(item.position.y);
    const float angle=x==number(0.0f)&&y==number(0.0f)?1.5707963705062866f:angle_to_float(y,x);
#if defined(TH10_ITEM_POLAR_MEMO) && TH10_ITEM_POLAR_MEMO
    const auto velocity=TH10_POLAR_MEMO(&item,angle,item.attraction_speed);item.velocity.x=velocity.x;item.velocity.y=velocity.y;
#else
    const auto velocity=polar(angle,item.attraction_speed);item.velocity.x=velocity.x;item.velocity.y=velocity.y;
#endif
    advance_position(item,*env.default_rate);
    if(number(item.attraction_speed)<number(12.0f))item.attraction_speed=Scalar::add(item.attraction_speed,.2f);
    if(*env.player_state==4){item.state=1;item.velocity.x=item.velocity.y=0;}
}
bool collect(Item& item,ItemFrameEnvironment& env){
    auto& economy=*env.economy;bool full_power=false;
    switch(item.kind){
    case 1:case 10:{
        const bool power_up=economy.add_power(1,env);env.update_power_display(economy.power/20,(economy.power%20)*100/20);
        if(power_up){
            env.refresh_player_power();env.popup(item.position,-1,0xffffff40);env.play_sound(0x1d,item.position.x);
            if(economy.power>=100)full_power=true;
            economy.add_rank(12);
        }else env.popup(item.position,economy.power/2,0xffff4040);
        economy.extend_faith_timer(60,env.default_rate);break;
    }
    case 2:case 5:{
        const auto total=static_cast<i32>(static_cast<u32>(economy.item_value)*10u);i32 points=total,rank=8;u32 color=0xffffff00;
        if(item.kind==2){
            if(number(env.player_position->y)<number(144.0f))points=wrapping_add(points,-(points%10));
            else{
                const auto factor=(number(env.player_position->y)-number(144.0f))*number(.003289473708719015f);
                const auto reduction=(factor*(Extended::from_int(economy.item_value)*number(.5f)-number(5000.0f))).truncate_int();
                points=wrapping_add(total/2,static_cast<i32>(0u-static_cast<u32>(reduction)));points=wrapping_add(points,-(points%10));
                color=0xffffffff;rank=1;
            }
        }
        env.popup(item.position,points,color);economy.add_rank(rank);economy.add_score(points);economy.extend_faith_timer(100,env.default_rate);break;
    }
    case 3:{
        const i32 value=economy.difficulty==2?8000:economy.difficulty==3||economy.difficulty==4?10000:5000;
        env.popup(item.position,value,0xff00ff00);economy.add_item_value(value);economy.extend_faith_timer(120,env.default_rate);break;
    }
    case 4:case 11:{
        const bool power_up=economy.add_power(20,env);env.update_power_display(economy.power/20,(economy.power%20)*100/20);
        if(power_up){
            env.refresh_player_power();env.play_sound(0x1d,item.position.x);env.popup(item.position,-1,0xffffff40);economy.add_rank(24);
            if(economy.power>=100)full_power=true;
        }else env.popup(item.position,economy.power/2,0xffff4040);
        economy.extend_faith_timer(20,env.default_rate);break;
    }
    case 7:economy.add_lives(1,env);economy.add_rank(256);break;
    case 8:economy.add_item_value(10);economy.extend_faith_timer(3,env.default_rate);economy.add_score(10);break;
    case 9:env.popup(item.position,100,0xff00ff00);economy.add_item_value(100);economy.extend_faith_timer(60,env.default_rate);break;
    }
    item.state=0;env.play_sound(0x14,item.position.x);return full_power;
}
}
// 0x41afd0: one item within the manager's ordered regular/faith pool traversal.
#if defined(TH10_FAST_ITEM) && TH10_FAST_ITEM
ItemUpdate Item::update(ItemFrameEnvironment& env){
    if(state==0)return ItemUpdate::Skipped;
    if(state==5){
        value_variant=wrapping_add(value_variant,-1);
        if(value_variant<0){state=2;env.initialize_animation(*this,wrapping_add(kind,0x176));}
        return ItemUpdate::Skipped;
    }
    // th10_port (TH10_FAST_ITEM): the original statements below, each with its
    // plain-float form first (see the helpers above); a declined step runs the
    // original statement.
    if(state==1){
        if(((*env.player_state!=2&&*env.player_state!=4)&&less(env.player_position->y,128.0f))||*env.auto_collect){
            attraction_speed=*env.player_attraction_speed;state=3;attract(*this,env);
        }else{
            advance_position(*this,*env.default_rate);
            float vertical_value;
            if(float_rate(*env.default_rate)&&scaled_sum(*env.default_rate,.03f,velocity.y,vertical_value)){velocity.y=vertical_value;if(0.0f<vertical_value||0.0f==vertical_value)velocity.x=0;}
            else{const auto vertical=number(*env.default_rate)*number(.03f)+number(velocity.y);velocity.y=vertical.to_float();
                if(number(0.0f)<vertical||number(0.0f)==vertical)velocity.x=0;}
            if(less(2.0f,velocity.y))velocity.y=2;
            if(less(472.0f,position.y)){state=0;return ItemUpdate::Skipped;}
        }
    }else if(state==2){
        advance_position(*this,*env.default_rate);
        float vertical_value;bool rising;
        if(float_rate(*env.default_rate)&&scaled_sum(*env.default_rate,.03f,velocity.y,vertical_value)){velocity.y=vertical_value;rising=!(0.0f<vertical_value||0.0f==vertical_value);}
        else{const auto vertical=number(*env.default_rate)*number(.03f)+number(velocity.y);velocity.y=vertical.to_float();
            rising=!(number(0.0f)<vertical||number(0.0f)==vertical);}
        if(!rising){attraction_speed=*env.player_attraction_speed;state=3;attract(*this,env);}
        else if(less(472.0f,position.y)){state=0;env.economy->add_rank(-4);return ItemUpdate::Skipped;}
    }else if(state==3||state==4)attract(*this,env);
    if(*env.player_state!=2){
        if(region_contains(*env.pickup_region,position))return collect(*this,env)?ItemUpdate::FullPower:ItemUpdate::Skipped;
        if(state!=3&&state!=4&&region_contains(*((*env.input_keys&4)?env.slow_region:env.fast_region),position)){
            state=4;attraction_speed=Scalar::mul(*env.player_attraction_speed,.3333333432674408f);
        }
    }
    animation.update(*env.animations);tick_item_timer(timer);return ItemUpdate::Active;
}
#else
ItemUpdate Item::update(ItemFrameEnvironment& env){
    if(state==0)return ItemUpdate::Skipped;
    if(state==5){
        value_variant=wrapping_add(value_variant,-1);
        if(value_variant<0){state=2;env.initialize_animation(*this,wrapping_add(kind,0x176));}
        return ItemUpdate::Skipped;
    }
    if(state==1){
        if(((*env.player_state!=2&&*env.player_state!=4)&&number(env.player_position->y)<number(128.0f))||*env.auto_collect){
            attraction_speed=*env.player_attraction_speed;state=3;attract(*this,env);
        }else{
            advance_position(*this,*env.default_rate);
            const auto vertical=number(*env.default_rate)*number(.03f)+number(velocity.y);velocity.y=vertical.to_float();
            if(number(0.0f)<vertical||number(0.0f)==vertical)velocity.x=0;
            if(number(2.0f)<number(velocity.y))velocity.y=2;
            if(number(472.0f)<number(position.y)){state=0;return ItemUpdate::Skipped;}
        }
    }else if(state==2){
        advance_position(*this,*env.default_rate);
        const auto vertical=number(*env.default_rate)*number(.03f)+number(velocity.y);velocity.y=vertical.to_float();
        if(number(0.0f)<vertical||number(0.0f)==vertical){attraction_speed=*env.player_attraction_speed;state=3;attract(*this,env);}
        else if(number(472.0f)<number(position.y)){state=0;env.economy->add_rank(-4);return ItemUpdate::Skipped;}
    }else if(state==3||state==4)attract(*this,env);
    if(*env.player_state!=2){
        if(env.pickup_region->contains(position))return collect(*this,env)?ItemUpdate::FullPower:ItemUpdate::Skipped;
        if(state!=3&&state!=4&&((*env.input_keys&4)?env.slow_region:env.fast_region)->contains(position)){
            state=4;attraction_speed=Scalar::mul(*env.player_attraction_speed,.3333333432674408f);
        }
    }
    animation.update(*env.animations);timer.tick();return ItemUpdate::Active;
}
#endif
i32 ItemManager::update(ItemFrameEnvironment& env){
    faith_count=active_count=0;bool full_power=false;
    auto update_item=[&](Item& item){const auto result=item.update(env);if(result==ItemUpdate::FullPower)full_power=true;else if(result==ItemUpdate::Active)++active_count;};
#ifdef TH10_ANM_INDEX_CHECK
    for(u32 i=0;i<150;++i)if(regular[i].state&&!(live_regular[i>>5]&(1u<<(i&31)))){std::fprintf(stderr,"live_regular misses item %u\n",i);std::abort();}
    for(u32 i=0;i<2048;++i)if(faith[i].state&&!(live_faith[i>>5]&(1u<<(i&31)))){std::fprintf(stderr,"live_faith misses item %u\n",i);std::abort();}
#endif
    // th10_port: an empty item returns Skipped with no effect, so only items
    // marked in live_regular/live_faith (see Item.hpp) are visited, in pool
    // order; each word is read again after every item.
    auto visit=[&](Item* pool,u32* live,u32 words){
        for(u32 word=0;word<words;++word)for(u32 bits=live[word];bits;){
            const u32 bit=static_cast<u32>(__builtin_ctz(bits));auto& item=pool[word*32+bit];
            if(!item.state)live[word]&=~(1u<<bit);else update_item(item);
            bits=live[word]&~((2u<<bit)-1u);
        }
    };
    visit(regular,live_regular,5);
    visit(faith,live_faith,64);
    if(full_power)convert_power(env);return 1;
}
// 0x41ba50. Only kinds 1 and 4 pass the original outer condition.
i32 ItemManager::convert_power(ItemEnvironment& env){
    for(auto& item:regular)if(item.state&&(item.kind==1||item.kind==4)){
        item.state=0;spawn(item.position,9,0xffffffff,-1.5707963705062866f,2.2f,env);env.spawn_effect(item.position,0x189);
    }
    return 0;
}
}
