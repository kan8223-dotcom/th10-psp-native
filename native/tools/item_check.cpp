// Checks the TH10_FAST_ITEM float paths (game/ItemFrame.cpp, ItemDraw.cpp),
// TH10_FAST_BIND (game/AnmFile.cpp AnmFile::bind_sprite UV scales) and
// TH10_FAST_TICK (game/Timer.cpp Timer::tick's scaled branch; the linked
// Timer.cpp's tick() is compared too, so build this with and without it)
// against the original Extended expressions, in all 12 precision/rounding
// modes: random and edge operands (zeros, subnormals, the normal floor and
// biased exponents 1/2/254, the largest finite, infinities, NaNs), exact
// cancellations (x == -y sums, x == y differences), zero factors and
// divisions by zero/infinity. Each fast function below is the macro-ON code
// and each original the macro-OFF code, verbatim apart from the struct names;
// every output bit (and every branch taken) must match.
//   cmake --build <dir> --target th10_item_check && <dir>/th10_item_check [millions]
#include "../../th10_web/cpp/game/GameMath.hpp"
#include "../../th10_web/cpp/game/FastFloat.hpp"
#include "../../th10_web/cpp/game/Timer.hpp"
#include "../../portable/numeric/DfAtan2.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
// glibc 2.39's static i386 libm exports only __ieee754_fmod (GameMath.cpp remainder; ring_check.cpp does the same).
extern "C" double __ieee754_fmod(double,double);
extern "C" double fmod(double x,double y){return __ieee754_fmod(x,y);}
using namespace th10;
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
namespace ff=fast_float;
static unsigned long long fast_hits[8];   // region advance gravity angle edge uv item-timer scaled-tick
// ---- ItemFrame.cpp helpers (TH10_FAST_ITEM), verbatim ----
inline bool less(float a,float b) noexcept {return ff::operand(a)&&ff::operand(b)?a<b:number(a)<number(b);}
struct Region { Vec3 minimum,maximum;
    // ItemRegion::contains, verbatim
    bool contains(const Vec3& point) const noexcept {
        return !(number(point.x)<number(minimum.x)||number(point.y)<number(minimum.y)||
                 number(maximum.x)<number(point.x)||number(maximum.y)<number(point.y));
    }
};
inline bool region_contains(const Region& r,const Vec3& p) noexcept {
    if(ff::operand(p.x)&&ff::operand(p.y)&&ff::operand(r.minimum.x)&&ff::operand(r.minimum.y)&&ff::operand(r.maximum.x)&&ff::operand(r.maximum.y))
        {++fast_hits[0];return !(p.x<r.minimum.x||p.y<r.minimum.y||r.maximum.x<p.x||r.maximum.y<p.y);}
    return r.contains(p);
}
inline bool scaled_sum(float rate,float v,float p,float& out) noexcept {
    float product;return ff::operand(v)&&ff::operand(p)&&ff::mul(rate,v,product)&&ff::add(product,p,out);
}
inline bool float_rate(float rate) noexcept {return single_precision_nearest()&&ff::operand(rate);}
struct It { Vec3 position,velocity; float attraction_speed; };
// advance_position: original
static void advance_original(It& item,float rate){
    const auto x=number(rate)*number(item.velocity.x),y=number(rate)*number(item.velocity.y);
    const float z=Scalar::mul(rate,item.velocity.z);
    item.position.x=(x+number(item.position.x)).to_float();item.position.y=(y+number(item.position.y)).to_float();
    item.position.z=Scalar::add(z,item.position.z);
}
// advance_position: TH10_FAST_ITEM
static void advance_fast(It& item,float rate){
    {   float x,y;
        if(float_rate(rate)&&scaled_sum(rate,item.velocity.x,item.position.x,x)&&scaled_sum(rate,item.velocity.y,item.position.y,y)){
            const float z=Scalar::mul(rate,item.velocity.z);
            item.position.x=x;item.position.y=y;item.position.z=Scalar::add(z,item.position.z);++fast_hits[1];return;
        }
    }
    advance_original(item,rate);
}
// Item::update gravity statement (state 1 and 2): new velocity.y, and whether 0 <= vertical.
static bool gravity_original(It& item,float rate){
    const auto vertical=number(rate)*number(.03f)+number(item.velocity.y);item.velocity.y=vertical.to_float();
    return number(0.0f)<vertical||number(0.0f)==vertical;
}
static bool gravity_fast(It& item,float rate){
    float vertical_value;
    if(float_rate(rate)&&scaled_sum(rate,.03f,item.velocity.y,vertical_value)){item.velocity.y=vertical_value;++fast_hits[2];return 0.0f<vertical_value||0.0f==vertical_value;}
    return gravity_original(item,rate);
}
// attract's angle (and the attraction-speed comparison): original
static float angle_original(const Vec3& player,const It& item,bool& below12){
    const auto x=number(player.x)-number(item.position.x),y=number(player.y)-number(item.position.y);
    const float angle=x==number(0.0f)&&y==number(0.0f)?1.5707963705062866f:angle_to_float(y,x);
    below12=number(item.attraction_speed)<number(12.0f);return angle;
}
static float angle_fast(const Vec3& player,const It& item,bool& below12){
    {   const float px=player.x,py=player.y,ix=item.position.x,iy=item.position.y;float dx,dy;
        if(single_precision_nearest()&&ff::operand(px)&&ff::operand(ix)&&ff::operand(py)&&ff::operand(iy)&&ff::sub(px,ix,dx)&&ff::sub(py,iy,dy)){
            float angle=1.5707963705062866f;
            if(!(dx==0.0f&&dy==0.0f)&&!touhou::numeric::df::atan2_float(dy,dx,angle))angle=angle_to(number(dy),number(dx)).to_float();
            below12=less(item.attraction_speed,12.0f);++fast_hits[3];return angle;
        }
    }
    return angle_original(player,item,below12);
}
// ItemDraw.cpp: the upper-edge test
static bool edge_original(float y){return number(y)<number(8.0f);}
static bool edge_fast(float y){const u32 y_bits=arithmetic::bits_of(y);if(arithmetic::representable(y_bits))++fast_hits[4];return arithmetic::representable(y_bits)?y<8.0f:number(y)<number(8.0f);}
// AnmFile::bind_sprite UV scales: original (uv00, uv11) and TH10_FAST_BIND
static void uv_original(float sx,float tw,float w,float sy,float th,float h,float& u0,float& u1){
    u0=(number(sx)/number(tw)*number(w)).to_float();
    const auto scale_y=number(sy)/number(th);
    u1=(scale_y*number(h)).to_float();
}
static void uv_fast(float sx,float tw,float w,float sy,float th,float h,float& u0,float& u1){
    if(single_precision_nearest()){
        using namespace arithmetic;
        if(representable(bits_of(sx))&&representable(bits_of(tw))&&representable(bits_of(w))&&representable(bits_of(sy))&&representable(bits_of(th))&&representable(bits_of(h))){
            const float qx=sx/tw,qy=sy/th;const u32 qxb=bits_of(qx),qyb=bits_of(qy);
            if(((qxb&0x7fffffffu)?nonzero_accepted(qxb):sx==0)&&((qyb&0x7fffffffu)?nonzero_accepted(qyb):sy==0)){
                const float ux=qx*w,uy=qy*h;const u32 uxb=bits_of(ux),uyb=bits_of(uy);
                if(((uxb&0x7fffffffu)?nonzero_accepted(uxb):(qx==0||w==0))&&((uyb&0x7fffffffu)?nonzero_accepted(uyb):(qy==0||h==0))){
                    u0=ux;u1=uy;++fast_hits[5];return;
                }
            }
        }
    }
    uv_original(sx,tw,w,sy,th,h,u0,u1);
}
// Timer::tick (game/Timer.cpp): the original, and the TH10_FAST_TICK version
static bool unscaled(float rate) noexcept { return 0.99f < rate && rate < 1.01f; }
static i32 tick_original(Timer& t) noexcept {
    t.previous = t.current;
    if (unscaled(*t.rate)) {
        t.current = wrapping_add(t.current, 1);
        t.fractional = Scalar::add(t.fractional,1.0f);
    } else {
        const auto sum = number(*t.rate) + number(t.fractional);
        t.fractional = sum.to_float();
        t.current = sum.truncate_int();
    }
    return t.current;
}
static i32 tick_fasttick(Timer& t) noexcept {
    t.previous = t.current;
    if (unscaled(*t.rate)) {
        t.current = wrapping_add(t.current, 1);
        t.fractional = Scalar::add(t.fractional,1.0f);
    } else {
        {   using namespace arithmetic;
            const float scale = *t.rate, held = t.fractional;
            if (single_precision_nearest() && representable(bits_of(scale)) && representable(bits_of(held))) {
                const float sum = scale + held; const u32 sum_bits = bits_of(sum);
                if (((sum_bits & 0x7fffffffu) ? nonzero_accepted(sum_bits) : scale == -held) && sum > -2147483648.0f && sum < 2147483648.0f) {
                    t.fractional = sum;
                    t.current = static_cast<i32>(sum);
                    ++fast_hits[7];return t.current;
                }
            }
        }
        const auto sum = number(*t.rate) + number(t.fractional);
        t.fractional = sum.to_float();
        t.current = sum.truncate_int();
    }
    return t.current;
}
// the item timer tick (ItemFrame.cpp: Timer::tick's unscaled branch inline), over the TH10_FAST_TICK tick
static void tick_fast(Timer& timer){
    const float rate=*timer.rate;
    if(0.99f<rate&&rate<1.01f){timer.previous=timer.current;timer.current=wrapping_add(timer.current,1);timer.fractional=Scalar::add(timer.fractional,1.0f);++fast_hits[6];}
    else tick_fasttick(timer);
}
// ---- operands ----
static u64 state=0x6a09e667f3bcc909ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x00ffffffu,0x01000000u,0x01000001u,0x3f800000u,0x3f000000u,
    0x40000000u,0x3cf5c28fu /*.03f*/,0x41400000u /*12*/,0x43000000u /*128*/,0x43ec0000u /*472*/,0x41000000u /*8*/,0x40000000u /*2*/,0x3e4ccccdu /*.2f*/,
    0x4b000000u,0x7f000000u,0x7f7fffffu,0x7f7ffffeu,0x7f800000u,0x7fc00000u,0x7fa00000u,0x33800000u,0x34000000u,0x0c000000u,0x73800000u};
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%7){
    case 0:return fbits(u32(r>>20));                                                 // any bits
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(bits(float(i32((r>>8)%961)-480)/4.0f));                       // game coordinates, quarter pixels
    case 3:return fbits(bits(float(i32((r>>8)%2000001)-1000000)/65536.0f)|0);          // fine positions/velocities
    case 4:return fbits(((u32(1+(r>>8)%3)<<23)|u32((r>>16)&0x7fffffu))|sign);         // biased exponent 1..3
    case 5:return fbits(((u32(252+(r>>8)%3)<<23)|u32((r>>16)&0x7fffffu))|sign);       // biased exponent 252..254
    default:return fbits(bits(float((r>>8)%2001)/64.0f)|sign);
    }
}
static float rate_value(){const u64 r=next()%8;return r<4?1.0f:r==4?0.5f:r==5?2.0f:draw_value();}
static bool same(float a,float b){return bits(a)==bits(b);}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):4;
    unsigned long long checked=0,mismatches=0;
    auto bad=[&](const char* what,int p,int r,const float* v,int n){if(mismatches<12){std::printf("MISMATCH %s mode=%d/%d",what,p,r);for(int i=0;i<n;++i)std::printf(" %08x",bits(v[i]));std::printf("\n");}++mismatches;};
    const Precision ps[]={Precision::Single,Precision::Double,Precision::Extended};
    const Rounding rs[]={Rounding::NearestEven,Rounding::TowardZero,Rounding::Down,Rounding::Up};
    for(const auto p:ps)for(const auto r:rs){
        arithmetic_mode(p,r);const bool main_mode=p==Precision::Single&&r==Rounding::NearestEven;
        const u64 n=(main_mode?millions:millions/4+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            // advance_position (with exact cancellations: position == -rate*velocity)
            {   const float rate=rate_value();It a{{draw_value(),draw_value(),draw_value()},{draw_value(),draw_value(),draw_value()},0};
                if((next()&7)==0)a.position.x=-(a.velocity.x*rate);if((next()&7)==0)a.position.y=-(a.velocity.y*rate);
                It b=a;advance_original(a,rate);advance_fast(b,rate);++checked;
                if(!same(a.position.x,b.position.x)||!same(a.position.y,b.position.y)||!same(a.position.z,b.position.z)){const float v[]={rate,a.velocity.x,a.velocity.y,a.position.x,b.position.x};bad("advance",int(p),int(r),v,5);}}
            // gravity (with vertical == 0 exactly: velocity.y == -rate*.03f)
            {   const float rate=rate_value();It a{{0,0,0},{0,draw_value(),0},0};if((next()&7)==0)a.velocity.y=-(rate*.03f);
                It b=a;const bool ga=gravity_original(a,rate),gb=gravity_fast(b,rate);++checked;
                if(ga!=gb||!same(a.velocity.y,b.velocity.y)){const float v[]={rate,a.velocity.y,b.velocity.y};bad("gravity",int(p),int(r),v,3);}}
            // attract angle (with dx == 0 and/or dy == 0 exactly)
            {   It a{{draw_value(),draw_value(),0},{0,0,0},draw_value()};Vec3 player{draw_value(),draw_value(),0};
                const u64 z=next()&15;if(z==0)player.x=a.position.x;if(z==1)player.y=a.position.y;if(z==2){player.x=a.position.x;player.y=a.position.y;}
                if(z==3){a.position.x=fbits(bits(float(i32(next()%961)-480)/4.0f));a.position.y=fbits(bits(float(i32(next()%961)-480)/4.0f));player.x=fbits(bits(float(i32(next()%385)-192)));player.y=fbits(bits(float(next()%449)));}
                bool la,lb;const float aa=angle_original(player,a,la),ab=angle_fast(player,a,lb);++checked;
                if(la!=lb||!same(aa,ab)){const float v[]={player.x,player.y,a.position.x,a.position.y,aa,ab};bad("angle",int(p),int(r),v,6);}}
            // comparisons with the constants and two variables
            {   const float x=draw_value(),y=draw_value();const float cs[]={2.0f,472.0f,128.0f,12.0f,8.0f};const float c=cs[next()%5];++checked;
                if(less(c,x)!=(number(c)<number(x))||less(x,c)!=(number(x)<number(c))||less(x,y)!=(number(x)<number(y))){const float v[]={x,y,c};bad("less",int(p),int(r),v,3);}
                if(edge_original(x)!=edge_fast(x)){const float v[]={x};bad("edge",int(p),int(r),v,1);}}
            // regions (degenerate, NaN and infinite bounds included)
            {   Region reg{{draw_value(),draw_value(),0},{draw_value(),draw_value(),0}};if((next()&3)==0){reg.minimum={-24,-24,0};reg.maximum={24,24,0};}
                Vec3 pt{draw_value(),draw_value(),draw_value()};if((next()&3)==0){pt.x=reg.minimum.x;pt.y=reg.maximum.y;}++checked;
                if(reg.contains(pt)!=region_contains(reg,pt)){const float v[]={pt.x,pt.y,reg.minimum.x,reg.minimum.y,reg.maximum.x,reg.maximum.y};bad("region",int(p),int(r),v,6);}}
            // bind_sprite UV scales (zero numerators, zero/inf denominators, texture sizes)
            {   const u64 q=next();
                const float tw=(q&3)==0?fbits(bits(float(1u<<(4+(q>>2)%8)))):draw_value(),th=(q&12)==0?fbits(bits(float(1u<<(4+(q>>5)%8)))):draw_value();
                const float sx=(q&48)==0?1.0f:draw_value(),sy=(q&192)==0?1.0f:draw_value();
                const float w=(q&256)?fbits(bits(float((q>>9)%257))):draw_value(),h=(q&512)?fbits(bits(float((q>>18)%257))):draw_value();
                float a0,a1,b0,b1;uv_original(sx,tw,w,sy,th,h,a0,a1);uv_fast(sx,tw,w,sy,th,h,b0,b1);++checked;
                if(!same(a0,b0)||!same(a1,b1)){const float v[]={sx,tw,w,sy,th,h,a0,b0,a1,b1};bad("uv",int(p),int(r),v,10);}}
            // item timer tick
            {   const u64 q=next();const float rate=(q&3)==0?draw_value():(q&3)==1?fbits(bits(float((q>>2)%64)/64.0f)):rate_value();
                float held=draw_value();if((q&28)==0)held=-rate;if((q&28)==4)held=fbits(bits(float(i32((q>>8)%100001))*0.5f));
                Timer a{i32(next()&0xffff),i32(next()&0xffff),held,&rate},b=a,c=a,d=a;tick_original(a);tick_fast(b);tick_fasttick(c);d.tick();++checked;
                if(a.previous!=b.previous||a.current!=b.current||!same(a.fractional,b.fractional)||a.previous!=c.previous||a.current!=c.current||!same(a.fractional,c.fractional)
                   ||a.previous!=d.previous||a.current!=d.current||!same(a.fractional,d.fractional)){const float v[]={rate,held,a.fractional,b.fractional,c.fractional};bad("timer",int(p),int(r),v,5);}}
        }}
    std::printf("item_check: %llu cases, float paths taken: region %llu advance %llu gravity %llu angle %llu edge %llu uv %llu item-timer %llu scaled-tick %llu; %llu mismatches\n",
        checked,fast_hits[0],fast_hits[1],fast_hits[2],fast_hits[3],fast_hits[4],fast_hits[5],fast_hits[6],fast_hits[7],mismatches);
    return mismatches?1:0;
}
