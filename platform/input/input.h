/* Controller mapping engine: physical Switch buttons -> logical Ren'Py actions.
 *
 * Portable C99, no libnx dependency, so it builds and is unit-tested on the
 * host. The Switch-specific part (platform/input/input_switch.c) only fills
 * rsl_raw_input from libnx and hands it to rsl_input_update().
 *
 * Physical identities are Nintendo *labels* (A is the right face button,
 * B the bottom one). Nothing here goes through SDL's gamepad naming, which is
 * where Nintendo A/B usually gets swapped. */
#ifndef RSL_INPUT_H
#define RSL_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RSL_BTN_A, RSL_BTN_B, RSL_BTN_X, RSL_BTN_Y,
    RSL_BTN_L, RSL_BTN_R, RSL_BTN_ZL, RSL_BTN_ZR,
    RSL_BTN_PLUS, RSL_BTN_MINUS,
    RSL_BTN_DUP, RSL_BTN_DDOWN, RSL_BTN_DLEFT, RSL_BTN_DRIGHT,
    RSL_BTN_LSTICK, RSL_BTN_RSTICK,          /* stick clicks */
    RSL_BTN_SL, RSL_BTN_SR,                  /* Joy-Con rail buttons (either side) */
    /* Virtual buttons synthesised from the navigation stick after the deadzone. */
    RSL_BTN_NAV_UP, RSL_BTN_NAV_DOWN, RSL_BTN_NAV_LEFT, RSL_BTN_NAV_RIGHT,
    RSL_BTN_COUNT
} rsl_button;

typedef enum {
    RSL_ACT_CONFIRM,       /* A: confirm / advance dialogue */
    RSL_ACT_BACK,          /* B: back / cancel */
    RSL_ACT_HIDE_UI,       /* X: hide dialogue window */
    RSL_ACT_HISTORY,       /* Y: dialogue history */
    RSL_ACT_ROLLBACK,      /* L */
    RSL_ACT_ROLLFORWARD,   /* R */
    RSL_ACT_AUTO,          /* ZL: toggle auto-forward */
    RSL_ACT_SKIP,          /* ZR: skip while held */
    RSL_ACT_GAME_MENU,     /* + */
    RSL_ACT_QUICK_MENU,    /* - */
    RSL_ACT_UP, RSL_ACT_DOWN, RSL_ACT_LEFT, RSL_ACT_RIGHT,
    RSL_ACT_CLICK,         /* right stick press: virtual mouse click */
    RSL_ACT_OVERLAY,       /* left stick press: loader overlay */
    RSL_ACT_COUNT
} rsl_action;

typedef enum {
    RSL_MODE_PRESS,   /* one event on press */
    RSL_MODE_HOLD,    /* active for as long as it is held (skip) */
    RSL_MODE_REPEAT   /* press, then auto-repeat after a delay (navigation) */
} rsl_action_mode;

typedef enum { RSL_STICK_LEFT = 0, RSL_STICK_RIGHT = 1 } rsl_stick;

#define RSL_INPUT_VERSION 1
#define RSL_STICK_MAX 32767

typedef struct {
    uint32_t bindings[RSL_ACT_COUNT];  /* bitmask of rsl_button per action */
    float deadzone;                    /* 0..0.9, radial, on normalised stick */
    float cursor_speed;                /* screen widths per second at full tilt */
    float cursor_accel;                /* extra speed multiplier gained per second held */
    uint32_t repeat_delay_ms;
    uint32_t repeat_interval_ms;
    rsl_stick nav_stick;               /* stick that drives focus navigation */
    rsl_stick cursor_stick;            /* stick that drives the virtual mouse */
    bool cursor_visible;
} rsl_input_config;

/* One frame of normalised physical input. */
typedef struct {
    uint32_t buttons;                  /* bitmask of rsl_button (physical only) */
    int32_t stick_x[2], stick_y[2];    /* -32767..32767, +y is up (libnx convention) */
    bool touching;
    float touch_x, touch_y;            /* screen pixels */
} rsl_raw_input;

typedef enum {
    RSL_EV_ACTION_DOWN,
    RSL_EV_ACTION_UP,
    RSL_EV_ACTION_REPEAT,
    RSL_EV_EMERGENCY      /* fixed recovery combo fired; see rsl_input_update */
} rsl_event_type;

typedef struct {
    rsl_event_type type;
    rsl_action action;
} rsl_event;

#define RSL_MAX_EVENTS 64

/* Emergency recovery: hold + and - together for this long. Checked on raw
 * physical buttons before any mapping, so no config can disable it. */
#define RSL_EMERGENCY_BUTTONS ((1u << RSL_BTN_PLUS) | (1u << RSL_BTN_MINUS))
#define RSL_EMERGENCY_HOLD_MS 3000

typedef struct {
    rsl_input_config cfg;
    /* Outputs of the last update. Button masks include virtual nav buttons. */
    uint32_t buttons;                  /* effective buttons held */
    uint32_t pressed_buttons;          /* newly pressed this frame (for remap capture) */
    uint32_t active_actions;           /* bitmask of rsl_action currently held */
    float cursor_x, cursor_y;          /* normalised 0..1; backends may overwrite on touch */
    bool cursor_moved;
    rsl_event events[RSL_MAX_EVENTS];
    int event_count;
    float aspect;                      /* width/height of the cursor surface */
    /* Internal state. */
    uint32_t prev_buttons;
    uint32_t blocked_buttons;          /* held across a reset; ignored until released */
    uint32_t next_repeat_ms[RSL_ACT_COUNT];
    uint32_t emergency_since_ms;
    bool emergency_holding, emergency_fired;
    float cursor_held_s;
} rsl_input;

extern const char *const rsl_button_names[RSL_BTN_COUNT];
extern const char *const rsl_action_names[RSL_ACT_COUNT];

rsl_action_mode rsl_action_mode_of(rsl_action a);

typedef enum { RSL_PRESET_DEFAULT, RSL_PRESET_LEFT_HANDED, RSL_PRESET_ONE_HANDED_RIGHT, RSL_PRESET_COUNT } rsl_preset;
extern const char *const rsl_preset_names[RSL_PRESET_COUNT];
void rsl_config_preset(rsl_input_config *cfg, rsl_preset p);

/* Clamp numeric fields to sane ranges. Returns false if anything was clamped. */
bool rsl_config_sanitize(rsl_input_config *cfg);

/* Bitmask of actions sharing at least one button with another action. */
uint32_t rsl_config_conflicts(const rsl_input_config *cfg);

/* Assign `button` as the only binding of `action`. Returns bitmask of other
 * actions that also use `button` (callers show the conflict; nothing is
 * silently unbound). */
uint32_t rsl_config_bind(rsl_input_config *cfg, rsl_action action, rsl_button button);

/* Every action must stay reachable, otherwise the user could lock themselves
 * out of a menu. Returns false and sets *unbound to the first unbound action. */
bool rsl_config_complete(const rsl_input_config *cfg, rsl_action *unbound);

void rsl_input_init(rsl_input *in, const rsl_input_config *cfg);

/* Drop all held state, e.g. after resume or controller reconnect, so nothing
 * stays stuck and no repeat fires for a press made before the suspend. Held
 * physical buttons must be released before they count again. */
void rsl_input_reset(rsl_input *in, uint32_t currently_held);

/* Process one frame. now_ms is a monotonic clock, dt_s the frame time.
 * Fills in->events. When the emergency combo fires, the config is reset to
 * defaults in memory and an RSL_EV_EMERGENCY event is emitted; the caller
 * opens the overlay and decides whether to persist. */
void rsl_input_update(rsl_input *in, const rsl_raw_input *raw, uint32_t now_ms, float dt_s);

bool rsl_action_active(const rsl_input *in, rsl_action a);

/* Device input normaliser: libnx HidNpadButton bitmask -> rsl_button mask.
 * The bit positions are mirrored in input.c and checked against libnx's own
 * constants with static asserts in input_switch.c, so this stays host-testable. */
uint32_t rsl_normalize_npad(uint64_t npad_buttons);
extern const uint8_t rsl_npad_bit[RSL_BTN_NAV_UP];

/* Radial deadzone with rescale. Inputs -32767..32767, outputs -1..1. */
void rsl_stick_normalize(int32_t x, int32_t y, float deadzone, float *ox, float *oy);

/* Letterboxed fit of a game surface (gw x gh) into a screen (sw x sh). */
typedef struct { float x, y, w, h; } rsl_rect;
rsl_rect rsl_letterbox(float sw, float sh, float gw, float gh);

/* Screen pixel -> game pixel through the letterbox. Returns false if the
 * point is in the black bars. */
bool rsl_screen_to_game(rsl_rect view, float gw, float gh, float sx, float sy, float *gx, float *gy);

#endif
