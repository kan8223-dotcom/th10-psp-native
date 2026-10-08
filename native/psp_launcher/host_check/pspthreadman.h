#pragma once
// Host build (host_check/build.sh): the system clock in microseconds.
#include <chrono>
inline long long sceKernelGetSystemTimeWide()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
