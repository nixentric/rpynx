/* Ren'Py Switch Loader - Milestone 1/2 launcher.
 *
 * Text-console UI (libnx console) for the library listing, controller
 * remapping, controller test and diagnostics. Every menu is driven by the
 * logical actions from the mapping engine, so the loader's own UI follows the
 * user's controller mapping. The graphical (SDL) library UI is Milestone 7. */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "cJSON.h"
#include "fs.h"
#include "input.h"
#include "input_config.h"
#include "input_switch.h"
#include "log.h"
#include "procinfo.h"

#ifndef RSL_VERSION
#define RSL_VERSION "dev"
#endif

#define ROOT       "sdmc:/switch/renpy-loader"
#define GAMES_DIR  ROOT "/games"
#define CTRL_DIR   ROOT "/config/controller"
#define GLOBAL_CFG CTRL_DIR "/global.json"
#define RUNTIME_NRO ROOT "/runtimes/renpy-runtime.nro"
#define MAX_GAMES  64
#define CAPTURE_MS 5000
#define SCREEN_W   1280.0f
#define SCREEN_H   720.0f

#define C_RESET  "\x1b[0m"
#define C_SEL    "\x1b[36m"
#define C_BAD    "\x1b[31m"
#define C_GOOD   "\x1b[32m"
#define C_DIM    "\x1b[37m"

typedef enum { SCR_MAIN, SCR_LIBRARY, SCR_REMAP, SCR_TEST, SCR_DIAG } screen_id;

enum { ROW_DEADZONE = RSL_ACT_COUNT, ROW_SPEED, ROW_ACCEL, ROW_DELAY, ROW_INTERVAL,
       ROW_STICKS, ROW_CURSOR_VIS, ROW_COUNT };

static const char *const main_items[] = {
    "Game Library", "Controller Settings", "Controller Test", "Diagnostics", "Exit to Homebrew Menu",
};
#define MAIN_COUNT (int)(sizeof main_items / sizeof main_items[0])

static struct {
    screen_id screen;
    int sel;
    bool quit;
    char status[512];             /* the applet-mode refusal is ~260 chars; 160 cut "hold R" off (run 27) */
    rsl_input in;
    rsl_raw_input raw;
    PadState pad;
    uint32_t pad_sig;
    volatile bool reset_pending;
    uint32_t now;
    rsl_load_result global_load;
    bool sd_ok;
    char self_path[256];           /* this .nro, to come back to after a game */

    char games[MAX_GAMES][64];
    bool game_ok[MAX_GAMES];
    struct game_info {
        char renpy[24];            /* Ren'Py version from manifest.json, "" if none */
        char state[16];            /* experimental / unsupported; "" without manifest */
        char runtime[49];          /* runtime the game needs */
        char nro[160];             /* where that runtime would be */
        char problem[160];         /* why it must not start, "" if it may */
        int issues;                /* performance.issues from the importer */
        char issue[2][160];        /* the first two of them */
        bool recompile, runtime_ok;
    } info[MAX_GAMES];
    int game_count;

    rsl_input_config edit;
    char edit_path[256];
    char edit_label[80];
    int edit_preset;
    bool edit_dirty, discard_armed;
    int capture;                   /* action being captured, -1 if none */
    uint32_t capture_deadline;

    char evlog[8][40];
    int evcount;
    uint32_t back_since;
    bool back_held;
    bool dirty;                    /* redraw on the next frame */
    uint32_t last_draw;
} g;

static uint32_t now_ms(void)
{
    return (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}

static void status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof g.status, fmt, ap);
    va_end(ap);
    g.dirty = true;
    rsl_log("%s", g.status);
}

/* "A, NavUp" */
static const char *mask_str(uint32_t mask, char *buf, size_t len)
{
    buf[0] = '\0';
    for (int b = 0; b < RSL_BTN_COUNT; b++)
        if (mask & (1u << b)) {
            size_t n = strlen(buf);
            snprintf(buf + n, len - n, "%s%s", n ? ", " : "", rsl_button_names[b]);
        }
    return buf[0] ? buf : "-";
}

/* Label of the live button for a UI action, for the help lines. */
static const char *key(rsl_action a)
{
    uint32_t m = g.in.cfg.bindings[a];
    for (int b = 0; b < RSL_BTN_COUNT; b++)
        if (m & (1u << b))
            return rsl_button_names[b];
    return "?";
}

static const char *applet_type_str(void)
{
    switch (appletGetAppletType()) {
    case AppletType_Application:
    case AppletType_SystemApplication:
        return "Application (full memory)";
    case AppletType_LibraryApplet:
        return "Library applet (limited memory)";
    default:
        return "Other applet";
    }
}

static bool full_memory(void)
{
    AppletType t = appletGetAppletType();
    return t == AppletType_Application || t == AppletType_SystemApplication;
}

static const char *mode_str(void)
{
    return appletGetOperationMode() == AppletOperationMode_Console ? "Docked" : "Handheld";
}

static void on_applet(AppletHookType hook, void *param)
{
    (void)param;
    switch (hook) {
    case AppletHookType_OnFocusState:
        rsl_log("focus state %d", (int)appletGetFocusState());
        g.reset_pending = true;
        break;
    case AppletHookType_OnResume:
        rsl_log("resumed from sleep");
        g.reset_pending = true;
        break;
    case AppletHookType_OnOperationMode:
        rsl_log("operation mode: %s", mode_str());
        break;
    default:
        break;
    }
}

static void go(screen_id s)
{
    g.dirty = true;
    g.screen = s;
    g.sel = 0;
    g.capture = -1;
    g.status[0] = '\0';
}

/* ------------------------------------------------------------------ library */

/* What a game needs, from the manifest.json the importer writes
 * (docs/IMPORTER.md). A game copied by hand has none and gets the default
 * runtime. Fills gi->problem when the game must not start. */
static void read_manifest(const char *game, struct game_info *gi)
{
    char path[256], err[128];
    struct stat st;
    memset(gi, 0, sizeof *gi);
    strcpy(gi->runtime, "renpy-8.5.3");
    snprintf(path, sizeof path, GAMES_DIR "/%s/manifest.json", game);
    char *text = rsl_read_file(path, 1 << 20, NULL, err, sizeof err);
    if (text) {
        cJSON *m = cJSON_Parse(text);
        free(text);
        const char *format = cJSON_GetStringValue(cJSON_GetObjectItem(m, "format"));
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(m, "status"));
        const char *runtime = cJSON_GetStringValue(cJSON_GetObjectItem(m, "runtime"));
        const char *version = cJSON_GetStringValue(cJSON_GetObjectItem(m, "renpy_version"));
        const char *reason = cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(m, "reasons"), 0));
        snprintf(gi->renpy, sizeof gi->renpy, "%s", version ? version : "?");
        snprintf(gi->state, sizeof gi->state, "%s", state ? state : "?");
        gi->recompile = cJSON_IsTrue(cJSON_GetObjectItem(m, "recompile"));
        cJSON *issues = cJSON_GetObjectItem(cJSON_GetObjectItem(m, "performance"), "issues");
        gi->issues = cJSON_GetArraySize(issues);
        for (int i = 0; i < 2 && i < gi->issues; i++) {
            const char *text = cJSON_GetStringValue(cJSON_GetArrayItem(issues, i));
            snprintf(gi->issue[i], sizeof gi->issue[i], "%s", text ? text : "?");
        }
        gi->runtime[0] = '\0';
        if (!format || strcmp(format, "renpy-switch-loader/manifest") != 0)
            snprintf(gi->problem, sizeof gi->problem, "manifest.json is unreadable; export the game again.");
        else if (!state || strcmp(state, "unsupported") == 0 || !runtime)
            snprintf(gi->problem, sizeof gi->problem, "cannot run here: %.130s", reason ? reason : "unsupported");
        else if (!runtime[0] || strlen(runtime) >= sizeof gi->runtime ||
                 strspn(runtime, "abcdefghijklmnopqrstuvwxyz0123456789.-") != strlen(runtime))
            snprintf(gi->problem, sizeof gi->problem, "bad runtime name in manifest.json.");
        else
            strcpy(gi->runtime, runtime);
        cJSON_Delete(m);
    }
    if (!gi->runtime[0])
        return;
    char runtime[sizeof gi->runtime], nro[sizeof gi->nro];
    strcpy(runtime, gi->runtime);
    snprintf(nro, sizeof nro, ROOT "/runtimes/%s.nro", runtime);
    /* renpy-8.5.3 is the runtime installed as renpy-runtime.nro. */
    if (stat(nro, &st) != 0 && strcmp(runtime, "renpy-8.5.3") == 0)
        snprintf(nro, sizeof nro, "%s", RUNTIME_NRO);
    strcpy(gi->nro, nro);
    gi->runtime_ok = stat(nro, &st) == 0;
    if (!gi->runtime_ok)
        snprintf(gi->problem, sizeof gi->problem, "needs runtime %s; copy it to %.90s", runtime, nro);
}

static void scan_games(void)
{
    g.game_count = 0;
    DIR *d = opendir(GAMES_DIR);
    if (!d) {
        status("Cannot open %s", GAMES_DIR);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) && g.game_count < MAX_GAMES) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof g.games[0])
            continue;
        char p[512];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", GAMES_DIR, e->d_name);
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        strcpy(g.games[g.game_count], e->d_name);
        snprintf(p, sizeof p, "%s/%s/game", GAMES_DIR, e->d_name);
        g.game_ok[g.game_count] = stat(p, &st) == 0 && S_ISDIR(st.st_mode);
        read_manifest(e->d_name, &g.info[g.game_count]);
        g.game_count++;
    }
    closedir(d);
    rsl_log("library scan: %d folder(s)", g.game_count);
}

/* -------------------------------------------------------------- remapping */

static void open_remap(const char *path, const char *label, bool fallback_to_global)
{
    char err[256];
    go(SCR_REMAP);
    snprintf(g.edit_path, sizeof g.edit_path, "%s", path);
    snprintf(g.edit_label, sizeof g.edit_label, "%s", label);
    struct stat st;
    bool exists = stat(path, &st) == 0;
    rsl_load_result r = rsl_config_load(path, &g.edit, err, sizeof err);
    if (r == RSL_LOAD_DEFAULTS && fallback_to_global)
        g.edit = g.in.cfg;
    if (r == RSL_LOAD_BACKUP)
        status("Main file unreadable, loaded backup. %s", err);
    else if (r == RSL_LOAD_DEFAULTS && exists)
        status(C_BAD "Unreadable, starting from %s: %s" C_RESET,
               fallback_to_global ? "global controls" : "defaults", err);
    g.edit_preset = 0;
    g.edit_dirty = g.discard_armed = false;
}

static void save_edit(void)
{
    char err[256], dir[256];
    snprintf(dir, sizeof dir, "%s", g.edit_path);
    *strrchr(dir, '/') = '\0';
    if (!rsl_mkdirs(dir)) {
        status(C_BAD "Cannot create %s (SD card missing or read-only?)" C_RESET, dir);
        return;
    }
    if (!rsl_config_save(g.edit_path, &g.edit, err, sizeof err)) {
        status(C_BAD "Not saved: %s" C_RESET, err);
        return;
    }
    g.edit_dirty = false;
    if (strcmp(g.edit_path, GLOBAL_CFG) == 0) {
        /* Apply live. Held buttons (the save button itself) must be released
         * before they count under the new mapping. */
        rsl_input_init(&g.in, &g.edit);
        rsl_input_reset(&g.in, g.raw.buttons);
        status(C_GOOD "Saved and applied." C_RESET);
    } else {
        status(C_GOOD "Saved per-game profile." C_RESET);
    }
}

static void adjust_row(int row, int dir)
{
    rsl_input_config *c = &g.edit;
    switch (row) {
    case ROW_DEADZONE: c->deadzone += 0.01f * dir; break;
    case ROW_SPEED: c->cursor_speed += 0.05f * dir; break;
    case ROW_ACCEL: c->cursor_accel += 0.25f * dir; break;
    case ROW_DELAY: c->repeat_delay_ms = (uint32_t)((int)c->repeat_delay_ms + 25 * dir); break;
    case ROW_INTERVAL: c->repeat_interval_ms = (uint32_t)((int)c->repeat_interval_ms + 10 * dir); break;
    case ROW_STICKS: {
        rsl_stick t = c->nav_stick;
        c->nav_stick = c->cursor_stick;
        c->cursor_stick = t;
        break;
    }
    case ROW_CURSOR_VIS: c->cursor_visible = !c->cursor_visible; break;
    default: return;
    }
    rsl_config_sanitize(c);
    g.edit_dirty = true;
}

static void remap_capture_tick(void)
{
    if (g.capture < 0)
        return;
    if ((int32_t)(g.now - g.capture_deadline) >= 0) {
        g.capture = -1;
        status("Assignment cancelled (no button pressed).");
        return;
    }
    uint32_t p = g.in.pressed_buttons;
    if (!p)
        return;
    int b = __builtin_ctz(p);
    uint32_t clash = rsl_config_bind(&g.edit, (rsl_action)g.capture, (rsl_button)b);
    g.edit_dirty = true;
    if (clash) {
        char names[96] = "";
        for (int a = 0; a < RSL_ACT_COUNT; a++)
            if (clash & (1u << a)) {
                size_t n = strlen(names);
                snprintf(names + n, sizeof names - n, "%s%s", n ? ", " : "", rsl_action_names[a]);
            }
        status(C_BAD "%s -> %s conflicts with: %s" C_RESET, rsl_action_names[g.capture],
               rsl_button_names[b], names);
    } else {
        status("%s -> %s", rsl_action_names[g.capture], rsl_button_names[b]);
    }
    g.capture = -1;
    /* the captured button is still down: do not let it act as a UI key */
    rsl_input_reset(&g.in, g.in.buttons);
}

static void remap_action(rsl_action a)
{
    switch (a) {
    case RSL_ACT_CONFIRM:
        if (g.sel < RSL_ACT_COUNT) {
            g.capture = g.sel;
            g.capture_deadline = g.now + CAPTURE_MS;
            snprintf(g.status, sizeof g.status, "Press the new button for \"%s\" ...",
                     rsl_action_names[g.sel]);
        } else {
            adjust_row(g.sel, 1);
        }
        break;
    case RSL_ACT_LEFT: adjust_row(g.sel, -1); break;
    case RSL_ACT_RIGHT: adjust_row(g.sel, 1); break;
    case RSL_ACT_HISTORY:
        g.edit_preset = (g.edit_preset + 1) % RSL_PRESET_COUNT;
        rsl_config_preset(&g.edit, (rsl_preset)g.edit_preset);
        g.edit_dirty = true;
        status("Preset: %s", rsl_preset_names[g.edit_preset]);
        break;
    case RSL_ACT_HIDE_UI:
        rsl_config_preset(&g.edit, RSL_PRESET_DEFAULT);
        g.edit_preset = 0;
        g.edit_dirty = true;
        status("Reset to default (not saved yet).");
        break;
    case RSL_ACT_GAME_MENU:
        save_edit();
        break;
    case RSL_ACT_BACK:
        if (g.edit_dirty && !g.discard_armed) {
            g.discard_armed = true;
            status("Unsaved changes. Press %s again to discard.", key(RSL_ACT_BACK));
        } else {
            go(SCR_MAIN);
        }
        return;
    default:
        return;
    }
    g.discard_armed = false;
}

static void draw_remap(void)
{
    char buf[96];
    const rsl_input_config *c = &g.edit;
    uint32_t clash = rsl_config_conflicts(c);
    printf("Controller Settings - %s%s\n", g.edit_label, g.edit_dirty ? " (unsaved)" : "");
    printf(C_DIM "%s\n\n" C_RESET, g.edit_path);
    for (int r = 0; r < ROW_COUNT; r++) {
        printf("%s%s ", r == g.sel ? C_SEL : "", r == g.sel ? ">" : " ");
        if (r < RSL_ACT_COUNT) {
            bool bad = clash & (1u << r);
            printf("%-12s %s%s%s\n" C_RESET, rsl_action_names[r], bad ? C_BAD : "",
                   g.capture == r ? "[press a button]" : mask_str(c->bindings[r], buf, sizeof buf),
                   bad ? "  (conflict)" : "");
            continue;
        }
        switch (r) {
        case ROW_DEADZONE: printf("%-20s %.2f\n", "Stick deadzone", c->deadzone); break;
        case ROW_SPEED: printf("%-20s %.2f screens/s\n", "Cursor speed", c->cursor_speed); break;
        case ROW_ACCEL: printf("%-20s %.2f\n", "Cursor acceleration", c->cursor_accel); break;
        case ROW_DELAY: printf("%-20s %u ms\n", "Repeat delay", (unsigned)c->repeat_delay_ms); break;
        case ROW_INTERVAL: printf("%-20s %u ms\n", "Repeat interval", (unsigned)c->repeat_interval_ms); break;
        case ROW_STICKS:
            printf("%-20s nav=%s cursor=%s\n", "Sticks", c->nav_stick ? "right" : "left",
                   c->cursor_stick ? "right" : "left");
            break;
        case ROW_CURSOR_VIS: printf("%-20s %s\n", "Cursor visible", c->cursor_visible ? "yes" : "no"); break;
        }
        printf(C_RESET);
    }
    printf("\n[%s] assign/toggle  [%s/%s] adjust  [%s] next preset (%s)\n", key(RSL_ACT_CONFIRM),
           key(RSL_ACT_LEFT), key(RSL_ACT_RIGHT), key(RSL_ACT_HISTORY), rsl_preset_names[g.edit_preset]);
    printf("[%s] reset to default  [%s] save  [%s] back\n", key(RSL_ACT_HIDE_UI),
           key(RSL_ACT_GAME_MENU), key(RSL_ACT_BACK));
    printf(C_DIM "Emergency: hold + and - for 3 s anywhere to restore default controls.\n" C_RESET);
}

/* ----------------------------------------------------------------- screens */

static void draw_main(void)
{
    printf("Ren'Py Switch Loader " RSL_VERSION "\n\n");
    for (int i = 0; i < MAIN_COUNT; i++)
        printf("%s %s %s\n" C_RESET, i == g.sel ? C_SEL : "", i == g.sel ? ">" : " ", main_items[i]);
    printf("\n[%s] select   %s / %s\n", key(RSL_ACT_CONFIRM), mode_str(), applet_type_str());
    if (!g.sd_ok)
        printf(C_BAD "\nSD card folder %s is not writable. Settings and logs will not be saved.\n" C_RESET, ROOT);
    if (g.global_load == RSL_LOAD_BACKUP)
        printf(C_BAD "\nController config was damaged; loaded the backup copy.\n" C_RESET);
}

static void draw_library(void)
{
    printf("Game Library  (%s)\n\n", GAMES_DIR);
    if (g.game_count == 0)
        printf("No game folders found. See docs/INSTALL.md.\n");
    else
        printf("  %-26s %-9s %-13s %s\n", "Game", "Ren'Py", "Status", "Runtime");
    bool any_recompiled = false;
    for (int i = 0; i < g.game_count; i++) {
        const struct game_info *gi = &g.info[i];
        char version[16];
        snprintf(version, sizeof version, "%.10s%s", gi->renpy[0] ? gi->renpy : "-", gi->recompile ? "*" : "");
        any_recompiled |= gi->recompile;
        printf("%s%s %-26.26s %-9.9s ", i == g.sel ? C_SEL : "", i == g.sel ? ">" : " ", g.games[i], version);
        if (!g.game_ok[i])
            printf(C_BAD "no game/ folder" C_RESET "\n");
        else if (!gi->runtime[0])
            printf(C_BAD "%-13.13s none" C_RESET "\n", gi->state[0] ? gi->state : "bad manifest");
        else
            printf("%-13.13s %-14.14s %s" C_RESET "%s\n", gi->state[0] ? gi->state : "no manifest", gi->runtime,
                   gi->runtime_ok ? C_GOOD "installed" : C_BAD "MISSING", gi->issues ? C_BAD " !" C_RESET : "");
    }
    if (any_recompiled)
        printf("\n* Ren'Py 7 game recompiled from its .rpy sources for Ren'Py 8 (experimental).\n");
    if (g.game_count && g.info[g.sel].issues) {
        /* Not optimised for the Switch: heavier, hotter, maybe choppy (docs/IMPORTER.md). */
        const struct game_info *gi = &g.info[g.sel];
        printf("\n" C_BAD "! %s: %d performance issue(s) from the importer:" C_RESET "\n",
               g.games[g.sel], gi->issues);
        for (int i = 0; i < 2 && i < gi->issues; i++)
            printf("  - %.150s\n", gi->issue[i]);
    }
    printf("\n[%s] launch  [%s] per-game controls  [%s] back\n", key(RSL_ACT_CONFIRM),
           key(RSL_ACT_HIDE_UI), key(RSL_ACT_BACK));
}

static void draw_test(void)
{
    char buf[160];
    const rsl_raw_input *r = &g.raw;
    float lx, ly, rx, ry;
    rsl_stick_normalize(r->stick_x[0], r->stick_y[0], g.in.cfg.deadzone, &lx, &ly);
    rsl_stick_normalize(r->stick_x[1], r->stick_y[1], g.in.cfg.deadzone, &rx, &ry);
    printf("Controller Test  (hold %s for 1 s to leave)\n\n", key(RSL_ACT_BACK));
    printf("Controller:   %s, style 0x%x, %s\n", padIsConnected(&g.pad) ? "connected" : C_BAD "none" C_RESET,
           (unsigned)padGetStyleSet(&g.pad), mode_str());
    printf("Physical:     %s\n", mask_str(r->buttons, buf, sizeof buf));
    printf("Effective:    %s\n", mask_str(g.in.buttons, buf, sizeof buf));
    printf("Left stick:   %6d %6d  -> %5.2f %5.2f\n", (int)r->stick_x[0], (int)r->stick_y[0], lx, ly);
    printf("Right stick:  %6d %6d  -> %5.2f %5.2f\n", (int)r->stick_x[1], (int)r->stick_y[1], rx, ry);
    if (r->touching)
        printf("Touch:        %.0f, %.0f\n", r->touch_x, r->touch_y);
    else
        printf("Touch:        -\n");
    buf[0] = '\0';
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        if (rsl_action_active(&g.in, (rsl_action)a)) {
            size_t n = strlen(buf);
            snprintf(buf + n, sizeof buf - n, "%s ", rsl_action_names[a]);
        }
    printf("Actions:      %s\n", buf[0] ? buf : "-");
    printf("Cursor:       %.3f, %.3f  (%4.0f, %4.0f px)%s\n", g.in.cursor_x, g.in.cursor_y,
           g.in.cursor_x * SCREEN_W, g.in.cursor_y * SCREEN_H, g.in.cursor_moved ? "  moving" : "");
    printf("\nRecent events:\n");
    for (int i = 0; i < g.evcount; i++)
        printf("  %s\n", g.evlog[i]);
}

static void draw_diag(void)
{
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    printf("Diagnostics\n\n");
    printf("Version:      %s\n", RSL_VERSION);
    printf("Applet type:  %s\n", applet_type_str());
    if (appletGetAppletType() != AppletType_Application)
        printf(C_BAD "              Games will need full memory: launch via title override\n"
               "              (hold R while starting an installed game).\n" C_RESET);
    printf("Display:      %s\n", mode_str());
    printf("Memory:       %llu / %llu MiB used\n", (unsigned long long)(used >> 20),
           (unsigned long long)(total >> 20));
    printf("SD folder:    %s %s\n", ROOT, g.sd_ok ? C_GOOD "ok" C_RESET : C_BAD "not writable" C_RESET);
    printf("Controls:     %s (%s)\n", GLOBAL_CFG,
           g.global_load == RSL_LOAD_OK ? "loaded" :
           g.global_load == RSL_LOAD_BACKUP ? "loaded from backup" : "defaults");
    struct stat st;
    printf("Runtime:      %s %s\n", RUNTIME_NRO,
           stat(RUNTIME_NRO, &st) == 0 ? C_GOOD "found" C_RESET : C_BAD "missing" C_RESET);
    printf("\nRecent log (%s/logs/loader.log):\n", ROOT);
    for (int i = 0; rsl_log_recent(i); i++)
        printf("  %.76s\n", rsl_log_recent(i));
    printf("\n[%s] run Python self-test  [%s] back\n", key(RSL_ACT_CONFIRM), key(RSL_ACT_BACK));
}

/* Exit to `nro` (via the Homebrew loader) and have it bring us back after. */
static void chain_load(const char *nro, const char *args)
{
    struct stat st;
    char argv[1024];
    if (!envHasNextLoad()) {
        status(C_BAD "Cannot start other programs: launch the loader from the Homebrew Menu." C_RESET);
        return;
    }
    if (stat(nro, &st) != 0) {
        status(C_BAD "Runtime not installed: %s" C_RESET, nro);
        return;
    }
    /* libnx splits on spaces and strips "..." around an argument. */
    snprintf(argv, sizeof argv, "\"%s\" --return \"%s\" %s", nro, g.self_path, args);
    Result rc = envSetNextLoad(nro, argv);
    if (R_FAILED(rc)) {
        status(C_BAD "envSetNextLoad failed: 0x%x" C_RESET, (unsigned)rc);
        return;
    }
    rsl_log("chain-loading: %s", argv);
    g.quit = true;
}

static void on_action(rsl_action a)
{
    switch (g.screen) {
    case SCR_MAIN:
        if (a == RSL_ACT_UP) g.sel = (g.sel + MAIN_COUNT - 1) % MAIN_COUNT;
        if (a == RSL_ACT_DOWN) g.sel = (g.sel + 1) % MAIN_COUNT;
        if (a != RSL_ACT_CONFIRM) break;
        switch (g.sel) {
        case 0: go(SCR_LIBRARY); scan_games(); break;
        case 1: open_remap(GLOBAL_CFG, "global", false); break;
        case 2: go(SCR_TEST); g.evcount = 0; break;
        case 3: go(SCR_DIAG); break;
        case 4: g.quit = true; break;
        }
        break;
    case SCR_LIBRARY:
        if (a == RSL_ACT_BACK) { go(SCR_MAIN); break; }
        if (!g.game_count) break;
        if (a == RSL_ACT_UP) g.sel = (g.sel + g.game_count - 1) % g.game_count;
        if (a == RSL_ACT_DOWN) g.sel = (g.sel + 1) % g.game_count;
        if (a == RSL_ACT_CONFIRM) {
            char args[512];
            if (!g.game_ok[g.sel])
                status(C_BAD "%s has no game/ folder; copy the whole game folder." C_RESET, g.games[g.sel]);
            else if (strchr(g.games[g.sel], '"'))
                status(C_BAD "Rename %s: folder names cannot contain quotes." C_RESET, g.games[g.sel]);
            else if (!full_memory()) {
                /* Applet mode (the loader opened from the Album): a Ren'Py game took the
                 * whole console down with a fatal error, twice (hardware runs 8 and 26),
                 * also with a 64 MB image cache. Games do not start here, no override. */
                u64 total = 0;
                svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
                rsl_log("%s: not started, applet mode (%llu MiB)", g.games[g.sel], (unsigned long long)(total >> 20));
                status(C_BAD "Games cannot start in applet mode (the loader was opened from the Album,\n"
                       "with only %llu MiB of memory): they crash the whole console. Close the\n"
                       "loader, then hold R while opening any game to start it with full memory." C_RESET,
                       (unsigned long long)(total >> 20));
            } else if (g.info[g.sel].problem[0]) {
                status(C_BAD "%s: %s" C_RESET, g.games[g.sel], g.info[g.sel].problem);
            } else {
                rsl_log("%s: runtime %s (%s)", g.games[g.sel], g.info[g.sel].runtime, g.info[g.sel].nro);
                snprintf(args, sizeof args, "--game \"" GAMES_DIR "/%s\"", g.games[g.sel]);
                chain_load(g.info[g.sel].nro, args);
            }
        }
        if (a == RSL_ACT_HIDE_UI) {
            char path[256], label[80];
            snprintf(path, sizeof path, CTRL_DIR "/games/%s.json", g.games[g.sel]);
            snprintf(label, sizeof label, "game: %.60s", g.games[g.sel]);
            open_remap(path, label, true);
        }
        break;
    case SCR_REMAP:
        if (g.capture >= 0) break;
        if (a == RSL_ACT_UP) g.sel = (g.sel + ROW_COUNT - 1) % ROW_COUNT;
        else if (a == RSL_ACT_DOWN) g.sel = (g.sel + 1) % ROW_COUNT;
        else remap_action(a);
        break;
    case SCR_DIAG:
        if (a == RSL_ACT_BACK) go(SCR_MAIN);
        if (a == RSL_ACT_CONFIRM) chain_load(RUNTIME_NRO, "\"romfs:/selftest.py\"");
        break;
    case SCR_TEST:
        break;
    }
}

static void log_event(const rsl_event *e)
{
    static const char *const kind[] = { "down  ", "up    ", "repeat", "EMERGENCY" };
    if (g.evcount == 8) {
        memmove(g.evlog[0], g.evlog[1], sizeof g.evlog[0] * 7);
        g.evcount = 7;
    }
    snprintf(g.evlog[g.evcount++], sizeof g.evlog[0], "%s %s", kind[e->type], rsl_action_names[e->action]);
}

static void frame(float dt)
{
    rsl_switch_input_read(&g.pad, &g.raw);

    uint32_t sig = rsl_switch_pad_signature(&g.pad);
    if (sig != g.pad_sig) {
        rsl_log("controller change: style 0x%x -> 0x%x", (unsigned)g.pad_sig, (unsigned)sig);
        g.pad_sig = sig;
        g.reset_pending = true;
    }
    if (g.reset_pending) {
        g.reset_pending = false;
        rsl_input_reset(&g.in, g.raw.buttons);
    }

    rsl_input_update(&g.in, &g.raw, g.now, dt);
    if (g.screen == SCR_TEST && g.raw.touching) {
        g.in.cursor_x = g.raw.touch_x / SCREEN_W;
        g.in.cursor_y = g.raw.touch_y / SCREEN_H;
    }
    /* Before the event loop: a capture started by this frame's confirm press
     * must not capture that same press. A completed capture clears this
     * frame's events, so the captured button does not also act as a UI key. */
    if (g.screen == SCR_REMAP)
        remap_capture_tick();

    for (int i = 0; i < g.in.event_count; i++) {
        const rsl_event *e = &g.in.events[i];
        if (g.screen == SCR_TEST)
            log_event(e);
        if (e->type == RSL_EV_EMERGENCY) {
            rsl_log("emergency combo: default controls restored");
            open_remap(GLOBAL_CFG, "global", false);
            g.edit = g.in.cfg;
            g.edit_dirty = true;
            status(C_BAD "Emergency reset: default controls are active. Press + to save them." C_RESET);
        } else if (e->type != RSL_EV_ACTION_UP && g.capture < 0) {
            on_action(e->action);
        }
    }
    if (g.screen == SCR_TEST) {
        if (!rsl_action_active(&g.in, RSL_ACT_BACK))
            g.back_held = false;
        else if (!g.back_held)
            g.back_held = true, g.back_since = g.now;
        else if (g.now - g.back_since >= 1000)
            go(SCR_MAIN), g.back_held = false;
    }

    /* The libnx console renders text on the CPU. Redraw only when something
     * can have changed: input, a button capture, the live test screen, or once
     * a second (diagnostics). Otherwise just keep polling input at ~60 Hz. */
    bool redraw = g.dirty || g.in.event_count > 0 || g.in.pressed_buttons || g.capture >= 0 ||
                  g.screen == SCR_TEST || g.now - g.last_draw >= 1000;
    if (!redraw) {
        svcSleepThread(16000000ULL);
        return;
    }
    g.dirty = false;
    g.last_draw = g.now;

    consoleClear();
    switch (g.screen) {
    case SCR_MAIN: draw_main(); break;
    case SCR_LIBRARY: draw_library(); break;
    case SCR_REMAP: draw_remap(); break;
    case SCR_TEST: draw_test(); break;
    case SCR_DIAG: draw_diag(); break;
    }
    if (g.status[0])
        printf("\n%s" C_RESET "\n", g.status);
    consoleUpdate(NULL);
}

int main(int argc, char **argv)
{
    /* argv[0] is our own path when started from the Homebrew Menu. */
    snprintf(g.self_path, sizeof g.self_path, "%s",
             argc > 0 && strncmp(argv[0], "sdmc:/", 6) == 0 ? argv[0] : ROOT "/renpy-loader.nro");
    consoleInit(NULL);

    g.sd_ok = rsl_mkdirs(ROOT "/logs") && rsl_mkdirs(CTRL_DIR "/games") && rsl_mkdirs(GAMES_DIR);
    rsl_log_open(ROOT "/logs/loader.log");
    rsl_log("Ren'Py Switch Loader %s, %s, %s", RSL_VERSION, applet_type_str(), mode_str());
    /* Same process as the game that may have just quit: report and repair
     * what it left in the heap before another game starts (hardware run 7). */
    rsl_heap_audit("loader start");

    rsl_input_config cfg;
    char err[256];
    g.global_load = rsl_config_load(GLOBAL_CFG, &cfg, err, sizeof err);
    rsl_log("controller config: %s%s%s",
            g.global_load == RSL_LOAD_OK ? "loaded" : g.global_load == RSL_LOAD_BACKUP ? "backup" : "defaults",
            err[0] ? " - " : "", err);
    rsl_input_init(&g.in, &cfg);
    rsl_switch_input_init(&g.pad);
    g.capture = -1;

    AppletHookCookie hook;
    appletHook(&hook, on_applet, NULL);

    uint32_t last = now_ms();
    while (!g.quit && appletMainLoop()) {
        g.now = now_ms();
        float dt = (float)(g.now - last) / 1000.0f;
        last = g.now;
        frame(dt > 0.1f ? 0.1f : dt);
    }

    rsl_log("exiting normally");
    appletUnhook(&hook);
    rsl_log_close();
    consoleExit(NULL);
    return 0;
}
