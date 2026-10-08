#include "Interpolation.hpp"
#if TH10_FAST_EASING
#include "../../../portable/numeric/SpriteNumber.hpp"
#include <cmath>
#endif
namespace th10 {
// 0x44c350. Each multiplication keeps the original left-to-right order.
Extended easing(float elapsed, float duration, InterpolationMode mode) noexcept {
    auto t = number(elapsed) / number(duration);
    const auto one = number(1.0f), two = number(2.0f), half = number(0.5f);
    switch (mode) {
    case InterpolationMode::Accelerate2: return t*t;
    case InterpolationMode::Accelerate3: return t*t*t;
    case InterpolationMode::Accelerate4: return t*t*t*t;
    case InterpolationMode::Decelerate2: t=one-t; return one-t*t;
    case InterpolationMode::Decelerate3: t=one-t; return one-t*t*t;
    case InterpolationMode::Decelerate4: t=one-t; return one-t*t*t*t;
    case InterpolationMode::Smooth2:
        t=t+t; if(t<one)return t*t*half; t=two-t; return (two-t*t)*half;
    case InterpolationMode::Smooth3:
        t=t+t; if(t<one)return t*t*t*half; t=two-t; return (two-t*t*t)*half;
    case InterpolationMode::Smooth4:
        t=t+t; if(t<one)return t*t*t*t*half; t=two-t; return (two-t*t*t*t)*half;
    case InterpolationMode::FastSlow2:
        t=t+t; if(t<one){t=one-t;return half-t*t*half;} t=t-one; return t*t*half+half;
    case InterpolationMode::FastSlow3:
        t=t+t; if(t<one){t=one-t;return half-t*t*t*half;} t=t-one; return t*t*t*half+half;
    case InterpolationMode::FastSlow4:
        t=t+t; if(t<one){t=one-t;return half-t*t*t*t*half;} t=t-one; return t*t*t*t*half+half;
    case InterpolationMode::HoldStart: return number(0.0f);
    case InterpolationMode::HoldEnd: return one;
    default: return t;
    }
}
#if TH10_FAST_EASING
// th10_port (TH10_FAST_EASING): easing() on either number type (the same text),
// and the range in which plain floats give the Extended results bit for bit:
// precision 32 nearest, elapsed 0 or in [1,2^20], duration in [1,2^16], so
// t = elapsed/duration is 0 or in [2^-16,2^20]; every easing result is then 0
// or in [2^-64,2^81] (the smallest is t^4 at t = 2^-16; 1-t is 0 or at least
// 2^-24), and with coordinates 0 or in [2^-20,2^20] (differences 0 or at
// least 2^-43) every product is 0 or at least 2^-107 and every sum is an exact
// cancellation or accepted: no step leaves the Extended fast paths.
template<class Number> auto easing_as(Number number, float elapsed, float duration, InterpolationMode mode) noexcept -> decltype(number(0.0f)) {
    auto t = number(elapsed) / number(duration);
    const auto one = number(1.0f), two = number(2.0f), half = number(0.5f);
    switch (mode) {
    case InterpolationMode::Accelerate2: return t*t;
    case InterpolationMode::Accelerate3: return t*t*t;
    case InterpolationMode::Accelerate4: return t*t*t*t;
    case InterpolationMode::Decelerate2: t=one-t; return one-t*t;
    case InterpolationMode::Decelerate3: t=one-t; return one-t*t*t;
    case InterpolationMode::Decelerate4: t=one-t; return one-t*t*t*t;
    case InterpolationMode::Smooth2:
        t=t+t; if(t<one)return t*t*half; t=two-t; return (two-t*t)*half;
    case InterpolationMode::Smooth3:
        t=t+t; if(t<one)return t*t*t*half; t=two-t; return (two-t*t*t)*half;
    case InterpolationMode::Smooth4:
        t=t+t; if(t<one)return t*t*t*t*half; t=two-t; return (two-t*t*t*t)*half;
    case InterpolationMode::FastSlow2:
        t=t+t; if(t<one){t=one-t;return half-t*t*half;} t=t-one; return t*t*half+half;
    case InterpolationMode::FastSlow3:
        t=t+t; if(t<one){t=one-t;return half-t*t*t*half;} t=t-one; return t*t*t*half+half;
    case InterpolationMode::FastSlow4:
        t=t+t; if(t<one){t=one-t;return half-t*t*t*t*half;} t=t-one; return t*t*t*t*half+half;
    case InterpolationMode::HoldStart: return number(0.0f);
    case InterpolationMode::HoldEnd: return one;
    default: return t;
    }
}
inline bool easing_inputs(float elapsed,float duration){
    const float e=std::fabs(elapsed);
    return single_precision_nearest()&&(e==0||(e>=1.0f&&e<=0x1p20f))&&duration>=1.0f&&duration<=0x1p16f;
}
inline bool easing_coordinate(float v){const float a=std::fabs(v);return a==0||(a>=0x1p-20f&&a<=0x1p20f);}
#if TH10_FAST_EASING_INLINE
// th10_port (TH10_FAST_EASING_INLINE): a functor type (not a function pointer), so
// easing_as makes no indirect call per operand; the same float operations in the same order.
struct AsFloatNumber{touhou::numeric::SpriteNumber operator()(float v)const noexcept{return touhou::numeric::SpriteNumber(v);}};
#define TH10_AS_FLOAT AsFloatNumber{}
// Extended::from_int(d).to_float() with the out-of-line bodies' own code (Arithmetic.cpp:105,104).
inline float duration_float(i32 d) noexcept {return arithmetic::to_float_fast(arithmetic::from_int_fast(d));}
#else
inline touhou::numeric::SpriteNumber as_float_number(float v){return touhou::numeric::SpriteNumber(v);}
#define TH10_AS_FLOAT as_float_number
inline float duration_float(i32 d) noexcept {return Extended::from_int(d).to_float();}
#endif
#endif
namespace {
template<class T,unsigned N> bool finish(Interpolator<T,N>& value, const float* default_rate) noexcept {
    if(value.duration<=0)return false;
    TH10_TIMER_TICK(value.timer);
    if(value.timer.current<value.duration)return false;
    if(!(value.flags&1)){value.timer.reset();value.timer.rate=default_rate;value.flags|=1;}
    value.timer.current=value.duration;
    value.timer.previous=wrapping_add(value.duration,-1);
    value.timer.fractional=Extended::from_int(value.duration).to_float();
    value.duration=0;
    return true;
}
template<unsigned N> void moving(Interpolator<float,N>& value,float* output,bool accelerated) noexcept {
    for(unsigned axis=0;axis<N;++axis){
        const float delta=accelerated?value.final_tangent[axis]:value.end[axis];
        value.start[axis]=Scalar::add(value.start[axis],delta);
        output[axis]=value.start[axis];
    }
    if(accelerated)for(unsigned axis=0;axis<N;++axis)
        value.final_tangent[axis]=Scalar::add(value.final_tangent[axis],value.end[axis]);
}
template<unsigned N> void hermite(const Interpolator<float,N>& value,float* output) noexcept {
    const auto rounded=[](Extended v){return number(v.to_float());};
    const auto t=number(value.timer.fractional)/Extended::from_int(value.duration);
    const auto one=number(1.0f),three=number(3.0f),minus_one=rounded(t-one);
    const auto first=rounded((one+(t+t))*minus_one*minus_one);
    const auto second=(three-(t+t))*t*t;
    const auto third=(one-t)*(one-t)*t;
    const auto fourth=minus_one*t*t;
    Extended a[N],b[N],c[N],d[N];
    for(unsigned axis=0;axis<N;++axis){
        a[axis]=first*number(value.start[axis]);
        b[axis]=second*number(value.end[axis]);
        c[axis]=rounded(third*number(value.initial_tangent[axis]));
        d[axis]=rounded(fourth*number(value.final_tangent[axis]));
        if(axis+1<N)b[axis]=rounded(b[axis]);
        else a[axis]=rounded(a[axis]);
    }
    // The compiler spilled the last coordinate's first sum in both versions;
    // the three-coordinate version also spilled each partial sum for x.
    if constexpr(N==2){
        output[0]=(a[0]+b[0]+c[0]+d[0]).to_float();
        output[1]=(rounded(a[1]+b[1])+c[1]+d[1]).to_float();
    }else{
        output[0]=(rounded(rounded(a[0]+b[0])+c[0])+d[0]).to_float();
        output[1]=(a[1]+b[1]+c[1]+d[1]).to_float();
        output[2]=(rounded(a[2]+b[2])+c[2]+d[2]).to_float();
    }
}
template<unsigned N> bool special(Interpolator<float,N>& value,float* output,const float* default_rate) noexcept {
    if(finish(value,default_rate)){
        const auto* result=value.mode==InterpolationMode::Velocity?value.start:value.end;
        std::memcpy(output,result,N*sizeof(float));return true;
    }
    if(value.mode==InterpolationMode::Velocity){moving(value,output,false);return true;}
    if(value.mode==InterpolationMode::Acceleration){moving(value,output,true);return true;}
    if(value.mode==InterpolationMode::Hermite){hermite(value,output);return true;}
    return false;
}
}
// 0x441ad0. The two coordinates have different spill points in the original.
Vec2 sample(Vec2Interpolator& value,const float* default_rate) noexcept {
    float out[2];
    if(!special(value,out,default_rate)){
#if TH10_FAST_EASING
        const float duration=duration_float(value.duration);
        if(easing_inputs(value.timer.fractional,duration)&&easing_coordinate(value.start[0])&&easing_coordinate(value.end[0])&&easing_coordinate(value.start[1])&&easing_coordinate(value.end[1])){
            using touhou::numeric::SpriteNumber;const auto t=easing_as(TH10_AS_FLOAT,value.timer.fractional,duration,value.mode);
            out[0]=((SpriteNumber(value.end[0])-SpriteNumber(value.start[0]))*t+SpriteNumber(value.start[0])).to_float();
            const auto difference=SpriteNumber(Scalar::sub(value.end[1],value.start[1]));
            out[1]=(SpriteNumber((difference*t).to_float())+SpriteNumber(value.start[1])).to_float();
            return {out[0],out[1]};
        }
#endif
        const auto t=easing(value.timer.fractional,Extended::from_int(value.duration).to_float(),value.mode);
        out[0]=((number(value.end[0])-number(value.start[0]))*t+number(value.start[0])).to_float();
        const auto difference=number(Scalar::sub(value.end[1],value.start[1]));
        out[1]=(number((difference*t).to_float())+number(value.start[1])).to_float();
    }
    return {out[0],out[1]};
}
// 0x404610.
Vec3 sample(Vec3Interpolator& value,const float* default_rate) noexcept {
    float out[3];
    if(!special(value,out,default_rate)){
#if TH10_FAST_EASING
        const float duration=duration_float(value.duration);
        if(easing_inputs(value.timer.fractional,duration)&&easing_coordinate(value.start[0])&&easing_coordinate(value.end[0])&&easing_coordinate(value.start[1])&&easing_coordinate(value.end[1])&&easing_coordinate(value.start[2])&&easing_coordinate(value.end[2])){
            using touhou::numeric::SpriteNumber;const auto t=easing_as(TH10_AS_FLOAT,value.timer.fractional,duration,value.mode);
            out[0]=(SpriteNumber(((SpriteNumber(value.end[0])-SpriteNumber(value.start[0]))*t).to_float())+SpriteNumber(value.start[0])).to_float();
            out[1]=((SpriteNumber(value.end[1])-SpriteNumber(value.start[1]))*t+SpriteNumber(value.start[1])).to_float();
            const auto difference=SpriteNumber(Scalar::sub(value.end[2],value.start[2]));
            out[2]=(SpriteNumber((difference*t).to_float())+SpriteNumber(value.start[2])).to_float();
            return {out[0],out[1],out[2]};
        }
#endif
        const auto t=easing(value.timer.fractional,Extended::from_int(value.duration).to_float(),value.mode);
        out[0]=(number(((number(value.end[0])-number(value.start[0]))*t).to_float())+number(value.start[0])).to_float();
        out[1]=((number(value.end[1])-number(value.start[1]))*t+number(value.start[1])).to_float();
        const auto difference=number(Scalar::sub(value.end[2],value.start[2]));
        out[2]=(number((difference*t).to_float())+number(value.start[2])).to_float();
    }
    return {out[0],out[1],out[2]};
}
namespace {
template<unsigned N> bool integer_motion(Interpolator<i32,N>& value,i32* output,const float* default_rate) noexcept {
    if(finish(value,default_rate)){
        std::memcpy(output,value.mode==InterpolationMode::Velocity?value.start:value.end,N*4);return true;
    }
    if(value.mode!=InterpolationMode::Velocity&&value.mode!=InterpolationMode::Acceleration)return false;
    for(unsigned axis=0;axis<N;++axis){
        value.start[axis]=wrapping_add(value.start[axis],value.mode==InterpolationMode::Velocity?value.end[axis]:value.final_tangent[axis]);
        output[axis]=value.start[axis];
    }
    if(value.mode==InterpolationMode::Acceleration)for(unsigned axis=0;axis<N;++axis)
        value.final_tangent[axis]=wrapping_add(value.final_tangent[axis],value.end[axis]);
    return true;
}
i32 difference(i32 end,i32 start){const u32 value=static_cast<u32>(end)-static_cast<u32>(start);i32 result;std::memcpy(&result,&value,4);return result;}
#if TH10_FAST_EASING && TH10_FAST_EASING_INT
// th10_port (TH10_FAST_EASING_INT): the integer interpolators' easing path in
// plain floats. With easing_inputs() (precision 32 nearest, elapsed 0 or in [1,2^20],
// duration in [1,2^16]) easing_as equals easing() (FAST_EASING) and t is 0 or in
// [2^-64,2^81]; from_int(v) is the tagged float(v) for |v| <= 2^24 (from_int_fast).
// Products t*d with |d| in {0}u[1,2^24] are 0 (a zero factor) or in [2^-64,2^105]:
// accepted by mul_fast. A sum p+s with integer |s| <= 2^24 is a multiple of 2^-87,
// so 0 (p == -s, accepted) or at least 2^-87 and below 2^106: accepted by add_fast.
// Truncation goes through truncate_fast on the same tagged value.
// tools/int_easing_check.cpp compares both with the Extended lines.
inline bool int_operand(i32 v) noexcept {return v>=-16777216&&v<=16777216;}
#ifdef TH10_INT_EASING_COUNT
}
extern "C" unsigned long long th10_int_easing_taken;
namespace {
#define TH10_INT_TAKEN() (++th10_int_easing_taken)
#else
#define TH10_INT_TAKEN() ((void)0)
#endif
inline i32 truncate_float(float v) noexcept {return arithmetic::truncate_fast(arithmetic::tagged(arithmetic::bits_of(v)));}
#endif
}
// 0x441600. Color components convert each weighted term separately.
Rgb sample(RgbInterpolator& value,const float* default_rate) noexcept {
    i32 out[3];
    if(!integer_motion(value,out,default_rate)){
        if(value.mode==InterpolationMode::Hermite){
            const auto ratio=number(value.timer.fractional)/Extended::from_int(value.duration);
            const auto t=number(ratio.to_float()),two_t=number((ratio+ratio).to_float());
            const auto one=number(1.0f),minus=number((t-one).to_float());
            const Extended weights[]={number(((two_t+one)*minus*minus).to_float()),
                number(((number(3.0f)-two_t)*t*t).to_float()),
                number(((one-t)*(one-t)*t).to_float()),number((minus*t*t).to_float())};
            const i32* terms[]={value.start,value.end,value.initial_tangent,value.final_tangent};
            for(unsigned axis=0;axis<3;++axis){
                out[axis]=0;unsigned term=0;
                for(const auto weight:weights)out[axis]=wrapping_add(out[axis],(Extended::from_int(terms[term++][axis])*weight).truncate_int());
            }
        }else{
#if TH10_FAST_EASING && TH10_FAST_EASING_INT
            const float duration=duration_float(value.duration);
            const i32 d0=difference(value.end[0],value.start[0]),d1=difference(value.end[1],value.start[1]),d2=difference(value.end[2],value.start[2]);
            if(easing_inputs(value.timer.fractional,duration)&&int_operand(d0)&&int_operand(d1)&&int_operand(d2)){
                TH10_INT_TAKEN();const float t=easing_as(TH10_AS_FLOAT,value.timer.fractional,duration,value.mode).value;
                out[0]=wrapping_add(value.start[0],truncate_float(float(d0)*t));
                out[1]=wrapping_add(value.start[1],truncate_float(float(d1)*t));
                out[2]=wrapping_add(value.start[2],truncate_float(float(d2)*t));
                return {out[0],out[1],out[2]};
            }
#endif
            const auto t=easing(value.timer.fractional,Extended::from_int(value.duration).to_float(),value.mode);
            for(unsigned axis=0;axis<3;++axis)
                out[axis]=wrapping_add(value.start[axis],(Extended::from_int(difference(value.end[axis],value.start[axis]))*t).truncate_int());
        }
    }
    return {out[0],out[1],out[2]};
}
// 0x441950. Alpha converts the final sum, unlike the RGB interpolator.
i32 sample(AlphaInterpolator& value,const float* default_rate) noexcept {
    i32 result;
    if(integer_motion(value,&result,default_rate))return result;
    if(value.mode==InterpolationMode::Hermite){
        const auto t=number(value.timer.fractional)/Extended::from_int(value.duration),one=number(1.0f);
        const auto two_t=t+t,minus=t-one,inverse=one-t;
        const auto end=(number(3.0f)-two_t)*t*t*Extended::from_int(value.end[0]);
        const auto final_tangent=minus*t*t*Extended::from_int(value.final_tangent[0]);
        const auto initial_tangent=inverse*inverse*t*Extended::from_int(value.initial_tangent[0]);
        const auto start=(two_t+one)*minus*minus*Extended::from_int(value.start[0]);
        return (end+final_tangent+initial_tangent+start).truncate_int();
    }
#if TH10_FAST_EASING && TH10_FAST_EASING_INT
    {   const float duration=duration_float(value.duration);const i32 d=difference(value.end[0],value.start[0]);
        if(easing_inputs(value.timer.fractional,duration)&&int_operand(d)&&int_operand(value.start[0])){
            TH10_INT_TAKEN();const float t=easing_as(TH10_AS_FLOAT,value.timer.fractional,duration,value.mode).value;
            return truncate_float(t*float(d)+float(value.start[0]));
        }
    }
#endif
    const auto t=easing(value.timer.fractional,Extended::from_int(value.duration).to_float(),value.mode);
    return (t*Extended::from_int(difference(value.end[0],value.start[0]))+Extended::from_int(value.start[0])).truncate_int();
}
}
