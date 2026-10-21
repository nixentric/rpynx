"""Text fields and the Switch keyboard, for rsl_bridge.

A text field opens the full Switch keyboard (its own text box shows what is
typed); OK puts the text into the field, Cancel leaves it, and neither moves
on: the player does (runs 14-16).
"""
import time
import weakref

import renpy
import renpy.pygame as pygame
import renpy.pygame.key

import _rslnx

# The text field last drawn as editable (Ren'Py keeps no reference to it; an
# Input without a value, such as renpy.input()'s, is not in behavior.inputs).
_last_input = None
pending = False           # a text field appeared: open the keyboard for it (rsl_bridge.step)
_closed_at = -1.0         # when the keyboard last closed (time.monotonic)


def active_input():
    i = _last_input and _last_input()
    if i is None or not i.editable or renpy.display.interface.text_rect is None:
        return None
    return i


def _finishes_on_enter(i):
    return i.action is not None or (i.value is None and not i.changed) or getattr(i.value, "returnable", False)


def open_keyboard():
    """The full Switch keyboard for the active text field: its own text box shows
    what is typed, starting from the field's text. OK puts the text into the
    field, Cancel leaves it; neither moves on, the player does (run 16)."""
    global pending, _closed_at
    pending = False
    i = active_input()
    if i is None:
        return
    text = _rslnx.keyboard(i.content, "", i.length or 0)
    _closed_at = time.monotonic()
    if text is not None:
        i.caret_pos = len(text)
        i.update_text(text, True)


def text_field_press(action):
    """Called on a button press; True if a text field took it.
    - Right after the keyboard closes, the press that closed it is dropped.
    - Y, or A with no button selected, opens the keyboard again.
    - A on a screen with no buttons at all presses Enter, because A cannot
      submit a Ren'Py input (input_enter is K_RETURN only)."""
    if action not in ("confirm", "back", "hide_ui") or active_input() is None:
        return False
    if time.monotonic() - _closed_at < 0.5:
        return True
    focus = renpy.display.focus
    if action == "back" or (action == "confirm" and focus.get_focused() is not None):
        return False
    buttons = [f for f in focus.focus_list if f.x is not None and f.widget.style.keyboard_focus]
    if action == "confirm" and not buttons and _finishes_on_enter(active_input()):
        for kind in (pygame.KEYDOWN, pygame.KEYUP):
            pygame.event.post(pygame.event.Event(kind, key=pygame.K_RETURN, mod=0, scancode=40,
                                                 unicode="\r", repeat=False))
    else:
        open_keyboard()
    return True


def install():
    """Ren'Py 8.5.3 starts a screen keyboard again on every redraw while SDL
    reports one as supported but not shown. That suits Android; on the Switch
    SDL's inline keyboard reappeared as soon as it closed (run 14), and it has
    no text box (run 16). Ren'Py's start_text_input now only asks for the full
    keyboard once per field; SDL's inline one is never shown."""
    key = renpy.pygame.key
    key.has_screen_keyboard_support = lambda: False

    def start_text_input():
        global pending
        pending = True

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
