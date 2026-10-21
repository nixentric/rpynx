/* Shared by the runtime's files: main.c (process, game and script runs, watchdog),
 * rslnx.c (the _rslnx module for the Python bridge), pyrun.c (CPython) and threads.c
 * (thread tracking). Include it first: it includes Python.h, which must come before
 * any system header. */
#ifndef RSL_RUNTIME_H
#define RSL_RUNTIME_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

#define ROOT        "sdmc:/switch/renpy-loader"
#define RENPY_BASE  "romfs:/renpy-base"

/* rslnx.c */
PyObject *PyInit__rslnx(void);
void load_controls(const char *game_id);
void on_applet(AppletHookType hook, void *param);   /* resets the controller state */

/* pyrun.c */
struct job {
    const char *script;     /* file run as __main__ */
    int argc;
    char *argv[8];          /* sys.argv */
    bool renpy;             /* engine on sys.path */
    int rc;
    volatile bool done;
};
extern PyInterpreterState *volatile g_interp;   /* set while Python is initialised */
bool start_python(struct job *j, pthread_t *th);

/* threads.c */
size_t wait_threads(int ms);
void close_threads(void);
extern size_t g_threads_stuck;   /* still running at exit: see main() */

#endif
