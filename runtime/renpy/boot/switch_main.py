"""Ren'Py Switch Loader: game entry point, run instead of renpy.py.

Ren'Py lets a distribution customise the path_to_* functions of its launcher
script (renpy.py); this loads that script unchanged, overrides where logs go
(the loader's log folder, not the game's own folder), installs the
controller bridge, and starts Ren'Py.

The runtime sets sys.argv to [<renpy-base>/renpy.py, <basedir>, "run",
"--savedir", <savedir>] and RSL_LOGDIR to the per-game log folder.
"""
import importlib.util
import os
import sys

BASE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.environ.get("RSL_LOGDIR")

# Not named renpy*: Ren'Py pickles the state of every renpy* module for
# restarts, and this one holds plain functions.
LAUNCHER = "rsl_renpy_launcher"


def load_renpy_launcher():
    spec = importlib.util.spec_from_file_location(LAUNCHER, os.path.join(BASE, "renpy.py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[LAUNCHER] = module
    spec.loader.exec_module(module)
    return module


def path_to_logdir(basedir):
    return LOGDIR


def main():
    launcher = load_renpy_launcher()

    if LOGDIR:
        os.makedirs(LOGDIR, exist_ok=True)
        launcher.path_to_logdir = path_to_logdir

    import rsl_bridge
    rsl_bridge.install()

    launcher.main()


main()
