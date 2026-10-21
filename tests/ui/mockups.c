/* Renders every loader screen with sample data into BMP files, using the real
 * app/ui code and LVGL's software renderer on the host. scripts/mockups.sh
 * builds it, makes sample covers and converts the BMPs to PNG.
 *   mockups FONT.ttf OUT_DIR COVER_DIR
 * Also the regression test for "redraws every frame" (docs/LOADER_UI.md): on
 * every screen, once it has settled, frames with nothing changed must draw
 * nothing. And that a tap on the touchscreen reaches the item or footer hint
 * under it. Exits 1 if a check fails. */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "loader.h"
#include "log.h"
#include "ui.h"

loader_state g;

static uint32_t fb[1280 * 720];
static uint32_t ms;
static int flushes, failures;

static uint32_t tick(void) { return ms; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a, (void)px;
    if (lv_display_flush_is_last(d))
        flushes++;
    lv_display_flush_ready(d);
}

static void lvgl_log(lv_log_level_t level, const char *msg)
{
    (void)level;
    fputs(msg, stderr);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> 8 * i);
}

static void frames(int n)
{
    for (int i = 0; i < n; i++) {
        g.now = ms;
        ui_update();
        lv_timer_handler();
        ms += 16;
    }
}

/* A finger on the touchscreen, for the tap check. */
static bool finger;
static lv_point_t finger_at;

static void touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = finger_at;
    data->state = finger ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Tap at x, y; fails the run unless ui_tap() then reports action a on item. */
static void tap(int x, int y, int a, int item)
{
    int got_item, got;
    finger_at = (lv_point_t){ x, y };
    finger = true;
    frames(4);
    finger = false;
    frames(4);
    got = ui_tap(&got_item);
    if (got != a || got_item != item) {
        fprintf(stderr, "tap at %d,%d: action %d item %d, expected %d %d\n", x, y, got, got_item, a, item);
        failures++;
    }
}

/* Run about a second of frames so fades, focus transitions, scrolling and
 * the one-cover-per-frame decoding all finish. */
static void settle(void)
{
    frames(70);
    lv_refr_now(NULL);
}

/* XRGB8888 is B,G,R,X in memory: exactly a 32-bit BMP row. Before saving:
 * the redraw check, then a timed full redraw (host CPU, not Switch timing). */
static void shot(const char *dir, const char *name)
{
    int before = flushes;
    frames(10);
    if (flushes != before) {
        fprintf(stderr, "FAIL %s: %d frames drawn with nothing changed\n", name, flushes - before);
        failures++;
    }
    struct timespec t0, t1;
    lv_obj_invalidate(lv_screen_active());
    clock_gettime(CLOCK_MONOTONIC, &t0);
    lv_refr_now(NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "host-only full redraw  %-28s %5.1f ms\n", name,
            (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6);

    char path[512];
    uint8_t h[54] = { 'B', 'M' };
    snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    put32(h + 2, 54 + sizeof fb);
    put32(h + 10, 54);
    put32(h + 14, 40);
    put32(h + 18, 1280);
    put32(h + 22, 720);
    h[26] = 1;
    h[28] = 32;
    put32(h + 34, sizeof fb);
    fwrite(h, 1, sizeof h, f);
    for (int y = 719; y >= 0; y--)
        fwrite(fb + y * 1280, 4, 1280, f);
    fclose(f);
    printf("%s\n", path);
}

static int add(const char *id, const char *title, const char *renpy, const char *runtime)
{
    int i = g.game_count++;
    struct game_info *gi = &g.info[i];
    snprintf(g.games[i], sizeof g.games[i], "%s", id);
    g.game_ok[i] = true;
    snprintf(gi->title, sizeof gi->title, "%s", title);
    snprintf(gi->renpy, sizeof gi->renpy, "%s", renpy);
    snprintf(gi->state, sizeof gi->state, "%s", renpy[0] ? "experimental" : "");
    snprintf(gi->runtime, sizeof gi->runtime, "%s", runtime);
    gi->runtime_ok = true;
    return i;
}

static void cover(int i, const char *dir, const char *name)
{
    snprintf(g.info[i].cover, sizeof g.info[i].cover, "%s/%s.jpg", dir, name);
}

static void backdrop(int i, const char *dir, const char *name)
{
    snprintf(g.info[i].backdrop, sizeof g.info[i].backdrop, "%s/bd-%s.jpg", dir, name);
}

static void event(const char *text)
{
    snprintf(g.evlog[g.evcount++], sizeof g.evlog[0], "%s", text);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s FONT.ttf OUT_DIR COVER_DIR\n", argv[0]);
        return 2;
    }
    const char *out = argv[2], *covers = argv[3];
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    void *ttf = malloc((size_t)len);
    if (!ttf || fread(ttf, 1, (size_t)len, f) != (size_t)len)
        return 1;
    fclose(f);

    lv_init();
    lv_tick_set_cb(tick);
    lv_log_register_print_cb(lvgl_log);
    lv_display_t *d = lv_display_create(1280, 720);
    lv_display_set_flush_cb(d, flush);
    lv_display_set_buffers(d, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, touch);

    rsl_input_config cfg;
    rsl_config_preset(&cfg, RSL_PRESET_DEFAULT);
    rsl_input_init(&g.in, &cfg);
    g.capture = -1;
    g.accent = 0xff5fa8;
    g.accent_hue = 330;
    g.sd_ok = g.full_memory = g.pad_connected = g.runtime_found = g.backdrops = true;
    g.global_load = RSL_LOAD_OK;
    g.applet_type = "Application (full memory)";
    g.mode = "Handheld";
    g.pad_desc = "Handheld (Joy-Con attached)";
    g.mem_total = 3232ull << 20;
    g.mem_used = 46ull << 20;

    int i;
    cover(add("the-question", "The Question", "8.5.3.26051504", "renpy-8.5.3"), covers, "the_question-main_menu");
    cover(add("lighthouse-letters", "Lighthouse Letters", "8.3.4", "renpy-8.5.3"), covers, "custom");
    cover(add("moonlit-cafe", "Moonlit Caf\xc3\xa9", "8.5.3", "renpy-8.5.3"), covers, "the_question-bgclub");
    add("natsu-no-owari", "\xe5\xa4\x8f\xe3\x81\xae\xe7\xb5\x82\xe3\x82\x8f\xe3\x82\x8a\xe3\x81\xab", "8.2.1",
        "renpy-8.5.3");
    i = add("starfall-academy", "Starfall Academy", "8.5.3", "renpy-8.5.3");
    cover(i, covers, "the_question-bglecturehall");
    g.info[i].issues = 4;
    snprintf(g.info[i].issue[0], sizeof g.info[i].issue[0],
             "gui/splash.webm: 1920x1080 at 60 fps; the Switch decodes video on its CPU");
    snprintf(g.info[i].issue[1], sizeof g.info[i].issue[1],
             "images/bg/sky.png: 5120x2880, above the GPU's 4096x4096 texture limit");
    snprintf(g.info[i].issue[2], sizeof g.info[i].issue[2],
             "archive.rpa: locked archive, its contents were not checked");
    i = add("echoes-of-autumn", "Echoes of Autumn", "7.4.11", "renpy-8.5.3");
    cover(i, covers, "tutorial-bgcave");
    g.info[i].recompile = true;
    i = add("clockwork-garden", "Clockwork Garden", "7.3.5", "renpy-7.3.5");
    g.info[i].runtime_ok = false;
    snprintf(g.info[i].problem, sizeof g.info[i].problem,
             "needs runtime renpy-7.3.5; copy it to " ROOT "/runtimes/renpy-7.3.5.nro");
    add("my-hand-copied-game", "", "", "renpy-8.5.3");
    i = add("broken-folder", "", "", "renpy-8.5.3");
    g.game_ok[i] = false;
    cover(add("winter-tides", "Winter Tides", "8.4.0", "renpy-8.5.3"), covers, "tutorial-bgwashington");
    cover(add("tutorial", "Ren'Py Tutorial", "8.5.3.26051504", "renpy-8.5.3"), covers, "tutorial-main_menu");
    add("neon-rain", "Neon Rain Detective", "8.1.3", "renpy-8.5.3");

    static const char *const art[] = { "the_question-main_menu", "the_question-bguni", NULL,
                                       NULL, "the_question-bglecturehall", "tutorial-bgcave", NULL, NULL,
                                       NULL, "tutorial-bgwashington", "tutorial-main_menu" };
    for (int k = 0; k < (int)(sizeof art / sizeof art[0]); k++)
        if (art[k])
            backdrop(k, covers, art[k]);

    rsl_log("Ren'Py Switch Loader 0.1.0, Application (full memory), Handheld");
    rsl_log("heap audit (loader start): 0 leaked blocks");
    rsl_log("controller config: loaded");
    rsl_log("library scan: 12 folder(s)");
    rsl_log("the-question: runtime renpy-8.5.3 (" RUNTIME_NRO ")");
    rsl_log("chain-loading: \"" RUNTIME_NRO "\" --return ...");

    ui_init(ttf, (size_t)len, NULL, 0);

    g.screen = SCR_LIBRARY;
    settle();
    shot(out, "1a-library-row");
    g.sel = 6;
    settle();
    shot(out, "1b-library-row-moved");
    g.sel = 0;
    g.layout = LAYOUT_GRID;
    settle();
    shot(out, "1c-library-grid");
    g.layout = LAYOUT_ROW;
    g.accent = 0x25d0f2;
    settle();
    shot(out, "1d-library-row-blue");
    g.accent = 0xff5fa8;
    g.layout = LAYOUT_CINEMA;
    settle();
    shot(out, "1e-library-cinematic");
    g.sel = 4;
    settle();
    shot(out, "1f-library-cinematic-moved");
    g.sel = 2;   /* a cover but no menu art: a glow in the cover's colour */
    settle();
    shot(out, "1g-library-cinematic-no-art");
    g.sel = 0;
    g.layout = LAYOUT_ROW;
    g.menu_open = true;
    g.menu_sel = MENU_LAYOUT;
    settle();
    shot(out, "2-menu");
    g.menu_open = false;
    g.menu_sel = 0;

    g.screen = SCR_DETAIL;
    g.game = 4;
    settle();
    shot(out, "3-detail");

    g.screen = SCR_LIBRARY;
    settle();
    g.screen = SCR_DETAIL;
    g.game = 5;   /* recompiled, and with issues: longer than the room above the buttons */
    g.info[5].issues = g.info[4].issues;
    memcpy(g.info[5].issue, g.info[4].issue, sizeof g.info[5].issue);
    settle();
    shot(out, "3b-detail-long");
    ui_scroll(1);
    settle();
    shot(out, "3c-detail-long-scrolled");
    g.info[5].issues = 0;

    g.screen = SCR_LIBRARY;
    settle();
    g.screen = SCR_DETAIL;
    g.game = 4;
    g.backdrops = false;   /* Layout page: background off */
    settle();
    shot(out, "3d-detail-no-background");
    g.backdrops = true;
    tap(1112, 611, RSL_ACT_CONFIRM, DETAIL_LAUNCH);   /* the Launch button */
    tap(1200, 688, RSL_ACT_BACK, -1);                 /* the "B Back" hint */

    g.screen = SCR_LIBRARY;
    settle();
    g.screen = SCR_DETAIL;
    g.game = 6;
    snprintf(g.status, sizeof g.status, "Clockwork Garden: %s", g.info[6].problem);
    g.status_kind = ST_BAD;
    settle();
    shot(out, "4-detail-problem");
    g.status[0] = '\0';

    g.screen = SCR_LIBRARY;
    settle();
    g.screen = SCR_DETAIL;
    g.game = 0;
    g.full_memory = false;
    g.applet_type = "Library applet (limited memory)";
    g.mem_total = 442ull << 20;
    g.dialog_open = true;
    settle();
    shot(out, "5-applet-notice");
    g.dialog_open = false;
    g.full_memory = true;
    g.applet_type = "Application (full memory)";
    g.mem_total = 3232ull << 20;

    g.screen = SCR_REMAP;
    g.sel = 3;
    g.edit = g.in.cfg;
    snprintf(g.edit_label, sizeof g.edit_label, "Global");
    rsl_config_bind(&g.edit, RSL_ACT_HISTORY, RSL_BTN_X);
    g.edit_dirty = true;
    snprintf(g.status, sizeof g.status, "History \xe2\x86\x92 X: also used by Hide dialogue");
    g.status_kind = ST_BAD;
    settle();
    shot(out, "6-controller-settings");
    g.status[0] = '\0';

    g.screen = SCR_TEST;
    g.sel = 0;
    g.raw.buttons = 1u << RSL_BTN_A | 1u << RSL_BTN_ZR;
    g.raw.stick_x[0] = 19000;
    g.raw.stick_y[0] = 9000;
    rsl_input_update(&g.in, &g.raw, ms, 0.016f);
    event("down   up");
    event("repeat up");
    event("up     up");
    event("down   confirm");
    event("down   skip");
    settle();
    shot(out, "7-controller-test");
    memset(&g.raw, 0, sizeof g.raw);
    rsl_input_reset(&g.in, 0);

    g.screen = SCR_DIAG;
    settle();
    shot(out, "8-diagnostics");

    g.screen = SCR_HELP;
    settle();
    shot(out, "10a-how-to-play");
    g.sel = 3;
    settle();
    shot(out, "10b-how-to-play-scrolled");
    g.sel = 0;

    g.screen = SCR_LAYOUT;
    g.sel = 0;
    settle();
    shot(out, "11-layout");
    g.sel = SET_PRESET + 2;
    g.accent = 0xa78bfa;
    settle();
    shot(out, "12a-layout-colour");
    g.sel = SET_CUSTOM;
    g.accent_hue = 20;
    g.accent = ui_hue_color(20);
    settle();
    shot(out, "12b-layout-custom-near-red");
    g.sel = 0;
    g.accent = 0xff5fa8;
    g.accent_hue = 330;

    g.screen = SCR_LIBRARY;
    g.game_count = 0;
    g.sd_ok = false;
    g.full_memory = false;
    settle();
    shot(out, "9-library-empty");
    if (failures)
        fprintf(stderr, "%d check(s) failed: redraws, taps\n", failures);
    return failures != 0;
}
