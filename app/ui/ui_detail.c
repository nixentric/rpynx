/* Game details: the facts beside the big cover, the buttons that stay at
 * the bottom right, and the applet-mode notice. */
#include "ui_internal.h"

static const hint dialog_hints[] = { { RSL_ACT_CONFIRM, -1, "OK" }, { RSL_ACT_BACK, -1, "Back" } };

static const hint detail_scroll_hints[] = {
    { RSL_ACT_UP, RSL_ACT_DOWN, "Scroll" }, { RSL_ACT_CONFIRM, -1, "Select" }, { RSL_ACT_BACK, -1, "Back" },
};

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

    lv_obj_t *c = cover(s, i, BIG_COVER_W, BIG_COVER_H, detail_cover(i));
    lv_obj_set_style_clip_corner(c, true, 0);   /* the backdrop shows around the corners */
    lv_obj_set_pos(c, MARGIN, BODY_Y);

    /* Bottom right, whatever the length of the facts: the buttons, Launch last. */
    lv_obj_t *row = flex(s, LV_FLEX_FLOW_ROW, 20);
    make_room(row);
    u.items[DETAIL_PICTURE] = button(row, "Change picture", 240);
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
