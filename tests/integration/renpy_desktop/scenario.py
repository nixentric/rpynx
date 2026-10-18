"""Scripted play-through of "The Question" through the controller bridge.

Driven from _rslnx.input_poll(), i.e. on Ren'Py's own thread inside its event
loop. Each step sends logical actions (exactly what the C mapping engine
would emit) and waits, with a timeout, for Ren'Py's state to change.
Writes scenario-report.txt and screenshots to $RSL_LOGDIR, then quits Ren'Py
with status 0 if every check passed.
"""
import os
import time

import renpy

import _rslnx

OUT = os.environ.get("RSL_LOGDIR", ".")
results = []


class Fail(Exception):
    pass


def record(ok, name, detail=""):
    results.append((ok, name, detail))
    print("%s %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def screen(name):
    return renpy.exports.get_screen(name) is not None


def line():
    return renpy.exports.get_filename_line()


def shot(name):
    renpy.exports.screenshot(os.path.join(OUT, name))


def until(name, pred, timeout=15.0):
    """Generator helper: yield until pred() is true, else fail the step."""
    deadline = time.time() + timeout
    while True:
        try:
            if pred():
                record(True, name)
                return
        except Exception as e:  # a predicate touching state mid-transition
            last = repr(e)
        else:
            last = ""
        if time.time() > deadline:
            raise Fail("%s: timed out after %.0f s %s" % (name, timeout, last))
        yield


def pause(seconds):
    end = time.time() + seconds
    while time.time() < end:
        yield


def focus_center(match):
    """Virtual-screen centre of the first focusable whose action matches."""
    for f in renpy.display.focus.focus_list:
        action = getattr(f.widget, "action", None)
        if f.x is not None and match(action):
            return f.x + f.w / 2.0, f.y + f.h / 2.0
    return None


def steps():
    cfg = renpy.config
    yield from until("main menu shown", lambda: screen("main_menu"), 60)
    yield from pause(1.0)
    shot("01-main-menu.png")

    # Virtual cursor: move onto "Start" and click it.
    target = None
    while target is None:
        target = focus_center(lambda a: isinstance(a, renpy.store.Start))
        yield
    _rslnx.move_cursor(target[0] / cfg.screen_width, target[1] / cfg.screen_height)
    yield from pause(0.3)

    def at_target():
        x, y = renpy.display.draw.get_mouse_pos()
        return abs(x - target[0]) < 2 and abs(y - target[1]) < 2
    yield from until("virtual cursor moves Ren'Py's mouse", at_target, 5)
    _rslnx.tap("click")
    yield from until("cursor click on Start begins the game", lambda: screen("say"), 30)
    yield from pause(0.5)
    shot("02-dialogue.png")

    first = line()
    _rslnx.tap("confirm")
    yield from until("confirm (A) advances dialogue", lambda: line() != first)
    second = line()
    yield from pause(0.3)

    _rslnx.tap("rollback")
    yield from until("rollback (L) returns to the previous line", lambda: line() == first)
    _rslnx.tap("rollforward")
    yield from until("roll forward (R) returns to the later line", lambda: line() == second)
    _rslnx.tap("back")
    yield from until("back (B) in the game rolls back, like L", lambda: line() == first)
    _rslnx.tap("rollforward")
    yield from until("roll forward (R) after B", lambda: line() == second)

    _rslnx.tap("hide_ui")
    yield from until("hide UI (X) hides the dialogue window", lambda: renpy.store._windows_hidden)
    shot("03-hidden.png")
    _rslnx.tap("hide_ui")
    yield from until("hide UI (X) again restores it", lambda: not renpy.store._windows_hidden)

    _rslnx.tap("history")
    yield from until("history (Y) opens the history screen", lambda: screen("history"))
    yield from pause(0.5)
    shot("04-history.png")
    _rslnx.tap("back")
    yield from until("back (B) closes history", lambda: not screen("history") and screen("say"))

    _rslnx.tap("game_menu")
    yield from until("game menu (+) opens the save screen", lambda: screen("save"))
    yield from pause(0.5)
    shot("05-game-menu.png")
    _rslnx.tap("back")
    yield from until("back (B) closes the game menu", lambda: not screen("save") and screen("say"))

    quick = renpy.store.quick_menu
    _rslnx.tap("quick_menu")
    yield from until("quick menu (-) toggles the quick menu", lambda: renpy.store.quick_menu != quick)
    _rslnx.tap("quick_menu")
    yield from until("quick menu (-) toggles back", lambda: renpy.store.quick_menu == quick)

    afm = renpy.game.preferences.afm_enable
    _rslnx.tap("auto")
    yield from until("auto (ZL) toggles auto-forward", lambda: renpy.game.preferences.afm_enable != afm)
    _rslnx.tap("auto")
    yield from until("auto (ZL) toggles it off again", lambda: renpy.game.preferences.afm_enable == afm)

    before = line()
    _rslnx.post(_rslnx.DOWN, "skip")
    yield from pause(2.0)
    _rslnx.post(_rslnx.UP, "skip")
    yield from until("holding skip (ZR) skips ahead", lambda: line() != before or screen("choice"), 5)
    yield from pause(0.5)
    yield from until("releasing skip stops skipping", lambda: not renpy.config.skipping, 5)

    for _ in range(80):
        if screen("choice"):
            break
        _rslnx.tap("confirm")
        yield from pause(0.4)
    yield from until("dialogue reaches the first menu", lambda: screen("choice"), 5)
    yield from pause(0.5)
    shot("06-choice.png")
    _rslnx.tap("down")
    yield from pause(0.3)
    _rslnx.tap("confirm")
    yield from until("D-pad focus + confirm picks a menu choice", lambda: not screen("choice"))

    renpy.exports.save("1-1", "controller bridge test")
    savedir = renpy.config.savedir
    yield from until("save lands in the per-game save folder",
                     lambda: any(f.startswith("1-1") for f in os.listdir(savedir)), 10)
    record(renpy.exports.can_load("1-1"), "save slot 1-1 is loadable", savedir)

    _rslnx.post(_rslnx.EMERGENCY, "overlay")
    yield from until("emergency combo shows a notification", lambda: screen("notify"), 5)

    _rslnx.tap("overlay")
    yield from until("overlay (L-stick press) reports its status", lambda: screen("notify"), 5)


_gen = None
_done = False


def tick():
    global _gen, _done
    if _done:
        return
    if _gen is None:
        _gen = steps()
    try:
        next(_gen)
        return
    except StopIteration:
        pass
    except Fail as e:
        record(False, str(e))
    except Exception as e:
        record(False, "scenario error: %r" % e)
    _done = True
    failed = sum(1 for ok, _, _ in results if not ok)
    with open(os.path.join(OUT, "scenario-report.txt"), "w") as f:
        for ok, name, detail in results:
            f.write("%s %s %s\n" % ("PASS" if ok else "FAIL", name, detail))
        f.write("%d passed, %d failed\n" % (len(results) - failed, failed))
    renpy.exports.quit(status=1 if failed else 0)
