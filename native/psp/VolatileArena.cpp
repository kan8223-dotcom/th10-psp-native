// th10_port (TH10_VOLATILE_ARENA): see VolatileArena.hpp. Linked only when the
// build defines TH10_VOLATILE_ARENA=1 (native/psp_ge/Makefile).
#include "VolatileArena.hpp"
#include <pspsuspend.h>
#include <cstdint>
namespace {
using u32 = std::uint32_t;
constexpr u32 align = 64, max_blocks = 512;
// Allocated blocks sorted by offset; a new block takes the first gap that fits.
// The game allocates a few dozen long-lived blocks here (sound buffers), all on
// the game thread, so a sorted array is enough and keeps no headers in the arena.
struct Block { u32 offset, bytes; };
Block blocks[max_blocks];
u32 count = 0, in_use = 0, peak = 0, allocations = 0, fallbacks = 0;
unsigned char* base = nullptr; u32 size = 0; bool locked = false;
}
extern "C" int th10_volatile_init(void) {
    if (locked) return 1;
    void* p = nullptr; int bytes = 0;
    const int rc = sceKernelVolatileMemTryLock(0, &p, &bytes);
    if (rc != 0 || !p || bytes <= 0) { if (rc == 0) sceKernelVolatileMemUnlock(0); return 0; }
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(p);
    const std::uintptr_t aligned = (raw + align - 1) & ~std::uintptr_t(align - 1);
    base = reinterpret_cast<unsigned char*>(aligned);
    size = u32(bytes) - u32(aligned - raw);
    locked = true; return 1;
}
extern "C" void th10_volatile_shutdown(void) {
    if (!locked) return;
    sceKernelVolatileMemUnlock(0);
    locked = false;   // blocks still listed are only bookkept from here on (frees keep working)
}
extern "C" void* th10_volatile_alloc(std::size_t request) {
    if (!locked || request == 0 || request > size || count >= max_blocks) { if (request) ++fallbacks; return nullptr; }
    const u32 need = (u32(request) + align - 1) & ~(align - 1);
    u32 at = 0, i = 0;
    for (; i < count; ++i) {
        if (blocks[i].offset - at >= need) break;
        at = blocks[i].offset + blocks[i].bytes;
    }
    if (i == count && size - at < need) { ++fallbacks; return nullptr; }
    for (u32 k = count; k > i; --k) blocks[k] = blocks[k - 1];
    blocks[i] = Block{at, need}; ++count; ++allocations;
    in_use += need; if (in_use > peak) peak = in_use;
    return base + at;
}
extern "C" int th10_volatile_free(void* p) {
    if (!base || !p) return 0;
    const unsigned char* q = static_cast<unsigned char*>(p);
    if (q < base || q >= base + size) return 0;
    const u32 off = u32(q - base);
    for (u32 i = 0; i < count; ++i) {
        if (blocks[i].offset != off) continue;
        in_use -= blocks[i].bytes;
        for (u32 k = i + 1; k < count; ++k) blocks[k - 1] = blocks[k];
        --count; return 1;
    }
    return 0;   // inside the arena but not a block start: never handed out
}
extern "C" void th10_volatile_stats(unsigned* out) {
    out[0] = locked ? 1u : 0u; out[1] = u32(reinterpret_cast<std::uintptr_t>(base)); out[2] = size;
    out[3] = in_use; out[4] = peak; out[5] = allocations; out[6] = fallbacks; out[7] = count;
}
