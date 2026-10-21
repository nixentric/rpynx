/* Internal to app/ui: what its files share, grouped below by the file that
 * defines it. ui_controls.c has controller settings, controller test and
 * diagnostics; ui_pages.c how to play and the Layout page. */
#ifndef RSL_UI_INTERNAL_H
#define RSL_UI_INTERNAL_H

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
#define COVER_W 220          /* portrait 2:3 like GOG box art; five per row in the grid */
#define COVER_H 330
#define BIG_COVER_W 300      /* detail page; the importer writes covers at this size */
#define BIG_COVER_H 450
#define COVER_MAX_PX (600 * 900)  /* larger JPEGs are not decoded: too slow on the CPU */
#define GAP 21               /* grid: 5 * 220 + 4 * 21 = 1184, the width between the margins */
#define ROW_GAP 28           /* row layout: the focused cover grows 12 % into this */
#define ROW_ROOM 28
#define ROW_Y (HEADER_H + 20)
#define ROOM 12              /* space a focused item's outline and scale need */
#define RADIUS 20            /* covers: well rounded, like cards in a game store */
#define ICON_PX 26           /* height of a button icon next to text */
#define TEST_ICON_PX 44      /* controller test */

/* OLED dark: pure black, near-black cards with a hairline edge, the accent
 * for focus. Errors are orange-red so they never read as "selected". */
#define BG         0x000000
#define SURFACE    0x121215
#define SURFACE_HI 0x1f1f25
#define LINE       0x26262d
#define TEXT       0xf0f0f3
#define DIM        0x9b9ba4
#define ACCENT     g.accent  /* + menu, Accent colour */
#define RED        0xff4b2b
#define AMBER      0xffb547
#define GREEN      0x52d48a
#define ERROR_HUE  10        /* RED's hue: custom accents near it get a warning */

typedef struct {
    int a, b;            /* actions whose live buttons are shown, -1 for none */
    const char *text;
} hint;
#define HINTS(h) h, (int)(sizeof h / sizeof h[0])

struct accent_preset { const char *name; uint32_t rgb; };

/* Objects of the current screen, reset when it is rebuilt. */
struct ui_screen {
    lv_obj_t *scr, *head, *chips, *status_line, *hints;
    const char *note;                    /* footer text while there is no status */
    screen_id screen;
    int layout, info_sel;
    uint32_t accent;
    bool fresh;
    const hint *hint;
    int hint_n;
    uint32_t hint_cfg[RSL_ACT_COUNT];
    lv_obj_t *items[MAX_GAMES];          /* focusable items, indexed like g.sel */
    int count, focused;
    lv_obj_t *menu, *menu_panel, *menu_items[MENU_COUNT], *dialog, *dialog_ok;
    int menu_focused, dialog_focused;
    bool menu_shown, dialog_shown, status_set;
    char status[sizeof g.status];
    status_kind status_kind;
    lv_obj_t *covers[MAX_GAMES], *info, *backdrop;                /* library */
    int backdrop_sel, sel_seen;
    uint32_t sel_at;
    lv_obj_t *shade[2], *frame, *shot_text;                       /* picture page */
    int crop_drawn;
    unsigned shot_drawn;
    lv_obj_t *values[ROW_COUNT], *capture, *profile, *preset, *unsaved;  /* remap */
    uint32_t row_sig[RSL_ACT_COUNT];
    lv_obj_t *btn[RSL_BTN_NAV_UP], *act[RSL_ACT_COUNT], *dot[2], *stick_val[2], *events, *pad;
    int dot_x[2], dot_y[2];
    lv_obj_t *mem_bar, *mem_text, *log[RSL_LOG_KEEP];             /* diagnostics */
    lv_obj_t *swatch[ACCENT_PRESETS], *knob, *hue_text, *warn, *bg_value;   /* layout */
    uint32_t accent_drawn;
    int hue_drawn, bg_drawn;
};

/* ui.c: the screen frame, building and updating screens */
extern struct ui_screen u;
extern const char *const action_label[RSL_ACT_COUNT];
extern const char *const preset_label[RSL_PRESET_COUNT];
extern const char *const layout_name[LAYOUT_COUNT];
lv_obj_t *screen(const char *title, const hint *h, int n);

/* ui_style.c: fonts, shared styles, the accent */
extern const lv_font_t *f_s, *f_m, *f_l, *f_xl, *f_hero;
extern const lv_font_t *f_icon, *f_icon_big;     /* the console's button icons; NULL on the host */
extern char icon[RSL_BTN_NAV_UP][4];              /* UTF-8 of each button's icon, "" if none */
extern char stick_icon[2][4];                     /* the left and right stick's */
extern lv_style_t st_screen, st_card, st_pill, st_focusable, st_focus, st_grow, st_grow_row, st_row,
                  st_focus_fill, st_button, st_chip, st_toggle, st_toggle_on, st_title_focus, st_icon_on;
extern const struct accent_preset presets[ACCENT_PRESETS];
void style_init(const void *ttf, size_t ttf_len, const void *icons, size_t icons_len);
void set_accent(void);

/* ui_widgets.c: building blocks, touch */
lv_obj_t *box(lv_obj_t *parent);
lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap);
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *para(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa, int radius);
void set_text(lv_obj_t *l, const char *s);
void set_textf(lv_obj_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
lv_obj_t *pill(lv_obj_t *parent, uint32_t color, const char *text);
void focusable(lv_obj_t *o, lv_style_t *grow);
void focus_text(lv_obj_t *row, lv_obj_t *text);
lv_obj_t *button(lv_obj_t *parent, const char *text, int width);
lv_obj_t *card(lv_obj_t *parent, const char *title, int x, int y, int w, int h);
lv_obj_t *fact(lv_obj_t *parent, const char *key);
lv_obj_t *note(lv_obj_t *parent, uint32_t color, const char *text);
void heading(lv_obj_t *list, const char *text, int above);
void scrolls(lv_obj_t *o, lv_dir_t dir);
lv_obj_t *scroller(lv_obj_t *parent, int y);
lv_obj_t *overlay(lv_obj_t *parent);
const char *glyph(const rsl_input_config *c, int b);
int key(int a);
lv_obj_t *button_chip(lv_obj_t *parent, const rsl_input_config *c, int b);
void hints(lv_obj_t *row, const hint *h, int n);
void make_room(lv_obj_t *o);
void tappable_item(lv_obj_t *o, int i);

/* ui_art.c: covers and backdrops */
extern lv_draw_buf_t *thumb[MAX_GAMES];           /* grid covers, NULL until decoded or without one */
const char *title_of(int i);
uint32_t tint(int i);
void paint(lv_draw_buf_t *dst, const void *src, int32_t scale, const lv_area_t *a);
lv_obj_t *cover(lv_obj_t *parent, int i, int w, int h, lv_draw_buf_t *buf);
lv_draw_buf_t *detail_cover(int i);
void load_next_thumb(void);
void render_backdrop(int i);
void backdrop_under(lv_obj_t *s);
void art_init(void);

/* ui_library.c: the library layouts, the + menu */
void side_menu(lv_obj_t *s, const char *title, const char *const (*text)[2], int n, int dyn,
               const char *dyn_sub);
void status_pill(lv_obj_t *parent, const struct game_info *gi);
void row_info(void);
void build_library(lv_obj_t *s);

/* ui_detail.c */
void build_detail(lv_obj_t *s);

/* ui_picture.c */
void build_picture(lv_obj_t *s);
void update_picture(void);

/* ui_controls.c */
void build_remap(lv_obj_t *s);
void update_remap(void);
void build_test(lv_obj_t *s);
void update_test(void);
void build_diag(lv_obj_t *s);
void update_diag(void);

/* ui_pages.c */
void build_help(lv_obj_t *s);
void build_layout(lv_obj_t *s);
void update_layout(void);

#endif
