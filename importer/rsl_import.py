#!/usr/bin/env python3
"""rpynx importer (Milestone 5).

Analyses a DRM-free Ren'Py game (folder, .zip, .7z or .rar), writes a compatibility
report and, with --out, an SD-ready tree:

    <out>/switch/renpy-loader/games/<id>/game/...
    <out>/switch/renpy-loader/games/<id>/manifest.json

--out ftp://HOST:PORT uploads the same tree to the Switch over sphaira's FTP server.

Standard library only; runs on Python 3.9+ on Windows, macOS (/usr/bin/python3) and
Linux. This file is the command line; the code is the rsl/ package next to it (rsl/__init__.py
lists its parts), and importer/rsl_import_gui.py is a window over the same package. It never executes
game code, never unpickles anything and never writes to the source. The optional --decompile
hands copies of .rpyc files to unrpyc, run in its own process. See docs/IMPORTER.md.

usage: python3 importer/rsl_import.py SOURCE [--out DIR] [--id ID] [--replace] [--cover IMAGE]
"""
import argparse
import sys
import tempfile

from rsl import DECOMPILERS, ERRORS, export, ID_MAX, load, make_art, make_id, report, rmtree, TOOL_DIRS


def main(argv=None):
    ap = argparse.ArgumentParser(description="Analyse a Ren'Py game and export it for rpynx on the Switch.")
    ap.add_argument("source", help="game folder, .zip, .7z or .rar")
    ap.add_argument("--out", help="export root, e.g. the SD card, or ftp://HOST:PORT for the Switch over "
                                  "sphaira's FTP server; writes switch/renpy-loader/games/<id>")
    ap.add_argument("--id", help="game id (folder name); default from the title")
    ap.add_argument("--replace", action="store_true", help="update an existing export (saves are kept)")
    ap.add_argument("--optimize", action="store_true",
                    help="re-encode videos above 30 fps at 30 fps (same size, needs FFmpeg)")
    ap.add_argument("--recompile", action="store_true",
                    help="Python 2 game with .rpy sources: export without .rpyc for Ren'Py 8 to recompile")
    ap.add_argument("--decompile", action="store_true",
                    help="--recompile, and first rebuild .rpy for .rpyc that have none, with the "
                         "--decompiler, run as a separate process")
    ap.add_argument("--decompiler", choices=DECOMPILERS,
                    help="with --decompile: unrpyc (default; third_party/unrpyc/fetch.sh) or rpycdec "
                         "(pip install rpycdec, Python 3.10+); docs/IMPORTER.md compares them")
    ap.add_argument("--rpycdec-python", metavar="PYTHON",
                    help="Python 3.10+ interpreter that has rpycdec installed; default: rpycdec on PATH")
    ap.add_argument("--cover", metavar="IMAGE",
                    help="the game's cover in the loader, any picture FFmpeg reads (a 2:3 poster suits it "
                         "best; other shapes are cut from the middle); default: the middle of its menu art")
    ap.add_argument("--backdrop", metavar="IMAGE",
                    help="the picture behind the loader's detail page and Cinematic layout (16:9); "
                         "default: the game's menu art. --replace keeps a cover or backdrop chosen before")
    ap.add_argument("--tools", metavar="DIR", help="folder with ffmpeg/ffprobe or 7z, if they are not on PATH")
    a = ap.parse_args(argv)
    if a.id is not None and make_id(a.id) != a.id:
        ap.error("--id must be lowercase a-z, 0-9 and '-', at most %d chars (try %r)" % (ID_MAX, make_id(a.id)))
    for stream in (sys.stdout, sys.stderr):   # a Windows code page cannot encode every title
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    if a.decompiler and not a.decompile:
        ap.error("--decompiler needs --decompile")
    if a.rpycdec_python and a.decompiler != "rpycdec":
        ap.error("--rpycdec-python needs --decompile --decompiler rpycdec")
    if a.tools:
        TOOL_DIRS[:] = [a.tools]

    def show(done, total, rel, note):
        if note:
            print("  %s: %s" % (note, rel), flush=True)

    tmp = tempfile.mkdtemp(prefix="rsl-import-")
    try:
        decompile = (a.decompiler or DECOMPILERS[0]) if a.decompile else None
        manifest, gamedir, files, extra = load(a.source, tmp, a.id, a.recompile, a.optimize, decompile,
                                               a.rpycdec_python)
        art = make_art(manifest, gamedir, files, tmp, a.cover, a.backdrop)
        print(report(manifest), flush=True)
        if a.out:
            print("Exported to %s" % export(manifest, gamedir, files, a.out, a.replace, extra, show, art))
        return 0
    except ERRORS as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    finally:
        rmtree(tmp)


if __name__ == "__main__":
    sys.exit(main())
