// th10_port: PSP soft-float census (make CENSUS=1, a PPSSPP measurement build;
// TH08 psp/softfloat_census.cpp sha fff2bf35 is the model). The linker wraps
// libgcc's binary64 helpers and libm entries (--wrap); each wrapper counts the
// call under the chain callback that is running (th10_chain_token) and under
// the calling return address, then forwards to the real routine. The counts
// are deterministic, so PPSSPP gives the device's numbers; the PC build uses
// hardware double and cannot show this cost.
#if TH10_SOFTFLOAT_CENSUS
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" volatile int th10_census_token;   // main.cpp: the running chain callback, -1 outside
namespace {
enum Op : unsigned {DfAdd,DfMul,DfDiv,DfCmp,DfConv,LibmTrig,LibmSqrt,LibmFloor,LibmOther,OpCount};
const char* const op_names[OpCount]={"df_add","df_mul","df_div","df_cmp","df_conv","libm_trig","libm_sqrt","libm_floor","libm_other"};
std::uint32_t per_token[257][OpCount];   // token+1 (0 = outside every callback)
struct Site {std::uint32_t pc,count;};
Site sites[1024];
std::uint32_t libm_depth=0,internal=0;
inline void note(Op op,std::uint32_t pc){
    if(libm_depth&&op<LibmTrig){++internal;return;}   // a libm routine's own binary64 helpers
    const int token=th10_census_token;++per_token[token>=0&&token<256?token+1:0][op];
    std::uint32_t h=(pc>>2)*2654435761u>>22;
    for(unsigned probe=0;probe<1024;++probe,h=(h+1)&1023u){
        if(sites[h].pc==pc){++sites[h].count;return;}
        if(!sites[h].pc){sites[h].pc=pc;sites[h].count=1;return;}
    }
}
}
// Result lines (main.cpp appends them to th10_result.txt). names: the chain
// callback names indexed by token (128 update, 128 draw).
extern "C" void th10_census_report(void (*add)(const char*),const char* const* names,unsigned name_count){
    char line[512];std::uint64_t total[OpCount]{};
    for(unsigned t=0;t<257;++t)for(unsigned o=0;o<OpCount;++o)total[o]+=per_token[t][o];
    int n=std::snprintf(line,sizeof(line),"softfloat census:");
    for(unsigned o=0;o<OpCount;++o)n+=std::snprintf(line+n,sizeof(line)-n," %s=%llu",op_names[o],(unsigned long long)total[o]);
    std::snprintf(line+n,sizeof(line)-n," libm_internal=%u\n",internal);add(line);
    for(unsigned t=0;t<257;++t){std::uint64_t sum=0;for(unsigned o=0;o<OpCount;++o)sum+=per_token[t][o];if(!sum)continue;
        const int token=int(t)-1;const unsigned index=token<0?0u:unsigned(token&127);
        n=std::snprintf(line,sizeof(line),"census %s%s total=%llu",token<0?"outside":(token>=128?"draw:":""),token<0?"":(index<name_count?names[index]:"?"),(unsigned long long)sum);
        for(unsigned o=0;o<OpCount;++o)if(per_token[t][o])n+=std::snprintf(line+n,sizeof(line)-n," %s=%u",op_names[o],per_token[t][o]);
        std::snprintf(line+n,sizeof(line)-n,"\n");add(line);}
    // top call sites
    for(unsigned k=0;k<24;++k){unsigned best=1024;for(unsigned i=0;i<1024;++i)if(sites[i].count&&(best==1024||sites[i].count>sites[best].count))best=i;
        if(best==1024)break;std::snprintf(line,sizeof(line),"census site pc=%08x calls=%u\n",sites[best].pc,sites[best].count);add(line);sites[best].count=0;}
}
#define TH10_WRAP2(name,type,op) extern "C" type __real_##name(type,type); \
    extern "C" type __wrap_##name(type a,type b){note(op,std::uint32_t(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))));return __real_##name(a,b);}
#define TH10_WRAP_CMP(name) extern "C" int __real_##name(double,double); \
    extern "C" int __wrap_##name(double a,double b){note(DfCmp,std::uint32_t(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))));return __real_##name(a,b);}
#define TH10_WRAP_CONV(name,rtype,atype) extern "C" rtype __real_##name(atype); \
    extern "C" rtype __wrap_##name(atype a){note(DfConv,std::uint32_t(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))));return __real_##name(a);}
#define TH10_WRAP_LIBM1(name,op) extern "C" double __real_##name(double); \
    extern "C" double __wrap_##name(double a){note(op,std::uint32_t(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))));++libm_depth;const double r=__real_##name(a);--libm_depth;return r;}
#define TH10_WRAP_LIBM2(name,op) extern "C" double __real_##name(double,double); \
    extern "C" double __wrap_##name(double a,double b){note(op,std::uint32_t(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))));++libm_depth;const double r=__real_##name(a,b);--libm_depth;return r;}
TH10_WRAP2(__adddf3,double,DfAdd)
TH10_WRAP2(__subdf3,double,DfAdd)
TH10_WRAP2(__muldf3,double,DfMul)
TH10_WRAP2(__divdf3,double,DfDiv)
TH10_WRAP_CMP(__eqdf2)
TH10_WRAP_CMP(__nedf2)
TH10_WRAP_CMP(__gtdf2)
TH10_WRAP_CMP(__gedf2)
TH10_WRAP_CMP(__ltdf2)
TH10_WRAP_CMP(__ledf2)
TH10_WRAP_CMP(__unorddf2)
TH10_WRAP_CONV(__extendsfdf2,double,float)
TH10_WRAP_CONV(__truncdfsf2,float,double)
TH10_WRAP_CONV(__fixdfsi,int,double)
TH10_WRAP_CONV(__fixunsdfsi,unsigned,double)
TH10_WRAP_CONV(__floatsidf,double,int)
TH10_WRAP_CONV(__floatunsidf,double,unsigned)
TH10_WRAP_LIBM1(sin,LibmTrig)
TH10_WRAP_LIBM1(cos,LibmTrig)
TH10_WRAP_LIBM1(tan,LibmTrig)
TH10_WRAP_LIBM1(atan,LibmTrig)
TH10_WRAP_LIBM1(acos,LibmTrig)
TH10_WRAP_LIBM2(atan2,LibmTrig)
TH10_WRAP_LIBM1(sqrt,LibmSqrt)
TH10_WRAP_LIBM1(floor,LibmFloor)
TH10_WRAP_LIBM1(ceil,LibmFloor)
TH10_WRAP_LIBM2(fmod,LibmOther)
TH10_WRAP_LIBM2(pow,LibmOther)
#endif
