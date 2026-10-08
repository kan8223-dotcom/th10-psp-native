#include "Camera.hpp"
#include "GameMath.hpp"
#include <cstring>
namespace th10 {
namespace {
Extended unsigned_number(u32 bits){auto value=Extended::from_int(static_cast<i32>(bits));if(bits&0x80000000)value=value+number(4294967296.0f);return value;}
Vec3 add(const Vec3& a,const Vec3& b){return {Scalar::add(a.x,b.x),Scalar::add(a.y,b.y),Scalar::add(a.z,b.z)};}
// th10_port: configure_flat/configure_world compute pure functions of a few
// camera fields and the arithmetic mode (look_at, perspective, normalize, a
// constant tangent). Draw passes repeat them with unchanged inputs up to 12
// times a frame, so an unchanged key reuses the stored outputs; every side
// effect (flush, set_transform, copy_offset) still happens.
struct FlatKey {u32 width,height,mode;};
struct WorldKey {Vec3 position,target_offset,up,eye_offset;float field_of_view;u32 width,height,mode;};
// A few entries: the UI (640x480) and world (384x448) viewports alternate.
template<class Key,class Result> struct CameraCache {
    struct Entry {Key key;Result result;bool valid;};Entry entries[4]{};u32 next=0;
    bool find(const Key& k,Result& out) const {for(const auto& e:entries)if(e.valid&&!std::memcmp(&k,&e.key,sizeof(Key))){out=e.result;return true;}return false;}
    void store(const Key& k,const Result& r){entries[next]={k,r,true};next=(next+1)&3;}
};
struct FlatResult {Matrix4 view,projection;};
struct WorldResult {Matrix4 view,projection;Vec3 right;};
CameraCache<FlatKey,FlatResult> flat_cache;
CameraCache<WorldKey,WorldResult> world_cache;
}
void CameraEnvironment::flush(){if(auto* manager=*animation_manager){AnmRenderer renderer{*manager,*render_environment};renderer.flush();}}
void CameraEnvironment::copy_offset(const Camera& camera){if(auto* manager=*animation_manager)manager->draw_offset=camera.draw_offset;}
// 0x421480. Screen-space sprites still use perspective projection; its eye
// distance cancels the FOV at z=0. Aspect uses stored floats, the half extents
// retain the unsigned conversion's extended precision until vector storage.
void Camera::configure_flat(CameraEnvironment& environment){
    environment.flush();
    const FlatKey key{viewport.width,viewport.height,arithmetic_mode_key()};FlatResult cached;
    if(flat_cache.find(key,cached)){view=cached.view;projection=cached.projection;
        environment.render_environment->set_transform(Matrices::View,view);environment.render_environment->set_transform(Matrices::Projection,projection);
        environment.copy_offset(*this);return;}
    const auto width=unsigned_number(viewport.width),height=unsigned_number(viewport.height);
    const auto half_width=width*number(.5f),half_height=height*number(.5f);
    const auto aspect=(number(width.to_float())/number(height.to_float())).to_float();
    const Vec3 target{half_width.to_float(),half_height.to_float(),0};
    const Vec3 eye{half_width.to_float(),half_height.to_float(),(half_height/tangent(Extended::from_double(0.15707963705062866))).to_float()};
    const Vec3 down{0,-1,0};
    environment.look_at(view,eye,target,down);
    environment.perspective(projection,0x1.41b2f8p-2f,aspect,1,10000);
    flat_cache.store(key,{view,projection});
    environment.render_environment->set_transform(Matrices::View,view);environment.render_environment->set_transform(Matrices::Projection,projection);
    environment.copy_offset(*this);
}
// 0x4215a0. Read camera fields after each platform call where the original does.
void Camera::configure_world(CameraEnvironment& environment){
    environment.flush();
    const WorldKey key{position,target_offset,up,eye_offset,field_of_view,viewport.width,viewport.height,arithmetic_mode_key()};WorldResult cached;
    if(world_cache.find(key,cached)){view=cached.view;projection=cached.projection;right=cached.right;
        environment.render_environment->set_transform(Matrices::View,view);environment.render_environment->set_transform(Matrices::Projection,projection);
        environment.copy_offset(*this);return;}
    const auto target=add(target_offset,position),eye=add(eye_offset,position);
    environment.look_at(view,eye,target,up);
    environment.perspective(projection,field_of_view,(unsigned_number(viewport.width)/unsigned_number(viewport.height)).to_float(),30,1800);
    environment.render_environment->set_transform(Matrices::View,view);environment.render_environment->set_transform(Matrices::Projection,projection);
    right={(number(up.z)*number(target_offset.y)-number(up.y)*number(target_offset.z)).to_float(),(number(up.x)*number(target_offset.z)-number(up.z)*number(target_offset.x)).to_float(),(number(up.y)*number(target_offset.x)-number(up.x)*number(target_offset.y)).to_float()};
    environment.normalize(right);world_cache.store(key,{view,projection,right});environment.copy_offset(*this);
}
// 0x4216f0 preserves matrices, interpolation-related fields and fog state.
void Camera::initialize(Camera& world,Camera& ui) noexcept {
    ui.position={0,0,1000};ui.target_offset={0,0,0};ui.up={0,1,0};ui.eye_offset={0,0,0};
    world.position={0,0,1000};world.target_offset={0,0,0};world.up={0,1,0};
    ui.field_of_view=0x1.0c1524p-1f;ui.viewport={0,0,640,480,0,1};ui.screen_space=1;
    world.field_of_view=0x1.0c1524p-1f;world.viewport={32,16,384,448,0,1};world.screen_space=0;world.eye_offset={0,0,0};
}
}
