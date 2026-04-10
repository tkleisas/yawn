#pragma once
// SystemInfo — Lightweight process memory query.

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <cstdio>
#endif

namespace yawn {
namespace util {

// Returns the current process working set (resident memory) in bytes.
inline uint64_t processMemoryBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize;
    return 0;
#else
    // Linux: read /proc/self/statm (pages), multiply by page size
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long pages = 0;
    if (fscanf(f, "%*u %lu", &pages) != 1) pages = 0;  // second field = RSS
    fclose(f);
    return pages * 4096ULL;
#endif
}

// Returns process memory in megabytes.
inline float processMemoryMB() {
    return static_cast<float>(processMemoryBytes()) / (1024.0f * 1024.0f);
}

} // namespace util
} // namespace yawn
