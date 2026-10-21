#include <stdio.h>

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

static float read_temp(TsDeviceCode code, TsLocation legacy)
{
    TsSession s;
    float t = -1.0f;
    if (R_SUCCEEDED(tsOpenSession(&s, code))) {
        if (R_FAILED(tsSessionGetTemperature(&s, &t)))
            t = -1.0f;
        tsSessionClose(&s);
    } else {
        s32 mc;
        if (R_SUCCEEDED(tsGetTemperatureMilliC(legacy, &mc)))
            t = mc / 1000.0f;
    }
    return t;
}

/* Busy % per core since the last call, as " 12% 40% 3%"; "n/a" on the first call or for
 * a core this thread may not run on. A core's idle ticks can only be read from that core
 * (libnx svc.h), so the thread visits each one, then gets its own affinity back. */
static void cores_busy(char *out, size_t size, rsl_telemetry *t)
{
    s32 core0;
    u64 mask0;
    size_t n = 0;
    out[0] = 0;
    if (R_FAILED(svcGetThreadCoreMask(&core0, &mask0, CUR_THREAD_HANDLE)))
        return;
    for (int c = 0; c < RSL_TELEMETRY_CORES; c++) {
        u64 idle = 0, tick = 0;
        bool ok = R_SUCCEEDED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, c, 1u << c)) &&
                  svcGetCurrentProcessorNumber() == (u32)c &&
                  R_SUCCEEDED(svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, (u64)c));
        tick = armGetSystemTick();
        if (ok && t->tick[c] && tick > t->tick[c])
            n += snprintf(out + n, size - n, " %d%%", 100 - (int)(100 * (idle - t->idle[c]) / (tick - t->tick[c])));
        else
            n += snprintf(out + n, size - n, " n/a");
        t->idle[c] = ok ? idle : 0;
        t->tick[c] = ok ? tick : 0;
    }
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core0, (u32)mask0);
}

void rsl_telemetry_line(rsl_telemetry *t, char *out, size_t size)
{
    char busy[40];
    cores_busy(busy, sizeof busy, t);
    u32 battery = 0;
    PsmChargerType charger = PsmChargerType_Unconnected;
    psmGetBatteryChargePercentage(&battery);
    psmGetChargerType(&charger);
    snprintf(out, size, "SoC %.1f C, board %.1f C, cores 0-2 busy%s, battery %u%%%s, %s",
             read_temp(TsDeviceCode_LocationExternal, TsLocation_External),
             read_temp(TsDeviceCode_LocationInternal, TsLocation_Internal), busy, (unsigned)battery,
             charger != PsmChargerType_Unconnected ? " (charging)" : "",
             appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");
}
