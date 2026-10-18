"""Bridge from the loader's controller mapping engine to Ren'Py.

The C engine (_rslnx, platform/input) reads the Switch controllers and turns
physical buttons into logical actions using the user's mapping. This module
turns those actions into Ren'Py events, on Ren'Py's own thread:

- Most actions become Ren'Py *pad controls* posted through Ren'Py's
  controller.post_event(), so the game's config.pad_bindings decide what they
  do, exactly as with a real gamepad. (Confirm = "a", Back = "b", ...)
- Actions Ren'Py has no pad binding for become named events: hold-to-skip
  and auto-forward use Ren'Py's own "skip" and "toggle_afm" events; history,
  quick menu and the loader overlay use rsl_* events handled by a Keymap
  added to the underlay.
- The virtual cursor moves Ren'Py's mouse (draw.set_mouse_pos), so hover,
  focus and mouse/keyboard mode switching are Ren'Py's normal behaviour.
  Switch SDL draws no hardware cursor, so a software cursor is installed.

Ren'Py's own SDL game-controller support is disabled; otherwise every press
would arrive twice (and with SDL's position-based A/B naming).
"""
import time

import renpy
import renpy.display.controller
import renpy.pygame as pygame
import renpy.pygame.event

import _rslnx

# Logical action -> Ren'Py pad control name (see config.pad_bindings).
PAD = {
    "confirm": "a",
    "back": "b",
    "hide_ui": "y",
    "rollback": "leftshoulder",
    "rollforward": "rightshoulder",
    "game_menu": "start",
    "up": "dpup",
    "down": "dpdown",
    "left": "dpleft",
    "right": "dpright",
}

# Logical action -> named event handled by the underlay Keymap below.
NAMED = {
    "history": "rsl_history",
    "auto": "toggle_afm",
    "quick_menu": "rsl_quick_menu",
    "overlay": "rsl_overlay",
}

DOWN, UP, REPEAT, EMERGENCY = 0, 1, 2, 3

_orig_poll = renpy.pygame.event.poll
_orig_wait = renpy.pygame.event.wait
_mouse_button = False


def _show_history():
    if renpy.exports.has_screen("history"):
        return renpy.exports.run(renpy.store.ShowMenu("history"))
    renpy.exports.notify("This game has no history screen.")


def _toggle_quick_menu():
    store = renpy.store
    if hasattr(store, "quick_menu"):
        store.quick_menu = not store.quick_menu
        renpy.exports.restart_interaction()


def _overlay():
    # ponytail: the in-game loader overlay (remap/test/settings) is Milestone 6.
    renpy.exports.notify("Loader overlay: not implemented yet (Milestone 6).")


def _setup_display():
    """Runs from Interface.start(), once the game's config exists."""
    config = renpy.config
    # Ren'Py also matches these names against ordinary key/mouse events, and
    # a name missing from config.keymap is parsed as a key specifier (error).
    for name in NAMED.values():
        config.keymap.setdefault(name, [])
    underlay = [i for i in config.underlay if not getattr(i, "_rsl", False)]
    keymap = renpy.display.behavior.Keymap(
        rsl_history=_show_history,
        rsl_quick_menu=_toggle_quick_menu,
        rsl_overlay=_overlay,
    )
    keymap._rsl = True
    underlay.append(keymap)
    config.underlay[:] = underlay

    # Applet mode (album) has a small fraction of the console's memory; Mesa
    # crashed copying a texture there (hardware run 8). Keep Ren'Py's image
    # cache (400 MB by default) small. ponytail: fixed 64 MB; scale with the
    # real budget if applet mode turns out to be worth supporting.
    if _rslnx.applet_type() not in (0, 4):
        config.image_cache_size = None
        config.image_cache_size_mb = min(config.image_cache_size_mb, 64)
        cache = renpy.display.im.cache
        cache.cache_limit = min(cache.cache_limit or 1 << 62, 64 * 1024 * 1024 // 4)

    if _rslnx.cursor_visible() and config.mouse_displayable is None:
        image = "_rsl_cursor.png"
        if config.mouse and config.mouse.get("default"):
            image, hx, hy = config.mouse["default"][0][:3]
        else:
            hx = hy = 0
        config.mouse_displayable = renpy.store.MouseDisplayable(image, hx, hy)


def _post_mouse_button(down):
    global _mouse_button
    if down == _mouse_button:
        return
    _mouse_button = down
    kind = pygame.MOUSEBUTTONDOWN if down else pygame.MOUSEBUTTONUP
    pygame.event.post(pygame.event.Event(kind, pos=pygame.mouse.get_pos(), button=1))


def step():
    """Read the controllers once and post the resulting Ren'Py events.
    Returns True if there was any controller activity."""
    interface = renpy.display.interface
    if interface is None or not interface.started:
        return False

    events, cx, cy, moved = _rslnx.input_poll()
    for kind, action in events:
        if kind == EMERGENCY:
            renpy.exports.notify("Controls reset to default (+ and − held for 3 s).")
        elif action in PAD:
            if kind == UP:
                renpy.display.controller.post_event(PAD[action], "release", False)
            else:
                renpy.display.controller.post_event(PAD[action], "press", kind == REPEAT)
        elif action == "skip":
            if kind != REPEAT:
                renpy.exports.queue_event("skip", up=(kind == UP))
        elif action == "click":
            if kind != REPEAT:
                _post_mouse_button(kind == DOWN)
        elif action in NAMED and kind == DOWN:
            renpy.exports.queue_event(NAMED[action])

    if moved and renpy.display.draw is not None:
        renpy.display.draw.set_mouse_pos(cx * renpy.config.screen_width, cy * renpy.config.screen_height)
    return bool(events) or bool(moved)


def poll():
    step()
    return _orig_poll()


# Ren'Py blocks in wait() when idle. Read the controllers every 16 ms while
# they are in use, every 33 ms after 2 s without input (less wake-ups, less
# heat; at most ~33 ms extra latency on the first press).
ACTIVE_MS, IDLE_MS, IDLE_AFTER = 16, 33, 2.0
_last_activity = 0.0


def wait():
    global _last_activity
    while True:
        now = time.monotonic()
        if step():
            _last_activity = now
        ev = _orig_poll()
        if ev.type != pygame.NOEVENT:
            return ev
        # _rslnx.wait_event releases the GIL while SDL waits.
        _rslnx.wait_event(ACTIVE_MS if now - _last_activity < IDLE_AFTER else IDLE_MS)


def install():
    renpy.display.controller.init = _setup_display
    renpy.pygame.event.poll = poll
    renpy.pygame.event.wait = wait
