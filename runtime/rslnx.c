/* _rslnx: Switch facts and controller input for the Python bridge
 * (runtime/renpy/boot/rsl_bridge.py), and the controller state behind it. */
#include "runtime.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "input.h"
#include "input_config.h"
#include "input_switch.h"
#include "log.h"

/* ---- controller state shared with the Python bridge ---- */

static rsl_input g_in;
static PadState g_pad;
static uint32_t g_pad_sig, g_last_ms;
static volatile bool g_input_reset;

static uint32_t now_ms(void)
{
    return (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}

void on_applet(AppletHookType hook, void *param)
{
    (void)param;
    if (hook == AppletHookType_OnFocusState || hook == AppletHookType_OnResume) {
        rsl_log("applet event %d: input reset", (int)hook);
        g_input_reset = true;
    }
}

#define GLOBAL_CONTROLS ROOT "/config/controller/global.json"
static char g_controls_path[512] = GLOBAL_CONTROLS;   /* the profile this game loaded */

void load_controls(const char *game_id)
{
    char *path = g_controls_path, err[256];
    struct stat st;
    snprintf(path, sizeof g_controls_path, ROOT "/config/controller/games/%s.json", game_id);
    if (stat(path, &st) != 0)
        snprintf(path, sizeof g_controls_path, GLOBAL_CONTROLS);
    rsl_input_config cfg;
    rsl_load_result r = rsl_config_load(path, &cfg, err, sizeof err);
    rsl_log("controls %s: %s%s%s", path,
            r == RSL_LOAD_OK ? "loaded" : r == RSL_LOAD_BACKUP ? "loaded backup" : "defaults",
            err[0] ? " - " : "", err);
    rsl_input_init(&g_in, &cfg);
    rsl_switch_input_init(&g_pad);
    g_last_ms = now_ms();
}

/* ---- _rslnx ---- */

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

/* The emergency combo in a game also saves the defaults, to this game's profile and to the
 * loader's global.json: a reset that lasted only until the loader came back left the same
 * unusable mapping there (user report, run 26). rsl_config_save keeps each old file as .bak. */
static void save_reset_controls(const rsl_input_config *cfg)
{
    const char *paths[2] = { g_controls_path, strcmp(g_controls_path, GLOBAL_CONTROLS) ? GLOBAL_CONTROLS : NULL };
    for (int i = 0; i < 2 && paths[i]; i++) {
        char err[256] = "";
        bool ok = rsl_config_save(paths[i], cfg, err, sizeof err);
        rsl_log("emergency reset saved to %s: %s%s", paths[i], ok ? "ok" : "failed - ", ok ? "" : err);
    }
}

/* One runtime.log line per press (not per repeat): the action and the physical button(s)
 * that fired it, so a remap can be checked in a game from the log alone. */
static void log_input_events(const rsl_input *in)
{
    for (int i = 0; i < in->event_count; i++) {
        const rsl_event *e = &in->events[i];
        if (e->type == RSL_EV_EMERGENCY) {
            rsl_log("input: emergency combo, default controls restored");
            save_reset_controls(&in->cfg);
        } else if (e->type == RSL_EV_ACTION_DOWN) {
            char names[96] = "";
            size_t n = 0;
            uint32_t mask = in->pressed_buttons & in->cfg.bindings[e->action];
            for (int b = 0; b < RSL_BTN_COUNT && n < sizeof names; b++)
                if (mask & (1u << b))
                    n += snprintf(names + n, sizeof names - n, "%s%s", n ? "+" : "", rsl_button_names[b]);
            rsl_log("input: %s (%s)", rsl_action_names[e->action], n ? names : "held");
        }
    }
}

/* -> ([(kind, action), ...], cursor_x, cursor_y, cursor_moved)
 * kind: 0 down, 1 up, 2 repeat, 3 emergency combo. Cursor is normalised. */
static PyObject *nx_input_poll(PyObject *self, PyObject *unused)
{
    uint32_t now = now_ms();
    float dt = (float)(now - g_last_ms) / 1000.0f;
    g_last_ms = now;
    rsl_raw_input raw;
    rsl_switch_input_read(&g_pad, &raw);
    uint32_t sig = rsl_switch_pad_signature(&g_pad);
    if (sig != g_pad_sig) {
        rsl_log("controller change: style 0x%x -> 0x%x", (unsigned)g_pad_sig, (unsigned)sig);
        g_pad_sig = sig;
        g_input_reset = true;
    }
    if (g_input_reset) {
        g_input_reset = false;
        rsl_input_reset(&g_in, raw.buttons);
    }
    rsl_input_update(&g_in, &raw, now, dt > 0.1f ? 0.1f : dt);
    log_input_events(&g_in);

    PyObject *events = PyList_New(g_in.event_count);
    if (!events)
        return NULL;
    for (int i = 0; i < g_in.event_count; i++) {
        PyObject *ev = Py_BuildValue("(is)", (int)g_in.events[i].type, rsl_action_names[g_in.events[i].action]);
        if (!ev) {
            Py_DECREF(events);
            return NULL;
        }
        PyList_SET_ITEM(events, i, ev);
    }
    return Py_BuildValue("(Nffi)", events, g_in.cursor_x, g_in.cursor_y, (int)g_in.cursor_moved);
}

static PyObject *nx_cursor_visible(PyObject *self, PyObject *unused)
{
    return PyBool_FromLong(g_in.cfg.cursor_visible);
}

/* Block until an SDL event is pending or `ms` pass, without holding the GIL. */
static PyObject *nx_wait_event(PyObject *self, PyObject *arg)
{
    long ms = PyLong_AsLong(arg);
    if (ms == -1 && PyErr_Occurred())
        return NULL;
    Py_BEGIN_ALLOW_THREADS
    SDL_WaitEventTimeout(NULL, (int)ms);
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}

/* The full Switch keyboard, a library applet with its own text box so the
 * player sees what they type, started from `initial`. SDL's inline keyboard
 * has no text box and sends nothing until Submit (hardware run 16). Blocks
 * with the GIL released; returns the text, or None if cancelled. */
static PyObject *nx_keyboard(PyObject *self, PyObject *args)
{
    const char *initial, *guide;
    int maxlen;
    if (!PyArg_ParseTuple(args, "ssi", &initial, &guide, &maxlen))
        return NULL;
    char out[1024] = { 0 };
    SwkbdConfig kbd;
    Result rc;
    Py_BEGIN_ALLOW_THREADS
    rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetInitialText(&kbd, initial);
        swkbdConfigSetInitialCursorPos(&kbd, 1);   /* end of the text */
        if (guide[0])
            swkbdConfigSetGuideText(&kbd, guide);
        if (maxlen > 0)
            swkbdConfigSetStringLenMax(&kbd, (u32)maxlen);
        rc = swkbdShow(&kbd, out, sizeof out);
        swkbdClose(&kbd);
    }
    Py_END_ALLOW_THREADS
    rsl_log("keyboard: %s (0x%x)", R_SUCCEEDED(rc) ? "OK" : "cancelled or failed", (unsigned)rc);
    if (R_FAILED(rc))
        Py_RETURN_NONE;
    return PyUnicode_DecodeUTF8(out, (Py_ssize_t)strlen(out), "replace");
}

static PyMethodDef nx_methods[] = {
    { "memory", nx_memory, METH_NOARGS, "(used, total) process memory in bytes" },
    { "applet_type", nx_applet_type, METH_NOARGS, "libnx AppletType (0 = application)" },
    { "input_poll", nx_input_poll, METH_NOARGS, "read controllers: (events, cursor_x, cursor_y, moved)" },
    { "cursor_visible", nx_cursor_visible, METH_NOARGS, "virtual cursor enabled in the mapping" },
    { "wait_event", nx_wait_event, METH_O, "wait up to ms for an SDL event (GIL released)" },
    { "keyboard", nx_keyboard, METH_VARARGS, "keyboard(initial, guide, maxlen): typed text or None" },
    { NULL, NULL, 0, NULL },
};

static struct PyModuleDef nx_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_rslnx",
    .m_size = -1,
    .m_methods = nx_methods,
};

PyObject *PyInit__rslnx(void)
{
    return PyModule_Create(&nx_module);
}
