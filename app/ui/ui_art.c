/* Covers and backdrops: decoded once into buffers so that drawing them is
 * a copy (grid thumbnails, the detail page's cover, the full-screen
 * backdrop), the placeholder tiles, and the glow for games without art. */
#include "ui_internal.h"

/* Kept for the whole run. */
static int covers_decoded, covers_failed;
static lv_obj_t *canvas;                 /* hidden; draws covers into their buffers */
lv_draw_buf_t *thumb[MAX_GAMES];  /* grid covers; games are scanned once at start */
static bool thumb_tried[MAX_GAMES];
static lv_draw_buf_t *big;               /* detail page cover */
static lv_draw_buf_t *backdrop;          /* Cinematic layout, full screen, made on first use */

const char *title_of(int i)
{
    return g.info[i].title[0] ? g.info[i].title : g.games[i];
}

/* Draw src into dst at area a, scaled by scale/256 from a's top left. */
void paint(lv_draw_buf_t *dst, const void *src, int32_t scale, const lv_area_t *a)
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

/* Round buf's corners (ARGB8888): what lies outside a RADIUS arc turns
 * transparent, the arc's edge partly (4x4 samples a pixel). Baked in black
 * instead, the corners hid the middle of a focused cover's outline arc, which
 * passes inside the cover's square, and showed as black over the Cinematic art. */
static void round_corners(lv_draw_buf_t *buf)
{
    int32_t w = buf->header.w, h = buf->header.h, r8 = RADIUS * 8;
    for (int32_t y = 0; y < RADIUS; y++)
        for (int32_t x = 0; x < RADIUS; x++) {
            int in = 0;   /* samples inside, in 1/8 px from the arc's centre */
            for (int s = 0; s < 16; s++) {
                int32_t dx = r8 - (x * 8 + s % 4 * 2 + 1), dy = r8 - (y * 8 + s / 4 * 2 + 1);
                in += dx * dx + dy * dy <= r8 * r8;
            }
            if (in == 16)
                continue;
            uint8_t opa = (uint8_t)(in * 255 / 16);
            const int32_t px[4][2] = { { x, y }, { w - 1 - x, y }, { x, h - 1 - y }, { w - 1 - x, h - 1 - y } };
            for (int k = 0; k < 4; k++)
                ((lv_color32_t *)(buf->data + px[k][1] * buf->header.stride))[px[k][0]].alpha = opa;
        }
}

/* Decode the JPEG at path once, scaled to fill buf (cropped to its aspect),
 * the corners rounded when asked, so that drawing it later is a copy.
 * LVGL's JPEG decoder keeps nothing: drawing the file itself would decode it
 * again on every frame of a scroll. */
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
            rsl_log("ui warning: image not shown (needs a baseline JPEG or a BMP, no bigger than 600x900): %s",
                    path);
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
    if (round)
        round_corners(buf);
    return true;
}

static void show_cover(lv_obj_t *c, lv_draw_buf_t *buf)
{
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);   /* its edge would show as a thin ring at the corners */
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
lv_obj_t *cover(lv_obj_t *parent, int i, int w, int h, lv_draw_buf_t *buf)
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

/* The detail page's big cover of game i, decoded now; NULL without one. */
lv_draw_buf_t *detail_cover(int i)
{
    return render_cover(big, g.info[i].cover, false) ? big : NULL;
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
        lv_draw_buf_t *b = lv_draw_buf_create(COVER_W, COVER_H, LV_COLOR_FORMAT_ARGB8888, 0);   /* round corners */
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
void backdrop_under(lv_obj_t *s)
{
    if (!backdrop) {
        backdrop = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_XRGB8888, 0);
        lv_draw_buf_clear(backdrop, NULL);
    }
    u.backdrop = lv_image_create(s);
    lv_image_set_src(u.backdrop, backdrop);
    lv_obj_move_to_index(u.backdrop, 0);
}

void ui_art_changed(int i)
{
    if (thumb[i])
        lv_draw_buf_destroy(thumb[i]);
    thumb[i] = NULL;
    thumb_tried[i] = false;
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

void art_init(void)
{
    canvas = lv_canvas_create(lv_layer_sys());
    lv_obj_set_hidden(canvas, true);
    big = lv_draw_buf_create(BIG_COVER_W, BIG_COVER_H, LV_COLOR_FORMAT_XRGB8888, 0);
}
