#include "Item.hpp"
namespace th10 {
// 0x41b8e0. This includes the original upper-edge indicator alpha and the
// sprite comparison/binding index difference; neither is normalized here.
void Item::draw(ItemDrawEnvironment& env){
    if(!state)return;
    auto& vm=animation;vm.script_position.x=Scalar::add(position.x,224.0f);
    vm.script_position.y=Scalar::add(position.y,16.0f);vm.script_position.z=position.z;
#if defined(TH10_FAST_ITEM) && TH10_FAST_ITEM
    // th10_port: number(y)<number(8) is the float comparison for a representable y (tagged operands).
    const u32 y_bits=arithmetic::bits_of(vm.script_position.y);
    if(arithmetic::representable(y_bits)?vm.script_position.y<8.0f:number(vm.script_position.y)<number(8.0f)){
#else
    if(number(vm.script_position.y)<number(8.0f)){
#endif
        const auto delta=number(vm.script_position.y)-number(8.0f);vm.script_position.y=24.0f;
        const u8 alpha=number(32.0f)<delta||number(32.0f)==delta?255:static_cast<u8>((delta*number(.03125f)*number(255.0f)).truncate_int());
        vm.color=(vm.color&0x00ffffff)|(static_cast<u32>(alpha)<<24);
        if(vm.sprite_index!=wrapping_add(sprite_kind,0x161))env.bind_item_sprite(vm,wrapping_add(sprite_kind,0x160));
    }else if(vm.sprite_index!=wrapping_add(sprite_kind,0x158)){
        env.bind_item_sprite(vm,wrapping_add(sprite_kind,0x157));vm.color|=0xff000000;
    }
    env.draw_animation(vm);
}
// th10_port: empty items draw nothing; visit the live_* marks in pool order.
i32 ItemManager::draw(ItemDrawEnvironment& env){
    auto visit=[&](Item* pool,const u32* live,u32 words){
        for(u32 word=0;word<words;++word)for(u32 bits=live[word];bits;bits&=bits-1u)pool[word*32+static_cast<u32>(__builtin_ctz(bits))].draw(env);
    };
    visit(regular,live_regular,5);visit(faith,live_faith,64);return 1;
}
}
