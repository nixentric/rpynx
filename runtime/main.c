/* Ren'Py Switch runtime: the process that hosts the embedded CPython.
 *
 * The loader chain-loads this .nro per game, so every game gets a fresh
 * process (Mesa/nouveau cannot be re-initialised inside one process).
 * Milestone 3: runs a Python script (default: the bundled self-test) and
 * shows its output. Milestone 4 adds running a Ren'Py game.
 *
 * usage: renpy-runtime.nro [--return loader.nro] [script.py]
 *   --return  chain-load this .nro on exit (set by the loader) */
/* Python.h first: it sets feature macros that system headers depend on. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "fs.h"
#include "log.h"

#define ROOT        "sdmc:/switch/renpy-loader"
#define STDLIB_ZIP  L"romfs:/lib/python312.zip"
#define SELFTEST    "romfs:/selftest.py"
/* Ren'Py recurses deeply (displayables, pickling); the loader-provided main
 * thread stack is small, so Python runs on its own thread. */
#define PY_STACK    (8 * 1024 * 1024)

/* ---- _rslnx: Switch facts for Python code (diagnostics, self-test) ---- */

static PyObject *nx_memory(PyObject *self, PyObject *unused)
{
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    return Py_BuildValue("KK", (unsigned long long)used, (unsigned long long)total);
}

static PyObject *nx_applet_type(PyObject *self, PyObject *unused)
{
    return PyLong_FromLong(appletGetAppletType());
}

static PyMethodDef nx_methods[] = {
    { "memory", nx_memory, METH_NOARGS, "(used, total) process memory in bytes" },
    { "applet_type", nx_applet_type, METH_NOARGS, "libnx AppletType (0 = application)" },
    { NULL, NULL, 0, NULL },
};

static struct PyModuleDef nx_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_rslnx",
    .m_size = -1,
    .m_methods = nx_methods,
};

static PyObject *PyInit__rslnx(void)
{
    return PyModule_Create(&nx_module);
}

/* ---- running a script ---- */

static int exit_code_of(PyObject *exc)
{
    PyObject *code = PyObject_GetAttrString(exc, "code");
    int rc = 1;
    if (code == Py_None)
        rc = 0;
    else if (code && PyLong_Check(code))
        rc = (int)PyLong_AsLong(code);
    Py_XDECREF(code);
    return rc;
}

/* Returns the script's exit status, or -1 if Python itself failed. */
static int run_script(const char *script, const char *scratch)
{
    PyConfig c;
    PyConfig_InitIsolatedConfig(&c);
    c.site_import = 0;
    c.write_bytecode = 0;
    c.buffered_stdio = 0;
    c.install_signal_handlers = 0;
    c.parse_argv = 0;
    c.module_search_paths_set = 1;
    char *argv[] = { (char *)script, (char *)scratch };
    PyStatus st = PyWideStringList_Append(&c.module_search_paths, STDLIB_ZIP);
    if (!PyStatus_Exception(st))
        st = PyConfig_SetString(&c, &c.home, L"romfs:/lib");
    if (!PyStatus_Exception(st))
        st = PyConfig_SetBytesArgv(&c, 2, argv);
    if (!PyStatus_Exception(st))
        st = Py_InitializeFromConfig(&c);
    PyConfig_Clear(&c);
    if (PyStatus_Exception(st)) {
        rsl_log("Python init failed: %s: %s", st.func ? st.func : "?", st.err_msg ? st.err_msg : "?");
        printf("Python init failed: %s: %s\n", st.func ? st.func : "?", st.err_msg ? st.err_msg : "?");
        return -1;
    }
    rsl_log("Python %s initialised", Py_GetVersion());

    int rc = 0;
    FILE *f = fopen(script, "rb");
    if (!f) {
        rsl_log("cannot open %s", script);
        printf("cannot open %s\n", script);
        rc = -1;
    } else {
        PyObject *globals = PyModule_GetDict(PyImport_AddModule("__main__"));
        PyObject *file = PyUnicode_FromString(script);
        PyDict_SetItemString(globals, "__file__", file);
        Py_XDECREF(file);
        PyObject *r = PyRun_FileExFlags(f, script, Py_file_input, globals, globals, 1, NULL);
        if (r) {
            Py_DECREF(r);
        } else if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            /* PyErr_Print would call exit() on SystemExit; take the code instead. */
            PyObject *exc = PyErr_GetRaisedException();
            rc = exit_code_of(exc);
            Py_DECREF(exc);
        } else {
            PyErr_Print();
            rc = 1;
        }
    }
    if (Py_FinalizeEx() < 0)
        rsl_log("Py_FinalizeEx reported an error");
    return rc;
}

struct job { const char *script, *scratch; int rc; volatile bool done; };

static void *python_thread(void *arg)
{
    struct job *j = arg;
    j->rc = run_script(j->script, j->scratch);
    j->done = true;
    return NULL;
}

int main(int argc, char **argv)
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    rsl_mkdirs(ROOT "/logs");
    rsl_mkdirs(ROOT "/cache");
    rsl_log_open(ROOT "/logs/runtime.log");
    Result rc = romfsInit();
    if (R_FAILED(rc))
        rsl_log("romfsInit failed: 0x%x", (unsigned)rc);
    if (PyImport_AppendInittab("_rslnx", PyInit__rslnx) < 0)
        rsl_log("could not register _rslnx");

    const char *ret = NULL;
    int i = 1;
    if (argc > 2 && strcmp(argv[1], "--return") == 0) {
        ret = argv[2];
        i = 3;
    }
    struct job job = { argc > i ? argv[i] : SELFTEST, ROOT "/cache", -1, false };
    rsl_log("runtime start: %s, applet type %d", job.script, (int)appletGetAppletType());
    printf("Ren'Py Switch runtime: %s\n\n", job.script);
    consoleUpdate(NULL);

    pthread_attr_t attr;
    pthread_t th;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PY_STACK);
    if (pthread_create(&th, &attr, python_thread, &job) == 0) {
        /* Keep servicing applet messages (HOME, sleep) and presenting the
         * console while Python runs. ponytail: the console is not thread-safe;
         * worst case is a torn frame of text. Fine for a text self-test. */
        while (!job.done && appletMainLoop()) {
            consoleUpdate(NULL);
            svcSleepThread(16000000);
        }
        pthread_join(th, NULL);
    } else {
        printf("could not start the Python thread\n");
    }
    pthread_attr_destroy(&attr);

    rsl_log("script finished with status %d", job.rc);
    printf("\nFinished with status %d. Press + to exit.\n", job.rc);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    if (ret && envHasNextLoad()) {
        Result r = envSetNextLoad(ret, ret);
        rsl_log("return to %s: 0x%x", ret, (unsigned)r);
    }
    romfsExit();
    rsl_log_close();
    consoleExit(NULL);
    return job.rc == 0 ? 0 : 1;
}
