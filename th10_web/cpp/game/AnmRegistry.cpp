#include "AnmRegistry.hpp"
#include "AnmFrame.hpp"
#ifdef TH10_ANM_INDEX_CHECK
#include <cstdio>
#include <cstdlib>
#endif
namespace th10 {
namespace {
template<class F> void with_children(AnmVm* vm,F apply){
    if(!vm)return;apply(*vm);
    if(!vm->child_node.previous)for(auto* node=vm->child_node.next;node;node=node->next)apply(*node->value);
}
}
namespace {
// 0x4491c0: world entries take priority when an id appears in both lists.
AnmVm* scan(const AnmRegistry& registry,u32 id) noexcept {
    for(auto* node=registry.world_head;node;node=node->next)if(node->value->id==id)return node->value;
    for(auto* node=registry.ui_head;node;node=node->next)if(node->value->id==id)return node->value;
    return nullptr;
}
// Linear probing on id & mask (ids are sequential), backward-shift deletion.
constexpr u32 index_size=1u<<14,index_mask=index_size-1;
struct IndexEntry {u32 id;AnmVm* vm;};
IndexEntry index_table[index_size];
const AnmRegistry* index_owner=nullptr;u32 index_count=0;bool index_broken=false;
bool index_usable(const AnmRegistry& registry) noexcept {return !index_broken&&index_owner==&registry;}
#ifdef TH10_ANM_INDEX_CHECK
void give_up(const char* why) noexcept {std::fprintf(stderr,"anm index disabled: %s (count %u)\n",why,index_count);std::abort();}
#else
void give_up(const char*) noexcept {index_broken=true;}
#endif
}
void anm_index_insert(const AnmRegistry& registry,u32 id,AnmVm* vm) noexcept {
    if(index_broken||!id)return;
    if(!index_owner)index_owner=&registry;else if(index_owner!=&registry){give_up("second registry");return;}
    if(index_count>=index_size/2){give_up("full");return;}
    u32 slot=id&index_mask;
    for(;index_table[slot].id;slot=(slot+1)&index_mask)if(index_table[slot].id==id){give_up("duplicate id");return;}
    index_table[slot]={id,vm};++index_count;
}
void anm_index_erase(const AnmRegistry& registry,u32 id,const AnmVm* vm) noexcept {
    if(!index_usable(registry)||!id)return;
    u32 hole=id&index_mask;
    for(;index_table[hole].id!=id;hole=(hole+1)&index_mask)if(!index_table[hole].id)return;
    if(index_table[hole].vm!=vm)return;   // not the listed VM with this id
    for(u32 next=(hole+1)&index_mask;index_table[next].id;next=(next+1)&index_mask){
        const u32 home=index_table[next].id&index_mask;
        if(((next-home)&index_mask)>=((next-hole)&index_mask)){index_table[hole]=index_table[next];hole=next;}
    }
    index_table[hole]={0,nullptr};--index_count;
}
AnmVm* AnmRegistry::find(u32 id) const noexcept {
    if(!id)return nullptr;
    if(!index_usable(*this))return scan(*this,id);
    AnmVm* found=nullptr;
    for(u32 slot=id&index_mask;index_table[slot].id;slot=(slot+1)&index_mask)if(index_table[slot].id==id){found=index_table[slot].vm;break;}
    if(found&&found->id!=id)found=nullptr;
#ifdef TH10_ANM_INDEX_CHECK
    if(found!=scan(*this,id)){std::fprintf(stderr,"anm index mismatch: id %u index %p scan %p\n",id,static_cast<void*>(found),static_cast<void*>(scan(*this,id)));std::abort();}
#endif
    return found;
}
AnmVm* AnmRegistry::find_and_clear(u32& id) const noexcept {auto* vm=find(id);if(!vm)id=0;return vm;}
// 0x4497d0 includes the parent's own node before searching its children.
u32 AnmRegistry::find_child(u32& id,i32 script) const noexcept {auto* vm=find_and_clear(id);if(!vm)return 0;for(auto* node=&vm->child_node;node;node=node->next)if(node->value->script_index==script)return node->value->id;return 0;}
// 0x449210/0x449470, 0x449590/0x4495e0, 0x4492a0.
void AnmRegistry::interrupt(u32 id,std::int16_t label) const noexcept {with_children(find(id),[&](AnmVm& vm){vm.pending_interrupt=label;});}
// 0x449250. Updating may change the parent's child list or a child's successor,
// so these links are read after their respective updates.
i32 AnmRegistry::interrupt_and_update(u32 id,std::int16_t label,AnmFrameEnvironment& env) const {
    auto* vm=find(id);if(!vm)return 0;vm->pending_interrupt=label;env.update(*vm);i32 result=reinterpret_cast<uintptr_t>(vm->child_node.previous);if(!result)for(auto* node=vm->child_node.next;node;node=node->next){node->value->pending_interrupt=label;result=env.update(*node->value);}return result;
}
void AnmRegistry::set_visibility(u32 id,bool visible) const noexcept {
    with_children(find(id),[&](AnmVm& vm){if(visible)vm.flags|=2;else vm.flags&=~2u;});
}
void AnmRegistry::request_delete(u32 id) const noexcept {with_children(find(id),[](AnmVm& vm){vm.flags|=0x4000000;});}
// 0x449630. The handle is cleared even when the animation has already expired.
void AnmRegistry::delete_and_clear(u32& id) const noexcept {request_delete(id);id=0;}
// 0x449350/0x4492f0. Only the root propagates to the flat child list.
void AnmRegistry::set_position(u32 id,const Vec3& position,bool playfield_coordinates) const noexcept {
    with_children(find(id),[&](AnmVm& vm){
        vm.position.x=playfield_coordinates?Scalar::add(position.x,224.0f):position.x;
        vm.position.y=playfield_coordinates?Scalar::add(position.y,16.0f):position.y;
        vm.position.z=position.z;
    });
}
}
