/* The picture page (Change picture): an Album screenshot, the cover's frame
 * on it, and what it becomes. */
#include "ui_internal.h"

static lv_draw_buf_t shot, part;         /* picture page: over g.shot, and what is cut from it */

static const char *const use_text[USE_COUNT][2] = {
    { "Use as cover", "The framed part, in the library and on the details" },
    { "Use as background", "The whole screenshot, behind the details" },
    { "Use for both", "Cover and background from this screenshot" },
};

/* Move the cover's frame, and the shade beside it, to g.crop_x. */
static void place_frame(void)
{
    int w = g.shot_h * 2 / 3;
    u.crop_drawn = g.crop_x;
    lv_obj_set_pos(u.frame, g.crop_x, 0);
    lv_obj_set_size(u.frame, w, H);
    lv_obj_set_width(u.shade[0], g.crop_x);
    lv_obj_set_pos(u.shade[1], g.crop_x + w, 0);
    lv_obj_set_width(u.shade[1], W - g.crop_x - w);
}

/* An Album screenshot at its own size (the screen's), the part outside the
 * cover's frame shaded, dark bands behind the header and the footer; the
 * choice of what it becomes slides in like the + menu. */
void build_picture(lv_obj_t *s)
{
    lv_draw_buf_init(&shot, g.shot_w, g.shot_h, LV_COLOR_FORMAT_XRGB8888, g.shot_w * 4, g.shot,
                     (uint32_t)(g.shot_w * g.shot_h * 4));
    u.backdrop = lv_image_create(s);
    lv_image_set_src(u.backdrop, &shot);
    u.shade[0] = shape(s, 0, 0, 0, H, BG, LV_OPA_60, 0);
    u.shade[1] = shape(s, 0, 0, 0, H, BG, LV_OPA_60, 0);
    u.frame = box(s);
    lv_obj_set_style_border_color(u.frame, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(u.frame, 3, 0);
    lv_obj_set_style_radius(u.frame, RADIUS, 0);
    lv_obj_t *top = shape(s, 0, 0, W, HEADER_H, BG, LV_OPA_70, 0);
    lv_obj_t *bottom = shape(s, 0, H - FOOTER_H, W, FOOTER_H, BG, LV_OPA_70, 0);
    lv_obj_t *under[] = { u.backdrop, u.shade[0], u.shade[1], top, bottom, u.frame };
    for (int k = 0; k < 6; k++)   /* under the header and the footer screen() made */
        lv_obj_move_to_index(under[k], k);
    place_frame();
    u.shot_drawn = g.shot_gen;

    char buf[120];
    snprintf(buf, sizeof buf, "Picture for %s", title_of(g.game));
    lv_obj_t *t = label(u.head, f_l, TEXT, buf);
    lv_obj_set_width(t, 760);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    u.shot_text = label(u.chips, f_m, TEXT, "");
    update_picture();
    side_menu(s, "Use this screenshot", use_text, USE_COUNT, -1, NULL);
}

void update_picture(void)
{
    if (u.shot_drawn != g.shot_gen) {   /* a new screenshot in the same buffer */
        u.shot_drawn = g.shot_gen;
        lv_obj_invalidate(u.backdrop);
    }
    if (u.crop_drawn != g.crop_x)
        place_frame();
    set_textf(u.shot_text, "%d of %d%s%s", g.sel + 1, g.shot_count, g.shot_date[0] ? "   " : "", g.shot_date);
}

void ui_shot_part(int x, int y, int w, int h, uint32_t *out, int ow, int oh)
{
    int32_t scale = ow * 256 / w;
    lv_area_t a = { -x * scale / 256, -y * scale / 256, 0, 0 };
    a.x2 = a.x1 + g.shot_w - 1;
    a.y2 = a.y1 + g.shot_h - 1;
    lv_draw_buf_init(&shot, g.shot_w, g.shot_h, LV_COLOR_FORMAT_XRGB8888, g.shot_w * 4, g.shot,
                     (uint32_t)(g.shot_w * g.shot_h * 4));
    lv_draw_buf_init(&part, ow, oh, LV_COLOR_FORMAT_XRGB8888, ow * 4, out, (uint32_t)(ow * oh * 4));
    paint(&part, &shot, scale, &a);
    (void)h;   /* the same aspect: the scale follows from the widths */
}
