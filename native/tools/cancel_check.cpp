// Checks TH10_FAST_CANCEL (game/BulletCancellation.cpp cancel_bullet_circle's
// reach/distance test, FastFloat.hpp circle_outside) and TH10_FAST_PLAYFIELD
// (game/PlayerShooting.cpp outside_playfield and game/LaserFrame.cpp
// laser_outside_playfield, FastFloat.hpp outside_playfield) against the
// original Extended statements, in all 12 precision/rounding modes: random
// and edge operands (zeros of both signs, subnormals, the normal floor and
// biased exponents 1/2/3, 252..254, the largest finite, infinities, NaNs),
// game coordinates, exact cancellations (w+x == 0, x-w == 0, half+radius ==
// 0), operands that land exactly on the -192/192/0/448 edges, and circles
// whose reach*reach equals dx*dx+dy*dy exactly (scaled Pythagorean triples).
// Each "patched" function below is the macro-ON code and each "original" the
// macro-OFF code, verbatim apart from the function names; the helpers are the
// real FastFloat.hpp ones. Every result (and every branch) must match.
//   cmake --build <dir> --target th10_cancel_check && <dir>/th10_cancel_check [millions]
#include "../../th10_web/cpp/game/FastFloat.hpp"
#include "../../th10_web/cpp/game/Types.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !TH10_FAST_CANCEL || !TH10_FAST_PLAYFIELD
#error build with -DTH10_FAST_CANCEL=1 -DTH10_FAST_PLAYFIELD=1 (CMake target th10_cancel_check)
#endif
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
static unsigned long long fast_hits[3],fast_tries[3];   // playfield laser circle
// float-path coverage: outcomes, and values exactly on an edge (left == -192, right == 192, top == 0, bottom == 448; reach*reach == distance)
static unsigned long long fast_true[3],fast_edge[3];
static void note_playfield(int k,float x,float y,float w,float h,bool outside){if(outside)++fast_true[k];if(w+x==-192.f||x-w==192.f||h+y==0.f||y-h==448.f)++fast_edge[k];}
// ---- PlayerShooting.cpp outside_playfield: original (macro off) ----
static bool playfield_original(const Vec3& position,float half_width,float half_height) noexcept {
    const auto left=number(half_width)+number(position.x),right=number(position.x)-number(half_width);
    const auto top=number(half_height)+number(position.y),bottom=number(position.y)-number(half_height);
    return left<number(-192.f)||left==number(-192.f)||number(192.f)<right||right==number(192.f)||
           top<number(0.f)||top==number(0.f)||number(448.f)<bottom||bottom==number(448.f);
}
// ---- the same, TH10_FAST_PLAYFIELD ----
static bool playfield_patched(const Vec3& position,float half_width,float half_height) noexcept {
    ++fast_tries[0];
    {bool outside;if(fast_float::outside_playfield(position.x,position.y,half_width,half_height,outside)){++fast_hits[0];note_playfield(0,position.x,position.y,half_width,half_height,outside);return outside;}}
    const auto left=number(half_width)+number(position.x),right=number(position.x)-number(half_width);
    const auto top=number(half_height)+number(position.y),bottom=number(position.y)-number(half_height);
    return left<number(-192.f)||left==number(-192.f)||number(192.f)<right||right==number(192.f)||
           top<number(0.f)||top==number(0.f)||number(448.f)<bottom||bottom==number(448.f);
}
// ---- LaserFrame.cpp laser_outside_playfield: original and TH10_FAST_PLAYFIELD ----
static bool laser_original(const Vec3& point,float x,float y) noexcept {
    const auto left=number(x)+number(point.x),right=number(point.x)-number(x),top=number(y)+number(point.y),bottom=number(point.y)-number(y);
    return left<number(-192.f)||left==number(-192.f)||number(192.f)<right||right==number(192.f)||
           top<number(0.f)||top==number(0.f)||number(448.f)<bottom||bottom==number(448.f);
}
static bool laser_patched(const Vec3& point,float x,float y) noexcept {
    ++fast_tries[1];
    {bool outside;if(fast_float::outside_playfield(point.x,point.y,x,y,outside)){++fast_hits[1];note_playfield(1,point.x,point.y,x,y,outside);return outside;}}
    const auto left=number(x)+number(point.x),right=number(point.x)-number(x),top=number(y)+number(point.y),bottom=number(point.y)-number(y);
    return left<number(-192.f)||left==number(-192.f)||number(192.f)<right||right==number(192.f)||
           top<number(0.f)||top==number(0.f)||number(448.f)<bottom||bottom==number(448.f);
}
// ---- BulletCancellation.cpp cancel_bullet_circle, one bullet: true = the loop's "continue" ----
struct Bullet { float cancel_size; Vec3 position; };
static bool circle_original(const Bullet& bullet,const Vec3& position,float radius){
    const float dx=Scalar::sub(bullet.position.x,position.x),dy=Scalar::sub(bullet.position.y,position.y);
    const auto reach=number(bullet.cancel_size)*number(.5f)+number(radius),distance=number(dy)*number(dy)+number(dx)*number(dx);
    if(reach*reach<distance)return true;
    return false;
}
static bool circle_patched(const Bullet& bullet,const Vec3& position,float radius){
    const float dx=Scalar::sub(bullet.position.x,position.x),dy=Scalar::sub(bullet.position.y,position.y);
    ++fast_tries[2];
    if(const int outside=fast_float::circle_outside(bullet.cancel_size,radius,dx,dy);outside>=0){++fast_hits[2];
        {const float reach=bullet.cancel_size*.5f+radius;if(outside)++fast_true[2];if(reach*reach==dy*dy+dx*dx)++fast_edge[2];}
        if(outside)return true;}
    else{
        const auto reach=number(bullet.cancel_size)*number(.5f)+number(radius),distance=number(dy)*number(dy)+number(dx)*number(dx);
        if(reach*reach<distance)return true;
    }
    return false;
}
// ---- operands ----
static u64 state=0x9e3779b97f4a7c15ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x00000002u,0x007fffffu,0x00800000u,0x00800001u,0x00ffffffu,0x01000000u,0x01000001u,0x01800000u,
    0x3f800000u,0x3f000000u,0x40000000u,0x40800000u,0x41000000u /*8*/,0x43400000u /*192*/,0x43e00000u /*448*/,0x42800000u /*64*/,0x43800000u /*256*/,
    0x1f800000u /*2^-64*/,0x1f000000u,0x5f800000u /*2^64*/,0x5f000000u,0x4b000000u,0x7e800000u,0x7f000000u,0x7f7fffffu,0x7f7ffffeu,
    0x7f800000u,0x7fc00000u,0x7fa00000u,0x7fffffffu,0x33800000u,0x34000000u,0x0c000000u,0x73800000u,0x20000000u,0x1fffffffu,0x5f7fffffu};
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%10){
    case 0:return fbits(u32(r>>20));                                                  // any bits
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%2049)-1024)/4.0f));                      // game coordinates, quarter pixels
    case 3:return fbits(bits(float(i32((r>>8)%2000001)-1000000)/65536.0f));            // fine positions
    case 4:return fbits(((u32(1+(r>>8)%3)<<23)|u32((r>>16)&0x7fffffu))|sign);          // biased exponent 1..3
    case 5:return fbits(((u32(252+(r>>8)%3)<<23)|u32((r>>16)&0x7fffffu))|sign);        // biased exponent 252..254
    case 6:return fbits(((u32(60+(r>>8)%8)<<23)|u32((r>>16)&0x7fffffu))|sign);         // squares near the normal floor (2^-67..2^-60)
    case 7:return fbits(((u32(188+(r>>8)%8)<<23)|u32((r>>16)&0x7fffffu))|sign);        // squares near the top (2^61..2^68)
    case 8:return fbits(((u32(40+(r>>8)%31)<<23)|u32((r>>16)&0x7fffffu))|sign);        // squares below the f32 range (2^-87..2^-57: subnormal or 0)
    default:return fbits(bits(float((r>>8)%4097)/64.0f)|sign);
    }
}
// a size like the game's (cancel sizes, sprite extents) or any value
static float size_value(){const u64 r=next();return (r&3)==0?draw_value():fbits(bits(float(1+(r>>2)%96)*((r>>9)&1?1.0f:0.5f)));}
static bool same(float a,float b){return bits(a)==bits(b);}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):8;
    unsigned long long checked=0,mismatches=0,edge_cases=0,equal_cases=0;
    auto bad=[&](const char* what,int p,int r,const float* v,int n,bool a,bool b){if(mismatches<16){std::printf("MISMATCH %s mode=%d/%d original=%d patched=%d",what,p,r,a,b);for(int i=0;i<n;++i)std::printf(" %08x",bits(v[i]));std::printf("\n");}++mismatches;};
    const Precision ps[]={Precision::Single,Precision::Double,Precision::Extended};
    const Rounding rs[]={Rounding::NearestEven,Rounding::TowardZero,Rounding::Down,Rounding::Up};
    const float edges[]={-192.f,192.f,0.f,448.f};
    for(const auto p:ps)for(const auto r:rs){
        arithmetic_mode(p,r);const bool main_mode=p==Precision::Single&&r==Rounding::NearestEven;
        const u64 n=(main_mode?millions:millions/4+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            // playfield tests: random, then aimed at the four edges and at exact cancellations
            {   Vec3 pt{draw_value(),draw_value(),draw_value()};float w=size_value(),h=size_value();const u64 q=next();
                switch(q&7){
                case 0:{const float e=edges[(q>>3)&3];if((q>>5)&1)pt.x=e-w;else pt.x=e+w;break;}          // left/right on +-192 (when exact)
                case 1:{const float e=edges[2+((q>>3)&1)];if((q>>5)&1)pt.y=e-h;else pt.y=e+h;break;}       // top/bottom on 0/448 (when exact)
                case 2:pt.x=-w;pt.y=h;break;                                                              // w+x == 0, y-h == 0
                case 3:pt.x=w;pt.y=-h;break;                                                              // x-w == 0, h+y == 0
                case 4:pt.x=fbits(bits(float(i32((q>>8)%801)-400)/2.0f));pt.y=fbits(bits(float(i32((q>>20)%1001)-200)/2.0f));break;
                default:break;
                }
                if((q&7)<4)++edge_cases;
                ++checked;const bool a=playfield_original(pt,w,h),b=playfield_patched(pt,w,h);
                if(a!=b){const float v[]={pt.x,pt.y,w,h};bad("playfield",int(p),int(r),v,4,a,b);}
                ++checked;const bool c=laser_original(pt,w,h),d=laser_patched(pt,w,h);
                if(c!=d){const float v[]={pt.x,pt.y,w,h};bad("laser",int(p),int(r),v,4,c,d);}
            }
            // circle tests: random, game-like, exact equality (scaled Pythagorean triples), zero reach, overflow/underflow
            {   Bullet bullet{size_value(),{draw_value(),draw_value(),draw_value()}};Vec3 at{draw_value(),draw_value(),draw_value()};float radius=draw_value();const u64 q=next();
                switch(q&7){
                case 0:{   // reach*reach == dx*dx+dy*dy exactly: reach = c*s, |dx|,|dy| = a*s, b*s
                    static const int triples[][3]={{3,4,5},{5,12,13},{8,15,17},{7,24,25},{20,21,29},{0,1,1},{1,0,1},{0,0,0}};
                    const auto& t=triples[(q>>3)%8];const float s=fbits(u32(127+i32((q>>6)%41)-20)<<23);   // 2^-20..2^20
                    bullet.cancel_size=float(2*t[2])*s;radius=0;if((q>>12)&1){bullet.cancel_size=0;radius=float(t[2])*s;}
                    const i32 i=i32((q>>13)%97)-48,j=i32((q>>21)%97)-48;                                   // centre (i*s, j*s): every sum exact
                    at={float(i)*s,float(j)*s,0};
                    bullet.position={float(i+((q>>29)&1?-t[0]:t[0]))*s,float(j+((q>>30)&1?-t[1]:t[1]))*s,0};
                    if((q>>31)&1){const u32 nudge=bits(bullet.cancel_size?bullet.cancel_size:radius);   // one ulp either side of the edge
                        const float moved=fbits((q>>32)&1?nudge+1:nudge-1);if(bullet.cancel_size)bullet.cancel_size=moved;else radius=moved;}
                    ++equal_cases;break;}
                case 1:radius=-(bullet.cancel_size*.5f);break;                                          // reach == 0 exactly
                case 2:bullet.position=at;break;                                                         // dx == dy == 0
                case 4:{   // tiny offsets (squares that underflow f32) around a zero or tiny reach
                    const float tiny[]={draw_value(),fbits(((u32(40+next()%31)<<23)|u32(next()&0x7fffffu))|u32(next()&1)<<31)};
                    at={0,0,0};bullet.position={tiny[next()&1],tiny[next()&1],0};if(next()&1)bullet.position.x=0;
                    if(next()&1){bullet.cancel_size=0;radius=0;}else if(next()&1){bullet.cancel_size=0;radius=tiny[1];}break;}
                case 3:{   // game-like: bomb circles over the playfield
                    bullet.cancel_size=fbits(bits(float(2+(q>>3)%62)));radius=fbits(bits(float((q>>9)%257)));
                    at={fbits(bits(float(i32((q>>17)%385)-192))),fbits(bits(float((q>>26)%449))),0};
                    bullet.position={fbits(bits(float(i32(next()%1601)-800)/4.0f)),fbits(bits(float(i32(next()%1993)-100)/4.0f)),0};break;}
                default:break;
                }
                ++checked;const bool a=circle_original(bullet,at,radius),b=circle_patched(bullet,at,radius);
                if(a!=b){const float v[]={bullet.cancel_size,radius,bullet.position.x,bullet.position.y,at.x,at.y};bad("circle",int(p),int(r),v,6,a,b);}
            }
        }
        std::printf("mode %d/%d done: %llu cases so far, float paths %llu/%llu %llu/%llu %llu/%llu, %llu mismatches\n",int(p),int(r),checked,
            fast_hits[0],fast_tries[0],fast_hits[1],fast_tries[1],fast_hits[2],fast_tries[2],mismatches);std::fflush(stdout);
    }
    std::printf("cancel_check: %llu cases (%llu aimed at playfield edges/cancellations, %llu exact-equality circles), float paths taken: playfield %llu (outside %llu, on an edge %llu) laser %llu (outside %llu, on an edge %llu) circle %llu (outside %llu, reach^2 == distance %llu); %llu mismatches\n",
        checked,edge_cases,equal_cases,fast_hits[0],fast_true[0],fast_edge[0],fast_hits[1],fast_true[1],fast_edge[1],fast_hits[2],fast_true[2],fast_edge[2],mismatches);
    return mismatches?1:0;
}
