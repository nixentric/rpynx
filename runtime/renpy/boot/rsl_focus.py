"""Which button is selected, for rsl_bridge: the first one when a screen appears,
the game menu's side menu, and D-pad moves to buttons whose boxes overlap."""
import renpy

import rsl_keyboard

_auto_focused = None   # the screen set whose first button was already selected
_last_shown = None     # the screen set of the previous call
NOT_CONTENT = {"say", "nvl", "notify", "skip_indicator", "quick_menu", "_performance", "_trace_screen"}


def _screens():
    layers = renpy.display.scenelists.scene_lists().layers
    return [e.displayable for entries in layers.values() for e in entries
            if isinstance(e.displayable, renpy.display.screen.ScreenDisplayable)]


def shown_screens():
    # Names, not tags: every game menu screen (save, load, history, ...) has the tag "menu".
    return tuple(sorted(str(sd.screen_name[0]) for sd in _screens()))


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


def focus_first_button(moved):
    """When a new set of screens appears and no button is selected, select the
    first button of the screen the player acts on, so the D-pad and A work at
    once in every game (runs 14-18): New Game, the first choice, the first card.
    Never a quick menu or other overlay button: Forbidden Fantasy's always-on
    debug_screen made its quick menu's Back look like content, and A went back
    instead of advancing (run 18). Not while a text field is active (the first
    button may be "Use default name") or the virtual cursor is in use."""
    global _auto_focused, _last_shown
    shown = shown_screens()
    if shown != _last_shown:   # screens changed: a menu opened again is a new menu
        _last_shown, _auto_focused = shown, None
    if moved:
        _auto_focused = shown
    focus = renpy.display.focus
    if shown == _auto_focused or not focus.focus_list:
        return
    if isinstance(focus.get_focused(), renpy.display.behavior.Button):
        _auto_focused = shown
        return
    if rsl_keyboard.active_input() is not None:
        return
    ids = _content_buttons()
    if not ids:
        return
    candidates = [f for f in focus.focus_list
                  if id(f.widget) in ids and f.x is not None and f.widget.style.keyboard_focus]
    if renpy.store._menu:
        # In a game menu, start on its side menu ("Save" on the save screen), not on the
        # first save slot: the slots come first in the standard screens (user request).
        candidates = [f for f in candidates if _current_menu_entry(f.widget)] + candidates
    if candidates:
        _auto_focused = shown
        focus.pending_focus_type = "keyboard"
        focus.change_focus(candidates[0])


def _current_menu_entry(widget):
    """True for the game menu's navigation button of the screen shown now: a selected
    button whose action is ShowMenu, as the standard navigation screen makes them."""
    action = getattr(widget, "action", None)
    actions = action if isinstance(action, list) else [action]
    return (any(isinstance(a, renpy.store.ShowMenu) for a in actions)
            and renpy.display.behavior.is_selected(action))


def _focus_nearest(fx0, fy0, fx1, fy1, tx0, ty0, tx1, ty1, line_dist, condition, *rest):
    """Ren'Py's D-pad navigation only goes to a button whose box starts past the selected
    one's edge. Buttons taller than the space between them overlap, so the next one was
    skipped (Forbidden Fantasy: down from New Game went past Continue, run 24). Also accept
    a box that overlaps but whose centre is clearly further that way; Ren'Py's distance
    still picks the nearest."""
    axis = 0 if fx0 != tx0 else 1
    sign = 1 if (fx0 > tx0 if axis == 0 else fy0 > ty0) else -1

    def further(old, new):
        if condition(old, new):
            return True
        old_c = old[axis] + old[axis + 2] / 2.0
        new_c = new[axis] + new[axis + 2] / 2.0
        return (new_c - old_c) * sign > 0.25 * max(old[axis + 2], new[axis + 2])

    return _orig_focus_nearest(fx0, fy0, fx1, fy1, tx0, ty0, tx1, ty1, line_dist, further, *rest)


_orig_focus_nearest = None


def install():
    """Runs from rsl_bridge's display setup, when renpy.display.focus exists."""
    global _orig_focus_nearest
    focus = renpy.display.focus
    if focus.focus_nearest is not _focus_nearest:   # focus.py calls it by its global name
        _orig_focus_nearest = focus.focus_nearest
        focus.focus_nearest = _focus_nearest
