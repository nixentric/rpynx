/* Input and navigation: each frame's controller input through the mapping
 * engine, and what each action does on each screen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "input.h"
#include "input_switch.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"

void status(status_kind kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof g.status, fmt, ap);
    va_end(ap);
    g.status_kind = kind;
    rsl_log("%s", g.status);
}

const char *pad_desc(void)
{
    u32 s = padGetStyleSet(&m.pad);
    return s & HidNpadStyleTag_NpadHandheld ? "Handheld" :
           s & HidNpadStyleTag_NpadFullKey ? "Pro Controller" :
           s & HidNpadStyleTag_NpadJoyDual ? "Joy-Con pair" :
           s & HidNpadStyleTag_NpadJoyLeft ? "Left Joy-Con" :
           s & HidNpadStyleTag_NpadJoyRight ? "Right Joy-Con" : "Controller";
}

/* Every screen opens with its first item focused. */
void go(screen_id s)
{
    g.screen = s;
    g.sel = 0;
    g.capture = -1;
    g.status[0] = '\0';
    g.menu_open = g.dialog_open = false;
}

void go_library(void)
{
    go(SCR_LIBRARY);
    g.sel = m.lib_sel < g.game_count ? m.lib_sel : 0;
}

static void menu_action(rsl_action a)
{
    switch (a) {
    case RSL_ACT_UP: g.menu_sel = (g.menu_sel + MENU_COUNT - 1) % MENU_COUNT; break;
    case RSL_ACT_DOWN: g.menu_sel = (g.menu_sel + 1) % MENU_COUNT; break;
    case RSL_ACT_BACK:
    case RSL_ACT_GAME_MENU: g.menu_open = false; break;
    case RSL_ACT_CONFIRM:
        m.lib_sel = g.sel;
        switch (g.menu_sel) {
        case MENU_REMAP: open_remap(GLOBAL_CFG, "Global", false, SCR_LIBRARY); break;
        case MENU_TEST: go(SCR_TEST); g.evcount = 0; break;
        case MENU_DIAG: go(SCR_DIAG); break;
        case MENU_LAYOUT:
            go(SCR_LAYOUT);
            g.sel = m.layout_col = g.layout;
            m.accent_col = SET_PRESET;
            break;
        case MENU_HELP: go(SCR_HELP); break;
        default: m.quit = true; break;
        }
        break;
    default:
        break;
    }
}

/* Row: one line, left/right. Grid: COLS per row. Both stop at
 * their edges. */
static void library_action(rsl_action a)
{
    int n = g.game_count, step = g.layout == LAYOUT_GRID ? COLS : 0;
    if (g.menu_open) {
        menu_action(a);
        return;
    }
    if (a == RSL_ACT_GAME_MENU) {
        g.menu_open = true;
        g.menu_sel = 0;
        return;
    }
    if (!n)
        return;
    switch (a) {
    case RSL_ACT_LEFT: if (g.sel > 0) g.sel--; break;
    case RSL_ACT_RIGHT: if (g.sel < n - 1) g.sel++; break;
    case RSL_ACT_UP: if (step && g.sel >= step) g.sel -= step; break;
    case RSL_ACT_DOWN:
        if (step && g.sel / step < (n - 1) / step)
            g.sel = g.sel + step < n ? g.sel + step : n - 1;
        break;
    case RSL_ACT_CONFIRM:
        m.lib_sel = g.game = g.sel;
        go(SCR_DETAIL);
        break;
    case RSL_ACT_HIDE_UI:
        m.lib_sel = g.sel;
        game_controls(g.sel, SCR_LIBRARY);
        break;
    default:
        break;
    }
}

/* The Layout page: left/right within a line (on the hue row: the hue, live),
 * up/down between the lines, confirm uses. Layout, background and presets
 * are saved at once; the custom hue on confirm or when the page is left. */
static void layout_action(rsl_action a)
{
    uint32_t rgb;
    const char *name;
    int s = g.sel;
    bool layout = s < LAYOUT_COUNT, preset = s >= SET_PRESET && s < SET_CUSTOM;
    if (layout)
        m.layout_col = s;
    if (preset)
        m.accent_col = s;
    switch (a) {
    case RSL_ACT_LEFT:
    case RSL_ACT_RIGHT:
        if (s == SET_CUSTOM) {
            g.accent_hue = (g.accent_hue + (a == RSL_ACT_LEFT ? 350 : 10)) % 360;
            g.accent = ui_hue_color(g.accent_hue);
            m.accent_dirty = true;
        } else if (a == RSL_ACT_LEFT && s != 0 && s != SET_PRESET) {
            g.sel--;
        } else if (a == RSL_ACT_RIGHT && s != LAYOUT_COUNT - 1 && s != SET_CUSTOM - 1) {
            g.sel++;
        }
        break;
    case RSL_ACT_UP:
        g.sel = s == SET_CUSTOM ? m.accent_col : preset ? m.layout_col : s;
        break;
    case RSL_ACT_DOWN:
        g.sel = layout ? m.accent_col : preset ? SET_CUSTOM : s;
        break;
    case RSL_ACT_CONFIRM:
        if (layout) {
            g.layout = s;
        } else if (preset) {
            ui_accent_preset(s - SET_PRESET, &rgb, &name);
            g.accent = rgb;
            status(ST_OK, "Accent colour: %s.", name);
        } else {
            g.accent = ui_hue_color(g.accent_hue);
            m.accent_dirty = false;
            status(ST_OK, "Accent colour: custom, hue %d.", g.accent_hue);
        }
        save_settings();
        break;
    case RSL_ACT_BACK:
        if (m.accent_dirty)
            save_settings();
        m.accent_dirty = false;
        go_library();   /* like every page the + menu opens */
        break;
    default:
        break;
    }
}

static void on_action(rsl_action a)
{
    switch (g.screen) {
    case SCR_LIBRARY:
        library_action(a);
        break;
    case SCR_DETAIL:
        if (g.dialog_open) {
            if (a == RSL_ACT_CONFIRM || a == RSL_ACT_BACK)
                g.dialog_open = false;
        } else if (a == RSL_ACT_LEFT || a == RSL_ACT_RIGHT) {   /* Launch is rightmost, index 0 */
            if (a == RSL_ACT_LEFT && g.sel < DETAIL_COUNT - 1)
                g.sel++;
            else if (a == RSL_ACT_RIGHT && g.sel > 0)
                g.sel--;
        } else if (a == RSL_ACT_UP || a == RSL_ACT_DOWN) {
            ui_scroll(a == RSL_ACT_DOWN ? 1 : -1);
        } else if (a == RSL_ACT_BACK) {
            go_library();
        } else if (a == RSL_ACT_CONFIRM) {
            if (g.sel == DETAIL_LAUNCH)
                launch(g.game);
            else if (g.sel == DETAIL_CONTROLS)
                game_controls(g.game, SCR_DETAIL);
            else
                open_picture(g.game);
        }
        break;
    case SCR_PICTURE:
        picture_action(a);
        break;
    case SCR_REMAP:
        remap_action(a);
        break;
    case SCR_DIAG:
        if (a == RSL_ACT_BACK)
            go_library();
        if (a == RSL_ACT_CONFIRM)
            chain_load(RUNTIME_NRO, "\"romfs:/selftest.py\"");
        break;
    case SCR_LAYOUT:
        layout_action(a);
        break;
    case SCR_HELP:
        if (a == RSL_ACT_UP && g.sel > 0)
            g.sel--;
        if (a == RSL_ACT_DOWN && g.sel < HELP_COUNT - 1)
            g.sel++;
        if (a == RSL_ACT_CONFIRM || a == RSL_ACT_BACK) {
            if (!m.help_seen) {
                m.help_seen = true;
                save_settings();
            }
            go_library();
        }
        break;
    case SCR_TEST:
        break;
    }
}

static void log_event(const rsl_event *e)
{
    static const char *const kind[] = { "down  ", "up    ", "repeat", "EMERGENCY" };
    if (g.evcount == 8) {
        memmove(g.evlog[0], g.evlog[1], sizeof g.evlog[0] * 7);
        g.evcount = 7;
    }
    snprintf(g.evlog[g.evcount++], sizeof g.evlog[0], "%s %s", kind[e->type], ui_action_name(e->action));
}

/* A tap on the touchscreen: the item is focused, then confirmed (a guide
 * card is only focused); a footer hint does its action. The controller
 * test is left by holding B, which a tap cannot do: its hint leaves at once. */
static void tap(int a, int item)
{
    if (item >= 0) {
        if (g.menu_open)
            g.menu_sel = item;
        else if (!g.dialog_open)
            g.sel = item;
        if (g.screen == SCR_HELP)
            return;
    }
    if (g.screen == SCR_TEST && a == RSL_ACT_BACK)
        go_library();
    else if (a >= 0)
        on_action(a);
}

void frame(float dt)
{
    rsl_switch_input_read(&m.pad, &g.raw);

    uint32_t sig = rsl_switch_pad_signature(&m.pad);
    if (sig != m.pad_sig) {
        rsl_log("controller change: style 0x%x -> 0x%x", (unsigned)m.pad_sig, (unsigned)sig);
        m.pad_sig = sig;
        m.reset_pending = true;
        g.pad_connected = padIsConnected(&m.pad);
        g.pad_desc = pad_desc();
    }
    if (m.reset_pending) {
        m.reset_pending = false;
        rsl_input_reset(&g.in, g.raw.buttons);
    }

    rsl_input_update(&g.in, &g.raw, g.now, dt);
    if (g.in.event_count || g.in.pressed_buttons || g.raw.touching)
        m.last_input = g.now;
    if (g.screen == SCR_TEST && g.raw.touching) {
        g.in.cursor_x = g.raw.touch_x / 1280.0f;
        g.in.cursor_y = g.raw.touch_y / 720.0f;
    }
    /* Before the event loop: a capture started by this frame's confirm press
     * must not capture that same press. A completed capture clears this
     * frame's events, so the captured button does not also act as a UI key. */
    if (g.screen == SCR_REMAP)
        remap_capture_tick();

    for (int i = 0; i < g.in.event_count; i++) {
        const rsl_event *e = &g.in.events[i];
        if (g.screen == SCR_TEST)
            log_event(e);
        if (e->type == RSL_EV_EMERGENCY) {
            rsl_log("emergency combo: default controls restored");
            open_remap(GLOBAL_CFG, "Global", false, SCR_LIBRARY);
            g.edit = g.in.cfg;
            g.edit_dirty = true;
            status(ST_BAD, "Emergency reset: default controls are active. Press + to save them.");
        } else if (e->type != RSL_EV_ACTION_UP && g.capture < 0) {
            on_action(e->action);
        }
    }
    int item, tapped = ui_tap(&item);
    if (g.capture < 0 && (tapped >= 0 || item >= 0))
        tap(tapped, item);
    picture_tick();
    if (g.screen == SCR_TEST) {
        if (!rsl_action_active(&g.in, RSL_ACT_BACK))
            m.back_held = false;
        else if (!m.back_held)
            m.back_held = true, m.back_since = g.now;
        else if (g.now - m.back_since >= 1000)
            go_library(), m.back_held = false;
    }
}
