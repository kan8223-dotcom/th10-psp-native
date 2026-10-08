// Checks th10::Extended (game/Arithmetic.cpp, float-tag fast paths) against
// SoftFloat in every precision/rounding mode: + - * / < == negation,
// magnitude, to_float, to_double, truncate_int, round_to_integer,
// square_root, from_float/from_double/from_int/from_int64. Operands are
// tagged floats, untagged SoftFloat conversions of the same floats, random
// extended values and earlier results (chains). Every result must equal the
// SoftFloat value, and a tagged result's float bits must be that value.
//   cmake --build build_sample --target th10_extended_check && build_sample/th10_extended_check [iterations]
#include "../../th10_web/cpp/game/Arithmetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
using th10::Extended;using th10::u16;using th10::u32;using th10::u64;using th10::i32;using th10::i64;
struct F32 {u32 v;};struct F64 {u64 v;};
extern "C" {
void f32_to_extF80M(F32,Extended*);void f64_to_extF80M(F64,Extended*);void i64_to_extF80M(i64,Extended*);
F32 extF80M_to_f32(const Extended*);F64 extF80M_to_f64(const Extended*);i64 extF80M_to_i64(const Extended*,unsigned char,bool);
void extF80M_add(const Extended*,const Extended*,Extended*);void extF80M_sub(const Extended*,const Extended*,Extended*);
void extF80M_mul(const Extended*,const Extended*,Extended*);void extF80M_div(const Extended*,const Extended*,Extended*);
void extF80M_sqrt(const Extended*,Extended*);void extF80M_roundToInt(const Extended*,unsigned char,bool,Extended*);
bool extF80M_lt_quiet(const Extended*,const Extended*);bool extF80M_eq(const Extended*,const Extended*);
extern unsigned char softfloat_roundingMode;
}
namespace {
u64 state=0x9e3779b97f4a7c15ull;
u64 next(){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
u64 checks=0,tagged_results=0;
// A fresh SoftFloat value (its padding may be anything; only the fields count).
Extended soft(const Extended& value){Extended r;r.significand=value.significand;r.exponent=value.exponent;return r;}
bool same(const Extended& a,const Extended& b){return a.significand==b.significand&&a.exponent==b.exponent;}
[[noreturn]] void fail(const char* what,const Extended& a,const Extended& b,const Extended& got,const Extended& want){
    std::printf("FAIL %s mode=%u/%u\n  a=%04x:%016llx tag=%04x/%08x\n  b=%04x:%016llx tag=%04x/%08x\n  got=%04x:%016llx tag=%04x/%08x\n  want=%04x:%016llx\n",what,0u,unsigned(softfloat_roundingMode),
        a.exponent,(unsigned long long)a.significand,a.reserved16,a.reserved32,b.exponent,(unsigned long long)b.significand,b.reserved16,b.reserved32,
        got.exponent,(unsigned long long)got.significand,got.reserved16,got.reserved32,want.exponent,(unsigned long long)want.significand);std::exit(1);}
// A tagged value's float bits must be exactly its value.
void check_tag(const char* what,const Extended& v){
    if(v.reserved16!=Extended::float_tag)return;++tagged_results;
    Extended from;f32_to_extF80M({v.reserved32},&from);
    const u32 e=(v.reserved32>>23)&255u;
    if(e==255u||(!e&&(v.reserved32&0x7fffffu))||!same(from,v)){std::printf("FAIL tag %s: %04x:%016llx bits %08x\n",what,v.exponent,(unsigned long long)v.significand,v.reserved32);std::exit(1);}
}
void check(const char* what,const Extended& a,const Extended& b,const Extended& got,const Extended& want){++checks;check_tag(what,got);if(!same(got,want))fail(what,a,b,got,want);}
u32 float_bits(u64 i,u64 r){
    static const u32 special[]{0,0x80000000u,1,0x807fffffu,0x00800000u,0x00800001u,0x00ffffffu,0x01000000u,0x3f7fffffu,0x3f800000u,0xbf800000u,0x3f800001u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u,0x4b000000u,0x4affffffu,0x00400000u,0x80800000u};
    switch(i%5){case 0:return special[r%(sizeof(special)/4)];
    case 1:case 2:return (u32(r)&0x807fffffu)|((118u+u32(r>>32)%22u)<<23);   // gameplay range
    case 3:return (u32(r)&0x807fffffu)|((1u+u32(r>>32)%4u)<<23);            // near the f32 normal boundary
    default:return u32(r);}
}
Extended random_extended(u64 r){Extended v;v.significand=next()|(r&1?0x8000000000000000ull:0);v.exponent=u16((r>>8)&0x8000u)|u16(r>>1&3?16256u+u32(r>>20)%260u:u32(r>>20)%32768u);return v;}
}
int main(int argc,char** argv){
    const u64 iterations=argc>1?std::strtoull(argv[1],nullptr,10):2000000;
    const th10::Precision precisions[]{th10::Precision::Single,th10::Precision::Double,th10::Precision::Extended};
    for(auto precision:precisions)for(int rounding=0;rounding<4;rounding++){
        th10::arithmetic_mode(precision,th10::Rounding(rounding));
        Extended chain[4]{Extended::from_float(1.0f),Extended::from_float(-3.5f),Extended::from_float(0.0f),Extended::from_float(1e-30f)};
        for(u64 i=0;i<iterations;i++){
            const u64 r1=next(),r2=next();u32 ua=float_bits(i,r1),ub=float_bits(i/5,r2);
            float fa,fb;std::memcpy(&fa,&ua,4);std::memcpy(&fb,&ub,4);
            Extended ta=Extended::from_float(fa),tb=Extended::from_float(fb),sa,sb;f32_to_extF80M({ua},&sa);f32_to_extF80M({ub},&sb);
            sa.reserved16=sb.reserved16=0;   // SoftFloat's struct store leaves stack bytes in the padding (seen: our own tag)
            check("from_float a",ta,ta,ta,sa);check("from_float b",tb,tb,tb,sb);
            // Operand pairs: tagged/tagged, tagged/untagged, untagged/tagged, chain/tagged, random/tagged, chain/chain.
            const Extended ra=random_extended(next());
            const Extended* lefts[]{&ta,&ta,&sa,&chain[i&3],&ra,&chain[(i+1)&3]};
            const Extended* rights[]{&tb,&sb,&tb,&tb,&tb,&chain[(i+2)&3]};
            for(int p=0;p<6;p++){
                const Extended a=*lefts[p],b=*rights[p],ca=soft(a),cb=soft(b);Extended want;
                extF80M_add(&ca,&cb,&want);check("add",a,b,a+b,want);
                extF80M_sub(&ca,&cb,&want);check("sub",a,b,a-b,want);
                extF80M_mul(&ca,&cb,&want);check("mul",a,b,a*b,want);
                extF80M_div(&ca,&cb,&want);check("div",a,b,a/b,want);
                checks+=2;if((a<b)!=extF80M_lt_quiet(&ca,&cb)||(a==b)!=extF80M_eq(&ca,&cb))fail("compare",a,b,a,b);
                if(p==5)chain[i&3]=(i&1)?a*b:a+b;
            }
            const Extended* values[]{&ta,&sa,&ra,&chain[i&3]};
            for(const Extended* value:values){
                const Extended v=*value,cv=soft(v);Extended want;
                want=cv;want.exponent^=0x8000;check("negate",v,v,-v,want);
                want=cv;want.exponent&=0x7fff;check("magnitude",v,v,v.magnitude(),want);
                extF80M_sqrt(&cv,&want);check("sqrt",v,v,v.square_root(),want);
                extF80M_roundToInt(&cv,softfloat_roundingMode,false,&want);check("round",v,v,v.round_to_integer(),want);
                checks+=3;
                const float f=v.to_float();u32 fb2;std::memcpy(&fb2,&f,4);if(fb2!=extF80M_to_f32(&cv).v)fail("to_float",v,v,v,cv);
                const double d=v.to_double();u64 db;std::memcpy(&db,&d,8);if(db!=extF80M_to_f64(&cv).v)fail("to_double",v,v,v,cv);
                if(u32(v.truncate_int())!=u32(extF80M_to_i64(&cv,1,false)))fail("truncate",v,v,v,cv);
            }
            {   u64 db=next();if(i&1)db=(db&0x800fffffe0000000ull)|(u64(897u+u32(db>>40)%254u)<<52);   // exact floats half the time
                if(i%7==0)db&=0x8000000000000000ull;double d;std::memcpy(&d,&db,8);Extended want;f64_to_extF80M({db},&want);
                const Extended got=Extended::from_double(d);check("from_double",got,got,got,want);}
            {   const i32 n=i32(i&2?next()%40000000u-20000000:next());Extended want;i64_to_extF80M(n,&want);
                const Extended got=Extended::from_int(n);check("from_int",got,got,got,want);
                const i64 m=i64(next());i64_to_extF80M(m,&want);const Extended big=Extended::from_int64(m);check("from_int64",big,big,big,want);}
        }
        std::printf("mode %d/%d ok\n",int(precision),rounding);
    }
    std::printf("PASS: %llu checks, %llu tagged results\n",(unsigned long long)checks,(unsigned long long)tagged_results);return 0;
}
