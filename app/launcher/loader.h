/* Loader state, shared by the logic (main.c) and the screens (app/ui/ui.c).
 * No libnx types, so the screens also build on the host (scripts/mockups.sh). */
#ifndef RSL_LOADER_H
#define RSL_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "input.h"
#include "input_config.h"

#ifndef RSL_VERSION
#define RSL_VERSION "dev"
#endif

#define ROOT        "sdmc:/switch/renpy-loader"
#define GAMES_DIR   ROOT "/games"
#define CTRL_DIR    ROOT "/config/controller"
#define GLOBAL_CFG  CTRL_DIR "/global.json"
#define RUNTIME_NRO ROOT "/runtimes/renpy-runtime.nro"
#define MAX_GAMES   64

typedef enum { SCR_LIBRARY, SCR_DETAIL, SCR_REMAP, SCR_TEST, SCR_DIAG, SCR_HELP } screen_id;

/* Rows of the controller settings screen: one per action, then these. */
enum { ROW_DEADZONE = RSL_ACT_COUNT, ROW_SPEED, ROW_ACCEL, ROW_DELAY, ROW_INTERVAL,
       ROW_STICKS, ROW_CURSOR_VIS, ROW_COUNT };

enum { MENU_REMAP, MENU_TEST, MENU_DIAG, MENU_LAYOUT, MENU_ACCENT, MENU_HELP, MENU_EXIT, MENU_COUNT };   /* + menu */
enum { LAYOUT_ROW, LAYOUT_GRID, LAYOUT_CINEMA };   /* library layouts, see docs/LOADER_UI.md */
enum { THEME_PINK, THEME_BLUE };    /* accent colour; nothing else changes */
enum { DETAIL_LAUNCH, DETAIL_CONTROLS, DETAIL_COUNT };              /* detail buttons */

typedef enum { ST_INFO, ST_OK, ST_BAD } status_kind;

struct game_info {
    char title[96];            /* manifest title, "" if none (the folder name is shown) */
    char cover[200];           /* cover JPEG path, "" for a placeholder */
    char backdrop[200];        /* 16:9 menu art JPEG for the Cinematic layout, "" if none */
    char renpy[24];            /* Ren'Py version from manifest.json, "" if none */
    char state[16];            /* experimental / unsupported; "" without manifest */
    char runtime[49];          /* runtime the game needs */
    char nro[160];             /* where that runtime would be */
    char problem[160];         /* why it must not start, "" if it may */
    int issues;                /* performance.issues from the importer */
    char issue[3][160];        /* the first three of them */
    bool recompile, runtime_ok;
};

typedef struct {
    screen_id screen;
    int layout;                    /* LAYOUT_ROW / LAYOUT_GRID, saved in config/loader.json */
    int accent;                    /* THEME_PINK / THEME_BLUE, saved there too */
    int sel;                       /* focused item on the current screen */
    int game;                      /* game of the detail page / per-game controls */
    bool menu_open;                /* the + menu, over the library */
    bool dialog_open;              /* applet-mode notice: games do not start in applet mode */
    int menu_sel;
    char status[200];
    status_kind status_kind;
    uint32_t now;

    rsl_input in;
    rsl_raw_input raw;

    char games[MAX_GAMES][64];     /* folder names under games/ */
    bool game_ok[MAX_GAMES];       /* has a game/ folder */
    struct game_info info[MAX_GAMES];
    int game_count;

    rsl_input_config edit;         /* controller settings being edited */
    char edit_path[256];
    char edit_label[80];
    int edit_preset;
    bool edit_dirty;
    int capture;                   /* action being captured, -1 if none */
    uint32_t capture_deadline;

    char evlog[8][40];             /* controller test: recent events */
    int evcount;

    /* Facts about the console, filled in by main.c. */
    bool sd_ok, full_memory, pad_connected, runtime_found;
    rsl_load_result global_load;
    const char *applet_type, *mode, *pad_desc;
    uint64_t mem_used, mem_total;
} loader_state;

extern loader_state g;

#endif
