/* Loader screens (Milestone 7), drawn with LVGL from the state in loader.h.
 *
 * Retained mode: a screen is built when g.screen changes, then ui_update()
 * changes only what differs from the last frame, so LVGL redraws just those
 * areas (the CPU renders everything). main.c moves the focus (g.sel) with the
 * remappable actions; here it becomes LV_STATE_FOCUSED: a pink outline that
 * grows in over 120 ms, plus a 4 % scale on covers.
 * Layout and navigation rules: docs/LOADER_UI.md. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "loader.h"
#include "log.h"
#include "ui.h"

#define W 1280
#define H 720
#define MARGIN 48
#define HEADER_H 76
#define FOOTER_H 64
#define BODY_Y (HEADER_H + 24)
#define BODY_H (H - FOOTER_H - 16 - BODY_Y)
#define COVER_W 176          /* portrait 2:3 like GOG box art; six per row in the grid */
#define COVER_H 264
#define BIG_COVER_W 300      /* detail page; the importer writes covers at this size */
#define BIG_COVER_H 450
#define COVER_MAX_PX (600 * 900)  /* larger JPEGs are not decoded: too slow on the CPU */
#define GAP 24
#define ROW_GAP 40           /* row layout: the focused cover grows 25 % into this */
#define ROW_ROOM 40
#define ROW_Y (HEADER_H + 36) /* row layout: top of the row, so row and details sit mid-screen */
#define ROOM 12              /* space a focused item's outline and scale need */
#define RADIUS 20           /* covers: well rounded, like cards in a game store */

/* OLED dark: pure black, near-black cards with a hairline edge, a pink
 * accent for focus. Errors are orange-red so they never read as "selected". */
#define BG         0x000000
#define SURFACE    0x121215
#define SURFACE_HI 0x1f1f25
#define LINE       0x26262d
#define TEXT       0xf0f0f3
#define DIM        0x9b9ba4
#define ACCENT     (g.accent == THEME_BLUE ? 0x25d0f2 : 0xff5fa8)   /* + menu setting */
#define RED        0xff4b2b
#define AMBER      0xffb547
#define GREEN      0x52d48a

typedef struct {
    int a, b;            /* actions whose live buttons are shown, -1 for none */
    const char *text;
} hint;
#define HINTS(h) h, (int)(sizeof h / sizeof h[0])

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
static const hint help_hints[] = { { RSL_ACT_CONFIRM, -1, "Got it" }, { RSL_ACT_BACK, -1, "Back" } };
/* How to play: what each button does in a game, with the user's own mapping. */
static const hint help_rows[] = {
    { RSL_ACT_CONFIRM, -1, "Advance the story, choose" },
    { RSL_ACT_UP, RSL_ACT_DOWN, "Move between choices" },
    { RSL_ACT_BACK, -1, "Back, cancel" },
    { RSL_ACT_HIDE_UI, -1, "Hide the text box" },
    { RSL_ACT_HISTORY, -1, "Dialogue history" },
    { RSL_ACT_ROLLBACK, RSL_ACT_ROLLFORWARD, "Roll back / forward" },
    { RSL_ACT_AUTO, -1, "Auto-forward on / off" },
    { RSL_ACT_SKIP, -1, "Skip, while held" },
    { RSL_ACT_GAME_MENU, -1, "Game menu: save, load, settings" },
    { RSL_ACT_QUICK_MENU, -1, "Quick menu" },
    { RSL_ACT_CLICK, -1, "Mouse click" },
    { RSL_ACT_OVERLAY, -1, "Loader overlay" },
};
static const hint menu_hints[] = { { RSL_ACT_CONFIRM, -1, "Select" }, { RSL_ACT_BACK, -1, "Close" } };
static const hint dialog_hints[] = { { RSL_ACT_CONFIRM, -1, "OK" }, { RSL_ACT_BACK, -1, "Back" } };

static const char *const action_label[RSL_ACT_COUNT] = {
    "Confirm / advance", "Back / cancel", "Hide dialogue", "History", "Rollback",
    "Roll forward", "Auto-forward", "Skip (hold)", "Game menu", "Quick menu",
    "Up", "Down", "Left", "Right", "Mouse click", "Loader overlay",
};
static const char *const action_short[RSL_ACT_COUNT] = {
    "Confirm", "Back", "Hide UI", "History", "Rollback", "Roll fwd", "Auto", "Skip",
    "Game menu", "Quick menu", "Up", "Down", "Left", "Right", "Click", "Overlay",
};
static const char *const setting_label[ROW_COUNT - RSL_ACT_COUNT] = {
    "Stick deadzone", "Cursor speed", "Cursor acceleration", "Repeat delay",
    "Repeat interval", "Sticks", "Cursor visible in games",
};
static const char *const preset_label[RSL_PRESET_COUNT] = { "Default", "Left-handed", "One-handed (right)" };
static const char *const menu_text[MENU_COUNT][2] = {
    { "Controller settings", "Buttons, sticks, repeat speed" },
    { "Controller test", "See buttons, sticks and actions live" },
    { "Diagnostics", "Memory, files, log, Python self-test" },
    { "Library layout", NULL },   /* subtitles: the current setting */
    { "Accent colour", NULL },
    { "How to play", "Buttons, full memory, adding games" },
    { "Exit to the Homebrew Menu", "Close the loader" },
};

/* Kept for the whole run. */
static const lv_font_t *f_s, *f_m, *f_l, *f_xl, *f_hero;
static lv_style_t st_screen, st_card, st_pill, st_focusable, st_focus, st_grow, st_grow_row, st_row, st_focus_fill,
                  st_button, st_chip, st_toggle, st_toggle_on, st_title_focus;
static lv_style_transition_dsc_t trans;
static lv_obj_t *canvas;                 /* hidden; draws covers into their buffers */
static lv_draw_buf_t *thumb[MAX_GAMES];  /* grid covers; games are scanned once at start */
static bool thumb_tried[MAX_GAMES];
static lv_draw_buf_t *big;               /* detail page cover */
static lv_draw_buf_t *backdrop;          /* Cinematic layout, full screen, made on first use */

/* Objects of the current screen, reset when it is rebuilt. */
static struct {
    lv_obj_t *scr, *head, *chips, *status_line, *hints;
    const char *note;                    /* footer text while there is no status */
    screen_id screen;
    int layout, accent, info_sel;
    bool fresh;
    const hint *hint;
    int hint_n;
    uint32_t hint_cfg[RSL_ACT_COUNT];
    lv_obj_t *items[MAX_GAMES];          /* focusable items, indexed like g.sel */
    int count, focused;
    lv_obj_t *menu, *menu_items[MENU_COUNT], *dialog, *dialog_ok;
    int menu_focused, dialog_focused;
    bool menu_shown, dialog_shown, status_set;
    char status[sizeof g.status];
    status_kind status_kind;
    lv_obj_t *covers[MAX_GAMES], *info, *backdrop;                /* library */
    int backdrop_sel, sel_seen;
    uint32_t sel_at;
    lv_obj_t *values[ROW_COUNT], *capture, *profile, *preset, *unsaved;  /* remap */
    uint32_t row_sig[RSL_ACT_COUNT];
    lv_obj_t *btn[RSL_BTN_NAV_UP], *act[RSL_ACT_COUNT], *dot[2], *stick_val[2], *events, *pad;
    int dot_x[2], dot_y[2];
    lv_obj_t *mem_bar, *mem_text, *log[RSL_LOG_KEEP];             /* diagnostics */
} u;

/* ------------------------------------------------------------------ helpers */

static const lv_font_t *font(const void *ttf, size_t len, int px)
{
    lv_font_t *f = ttf ? lv_tiny_ttf_create_data_ex(ttf, len, px, LV_FONT_KERNING_NORMAL, 256) : NULL;
    if (!f)
        return &lv_font_montserrat_14;
    f->fallback = &lv_font_montserrat_14;   /* LV_SYMBOL_* arrows */
    return f;
}

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return o;
}

static lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap)
{
    lv_obj_t *o = box(parent);
    lv_flex_align_t cross = flow & LV_FLEX_COLUMN ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER;
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, cross, cross);
    lv_obj_set_style_pad_gap(o, gap, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/* Labels are only touched when their text changes, so nothing is redrawn
 * on frames where nothing happened. */
static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0)
        lv_label_set_text(l, s);
}

static void set_textf(lv_obj_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_textf(lv_obj_t *l, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    set_text(l, buf);
}

/* A tag ("Experimental", "Handheld", ...): text in color on a 20 % tint. */
static lv_obj_t *pill(lv_obj_t *parent, uint32_t color, const char *text)
{
    lv_obj_t *p = label(parent, f_s, color, text);
    lv_obj_add_style(p, &st_pill, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(color), 0);
    return p;
}

/* Focus look: outline, optional scale, and the accent colour for the item's
 * own text (a button) or, through state trickling, for the label given (a
 * row's name). */
static void focusable(lv_obj_t *o, lv_style_t *grow)
{
    lv_obj_add_style(o, &st_focusable, 0);
    lv_obj_add_style(o, &st_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(o, &st_title_focus, LV_STATE_FOCUSED);
    if (grow)
        lv_obj_add_style(o, grow, LV_STATE_FOCUSED);
}

static void focus_text(lv_obj_t *row, lv_obj_t *text)
{
    lv_obj_set_state_trickle(row, true);
    lv_obj_add_style(text, &st_title_focus, LV_STATE_FOCUSED);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *b = label(parent, f_m, TEXT, text);
    lv_obj_add_style(b, &st_button, 0);
    focusable(b, NULL);
    lv_obj_add_style(b, &st_focus_fill, LV_STATE_FOCUSED);
    lv_obj_set_width(b, width);
    return b;
}

static lv_obj_t *card(lv_obj_t *parent, const char *title, int x, int y, int w, int h)
{
    lv_obj_t *c = flex(parent, LV_FLEX_FLOW_COLUMN, 12);
    lv_obj_add_style(c, &st_card, 0);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    if (title)
        label(c, f_s, DIM, title);
    return c;
}

/* "Key   value": returns the box the value goes in. */
static lv_obj_t *fact(lv_obj_t *parent, const char *key)
{
    lv_obj_t *row = flex(parent, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(label(row, f_m, DIM, key), 130);
    return flex(row, LV_FLEX_FLOW_ROW, 10);
}

static lv_obj_t *note(lv_obj_t *parent, uint32_t color, const char *text)
{
    lv_obj_t *n = flex(parent, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_set_width(n, lv_pct(100));
    lv_obj_set_style_bg_color(n, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(n, LV_OPA_10, 0);
    lv_obj_set_style_border_color(n, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(n, 4, 0);
    lv_obj_set_style_border_side(n, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(n, 8, 0);
    lv_obj_set_style_pad_hor(n, 16, 0);
    lv_obj_set_style_pad_ver(n, 12, 0);
    lv_obj_set_width(label(n, f_m, TEXT, text), lv_pct(100));
    return n;
}

/* A vertical scrolling area between header and footer, with room on every
 * side for the focus outline. */
static lv_obj_t *scroller(lv_obj_t *parent, int y)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(o, LV_DIR_VER);
    lv_obj_set_pos(o, MARGIN - ROOM, y);
    lv_obj_set_size(o, W - 2 * (MARGIN - ROOM), H - FOOTER_H - y);
    lv_obj_set_style_pad_hor(o, ROOM, 0);
    lv_obj_set_style_pad_ver(o, 24, 0);
    return o;
}

/* Full-screen dimmer for the menu and the notice, hidden until opened. */
static lv_obj_t *overlay(lv_obj_t *parent)
{
    lv_obj_t *o = box(parent);
    lv_obj_set_size(o, W, H);
    lv_obj_set_style_bg_color(o, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_60, 0);
    lv_obj_set_hidden(o, true);
    return o;
}

/* What is printed on controller button b. */
static const char *glyph(const rsl_input_config *c, int b)
{
    static const char *const fixed[RSL_BTN_NAV_UP] = {
        "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "+", "-",
        LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "LS", "RS", "SL", "SR",
    };
    static const char *const dir[4] = { LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT };
    static char nav[16];
    if (b < RSL_BTN_NAV_UP)
        return fixed[b];
    snprintf(nav, sizeof nav, "%s %s", c->nav_stick == RSL_STICK_RIGHT ? "RS" : "LS", dir[b - RSL_BTN_NAV_UP]);
    return nav;
}

/* The button a press of which does action a right now. */
static const char *key(int a)
{
    uint32_t m = g.in.cfg.bindings[a];
    return m ? glyph(&g.in.cfg, __builtin_ctz(m)) : "?";
}

static lv_obj_t *chip(lv_obj_t *parent, const char *text)
{
    lv_obj_t *c = lv_label_create(parent);
    lv_obj_add_style(c, &st_chip, 0);
    lv_label_set_text(c, text);
    return c;
}

static void hints(lv_obj_t *row, const hint *h, int n)
{
    lv_obj_clean(row);
    for (int i = 0; i < n; i++) {
        lv_obj_t *one = flex(row, LV_FLEX_FLOW_ROW, 6);
        if (h[i].a >= 0)
            chip(one, key(h[i].a));
        if (h[i].b >= 0)
            chip(one, key(h[i].b));
        label(one, f_s, TEXT, h[i].text);
    }
}

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
    lv_obj_scroll_to_view(items[sel], anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void room(lv_event_t *e)
{
    lv_event_set_ext_draw_size(e, ROOM);
}

/* Let o's children draw ROOM pixels outside it: a focused child's outline
 * and scale. LVGL clips children to their parent's box plus its own extra
 * draw size, which a plain container does not have. */
static void make_room(lv_obj_t *o)
{
    lv_obj_set_overflow_visible(o, true);
    lv_obj_add_event_cb(o, room, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
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
        rsl_log("image not shown (needs a baseline JPEG no bigger than 600x900): %s", path);
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

/* Cover of game i: the decoded JPEG, or until then (or without one) a tile
 * in a colour picked from the folder name, with the title's first letter. */
/* Placeholder colour of game i, picked from its folder name. */
static uint32_t tint(int i)
{
    static const uint32_t tints[] = { 0x3b4f63, 0x5a4566, 0x3f5a4a, 0x664848, 0x4a4f6b, 0x5e5540 };
    uint32_t hash = 5381;
    for (const char *p = g.games[i]; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    return tints[hash % 6];
}

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
    lv_obj_t *l = label(c, f_xl, TEXT, first);
    lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
    lv_obj_center(l);
    if (buf)
        show_cover(c, buf);
    if (!g.game_ok[i] || g.info[i].problem[0] || g.info[i].issues) {
        lv_obj_t *b = label(c, f_m, BG, "!");
        lv_obj_set_size(b, 30, 30);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(b, (30 - lv_font_get_line_height(f_m)) / 2, 0);
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
static void load_next_thumb(void)
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
        if (b && u.covers[i])
            show_cover(u.covers[i], b);
        return;
    }
}

/* ----------------------------------------------------------------- screens */

static lv_obj_t *screen(const char *title, const hint *h, int n)
{
    lv_obj_t *s = lv_obj_create(NULL);
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
        lv_obj_t *rule = box(s);
        lv_obj_set_size(rule, W - 2 * MARGIN, 1);
        lv_obj_set_pos(rule, MARGIN, y);
        lv_obj_set_style_bg_color(rule, lv_color_hex(LINE), 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
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

static int banner(lv_obj_t *s, int y, uint32_t color, const char *text)
{
    lv_obj_set_pos(pill(s, color, text), MARGIN, y + 14);
    return y + 14 + lv_font_get_line_height(f_s) + 8;
}

static void build_menu(lv_obj_t *s)
{
    u.menu = overlay(s);
    lv_obj_t *p = flex(u.menu, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_add_style(p, &st_card, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_hor(p, 32, 0);
    lv_obj_set_style_pad_ver(p, 24, 0);
    lv_obj_set_size(p, 460, H);
    lv_obj_align(p, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_pad_bottom(label(p, f_l, TEXT, "Menu"), 8, 0);
    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *row = flex(p, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_add_style(row, &st_row, 0);
        lv_obj_set_style_pad_ver(row, 9, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, menu_text[i][0]));
        static const char *const layouts[] = { "Row: one line of covers", "Grid: six covers per row",
                                               "Cinematic: the game's art behind the row" };
        char sub[80];
        if (i == MENU_LAYOUT)
            snprintf(sub, sizeof sub, "%s. A: next layout", layouts[g.layout]);
        label(row, f_s, DIM, menu_text[i][1] ? menu_text[i][1] : i == MENU_LAYOUT ? sub :
              g.accent == THEME_BLUE ? "Blue. A: switch to Pink" : "Pink. A: switch to Blue");
        u.menu_items[i] = row;
    }
    lv_obj_set_flex_grow(box(p), 1);
    hints(flex(p, LV_FLEX_FLOW_ROW, 28), HINTS(menu_hints));
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

/* Row layout: what the focused game is, under the row. Rebuilt when the
 * focus moves, never on other frames. */
static void row_info(void)
{
    int i = g.sel;
    const struct game_info *gi = &g.info[i];
    char buf[200];
    u.info_sel = i;
    lv_obj_clean(u.info);
    lv_obj_t *t = label(u.info, g.layout == LAYOUT_CINEMA ? f_hero : f_xl, ACCENT, title_of(i));   /* focused game */
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
    lv_obj_set_clickable(row, false);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
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

/* Cinematic layout: the focused game's 16:9 menu art across the whole
 * screen, darkened behind the text and the row, and baked into one buffer
 * so that drawing it is a copy. Redone 150 ms after the focus stops moving,
 * so holding a direction does not decode on every step. */
static void render_backdrop(int i)
{
    lv_layer_t layer;
    lv_draw_rect_dsc_t shade;
    lv_color_t black[2] = { lv_color_black(), lv_color_black() };
    lv_opa_t left[2] = { LV_OPA_80, LV_OPA_TRANSP }, down[2] = { LV_OPA_TRANSP, LV_OPA_COVER };
    lv_area_t a;
    u.backdrop_sel = i;
    if (!render_cover(backdrop, g.info[i].backdrop, false)) {
        lv_canvas_set_draw_buf(canvas, backdrop);
        lv_canvas_fill_bg(canvas, lv_color_hex(tint(i)), LV_OPA_COVER);
    }
    lv_canvas_set_draw_buf(canvas, backdrop);
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_init(&shade);
    shade.bg_color = lv_color_black();   /* dim it all, then darker to the left and bottom */
    shade.bg_opa = LV_OPA_50;
    a = (lv_area_t){ 0, 0, W - 1, H - 1 };
    lv_draw_rect(&layer, &shade, &a);
    shade.bg_opa = LV_OPA_COVER;
    a = (lv_area_t){ 0, 0, W * 2 / 3, H - 1 };
    lv_grad_init_stops(&shade.bg_grad, black, left, NULL, 2);
    lv_grad_horizontal_init(&shade.bg_grad);
    lv_draw_rect(&layer, &shade, &a);
    lv_grad_init_stops(&shade.bg_grad, black, down, NULL, 2);
    lv_grad_vertical_init(&shade.bg_grad);
    a = (lv_area_t){ 0, H * 2 / 5, W - 1, H - 1 };
    lv_draw_rect(&layer, &shade, &a);
    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_invalidate(u.backdrop);
}

static void build_cinema(lv_obj_t *s)
{
    if (!backdrop) {
        backdrop = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_XRGB8888, 0);
        lv_draw_buf_clear(backdrop, NULL);
    }
    u.backdrop = lv_image_create(s);
    lv_image_set_src(u.backdrop, backdrop);
    lv_obj_move_to_index(u.backdrop, 0);
    build_row(s, H - FOOTER_H - COVER_H - 2 * ROW_ROOM + 8, &st_grow);
    lv_obj_set_pos(u.info, MARGIN, HEADER_H + 20);
    lv_obj_set_width(u.info, 760);
}

static void build_library(lv_obj_t *s)
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
        lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    } else if (g.layout == LAYOUT_ROW) {
        build_row(s, ROW_Y, &st_grow_row);
        lv_obj_set_pos(u.info, MARGIN, ROW_Y + COVER_H + 2 * ROW_ROOM + 12);
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
    lv_obj_t *c = flex(u.dialog, LV_FLEX_FLOW_COLUMN, 16);
    lv_obj_add_style(c, &st_card, 0);
    lv_obj_set_style_pad_all(c, 32, 0);
    lv_obj_set_width(c, 700);
    lv_obj_center(c);
    label(c, f_l, RED, "Games need full memory");
    snprintf(text, sizeof text, "The loader was opened from the Album, as an applet with only %llu MiB of "
             "memory. Ren'Py games crash the whole console in this mode, so they do not start here.\n\n"
             "To play: close the loader, then hold R while opening any game from the HOME menu. "
             "The loader then starts with full memory.", (unsigned long long)(g.mem_total >> 20));
    lv_obj_set_width(label(c, f_m, TEXT, text), lv_pct(100));
    lv_obj_t *row = flex(c, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.dialog_ok = button(row, "OK", 240);
    hints(flex(c, LV_FLEX_FLOW_ROW, 28), HINTS(dialog_hints));
}

static void build_detail(lv_obj_t *s)
{
    int i = g.game;
    const struct game_info *gi = &g.info[i];
    const char *problem = !g.game_ok[i] ? "this folder has no game/ folder inside. Copy the whole "
                                          "exported game folder." : gi->problem;
    char buf[300];

    lv_obj_t *c = cover(s, i, BIG_COVER_W, BIG_COVER_H, render_cover(big, gi->cover, true) ? big : NULL);
    lv_obj_set_pos(c, MARGIN, BODY_Y);

    /* Right of the cover: what the manifest says, the buttons at the bottom. */
    int x = MARGIN + BIG_COVER_W + 40;
    lv_obj_t *col = flex(s, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_pos(col, x, BODY_Y - 6);
    lv_obj_set_size(col, W - MARGIN - x, BODY_H + 6);
    make_room(col);
    lv_obj_set_width(label(col, f_xl, TEXT, title_of(i)), lv_pct(100));
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
            lv_obj_set_width(label(n, f_s, DIM, buf), lv_pct(100));
        }
        if (gi->issues > 3)
            label(n, f_s, DIM, "-  ... see the importer's report");
    }
    lv_obj_set_flex_grow(box(col), 1);
    lv_obj_t *row = flex(col, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.items[DETAIL_LAUNCH] = button(row, problem[0] ? "Can't launch" : "Launch", 240);
    if (problem[0]) {   /* dim even when focused */
        lv_obj_set_style_text_color(u.items[DETAIL_LAUNCH], lv_color_hex(DIM), 0);
        lv_obj_set_style_text_color(u.items[DETAIL_LAUNCH], lv_color_hex(DIM), LV_STATE_FOCUSED);
    }
    u.items[DETAIL_CONTROLS] = button(row, "Controls for this game", 300);
    u.count = DETAIL_COUNT;
    build_dialog(s);
}

static void build_remap(lv_obj_t *s)
{
    u.profile = pill(u.chips, ACCENT, "");
    u.preset = pill(u.chips, DIM, "");
    u.unsaved = pill(u.chips, AMBER, "Unsaved");
    u.note = "Hold + and - for 3 s, anywhere: default controls.";
    lv_obj_t *list = scroller(s, HEADER_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    for (int r = 0; r < ROW_COUNT; r++) {
        if (r == 0 || r == ROW_DEADZONE)
            lv_obj_set_style_pad_top(label(list, f_s, DIM, r ? "Sticks and cursor" : "Buttons"), r ? 16 : 0, 0);
        lv_obj_t *row = box(list);
        lv_obj_set_size(row, lv_pct(100), 50);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_style(row, &st_row, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, r < RSL_ACT_COUNT ? action_label[r] : setting_label[r - RSL_ACT_COUNT]));
        u.values[r] = r < RSL_ACT_COUNT ? flex(row, LV_FLEX_FLOW_ROW, 8) : label(row, f_m, TEXT, "");
        u.items[r] = row;
    }
    u.count = ROW_COUNT;
    memset(u.row_sig, 0xff, sizeof u.row_sig);
}

static void update_remap(void)
{
    const rsl_input_config *c = &g.edit;
    uint32_t clash = rsl_config_conflicts(c);
    char v[48];
    set_text(u.profile, g.edit_label);
    set_textf(u.preset, "Preset: %s", preset_label[g.edit_preset]);
    lv_obj_set_hidden(u.unsaved, !g.edit_dirty);
    for (int r = 0; r < RSL_ACT_COUNT; r++) {
        uint32_t sig = c->bindings[r] | (clash >> r & 1) << 29 | (uint32_t)(g.capture == r) << 30 |
                       (uint32_t)c->nav_stick << 31;
        if (sig != u.row_sig[r]) {
            u.row_sig[r] = sig;
            lv_obj_clean(u.values[r]);
            if (g.capture == r) {
                u.capture = label(u.values[r], f_m, ACCENT, "");
            } else {
                if (clash >> r & 1)   /* before the buttons, so the buttons line up */
                    pill(u.values[r], RED, "Conflict");
                for (int b = 0; b < RSL_BTN_COUNT; b++)
                    if (c->bindings[r] >> b & 1)
                        chip(u.values[r], glyph(c, b));
            }
        }
        if (g.capture == r) {
            int32_t left = (int32_t)(g.capture_deadline - g.now);
            set_textf(u.capture, "Press a button ... %d s", left > 0 ? (int)(left + 999) / 1000 : 0);
        }
    }
    for (int r = ROW_DEADZONE; r < ROW_COUNT; r++) {
        switch (r) {
        case ROW_DEADZONE: snprintf(v, sizeof v, "%.2f", c->deadzone); break;
        case ROW_SPEED: snprintf(v, sizeof v, "%.2f screens/s", c->cursor_speed); break;
        case ROW_ACCEL: snprintf(v, sizeof v, "%.2f", c->cursor_accel); break;
        case ROW_DELAY: snprintf(v, sizeof v, "%u ms", (unsigned)c->repeat_delay_ms); break;
        case ROW_INTERVAL: snprintf(v, sizeof v, "%u ms", (unsigned)c->repeat_interval_ms); break;
        case ROW_STICKS:
            snprintf(v, sizeof v, "Navigate %s, cursor %s", c->nav_stick ? "right" : "left",
                     c->cursor_stick ? "right" : "left");
            break;
        default: snprintf(v, sizeof v, "%s", c->cursor_visible ? "Yes" : "No"); break;
        }
        set_textf(u.values[r], LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT, v);
    }
}

static lv_obj_t *toggle(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *t = label(parent, f_s, TEXT, text);
    lv_obj_add_style(t, &st_toggle, 0);
    lv_obj_add_style(t, &st_toggle_on, LV_STATE_CHECKED);
    lv_obj_set_width(t, width);
    return t;
}

static void stick(lv_obj_t *parent, const char *name, int s)
{
    lv_obj_t *col = flex(parent, LV_FLEX_FLOW_COLUMN, 8);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(col, f_s, DIM, name);
    lv_obj_t *ring = box(col);
    lv_obj_set_size(ring, 140, 140);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, lv_color_hex(SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    u.dot[s] = box(ring);
    lv_obj_set_size(u.dot[s], 22, 22);
    lv_obj_set_style_radius(u.dot[s], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(u.dot[s], lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_opa(u.dot[s], LV_OPA_COVER, 0);
    lv_obj_center(u.dot[s]);
    u.stick_val[s] = label(col, f_s, TEXT, "");
}

static void build_test(lv_obj_t *s)
{
    u.pad = pill(u.chips, ACCENT, "");
    lv_obj_t *c = card(s, "Buttons", MARGIN, BODY_Y, 560, 260);
    lv_obj_t *wrap = flex(c, LV_FLEX_FLOW_ROW_WRAP, 8);
    lv_obj_set_width(wrap, lv_pct(100));
    for (int b = 0; b < RSL_BTN_NAV_UP; b++)
        u.btn[b] = toggle(wrap, glyph(&g.in.cfg, b), 78);
    c = card(s, "Actions", MARGIN, BODY_Y + 276, 560, BODY_H - 276);
    wrap = flex(c, LV_FLEX_FLOW_ROW_WRAP, 8);
    lv_obj_set_width(wrap, lv_pct(100));
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        u.act[a] = toggle(wrap, action_short[a], 122);
    c = card(s, NULL, 632, BODY_Y, W - MARGIN - 632, 260);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    stick(c, "Left stick", 0);
    stick(c, "Right stick", 1);
    c = card(s, NULL, 632, BODY_Y + 276, W - MARGIN - 632, BODY_H - 276);
    u.events = label(c, f_s, TEXT, "");
    lv_obj_set_style_text_line_space(u.events, 2, 0);
}

static void update_test(void)
{
    char buf[512];
    set_text(u.pad, g.pad_connected ? g.pad_desc : "No controller");
    for (int b = 0; b < RSL_BTN_NAV_UP; b++)
        lv_obj_set_state(u.btn[b], LV_STATE_CHECKED, g.raw.buttons >> b & 1);
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        lv_obj_set_state(u.act[a], LV_STATE_CHECKED, rsl_action_active(&g.in, (rsl_action)a));
    for (int s = 0; s < 2; s++) {
        float x, y;
        rsl_stick_normalize(g.raw.stick_x[s], g.raw.stick_y[s], g.in.cfg.deadzone, &x, &y);
        int dx = (int)(x * 56), dy = (int)(-y * 56);
        if (dx != u.dot_x[s] || dy != u.dot_y[s]) {
            u.dot_x[s] = dx;
            u.dot_y[s] = dy;
            lv_obj_align(u.dot[s], LV_ALIGN_CENTER, dx, dy);
        }
        set_textf(u.stick_val[s], "%+.2f   %+.2f", (double)x, (double)y);
    }
    int n = g.raw.touching ? snprintf(buf, sizeof buf, "Touch     %.0f, %.0f\n", (double)g.raw.touch_x,
                                      (double)g.raw.touch_y)
                           : snprintf(buf, sizeof buf, "Touch     -\n");
    n += snprintf(buf + n, sizeof buf - n, "Cursor    %.3f, %.3f  (%.0f, %.0f px)%s\nRecent events:",
                  (double)g.in.cursor_x, (double)g.in.cursor_y, (double)(g.in.cursor_x * W),
                  (double)(g.in.cursor_y * H), g.in.cursor_moved ? "  moving" : "");
    for (int i = 0; i < g.evcount && n < (int)sizeof buf; i++)
        n += snprintf(buf + n, sizeof buf - n, "\n    %s", g.evlog[i]);
    set_text(u.events, buf);
}

static void build_diag(lv_obj_t *s)
{
    pill(u.chips, DIM, "Version " RSL_VERSION);
    lv_obj_t *left = flex(s, LV_FLEX_FLOW_COLUMN, 24);
    lv_obj_set_pos(left, MARGIN, BODY_Y);
    make_room(left);
    lv_obj_t *c = card(left, NULL, 0, 0, 600, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(c, 14, 0);
    label(fact(c, "Applet"), f_m, TEXT, g.applet_type);
    if (!g.full_memory)
        label(c, f_s, AMBER, "Games need full memory: hold R while starting an installed game.");
    label(fact(c, "Display"), f_m, TEXT, g.mode);
    lv_obj_t *v = fact(c, "Memory");
    u.mem_bar = lv_bar_create(v);
    lv_obj_set_size(u.mem_bar, 180, 10);
    lv_obj_set_style_radius(u.mem_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(u.mem_bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(u.mem_bar, lv_color_hex(SURFACE_HI), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(u.mem_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(u.mem_bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(u.mem_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    u.mem_text = label(v, f_m, TEXT, "");
    v = fact(c, "SD folder");
    pill(v, g.sd_ok ? GREEN : RED, g.sd_ok ? "Writable" : "Not writable");
    label(v, f_s, DIM, ROOT);
    v = fact(c, "Controls");
    pill(v, g.global_load == RSL_LOAD_OK ? GREEN : AMBER, g.global_load == RSL_LOAD_OK ? "Loaded" :
         g.global_load == RSL_LOAD_BACKUP ? "Loaded from backup" : "Defaults");
    label(v, f_s, DIM, "config/controller/global.json");
    v = fact(c, "Runtime");
    pill(v, g.runtime_found ? GREEN : RED, g.runtime_found ? "Found" : "Missing");
    label(v, f_s, DIM, "runtimes/renpy-runtime.nro");

    u.items[0] = button(left, "Run Python self-test", 600);
    u.count = 1;

    c = card(s, "Recent log  (logs/loader.log)", 680, BODY_Y, W - MARGIN - 680, BODY_H);
    lv_obj_set_style_pad_row(c, 6, 0);
    for (int i = 0; i < RSL_LOG_KEEP; i++) {
        u.log[i] = label(c, f_s, TEXT, "");
        lv_obj_set_width(u.log[i], lv_pct(100));
        lv_label_set_long_mode(u.log[i], LV_LABEL_LONG_MODE_CLIP);
    }
}

static void update_diag(void)
{
    lv_bar_set_range(u.mem_bar, 0, g.mem_total >> 20 ? (int32_t)(g.mem_total >> 20) : 1);
    lv_bar_set_value(u.mem_bar, (int32_t)(g.mem_used >> 20), LV_ANIM_OFF);
    set_textf(u.mem_text, "%llu / %llu MiB", (unsigned long long)(g.mem_used >> 20),
              (unsigned long long)(g.mem_total >> 20));
    for (int i = 0; i < RSL_LOG_KEEP; i++) {
        const char *l = rsl_log_recent(i);
        set_text(u.log[i], l ? l : "");
    }
}

/* How to play: shown on the first start and from the + menu. Everything on
 * it follows the user's mapping, like the help line. */
static void build_help(lv_obj_t *s)
{
    char buf[160];
    lv_obj_t *c = card(s, "In a game (your current buttons)", MARGIN, BODY_Y, 600, BODY_H);
    lv_obj_set_style_pad_row(c, 8, 0);
    for (size_t i = 0; i < sizeof help_rows / sizeof help_rows[0]; i++) {
        lv_obj_t *row = flex(c, LV_FLEX_FLOW_ROW, 8);
        lv_obj_t *keys = flex(row, LV_FLEX_FLOW_ROW, 6);
        lv_obj_set_width(keys, 110);
        chip(keys, key(help_rows[i].a));
        if (help_rows[i].b >= 0)
            chip(keys, key(help_rows[i].b));
        label(row, f_m, TEXT, help_rows[i].text);
    }
    snprintf(buf, sizeof buf, "The %s stick moves the mouse cursor.",
             g.in.cfg.cursor_stick == RSL_STICK_RIGHT ? "right" : "left");
    lv_obj_set_style_pad_top(label(c, f_s, DIM, buf), 6, 0);

    int x = MARGIN + 600 + 32, w = W - MARGIN - x;
    lv_obj_t *col = flex(s, LV_FLEX_FLOW_COLUMN, 16);
    lv_obj_set_pos(col, x, BODY_Y);
    lv_obj_set_width(col, w);
    make_room(col);
    c = card(col, "Start with full memory", 0, 0, w, LV_SIZE_CONTENT);
    lv_obj_set_width(label(c, f_m, TEXT, "Hold R while opening any game from the HOME menu. "
                                       "Opened from the Album, the loader has too little memory "
                                       "and games do not start."), lv_pct(100));
    c = card(col, "Adding games", 0, 0, w, LV_SIZE_CONTENT);
    lv_obj_set_width(label(c, f_m, TEXT, "Export them with the desktop importer, then copy them to "
                                       "switch/renpy-loader/games/ on the SD card."), lv_pct(100));
    c = card(col, "Buttons stopped working?", 0, 0, w, LV_SIZE_CONTENT);
    lv_obj_set_width(label(c, f_m, TEXT, "Hold + and - together for 3 seconds, anywhere: the "
                                       "default controls come back."), lv_pct(100));
    u.items[0] = button(col, "Got it", 200);
    u.count = 1;
}

static void build(void)
{
    bool first = !u.scr;
    lv_obj_t *s;
    memset(&u, 0, sizeof u);
    u.focused = u.menu_focused = u.dialog_focused = u.info_sel = u.backdrop_sel = u.sel_seen = -1;
    switch (g.screen) {
    case SCR_LIBRARY:
        s = !g.game_count ? screen("Library", HINTS(empty_hints)) :
            screen(g.layout == LAYOUT_CINEMA ? NULL : "Library", HINTS(lib_hints));
        build_library(s);
        break;
    case SCR_DETAIL:
        s = screen("Game details", HINTS(detail_hints));
        build_detail(s);
        break;
    case SCR_REMAP:
        s = screen("Controller settings", HINTS(remap_hints));
        build_remap(s);
        break;
    case SCR_TEST:
        s = screen("Controller test", HINTS(test_hints));
        build_test(s);
        break;
    case SCR_HELP:
        s = screen("How to play", HINTS(help_hints));
        build_help(s);
        break;
    default:
        s = screen("Diagnostics", HINTS(diag_hints));
        build_diag(s);
        break;
    }
    u.scr = s;
    u.screen = g.screen;
    u.layout = g.layout;
    u.accent = g.accent;
    u.fresh = true;
    lv_screen_load_anim(s, LV_SCREEN_LOAD_ANIM_FADE_IN, first ? 0 : 150, 0, true);
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

/* The shared styles that carry the accent colour; the screen is rebuilt
 * after a change, which recolours the rest. */
static void set_accent(void)
{
    lv_style_set_outline_color(&st_focusable, lv_color_hex(ACCENT));
    lv_style_set_bg_color(&st_toggle_on, lv_color_hex(ACCENT));
    lv_style_set_text_color(&st_title_focus, lv_color_hex(ACCENT));
    lv_style_set_bg_color(&st_focus_fill, lv_color_mix(lv_color_hex(ACCENT), lv_color_hex(SURFACE), LV_OPA_10));
    lv_obj_report_style_change(NULL);
}

/* ------------------------------------------------------------------- public */

void ui_init(const void *ttf, size_t ttf_len)
{
    f_s = font(ttf, ttf_len, 18);
    f_m = font(ttf, ttf_len, 22);
    f_l = font(ttf, ttf_len, 28);
    f_xl = font(ttf, ttf_len, 36);
    f_hero = font(ttf, ttf_len, 48);

    static const lv_style_prop_t props[] = {
        LV_STYLE_OUTLINE_WIDTH, LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y,
        LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR, LV_STYLE_TEXT_COLOR, 0,
    };
    lv_style_transition_dsc_init(&trans, props, lv_anim_path_ease_out, 120, 0, NULL);

    lv_style_init(&st_screen);
    lv_style_set_bg_color(&st_screen, lv_color_hex(BG));
    lv_style_set_bg_opa(&st_screen, LV_OPA_COVER);
    lv_style_set_text_color(&st_screen, lv_color_hex(TEXT));
    lv_style_set_text_font(&st_screen, f_m);

    lv_style_init(&st_pill);   /* every tag: a full pill with loose padding */
    lv_style_set_bg_opa(&st_pill, LV_OPA_20);
    lv_style_set_radius(&st_pill, LV_RADIUS_CIRCLE);
    lv_style_set_pad_hor(&st_pill, 16);
    lv_style_set_pad_ver(&st_pill, 7);

    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(SURFACE));
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_radius(&st_card, 14);
    lv_style_set_pad_all(&st_card, 20);
    lv_style_set_border_color(&st_card, lv_color_hex(LINE));   /* the edge, on black */
    lv_style_set_border_width(&st_card, 1);

    /* Focus: a pink outline that grows in; covers also scale up. */
    lv_style_init(&st_focusable);
    lv_style_set_outline_pad(&st_focusable, 3);
    lv_style_set_outline_width(&st_focusable, 0);
    lv_style_set_transform_pivot_x(&st_focusable, lv_pct(50));
    lv_style_set_transform_pivot_y(&st_focusable, lv_pct(50));
    lv_style_set_transition(&st_focusable, &trans);
    lv_style_init(&st_focus);
    lv_style_set_outline_width(&st_focus, 3);
    lv_style_set_transition(&st_focus, &trans);
    lv_style_init(&st_grow);
    lv_style_set_transform_scale_x(&st_grow, 266);   /* 256 = 100 % */
    lv_style_set_transform_scale_y(&st_grow, 266);
    lv_style_init(&st_grow_row);                    /* row layout: the focused cover stands out */
    lv_style_set_transform_scale_x(&st_grow_row, 320);
    lv_style_set_transform_scale_y(&st_grow_row, 320);

    lv_style_init(&st_row);
    lv_style_set_radius(&st_row, 10);
    lv_style_set_pad_hor(&st_row, 20);
    lv_style_set_bg_color(&st_row, lv_color_hex(SURFACE_HI));
    lv_style_set_bg_opa(&st_row, LV_OPA_TRANSP);
    lv_style_init(&st_focus_fill);   /* focused rows and buttons: a light accent tint inside */
    lv_style_set_bg_opa(&st_focus_fill, LV_OPA_COVER);

    lv_style_init(&st_button);
    lv_style_set_bg_color(&st_button, lv_color_hex(SURFACE_HI));
    lv_style_set_bg_opa(&st_button, LV_OPA_COVER);
    lv_style_set_radius(&st_button, 12);
    lv_style_set_pad_ver(&st_button, 16);
    lv_style_set_text_align(&st_button, LV_TEXT_ALIGN_CENTER);

    /* A controller button as the Switch draws it: dark glyph on a light disc. */
    lv_style_init(&st_chip);
    lv_style_set_bg_color(&st_chip, lv_color_hex(TEXT));
    lv_style_set_bg_opa(&st_chip, LV_OPA_COVER);
    lv_style_set_text_color(&st_chip, lv_color_hex(BG));
    lv_style_set_text_font(&st_chip, f_s);
    lv_style_set_radius(&st_chip, LV_RADIUS_CIRCLE);
    lv_style_set_min_width(&st_chip, 28);
    lv_style_set_pad_hor(&st_chip, 7);
    lv_style_set_pad_ver(&st_chip, 3);
    lv_style_set_text_align(&st_chip, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&st_toggle);
    lv_style_set_bg_color(&st_toggle, lv_color_hex(SURFACE_HI));
    lv_style_set_bg_opa(&st_toggle, LV_OPA_COVER);
    lv_style_set_radius(&st_toggle, 10);
    lv_style_set_pad_ver(&st_toggle, 10);
    lv_style_set_text_align(&st_toggle, LV_TEXT_ALIGN_CENTER);
    lv_style_init(&st_toggle_on);
    lv_style_set_text_color(&st_toggle_on, lv_color_hex(BG));

    lv_style_init(&st_title_focus);
    set_accent();

    canvas = lv_canvas_create(lv_layer_sys());
    lv_obj_set_hidden(canvas, true);
    big = lv_draw_buf_create(BIG_COVER_W, BIG_COVER_H, LV_COLOR_FORMAT_XRGB8888, 0);
}

void ui_update(void)
{
    if (u.scr && u.accent != g.accent)
        set_accent();
    if (!u.scr || u.screen != g.screen || u.layout != g.layout || u.accent != g.accent)
        build();
    switch (g.screen) {
    case SCR_LIBRARY:
        load_next_thumb();
        if (u.info && u.info_sel != g.sel && g.sel >= 0 && g.sel < g.game_count)
            row_info();
        if (u.backdrop && g.sel != u.sel_seen) {
            u.sel_seen = g.sel;
            u.sel_at = g.now;
        }
        if (u.backdrop && u.backdrop_sel != g.sel && g.now - u.sel_at >= 150 && g.sel >= 0 &&
            g.sel < g.game_count)
            render_backdrop(g.sel);
        break;
    case SCR_REMAP: update_remap(); break;
    case SCR_TEST: update_test(); break;
    case SCR_DIAG: update_diag(); break;
    default: break;
    }
    focus(u.items, u.count, &u.focused, g.sel, !u.fresh);
    if (u.menu) {
        show(u.menu, g.menu_open, &u.menu_shown);
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
