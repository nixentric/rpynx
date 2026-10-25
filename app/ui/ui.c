/* Loader screens (Milestone 7), drawn with LVGL from the state in loader.h.
 *
 * Retained mode: a screen is built when g.screen changes, then ui_update()
 * changes only what differs from the last frame, so LVGL redraws just those
 * areas (the CPU renders everything). main.c moves the focus (g.sel) with the
 * remappable actions; here it becomes LV_STATE_FOCUSED: an accent outline
 * that grows in over 120 ms, a dark accent fill on rows and buttons, and a
 * small scale on covers. Layout and navigation rules: docs/LOADER_UI.md.
 *
 * This file: the screen frame (header, footer, status line), building
 * and updating the current screen, focus, and the public functions.
 * ui_style.c holds the fonts and styles, ui_widgets.c the building blocks
 * and touch; the screens are ui_library.c, ui_detail.c, ui_picture.c,
 * ui_controls.c and ui_pages.c, the covers and backdrops ui_art.c. */
#include "ui_internal.h"

struct ui_screen u;     /* objects of the current screen, reset when it is rebuilt */

static const hint lib_hints[] = {
    { RSL_ACT_CONFIRM, -1, "Details" }, { RSL_ACT_HIDE_UI, -1, "Game controls" },
    { RSL_ACT_GAME_MENU, -1, "Menu" },
};

static const hint empty_hints[] = { { RSL_ACT_GAME_MENU, -1, "Menu" } };

static const hint detail_hints[] = { { RSL_ACT_CONFIRM, -1, "Select" }, { RSL_ACT_BACK, -1, "Back" } };

static const hint remap_hints[] = {
    { RSL_ACT_CONFIRM, -1, "Assign" }, { RSL_ACT_LEFT, RSL_ACT_RIGHT, "Adjust" },
    { RSL_ACT_HISTORY, -1, "Preset" }, { RSL_ACT_HIDE_UI, -1, "Reset" },
    { RSL_ACT_GAME_MENU, -1, "Save" }, { RSL_ACT_BACK, -1, "Back" },
};

static const hint test_hints[] = { { RSL_ACT_BACK, -1, "Hold 1 s to leave" } };

static const hint diag_hints[] = { { RSL_ACT_CONFIRM, -1, "Run self-test" }, { RSL_ACT_BACK, -1, "Back" } };

static const hint help_hints[] = {
    { RSL_ACT_UP, RSL_ACT_DOWN, "Scroll" }, { RSL_ACT_CONFIRM, -1, "Got it" }, { RSL_ACT_BACK, -1, "Back" },
};

static const hint picture_hints[] = {
    { RSL_ACT_LEFT, RSL_ACT_RIGHT, "Screenshot" }, { RSL_ACT_ROLLBACK, RSL_ACT_ROLLFORWARD, "Move cover" },
    { RSL_ACT_CONFIRM, -1, "Use" }, { RSL_ACT_BACK, -1, "Back" },
};

static const hint layout_hints[] = {
    { RSL_ACT_LEFT, RSL_ACT_RIGHT, "Choose / hue" }, { RSL_ACT_CONFIRM, -1, "Use" }, { RSL_ACT_BACK, -1, "Back" },
};

const char *const action_label[RSL_ACT_COUNT] = {
    "Confirm / advance", "Back / cancel", "Hide dialogue", "History", "Rollback",
    "Roll forward", "Auto-forward", "Skip (hold)", "Game menu", "Quick menu",
    "Up", "Down", "Left", "Right", "Mouse click", "Loader overlay",
};

const char *const preset_label[RSL_PRESET_COUNT] = { "Default", "Left-handed", "One-handed (right)" };

const char *const layout_name[LAYOUT_COUNT] = { "Row", "Grid" };

static void focus(lv_obj_t **items, int n, int *cur, int sel, bool anim)
{
    if (sel == *cur)
        return;
    if (*cur >= 0 && *cur < n)
        lv_obj_remove_state(items[*cur], LV_STATE_FOCUSED);
    *cur = sel;
    if (sel < 0 || sel >= n)
        return;
    lv_obj_add_state(items[sel], LV_STATE_FOCUSED);
    /* Every scrolling parent: a layout card or a colour swatch sits in a
     * row that does not scroll, inside the list that does. */
    lv_anim_enable_t a = anim ? LV_ANIM_ON : LV_ANIM_OFF;
    lv_obj_scroll_to_view_recursive(items[sel], a);
    /* On the first line of a list, all the way up: the heading above shows too. */
    lv_area_t first, at;
    lv_obj_get_coords(items[0], &first);
    lv_obj_get_coords(items[sel], &at);
    for (lv_obj_t *p = lv_obj_get_parent(items[sel]); p && at.y1 == first.y1; p = lv_obj_get_parent(p))
        if (lv_obj_is_scrollable(p) && lv_obj_get_scroll_dir(p) & LV_DIR_VER) {
            lv_obj_scroll_to_y(p, 0, a);
            break;
        }
}

static void show(lv_obj_t *o, bool on, bool *shown)
{
    if (on == *shown)
        return;
    *shown = on;
    lv_obj_set_hidden(o, !on);
    if (on)
        lv_obj_fade_in(o, 120, 0);
}

static void set_translate_x(void *o, int32_t v)
{
    lv_obj_set_style_translate_x(o, v, 0);
}

/* The menu panel slides in from the right edge. */
static void slide_in(lv_obj_t *o, int32_t from)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, set_translate_x);
    lv_anim_set_values(&a, from, 0);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ----------------------------------------------------------------- screens */

lv_obj_t *screen(const char *title, const hint *h, int n)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_click_focusable(s, false);
    lv_obj_add_style(s, &st_screen, 0);
    lv_obj_set_scrollable(s, false);

    u.head = flex(s, LV_FLEX_FLOW_ROW, 16);
    lv_obj_set_height(u.head, HEADER_H);
    lv_obj_set_pos(u.head, MARGIN, 0);
    if (title)
        label(u.head, f_l, TEXT, title);
    u.chips = flex(s, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_height(u.chips, HEADER_H);
    lv_obj_align(u.chips, LV_ALIGN_TOP_RIGHT, -MARGIN, 0);
    for (int y = HEADER_H - 1; title && y <= H - FOOTER_H; y += H - FOOTER_H - HEADER_H + 1) {
        lv_obj_t *rule = shape(s, MARGIN, y, W - 2 * MARGIN, 1, LINE, LV_OPA_COVER, 0);
        (void)rule;
    }

    /* Footer: the status line (or the screen's note) left, the help right. */
    lv_obj_t *foot = flex(s, LV_FLEX_FLOW_ROW, 28);
    lv_obj_set_size(foot, W - 2 * MARGIN, FOOTER_H);
    lv_obj_set_pos(foot, MARGIN, H - FOOTER_H);
    u.status_line = label(foot, f_s, DIM, "");
    lv_obj_set_flex_grow(u.status_line, 1);
    lv_obj_set_style_max_height(u.status_line, 2 * lv_font_get_line_height(f_s), 0);
    u.hints = flex(foot, LV_FLEX_FLOW_ROW, 28);
    u.hint = h;
    u.hint_n = n;
    memcpy(u.hint_cfg, g.in.cfg.bindings, sizeof u.hint_cfg);
    hints(u.hints, h, n);
    return s;
}

static void build(void)
{
    static const char *const titles[] = {
        "Library", "Game details", "Controller settings", "Controller test", "Diagnostics", "How to play",
        "Layout", NULL,
    };
    static const struct { const hint *h; int n; } footer[] = {
        { HINTS(lib_hints) }, { HINTS(detail_hints) }, { HINTS(remap_hints) }, { HINTS(test_hints) },
        { HINTS(diag_hints) }, { HINTS(help_hints) }, { HINTS(layout_hints) }, { HINTS(picture_hints) },
    };
    bool first = !u.scr;
    lv_obj_t *s;
    if (u.screen == SCR_PICTURE && u.backdrop)   /* its pixels are freed as the page is left; */
        lv_image_set_src(u.backdrop, NULL);      /* the page still shows during the fade */
    memset(&u, 0, sizeof u);
    u.focused = u.menu_focused = u.dialog_focused = u.info_sel = -1;
    if (g.screen == SCR_LIBRARY && !g.game_count)
        s = screen(titles[0], HINTS(empty_hints));
    else
        s = screen(g.screen == SCR_LIBRARY && g.layout == LAYOUT_ROW ? NULL : titles[g.screen],
                   footer[g.screen].h, footer[g.screen].n);
    switch (g.screen) {
    case SCR_LIBRARY: build_library(s); break;
    case SCR_DETAIL: build_detail(s); break;
    case SCR_REMAP: build_remap(s); break;
    case SCR_TEST: build_test(s); break;
    case SCR_DIAG: build_diag(s); break;
    case SCR_HELP: build_help(s); break;
    case SCR_LAYOUT: build_layout(s); break;
    case SCR_PICTURE: build_picture(s); break;
    }
    for (int i = 0; i < u.count; i++)
        tappable_item(u.items[i], i);
    for (int i = 0; u.menu && i < MENU_COUNT && u.menu_items[i]; i++)
        tappable_item(u.menu_items[i], i);
    if (u.dialog_ok)
        tappable_item(u.dialog_ok, 0);
    u.scr = s;
    u.screen = g.screen;
    u.layout = g.layout;
    u.accent = g.accent;
    u.fresh = true;
    lv_screen_load_anim(s, LV_SCREEN_LOAD_ANIM_FADE_IN, first ? 0 : SCREEN_FADE_MS, 0, true);
}

/* The status line: the last message from main.c, coloured by kind, or the
 * screen's note when there is none. */
static void update_status(void)
{
    if (u.status_set && u.status_kind == g.status_kind && !strcmp(u.status, g.status))
        return;
    u.status_set = true;
    u.status_kind = g.status_kind;
    snprintf(u.status, sizeof u.status, "%s", g.status);
    lv_label_set_text(u.status_line, g.status[0] ? g.status : u.note ? u.note : "");
    lv_obj_set_style_text_color(u.status_line, lv_color_hex(!g.status[0] ? DIM : g.status_kind == ST_BAD ? RED :
                                                              g.status_kind == ST_OK ? GREEN : TEXT), 0);
    if (g.status[0])
        lv_obj_fade_in(u.status_line, 120, 0);
}

/* ------------------------------------------------------------------- public */

void ui_init(const void *ttf, size_t ttf_len, const void *icons, size_t icons_len)
{
    style_init(ttf, ttf_len, icons, icons_len);
    art_init();
}

void ui_update(void)
{
    if (u.scr && u.accent != g.accent) {
        set_accent();
        if (g.screen == SCR_LAYOUT)
            u.accent = g.accent;   /* the page follows live, no rebuild */
    }
    if (!u.scr || u.screen != g.screen || u.layout != g.layout || u.accent != g.accent)
        build();
    switch (g.screen) {
    case SCR_LIBRARY:
        /* Not on the frame the screen was built on: a cover takes 110-150 ms
         * to decode on the console, more than the rest of start-up (first
         * frame 240 ms with it, 86-94 ms without, hardware 2026-10-23/25), and
         * the library is meant to appear first and fill in its covers one
         * frame at a time. */
        if (!u.fresh)
            load_next_thumb();
        if (u.info && u.info_sel != g.sel && g.sel >= 0 && g.sel < g.game_count)
            row_info();
        break;
    case SCR_REMAP: update_remap(); break;
    case SCR_TEST: update_test(); break;
    case SCR_DIAG: update_diag(); break;
    case SCR_LAYOUT: update_layout(); break;
    case SCR_PICTURE: update_picture(); break;
    default: break;
    }
    focus(u.items, u.count, &u.focused, g.sel, !u.fresh);
    if (u.menu) {
        bool was = u.menu_shown;
        show(u.menu, g.menu_open, &u.menu_shown);
        if (u.menu_shown && !was)
            slide_in(u.menu_panel, 460);
        focus(u.menu_items, MENU_COUNT, &u.menu_focused, g.menu_open ? g.menu_sel : -1, false);
    }
    if (u.dialog) {
        show(u.dialog, g.dialog_open, &u.dialog_shown);
        focus(&u.dialog_ok, 1, &u.dialog_focused, g.dialog_open ? 0 : -1, false);
    }
    update_status();
    /* A saved mapping applies at once: the help line shows the new buttons. */
    if (memcmp(u.hint_cfg, g.in.cfg.bindings, sizeof u.hint_cfg)) {
        memcpy(u.hint_cfg, g.in.cfg.bindings, sizeof u.hint_cfg);
        hints(u.hints, u.hint, u.hint_n);
    }
    u.fresh = false;
}

const char *ui_action_name(int a)
{
    return action_label[a];
}

const char *ui_preset_name(int p)
{
    return preset_label[p];
}

/* For text such as the status line: plain names (an icon inside running
 * text would depend on the text font's fallback). */
const char *ui_button_name(int b)
{
    static const char *const names[RSL_BTN_COUNT] = {
        "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "+", "-", "D-pad up", "D-pad down", "D-pad left",
        "D-pad right", "LS", "RS", "SL", "SR", "stick up", "stick down", "stick left", "stick right",
    };
    return names[b];
}
