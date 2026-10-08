#include "AnmManager.hpp"
namespace th10 {
// 0x449950. Probe two slots, then allocate on the heap; this is not a pool scan.
#if TH10_ANM_POOL_STATS
// How full the VM ring gets and how often allocate() takes the heap path
// (decides TH10_ANM_POOL_CAP for the PSP-1000 lane).
namespace {u32 pool_live=0,pool_live_max=0,pool_allocs=0,heap_allocs=0,heap_live=0,heap_live_max=0;}
}
extern "C" void th10_anm_pool_stats(unsigned* out){out[0]=TH10_ANM_POOL_CAP;out[1]=th10::pool_live_max;out[2]=th10::pool_allocs;out[3]=th10::heap_allocs;out[4]=th10::heap_live_max;}
namespace th10 {
#define TH10_POOL_NOTE(pooled_) do{if(pooled_){++pool_allocs;if(++pool_live>pool_live_max)pool_live_max=pool_live;}else{++heap_allocs;if(++heap_live>heap_live_max)heap_live_max=heap_live;}}while(0)
#define TH10_POOL_FREE(pooled_) do{if(pooled_)--pool_live;else --heap_live;}while(0)
#else
#define TH10_POOL_NOTE(pooled_) do{}while(0)
#define TH10_POOL_FREE(pooled_) do{}while(0)
#endif
AnmVm* AnmManager::allocate(AnmAllocationEnvironment& env){
    AnmVm* result;
    if(occupied[cursor])cursor=wrapping_add(cursor,1)%TH10_ANM_POOL_CAP;
    if(occupied[cursor]){result=env.allocate_animation();if(result)result->clear();result->initialize();TH10_POOL_NOTE(false);}
    else{result=&pool[cursor];occupied[cursor]=1;TH10_POOL_NOTE(true);}
    cursor=wrapping_add(cursor,1)%TH10_ANM_POOL_CAP;return result;
}
// 0x449a00.
bool AnmManager::is_pooled(const AnmVm* vm) const noexcept {
    const auto at=reinterpret_cast<uintptr_t>(vm);return at>=reinterpret_cast<uintptr_t>(pool)&&at<reinterpret_cast<uintptr_t>(pool+TH10_ANM_POOL_CAP);
}
// 0x4489d0 / 0x448a50 / 0x448ac0 / 0x448b40.
u32 AnmManager::insert(AnmVm& vm,AnimationPlacement placement) noexcept {
    const bool ui=placement==AnimationPlacement::UiBack||placement==AnimationPlacement::UiFront;
    const bool front=placement==AnimationPlacement::WorldFront||placement==AnimationPlacement::UiFront;
    auto*& head=ui?registry.ui_head:registry.world_head;auto*& tail=ui?registry.ui_tail:registry.world_tail;
    auto& node=vm.registry_node;node.initialize(&vm);
    if(front){if(!head)tail=&node;else{node.next=head;head->previous=&node;}head=&node;}
    else{if(!head)head=&node;else node.insert_after(*tail);tail=&node;}
    ++last_id;if(!last_id)++last_id;vm.id=last_id;anm_index_insert(registry,last_id,&vm);return last_id;
}
// 0x448bb0. Removing a parent detaches its child-list link without recursively
// freeing the children. Pooled VMs are reset; heap VMs are released.
i32 AnmManager::remove(AnmVm& vm,AnmAllocationEnvironment& env){
    auto& node=vm.registry_node;anm_index_erase(registry,vm.id,&vm);
    if(registry.world_tail==&node)registry.world_tail=node.previous;if(registry.world_head==&node)registry.world_head=node.next;
    if(registry.ui_tail==&node)registry.ui_tail=node.previous;if(registry.ui_head==&node)registry.ui_head=node.next;
    node.unlink();vm.child_node.unlink();
    const bool pooled=is_pooled(&vm);
    if(pooled)occupied[(&vm-pool)]=0;
    TH10_POOL_FREE(pooled);
    if(vm.geometry)env.release_memory(vm.geometry);vm.geometry=nullptr;
    if(pooled)vm.initialize();else env.release_memory(&vm);
    return 0;
}
u32 AnmManager::create(AnmFile& file,i32 script,u32 tag,AnimationPlacement placement,AnmEnvironment& animations,AnmAllocationEnvironment& allocation){
    auto& vm=*allocate(allocation);vm.owner_tag=tag;vm.flags|=0x40000000;
    file.prepare_script(vm,script,animations,started_scripts);return insert(vm,placement);
}
u32 AnmManager::create_at(AnmFile& file,i32 script,const Vec3& position,bool playfield,AnimationPlacement placement,AnmEnvironment& animations,AnmAllocationEnvironment& allocation){
    auto& vm=*allocate(allocation);vm.owner_tag=0;vm.flags|=0x40000000;
    vm.position={playfield?Scalar::add(position.x,224.0f):position.x,
                 playfield?Scalar::add(position.y,16.0f):position.y,position.z};
    file.initialize_script(vm,script,animations,started_scripts);return insert(vm,placement);
}
}
