#pragma once
// th10_port (TH10_VOLATILE_ARENA, the PSP-1000 lane): the kernel's 4 MiB
// volatile partition (partition 5 at 0x08400000, sceKernelVolatileMemTryLock)
// as extra RAM. TH08's PSP-1000 lane locked it on the device for its ANM
// scratch (th08-psp1000 psp/anm_scratch.cpp). It is ordinary main RAM: the SC,
// the GE and the ME read it like the heap. When the lock fails (or a block
// does not fit) the callers fall back to the heap, so the arena only changes
// where bytes live, never what the game computes.
#include <cstddef>
#include <new>
extern "C" {
int th10_volatile_init(void);                  // 1: the partition is locked and the arena is up
void th10_volatile_shutdown(void);             // unlocks it (exit paths)
void* th10_volatile_alloc(std::size_t bytes);  // 64-byte aligned, nullptr when it does not fit
int th10_volatile_free(void* p);               // 1 when p came from the arena
// out[0] locked, [1] base, [2] bytes, [3] in use, [4] peak, [5] allocations,
// [6] heap fallbacks (did not fit), [7] live blocks
void th10_volatile_stats(unsigned* out);
}
// std::vector<T, VolatileAllocator<T>>: arena first, heap otherwise.
template<class T> struct VolatileAllocator {
    using value_type = T;
    VolatileAllocator() noexcept = default;
    template<class U> VolatileAllocator(const VolatileAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        if (void* p = th10_volatile_alloc(n * sizeof(T))) return static_cast<T*>(p);
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept { if (!th10_volatile_free(p)) ::operator delete(p); }
    template<class U> bool operator==(const VolatileAllocator<U>&) const noexcept { return true; }
    template<class U> bool operator!=(const VolatileAllocator<U>&) const noexcept { return false; }
};
