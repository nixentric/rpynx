"""Desktop stand-in for the runtime's built-in _rslnx module.

Instead of reading Switch controllers through the C mapping engine, it
replays the logical actions a scenario asks for (scenario.py). Everything
above this module, rsl_bridge.py and Ren'Py itself, is the real code.
"""
import os
import time

DOWN, UP, REPEAT, EMERGENCY = 0, 1, 2, 3

_queue = []
_cursor = [0.5, 0.5, False]


def memory():
    return (0, 0)


def applet_type():
    return 0


def cursor_visible():
    return True


def wait_event(ms):
    time.sleep(ms / 1000.0)


# compat.sh: RSL_TAPS="2:down,3:confirm" presses those logical actions that many
# seconds after the first poll.
_taps = [(float(t), a) for t, a in (x.split(":") for x in os.environ.get("RSL_TAPS", "").split(",") if x)]
_start = []


def input_poll():
    if os.environ.get("RSL_SCENARIO", "1") != "0":   # compat.sh runs its own games
        import scenario
        scenario.tick()
    now = time.monotonic()
    _start[:] = _start or [now]
    while _taps and now - _start[0] >= _taps[0][0]:
        tap(_taps.pop(0)[1])
    events = list(_queue)
    del _queue[:]
    x, y, moved = _cursor
    _cursor[2] = False
    return events, x, y, moved


# Used by the scenario.

def post(kind, action):
    _queue.append((kind, action))


def tap(action):
    post(DOWN, action)
    post(UP, action)


def move_cursor(x, y):
    _cursor[:] = [x, y, True]


def keyboard(initial, guide, maxlen):
    """The full Switch keyboard: RSL_FAKE_KEYBOARD=type:<text> types that, cancel
    closes it. Each call is logged to RSL_KEYBOARD_LOG."""
    with open(os.environ.get("RSL_KEYBOARD_LOG", os.devnull), "a") as f:
        f.write("open %r\n" % initial)
    fake = os.environ.get("RSL_FAKE_KEYBOARD", "cancel")
    return fake[5:] if fake.startswith("type:") else None
