#ifndef TH10_PSP_UNIFIED_MODEL_DISPATCH_HPP
#define TH10_PSP_UNIFIED_MODEL_DISPATCH_HPP

#include <stdint.h>

struct Th10UnifiedModelSelection {
    int raw_model;
    uint32_t effective_model;
    uint32_t profile_id;
    bool safe_fallback;
    const char *reason;
};

/*
 * kuKernelGetModel is the user-mode-safe CFW bridge for sceKernelGetModel.
 * A failed or nonsensical query falls back to the smaller PSP-1000 payload.
 */
Th10UnifiedModelSelection th10_unified_select_model();

#endif
