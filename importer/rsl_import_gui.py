#!/usr/bin/env python3
"""Ren'Py Switch Loader importer, as a window (pywebview: the system's web view).

A thin layer over rsl_import.py. The page (rsl_import_gui.html) calls Api through
window.pywebview.api; every check and every copy is rsl_import's. pywebview runs each call
in its own thread, so unpacking or re-encoding (minutes) never blocks the window.

usage: python3 -m pip install pywebview
       python3 importer/rsl_import_gui.py      (Windows: py importer\\rsl_import_gui.py)
"""
import glob
import json
import os
import sys
import tempfile
import threading
import time

import rsl_import as imp

PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rsl_import_gui.html")


class Cancelled(Exception):
    pass


class Api:
    """What the page may call. Results are plain JSON; failures the user can act on come
    back as {"error": text}. Needs no window, so the tests drive it directly."""

    def __init__(self):
        self._window = None                  # set by main(); progress reaches the page through it
        self._tmp = None                     # unpacked archive; lives until the next analysis
        self._loaded = None                  # load() result for the last analysis
        self._cancel = threading.Event()
        self._working = threading.Lock()     # export or compare; main() waits on it before quitting

    def _js(self, function, *args):
        if self._window:
            self._window.evaluate_js("%s(...%s)" % (function, json.dumps(args)))

    def _drop_tmp(self):
        if self._tmp:
            imp.rmtree(self._tmp)
            self._tmp = None

    def tools(self):
        try:
            seven = imp.seven_zip()
        except imp.Refused:
            seven = None
        return {"7-Zip": bool(seven), "ffprobe": bool(imp.find_tool(imp.FFPROBE)),
                "ffmpeg": bool(imp.find_tool(imp.FFMPEG)), "unrpyc": bool(imp.find_unrpyc()),
                "rpycdec": bool(imp.find_tool(("rpycdec",)))}

    def sd_cards(self):
        """Mounted drives that already have a switch/ folder: likely the Switch's SD card."""
        if imp.WINDOWS:
            roots = ["%s:\\" % c for c in "DEFGHIJKLMNOPQRSTUVWXYZ"]
        else:
            roots = sorted(glob.glob("/Volumes/*") + glob.glob("/media/*/*") + glob.glob("/run/media/*/*"))
        return [r for r in roots if os.path.isdir(os.path.join(r, "switch"))]

    def pick(self, kind):
        """A native dialog: kind is "folder", "archive", "sd", "tools" or "collection" (a folder
        of games to compare). Returns a path or None."""
        import webview
        if kind == "archive":
            chosen = self._window.create_file_dialog(webview.FileDialog.OPEN, file_types=(
                "Game archives (*.zip;*.7z;*.rar)", "All files (*.*)"))
        else:
            chosen = self._window.create_file_dialog(webview.FileDialog.FOLDER)
        path = chosen[0] if chosen else None
        if path and kind == "tools":
            imp.TOOL_DIRS[:] = [path]
        return path

    def library(self, sd):
        """The games on the target: an SD card folder, or ftp://HOST:PORT for the Switch."""
        games = []
        try:
            found = imp.exported_games(sd)
        except imp.ERRORS as e:
            return {"error": str(e)}
        for game_id, m in found:
            try:
                text = imp.report(m) if m else "No manifest.json: copied by hand, or not by this importer."
            except (KeyError, TypeError, AttributeError):   # an older or hand-edited manifest
                text = json.dumps(m, indent=2, ensure_ascii=False)
            games.append({"id": game_id, "manifest": m, "report": text})
        return {"games": games, "root": imp.games_dir(sd)}

    def analyze(self, source, optimize=False, recompile=False, game_id="", decompile=""):
        """decompile: "", or the rsl_import.DECOMPILERS tool the page's Decompile option chose."""
        game_id = (game_id or "").strip()
        if game_id and imp.make_id(game_id) != game_id:
            return {"error": "Game id: use lowercase a-z, 0-9 and '-' only (try %r)." % imp.make_id(game_id)}
        self._loaded = None
        self._drop_tmp()
        self._tmp = tempfile.mkdtemp(prefix="rsl-import-")
        try:
            self._loaded = imp.load(source, self._tmp, game_id or None, recompile, optimize, decompile or None)
        except (imp.Refused, OSError) as e:
            return {"error": str(e)}
        m = self._loaded[0]
        return {"manifest": m, "report": imp.report(m)}

    def export(self, sd, replace=False):
        if not self._loaded:
            return {"error": "Analyze a game first."}
        if not sd:
            return {"error": "Choose where to export first."}
        manifest, gamedir, files, extra = self._loaded
        last = [0.0]

        def progress(done, total, rel, note):
            if self._cancel.is_set():
                raise Cancelled()
            if note or done == total or time.monotonic() - last[0] > 0.1:   # not 3000 page updates
                last[0] = time.monotonic()
                self._js("progress", done, total, rel, note)

        with self._working:
            self._cancel.clear()
            try:
                dest = imp.export(manifest, gamedir, files, sd, replace, extra, progress)
            except Cancelled:
                return {"error": "Export cancelled. The %s is as it was before." % (
                    "Switch" if imp.is_ftp(sd) else "SD card")}
            except imp.ERRORS as e:
                return {"error": str(e)}
        return {"dest": dest, "manifest": manifest, "report": imp.report(manifest)}   # notes failed re-encodes

    def delete(self, sd, game_id, saves=False):
        """Removes games/<id>/ from the target; saves/<id>/ too only if saves."""
        try:
            imp.delete_game(sd, game_id, saves)
        except imp.ERRORS as e:
            return {"error": str(e)}
        return {}

    def compare(self, folder, sd):
        """Read-only: the games in folder against the target's library."""
        if not sd:
            return {"error": "Choose a target first."}

        def progress(done, total, name):
            if self._cancel.is_set():
                raise Cancelled()
            self._js("compareProgress", done, total, name)

        with self._working:
            self._cancel.clear()
            try:
                return {"rows": imp.compare(folder, sd, progress)}
            except Cancelled:
                return {"error": "Comparison cancelled."}
            except imp.ERRORS as e:
                return {"error": str(e)}

    def cancel(self):
        self._cancel.set()   # export's or compare's progress callback raises at the next step


def unify_title_bar(window):
    """macOS: draw the page under a transparent title bar, as one surface. Unlike pywebview's
    frameless mode, the close/minimise/zoom buttons stay, and the bar still drags and zooms."""
    import AppKit
    from PyObjCTools import AppHelper

    def apply():
        ns = window.native
        ns.setStyleMask_(ns.styleMask() | AppKit.NSWindowStyleMaskFullSizeContentView)
        ns.setTitlebarAppearsTransparent_(True)
        ns.setTitleVisibility_(AppKit.NSWindowTitleHidden)
        bar = ns.contentView().superview().subviews().lastObject()   # pywebview paints it opaque
        if bar.respondsToSelector_(b"setBackgroundColor:"):
            bar.setBackgroundColor_(AppKit.NSColor.clearColor())
    AppHelper.callAfter(apply)   # AppKit only on the main thread


def main():
    try:
        import webview
        from webview.dom import DOMEventHandler
    except ImportError:
        sys.exit("The window needs pywebview: python3 -m pip install pywebview\n"
                 "(the command line needs nothing: python3 importer/rsl_import.py --help)")
    if imp.WINDOWS:
        try:
            import ctypes
            ctypes.windll.kernel32.SetErrorMode(1)   # no "insert a disk" box when sd_cards() probes drives
        except (AttributeError, OSError):
            pass
    api = Api()
    with open(PAGE, encoding="utf-8") as f:
        window = webview.create_window("Ren'Py Switch Loader importer", html=f.read(), js_api=api,
                                       width=1080, height=780, min_size=(820, 600))
    api._window = window
    if sys.platform == "darwin":
        window.events.before_show += unify_title_bar

    def dropped(event):   # the page cannot see a dropped file's path; pywebview adds it here
        files = event.get("dataTransfer", {}).get("files") or [{}]
        if files[0].get("pywebviewFullPath"):
            api._js("dropped", files[0]["pywebviewFullPath"])

    def bind(window):   # the page itself cancels dragover, which allows the drop
        window.dom.document.events.drop += DOMEventHandler(dropped, True, True)

    webview.start(bind, window)
    api.cancel()             # window closed during an export: roll it back first
    with api._working:
        pass
    api._drop_tmp()


if __name__ == "__main__":
    main()
