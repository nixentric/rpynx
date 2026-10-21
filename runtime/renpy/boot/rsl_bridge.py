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
import renpy.pygame.key

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


def _where():
    """file:line of the game's current statement (below any menu context)."""
    try:
        node = renpy.game.script.lookup(renpy.game.contexts[0].current)
        return "%s:%d" % (node.filename, node.linenumber)
    except Exception as e:   # a diagnostic must never break a save or load
        return "unknown (%r)" % e


def _after_load():
    print("rsl: loaded, now at %s" % _where(), flush=True)


def _log_saves():
    """Saves and loads go to python.txt with the script line, so a hardware test can
    check from the log that a load returns to the line that was saved."""
    loadsave = renpy.loadsave
    if not getattr(loadsave.save, "_rsl", False):
        save, load = loadsave.save, loadsave.load

        def logged_save(slotname, *args, **kwargs):
            print("rsl: save %s at %s" % (slotname, _where()), flush=True)
            return save(slotname, *args, **kwargs)

        def logged_load(filename):
            print("rsl: load %s" % filename, flush=True)
            return load(filename)
        logged_save._rsl = True
        loadsave.save = renpy.exports.save = logged_save   # renpy.save in game code is renpy.exports.save
        loadsave.load = renpy.exports.load = logged_load
    if _after_load not in renpy.config.after_load_callbacks:   # 00start.rpy resets the list at init
        renpy.config.after_load_callbacks.append(_after_load)


def _close_dying_streams():
    """Ren'Py's audio.quit() stops the thread that calls renpysound.periodic() before it
    fades the channels out, so a stream that ends then stays on the C side's "dying" list
    and its decode thread waits forever (ffmedia decode_thread in SDL_CondWait). On a PC
    the process ends anyway; here the next game runs in the same process, and that
    thread's stack crashed it (hardware runs 22-23). Runs after audio.quit()."""
    try:
        renpy.audio.renpysound.periodic()
        print("rsl: finished audio streams closed at exit", flush=True)
    except Exception as e:
        print("rsl: closing finished audio streams failed: %r" % e, flush=True)


def _setup_display():
    """Runs from Interface.start(), once the game's config exists."""
    config = renpy.config
    _log_saves()
    if _close_dying_streams not in config.python_exit_callbacks:
        config.python_exit_callbacks.append(_close_dying_streams)
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
            hx, hy = 4, 3   # the arrow's tip: HOTSPOT in runtime/renpy/make_cursor.py
        config.mouse_displayable = renpy.store.MouseDisplayable(image, hx, hy)

    # No cursor until the stick moves it (it sat in a corner, hardware run 15).
    if renpy.display.interface is not None:
        renpy.display.interface.hide_mouse()


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
            if kind == DOWN:
                interface.hide_mouse()   # buttons and D-pad: no cursor until the stick moves
                if action == "confirm":
                    # ponytail: temporary diagnostic for run 17 (A did not pick a card on the
                    # console); remove once that is understood.
                    f = renpy.display.focus.get_focused()
                    print("rsl: A pressed, focused %s %r, screens %s" % (
                        type(f).__name__ if f is not None else None, getattr(f, "action", None),
                        _shown_screens()), flush=True)
                if _text_field_press(action):
                    continue
            # B goes back in-game, like the games' own "Back" (Rollback) buttons;
            # in menus it still closes the menu (Ren'Py binds pad B to game_menu).
            pad = PAD["rollback"] if action == "back" and not renpy.store._menu else PAD[action]
            if kind == UP:
                renpy.display.controller.post_event(pad, "release", False)
            else:
                renpy.display.controller.post_event(pad, "press", kind == REPEAT)
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
    _focus_first_button(moved)
    if _kbd_pending:
        _open_keyboard()
    return bool(events) or bool(moved)


_auto_focused = None   # the screen set whose first button was already selected
NOT_CONTENT = {"say", "nvl", "notify", "skip_indicator", "quick_menu", "_performance", "_trace_screen"}


def _screens():
    layers = renpy.display.scenelists.scene_lists().layers
    return [e.displayable for entries in layers.values() for e in entries
            if isinstance(e.displayable, renpy.display.screen.ScreenDisplayable)]


def _shown_screens():
    return tuple(sorted(str(sd.tag) for sd in _screens()))


def _content_buttons():
    """ids of the buttons on the screens the player is meant to act on: a choice
    menu's options, or the screens that are not overlays (config.overlay_screens,
    such as the quick menu) nor dialogue. None while dialogue is up without a
    choice: A must advance it."""
    screens = _screens()
    names = {sd.screen_name[0] for sd in screens}
    if "choice" in names:
        content = [sd for sd in screens if sd.screen_name[0] == "choice"]
    elif names & {"say", "nvl"}:
        return None
    else:
        skip = NOT_CONTENT | set(renpy.config.overlay_screens)
        content = [sd for sd in screens if sd.screen_name[0] not in skip and sd.tag not in skip]
    Button = renpy.display.behavior.Button
    ids = set()
    for sd in content:
        sd.visit_all(lambda d: ids.add(id(d)) if isinstance(d, Button) else None)
    return ids


def _focus_first_button(moved):
    """When a new set of screens appears and no button is selected, select the
    first button of the screen the player acts on, so the D-pad and A work at
    once in every game (runs 14-18): New Game, the first choice, the first card.
    Never a quick menu or other overlay button: Forbidden Fantasy's always-on
    debug_screen made its quick menu's Back look like content, and A went back
    instead of advancing (run 18). Not while a text field is active (the first
    button may be "Use default name") or the virtual cursor is in use."""
    global _auto_focused
    shown = _shown_screens()
    if moved:
        _auto_focused = shown
    focus = renpy.display.focus
    if shown == _auto_focused or not focus.focus_list:
        return
    if isinstance(focus.get_focused(), renpy.display.behavior.Button):
        _auto_focused = shown
        return
    if _active_input() is not None:
        return
    ids = _content_buttons()
    if not ids:
        return
    for f in focus.focus_list:
        if id(f.widget) in ids and f.x is not None and f.widget.style.keyboard_focus:
            _auto_focused = shown
            focus.pending_focus_type = "keyboard"
            focus.change_focus(f)
            return


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


def _old_language_change():
    """True for games made with Ren'Py 7.5.3/8.0.3 or older (script_version.txt)."""
    v = renpy.config.script_version
    return bool(v) and (v <= (7, 5, 3) or (v[0] == 8 and v <= (8, 0, 3)))


def _install_language_compat():
    """Ren'Py 8.5.3 re-runs every `define gui.*` when the language changes;
    7.5.3/8.0.3 did that only in gui.rebuild(). A 7.x game that sets gui values
    in `init python` lost them (Forbidden Fantasy: button text size back to 0,
    every button label invisible, hardware run 13). For such games, skip that
    redefine unless gui.rebuild() asks for it (rebuild=True)."""
    import renpy.ast
    import renpy.exports
    import renpy.translation

    redefine, change_language = renpy.ast.redefine, renpy.translation.change_language
    skip = []

    def compat_redefine(stores):
        if not (skip and skip[-1] and stores == ["store.gui"]):
            redefine(stores)

    def compat_change_language(language, force=False, rebuild=False):
        skip.append(not rebuild and _old_language_change())
        try:
            return change_language(language, force=force, rebuild=rebuild)
        finally:
            skip.pop()

    renpy.ast.redefine = compat_redefine
    renpy.translation.change_language = compat_change_language
    renpy.exports.change_language = compat_change_language


# The text field last drawn as editable (Ren'Py keeps no reference to it; an
# Input without a value, such as renpy.input()'s, is not in behavior.inputs).
_last_input = None
_kbd_pending = False      # a text field appeared: open the keyboard for it
_kbd_closed_at = -1.0     # when the keyboard last closed (time.monotonic)


def _active_input():
    i = _last_input and _last_input()
    if i is None or not i.editable or renpy.display.interface.text_rect is None:
        return None
    return i


def _finishes_on_enter(i):
    return i.action is not None or (i.value is None and not i.changed) or getattr(i.value, "returnable", False)


def _open_keyboard():
    """The full Switch keyboard for the active text field: its own text box shows
    what is typed, starting from the field's text. OK puts the text into the
    field, Cancel leaves it; neither moves on, the player does (run 16)."""
    global _kbd_pending, _kbd_closed_at
    _kbd_pending = False
    i = _active_input()
    if i is None:
        return
    text = _rslnx.keyboard(i.content, "", i.length or 0)
    _kbd_closed_at = time.monotonic()
    if text is not None:
        i.caret_pos = len(text)
        i.update_text(text, True)


def _text_field_press(action):
    """Called on a button press; True if a text field took it.
    - Right after the keyboard closes, the press that closed it is dropped.
    - Y, or A with no button selected, opens the keyboard again.
    - A on a screen with no buttons at all presses Enter, because A cannot
      submit a Ren'Py input (input_enter is K_RETURN only)."""
    if action not in ("confirm", "back", "hide_ui") or _active_input() is None:
        return False
    if time.monotonic() - _kbd_closed_at < 0.5:
        return True
    focus = renpy.display.focus
    if action == "back" or (action == "confirm" and focus.get_focused() is not None):
        return False
    buttons = [f for f in focus.focus_list if f.x is not None and f.widget.style.keyboard_focus]
    if action == "confirm" and not buttons and _finishes_on_enter(_active_input()):
        for kind in (pygame.KEYDOWN, pygame.KEYUP):
            pygame.event.post(pygame.event.Event(kind, key=pygame.K_RETURN, mod=0, scancode=40,
                                                 unicode="\r", repeat=False))
    else:
        _open_keyboard()
    return True


def _install_text_input():
    """Ren'Py 8.5.3 starts a screen keyboard again on every redraw while SDL
    reports one as supported but not shown. That suits Android; on the Switch
    SDL's inline keyboard reappeared as soon as it closed (run 14), and it has
    no text box (run 16). Ren'Py's start_text_input now only asks for the full
    keyboard once per field; SDL's inline one is never shown."""
    import weakref

    key = renpy.pygame.key
    key.has_screen_keyboard_support = lambda: False

    def start_text_input():
        global _kbd_pending
        _kbd_pending = True

    key.start_text_input = start_text_input
    key.stop_text_input = lambda: None

    Input = renpy.display.behavior.Input
    render = Input.render

    def input_render(self, *args, **kwargs):
        global _last_input
        if self.editable:
            _last_input = weakref.ref(self)
        return render(self, *args, **kwargs)

    Input.render = input_render


def install():
    renpy.display.controller.init = _setup_display
    renpy.pygame.event.poll = poll
    renpy.pygame.event.wait = wait

    # renpy.ast/exports cannot be imported before renpy.import_all() (no
    # renpy.object yet). post_import() runs after import_all() and again after
    # every reload_all(), which restores the modules from a backup.
    post_import = renpy.post_import

    def post_import_with_compat():
        post_import()
        _install_language_compat()
        _install_text_input()

    renpy.post_import = post_import_with_compat
