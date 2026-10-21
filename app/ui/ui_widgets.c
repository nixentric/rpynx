/* Building blocks every screen uses (boxes, labels, pills, buttons, lists,
 * the help line) and touch: what a tap asks for, until ui_tap() takes it. */
#include "ui_internal.h"

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

/* An item of the current screen (g.sel, the menu's g.menu_sel, the
 * notice's OK): a tap focuses it and confirms. */
void tappable_item(lv_obj_t *o, int i)
{
    tappable(o, item_tapped, i);
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

int ui_tap(int *item)
{
    int a = tap_action;
    *item = tap_item;
    tap_action = tap_item = -1;
    return a;
}
