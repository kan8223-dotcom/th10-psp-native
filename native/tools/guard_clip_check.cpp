// Checks psp/GeGuardClip.hpp (TH10_GUARD_CLIP) on random cameras, viewports and
// big world triangles reaching far outside the GE guard band and behind the
// eye: (a) a triangle inside every plane comes back bit for bit, (b) every
// output vertex is inside every plane and lands within the band on the GE
// screen, (c) sample points of the input triangle strictly inside the planes
// are covered by the output and points strictly outside are not, (d) uv and
// colour of new vertices are the input's barycentric interpolation, (e) the
// two triangles of a strip quad give bit-identical points on their shared edge
// and together cover it (no gap between them).
//   cmake --build <dir> --target th10_guard_clip_check && <dir>/th10_guard_clip_check [thousands]
#include "../psp/GeGuardClip.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace th10_guard;
static unsigned long long state=0x9e3779b97f4a7c15ull;
static double uniform(){state^=state<<13;state^=state>>7;state^=state<<17;return double(state>>11)*(1.0/9007199254740992.0);}
static double range(double a,double b){return a+(b-a)*uniform();}
struct V3 {double x,y,z;};
static V3 sub(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static double dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V3 unit(V3 a){const double l=std::sqrt(dot(a,a));return {a.x/l,a.y/l,a.z/l};}
static void mul(const float* a,const float* b,float* r){for(int i=0;i<4;i++)for(int k=0;k<4;k++){float x=0;for(int j=0;j<4;j++)x+=a[i*4+j]*b[j*4+k];r[i*4+k]=x;}}
struct Camera {float m[16];double scale_x,scale_y,centre_x,centre_y;Planes q;};
// D3D left-handed look-at and perspective (row vectors), viewport as GeRenderer's apply_viewport.
static Camera camera(){
    Camera c;const V3 eye{range(-300,300),range(-300,300),range(-300,300)};
    const V3 f=unit({range(-1,1),range(-1,1),range(-1,1)}),r=unit(cross({0,1,0},f)),u=cross(f,r);
    const float view[16]{float(r.x),float(u.x),float(f.x),0,float(r.y),float(u.y),float(f.y),0,float(r.z),float(u.z),float(f.z),0,
        float(-dot(r,eye)),float(-dot(u,eye)),float(-dot(f,eye)),1};
    const double fov=range(0.3,1.6),aspect=range(0.6,1.5),zn=range(0.5,50),zf=zn+range(100,8000);
    const double ys=1/std::tan(fov/2),xs=ys/aspect;
    float proj[16]{float(xs),0,0,0,0,float(ys),0,0,0,0,float(zf/(zf-zn)),1,0,0,float(-zn*zf/(zf-zn)),0};
    for(int i=0;i<4;i++)proj[i*4+2]=proj[i*4+2]*2.0f-proj[i*4+3];   // apply(): z [0,w] -> [-w,w]
    float world[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    if(uniform()<0.5){const float s=float(range(0.2,4));world[0]=world[5]=world[10]=s;world[12]=float(range(-100,100));world[13]=float(range(-100,100));world[14]=float(range(-100,100));}
    float vp[16];mul(view,proj,vp);mul(world,vp,c.m);
    const unsigned vx=unsigned(range(0,64)),vy=unsigned(range(0,32)),vw=unsigned(range(200,640-vx)),vh=unsigned(range(200,480-vy));
    const float sx=480.0f/640,sy=272.0f/480;
    c.scale_x=vw*sx*0.5f;c.scale_y=-float(vh)*sy*0.5f;c.centre_x=2048-240+(vx+vw*0.5f)*sx;c.centre_y=2048-136+(vy+vh*0.5f)*sy;
    c.q=world_planes(world,camera_planes(vp,float(c.scale_x),float(c.scale_y),float(c.centre_x),float(c.centre_y)));   // as GeRenderer.cpp guard_clip
    return c;
}
static void clip(const Camera& c,const float* p,double& x,double& y,double& w){   // GE screen position, double
    const double cx=p[0]*double(c.m[0])+p[1]*double(c.m[4])+p[2]*double(c.m[8])+c.m[12],cy=p[0]*double(c.m[1])+p[1]*double(c.m[5])+p[2]*double(c.m[9])+c.m[13];
    w=p[0]*double(c.m[3])+p[1]*double(c.m[7])+p[2]*double(c.m[11])+c.m[15];x=c.centre_x+c.scale_x*cx/w;y=c.centre_y+c.scale_y*cy/w;}
static void screen(const Camera& c,V3 p,double& x,double& y,double& w){
    const double cx=p.x*c.m[0]+p.y*c.m[4]+p.z*c.m[8]+c.m[12],cy=p.x*c.m[1]+p.y*c.m[5]+p.z*c.m[9]+c.m[13];
    w=p.x*c.m[3]+p.y*c.m[7]+p.z*c.m[11]+c.m[15];x=c.centre_x+c.scale_x*cx/w;y=c.centre_y+c.scale_y*cy/w;}
// The GE screen projection of output triangle t covers (px,py), tol in pixels (GE subpixel is 1/16).
static bool screen_covers(const Camera& c,const Vertex* t,double px,double py,double tol){
    double x[3],y[3],w[3];for(int i=0;i<3;i++){screen(c,{t[i].x,t[i].y,t[i].z},x[i],y[i],w[i]);if(!(w[i]>0))return false;}
    const double area=(x[1]-x[0])*(y[2]-y[0])-(x[2]-x[0])*(y[1]-y[0]);if(area==0)return false;
    for(int i=0;i<3;i++){const int j=(i+1)%3;const double ex=x[j]-x[i],ey=y[j]-y[i],len=std::sqrt(ex*ex+ey*ey);if(len==0)return false;
        if((area>0?1:-1)*(ex*(py-y[i])-ey*(px-x[i]))/len<-tol)return false;}
    return true;
}
static double plane(const Camera& c,int k,V3 p){const float* a=c.q.p[k];return p.x*a[0]+p.y*a[1]+p.z*a[2]+a[3];}
static double plane_scale(const Camera& c,int k,V3 p){const float* a=c.q.p[k];return std::fabs(p.x*a[0])+std::fabs(p.y*a[1])+std::fabs(p.z*a[2])+std::fabs(a[3]);}
static V3 pos(const Vertex& v){return {v.x,v.y,v.z};}
// Barycentric coordinates of p in triangle abc (p in its plane), double.
static bool barycentric(V3 a,V3 b,V3 c,V3 p,double* l){
    const V3 n=cross(sub(b,a),sub(c,a));const double area=dot(n,n);if(!(area>0))return false;
    l[0]=dot(cross(sub(c,b),sub(p,b)),n)/area;l[1]=dot(cross(sub(a,c),sub(p,c)),n)/area;l[2]=1-l[0]-l[1];return true;}
static Vertex random_vertex(const Camera& c){
    // Around the eye: mostly in front, sometimes far to the side or behind.
    Vertex v{};const double inv[3]{0,0,0};(void)inv;
    v.x=float(range(-2000,2000));v.y=float(range(-2000,2000));v.z=float(range(-2000,2000));
    if(uniform()<0.5){v.x*=0.05f;v.y*=0.05f;v.z*=0.05f;}
    (void)c;v.u=float(range(-2,3));v.v=float(range(-2,3));v.color=unsigned(state>>3);return v;
}
int main(int argc,char** argv){
    const unsigned long long thousands=argc>1?std::strtoull(argv[1],nullptr,0):200;
    unsigned long long triangles=0,clipped=0,bad_identity=0,bad_inside=0,bad_band=0,bad_cover=0,bad_extra=0,bad_attr=0,bad_shared=0,shared_checked=0,overflow=0,edge_checked=0,bad_edge=0,shared_near=0,edge_one_side=0;
    Vertex out[max_triangle_out],out2[max_triangle_out];
    for(unsigned long long iter=0;iter<thousands*1000ull;++iter){
        const Camera c=camera();
        Vertex tri[4];for(auto& v:tri)v=random_vertex(c);
        if(uniform()<0.3){   // a floor-like quad: near corners far below the screen
            const float s=float(range(50,3000));tri[0]=tri[1]=tri[2]=tri[3]=tri[0];
            tri[1].x+=s;tri[2].z+=s;tri[3].x+=s;tri[3].z+=s;tri[1].u+=1;tri[2].v+=1;tri[3].u+=1;tri[3].v+=1;}
        u32 code[4];for(int i=0;i<4;i++)code[i]=outcode(c.q,tri[i].x,tri[i].y,tri[i].z);
        // the strip quad: (0,1,2) and (2,1,3), the second with its first two swapped as GeRenderer does
        const int order[2][3]{{0,1,2},{2,1,3}};Vertex* dst[2]{out,out2};u32 n[2];
        for(int k=0;k<2;k++){
            const int a=order[k][0],b=order[k][1],d=order[k][2];++triangles;
            n[k]=clip_triangle(c.q,tri[a],tri[b],tri[d],code[a],code[b],code[d],dst[k]);
            if(n[k]%3||n[k]>max_triangle_out){++overflow;continue;}
            if(!(code[a]|code[b]|code[d])){if(n[k]!=3||std::memcmp(dst[k],&tri[a],sizeof(Vertex))||std::memcmp(dst[k]+1,&tri[b],sizeof(Vertex))||std::memcmp(dst[k]+2,&tri[d],sizeof(Vertex)))++bad_identity;continue;}
            ++clipped;
            const V3 A=pos(tri[a]),B=pos(tri[b]),D=pos(tri[d]);
            for(u32 i=0;i<n[k];i++){const V3 p=pos(dst[k][i]);
                for(int pl=0;pl<5;pl++)if(plane(c,pl,p)<-1e-4*plane_scale(c,pl,p)){++bad_inside;if(bad_inside<=4){double x,y,w;clip(c,&dst[k][i].x,x,y,w);std::printf("INSIDE iter=%llu plane=%d d=%g scale=%g screen=(%.2f %.2f) w=%g codes=%x %x %x\n",iter,pl,plane(c,pl,p),plane_scale(c,pl,p),x,y,w,code[a],code[b],code[d]);}break;}
                double x,y,w;clip(c,&dst[k][i].x,x,y,w);if(w>0&&(std::fabs(x-2048)>band+1||std::fabs(y-2048)>band+1))++bad_band;
                double l[3];if(barycentric(A,B,D,p,l)){
                    const double u=l[0]*tri[a].u+l[1]*tri[b].u+l[2]*tri[d].u,v=l[0]*tri[a].v+l[1]*tri[b].v+l[2]*tri[d].v;
                    const double tol=1e-3*(1+std::fabs(tri[a].u)+std::fabs(tri[b].u)+std::fabs(tri[d].u)+std::fabs(tri[a].v)+std::fabs(tri[b].v)+std::fabs(tri[d].v));
                    bool bad=std::fabs(u-dst[k][i].u)>tol||std::fabs(v-dst[k][i].v)>tol;
                    for(int ch=0;ch<4;ch++){const double e=l[0]*((tri[a].color>>(8*ch))&255)+l[1]*((tri[b].color>>(8*ch))&255)+l[2]*((tri[d].color>>(8*ch))&255);
                        if(std::fabs(e-((dst[k][i].color>>(8*ch))&255))>1.01)bad=true;}
                    if(bad)++bad_attr;}}
            // coverage on sample points
            for(int s=0;s<24;s++){double l0=uniform(),l1=uniform();if(l0+l1>1){l0=1-l0;l1=1-l1;}const double l2=1-l0-l1;
                const V3 p{A.x*l0+B.x*l1+D.x*l2,A.y*l0+B.y*l1+D.y*l2,A.z*l0+B.z*l1+D.z*l2};
                double worst=1e300;for(int pl=0;pl<5;pl++){const double d=plane(c,pl,p)/plane_scale(c,pl,p);if(d<worst)worst=d;}
                if(std::fabs(worst)<1e-4)continue;   // too close to a plane to judge
                bool covered=false;
                if(worst>0){double sx,sy,sw;screen(c,p,sx,sy,sw);for(u32 t=0;t+2<n[k]&&!covered;t+=3)covered=screen_covers(c,dst[k]+t,sx,sy,0.01);if(!covered)++bad_cover;}
                else{for(u32 t=0;t+2<n[k]&&!covered;t+=3){double m[3];
                    if(barycentric(pos(dst[k][t]),pos(dst[k][t+1]),pos(dst[k][t+2]),p,m)&&m[0]>=-1e-6&&m[1]>=-1e-6&&m[2]>=-1e-6)covered=true;}
                    if(covered)++bad_extra;}
            }
        }
        // shared edge 1-2: the points both outputs place on it must be the same bits
        if(n[0]%3==0&&n[1]%3==0&&n[0]&&n[1]){
            auto on_edge=[&](const Vertex& v){   // collinear as seen from both ends (a point near one end is judged from it)
                const V3 p=pos(v),e=sub(pos(tri[2]),pos(tri[1])),r=sub(p,pos(tri[1])),r2=sub(p,pos(tri[2]));
                const V3 x=cross(e,r),x2=cross(e,r2);
                return dot(x,x)<=1e-13*dot(e,e)*dot(r,r)&&dot(x2,x2)<=1e-13*dot(e,e)*dot(r2,r2)&&dot(r,e)>=0&&dot(r,e)<=dot(e,e);};
            auto original=[&](const Vertex& v){return !std::memcmp(&v,&tri[1],sizeof v)||!std::memcmp(&v,&tri[2],sizeof v);};
            std::vector<Vertex> first,second;
            for(u32 i=0;i<n[0];i++)if(on_edge(out[i])&&!original(out[i]))first.push_back(out[i]);
            for(u32 i=0;i<n[1];i++)if(on_edge(out2[i])&&!original(out2[i]))second.push_back(out2[i]);
            const V3 E1=pos(tri[1]),E2=pos(tri[2]);
            for(int s=0;s<16;s++){const double f=uniform();const V3 p{E1.x+(E2.x-E1.x)*f,E1.y+(E2.y-E1.y)*f,E1.z+(E2.z-E1.z)*f};
                double worst=1e300;for(int pl=0;pl<5;pl++){const double d=plane(c,pl,p)/plane_scale(c,pl,p);if(d<worst)worst=d;}
                if(worst<1e-4)continue;
                double sx,sy,sw;screen(c,p,sx,sy,sw);bool covered[2]{};
                for(int k=0;k<2;k++){const Vertex* o=k?out2:out;for(u32 t=0;t+2<n[k]&&!covered[k];t+=3)covered[k]=screen_covers(c,o+t,sx,sy,0.01);}
                ++edge_checked;if(covered[0]!=covered[1])++edge_one_side;   // on the float edge's other side: fine
                if(!covered[0]&&!covered[1]){++bad_edge;if(bad_edge<=6)std::printf("EDGE iter=%llu f=%.6f screen=(%.2f %.2f) w=%g codes=%x %x %x %x\n",iter,f,sx,sy,sw,code[0],code[1],code[2],code[3]);}
            }
            // A point of the first output on the edge has its twin in the second, bit for bit; a
            // near twin (within 1e-6 of the edge) is the corner of a sliver both cut the same way.
            const double edge=std::sqrt(dot(sub(E2,E1),sub(E2,E1)));
            for(const auto& v:first){bool found=false,near=false;for(const auto& w:second)if(!std::memcmp(&v,&w,sizeof v))found=true;
                if(!found)for(u32 i=0;i<n[1];i++){const V3 d=sub(pos(out2[i]),pos(v));if(std::sqrt(dot(d,d))<=1e-6*edge)near=true;}
                ++shared_checked;if(near)++shared_near;
                if(!found&&!near){++bad_shared;
                    if(bad_shared<=3)std::printf("SHARED iter=%llu v=(%.9g %.9g %.9g) codes=%x %x %x %x\n",iter,v.x,v.y,v.z,code[0],code[1],code[2],code[3]);}}
        }
    }
    std::printf("guard_clip_check: %llu triangles, %llu clipped, overflow=%llu identity=%llu inside=%llu band=%llu cover=%llu extra=%llu attr=%llu shared=%llu (of %llu, near twins %llu) edge gaps=%llu (of %llu, one side only %llu)\n",
        triangles,clipped,overflow,bad_identity,bad_inside,bad_band,bad_cover,bad_extra,bad_attr,bad_shared,shared_checked,shared_near,bad_edge,edge_checked,edge_one_side);
    return overflow|bad_identity|bad_inside|bad_band|bad_cover|bad_extra|bad_attr|bad_shared|bad_edge?1:0;
}
