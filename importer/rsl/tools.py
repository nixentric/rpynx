"""External programs: finding 7-Zip and FFmpeg, and running them (no console window on Windows)."""
import os
import shutil
import stat
import subprocess

from .common import Refused


SEVEN_ZIP = ("7z", "7zz", "/opt/homebrew/bin/7z", "/usr/local/bin/7z")   # + Program Files\7-Zip
TOOL_DIRS = []   # folders searched before PATH for 7z/ffmpeg/ffprobe (--tools, the UI)
WINDOWS = os.name == "nt"
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)   # Windows: tools run without a console
FFPROBE = ("ffprobe", "/opt/homebrew/bin/ffprobe", "/usr/local/bin/ffprobe")
FFMPEG = ("ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg")


def find_tool(names):
    """First of names found in TOOL_DIRS, then on PATH (Windows adds .exe); absolute
    names are taken as they are."""
    path = os.pathsep.join(TOOL_DIRS + [os.environ.get("PATH", os.defpath)])
    return next(filter(None, (shutil.which(n, path=path) for n in names)), None)


def run(args, **kw):
    """An external tool: no stdin, output captured as UTF-8 that always decodes, and no
    console window flashing up on Windows."""
    kw.setdefault("errors", "replace")
    return subprocess.run(args, stdin=subprocess.DEVNULL, capture_output=True, encoding="utf-8",
                          creationflags=NO_WINDOW, **kw)


def seven_zip():
    installed = tuple(os.path.join(os.environ[v], "7-Zip", "7z.exe")
                      for v in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)") if os.environ.get(v))
    found = find_tool(SEVEN_ZIP + installed)
    if not found:
        raise Refused("7-Zip not found; install it (Windows: 7-zip.org, macOS: `brew install p7zip`, "
                      "Linux: p7zip-full) or choose its folder (--tools)")
    return found


def run_7z(args):
    # -sccUTF-8: 7-Zip on Windows otherwise prints names in the console code page.
    return run([seven_zip(), args[0], "-sccUTF-8"] + args[1:], errors="surrogateescape",
               env=dict(os.environ, LC_ALL="en_US.UTF-8"))


def rmtree(path):
    """shutil.rmtree that also removes read-only files (7-Zip keeps that attribute on
    Windows) and otherwise ignores errors."""
    def retry(func, p, _):
        try:
            os.chmod(p, stat.S_IWRITE)
            func(p)
        except OSError:
            pass
    shutil.rmtree(path, onerror=retry)
