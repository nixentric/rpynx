#include "input.h"

#include <math.h>
#include <string.h>

#define BIT(b) (1u << (b))

const char *const rsl_button_names[RSL_BTN_COUNT] = {
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Plus", "Minus",
    "DUp", "DDown", "DLeft", "DRight", "LStick", "RStick", "SL", "SR",
    "NavUp", "NavDown", "NavLeft", "NavRight",
};

const char *const rsl_action_names[RSL_ACT_COUNT] = {
    "confirm", "back", "hide_ui", "history", "rollback", "rollforward",
    "auto", "skip", "game_menu", "quick_menu", "up", "down", "left", "right",
    "click", "overlay",
};

const char *const rsl_preset_names[RSL_PRESET_COUNT] = {
    "default", "left_handed", "one_handed_right",
};

/* Actions that must stay bound so menus can always be operated. */
static const rsl_action essential[] = {
    RSL_ACT_CONFIRM, RSL_ACT_BACK, RSL_ACT_GAME_MENU, RSL_ACT_OVERLAY,
    RSL_ACT_UP, RSL_ACT_DOWN, RSL_ACT_LEFT, RSL_ACT_RIGHT,
};

#define NAV_THRESHOLD 0.5f
#define CURSOR_MAX_BOOST 4.0f

/* libnx HidNpadButton bit index for each rsl_button (see input_switch.c). */
const uint8_t rsl_npad_bit[RSL_BTN_NAV_UP] = {
    [RSL_BTN_A] = 0, [RSL_BTN_B] = 1, [RSL_BTN_X] = 2, [RSL_BTN_Y] = 3,
    [RSL_BTN_LSTICK] = 4, [RSL_BTN_RSTICK] = 5, [RSL_BTN_L] = 6, [RSL_BTN_R] = 7,
    [RSL_BTN_ZL] = 8, [RSL_BTN_ZR] = 9, [RSL_BTN_PLUS] = 10, [RSL_BTN_MINUS] = 11,
    [RSL_BTN_DLEFT] = 12, [RSL_BTN_DUP] = 13, [RSL_BTN_DRIGHT] = 14, [RSL_BTN_DDOWN] = 15,
    [RSL_BTN_SL] = 24,   /* LeftSL; RightSL (26) folded in below */
    [RSL_BTN_SR] = 25,   /* LeftSR; RightSR (27) folded in below */
};

uint32_t rsl_normalize_npad(uint64_t npad)
{
    uint32_t out = 0;
    for (int b = 0; b < RSL_BTN_NAV_UP; b++)
        if (npad & (1ull << rsl_npad_bit[b]))
            out |= BIT(b);
    if (npad & (1ull << 26)) out |= BIT(RSL_BTN_SL);
    if (npad & (1ull << 27)) out |= BIT(RSL_BTN_SR);
    /* Stick-direction bits (16-23) are ignored on purpose: the nav stick is
     * derived from analog values so the deadzone is configurable. */
    return out;
}

rsl_action_mode rsl_action_mode_of(rsl_action a)
{
    switch (a) {
    case RSL_ACT_SKIP:
        return RSL_MODE_HOLD;
    case RSL_ACT_UP: case RSL_ACT_DOWN: case RSL_ACT_LEFT: case RSL_ACT_RIGHT:
    case RSL_ACT_ROLLBACK: case RSL_ACT_ROLLFORWARD:
        return RSL_MODE_REPEAT;
    default:
        return RSL_MODE_PRESS;
    }
}

void rsl_config_preset(rsl_input_config *cfg, rsl_preset p)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->deadzone = 0.15f;
    cfg->cursor_speed = 0.6f;
    cfg->cursor_accel = 1.5f;
    cfg->repeat_delay_ms = 400;
    cfg->repeat_interval_ms = 90;
    cfg->cursor_visible = true;
    uint32_t *b = cfg->bindings;

    switch (p) {
    case RSL_PRESET_LEFT_HANDED:   /* mirror image of the default layout */
        b[RSL_ACT_CONFIRM] = BIT(RSL_BTN_DRIGHT);
        b[RSL_ACT_BACK] = BIT(RSL_BTN_DDOWN);
        b[RSL_ACT_HIDE_UI] = BIT(RSL_BTN_DUP);
        b[RSL_ACT_HISTORY] = BIT(RSL_BTN_DLEFT);
        b[RSL_ACT_ROLLBACK] = BIT(RSL_BTN_R);
        b[RSL_ACT_ROLLFORWARD] = BIT(RSL_BTN_L);
        b[RSL_ACT_AUTO] = BIT(RSL_BTN_ZR);
        b[RSL_ACT_SKIP] = BIT(RSL_BTN_ZL);
        b[RSL_ACT_GAME_MENU] = BIT(RSL_BTN_MINUS);
        b[RSL_ACT_QUICK_MENU] = BIT(RSL_BTN_PLUS);
        b[RSL_ACT_UP] = BIT(RSL_BTN_X) | BIT(RSL_BTN_NAV_UP);
        b[RSL_ACT_DOWN] = BIT(RSL_BTN_B) | BIT(RSL_BTN_NAV_DOWN);
        b[RSL_ACT_LEFT] = BIT(RSL_BTN_Y) | BIT(RSL_BTN_NAV_LEFT);
        b[RSL_ACT_RIGHT] = BIT(RSL_BTN_A) | BIT(RSL_BTN_NAV_RIGHT);
        b[RSL_ACT_CLICK] = BIT(RSL_BTN_LSTICK);
        b[RSL_ACT_OVERLAY] = BIT(RSL_BTN_RSTICK);
        cfg->nav_stick = RSL_STICK_RIGHT;
        cfg->cursor_stick = RSL_STICK_LEFT;
        break;

    case RSL_PRESET_ONE_HANDED_RIGHT:
        /* Right half of a handheld console only. The right stick navigates;
         * pointer input comes from the touchscreen. quick_menu, rollforward,
         * auto and click are left unbound: there are not enough buttons. */
        b[RSL_ACT_CONFIRM] = BIT(RSL_BTN_A);
        b[RSL_ACT_BACK] = BIT(RSL_BTN_B);
        b[RSL_ACT_HIDE_UI] = BIT(RSL_BTN_X);
        b[RSL_ACT_HISTORY] = BIT(RSL_BTN_Y);
        b[RSL_ACT_ROLLBACK] = BIT(RSL_BTN_R);
        b[RSL_ACT_SKIP] = BIT(RSL_BTN_ZR);
        b[RSL_ACT_GAME_MENU] = BIT(RSL_BTN_PLUS);
        b[RSL_ACT_UP] = BIT(RSL_BTN_NAV_UP);
        b[RSL_ACT_DOWN] = BIT(RSL_BTN_NAV_DOWN);
        b[RSL_ACT_LEFT] = BIT(RSL_BTN_NAV_LEFT);
        b[RSL_ACT_RIGHT] = BIT(RSL_BTN_NAV_RIGHT);
        b[RSL_ACT_OVERLAY] = BIT(RSL_BTN_RSTICK);
        cfg->nav_stick = RSL_STICK_RIGHT;
        cfg->cursor_stick = RSL_STICK_LEFT;
        cfg->cursor_visible = false;
        break;

    default:
        b[RSL_ACT_CONFIRM] = BIT(RSL_BTN_A);
        b[RSL_ACT_BACK] = BIT(RSL_BTN_B);
        b[RSL_ACT_HIDE_UI] = BIT(RSL_BTN_X);
        b[RSL_ACT_HISTORY] = BIT(RSL_BTN_Y);
        b[RSL_ACT_ROLLBACK] = BIT(RSL_BTN_L);
        b[RSL_ACT_ROLLFORWARD] = BIT(RSL_BTN_R);
        b[RSL_ACT_AUTO] = BIT(RSL_BTN_ZL);
        b[RSL_ACT_SKIP] = BIT(RSL_BTN_ZR);
        b[RSL_ACT_GAME_MENU] = BIT(RSL_BTN_PLUS);
        b[RSL_ACT_QUICK_MENU] = BIT(RSL_BTN_MINUS);
        b[RSL_ACT_UP] = BIT(RSL_BTN_DUP) | BIT(RSL_BTN_NAV_UP);
        b[RSL_ACT_DOWN] = BIT(RSL_BTN_DDOWN) | BIT(RSL_BTN_NAV_DOWN);
        b[RSL_ACT_LEFT] = BIT(RSL_BTN_DLEFT) | BIT(RSL_BTN_NAV_LEFT);
        b[RSL_ACT_RIGHT] = BIT(RSL_BTN_DRIGHT) | BIT(RSL_BTN_NAV_RIGHT);
        b[RSL_ACT_CLICK] = BIT(RSL_BTN_RSTICK);
        b[RSL_ACT_OVERLAY] = BIT(RSL_BTN_LSTICK);
        cfg->nav_stick = RSL_STICK_LEFT;
        cfg->cursor_stick = RSL_STICK_RIGHT;
        break;
    }
}

static bool clampf(float *v, float lo, float hi)
{
    if (!(*v >= lo)) { *v = lo; return false; }   /* also catches NaN */
    if (*v > hi) { *v = hi; return false; }
    return true;
}

static bool clampu(uint32_t *v, uint32_t lo, uint32_t hi)
{
    if (*v < lo) { *v = lo; return false; }
    if (*v > hi) { *v = hi; return false; }
    return true;
}

bool rsl_config_sanitize(rsl_input_config *cfg)
{
    bool ok = true;
    ok &= clampf(&cfg->deadzone, 0.0f, 0.9f);
    ok &= clampf(&cfg->cursor_speed, 0.05f, 5.0f);
    ok &= clampf(&cfg->cursor_accel, 0.0f, 10.0f);
    ok &= clampu(&cfg->repeat_delay_ms, 50, 2000);
    ok &= clampu(&cfg->repeat_interval_ms, 16, 1000);
    if (cfg->nav_stick != RSL_STICK_LEFT && cfg->nav_stick != RSL_STICK_RIGHT) {
        cfg->nav_stick = RSL_STICK_LEFT;
        ok = false;
    }
    rsl_stick other = cfg->nav_stick == RSL_STICK_LEFT ? RSL_STICK_RIGHT : RSL_STICK_LEFT;
    if (cfg->cursor_stick != other) {
        cfg->cursor_stick = other;
        ok = false;
    }
    for (int a = 0; a < RSL_ACT_COUNT; a++) {
        uint32_t valid = cfg->bindings[a] & (BIT(RSL_BTN_COUNT) - 1);
        if (valid != cfg->bindings[a]) {
            cfg->bindings[a] = valid;
            ok = false;
        }
    }
    return ok;
}

uint32_t rsl_config_conflicts(const rsl_input_config *cfg)
{
    uint32_t out = 0;
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        for (int b = a + 1; b < RSL_ACT_COUNT; b++)
            if (cfg->bindings[a] & cfg->bindings[b])
                out |= BIT(a) | BIT(b);
    return out;
}

uint32_t rsl_config_bind(rsl_input_config *cfg, rsl_action action, rsl_button button)
{
    cfg->bindings[action] = BIT(button);
    uint32_t clash = 0;
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        if (a != (int)action && (cfg->bindings[a] & BIT(button)))
            clash |= BIT(a);
    return clash;
}

bool rsl_config_complete(const rsl_input_config *cfg, rsl_action *unbound)
{
    for (size_t i = 0; i < sizeof essential / sizeof essential[0]; i++) {
        if (!cfg->bindings[essential[i]]) {
            if (unbound) *unbound = essential[i];
            return false;
        }
    }
    return true;
}

void rsl_stick_normalize(int32_t x, int32_t y, float deadzone, float *ox, float *oy)
{
    float fx = (float)x / RSL_STICK_MAX, fy = (float)y / RSL_STICK_MAX;
    float mag = sqrtf(fx * fx + fy * fy);
    if (mag <= deadzone || mag == 0.0f) {
        *ox = *oy = 0.0f;
        return;
    }
    float scaled = (fminf(mag, 1.0f) - deadzone) / (1.0f - deadzone);
    *ox = fx / mag * scaled;
    *oy = fy / mag * scaled;
}

void rsl_input_init(rsl_input *in, const rsl_input_config *cfg)
{
    memset(in, 0, sizeof *in);
    in->cfg = *cfg;
    rsl_config_sanitize(&in->cfg);
    in->cursor_x = in->cursor_y = 0.5f;
    in->aspect = 16.0f / 9.0f;
}

void rsl_input_reset(rsl_input *in, uint32_t currently_held)
{
    in->active_actions = 0;
    in->prev_buttons = 0;
    in->blocked_buttons = currently_held;
    in->cursor_held_s = 0.0f;
    in->emergency_holding = false;
    in->emergency_fired = (currently_held & RSL_EMERGENCY_BUTTONS) == RSL_EMERGENCY_BUTTONS;
    in->event_count = 0;
}

static void emit(rsl_input *in, rsl_event_type t, rsl_action a)
{
    if (in->event_count < RSL_MAX_EVENTS)
        in->events[in->event_count++] = (rsl_event){ t, a };
}

static uint32_t nav_buttons(const rsl_input *in, const rsl_raw_input *raw)
{
    float x, y;
    int s = in->cfg.nav_stick;
    rsl_stick_normalize(raw->stick_x[s], raw->stick_y[s], in->cfg.deadzone, &x, &y);
    if (fabsf(x) >= fabsf(y)) {
        if (x > NAV_THRESHOLD) return BIT(RSL_BTN_NAV_RIGHT);
        if (x < -NAV_THRESHOLD) return BIT(RSL_BTN_NAV_LEFT);
    } else {
        if (y > NAV_THRESHOLD) return BIT(RSL_BTN_NAV_UP);
        if (y < -NAV_THRESHOLD) return BIT(RSL_BTN_NAV_DOWN);
    }
    return 0;
}

void rsl_input_update(rsl_input *in, const rsl_raw_input *raw, uint32_t now_ms, float dt_s)
{
    in->event_count = 0;
    in->cursor_moved = false;
    uint32_t physical = raw->buttons & (BIT(RSL_BTN_NAV_UP) - 1);

    /* Emergency recovery, on raw buttons, before mapping. */
    bool combo = (physical & RSL_EMERGENCY_BUTTONS) == RSL_EMERGENCY_BUTTONS;
    if (!combo) {
        in->emergency_holding = false;
        in->emergency_fired = false;
    } else if (!in->emergency_holding) {
        in->emergency_holding = true;
        in->emergency_since_ms = now_ms;
    } else if (!in->emergency_fired && now_ms - in->emergency_since_ms >= RSL_EMERGENCY_HOLD_MS) {
        in->emergency_fired = true;
        for (int a = 0; a < RSL_ACT_COUNT; a++)
            if (in->active_actions & BIT(a))
                emit(in, RSL_EV_ACTION_UP, (rsl_action)a);
        in->active_actions = 0;
        rsl_config_preset(&in->cfg, RSL_PRESET_DEFAULT);
        emit(in, RSL_EV_EMERGENCY, RSL_ACT_OVERLAY);
    }
    /* While the combo is down, + and - do not also open game menus. */
    if (combo)
        physical &= ~RSL_EMERGENCY_BUTTONS;

    uint32_t buttons = physical | nav_buttons(in, raw);
    in->blocked_buttons &= buttons;          /* released buttons unblock */
    buttons &= ~in->blocked_buttons;
    in->pressed_buttons = buttons & ~in->prev_buttons;
    in->prev_buttons = buttons;
    in->buttons = buttons;

    for (int a = 0; a < RSL_ACT_COUNT; a++) {
        bool held = (buttons & in->cfg.bindings[a]) != 0;
        bool was = (in->active_actions & BIT(a)) != 0;
        if (held && !was) {
            in->active_actions |= BIT(a);
            emit(in, RSL_EV_ACTION_DOWN, (rsl_action)a);
            in->next_repeat_ms[a] = now_ms + in->cfg.repeat_delay_ms;
        } else if (!held && was) {
            in->active_actions &= ~BIT(a);
            emit(in, RSL_EV_ACTION_UP, (rsl_action)a);
        } else if (held && rsl_action_mode_of((rsl_action)a) == RSL_MODE_REPEAT &&
                   (int32_t)(now_ms - in->next_repeat_ms[a]) >= 0) {
            emit(in, RSL_EV_ACTION_REPEAT, (rsl_action)a);
            /* from now, not from the due time: a long frame stall must not
             * produce a burst of repeats */
            in->next_repeat_ms[a] = now_ms + in->cfg.repeat_interval_ms;
        }
    }

    float cx, cy;
    int s = in->cfg.cursor_stick;
    rsl_stick_normalize(raw->stick_x[s], raw->stick_y[s], in->cfg.deadzone, &cx, &cy);
    if (cx == 0.0f && cy == 0.0f) {
        in->cursor_held_s = 0.0f;
        return;
    }
    in->cursor_held_s += dt_s;
    float boost = fminf(1.0f + in->cfg.cursor_accel * in->cursor_held_s, CURSOR_MAX_BOOST);
    float step = in->cfg.cursor_speed * boost * dt_s;
    in->cursor_x = fminf(fmaxf(in->cursor_x + cx * step, 0.0f), 1.0f);
    /* normalised y covers fewer physical pixels; keep diagonal speed even */
    in->cursor_y = fminf(fmaxf(in->cursor_y - cy * step * in->aspect, 0.0f), 1.0f);
    in->cursor_moved = true;
}

bool rsl_action_active(const rsl_input *in, rsl_action a)
{
    return (in->active_actions & BIT(a)) != 0;
}

rsl_rect rsl_letterbox(float sw, float sh, float gw, float gh)
{
    /* Avoid a rounded scale factor so exact fits stay exact. */
    rsl_rect r = { 0, 0, sw, sh };
    if (sw * gh <= sh * gw)
        r.h = gh * sw / gw;
    else
        r.w = gw * sh / gh;
    r.x = (sw - r.w) / 2.0f;
    r.y = (sh - r.h) / 2.0f;
    return r;
}

bool rsl_screen_to_game(rsl_rect view, float gw, float gh, float sx, float sy, float *gx, float *gy)
{
    float u = (sx - view.x) / view.w, v = (sy - view.y) / view.h;
    *gx = fminf(fmaxf(u, 0.0f), 1.0f) * gw;
    *gy = fminf(fmaxf(v, 0.0f), 1.0f) * gh;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}
