#pragma once
#include "AnmFile.hpp"
#include "AnmEnvironment.hpp"
#include "AnmRegistry.hpp"
// th10_port (TH08 1000 lane src/AnmManager.hpp:1385-1395 TH08_PSP_ANM_STAGING_COMPACT,
// sha of AnmManager.cpp 769c10e9): memory-limited builds keep only one batch
// in the sprite vertex buffer. The GE and headless renderers copy a batch's
// vertices when it is drawn, so every flush restarts the buffer and a full one
// flushes early. Measured on PC: the largest batch is 14,118 vertices (stage 3
// replay); 32768 keeps 2.3x of that and saves 2.75 MB.
#ifndef TH10_ANM_VERTEX_CAP
#define TH10_ANM_VERTEX_CAP 131072
#endif
static_assert(TH10_ANM_VERTEX_CAP>=6&&TH10_ANM_VERTEX_CAP<=131072,"sprite vertex cap");
// th10_port (PSP-1000 lane): fewer pooled VMs. allocate() probes two ring
// slots and takes a heap VM when both are busy (the original's own overflow
// path), so a smaller ring only moves VMs from the pool to the heap.
#ifndef TH10_ANM_POOL_CAP
#define TH10_ANM_POOL_CAP 4096
#endif
static_assert(TH10_ANM_POOL_CAP>=64&&TH10_ANM_POOL_CAP<=4096&&TH10_ANM_POOL_CAP%4==0,"VM pool cap");
namespace th10 {
struct AnmFrameEnvironment;
struct AnmSystemEnvironment;
struct AnmModelVertex {Vec3 position;Vec2 uv;};
enum class AnimationPlacement { WorldBack,WorldFront,UiBack,UiFront };
struct AnmAllocationEnvironment {
    virtual AnmVm* allocate_animation()=0;
    virtual void release_memory(void* memory)=0;
};
struct AnmManager {
    u8 header[0x4c];
    u32 started_scripts;
    u32 reserved_050,submitted_draws,flushed_batches;
    Vec2 draw_offset;
    u32 processed_count;
    AnmVm pool[TH10_ANM_POOL_CAP];
    u8 occupied[TH10_ANM_POOL_CAP];
    i32 cursor;
    AnmFile* files[33];
    Matrix4 render_world_matrix;
    AnmVm resource_animation;
    u8 resource_state[0x3ada60-0x3ad4dc];
    u32 current_material_color;
    void* current_texture;
    u8 cached_draw_state[8];
    AnmSprite* current_uv_sprite;
    void* model_vertex_buffer;
    AnmModelVertex model_vertices[4];
    u32 batch_quads;
    AnmVertex vertex_buffer[TH10_ANM_VERTEX_CAP];   // th10_port: 131072 in the original
    AnmVertex* vertex_write;
    AnmVertex* batch_start;
    AnmRegistry registry;
    AnmVm draw_layers[20];
    u32 last_id;
    u32 tint,tint_enabled;
    void initialize(AnmSystemEnvironment& environment);
    void release(AnmAllocationEnvironment& environment);
    void initialize_model(AnmSystemEnvironment& environment);
    AnmFile* open(i32 slot,const char* name,AnmResourceEnvironment& environment);
    AnmFile* load(i32 slot,const char* name,AnmResourceEnvironment& environment);
    void unload(i32 slot,AnmResourceEnvironment& environment);
    i32 process_loading(AnmResourceEnvironment& environment);
    bool resources_ready() const noexcept;
    i32 update_world(AnmFrameEnvironment& environment);
    i32 update_ui(AnmFrameEnvironment& environment);
    i32 draw_layer(u32 layer,AnmFrameEnvironment& environment);
    AnmVm* allocate(AnmAllocationEnvironment& environment);
    bool is_pooled(const AnmVm* vm) const noexcept;
    u32 insert(AnmVm& vm,AnimationPlacement placement) noexcept;
    i32 remove(AnmVm& vm,AnmAllocationEnvironment& environment);
    u32 create(AnmFile& file,i32 script,u32 tag,AnimationPlacement placement,AnmEnvironment& animations,AnmAllocationEnvironment& allocation);
    u32 create_at(AnmFile& file,i32 script,const Vec3& position,bool playfield_coordinates,AnimationPlacement placement,AnmEnvironment& animations,AnmAllocationEnvironment& allocation);
};
static_assert(offsetof(AnmManager,pool)==0x68);
// Fields after the pool move by the bytes a smaller VM pool removes.
constexpr u32 anm_pool_shift=(4096u-TH10_ANM_POOL_CAP)*u32(sizeof(AnmVm)+1u);
static_assert(offsetof(AnmManager,cursor)==0x3ad068-anm_pool_shift);
static_assert(offsetof(AnmManager,files)==0x3ad06c-anm_pool_shift);
// Fields after the vertex buffer move by the bytes a smaller cap removes.
constexpr u32 anm_vertex_shift=(131072u-TH10_ANM_VERTEX_CAP)*28u+anm_pool_shift;
static_assert(offsetof(AnmManager,registry)==0x72dad4-anm_vertex_shift);
static_assert(offsetof(AnmManager,last_id)==0x732454-anm_vertex_shift);
static_assert(offsetof(AnmManager,draw_layers)==0x72dae4-anm_vertex_shift);
static_assert(offsetof(AnmManager,current_texture)==0x3ada64-anm_pool_shift);
static_assert(offsetof(AnmManager,batch_quads)==0x3adac8-anm_pool_shift);
static_assert(offsetof(AnmManager,vertex_write)==0x72dacc-anm_vertex_shift);
static_assert(offsetof(AnmManager,tint)==0x732458-anm_vertex_shift);
static_assert(sizeof(AnmManager)==0x732460-anm_vertex_shift);
}
