// Checks the TH10_HOT_MOVE float path (game/BulletFrame.cpp move) against the
// original Extended operations: random and edge-case rates, velocities and
// positions (zeros, subnormals, the 2^-126..2^-124 boundary, huge values,
// infinities, NaNs, typical playfield values), both half settings, in every
// precision/rounding mode the game uses. Bit-exact positions are required.
//   cmake --build <dir> --target th10_move_check && <dir>/th10_move_check [millions]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th10;
struct V3 {float x,y,z;};
struct Motion {V3 position,velocity;};
static float fbits(u32 b){float f;std::memcpy(&f,&b,4);return f;}
static u32 bits(float f){u32 b;std::memcpy(&b,&f,4);return b;}
// The original (BulletFrame.cpp, the #else branch), verbatim.
static void move_original(Motion& motion,float rate,bool half){
    auto x=number(rate)*number(motion.velocity.x),y=number(rate)*number(motion.velocity.y);
    auto z=number(Scalar::mul(rate,motion.velocity.z));
    if(half){x=number((x*number(.5f)).to_float());y=y*number(.5f);z=z*number(.5f);}
    motion.position.x=(x+number(motion.position.x)).to_float();motion.position.y=(y+number(motion.position.y)).to_float();motion.position.z=(z+number(motion.position.z)).to_float();
}
// The TH10_HOT_MOVE branch and its cold fallback, verbatim apart from counting
// the float-path returns.
static u64 fast_hits=0;
__attribute__((noinline,cold)) static void move_general(Motion& motion,float rate,bool half,float zmul){
    auto x=number(rate)*number(motion.velocity.x),y=number(rate)*number(motion.velocity.y);
    auto z=number(zmul);
    if(half){x=number((x*number(.5f)).to_float());y=y*number(.5f);z=z*number(.5f);}
    motion.position.x=(x+number(motion.position.x)).to_float();motion.position.y=(y+number(motion.position.y)).to_float();motion.position.z=(z+number(motion.position.z)).to_float();
}
static void move_hot(Motion& motion,float rate,bool half){
    const float zmul=Scalar::mul(rate,motion.velocity.z);
    if(single_precision_nearest()){
        using namespace arithmetic;
        const float vx=motion.velocity.x,vy=motion.velocity.y,px=motion.position.x,py=motion.position.y,pz=motion.position.z;
        const auto mul_ok=[](float a,float b,float r){const u32 bits=bits_of(r);return (bits&0x7fffffffu)?nonzero_accepted(bits):(a==0||b==0);};
        const auto add_ok=[](float a,float b,float r){const u32 bits=bits_of(r);return (bits&0x7fffffffu)?nonzero_accepted(bits):a==-b;};
        if(representable(bits_of(rate))&&representable(bits_of(vx))&&representable(bits_of(vy))&&representable(bits_of(zmul))&&representable(bits_of(px))&&representable(bits_of(py))&&representable(bits_of(pz))){
            float x=rate*vx,y=rate*vy,z=zmul;bool ok=mul_ok(rate,vx,x)&&mul_ok(rate,vy,y);
            if(half){const float hx=x*.5f,hy=y*.5f,hz=z*.5f;ok=ok&&mul_ok(x,.5f,hx)&&mul_ok(y,.5f,hy)&&mul_ok(z,.5f,hz);x=hx;y=hy;z=hz;}
            const float nx=x+px,ny=y+py,nz=z+pz;
            if(ok&&add_ok(x,px,nx)&&add_ok(y,py,ny)&&add_ok(z,pz,nz)){motion.position.x=nx;motion.position.y=ny;motion.position.z=nz;++fast_hits;return;}
        }
    }
    move_general(motion,rate,half,zmul);
}
static u64 state=0x9e3779b97f4a7c15ull;
static u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static const u32 special[]={0x00000000u,0x80000000u,0x00000001u,0x007fffffu,0x00800000u,0x00800001u,0x01000000u,0x01000001u,0x01800000u,
    0x3f800000u,0x3f000000u,0x40000000u,0x7f7fffffu,0x7f000000u,0x7e800000u,0x7f800000u,0x7fc00000u,0x3f7fffffu,0x34000000u,0x33800000u};
// A value of one of several kinds: any bits, a special value (either sign),
// near the f32 normal floor, a playfield-sized value, a small step value.
static float draw_value(){
    const u64 r=next();const u32 sign=u32(r>>63)<<31;
    switch(r%6){
    case 0:return fbits(u32(r>>20));
    case 1:return fbits(special[(r>>8)%(sizeof(special)/4)]|sign);
    case 2:return fbits(((u32(1+(r>>8)%4)<<23)|u32((r>>16)&0x7fffffu))|sign);   // biased exponent 1..4
    case 3:return fbits(bits(float((r>>8)%100000)/97.0f)|sign);                // 0..1030, playfield scale
    case 4:return fbits(bits(float((r>>8)%4096)/1024.0f)|sign);                // 0..4, velocities and rates
    default:return fbits(((u32(80+(r>>8)%100)<<23)|u32((r>>16)&0x7fffffu))|sign);  // 2^-47..2^52
    }
}
int main(int argc,char** argv){
    const u64 millions=argc>1?std::strtoull(argv[1],nullptr,0):20;
    const struct {Precision p;Rounding r;} modes[]={{Precision::Single,Rounding::NearestEven},{Precision::Single,Rounding::TowardZero},{Precision::Double,Rounding::NearestEven},{Precision::Extended,Rounding::NearestEven}};
    u64 checked=0,mismatches=0;
    for(const auto& m:modes){arithmetic_mode(m.p,m.r);
        const u64 n=(m.p==Precision::Single&&m.r==Rounding::NearestEven?millions:millions/10+1)*1000000ull;
        for(u64 k=0;k<n;++k){
            const float rate=(next()&3)?(next()&1?1.0f:0.5f):draw_value();
            Motion a{{draw_value(),draw_value(),draw_value()},{draw_value(),draw_value(),draw_value()}},b=a;
            const bool half=next()&1;
            move_original(a,rate,half);move_hot(b,rate,half);++checked;
            if(bits(a.position.x)!=bits(b.position.x)||bits(a.position.y)!=bits(b.position.y)||bits(a.position.z)!=bits(b.position.z)){
                if(mismatches<8)std::printf("MISMATCH mode=%d/%d rate=%08x half=%d\n",int(m.p),int(m.r),bits(rate),half);++mismatches;}
        }}
    std::printf("move_check: %llu cases (%llu on the float path), %llu mismatches\n",(unsigned long long)checked,(unsigned long long)fast_hits,(unsigned long long)mismatches);
    return mismatches?1:0;
}
