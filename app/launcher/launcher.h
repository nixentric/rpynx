/* Internal to app/launcher: what the loader's files share.
 *   main.c      start-up, the display, the main loop and the "ui" log lines
 *   actions.c   input, and what each action does on each screen
 *   library.c   the game scan, manifests, the loader's settings, launching
 *   controls.c  controller settings: remapping, presets, saving
 * The screens themselves are app/ui. */
#ifndef RSL_LAUNCHER_H
#define RSL_LAUNCHER_H

#include <switch.h>

#include "loader.h"

#define LOADER_CFG   ROOT "/config/loader.json"
#define CAPTURE_MS   5000
#define COLS         5        /* grid layout: covers per row (app/ui) */
#define DEFAULT_ACCENT 0xff5fa8
#define DEFAULT_HUE  330

/* The loader's own state, beside g (loader.h), which the screens read. */
struct launcher {
    bool quit;
    char self_path[256];       /* this .nro, to come back to after a game */
    PadState pad;
    uint32_t pad_sig;
    volatile bool reset_pending;
    bool discard_armed;
    int lib_sel;               /* focused game, kept while away from the library */
    screen_id remap_back;      /* where back leaves controller settings for */
    bool help_seen;
    bool accent_dirty;         /* custom hue changed, saved when the page is left */
    int layout_col, accent_col; /* Layout page: last layout and preset focused (g.sel) */
    uint32_t back_since;
    bool back_held;
    uint32_t last_input;       /* for the "redrawing while idle" warning */
};
extern struct launcher m;

/* actions.c */
void status(status_kind kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
const char *pad_desc(void);
void go(screen_id s);
void go_library(void);
void frame(float dt);

/* library.c */
const char *title(int i);
void load_settings(void);
void save_settings(void);
void scan_games(void);
void chain_load(const char *nro, const char *args);
void launch(int i);

/* controls.c */
void open_remap(const char *path, const char *label, bool fallback_to_global, screen_id back);
void game_controls(int i, screen_id back);
void remap_capture_tick(void);
void remap_action(rsl_action a);

#endif
