#include "Graphics.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace th10::browser {
namespace {u32 address(const void* value){return static_cast<u32>(reinterpret_cast<uintptr_t>(value));}}
GraphicsRenderer::GraphicsRenderer(GraphicsDevice& d,AnmManager& manager,Camera& active,Camera& world,AnmVertex* vertices):device(d),camera(active){global_manager=&manager;quad=vertices;viewport=reinterpret_cast<const RenderViewport*>(&active.viewport);camera_unit=&active.right;camera_position=&world.position;fog=&world.fog;}
#ifndef TH_NATIVE_PLATFORM
void GraphicsRenderer::texture_stage(u32 stage,u32 setting,u32 value){device.host.texture_stage(device.handle,stage,setting,value);}
void GraphicsRenderer::sampler_state(u32 stage,u32 setting,u32 value){device.host.sampler(device.handle,stage,setting,value);}
void GraphicsRenderer::render_state(u32 setting,u32 value){device.host.render_state(device.handle,setting,value);}
#endif
void GraphicsRenderer::set_texture(void* texture){device.host.bind_texture(device.handle,address(texture));}
void GraphicsRenderer::vertex_format(LayoutParameter format){device.host.vertex_format(device.handle,format);}
void GraphicsRenderer::draw_triangles(TopologyParameter primitive,u32 count,const void* vertices,u32 stride){device.host.draw(device.handle,primitive,count,vertices,stride);}
void GraphicsRenderer::set_transform(MatrixParameter kind,const Matrix4& matrix){device.host.transform(device.handle,kind,matrix);}
void GraphicsRenderer::stream_source(void* buffer,u32 stride){device.vertex_buffer(buffer,stride);}
void GraphicsRenderer::draw_buffer(TopologyParameter primitive,u32 first,u32 count){device.draw_vertices(primitive,first,count);}
void GraphicsRenderer::rotation(Matrix4& matrix,u32 axis,float radians){GraphicsMath::rotation(matrix,axis,radians);}
void GraphicsRenderer::multiply(Matrix4& output,const Matrix4& first,const Matrix4& second){GraphicsMath::multiply(output,first,second);}
#if TH10_RENDER_FAST_PROJECT
// th10_port (platform opt-in): only AnmProjection draws through here (quad
// corners and billboard points); gameplay projections call GraphicsMath
// directly (WorldEnemies). The point goes through world, then through
// view*projection*viewport formed once per camera state, in plain float,
// instead of the exact combined() and extended viewport mapping per call.
// Differences are float rounding (sub-pixel; measured by the PC check).
namespace {
struct ProjectCache {Matrix4 view,projection;CameraViewport viewport;float m[4][4];bool valid=false;} project_cache;
void product(float out[4][4],const float a[4][4],const float b[4][4]){
    float r[4][4];for(u32 i=0;i<4;++i)for(u32 j=0;j<4;++j)r[i][j]=a[i][0]*b[0][j]+a[i][1]*b[1][j]+a[i][2]*b[2][j]+a[i][3]*b[3][j];
    std::memcpy(out,r,sizeof(r));
}
const ProjectCache& camera_product(const Camera& camera){
    auto& c=project_cache;
    if(c.valid&&!std::memcmp(&c.view,&camera.view,sizeof(Matrix4))&&!std::memcmp(&c.projection,&camera.projection,sizeof(Matrix4))&&!std::memcmp(&c.viewport,&camera.viewport,sizeof(CameraViewport)))return c;
    c.view=camera.view;c.projection=camera.projection;c.viewport=camera.viewport;
    const auto& v=camera.viewport;const float half_width=float(v.width)*.5f,height=float(v.height);
    const float mapping[4][4]={{half_width,0,0,0},{0,-.5f*height,0,0},{0,0,v.far_depth-v.near_depth,0},{float(v.x)+half_width,float(v.y)+height*.5f,v.near_depth,1}};
    product(c.m,camera.view.elements,camera.projection.elements);product(c.m,c.m,mapping);c.valid=true;return c;
}
void fast_project(Vec3& output,const Vec3& input,const Camera& camera,const Matrix4& world){
    const auto& m=camera_product(camera).m;const auto& w=world.elements;float v[4],p[4];
    for(u32 c=0;c<4;++c)v[c]=input.x*w[0][c]+input.y*w[1][c]+input.z*w[2][c]+w[3][c];
    for(u32 c=0;c<4;++c)p[c]=v[0]*m[0][c]+v[1]*m[1][c]+v[2]*m[2][c]+v[3]*m[3][c];
    const float scale=1.0f/p[3];output={p[0]*scale,p[1]*scale,p[2]*scale};
}
}
#endif
void GraphicsRenderer::project(Vec3& output,const Vec3& input,const Matrix4& world){
#if TH10_RENDER_FAST_PROJECT && !defined(TH10_FAST_PROJECT_CHECK)
    fast_project(output,input,camera,world);
#else
    GraphicsMath::project(output,input,&camera.viewport,&camera.projection,&camera.view,&world);
#if defined(TH10_FAST_PROJECT_CHECK)
    {   // PC: measure the fast path against the exact result (screen pixels / depth).
        static float worst_xy=0,worst_z=0;Vec3 fast;fast_project(fast,input,camera,world);
        const float dxy=std::fmax(std::fabs(fast.x-output.x),std::fabs(fast.y-output.y)),dz=std::fabs(fast.z-output.z);
        if(output.z>=0&&output.z<=1&&std::fabs(output.x)<4096&&std::fabs(output.y)<4096&&(dxy>worst_xy||dz>worst_z)){
            worst_xy=std::fmax(worst_xy,dxy);worst_z=std::fmax(worst_z,dz);
            std::fprintf(stderr,"fast_project worst: xy %.6g px, z %.3g (at %g,%g,%g)\n",worst_xy,worst_z,output.x,output.y,output.z);}
    }
#endif
#endif
}
void GraphicsRenderer::transform(float* output,const Vec3& input,const Matrix4& world){GraphicsMath::transform(output,input,world);}
i32 GraphicsRenderer::special_draw(AnmManager& manager,AnmVm& vm,u32 mode){
    AnmRenderer renderer{manager,*this};AnmProjection projection{renderer,*this};
    if(mode==4||mode==6)return projection.draw_billboard(vm,mode==6);
    if(mode==5||mode==7)return projection.draw_projected(vm,mode==7);
    if(mode==8)return projection.draw_model(vm);
    return projection.draw_strip(vm,vm.geometry,static_cast<u32>(vm.integer_variables[0])*2);
}
void GraphicsCamera::set_viewport(const CameraViewport& viewport){renderer.device.host.viewport(renderer.device.handle,viewport);}
}
