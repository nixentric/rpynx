/* Fonts, shared styles and the accent colour: set up once by style_init(),
 * the accent restyled by set_accent() when it changes. */
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
char stick_icon[2][4];
lv_style_t st_screen, st_card, st_pill, st_focusable, st_focus, st_grow, st_grow_row, st_row,
           st_focus_fill, st_button, st_chip, st_toggle, st_toggle_on, st_title_focus, st_icon_on;
static lv_style_transition_dsc_t trans;

const struct accent_preset presets[ACCENT_PRESETS] = {
    { "Pink", 0xff5fa8 }, { "Blue", 0x25d0f2 }, { "Violet", 0xa78bfa }, { "Indigo", 0x818cf8 },
    { "Teal", 0x2dd4bf }, { "Lime", 0xa3e635 }, { "Sky", 0x60a5fa }, { "White", 0xe5e7eb },
};

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

/* The shared styles that carry the accent colour. Elsewhere the screen is
 * rebuilt after a change, which recolours the rest; the accent page itself
 * only follows the shared styles, so the colour changes live as it is picked. */
void set_accent(void)
{
    lv_style_set_outline_color(&st_focusable, lv_color_hex(ACCENT));
    lv_style_set_bg_color(&st_toggle_on, lv_color_hex(ACCENT));
    lv_style_set_text_color(&st_title_focus, lv_color_hex(ACCENT));
    lv_style_set_text_color(&st_icon_on, lv_color_hex(ACCENT));
    lv_style_set_bg_color(&st_focus_fill, lv_color_mix(lv_color_hex(ACCENT), lv_color_hex(SURFACE), LV_OPA_10));
    lv_obj_report_style_change(NULL);
}

/* Fonts from the console's TrueType data (NULL: Montserrat 14 and drawn
 * chips, the host), then every shared style. */
void style_init(const void *ttf, size_t ttf_len, const void *icons, size_t icons_len)
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
