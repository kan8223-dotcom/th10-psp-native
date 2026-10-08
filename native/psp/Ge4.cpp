// GE 4 MiB eDRAM mode (Slim+/Go), as in the TH07/TH08 PSP ports: the proven
// kernel wrapper ge4wrap_texv1.prx (module th07_ge4_texbw_v1_wrap, the
// Slim+/Go variant, sha256 3dc5c753, checked by psp_ge/Makefile) is loaded
// from the EBOOT folder and its exports are called only through KUBridge. The upper 2 MiB is GE-only (textures there are uploaded with
// sceGuCopyImage). Every refusal keeps the ordinary 2 MiB aperture; a state
// that cannot be proven (size readback, power lock ownership) stops in a
// loop that needs a cold power-off, exactly like the TH08 bridge.
#include "../psp_cfw/cfw_imports.h"
#include <pspge.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspmodulemgr.h>
#include <psppower.h>
#include <pspsdk.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
constexpr char wrapper_path[]="./ge4wrap_texv1.prx",wrapper_module[]="th07_ge4_texbw_v1_wrap",wrapper_library[]="ge4wrap_texv1";
constexpr unsigned hw_size_nid=0x2ddac688u,model_nid=0xbb75238fu,set_size_nid=0x703b997bu;
constexpr unsigned two_mib=0x00200000u,four_mib=0x00400000u,edram_base=0x04000000u;
static_assert(sizeof(KernelCallArg)==56u&&offsetof(KernelCallArg,ret1)==48u,"KUBridge KernelCallArg ABI");
SceUID wrapper=-1;unsigned get_hw_size=0,get_model=0,set_size=0;bool locked=false,active=false;const char* state="off";
struct Call {int outer;unsigned value;};
Call call(unsigned address,unsigned arg1=0){KernelCallArg a{};a.arg1=arg1;const int outer=kuKernelCall(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),&a);return {outer,a.ret1};}
[[noreturn]] void cold_off(){sceIoSync("ms0:",0);for(;;)sceKernelDelayThread(1000*1000);}
int wait_idle(){
    const unsigned long long start=sceKernelGetSystemTimeWide();
    for(;;){const int s=sceGeDrawSync(1);if(s==PSP_GE_LIST_DONE||s<0)return s;sceKernelDelayThread(50);
        if(sceKernelGetSystemTimeWide()-start>5000000ull)return -1;}
}
void unload(){
    if(wrapper<0)return;int status=0;const int stop=sceKernelStopModule(wrapper,0,nullptr,&status,nullptr);
    if(stop<0||sceKernelUnloadModule(wrapper)<0)cold_off();
    wrapper=-1;get_hw_size=get_model=set_size=0;
}
void refuse(const char* why){state=why;unload();if(locked){if(scePowerUnlock(0)<0)cold_off();locked=false;}}
bool under_ppsspp(){SceIoStat st;return sceIoGetstat("ms0:/PSP/SYSTEM/ppsspp.ini",&st)>=0;}
}

extern "C" {
// Call once, with the GE idle, before any VRAM above the frames is used.
bool th10_ge4_enable(void){
    if(active)return true;
    if(under_ppsspp()){state="skipped (PPSSPP)";return false;}
    wrapper=pspSdkLoadStartModule(wrapper_path,PSP_MEMORY_PARTITION_KERNEL);
    if(wrapper<0){state="wrapper not loaded";wrapper=-1;return false;}
    get_hw_size=sctrlHENFindFunction(wrapper_module,wrapper_library,hw_size_nid);get_model=sctrlHENFindFunction(wrapper_module,wrapper_library,model_nid);
    set_size=sctrlHENFindFunction(wrapper_module,wrapper_library,set_size_nid);
    if(!get_hw_size||!get_model||!set_size){refuse("exports not found");return false;}
    const Call model=call(get_model),hw=call(get_hw_size);
    const unsigned base=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr())),size=sceGeEdramGetSize();
    if(model.outer<0||hw.outer<0||int(model.value)<1||base!=edram_base||hw.value!=four_mib||size!=two_mib){refuse("gate (model/base/size)");return false;}
    const int lock=scePowerLock(0);
    if(lock<0){refuse("power lock failed");return false;}
    if(lock>0)cold_off();   // ownership of the shared lock is not provable
    locked=true;
    if(wait_idle()!=PSP_GE_LIST_DONE)cold_off();
    sceKernelDcacheWritebackInvalidateAll();
    const Call set4=call(set_size,four_mib);const unsigned after=sceGeEdramGetSize();
    if(set4.outer<0||int(set4.value)<0||after!=four_mib){
        if(after==four_mib){sceKernelDcacheWritebackInvalidateAll();const Call back=call(set_size,two_mib);if(back.outer<0||sceGeEdramGetSize()!=two_mib)cold_off();}
        else if(after!=two_mib)cold_off();
        // Wrapper refusals: -0x3d20 model (the model-3-only base file), -0x3d21 hardware size, -0x3d22 size argument.
        const char* code=set4.value==0xffffc2e0u?" wrapper model check":set4.value==0xffffc2dfu?" wrapper hw size":set4.value==0xffffc2deu?" wrapper size arg":"";
        static char why[128];std::snprintf(why,sizeof(why),"set 4 MiB failed (outer %08x inner %08x%s size %08x)",unsigned(set4.outer),set4.value,code,after);
        refuse(why);return false;
    }
    if(wait_idle()!=PSP_GE_LIST_DONE){sceKernelDcacheWritebackInvalidateAll();const Call back=call(set_size,two_mib);if(back.outer<0||sceGeEdramGetSize()!=two_mib)cold_off();refuse("GE not idle after set");return false;}
    active=true;state="4 MiB";return true;
}
bool th10_ge4_active(void){return active;}
const char* th10_ge4_state(void){return state;}
// With the GE idle and nothing drawn from the upper 2 MiB any more.
void th10_ge4_shutdown(void){
    if(!active){unload();return;}
    if(wait_idle()!=PSP_GE_LIST_DONE)cold_off();
    sceKernelDcacheWritebackInvalidateAll();
    const Call back=call(set_size,two_mib);if(back.outer<0||int(back.value)<0||sceGeEdramGetSize()!=two_mib)cold_off();
    active=false;state="restored 2 MiB";unload();
    if(locked){if(scePowerUnlock(0)<0)cold_off();locked=false;}
}
}
