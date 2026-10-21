/* Library screens: covers (decoded once into buffers), the three library
 * layouts, the + menu, game details and the applet-mode notice. */
#include "ui_internal.h"

/* Kept for the whole run. */
static int covers_decoded, covers_failed;
static lv_obj_t *canvas;                 /* hidden; draws covers into their buffers */
static lv_draw_buf_t *thumb[MAX_GAMES];  /* grid covers; games are scanned once at start */
static bool thumb_tried[MAX_GAMES];
static lv_draw_buf_t *big;               /* detail page cover */
static lv_draw_buf_t *backdrop;          /* Cinematic layout, full screen, made on first use */

static const hint menu_hints[] = { { RSL_ACT_CONFIRM, -1, "Select" }, { RSL_ACT_BACK, -1, "Close" } };

static const hint dialog_hints[] = { { RSL_ACT_CONFIRM, -1, "OK" }, { RSL_ACT_BACK, -1, "Back" } };

static const hint detail_scroll_hints[] = {
    { RSL_ACT_UP, RSL_ACT_DOWN, "Scroll" }, { RSL_ACT_CONFIRM, -1, "Select" }, { RSL_ACT_BACK, -1, "Back" },
};

static const char *const menu_text[MENU_COUNT][2] = {
    { "Controller settings", "Buttons, sticks, repeat speed" },
    { "Controller test", "See buttons, sticks and actions live" },
    { "Diagnostics", "Memory, files, log, Python self-test" },
    { "Layout", NULL },   /* subtitle: the current settings */
    { "How to play", "Buttons, full memory, adding games" },
    { "Exit to the Homebrew Menu", "Close the loader" },
};

/* ------------------------------------------------------------------ covers */

static const char *title_of(int i)
{
    return g.info[i].title[0] ? g.info[i].title : g.games[i];
}

/* Draw src into dst at area a, scaled by scale/256 from a's top left. */
static void paint(lv_draw_buf_t *dst, const void *src, int32_t scale, const lv_area_t *a)
{
    lv_layer_t layer;
    lv_draw_image_dsc_t img;
    lv_canvas_set_draw_buf(canvas, dst);
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_image_dsc_init(&img);
    img.src = src;
    img.scale_x = img.scale_y = scale;
    lv_draw_image(&layer, &img, a);
    lv_canvas_finish_layer(canvas, &layer);
}

/* Decode the JPEG at path once, scaled to fill buf (cropped to its aspect),
 * with the corners rounded on the background colour, so that drawing it
 * later is a plain copy. LVGL's JPEG decoder keeps nothing: drawing the
 * file itself would decode it again on every frame of a scroll. */
static bool render_cover(lv_draw_buf_t *buf, const char *path, bool round)
{
    char src[sizeof g.info[0].cover + 2];
    lv_image_header_t hd;
    int32_t w = buf->header.w, h = buf->header.h;
    if (!path[0])
        return false;
    snprintf(src, sizeof src, "S:%s", path);
    if (lv_image_decoder_get_info(src, &hd) != LV_RESULT_OK || (uint32_t)hd.w * hd.h > COVER_MAX_PX) {
        static uint32_t warned;   /* at most one line a minute; the 60 s summary counts them */
        if (!warned || g.now - warned >= 60000) {
            warned = g.now | 1;
            rsl_log("ui warning: image not shown (needs a baseline JPEG no bigger than 600x900): %s", path);
        }
        return false;
    }
    /* The decoder works in 8-pixel strips, which LVGL cannot scale:
     * decode the whole picture first, then scale that. */
    lv_draw_buf_t *full = lv_draw_buf_create(hd.w, hd.h, LV_COLOR_FORMAT_XRGB8888, 0);
    if (!full)
        return false;
    lv_area_t a = { 0, 0, (int32_t)hd.w - 1, (int32_t)hd.h - 1 };
    paint(full, src, LV_SCALE_NONE, &a);

    int32_t scale = LV_MAX(w * 256 / (int32_t)hd.w, h * 256 / (int32_t)hd.h) + 1;
    lv_area_move(&a, (w - (int32_t)hd.w * scale / 256) / 2, (h - (int32_t)hd.h * scale / 256) / 2);
    lv_canvas_set_draw_buf(canvas, buf);
    lv_canvas_fill_bg(canvas, lv_color_hex(BG), LV_OPA_COVER);
    paint(buf, full, scale, &a);
    lv_draw_buf_destroy(full);
    if (!round)
        return true;

    /* Round the corners: a border whose inner edge is the cover's outline. */
    lv_layer_t layer;
    lv_draw_rect_dsc_t frame;
    lv_area_t f = { -RADIUS, -RADIUS, w - 1 + RADIUS, h - 1 + RADIUS };
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_init(&frame);
    frame.bg_opa = LV_OPA_TRANSP;
    frame.border_color = lv_color_hex(BG);
    frame.border_width = RADIUS;
    frame.radius = 2 * RADIUS;
    lv_draw_rect(&layer, &frame, &f);
    lv_canvas_finish_layer(canvas, &layer);
    return true;
}

static void show_cover(lv_obj_t *c, lv_draw_buf_t *buf)
{
    lv_obj_delete(lv_obj_get_child(c, 0));   /* the placeholder letter */
    lv_obj_t *im = lv_image_create(c);
    lv_image_set_src(im, buf);
    lv_obj_move_to_index(im, 0);             /* under the badge */
}

/* Placeholder colour of game i, picked from its folder name. */
uint32_t tint(int i)
{
    static const uint32_t tints[] = { 0x3b4f63, 0x5a4566, 0x3f5a4a, 0x664848, 0x4a4f6b, 0x5e5540 };
    uint32_t hash = 5381;
    for (const char *p = g.games[i]; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    return tints[hash % 6];
}

/* Cover of game i: the decoded JPEG, or until then (or without one) a tile
 * in its placeholder colour with the title's first letter. */
static lv_obj_t *cover(lv_obj_t *parent, int i, int w, int h, lv_draw_buf_t *buf)
{
    const char *t = title_of(i);
    unsigned char c0 = (unsigned char)t[0];
    char first[5] = "";
    size_t n = c0 >= 0xf0 ? 4 : c0 >= 0xe0 ? 3 : c0 >= 0xc0 ? 2 : 1;
    memcpy(first, t, LV_MIN(n, strlen(t)));

    lv_obj_t *c = box(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, RADIUS, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(tint(i)), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_t *l = label(c, f_hero, TEXT, first);
    lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
    lv_obj_center(l);
    if (buf)
        show_cover(c, buf);
    if (!g.game_ok[i] || g.info[i].problem[0] || g.info[i].issues) {
        lv_obj_t *b = label(c, f_m, BG, "!");
        lv_obj_set_size(b, 32, 32);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(b, (32 - lv_font_get_line_height(f_m)) / 2, 0);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(g.info[i].issues && g.game_ok[i] && !g.info[i].problem[0]
                                                  ? AMBER : RED), 0);
        lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -12, 12);
    }
    return c;
}

/* Decode the next grid cover, if one is left: one per frame, so the first
 * frame is not held up by a library full of covers. */
void load_next_thumb(void)
{
    for (int i = 0; i < g.game_count; i++) {
        if (thumb_tried[i])
            continue;
        thumb_tried[i] = true;
        if (!g.info[i].cover[0])
            continue;
        lv_draw_buf_t *b = lv_draw_buf_create(COVER_W, COVER_H, LV_COLOR_FORMAT_XRGB8888, 0);
        if (b && !render_cover(b, g.info[i].cover, true)) {
            lv_draw_buf_destroy(b);
            b = NULL;
        }
        thumb[i] = b;
        if (b)
            covers_decoded++;
        else
            covers_failed++;
        if (b && u.covers[i])
            show_cover(u.covers[i], b);
        return;
    }
}

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

static void build_menu(lv_obj_t *s)
{
    char sub[80];
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
    lv_obj_set_style_pad_hor(label(p, f_l, TEXT, "Menu"), ROOM, 0);
    /* The items scroll when they do not fit (the console's font is taller);
     * the help stays at the bottom under a rule. */
    lv_obj_t *list = flex(p, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    scrolls(list, LV_DIR_VER);
    lv_obj_set_style_pad_all(list, ROOM, 0);   /* the focus outline */
    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *row = flex(list, LV_FLEX_FLOW_COLUMN, 4);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_add_style(row, &st_row, 0);
        lv_obj_set_style_pad_ver(row, 7, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, menu_text[i][0]));
        if (i == MENU_LAYOUT)
            snprintf(sub, sizeof sub, "%s, %s, background %s", layout_name[g.layout], accent_name(),
                     g.backdrops ? "on" : "off");
        label(row, f_s, DIM, menu_text[i][1] ? menu_text[i][1] : sub);
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
static void status_pill(lv_obj_t *parent, const struct game_info *gi)
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

/* The glow behind a game without art: its cover's main colour, darkened.
 * Every 4th pixel of every 4th line goes into one of six hue bins by the
 * order of its channels, weighted by how colourful it is; the fullest bin's
 * average wins (a plain average of sky and brick is grey). A grey cover, or
 * none, gets the placeholder colour. */
static lv_color_t ambient(int i)
{
    const lv_draw_buf_t *b = thumb[i];
    uint32_t bin[6][4] = { { 0 } }, best = 0;
    for (uint32_t y = 0; b && y < b->header.h; y += 4) {
        const lv_color32_t *px = (const lv_color32_t *)(b->data + y * b->header.stride);
        for (uint32_t x = 0; x < b->header.w; x += 4) {
            lv_color32_t p = px[x];
            int hi = LV_MAX(p.red, LV_MAX(p.green, p.blue)), lo = LV_MIN(p.red, LV_MIN(p.green, p.blue));
            int k = hi == p.red ? (p.green >= p.blue ? 0 : 5) : hi == p.green ? (p.blue >= p.red ? 2 : 1)
                                                                              : (p.red >= p.green ? 4 : 3);
            uint32_t w = (uint32_t)(hi - lo);
            bin[k][0] += p.red * w;
            bin[k][1] += p.green * w;
            bin[k][2] += p.blue * w;
            bin[k][3] += w;
        }
    }
    for (uint32_t k = 1; k < 6; k++)
        if (bin[k][3] > bin[best][3])
            best = k;
    uint32_t *c = bin[best];
    if (!c[3])
        return lv_color_mix(lv_color_hex(tint(i)), lv_color_black(), LV_OPA_70);
    return lv_color_mix(lv_color_make(c[0] / c[3], c[1] / c[3], c[2] / c[3]), lv_color_black(), LV_OPA_60);
}

/* The full-screen picture behind the Cinematic layout and the detail page:
 * the game's 16:9 menu art (on the detail page else its cover), or a dark
 * glow in its cover's colour, baked into one buffer so that drawing it is a
 * copy. Both darken it under the header and toward the bottom; the
 * Cinematic layout also toward the left, behind the title, and the detail
 * page fades it further. The Cinematic layout redoes it 150 ms after the
 * focus stops moving, so holding a direction does not decode on every step. */
void render_backdrop(int i)
{
    bool detail = g.screen == SCR_DETAIL;
    lv_layer_t layer;
    lv_draw_rect_dsc_t shade;
    lv_color_t black[2] = { lv_color_black(), lv_color_black() };
    lv_opa_t left[2] = { LV_OPA_80, LV_OPA_TRANSP }, down[2] = { LV_OPA_TRANSP, LV_OPA_COVER },
             top[2] = { LV_OPA_80, LV_OPA_TRANSP };
    lv_area_t a = { 0, 0, W - 1, H - 1 };
    bool art = render_cover(backdrop, g.info[i].backdrop, false) ||
               (detail && render_cover(backdrop, g.info[i].cover, false));
    u.backdrop_sel = i;
    lv_canvas_set_draw_buf(canvas, backdrop);
    if (!art)
        lv_canvas_fill_bg(canvas, ambient(i), LV_OPA_COVER);
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_init(&shade);
    shade.bg_color = lv_color_black();
    shade.bg_opa = !art ? LV_OPA_TRANSP : detail ? LV_OPA_70 : LV_OPA_50;
    lv_draw_rect(&layer, &shade, &a);
    shade.bg_opa = LV_OPA_COVER;
    if (!detail) {
        a = (lv_area_t){ 0, 0, W * 2 / 3, H - 1 };
        lv_grad_init_stops(&shade.bg_grad, black, left, NULL, 2);
        lv_grad_horizontal_init(&shade.bg_grad);
        lv_draw_rect(&layer, &shade, &a);
    }
    lv_grad_init_stops(&shade.bg_grad, black, top, NULL, 2);
    lv_grad_vertical_init(&shade.bg_grad);
    a = (lv_area_t){ 0, 0, W - 1, HEADER_H + 60 };
    lv_draw_rect(&layer, &shade, &a);
    lv_grad_init_stops(&shade.bg_grad, black, down, NULL, 2);
    lv_grad_vertical_init(&shade.bg_grad);
    a = (lv_area_t){ 0, detail ? H / 3 : H * 2 / 5, W - 1, H - 1 };
    lv_draw_rect(&layer, &shade, &a);
    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_invalidate(u.backdrop);
}

/* The full-screen picture under everything else on s; render_backdrop fills it. */
static void backdrop_under(lv_obj_t *s)
{
    if (!backdrop) {
        backdrop = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_XRGB8888, 0);
        lv_draw_buf_clear(backdrop, NULL);
    }
    u.backdrop = lv_image_create(s);
    lv_image_set_src(u.backdrop, backdrop);
    lv_obj_move_to_index(u.backdrop, 0);
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

/* Launch in applet mode: games do not start there, with no override. A game
 * started that way took the whole console down twice (hardware runs 8, 26). */
static void build_dialog(lv_obj_t *s)
{
    char text[400];
    u.dialog = overlay(s);
    lv_obj_t *c = flex(u.dialog, LV_FLEX_FLOW_COLUMN, 18);
    lv_obj_add_style(c, &st_card, 0);
    lv_obj_set_style_pad_all(c, 32, 0);
    lv_obj_set_width(c, 700);
    lv_obj_center(c);
    lv_obj_set_clickable(c, true);   /* a tap on the notice is not one beside it */
    lv_obj_set_click_focusable(c, false);
    label(c, f_l, RED, "Games need full memory");
    snprintf(text, sizeof text, "The loader was opened from the Album, as an applet with only %llu MiB of "
             "memory. Ren'Py games crash the whole console in this mode, so they do not start here.\n\n"
             "To play: close the loader, then hold R while opening any game from the HOME menu. "
             "The loader then starts with full memory.", (unsigned long long)(g.mem_total >> 20));
    para(c, f_m, TEXT, text);
    lv_obj_t *row = flex(c, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.dialog_ok = button(row, "OK", 240);
    hints(flex(c, LV_FLEX_FLOW_ROW, 28), HINTS(dialog_hints));
}

/* A play triangle filling the object, in its (inherited) text colour: the
 * console's text font has no ▶. */
static void draw_play(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_draw_triangle_dsc_t d;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_obj_get_style_text_color(o, 0);
    d.p[0].x = d.p[2].x = a.x1;
    d.p[0].y = a.y1;
    d.p[1].x = a.x2;
    d.p[1].y = (a.y1 + a.y2) / 2;
    d.p[2].y = a.y2;
    lv_draw_triangle(lv_event_get_layer(e), &d);
}

/* button() with a play icon before the text; both follow the focus colour. */
static lv_obj_t *play_button(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *b = flex(parent, LV_FLEX_FLOW_ROW, 14);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_color(b, lv_color_hex(TEXT), 0);
    lv_obj_add_style(b, &st_button, 0);
    focusable(b, NULL);
    lv_obj_add_style(b, &st_focus_fill, LV_STATE_FOCUSED);
    lv_obj_set_width(b, width);
    lv_obj_t *ic = box(b);
    lv_obj_set_size(ic, 16, 18);
    lv_obj_add_event_cb(ic, draw_play, LV_EVENT_DRAW_MAIN, NULL);
    lv_label_set_text(lv_label_create(b), text);
    return b;
}

/* Up and down on the detail page: the facts can run longer than the room
 * above the buttons. */
void ui_scroll(int dir)
{
    if (u.screen == SCR_DETAIL && u.info)
        lv_obj_scroll_by_bounded(u.info, 0, -dir * 160, LV_ANIM_ON);
}

void build_detail(lv_obj_t *s)
{
    int i = g.game;
    const struct game_info *gi = &g.info[i];
    const char *problem = !g.game_ok[i] ? "this folder has no game/ folder inside. Copy the whole "
                                          "exported game folder." : gi->problem;
    char buf[300];

    if (g.backdrops) {
        backdrop_under(s);
        render_backdrop(i);
    }
    lv_obj_t *c = cover(s, i, BIG_COVER_W, BIG_COVER_H, render_cover(big, gi->cover, false) ? big : NULL);
    lv_obj_set_style_clip_corner(c, true, 0);   /* the backdrop shows around the corners */
    lv_obj_set_pos(c, MARGIN, BODY_Y);

    /* Bottom right, whatever the length of the facts: the buttons, Launch last. */
    lv_obj_t *row = flex(s, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.items[DETAIL_CONTROLS] = button(row, "Controls for this game", 300);
    u.items[DETAIL_LAUNCH] = play_button(row, problem[0] ? "Can't launch" : "Launch", 240);
    if (problem[0]) {   /* dim even when focused */
        lv_obj_set_style_text_color(u.items[DETAIL_LAUNCH], lv_color_hex(DIM), 0);
        lv_obj_set_style_text_color(u.items[DETAIL_LAUNCH], lv_color_hex(DIM), LV_STATE_FOCUSED);
    }
    u.count = DETAIL_COUNT;
    lv_obj_align(row, LV_ALIGN_BOTTOM_RIGHT, -MARGIN, -(H - BODY_Y - BODY_H));
    lv_obj_update_layout(row);

    /* Right of the cover, above the buttons: what the manifest says. */
    int x = MARGIN + BIG_COVER_W + 40;
    lv_obj_t *col = u.info = flex(s, LV_FLEX_FLOW_COLUMN, 14);
    scrolls(col, LV_DIR_VER);
    lv_obj_set_pos(col, x, BODY_Y - 6);
    lv_obj_set_size(col, W - MARGIN - x, lv_obj_get_y(row) - 24 - (BODY_Y - 6));
    para(col, f_xl, TEXT, title_of(i));
    snprintf(buf, sizeof buf, "games/%s", g.games[i]);
    lv_obj_set_style_pad_bottom(label(col, f_s, DIM, buf), 6, 0);

    lv_obj_t *v = fact(col, "Ren'Py");
    label(v, f_m, TEXT, gi->renpy[0] ? gi->renpy : "unknown");
    status_pill(fact(col, "Status"), gi);
    v = fact(col, "Runtime");
    label(v, f_m, TEXT, gi->runtime[0] ? gi->runtime : "none");
    if (gi->runtime[0])
        pill(v, gi->runtime_ok ? GREEN : RED, gi->runtime_ok ? "Installed" : "Missing");

    if (gi->recompile)
        note(col, AMBER, "Ren'Py 7 game, recompiled from its .rpy sources for Ren'Py 8. Experimental.");
    if (problem[0]) {
        snprintf(buf, sizeof buf, "Can't start: %s", problem);
        note(col, RED, buf);
    }
    if (gi->issues) {
        snprintf(buf, sizeof buf, gi->issues == 1 ? "%d performance issue found by the importer:" :
                 "%d performance issues found by the importer:", gi->issues);
        lv_obj_t *n = note(col, AMBER, buf);
        for (int k = 0; k < gi->issues && k < 3; k++) {
            snprintf(buf, sizeof buf, "-  %s", gi->issue[k]);
            para(n, f_s, DIM, buf);
        }
        if (gi->issues > 3)
            label(n, f_s, DIM, "-  ... see the importer's report");
    }
    lv_obj_update_layout(col);
    if (lv_obj_get_scroll_bottom(col) > 0) {   /* longer than the room: say how to see the rest */
        u.hint = detail_scroll_hints;
        u.hint_n = (int)(sizeof detail_scroll_hints / sizeof detail_scroll_hints[0]);
        hints(u.hints, u.hint, u.hint_n);
    }
    build_dialog(s);
}

bool ui_idle(void)
{
    if (g.screen == SCR_LIBRARY) {
        for (int i = 0; i < g.game_count; i++)
            if (!thumb_tried[i])
                return false;
        if (u.backdrop && u.backdrop_sel != g.sel)
            return false;
    }
    return lv_anim_count_running() == 0;
}

void ui_cover_counts(int *decoded, int *failed)
{
    *decoded = covers_decoded;
    *failed = covers_failed;
}

void library_init(void)
{
    canvas = lv_canvas_create(lv_layer_sys());
    lv_obj_set_hidden(canvas, true);
    big = lv_draw_buf_create(BIG_COVER_W, BIG_COVER_H, LV_COLOR_FORMAT_XRGB8888, 0);
}
