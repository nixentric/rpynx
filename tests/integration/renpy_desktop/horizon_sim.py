"""Run switch_main.py in a desktop approximation of the Switch's Python.

- sys.platform is "horizon" (what our CPython build reports);
- modules our Switch build lacks are made unimportable;
- subprocess is imported first: on the Switch it is patched to know it cannot
  spawn processes, which the SDK's copy is not.

Still the desktop SDK underneath: native code, paths and GL are not the Switch's.
usage: python horizon_sim.py <switch_main.py> <ren'py args...>
"""
import runpy
import subprocess  # noqa: F401  (see docstring)
import sys

MISSING = [
    "_ctypes", "ctypes", "_ssl", "ssl", "_hashlib", "_sqlite3", "sqlite3", "mmap",
    "_posixsubprocess", "resource", "grp", "pwd", "termios", "readline", "_curses",
    "_uuid", "_dbm", "_gdbm", "spwd", "nis", "syslog", "_crypt", "_posixshmem", "_tkinter",
]
for name in MISSING:
    sys.modules[name] = None
sys.platform = "horizon"

script = sys.argv[1]
sys.argv = sys.argv[1:]
runpy.run_path(script, run_name="__main__")
