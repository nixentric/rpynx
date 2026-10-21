#include <switch.h>

#include "log.h"
#include "procinfo.h"

/* Every .nro started from the homebrew loader runs in the same process and
 * heap. A second game launch after a clean quit crashed inside malloc
 * (hardware run 7), so check what this run leaves behind in the heap:
 * borrowed pages (stacks of threads still mapped), uncached or device-mapped
 * pages (GPU buffers). Uncached attributes are reset, which is safe once
 * Mesa has shut down; the rest is reported. */
void rsl_heap_audit(const char *when)
{
    u64 base = 0, size = 0, uncached = 0, borrowed = 0, device = 0, locked = 0;
    svcGetInfo(&base, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&size, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
    int reset_failed = 0;
    for (u64 addr = base; addr < base + size;) {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, addr)) || mi.size == 0)
            break;
        u64 lo = mi.addr < base ? base : mi.addr;
        u64 hi = mi.addr + mi.size > base + size ? base + size : mi.addr + mi.size;
        if (mi.type != MemType_Unmapped && hi > lo) {
            if (mi.attr & MemAttr_IsBorrowed) borrowed += hi - lo;
            if (mi.attr & MemAttr_IsDeviceMapped) device += hi - lo;
            if (mi.attr & MemAttr_IsPermissionLocked) locked += hi - lo;
            if (mi.attr & MemAttr_IsUncached) {
                uncached += hi - lo;
                if (R_FAILED(svcSetMemoryAttribute((void *)lo, hi - lo, MemAttr_IsUncached, 0)))
                    reset_failed++;
            }
        }
        addr = mi.addr + mi.size;
    }
    rsl_log("heap audit (%s): uncached %llu KiB (reset, %d failed), borrowed %llu KiB, device-mapped %llu KiB, locked %llu KiB", when,
            (unsigned long long)(uncached >> 10), reset_failed, (unsigned long long)(borrowed >> 10),
            (unsigned long long)(device >> 10), (unsigned long long)(locked >> 10));
    /* Threads charged to this process's resource limit: in application mode that is this
     * process alone, in applet mode it is shared with the other applets. (svcGetThreadList
     * without a debug handle counts every thread on the console: 575 in runs 9-21.) */
    u64 limit = INVALID_HANDLE;
    s64 threads = 0;
    if (envIsSyscallHinted(0x31) && R_SUCCEEDED(svcGetInfo(&limit, InfoType_ResourceLimit, INVALID_HANDLE, 0)) &&
        limit != INVALID_HANDLE) {
        if (R_SUCCEEDED(svcGetResourceLimitCurrentValue(&threads, (Handle)limit, LimitableResource_Threads)))
            rsl_log("heap audit (%s): %lld thread(s) in this process's resource limit", when, (long long)threads);
        svcCloseHandle((Handle)limit);
    }
}
