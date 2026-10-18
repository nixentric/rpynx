"""Desktop stand-in for the runtime's built-in _rslnx module.

Instead of reading Switch controllers through the C mapping engine, it
replays the logical actions a scenario asks for (scenario.py). Everything
above this module, rsl_bridge.py and Ren'Py itself, is the real code.
"""
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


def input_poll():
    import scenario
    scenario.tick()
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
