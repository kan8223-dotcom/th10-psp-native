// th10_port: CPU clipping of world (GU_TRANSFORM_3D) triangles against the GE
// guard band (TH10_GUARD_CLIP, GeRenderer.cpp). The GE drops a whole triangle
// when one vertex lands outside its 4096x4096 screen space, so a big near
// quad (stage 5 floor) vanishes while it is on screen. TH08 met the same loss
// and split such quads n x n (r260 TH08_PSP_R260_BG_QUAD_SUBDIVIDE in
// d3d8_psp_native.cpp); this clips instead, so any triangle of any size fits.
// The clipper follows SCARLET HARBOR src/world_clip.hpp (outside -> inside
// intersections, so a shared edge gives the same point in both triangles;
// fan output), with its look-at frustum replaced by planes built from the
// draw's world*view*projection' and the GE viewport: the near plane and four
// planes `band` px from the GE screen centre. New vertices interpolate world
// position, uv and colour with one t (clip space is affine in world space,
// and uv along an edge is what perspective-correct mapping gives); they still
// go through the GE's 3D transform. No SDK dependency:
// tools/guard_clip_check.cpp tests it on the PC.
#pragma once
#include <cstdint>

#ifndef TH10_GUARD_NEAR
#define TH10_GUARD_NEAR 0
#endif
namespace th10_guard {
using u32=std::uint32_t;
// TH10_GUARD_NEAR: draws with a vertex behind the near plane are clipped here
// too. PPSSPP near-clips them, but the device dropped the stage 6 near floor
// (device check, 2026-10-01); the plane sits 2^-12 inside (D3D z >= 2^-12) so the new
// vertices stay in front of it after rounding and the GE has nothing to clip.
constexpr float near_w=TH10_GUARD_NEAR?1.0f-0x1p-11f:1.0f;
struct Vertex {float u,v;u32 color;float x,y,z;};   // GeVertex: TEXTURE_32BITF, COLOR_8888, VERTEX_32BITF
constexpr float band=1900.0f;                       // the GE keeps 2048 px around (2048,2048); 148 px margin
constexpr u32 max_points=16,max_triangle_out=3*(max_points-2);
// World-space planes, d = x p[0] + y p[1] + z p[2] + p[3], inside d >= 0:
// 0 near (D3D z >= 0, or 2^-12 with TH10_GUARD_NEAR), 1 left, 2 right, 3 top, 4 bottom. w: clip w.
struct Planes {float p[5][4],w[4];};
// Built in two steps, so that a draw pays only for its world matrix: per
// camera, the planes in the input space of view*projection' (row vectors,
// clip_j = sum_l v_l vp[l*4+j]) with the GE viewport (GE x = centre_x +
// scale_x cx/cw); per draw, world folded in (row r: world[r*4+l] b[l]).
struct CameraPlanes {float b[6][4];};   // 0..4 the planes, 5 clip w
inline CameraPlanes camera_planes(const float* vp,float scale_x,float scale_y,float centre_x,float centre_y){
    const float a[6][4]{{0,0,1,near_w},{scale_x,0,0,centre_x-2048.0f+band},{-scale_x,0,0,2048.0f+band-centre_x},
        {0,scale_y,0,centre_y-2048.0f+band},{0,-scale_y,0,2048.0f+band-centre_y},{0,0,0,1}};
    CameraPlanes c;
    for(int k=0;k<6;k++)for(int l=0;l<4;l++)c.b[k][l]=a[k][0]*vp[l*4]+a[k][1]*vp[l*4+1]+a[k][2]*vp[l*4+2]+a[k][3]*vp[l*4+3];
    return c;
}
inline Planes world_planes(const float* world,const CameraPlanes& c){
    Planes q;
    for(int k=0;k<6;k++){float* d=k<5?q.p[k]:q.w;const float* b=c.b[k];
        for(int r=0;r<4;r++)d[r]=world[r*4]*b[0]+world[r*4+1]*b[1]+world[r*4+2]*b[2]+world[r*4+3]*b[3];}
    return q;
}
inline float distance(const float* p,float x,float y,float z){return x*p[0]+y*p[1]+z*p[2]+p[3];}
// bit k set: outside plane k.
inline u32 outcode(const Planes& q,float x,float y,float z){
    u32 code=0;for(u32 k=0;k<5;k++)if(distance(q.p[k],x,y,z)<0.0f)code|=1u<<k;
    return code;
}
// A vertex the GE would cull the triangle for: in front of the eye, outside
// the band (and, with TH10_GUARD_NEAR, any vertex behind the near plane).
inline bool trips(const Planes& q,u32 code,float x,float y,float z){
#if TH10_GUARD_NEAR
    if(code&1u)return true;
#endif
    return (code&30u)&&distance(q.w,x,y,z)>0.0f;}

namespace detail {
struct Point {float x,y,z,u,v,c[4];};
inline Point load(const Vertex& v){Point a{v.x,v.y,v.z,v.u,v.v,{}};for(u32 i=0;i<4;i++)a.c[i]=float((v.color>>(8*i))&255u);return a;}
inline Vertex store(const Point& a){
    Vertex v{a.u,a.v,0,a.x,a.y,a.z};
    for(u32 i=0;i<4;i++){const float f=a.c[i]<0.0f?0.0f:a.c[i]>255.0f?255.0f:a.c[i];v.color|=u32(f+0.5f)<<(8*i);}
    return v;
}
// Always called with the outside point first, whatever the edge's direction,
// so a shared edge gives the same point. Interpolates from the end nearer the
// crossing with that end's own parameter (t <= 1/2): no 1-t cancellation when
// the other end is far outside (a vertex far behind the eye).
inline Point cut(const Point& out,const Point& in,float od,float id){
    const bool near_out=-od<=id;const Point& a=near_out?out:in;const Point& b=near_out?in:out;
    float t=near_out?od/(od-id):id/(id-od);if(!(t>0.0f))t=0.0f;if(t>0.5f)t=0.5f;
    Point r{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,{}};
    for(u32 i=0;i<4;i++)r.c[i]=a.c[i]+(b.c[i]-a.c[i])*t;
    return r;
}
inline bool same(const Point& a,const Point& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
}

// One triangle with its vertices' outcodes into dst (room for max_triangle_out
// vertices). Returns 0 or a multiple of 3. Inside every plane: the three
// vertices bit for bit. Wholly outside one plane: 0.
inline u32 clip_triangle(const Planes& q,const Vertex& a,const Vertex& b,const Vertex& c,u32 ca,u32 cb,u32 cc,Vertex* dst){
    if(ca&cb&cc)return 0;
    const u32 any=ca|cb|cc;
    if(!any){dst[0]=a;dst[1]=b;dst[2]=c;return 3;}
    detail::Point buffer[2][max_points];detail::Point* src=buffer[0];detail::Point* out=buffer[1];u32 count=3;
    src[0]=detail::load(a);src[1]=detail::load(b);src[2]=detail::load(c);
    for(u32 k=0;k<5;k++){
        if(!(any&(1u<<k)))continue;
        const float* p=q.p[k];u32 next=0,prev=count-1;float pd=distance(p,src[prev].x,src[prev].y,src[prev].z);
        for(u32 i=0;i<count;i++){
            const float cd=distance(p,src[i].x,src[i].y,src[i].z);
            if((pd<0.0f)!=(cd<0.0f)){
                const detail::Point hit=pd<0.0f?detail::cut(src[prev],src[i],pd,cd):detail::cut(src[i],src[prev],cd,pd);
                if(!(next&&detail::same(out[next-1],hit))){if(next==max_points)return 0;out[next++]=hit;}
            }
            if(cd>=0.0f&&!(next&&detail::same(out[next-1],src[i]))){if(next==max_points)return 0;out[next++]=src[i];}
            prev=i;pd=cd;
        }
        if(next>1&&detail::same(out[0],out[next-1]))--next;
        if(next<3)return 0;
        count=next;detail::Point* swap=src;src=out;out=swap;
    }
    const Vertex first=detail::store(src[0]);u32 written=0;
    for(u32 i=1;i+1<count;i++){dst[written++]=first;dst[written++]=detail::store(src[i]);dst[written++]=detail::store(src[i+1]);}
    return written;
}
}
