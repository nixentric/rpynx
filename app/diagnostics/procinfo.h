#ifndef RSL_PROCINFO_H
#define RSL_PROCINFO_H

/* Log what the heap holds that the next .nro in this process could trip
 * over, and reset leftover "uncached" attributes (see procinfo.c). */
void rsl_heap_audit(const char *when);

#endif
