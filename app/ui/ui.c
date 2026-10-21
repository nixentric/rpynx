/* Loader screens (Milestone 7), drawn with LVGL from the state in loader.h.
 *
 * Retained mode: a screen is built when g.screen changes, then ui_update()
 * changes only what differs from the last frame, so LVGL redraws just those
 * areas (the CPU renders everything). main.c moves the focus (g.sel) with the
 * remappable actions; here it becomes LV_STATE_FOCUSED: an accent outline
 * that grows in over 120 ms, a dark accent fill on rows and buttons, and a
 * small scale on covers. Layout and navigation rules: docs/LOADER_UI.md.
 *
 * This file: fonts, styles, the building blocks every screen uses, the
 * screen frame (header, footer, status line) and the public functions. The
 * screens themselves are in ui_library.c, ui_controls.c and ui_pages.c. */
#include "ui_internal.h"

/* Kept for the whole run. */
const lv_font_t *f_s, *f_m, *f_l, *f_xl, *f_hero;
const lv_font_t *f_icon, *f_icon_big;
/* Their code points in the shared font "Nintendo Extended", as sphaira uses
 * them; SL and SR have none. icon[] holds the UTF-8 of those the font has. */
static const uint16_t icon_cp[RSL_BTN_NAV_UP] = {
    0xE0E0, 0xE0E1, 0xE0E2, 0xE0E3, 0xE0E4, 0xE0E5, 0xE0E6, 0xE0E7, 0xE0EF, 0xE0F0,
    0xE0EB, 0xE0EC, 0xE0ED, 0xE0EE, 0xE104, 0xE105, 0, 0,
};
static const uint16_t stick_cp[2] = { 0xE101, 0xE102 };   /* left, right stick: the nav directions */
char icon[RSL_BTN_NAV_UP][4];
static char stick_icon[2][4];
lv_style_t st_screen, st_card, st_pill, st_focusable, st_focus, st_grow, st_grow_row, st_row,
           st_focus_fill, st_button, st_chip, st_toggle, st_toggle_on, st_title_focus, st_icon_on;
static lv_style_transition_dsc_t trans;
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

static const hint layout_hints[] = {
    { RSL_ACT_LEFT, RSL_ACT_RIGHT, "Choose / hue" }, { RSL_ACT_CONFIRM, -1, "Use" }, { RSL_ACT_BACK, -1, "Back" },
};

const char *const action_label[RSL_ACT_COUNT] = {
    "Confirm / advance", "Back / cancel", "Hide dialogue", "History", "Rollback",
    "Roll forward", "Auto-forward", "Skip (hold)", "Game menu", "Quick menu",
    "Up", "Down", "Left", "Right", "Mouse click", "Loader overlay",
};

const char *const preset_label[RSL_PRESET_COUNT] = { "Default", "Left-handed", "One-handed (right)" };

const char *const layout_name[LAYOUT_COUNT] = { "Row", "Grid", "Cinematic" };

const struct accent_preset presets[ACCENT_PRESETS] = {
    { "Pink", 0xff5fa8 }, { "Blue", 0x25d0f2 }, { "Violet", 0xa78bfa }, { "Indigo", 0x818cf8 },
    { "Teal", 0x2dd4bf }, { "Lime", 0xa3e635 }, { "Sky", 0x60a5fa }, { "White", 0xe5e7eb },
};

/* ------------------------------------------------------------------ helpers */

/* The console's text font reports a line box that is too tight: descenders
 * were cut and wrapped lines overlapped (first hardware run, 2026-10-22).
 * Measure real glyphs and set the line box from them, with air around. */
static void fit_lines(lv_font_t *f, int px)
{
    static const uint32_t probe[] = { 'A', 0xC1, 'd', 'g', 'j', 'p', 'y', 0x590F };
    int top = 0, bottom = 0;
    for (size_t i = 0; i < sizeof probe / sizeof probe[0]; i++) {
        lv_font_glyph_dsc_t d;
        memset(&d, 0, sizeof d);
        if (lv_font_get_glyph_dsc(f, &d, probe[i], 0) && d.resolved_font == f && d.box_h) {
            top = LV_MAX(top, d.ofs_y + d.box_h);
            bottom = LV_MIN(bottom, d.ofs_y);
        }
    }
    if (top <= 0)
        return;
    int air = px / 8;
    f->base_line = -bottom + air;
    f->line_height = top - bottom + 2 * air;
}

/* Text fonts fall back to Montserrat 14, which only the host ever needs. */
static const lv_font_t *font(const void *ttf, size_t len, int px)
{
    lv_font_t *f = ttf ? lv_tiny_ttf_create_data_ex(ttf, len, px, LV_FONT_KERNING_NORMAL, 256) : NULL;
    if (!f)
        return &lv_font_montserrat_14;
    fit_lines(f, px);
    f->fallback = &lv_font_montserrat_14;
    return f;
}

/* The icon font at a size where an icon is about px tall, with the icon
 * centred in its line box so it lines up with the text beside it. */
static const lv_font_t *icon_font(const void *data, size_t len, int px)
{
    lv_font_glyph_dsc_t d;
    lv_font_t *f = lv_tiny_ttf_create_data_ex(data, len, px, LV_FONT_KERNING_NONE, 64);
    if (!f)
        return NULL;
    memset(&d, 0, sizeof d);
    if (!lv_font_get_glyph_dsc(f, &d, icon_cp[0], 0) || d.resolved_font != f || !d.box_h) {
        lv_tiny_ttf_destroy(f);
        return NULL;
    }
    if (d.box_h != px) {
        lv_tiny_ttf_set_size(f, px * px / d.box_h);
        lv_font_get_glyph_dsc(f, &d, icon_cp[0], 0);
    }
    f->line_height = d.box_h + 2;
    f->base_line = 1 - d.ofs_y;
    return f;
}

static void utf8(char out[4], uint16_t cp)
{
    out[0] = (char)(0xE0 | cp >> 12);
    out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    out[3] = 0;
}

static bool has_glyph(const lv_font_t *f, uint32_t cp)
{
    lv_font_glyph_dsc_t d;
    memset(&d, 0, sizeof d);
    return cp && lv_font_get_glyph_dsc(f, &d, cp, 0) && d.resolved_font == f && d.box_w;
}

/* The last tap, until ui_tap() takes it: what it asks for and the item
 * tapped (-1 for none). */
static int tap_action = -1, tap_item = -1;

static void item_tapped(lv_event_t *e)
{
    tap_item = (int)(intptr_t)lv_event_get_user_data(e);
    tap_action = RSL_ACT_CONFIRM;
}

static void action_tapped(lv_event_t *e)
{
    tap_item = -1;
    tap_action = (int)(intptr_t)lv_event_get_user_data(e);
}

/* o takes taps and reports them through cb with v. LVGL's own click focus
 * stays off: focus() alone sets the focus look. */
static void tappable(lv_obj_t *o, lv_event_cb_t cb, int v)
{
    lv_obj_set_clickable(o, true);
    lv_obj_set_click_focusable(o, false);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)(intptr_t)v);
}

lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return o;
}

lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap)
{
    lv_obj_t *o = box(parent);
    lv_flex_align_t cross = flow & LV_FLEX_COLUMN ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER;
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, cross, cross);
    lv_obj_set_style_pad_gap(o, gap, 0);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/* Running text: wraps, with loose lines so it reads easily. */
lv_obj_t *para(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = label(parent, font, color, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_line_space(l, 8, 0);
    return l;
}

/* A filled rectangle at x, y: the illustrations and skeletons are made of these. */
lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *o = box(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

/* Labels are only touched when their text changes, so nothing is redrawn
 * on frames where nothing happened. */
void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0)
        lv_label_set_text(l, s);
}

void set_textf(lv_obj_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void set_textf(lv_obj_t *l, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    set_text(l, buf);
}

/* A tag ("Experimental", "Handheld", ...): text in color on a 20 % tint. */
lv_obj_t *pill(lv_obj_t *parent, uint32_t color, const char *text)
{
    lv_obj_t *p = label(parent, f_s, color, text);
    lv_obj_add_style(p, &st_pill, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(color), 0);
    return p;
}

/* Focus look: outline, optional scale, and the accent colour for the item's
 * own text (a button). */
void focusable(lv_obj_t *o, lv_style_t *grow)
{
    lv_obj_add_style(o, &st_focusable, 0);
    lv_obj_add_style(o, &st_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(o, &st_title_focus, LV_STATE_FOCUSED);
    if (grow)
        lv_obj_add_style(o, grow, LV_STATE_FOCUSED);
}

/* A row's name turns accent with the row, through state trickling. */
void focus_text(lv_obj_t *row, lv_obj_t *text)
{
    lv_obj_set_state_trickle(row, true);
    lv_obj_add_style(text, &st_title_focus, LV_STATE_FOCUSED);
}

lv_obj_t *button(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *b = label(parent, f_m, TEXT, text);
    lv_obj_add_style(b, &st_button, 0);
    focusable(b, NULL);
    lv_obj_add_style(b, &st_focus_fill, LV_STATE_FOCUSED);
    lv_obj_set_width(b, width);
    return b;
}

lv_obj_t *card(lv_obj_t *parent, const char *title, int x, int y, int w, int h)
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
lv_obj_t *fact(lv_obj_t *parent, const char *key)
{
    lv_obj_t *row = flex(parent, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(label(row, f_m, DIM, key), 130);
    return flex(row, LV_FLEX_FLOW_ROW, 10);
}

lv_obj_t *note(lv_obj_t *parent, uint32_t color, const char *text)
{
    lv_obj_t *n = flex(parent, LV_FLEX_FLOW_COLUMN, 6);
    lv_obj_set_width(n, lv_pct(100));
    lv_obj_set_style_bg_color(n, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(n, LV_OPA_10, 0);
    lv_obj_set_style_border_color(n, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(n, 4, 0);
    lv_obj_set_style_border_side(n, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(n, 8, 0);
    lv_obj_set_style_pad_hor(n, 16, 0);
    lv_obj_set_style_pad_ver(n, 12, 0);
    para(n, f_m, TEXT, text);
    return n;
}

/* o scrolls in dir: to the focused item, and by dragging on the
 * touchscreen. A drag that starts on an item inside scrolls o too. */
void scrolls(lv_obj_t *o, lv_dir_t dir)
{
    lv_obj_set_scrollable(o, true);
    lv_obj_set_scroll_dir(o, dir);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_clickable(o, true);
    lv_obj_set_click_focusable(o, false);
}

/* Section heading in a list: air above, lined up with the rows' text. */
void heading(lv_obj_t *list, const char *text, int above)
{
    lv_obj_t *h = label(list, f_s, DIM, text);
    lv_obj_set_style_pad_top(h, above, 0);
    lv_obj_set_style_pad_bottom(h, 8, 0);
    lv_obj_set_style_pad_left(h, 20, 0);
}

/* A vertical scrolling area between header and footer, with room on every
 * side for the focus outline. */
lv_obj_t *scroller(lv_obj_t *parent, int y)
{
    lv_obj_t *o = lv_obj_create(parent);
    scrolls(o, LV_DIR_VER);
    lv_obj_set_pos(o, MARGIN - ROOM, y);
    lv_obj_set_size(o, W - 2 * (MARGIN - ROOM), H - FOOTER_H - y);
    lv_obj_set_style_pad_hor(o, ROOM, 0);
    lv_obj_set_style_pad_ver(o, 24, 0);
    return o;
}

/* Full-screen dimmer for the menu and the notice, hidden until opened. */
lv_obj_t *overlay(lv_obj_t *parent)
{
    lv_obj_t *o = box(parent);
    lv_obj_set_size(o, W, H);
    lv_obj_set_style_bg_color(o, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_60, 0);
    tappable(o, action_tapped, RSL_ACT_BACK);   /* a tap beside the panel closes it */
    lv_obj_set_hidden(o, true);
    return o;
}

/* What is printed on controller button b where there is no icon. */
const char *glyph(const rsl_input_config *c, int b)
{
    static const char *const fixed[RSL_BTN_NAV_UP] = {
        "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "+", "-",
        "↑", "↓", "←", "→", "LS", "RS", "SL", "SR",
    };
    static const char *const dir[4] = { "↑", "↓", "←", "→" };
    static char nav[16];
    if (b < RSL_BTN_NAV_UP)
        return fixed[b];
    snprintf(nav, sizeof nav, "%s%s", c->nav_stick == RSL_STICK_RIGHT ? "RS" : "LS", dir[b - RSL_BTN_NAV_UP]);
    return nav;
}

/* The button a press of which does action a right now, -1 if none. */
int key(int a)
{
    uint32_t m = g.in.cfg.bindings[a];
    return m ? __builtin_ctz(m) : -1;
}

static lv_obj_t *chip(lv_obj_t *parent, const char *text)
{
    lv_obj_t *c = lv_label_create(parent);
    lv_obj_add_style(c, &st_chip, 0);
    lv_label_set_text(c, text);
    return c;
}

static const char *icon_of(const rsl_input_config *c, int b)
{
    return b < 0 ? "" : b < RSL_BTN_NAV_UP ? icon[b] : stick_icon[c->nav_stick];
}

/* Button b of config c: the console's own icon when its icon font is loaded
 * (the Switch), else a drawn chip (the host, and SL, SR). A stick direction
 * shows the stick: the row or line it is on says which way. */
lv_obj_t *button_chip(lv_obj_t *parent, const rsl_input_config *c, int b)
{
    const char *ic = icon_of(c, b);
    if (ic[0])
        return label(parent, f_icon, TEXT, ic);
    return chip(parent, b < 0 ? "?" : glyph(c, b));
}

/* The help line. Each hint is also a touch button for its action; a hint
 * with two buttons does the second one on that button's icon. */
void hints(lv_obj_t *row, const hint *h, int n)
{
    lv_obj_clean(row);
    for (int i = 0; i < n; i++) {
        lv_obj_t *one = flex(row, LV_FLEX_FLOW_ROW, 10);
        tappable(one, action_tapped, h[i].a);
        lv_obj_set_ext_click_area(one, 14);
        lv_obj_set_style_opa(one, LV_OPA_50, LV_STATE_PRESSED);
        if (h[i].a >= 0)
            button_chip(one, &g.in.cfg, key(h[i].a));
        if (h[i].b >= 0)
            tappable(button_chip(one, &g.in.cfg, key(h[i].b)), action_tapped, h[i].b);
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
void make_room(lv_obj_t *o)
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
        "Layout",
    };
    static const struct { const hint *h; int n; } footer[] = {
        { HINTS(lib_hints) }, { HINTS(detail_hints) }, { HINTS(remap_hints) }, { HINTS(test_hints) },
        { HINTS(diag_hints) }, { HINTS(help_hints) }, { HINTS(layout_hints) },
    };
    bool first = !u.scr;
    lv_obj_t *s;
    memset(&u, 0, sizeof u);
    u.focused = u.menu_focused = u.dialog_focused = u.info_sel = u.backdrop_sel = u.sel_seen = -1;
    if (g.screen == SCR_LIBRARY && !g.game_count)
        s = screen(titles[0], HINTS(empty_hints));
    else
        s = screen(g.screen == SCR_LIBRARY && g.layout == LAYOUT_CINEMA ? NULL : titles[g.screen],
                   footer[g.screen].h, footer[g.screen].n);
    switch (g.screen) {
    case SCR_LIBRARY: build_library(s); break;
    case SCR_DETAIL: build_detail(s); break;
    case SCR_REMAP: build_remap(s); break;
    case SCR_TEST: build_test(s); break;
    case SCR_DIAG: build_diag(s); break;
    case SCR_HELP: build_help(s); break;
    case SCR_LAYOUT: build_layout(s); break;
    }
    for (int i = 0; i < u.count; i++)
        tappable(u.items[i], item_tapped, i);
    for (int i = 0; u.menu && i < MENU_COUNT; i++)
        tappable(u.menu_items[i], item_tapped, i);
    if (u.dialog_ok)
        tappable(u.dialog_ok, item_tapped, 0);
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

/* The shared styles that carry the accent colour. Elsewhere the screen is
 * rebuilt after a change, which recolours the rest; the accent page itself
 * only follows the shared styles, so the colour changes live as it is picked. */
static void set_accent(void)
{
    lv_style_set_outline_color(&st_focusable, lv_color_hex(ACCENT));
    lv_style_set_bg_color(&st_toggle_on, lv_color_hex(ACCENT));
    lv_style_set_text_color(&st_title_focus, lv_color_hex(ACCENT));
    lv_style_set_text_color(&st_icon_on, lv_color_hex(ACCENT));
    lv_style_set_bg_color(&st_focus_fill, lv_color_mix(lv_color_hex(ACCENT), lv_color_hex(SURFACE), LV_OPA_10));
    lv_obj_report_style_change(NULL);
}

/* ------------------------------------------------------------------- public */

void ui_init(const void *ttf, size_t ttf_len, const void *icons, size_t icons_len)
{
    f_s = font(ttf, ttf_len, 18);
    f_m = font(ttf, ttf_len, 22);
    f_l = font(ttf, ttf_len, 28);
    f_xl = font(ttf, ttf_len, 36);
    f_hero = font(ttf, ttf_len, 48);
    if (icons && (f_icon = icon_font(icons, icons_len, ICON_PX))) {
        f_icon_big = icon_font(icons, icons_len, TEST_ICON_PX);
        for (int b = 0; b < RSL_BTN_NAV_UP; b++)
            if (has_glyph(f_icon, icon_cp[b]))
                utf8(icon[b], icon_cp[b]);
        for (int s = 0; s < 2; s++)
            if (has_glyph(f_icon, stick_cp[s]))
                utf8(stick_icon[s], stick_cp[s]);
    }

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
    lv_style_set_pad_ver(&st_pill, 6);

    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(SURFACE));
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_radius(&st_card, 14);
    lv_style_set_pad_all(&st_card, 20);
    lv_style_set_border_color(&st_card, lv_color_hex(LINE));   /* the edge, on black */
    lv_style_set_border_width(&st_card, 1);

    /* Focus: an accent outline that grows in; covers also scale up. */
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
    lv_style_set_transform_scale_x(&st_grow_row, 287);
    lv_style_set_transform_scale_y(&st_grow_row, 287);

    lv_style_init(&st_row);
    lv_style_set_radius(&st_row, 10);
    lv_style_set_pad_hor(&st_row, 20);
    lv_style_set_bg_color(&st_row, lv_color_hex(SURFACE_HI));
    lv_style_set_bg_opa(&st_row, LV_OPA_TRANSP);
    lv_style_init(&st_focus_fill);   /* focused rows and buttons: a dark accent tint inside */
    lv_style_set_bg_opa(&st_focus_fill, LV_OPA_COVER);

    lv_style_init(&st_button);
    lv_style_set_bg_color(&st_button, lv_color_hex(SURFACE_HI));
    lv_style_set_bg_opa(&st_button, LV_OPA_COVER);
    lv_style_set_radius(&st_button, 12);
    lv_style_set_pad_ver(&st_button, 14);
    lv_style_set_text_align(&st_button, LV_TEXT_ALIGN_CENTER);

    /* A controller button where the console's icon is missing: a dark glyph
     * on a light disc, as the Switch draws them. */
    lv_style_init(&st_chip);
    lv_style_set_bg_color(&st_chip, lv_color_hex(TEXT));
    lv_style_set_bg_opa(&st_chip, LV_OPA_COVER);
    lv_style_set_text_color(&st_chip, lv_color_hex(BG));
    lv_style_set_text_font(&st_chip, f_s);
    lv_style_set_radius(&st_chip, LV_RADIUS_CIRCLE);
    lv_style_set_min_width(&st_chip, 28);
    lv_style_set_pad_hor(&st_chip, 7);
    lv_style_set_pad_ver(&st_chip, 2);
    lv_style_set_text_align(&st_chip, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&st_toggle);
    lv_style_set_bg_color(&st_toggle, lv_color_hex(SURFACE_HI));
    lv_style_set_bg_opa(&st_toggle, LV_OPA_COVER);
    lv_style_set_radius(&st_toggle, 10);
    lv_style_set_pad_ver(&st_toggle, 7);
    lv_style_set_text_align(&st_toggle, LV_TEXT_ALIGN_CENTER);
    lv_style_init(&st_toggle_on);
    lv_style_set_text_color(&st_toggle_on, lv_color_hex(BG));
    lv_style_init(&st_icon_on);

    lv_style_init(&st_title_focus);
    set_accent();

    library_init();
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
    case SCR_LAYOUT: update_layout(); break;
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

int ui_tap(int *item)
{
    int a = tap_action;
    *item = tap_item;
    tap_action = tap_item = -1;
    return a;
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

void ui_accent_preset(int i, uint32_t *rgb, const char **name)
{
    *rgb = presets[i].rgb;
    *name = presets[i].name;
}

/* A bright, saturated colour for a hue, readable on black (HSV, S 62 %, V 100 %). */
uint32_t ui_hue_color(int hue)
{
    int h = (hue % 360 + 360) % 360, s = 158, v = 255;
    int f = (h % 60) * 255 / 60;
    int p = v * (255 - s) / 255, q = v * (255 - s * f / 255) / 255, t = v * (255 - s * (255 - f) / 255) / 255;
    int r, gr, b;
    switch (h / 60) {
    case 0: r = v, gr = t, b = p; break;
    case 1: r = q, gr = v, b = p; break;
    case 2: r = p, gr = v, b = t; break;
    case 3: r = p, gr = q, b = v; break;
    case 4: r = t, gr = p, b = v; break;
    default: r = v, gr = p, b = q; break;
    }
    return (uint32_t)(r << 16 | gr << 8 | b);
}
