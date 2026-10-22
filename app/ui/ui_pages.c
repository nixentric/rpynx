/* Pages the + menu opens: how to play, and Layout (library layout,
 * backgrounds, accent colour). */
#include "ui_internal.h"

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
    { RSL_ACT_GAME_MENU, -1, "Game menu: save, load" },
    { RSL_ACT_QUICK_MENU, -1, "Quick menu" },
    { RSL_ACT_CLICK, -1, "Mouse click" },
    { RSL_ACT_OVERLAY, -1, "Loader overlay" },
};

static const char *const layout_text[LAYOUT_COUNT] = {
    "One line of covers along the bottom, like the HOME menu.",
    "Five covers per row with their titles. Quick to scan a long list.",
};

/* ------------------------------------------------------------- how to play */

/* One card of the guide: title and text on the left, a small drawing on the
 * right. Returns the drawing's area (320 x 200). */
static lv_obj_t *guide_card(lv_obj_t *list, int index, const char *title, const char *text, lv_obj_t **col)
{
    lv_obj_t *c = flex(list, LV_FLEX_FLOW_ROW, 32);
    lv_obj_add_style(c, &st_card, 0);
    lv_obj_set_style_pad_all(c, 28, 0);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    focusable(c, NULL);
    lv_obj_t *left = flex(c, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_flex_grow(left, 1);
    focus_text(c, label(left, f_l, TEXT, title));
    if (text)
        para(left, f_m, DIM, text);
    lv_obj_t *art = box(c);
    lv_obj_set_size(art, 320, 200);
    lv_obj_set_style_bg_color(art, lv_color_hex(SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(art, 16, 0);
    lv_obj_set_style_clip_corner(art, true, 0);
    u.items[index] = c;
    if (col)
        *col = left;
    return art;
}

static void place(lv_obj_t *o, int x, int y)
{
    lv_obj_set_pos(o, x, y);
}

/* How to play: shown on the first start and from the + menu. A short guide
 * in cards, each with a drawing; the buttons follow the user's mapping. */
void build_help(lv_obj_t *s)
{
    char buf[160];
    lv_obj_t *list = scroller(s, HEADER_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 18, 0);
    lv_obj_t *col, *art, *o;

    /* 1. In a game: the buttons, in two columns, and a dialogue box. */
    art = guide_card(list, 0, "In a game", NULL, &col);
    lv_obj_t *keys = flex(col, LV_FLEX_FLOW_ROW_WRAP, 14);
    lv_obj_set_width(keys, lv_pct(100));
    for (size_t i = 0; i < sizeof help_rows / sizeof help_rows[0]; i++) {
        lv_obj_t *row = flex(keys, LV_FLEX_FLOW_ROW, 10);
        lv_obj_set_width(row, 380);
        lv_obj_t *btns = flex(row, LV_FLEX_FLOW_ROW, 4);
        lv_obj_set_width(btns, 74);
        button_chip(btns, &g.in.cfg, key(help_rows[i].a));
        if (help_rows[i].b >= 0)
            button_chip(btns, &g.in.cfg, key(help_rows[i].b));
        /* The rest of the row, so a line the console's wider font cannot fit
         * wraps instead of being cut off (hardware run 28). */
        lv_obj_set_flex_grow(label(row, f_m, TEXT, help_rows[i].text), 1);
    }
    snprintf(buf, sizeof buf, "The %s stick moves the mouse cursor. Buttons follow your controller settings.",
             g.in.cfg.cursor_stick == RSL_STICK_RIGHT ? "right" : "left");
    para(col, f_s, DIM, buf);
    shape(art, 0, 0, 320, 200, tint(0), LV_OPA_40, 0);                 /* the scene */
    shape(art, 120, 30, 80, 110, ACCENT, LV_OPA_30, 40);                /* someone */
    o = shape(art, 16, 112, 288, 72, BG, LV_OPA_80, 12);                /* the text box */
    pill(art, ACCENT, "Eileen");
    place(lv_obj_get_child(art, lv_obj_get_child_count(art) - 1), 28, 96);
    shape(o, 14, 26, 200, 8, DIM, LV_OPA_COVER, 4);
    shape(o, 14, 44, 140, 8, DIM, LV_OPA_COVER, 4);
    place(button_chip(art, &g.in.cfg, key(RSL_ACT_CONFIRM)), 266, 146);

    /* 2. Full memory: the HOME menu, hold R on a game. */
    art = guide_card(list, 1, "Start with full memory",
                     "Hold R while you open any game from the HOME menu, and keep holding until the "
                     "Homebrew Menu appears. Then open the loader. Opened from the Album instead, the "
                     "loader has too little memory, and games do not start.", NULL);
    for (int k = 0; k < 3; k++) {
        o = shape(art, 30 + k * 92, 34, 76, 76, k ? tint(k + 1) : tint(0), LV_OPA_COVER, 10);
        if (!k) {
            lv_obj_set_style_outline_color(o, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_outline_width(o, 3, 0);
            lv_obj_set_style_outline_pad(o, 3, 0);
        }
    }
    o = flex(art, LV_FLEX_FLOW_ROW, 10);
    place(o, 30, 136);
    button_chip(o, &g.in.cfg, RSL_BTN_R);
    label(o, f_m, TEXT, "hold   +");
    button_chip(o, &g.in.cfg, RSL_BTN_A);

    /* 3. Adding games: computer, arrow, SD card. */
    art = guide_card(list, 2, "Adding games",
                     "Export each game with the desktop importer on your computer, then copy it to "
                     "switch/renpy-loader/games/ on the SD card. Its cover and details come with it.", NULL);
    o = shape(art, 24, 40, 130, 84, BG, LV_OPA_COVER, 8);              /* laptop screen */
    lv_obj_set_style_border_color(o, lv_color_hex(DIM), 0);
    lv_obj_set_style_border_width(o, 3, 0);
    shape(o, 14, 16, 70, 10, ACCENT, LV_OPA_COVER, 5);
    shape(o, 14, 36, 90, 8, DIM, LV_OPA_COVER, 4);
    shape(art, 10, 126, 158, 10, DIM, LV_OPA_COVER, 5);                /* its base */
    place(label(art, f_xl, ACCENT, "→"), 182, 64);
    o = shape(art, 232, 36, 66, 90, DIM, LV_OPA_COVER, 8);              /* SD card */
    shape(o, 48, -2, 20, 20, SURFACE_HI, LV_OPA_COVER, 0);              /* its notch */
    shape(o, 12, 56, 42, 22, BG, LV_OPA_60, 4);
    place(label(art, f_s, DIM, "games/"), 236, 140);

    /* 4. The + menu: layouts and colours. */
    art = guide_card(list, 3, "Make it yours",
                     "Press + in the library for the menu: in Layout, choose Row or Grid, "
                     "the game backgrounds and the accent colour; or test your controller.", NULL);
    for (int k = 0; k < 4; k++) {
        o = shape(art, 26 + k * 64 - (k == 1 ? 6 : 0), k == 1 ? 18 : 26, k == 1 ? 60 : 48, k == 1 ? 88 : 72,
                  tint(k), LV_OPA_COVER, 8);
        if (k == 1) {
            lv_obj_set_style_outline_color(o, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_outline_width(o, 3, 0);
            lv_obj_set_style_outline_pad(o, 2, 0);
        }
    }
    shape(art, 26, 124, 120, 12, ACCENT, LV_OPA_COVER, 6);
    for (int k = 0; k < 5; k++)
        shape(art, 26 + k * 40, 152, 26, 26, presets[k].rgb, LV_OPA_COVER, LV_RADIUS_CIRCLE);

    /* 5. Emergency combo: + and -, held for 3 s. */
    art = guide_card(list, 4, "Buttons stopped working?",
                     "Hold + and - together for 3 seconds, anywhere, in the loader or in a game: the "
                     "default controls come back, and they are saved.", NULL);
    o = flex(art, LV_FLEX_FLOW_ROW, 10);
    place(o, 30, 82);
    button_chip(o, &g.in.cfg, RSL_BTN_PLUS);
    label(o, f_m, TEXT, "+");
    button_chip(o, &g.in.cfg, RSL_BTN_MINUS);
    o = shape(art, 188, 46, 108, 108, BG, LV_OPA_TRANSP, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(o, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(o, 8, 0);
    lv_obj_center(label(o, f_l, TEXT, "3 s"));
    u.count = HELP_COUNT;
}

/* ------------------------------------------------------------------ layout */

/* A wireframe of layout l in a 328 x 200 box. White, not the accent: the
 * page follows a new accent live, without being rebuilt. */
#define SK(v) ((v) * box / 328)        /* the wireframe was drawn 328 px wide */

static void skeleton(lv_obj_t *p, int l, int box)
{
    if (l == LAYOUT_ROW) {   /* the focused game is named above the covers */
        shape(p, SK(16), SK(20), SK(130), SK(14), TEXT, LV_OPA_COVER, 7);
        shape(p, SK(16), SK(42), SK(50), SK(10), AMBER, LV_OPA_70, 5);
        shape(p, SK(72), SK(42), SK(50), SK(10), DIM, LV_OPA_70, 5);
    }
    for (int k = 0; k < (l == LAYOUT_GRID ? 10 : 6); k++) {
        int x, y, w = 44, h = 66;
        if (l == LAYOUT_GRID) {
            x = 18 + k % 5 * 60;
            y = 16 + k / 5 * 92;
        } else {
            x = 16 + k * 54;
            y = 114;
        }
        if (l == LAYOUT_ROW && k == 1) {
            x -= 4;
            y -= 6;
            w += 8;
            h += 12;
        }
        lv_obj_t *o = shape(p, SK(x), SK(y), SK(w), SK(h), tint(k), LV_OPA_COVER, 6);
        if (k == (l == LAYOUT_ROW ? 1 : 0)) {
            lv_obj_set_style_outline_color(o, lv_color_hex(TEXT), 0);
            lv_obj_set_style_outline_width(o, 2, 0);
            lv_obj_set_style_outline_pad(o, 2, 0);
        }
        if (l == LAYOUT_GRID)
            shape(p, SK(x), SK(y + h + 6), SK(36), SK(6), DIM, LV_OPA_COVER, 3);
    }
}

/* One list, items in g.sel order: the two layouts, the colour presets,
 * the custom hue row. */
void build_layout(lv_obj_t *s)
{
    lv_obj_t *list = scroller(s, HEADER_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);

    heading(list, "Library", 0);
    /* The two layouts side by side, each card split in half: its wireframe on
     * the left, its name and what it does on the right. Full width, and the
     * colour row still fits under them. */
    lv_obj_t *cards = flex(list, LV_FLEX_FLOW_ROW, 24);
    lv_obj_set_width(cards, lv_pct(100));
    make_room(cards);
    for (int l = 0; l < LAYOUT_COUNT; l++) {
        lv_obj_t *c = flex(cards, LV_FLEX_FLOW_ROW, 20);
        lv_obj_add_style(c, &st_card, 0);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_height(c, 150 + 2 * 20);   /* the wireframe and its padding: both cards alike */
        lv_obj_set_style_pad_all(c, 20, 0);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        focusable(c, NULL);
        lv_obj_add_style(c, &st_focus_fill, LV_STATE_FOCUSED);
        lv_obj_t *p = box(c);
        lv_obj_set_size(p, 246, 150);
        lv_obj_set_style_bg_color(p, lv_color_hex(BG), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(p, 12, 0);
        lv_obj_set_style_clip_corner(p, true, 0);
        skeleton(p, l, 246);
        lv_obj_t *col = flex(c, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_t *t = flex(col, LV_FLEX_FLOW_ROW_WRAP, 10);
        lv_obj_set_width(t, lv_pct(100));
        focus_text(c, label(t, f_l, TEXT, layout_name[l]));
        if (l == g.layout)
            pill(t, TEXT, "In use");
        para(col, f_s, DIM, layout_text[l]);
        u.items[l] = c;
    }

    lv_obj_t *row = flex(list, LV_FLEX_FLOW_ROW, 16);   /* the heading, and a warning beside it */
    heading(row, "Colour", 28);
    u.warn = label(row, f_s, AMBER, "Close to the red used for problems: focus and errors may look alike.");
    lv_obj_set_style_pad_top(u.warn, 28, 0);
    lv_obj_set_style_pad_bottom(u.warn, 8, 0);
    row = flex(list, LV_FLEX_FLOW_ROW, 30);
    make_room(row);
    lv_obj_set_style_pad_left(row, 8, 0);
    for (int i = 0; i < ACCENT_PRESETS; i++) {
        lv_obj_t *col = flex(row, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(col, 104);
        make_room(col);
        lv_obj_t *dot = shape(col, 0, 0, 64, 64, presets[i].rgb, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(dot, lv_color_hex(TEXT), 0);
        focusable(dot, &st_grow);
        focus_text(col, label(col, f_s, DIM, presets[i].name));
        u.swatch[i] = dot;
        u.items[SET_PRESET + i] = col;
    }

    lv_obj_t *custom = flex(list, LV_FLEX_FLOW_ROW, 28);
    lv_obj_set_size(custom, lv_pct(100), 64);
    lv_obj_add_style(custom, &st_row, 0);
    focusable(custom, NULL);
    lv_obj_add_style(custom, &st_focus_fill, LV_STATE_FOCUSED);
    focus_text(custom, label(custom, f_m, TEXT, "Custom hue"));
    lv_obj_t *strip = box(custom);
    lv_obj_set_size(strip, 720, 24);
    lv_obj_set_overflow_visible(strip, true);
    for (int k = 0; k < 24; k++)
        shape(strip, k * 30, 0, 30, 24, ui_hue_color(k * 15), LV_OPA_COVER, k == 0 || k == 23 ? 8 : 0);
    u.knob = shape(strip, 0, -6, 12, 36, TEXT, LV_OPA_COVER, 6);
    lv_obj_set_style_border_color(u.knob, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(u.knob, 3, 0);
    u.hue_text = label(custom, f_m, TEXT, "");
    u.items[SET_CUSTOM] = custom;
    u.count = SET_COUNT;
    u.accent_drawn = ~g.accent;
    u.hue_drawn = -1;
}

void update_layout(void)
{
    if (u.hue_drawn != g.accent_hue || u.accent_drawn != g.accent) {
        u.hue_drawn = g.accent_hue;
        u.accent_drawn = g.accent;
        lv_obj_set_x(u.knob, g.accent_hue * 720 / 360 - 6);
        set_textf(u.hue_text, "%d°", g.accent_hue);
        for (int i = 0; i < ACCENT_PRESETS; i++)
            lv_obj_set_style_border_width(u.swatch[i], presets[i].rgb == g.accent ? 4 : 0, 0);
        int d = (g.accent_hue - ERROR_HUE + 540) % 360 - 180;
        lv_obj_set_hidden(u.warn, d < -25 || d > 25 || g.accent != ui_hue_color(g.accent_hue));
    }
}
