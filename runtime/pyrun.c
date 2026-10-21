/* Running CPython: on its own thread with a big stack, stdout/stderr reattached,
 * the game's threads given time to finish, then SDL shut down on that thread. */
#include "runtime.h"

#include <stdio.h>

#include <SDL2/SDL.h>

#include "log.h"

#define STDLIB_ZIP  L"romfs:/lib/python312.zip"
/* Ren'Py recurses deeply (displayables, pickling); the loader-provided main
 * thread stack is small, so Python runs on its own thread. */
#define PY_STACK    (8 * 1024 * 1024)
#define THREAD_WAIT_MS 1000   /* for the game's threads to finish before Python shuts down */

PyInterpreterState *volatile g_interp;

static int exit_code_of(PyObject *exc)
{
    PyObject *code = PyObject_GetAttrString(exc, "code");
    int rc = 1;
    if (code == Py_None)
        rc = 0;
    else if (code && PyLong_Check(code))
        rc = (int)PyLong_AsLong(code);
    Py_XDECREF(code);
    PyErr_Clear();
    return rc;
}

/* CPython checks stdout/stderr with fstat(), which libnx's console device does
 * not implement, so it leaves sys.stdout/sys.stderr as None and every print()
 * and traceback is silently dropped. Reattach them: to RSL_PYLOG (a file) if
 * set, otherwise to fds 1/2 (the console; FileIO tolerates the fstat error). */
static const char STDIO_SETUP[] =
    "import io, os, sys\n"
    "def _rsl_fd(fd):\n"
    "    return io.TextIOWrapper(io.FileIO(fd, 'w', closefd=False), encoding='utf-8',\n"
    "                            errors='backslashreplace', line_buffering=True)\n"
    "_rsl_log = os.environ.get('RSL_PYLOG')\n"
    "if _rsl_log:\n"
    "    sys.stdout = sys.stderr = open(_rsl_log, 'a', encoding='utf-8', errors='backslashreplace', buffering=1)\n"
    "else:\n"
    "    if sys.stdout is None: sys.stdout = _rsl_fd(1)\n"
    "    if sys.stderr is None: sys.stderr = _rsl_fd(2)\n"
    "del _rsl_fd, _rsl_log\n";

/* Returns the script's exit status, or -1 if Python itself failed. */
static int run_python(struct job *j)
{
    PyPreConfig pre;
    PyPreConfig_InitIsolatedConfig(&pre);
    pre.utf8_mode = 1;             /* as Ren'Py's own launchers do */
    PyStatus st = Py_PreInitialize(&pre);

    PyConfig c;
    PyConfig_InitIsolatedConfig(&c);
    c.site_import = 0;
    c.write_bytecode = 0;
    c.buffered_stdio = 0;
    c.install_signal_handlers = 0;
    c.parse_argv = 0;
    c.module_search_paths_set = 1;
    if (!PyStatus_Exception(st) && j->renpy)
        st = PyWideStringList_Append(&c.module_search_paths, L"" RENPY_BASE);
    if (!PyStatus_Exception(st))
        st = PyWideStringList_Append(&c.module_search_paths, STDLIB_ZIP);
    if (!PyStatus_Exception(st))
        st = PyConfig_SetString(&c, &c.home, L"romfs:/lib");
    if (!PyStatus_Exception(st))
        st = PyConfig_SetBytesArgv(&c, j->argc, j->argv);
    if (!PyStatus_Exception(st))
        st = Py_InitializeFromConfig(&c);
    PyConfig_Clear(&c);
    if (PyStatus_Exception(st)) {
        rsl_log("Python init failed: %s: %s", st.func ? st.func : "?", st.err_msg ? st.err_msg : "?");
        printf("Python init failed: %s: %s\n", st.func ? st.func : "?", st.err_msg ? st.err_msg : "?");
        return -1;
    }
    rsl_log("Python %s initialised", Py_GetVersion());
    g_interp = PyInterpreterState_Get();
    if (PyRun_SimpleString(STDIO_SETUP) != 0)
        rsl_log("could not attach Python stdout/stderr");

    int rc = 0;
    FILE *f = fopen(j->script, "rb");
    if (!f) {
        rsl_log("cannot open %s", j->script);
        printf("cannot open %s\n", j->script);
        rc = -1;
    } else {
        PyObject *globals = PyModule_GetDict(PyImport_AddModule("__main__"));
        PyObject *file = PyUnicode_FromString(j->script);
        PyDict_SetItemString(globals, "__file__", file);
        Py_XDECREF(file);
        PyObject *r = PyRun_FileExFlags(f, j->script, Py_file_input, globals, globals, 1, NULL);
        if (r) {
            Py_DECREF(r);
        } else if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            /* PyErr_Print would call exit() on SystemExit; take the code instead. */
            PyObject *exc = PyErr_GetRaisedException();
            rc = exit_code_of(exc);
            Py_DECREF(exc);
        } else {
            PyErr_Print();   /* stderr: the console in script mode */
            rc = 1;
        }
    }
    /* The game's threads may still need Python once more: a music decode thread frees
     * its file through Python after Ren'Py's audio.quit(). One that came after
     * Py_FinalizeEx stayed stuck, and its stack crashed the next game (hardware run 22).
     * So let them finish first, with the GIL released. */
    u64 t0 = armGetSystemTick();
    size_t left;
    Py_BEGIN_ALLOW_THREADS
    left = wait_threads(THREAD_WAIT_MS);
    Py_END_ALLOW_THREADS
    rsl_log("before Python shutdown: %zu thread(s) still running after %llu ms", left,
            (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ULL));
    g_interp = NULL;
    if (Py_FinalizeEx() < 0)
        rsl_log("Py_FinalizeEx reported an error");
    return rc;
}

/* Shut SDL down on the thread that initialised it (Ren'Py's): from the main
 * thread SDL_Quit() hung forever after the game quit (hardware runs 5-6).
 * Each subsystem is logged so a remaining hang names its culprit. */
static void quit_sdl(void)
{
    static const struct { Uint32 flag; const char *name; } subs[] = {
        { SDL_INIT_AUDIO, "audio" }, { SDL_INIT_GAMECONTROLLER, "gamecontroller" },
        { SDL_INIT_JOYSTICK, "joystick" }, { SDL_INIT_HAPTIC, "haptic" },
        { SDL_INIT_SENSOR, "sensor" }, { SDL_INIT_VIDEO, "video" },
        { SDL_INIT_TIMER, "timer" }, { SDL_INIT_EVENTS, "events" },
    };
    /* Ren'Py's set_timer starts SDL's timer thread through SDL_AddTimer, which
     * does not count the timer subsystem as initialised, so SDL_Quit() left the
     * thread running into the loader; it crashed there (hardware run 9). Taking
     * a reference makes the loop below quit the timer, which joins the thread. */
    SDL_InitSubSystem(SDL_INIT_TIMER);
    Uint32 init = SDL_WasInit(SDL_INIT_EVERYTHING);
    rsl_log("SDL shutdown: subsystems 0x%x", (unsigned)init);
    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++)
        if (init & subs[i].flag) {
            rsl_log("SDL shutdown: %s", subs[i].name);
            SDL_QuitSubSystem(subs[i].flag);
        }
    SDL_Quit();
    rsl_log("SDL shutdown: done");
}

static void *python_thread(void *arg)
{
    struct job *j = arg;
    j->rc = run_python(j);
    if (j->renpy)
        quit_sdl();
    j->done = true;
    return NULL;
}

bool start_python(struct job *j, pthread_t *th)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PY_STACK);
    bool ok = pthread_create(th, &attr, python_thread, j) == 0;
    pthread_attr_destroy(&attr);
    if (!ok)
        rsl_log("could not start the Python thread");
    return ok;
}
