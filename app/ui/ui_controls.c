/* Controller settings, controller test and diagnostics. */
#include "ui_internal.h"

static const char *const action_short[RSL_ACT_COUNT] = {
    "Confirm", "Back", "Hide UI", "History", "Rollback", "Roll fwd", "Auto", "Skip",
    "Game menu", "Quick menu", "Up", "Down", "Left", "Right", "Click", "Overlay",
};

static const char *const setting_label[ROW_COUNT - RSL_ACT_COUNT] = {
    "Stick deadzone", "Cursor speed", "Cursor acceleration", "Repeat delay",
    "Repeat interval", "Sticks", "Cursor visible in games",
};


void build_remap(lv_obj_t *s)
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
            heading(list, r ? "Sticks and cursor" : "Buttons", r ? 28 : 0);
        lv_obj_t *row = box(list);
        lv_obj_set_size(row, lv_pct(100), 54);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_style(row, &st_row, 0);
        focusable(row, NULL);
        lv_obj_add_style(row, &st_focus_fill, LV_STATE_FOCUSED);
        focus_text(row, label(row, f_m, TEXT, r < RSL_ACT_COUNT ? action_label[r] : setting_label[r - RSL_ACT_COUNT]));
        u.values[r] = r < RSL_ACT_COUNT ? flex(row, LV_FLEX_FLOW_ROW, 10) : label(row, f_m, TEXT, "");
        u.items[r] = row;
    }
    u.count = ROW_COUNT;
    memset(u.row_sig, 0xff, sizeof u.row_sig);
}

void update_remap(void)
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
                        button_chip(u.values[r], c, b);
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
        set_textf(u.values[r], "‹   %s   ›", v);
    }
}

/* ---------------------------------------------------------- controller test */

static lv_obj_t *toggle(lv_obj_t *parent, const char *text, int width)
{
    lv_obj_t *t = label(parent, f_s, TEXT, text);
    lv_obj_add_style(t, &st_toggle, 0);
    lv_obj_add_style(t, &st_toggle_on, LV_STATE_CHECKED);
    lv_obj_set_width(t, width);
    return t;
}

static void stick(lv_obj_t *parent, const char *name, int s, int x)
{
    lv_obj_t *col = flex(parent, LV_FLEX_FLOW_COLUMN, 8);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_pos(col, x, 0);
    lv_obj_set_width(col, 180);
    label(col, f_s, DIM, name);
    lv_obj_t *ring = box(col);
    lv_obj_set_size(ring, 140, 140);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, lv_color_hex(SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    u.dot[s] = shape(ring, 0, 0, 22, 22, ACCENT, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_obj_center(u.dot[s]);
    u.stick_val[s] = label(col, f_s, TEXT, "");
}

/* Where each button sits, roughly as on the controller: shoulder buttons
 * along the top, the D-pad and A/B/X/Y as crosses, the sticks between. */
static const struct { int8_t b; int16_t x, y; } pad_map[] = {
    { RSL_BTN_ZL, 10, 4 }, { RSL_BTN_L, 90, 4 }, { RSL_BTN_MINUS, 240, 4 },
    { RSL_BTN_DUP, 100, 76 }, { RSL_BTN_DLEFT, 46, 126 }, { RSL_BTN_DRIGHT, 154, 126 },
    { RSL_BTN_DDOWN, 100, 176 }, { RSL_BTN_LSTICK, 240, 126 }, { RSL_BTN_SL, 10, 216 }, { RSL_BTN_SR, 90, 216 },
    { RSL_BTN_PLUS, 856, 4 }, { RSL_BTN_R, 990, 4 }, { RSL_BTN_ZR, 1070, 4 },
    { RSL_BTN_X, 990, 76 }, { RSL_BTN_Y, 936, 126 }, { RSL_BTN_A, 1044, 126 }, { RSL_BTN_B, 990, 176 },
    { RSL_BTN_RSTICK, 856, 126 },
};

void build_test(lv_obj_t *s)
{
    u.pad = pill(u.chips, ACCENT, "");
    lv_obj_t *c = card(s, NULL, MARGIN, BODY_Y, W - 2 * MARGIN, 290);
    lv_obj_set_layout(c, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(c, 16, 0);
    for (size_t k = 0; k < sizeof pad_map / sizeof pad_map[0]; k++) {
        int b = pad_map[k].b;
        lv_obj_t *o;
        if (icon[b][0] && f_icon_big) {
            o = label(c, f_icon_big, DIM, icon[b]);
            lv_obj_add_style(o, &st_icon_on, LV_STATE_CHECKED);
        } else {
            o = toggle(c, glyph(&g.in.cfg, b), 60);
        }
        lv_obj_set_pos(o, pad_map[k].x, pad_map[k].y);
        u.btn[b] = o;
    }
    stick(c, "Left stick", 0, 400);
    stick(c, "Right stick", 1, 620);

    int y = BODY_Y + 306, h = BODY_H - 306;
    c = card(s, "Actions", MARGIN, y, 760, h);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_t *wrap = flex(c, LV_FLEX_FLOW_ROW_WRAP, 6);
    lv_obj_set_width(wrap, lv_pct(100));
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        u.act[a] = toggle(wrap, action_short[a], 176);
    c = card(s, NULL, MARGIN + 776, y, W - 2 * MARGIN - 776, h);
    lv_obj_set_style_pad_all(c, 16, 0);
    u.events = label(c, f_s, TEXT, "");
}

void update_test(void)
{
    char buf[512];
    set_text(u.pad, g.pad_connected ? g.pad_desc : "No controller");
    for (int b = 0; b < RSL_BTN_NAV_UP; b++)
        if (u.btn[b])
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
    int n = g.raw.touching ? snprintf(buf, sizeof buf, "Touch %.0f, %.0f", (double)g.raw.touch_x,
                                      (double)g.raw.touch_y)
                           : snprintf(buf, sizeof buf, "Touch -");
    n += snprintf(buf + n, sizeof buf - n, "    Cursor %.2f, %.2f%s", (double)g.in.cursor_x,
                  (double)g.in.cursor_y, g.in.cursor_moved ? "  moving" : "");
    for (int i = g.evcount > 6 ? g.evcount - 6 : 0; i < g.evcount && n < (int)sizeof buf; i++)
        n += snprintf(buf + n, sizeof buf - n, "\n%s", g.evlog[i]);
    set_text(u.events, buf);
}

/* ------------------------------------------------------------- diagnostics */

void build_diag(lv_obj_t *s)
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
    u.mem_bar = meter(v, 180);
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

void update_diag(void)
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
