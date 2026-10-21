/* Every .nro started from the homebrew loader shares this process. A thread
 * that exits keeps its stack mapped, and the heap pages behind it borrowed,
 * until someone closes it, and the next allocation that reaches those pages
 * faults (hardware runs 10-11: sphaira after Exit, memset in a relaunched
 * game). CPython detaches every thread it starts, but this libnx has no
 * pthread_detach (ENOSYS); some threads are never joined at all. So every
 * pthread is tracked (create/join are wrapped, see the Makefile): detached
 * ones are closed once they exit, and at runtime exit every exited thread is
 * closed, with the entry point of those nobody joined logged. */
#include "runtime.h"

#include <errno.h>

#include "log.h"

struct __pthread_t { Thread thr; void *rc; };   /* libnx's own layout */
void __libnx_free(void *p);
int __real___syscall_thread_create(struct __pthread_t **thread, void *(*func)(void *), void *arg,
                                   void *stack_addr, size_t stack_size);
void *__real___syscall_thread_join(struct __pthread_t *thread);
extern char _start[];   /* module start: entry offsets match the ELF */

static Mutex threads_lock;
static struct { struct __pthread_t *t; void *(*func)(void *); bool detached; } threads[256];
static size_t threads_n, threads_closed, threads_untracked;
size_t g_threads_stuck;

/* Caller holds threads_lock. Closes exited threads that are detached, or all
 * exited ones at exit; returns how many tracked threads still run. */
static size_t reap_threads(bool at_exit)
{
    size_t n = 0, running = 0;
    for (size_t i = 0; i < threads_n; i++) {
        struct __pthread_t *t = threads[i].t;
        bool exited = R_SUCCEEDED(svcWaitSynchronizationSingle(t->thr.handle, 0));
        if (exited && (threads[i].detached || at_exit)) {
            if (!threads[i].detached)
                rsl_log("thread never joined, closed: entry +0x%lx",
                        (unsigned long)((char *)threads[i].func - _start));
            threadClose(&t->thr);
            __libnx_free(t);
            threads_closed++;
            continue;
        }
        if (!exited) {
            running++;
            if (at_exit)
                rsl_log("thread still running at exit: entry +0x%lx",
                        (unsigned long)((char *)threads[i].func - _start));
        }
        threads[n++] = threads[i];
    }
    threads_n = n;
    return running;
}

int __wrap___syscall_thread_create(struct __pthread_t **thread, void *(*func)(void *), void *arg,
                                   void *stack_addr, size_t stack_size)
{
    int rc = __real___syscall_thread_create(thread, func, arg, stack_addr, stack_size);
    if (rc == 0) {
        mutexLock(&threads_lock);
        reap_threads(false);
        if (threads_n < sizeof threads / sizeof threads[0]) {
            threads[threads_n].t = *thread;
            threads[threads_n].func = func;
            threads[threads_n++].detached = false;
        } else {
            threads_untracked++;   /* full: this one leaks as before */
        }
        mutexUnlock(&threads_lock);
    }
    return rc;
}

void *__wrap___syscall_thread_join(struct __pthread_t *t)
{
    mutexLock(&threads_lock);   /* joining closes it: stop tracking */
    for (size_t i = 0; i < threads_n; i++)
        if (threads[i].t == t) {
            threads[i] = threads[--threads_n];
            break;
        }
    mutexUnlock(&threads_lock);
    return __real___syscall_thread_join(t);
}

int __syscall_thread_detach(struct __pthread_t *t)
{
    int rc = ESRCH;
    mutexLock(&threads_lock);
    for (size_t i = 0; i < threads_n; i++)
        if (threads[i].t == t) {
            threads[i].detached = true;
            rc = 0;
        }
    reap_threads(false);
    mutexUnlock(&threads_lock);
    return rc;
}

/* Waits up to `ms` for every tracked thread except the caller to finish; returns how
 * many still run. */
size_t wait_threads(int ms)
{
    Handle self = threadGetCurHandle();
    u64 deadline = armGetSystemTick() + armNsToTicks((u64)ms * 1000000ULL);
    size_t running;
    for (;;) {
        running = 0;
        mutexLock(&threads_lock);
        for (size_t i = 0; i < threads_n; i++)
            if (threads[i].t->thr.handle != self &&
                R_FAILED(svcWaitSynchronizationSingle(threads[i].t->thr.handle, 0)))
                running++;
        mutexUnlock(&threads_lock);
        if (!running || armGetSystemTick() >= deadline)
            return running;
        svcSleepThread(10000000ULL);
    }
}

void close_threads(void)
{
    mutexLock(&threads_lock);
    size_t running = reap_threads(true);
    mutexUnlock(&threads_lock);
    g_threads_stuck = running;
    rsl_log("threads: %zu closed, %zu still running, %zu untracked", threads_closed, running,
            threads_untracked);
}
