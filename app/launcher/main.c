/* rpynx - the loader .nro (Milestone 7).
 *
 * Start-up, the display and the main loop. The screens are app/ui (LVGL),
 * drawn on the CPU into the libnx framebuffer, and only when something
 * changed (docs/LOADER_UI.md). Input and navigation are actions.c, the
 * library library.c, controller settings controls.c (see launcher.h). Every
 * key goes through the mapping engine, so the loader follows the user's
 * controller mapping. */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "lvgl.h"

#include "fs.h"
#include "input.h"
#include "input_config.h"
#include "input_switch.h"
#include "launcher.h"
#include "log.h"
#include "procinfo.h"
#include "ui.h"

#define FRAME_MS     16       /* input poll; nothing is drawn unless something changed */
#define SUMMARY_MS   60000
#define SLOW_FRAME_US 33000

loader_state g;               /* shared with app/ui */
struct launcher m;
static Framebuffer fb;

/* "ui" lines in loader.log (docs/LOADER_UI.md, "Light on the Switch"). */
static struct {
    uint64_t start;
    uint32_t flushes, area, last_summary, warned_at[3], lvgl_lines;
    uint32_t frames, full_frames, frames_total;
    uint64_t render_us, render_max_us;
    rsl_telemetry telemetry;
} stats;

static const char *const screen_name[] = {
    "library", "details", "controller settings", "controller test", "diagnostics", "how to play",
    "layout", "picture",
};

static uint32_t now_ms(void)
{
    return (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}

/* At most one line of each kind a minute: rsl_log() writes every line to
 * the SD card. */
static bool may_warn(int kind)
{
    if (stats.warned_at[kind] && g.now - stats.warned_at[kind] < SUMMARY_MS)
        return false;
    stats.warned_at[kind] = g.now | 1;
    return true;
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
        m.reset_pending = true;
        break;
    case AppletHookType_OnResume:
        rsl_log("resumed from sleep");
        m.reset_pending = true;
        break;
    case AppletHookType_OnOperationMode:
        g.mode = mode_str();
        rsl_log("operation mode: %s", g.mode);
        break;
    default:
        break;
    }
}

/* ----------------------------------------------------------------- display */

/* LVGL draws straight into the framebuffer's linear buffer (direct mode);
 * presenting swizzles it into the next framebuffer. Only frames with
 * something new get here, so an idle loader presents nothing. */
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)px;
    stats.area += lv_area_get_size(a);
    if (lv_display_flush_is_last(d)) {
        framebufferBegin(&fb, NULL);
        framebufferEnd(&fb);
        stats.flushes++;
    }
    lv_display_flush_ready(d);
}

static void lvgl_log(lv_log_level_t level, const char *msg)
{
    (void)level;
    /* TJpgDec reads past the last row of blocks of every JPEG; the picture is
     * complete (docs/LOADER_UI.md). Logged, it would cost a line per cover. */
    if (strstr(msg, "jd_restart error") || strstr(msg, "jd_mcu_load error"))
        return;
    if (stats.lvgl_lines < 20 || may_warn(2)) {
        stats.lvgl_lines++;
        rsl_log("ui lvgl: %.*s", (int)strcspn(msg, "\n"), msg);
    }
}

static uint32_t tick_ms(void)
{
    return now_ms();
}

static lv_display_t *display_init(void)
{
    Result rc = framebufferCreate(&fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_BGRA_8888, 2);
    if (R_FAILED(rc) || R_FAILED(rc = framebufferMakeLinear(&fb))) {
        rsl_log("framebuffer: 0x%x", (unsigned)rc);
        return NULL;
    }
    lv_display_t *d = lv_display_create(1280, 720);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers_with_stride(d, fb.buf_linear, NULL, fb.stride * 720, fb.stride,
                                       LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    return d;
}

/* The touchscreen as an LVGL pointer: dragging scrolls lists and rows, a
 * tap reaches the item or hint under it (ui_tap, actions.c). On release
 * LVGL keeps the last point by itself. */
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = g.raw.touching ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (g.raw.touching) {
        data->point.x = (int32_t)g.raw.touch_x;
        data->point.y = (int32_t)g.raw.touch_y;
    }
}

static unsigned heap_kib(bool peak)
{
    struct mallinfo mi = mallinfo();
    return (unsigned)((peak ? mi.arena : mi.uordblks) >> 10);   /* newlib never gives arena back */
}

/* After a frame that drew something: count it, and warn about the bug
 * classes that would keep the console busy (slow frames, redrawing while
 * nothing happens). */
static void frame_drawn(uint64_t us)
{
    stats.frames++;
    stats.frames_total++;
    stats.render_us += us;
    if (us > stats.render_max_us)
        stats.render_max_us = us;
    if (stats.area >= 1280 * 720)
        stats.full_frames++;
    if (us > SLOW_FRAME_US && may_warn(0))
        rsl_log("ui warning: a frame took %llu ms on the %s screen (%u px redrawn)",
                (unsigned long long)(us / 1000), screen_name[g.screen], (unsigned)stats.area);
    bool static_screen = g.screen == SCR_LIBRARY || g.screen == SCR_DETAIL || g.screen == SCR_HELP;
    if (static_screen && g.now - m.last_input > 2000 && ui_idle() && may_warn(1))
        rsl_log("ui warning: redrawing while idle on the %s screen (%u px)", screen_name[g.screen],
                (unsigned)stats.area);
}

static void summary(void)
{
    char t[200];
    int decoded, failed;
    rsl_telemetry_line(&stats.telemetry, t, sizeof t);
    ui_cover_counts(&decoded, &failed);
    rsl_log("ui 60 s: %u frames, render avg %.1f ms max %.1f ms, %u full-screen; %s; heap %u KiB in use, "
            "%u KiB peak; covers %d decoded, %d failed", (unsigned)stats.frames,
            stats.frames ? (double)stats.render_us / stats.frames / 1000.0 : 0.0, (double)stats.render_max_us / 1000.0,
            (unsigned)stats.full_frames, t, heap_kib(false), heap_kib(true), decoded, failed);
    stats.frames = stats.full_frames = 0;
    stats.render_us = stats.render_max_us = 0;
    stats.last_summary = g.now;
}

/* Milliseconds since the last mark, for the start-up stages below: run 28
 * started in 80 ms with 4 games and 237 ms with 5, which no part of the code
 * explains yet, so the line says where the time goes. */
static uint32_t stage_ms(void)
{
    static uint32_t mark;
    uint32_t now = (uint32_t)(armTicksToNs(armGetSystemTick() - stats.start) / 1000000ULL), d = now - mark;
    mark = now;
    return d;
}

int main(int argc, char **argv)
{
    stats.start = armGetSystemTick();
    /* argv[0] is our own path when started from the Homebrew Menu. */
    snprintf(m.self_path, sizeof m.self_path, "%s",
             argc > 0 && strncmp(argv[0], "sdmc:/", 6) == 0 ? argv[0] : ROOT "/renpy-loader.nro");

    g.sd_ok = rsl_mkdirs(ROOT "/logs") && rsl_mkdirs(CTRL_DIR "/games") && rsl_mkdirs(GAMES_DIR);
    rsl_log_open(ROOT "/logs/loader.log");
    rsl_log("rpynx %s, %s, %s", RSL_VERSION, applet_type_str(), mode_str());
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
    rsl_switch_input_init(&m.pad);
    tsInitialize();
    psmInitialize();
    plInitialize(PlServiceType_User);

    uint32_t d_cfg = stage_ms();

    struct stat st;
    u64 total = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    g.mem_total = total;
    g.full_memory = full_memory();
    g.applet_type = applet_type_str();
    g.mode = mode_str();
    g.pad_desc = pad_desc();
    g.pad_connected = padIsConnected(&m.pad);
    g.runtime_found = stat(RUNTIME_NRO, &st) == 0;
    g.capture = -1;
    load_settings();
    uint32_t d_set = stage_ms();
    scan_games();
    uint32_t d_scan = stage_ms();
    go(m.help_seen ? SCR_LIBRARY : SCR_HELP);

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_log_register_print_cb(lvgl_log);
    if (!display_init()) {
        rsl_log("no display: exiting");
        m.quit = true;
    } else {
        lv_indev_t *touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, touch_read);
    }
    /* The console's own fonts, from shared memory: the text font, and the
     * button icons the HOME menu uses. Nothing of them is in this .nro. */
    uint32_t d_disp = stage_ms();
    PlFontData text = { 0 }, icons = { 0 };
    bool have_text = R_SUCCEEDED(plGetSharedFontByType(&text, PlSharedFontType_Standard));
    bool have_icons = R_SUCCEEDED(plGetSharedFontByType(&icons, PlSharedFontType_NintendoExt));
    if (!m.quit)
        ui_init(have_text ? text.address : NULL, text.size, have_icons ? icons.address : NULL, icons.size);
    rsl_log("ui start stages: services %u, settings %u, scan %u (%d game(s)), display %u, fonts and ui %u ms",
            (unsigned)d_cfg, (unsigned)d_set, (unsigned)d_scan, g.game_count, (unsigned)d_disp,
            (unsigned)stage_ms());
    rsl_telemetry_line(&stats.telemetry, err, sizeof err);   /* the first call only sets the baseline */

    AppletHookCookie hook;
    appletHook(&hook, on_applet, NULL);

    uint32_t last = now_ms(), mem_at = 0;
    stats.last_summary = m.last_input = last;
    while (!m.quit && appletMainLoop()) {
        g.now = now_ms();
        float dt = (float)(g.now - last) / 1000.0f;
        last = g.now;
        frame(dt > 0.1f ? 0.1f : dt);
        if (g.now - mem_at >= 1000) {   /* diagnostics shows it; once a second is enough */
            u64 used = 0;
            svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
            g.mem_used = used;
            mem_at = g.now;
        }
        ui_update();
        uint32_t d_build = stats.frames_total ? 0 : stage_ms();   /* the first screen's objects */

        uint32_t flushes = stats.flushes;
        uint64_t t0 = armGetSystemTick();
        stats.area = 0;
        lv_timer_handler();
        if (stats.flushes != flushes) {
            if (!stats.frames_total)
                rsl_log("ui start: first frame %llu ms after main(), %s, heap %u KiB in use"
                        " (build %u ms, draw %u ms)",
                        (unsigned long long)(armTicksToNs(armGetSystemTick() - stats.start) / 1000000ULL),
                        g.applet_type, heap_kib(false), (unsigned)d_build, (unsigned)stage_ms());
            frame_drawn(armTicksToNs(armGetSystemTick() - t0) / 1000ULL);
        }
        if (g.now - stats.last_summary >= SUMMARY_MS)
            summary();

        uint32_t spent = now_ms() - g.now;
        if (spent < FRAME_MS)
            svcSleepThread((u64)(FRAME_MS - spent) * 1000000ULL);
    }

    int decoded, failed;
    ui_cover_counts(&decoded, &failed);
    rsl_log("ui exit: %u frames drawn, heap %u KiB in use, %u KiB peak, covers %d decoded, %d failed",
            (unsigned)stats.frames_total, heap_kib(false), heap_kib(true), decoded, failed);
    rsl_log("exiting normally");
    appletUnhook(&hook);
    framebufferClose(&fb);
    plExit();
    psmExit();
    tsExit();
    rsl_log_close();
    return 0;
}
