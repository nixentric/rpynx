#ifndef RSL_PROCINFO_H
#define RSL_PROCINFO_H

#include <stddef.h>

#include <switch.h>

/* Log what the heap holds that the next .nro in this process could trip
 * over, and reset leftover "uncached" attributes (see procinfo.c). */
void rsl_heap_audit(const char *when);

/* Heat and load, shared by the loader and the runtime: SoC and board
 * temperature, how busy cores 0-2 were since the last call ("n/a" on the
 * first), battery and docked/handheld, as one line of text in out. Needs
 * tsInitialize() and psmInitialize(). */
#define RSL_TELEMETRY_CORES 3   /* an application runs on cores 0-2; core 3 is the system's */
typedef struct { u64 idle[RSL_TELEMETRY_CORES], tick[RSL_TELEMETRY_CORES]; } rsl_telemetry;
void rsl_telemetry_line(rsl_telemetry *t, char *out, size_t size);

#endif
