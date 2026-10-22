/* The library in its three layouts (Row, Grid, Cinematic) and the + menu,
 * whose sliding panel the picture page reuses. */
#include "ui_internal.h"

static const hint menu_hints[] = { { RSL_ACT_CONFIRM, -1, "Select" }, { RSL_ACT_BACK, -1, "Close" } };

static const char *const menu_text[MENU_COUNT][2] = {
    { "Controller settings", "Buttons, sticks, repeat speed" },
    { "Controller test", "See buttons, sticks and actions live" },
    { "Layout", NULL },   /* subtitle: the current settings */
    { "How to play", "Buttons, full memory, adding games" },
    { "Diagnostics", "Memory, files, log, Python self-test" },
    { "Exit to the Homebrew Menu", "Close the loader" },
};

static int banner(lv_obj_t *s, int y, uint32_t color, const char *text)
{
    lv_obj_set_pos(pill(s, color, text), MARGIN, y + 14);
    return y + 14 + lv_font_get_line_height(f_s) + 22;
}

static const char *accent_name(void)
{
    for (int i = 0; i < ACCENT_PRESETS; i++)
        if (presets[i].rgb == g.accent)
            return presets[i].name;
    return "Custom";
}

/* A panel that slides in from the right over s: a title, n entries of a name
 * and a line under it (entry dyn's line is dyn_sub), the help at the bottom.
 * g.menu_open shows it, g.menu_sel is the focused entry. */
void side_menu(lv_obj_t *s, const char *title, const char *const (*text)[2], int n, int dyn,
                      const char *dyn_sub)
{
    u.menu = overlay(s);
    lv_obj_t *p = flex(u.menu, LV_FLEX_FLOW_COLUMN, 4);
    u.menu_panel = p;
    lv_obj_add_style(p, &st_card, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_hor(p, 28 - ROOM, 0);
    lv_obj_set_style_pad_ver(p, 24, 0);
    lv_obj_set_size(p, 460, H);
    lv_obj_align(p, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_clickable(p, true);   /* a tap on the panel is not one beside it */
    lv_obj_set_click_focusable(p, false);
    lv_obj_set_style_pad_hor(label(p, f_l, TEXT, title), ROOM, 0);
    /* The items scroll when they do not fit (the console's font is taller);
     * the help stays at the bottom under a rule. */
    lv_obj_t *list = flex(p, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    scrolls(list, LV_DIR_VER);
    lv_obj_set_style_pad_all(list, ROOM, 0);   /* the focus outline */
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = flex(list, LV_FLEX_FLOW_COLUMN, 4);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_add_style(row, &st_row, 0);
        lv_obj_set_style_pad_ver(row, 7, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, text[i][0]));
        /* Full width, so the subtitle wraps instead of running past the
         * panel with the console's wider font (hardware run 28). */
        lv_obj_set_width(label(row, f_s, DIM, i == dyn ? dyn_sub : text[i][1]), lv_pct(100));
        u.menu_items[i] = row;
    }
    lv_obj_t *foot = box(p);
    lv_obj_set_width(foot, lv_pct(100));
    lv_obj_set_style_pad_hor(foot, ROOM, 0);
    lv_obj_t *bar = flex(foot, LV_FLEX_FLOW_ROW, 28);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(LINE), 0);
    lv_obj_set_style_pad_top(bar, 20, 0);
    hints(bar, HINTS(menu_hints));
}

static void build_menu(lv_obj_t *s)
{
    char sub[80];
    snprintf(sub, sizeof sub, "%s, %s, background %s", layout_name[g.layout], accent_name(),
             g.backdrops ? "on" : "off");
    side_menu(s, "Menu", menu_text, MENU_COUNT, MENU_LAYOUT, sub);
}

static void build_grid(lv_obj_t *s, int y)
{
    lv_obj_t *grid = scroller(s, y);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, GAP, 0);
    lv_obj_set_style_pad_row(grid, 24, 0);
    for (int i = 0; i < g.game_count; i++) {
        lv_obj_t *tile = flex(grid, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_width(tile, COVER_W);
        make_room(tile);
        lv_obj_set_state_trickle(tile, true);
        u.covers[i] = cover(tile, i, COVER_W, COVER_H, thumb[i]);
        focusable(u.covers[i], &st_grow);
        lv_obj_t *t = label(tile, f_s, DIM, title_of(i));
        lv_obj_set_width(t, COVER_W);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_add_style(t, &st_title_focus, LV_STATE_FOCUSED);
        u.items[i] = tile;
    }
    u.count = g.game_count;
}

/* The manifest status as a coloured pill. */
void status_pill(lv_obj_t *parent, const struct game_info *gi)
{
    if (!gi->state[0])
        pill(parent, DIM, "No manifest: copied by hand");
    else if (!strcmp(gi->state, "experimental"))
        pill(parent, AMBER, "Experimental");
    else if (!strcmp(gi->state, "verified"))
        pill(parent, GREEN, "Verified");
    else
        pill(parent, RED, !strcmp(gi->state, "unsupported") ? "Unsupported" : gi->state);
}

/* Row and Cinematic layouts: what the focused game is. Rebuilt when the
 * focus moves, never on other frames. */
void row_info(void)
{
    int i = g.sel;
    const struct game_info *gi = &g.info[i];
    char buf[200];
    u.info_sel = i;
    lv_obj_clean(u.info);
    lv_obj_t *t = g.layout == LAYOUT_CINEMA ? label(u.info, f_hero, TEXT, title_of(i))
                                            : label(u.info, f_xl, ACCENT, title_of(i));
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *meta = flex(u.info, LV_FLEX_FLOW_ROW, 10);
    status_pill(meta, gi);
    snprintf(buf, sizeof buf, "Ren'Py %s", gi->renpy[0] ? gi->renpy : "unknown");
    pill(meta, DIM, buf);
    if (gi->runtime[0]) {
        snprintf(buf, sizeof buf, "%s %s", gi->runtime, gi->runtime_ok ? "installed" : "missing");
        pill(meta, gi->runtime_ok ? GREEN : RED, buf);
    }
    const char *note = NULL;
    uint32_t color = AMBER;
    if (!g.game_ok[i] || gi->problem[0]) {
        snprintf(buf, sizeof buf, "Can't start: %s", g.game_ok[i] ? gi->problem : "no game/ folder inside");
        note = buf;
        color = RED;
    } else if (gi->issues) {
        snprintf(buf, sizeof buf, gi->issues == 1 ? "1 performance issue: see details" :
                 "%d performance issues: see details", gi->issues);
        note = buf;
    } else if (gi->recompile) {
        note = "Ren'Py 7 game, recompiled for Ren'Py 8. Experimental.";
    }
    if (note) {
        lv_obj_t *n = label(u.info, f_m, color, note);
        lv_obj_set_width(n, lv_pct(100));
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    }
}

/* One line of covers scrolling sideways, top at y; the focused game's
 * details go in u.info, which the caller places. */
static void build_row(lv_obj_t *s, int y, lv_style_t *grow)
{
    lv_obj_t *row = lv_obj_create(s);
    scrolls(row, LV_DIR_HOR);
    lv_obj_set_pos(row, MARGIN - ROW_ROOM, y);
    lv_obj_set_size(row, W - 2 * (MARGIN - ROW_ROOM), COVER_H + 2 * ROW_ROOM);
    lv_obj_set_style_pad_all(row, ROW_ROOM, 0);
    lv_obj_set_style_pad_column(row, ROW_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < g.game_count; i++) {
        u.covers[i] = u.items[i] = cover(row, i, COVER_W, COVER_H, thumb[i]);
        focusable(u.covers[i], grow);
    }
    u.count = g.game_count;
    u.info = flex(s, LV_FLEX_FLOW_COLUMN, 12);
    lv_obj_set_width(u.info, W - 2 * MARGIN);
}

static void build_cinema(lv_obj_t *s)
{
    if (g.backdrops)
        backdrop_under(s);
    build_row(s, H - FOOTER_H - COVER_H - 2 * ROW_ROOM, &st_grow);
    lv_obj_set_pos(u.info, MARGIN, HEADER_H + 16);
    lv_obj_set_width(u.info, 760);
}

void build_library(lv_obj_t *s)
{
    char buf[32];
    snprintf(buf, sizeof buf, g.game_count == 1 ? "%d game" : "%d games", g.game_count);
    if (g.layout != LAYOUT_CINEMA || !g.game_count)
        label(u.head, f_m, DIM, buf);
    pill(u.chips, DIM, g.mode);
    if (!g.full_memory)
        pill(u.chips, AMBER, "Applet mode: limited memory");

    int y = HEADER_H;
    if (!g.sd_ok)
        y = banner(s, y, RED, "The SD card folder is not writable: settings and logs will not be saved.");
    if (g.global_load == RSL_LOAD_BACKUP)
        y = banner(s, y, AMBER, "The controller settings file was damaged; its backup copy was loaded.");

    if (!g.game_count) {
        lv_obj_t *col = flex(s, LV_FLEX_FLOW_COLUMN, 12);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label(col, f_l, TEXT, "No games yet");
        lv_obj_t *t = label(col, f_m, DIM, "Export a game with the importer and copy its folder to\n"
                                           GAMES_DIR "/\nSee docs/INSTALL.md.");
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(t, 8, 0);
        lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    } else if (g.layout == LAYOUT_ROW) {
        build_row(s, ROW_Y, &st_grow_row);
        lv_obj_set_pos(u.info, MARGIN, ROW_Y + COVER_H + 2 * ROW_ROOM + 8);
    } else if (g.layout == LAYOUT_CINEMA) {
        build_cinema(s);
    } else {
        build_grid(s, y);
    }
    build_menu(s);
}
