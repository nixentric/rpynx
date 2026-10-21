#!/usr/bin/env python3
"""Ren'Py Switch Loader importer (Milestone 5).

Analyses a DRM-free Ren'Py game (folder, .zip, .7z or .rar), writes a compatibility
report and, with --out, an SD-ready tree:

    <out>/switch/renpy-loader/games/<id>/game/...
    <out>/switch/renpy-loader/games/<id>/manifest.json

--out ftp://HOST:PORT uploads the same tree to the Switch over sphaira's FTP server.

Standard library only; runs on Python 3.9+ on Windows, macOS (/usr/bin/python3) and
Linux; importer/rsl_import_gui.py is a window over the same functions. It never executes
game code, never unpickles anything and never writes to the source. The optional --decompile
hands copies of .rpyc files to unrpyc, run in its own process. See docs/IMPORTER.md.

usage: python3 importer/rsl_import.py SOURCE [--out DIR] [--id ID] [--replace]
"""
import argparse
import contextlib
import ftplib
import hashlib
import io
import json
import os
import pickle
import pickletools
import posixpath
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unicodedata
import urllib.parse
import zipfile
import zlib

# Archive limits. A source over any of them is refused before anything is written.
MAX_FILES = 200000
MAX_TOTAL_BYTES = 32 << 30      # uncompressed; large visual novels reach ~20 GB
MAX_RATIO = 200                 # uncompressed/compressed, per entry
RATIO_MIN_BYTES = 1 << 20       # smaller entries may compress as well as they like
LARGE_FILE_BYTES = 2 << 30      # warn: FAT32 SD cards stop at 4 GiB per file

# The loader skips folder names of 64+ bytes (app/launcher/main.c, games[64]).
ID_MAX = 63
RUNTIME = "renpy-8.5.3"
RUNTIME_VERSION = (8, 5, 3)
MANIFEST_FORMAT = "renpy-switch-loader/manifest"
SEVEN_ZIP = ("7z", "7zz", "/opt/homebrew/bin/7z", "/usr/local/bin/7z")   # + Program Files\7-Zip
TOOL_DIRS = []   # folders searched before PATH for 7z/ffmpeg/ffprobe (--tools, the UI)
WINDOWS = os.name == "nt"
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)   # Windows: tools run without a console
# Names Windows cannot create as given: device names (CON, NUL.txt, ...), alternate data
# streams (a:b), and trailing dots or spaces, which it strips, so ".. " would become "..".
WINDOWS_BAD_NAME = re.compile(r'[<>:"|?*\x00-\x1f]|[. ]$|^(con|prn|aux|nul|com\d|lpt\d)(\.|$)', re.I)

SCRIPT_EXTS = (".rpy", ".rpyc", ".rpa", ".rpym", ".rpymc")
NATIVE_EXTS = (".pyd", ".dll", ".so", ".dylib")
AUDIO_EXTS = (".ogg", ".opus", ".mp3", ".wav", ".flac", ".m4a", ".aac", ".wma", ".mp2")
VIDEO_EXTS = (".webm", ".ogv", ".mkv", ".mp4", ".m4v", ".mov", ".avi", ".wmv", ".mpg", ".mpeg", ".flv")
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".webp")
NO_PLAY_EXTS = (".mid", ".midi")   # the runtime's FFmpeg decodes the rest; Ren'Py has no MIDI

# What keeps the Switch smooth and cool. Video is decoded in software on a ~1 GHz
# Cortex-A57, and the handheld screen is 1280x720 (1920x1080 docked).
SWITCH_SCREEN = (1280, 720)
DOCKED_SCREEN = (1920, 1080)
MAX_VIDEO_FPS = 30
MAX_TEXTURE = 4096                 # GL_MAX_TEXTURE_SIZE on the Switch (Ren'Py log, run 13)
HEAVY_CODECS = ("hevc", "av1")     # the most expensive to decode in software
MAX_RPA_INDEX = 64 << 20           # decompressed archive index
FFPROBE = ("ffprobe", "/opt/homebrew/bin/ffprobe", "/usr/local/bin/ffprobe")
FFMPEG = ("ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg")
# --optimize re-encodes in the same codec family (the game names the file), at
# constant quality so it stays sharp. VP9 tiles let the Switch decode on several cores.
ENCODERS = {
    "vp9": ["-c:v", "libvpx-vp9", "-crf", "31", "-b:v", "0", "-deadline", "good", "-cpu-used", "2",
            "-row-mt", "1", "-tile-columns", "2"],
    "vp8": ["-c:v", "libvpx", "-crf", "8", "-b:v", "8M", "-deadline", "good", "-cpu-used", "1"],
    "h264": ["-c:v", "libx264", "-crf", "20", "-preset", "slow", "-profile:v", "high"],
}
# --decompile: third_party/unrpyc/fetch.sh puts unrpyc here; it runs on the importer's Python.
UNRPYC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "build", "unrpyc", "unrpyc", "unrpyc.py")
DECOMPILERS = ("unrpyc", "rpycdec")   # --decompiler; the first is the default (docs/IMPORTER.md compares them)
DECOMPILE_TIMEOUT = 600           # seconds for all of a game's sourceless scripts
JUNK = {".DS_Store", "Thumbs.db", "desktop.ini", "__MACOSX"}
LAUNCHER_EXTS = (".exe", ".sh", ".py", ".app")
LIBRARY = "switch/renpy-loader"   # games/, saves/ and logs/ under the SD card root
FTP_PORT = 5000                   # sphaira's FTP server
FTP_TIMEOUT = 10                  # seconds, per network operation
FTP_BLOCK = 64 << 10
PACKED = "rsl-nonascii"             # game/rsl-nonascii[-N].rpa: files the Switch SD cannot name
PACKED_MAX = 2 << 30               # bytes per archive, well under FAT32's 4 GiB


class Refused(Exception):
    """The source cannot be imported; the message says why."""


ERRORS = (Refused,) + ftplib.all_errors   # what the user can act on: refusals, disk and network


def make_id(text):
    """Folder-safe game id: lowercase ASCII [a-z0-9], other runs -> '-'."""
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:ID_MAX].strip("-") or "game"


def is_junk(name):
    return name in JUNK or name.startswith("._")


# ---------------------------------------------------------------- extraction

def member_parts(name):
    """Validated path components of an archive member, [] for the root."""
    p = name.replace("\\", "/")
    if p.startswith("/") or re.match(r"[A-Za-z]:", p) or "\0" in p:
        raise Refused("archive entry has an absolute path: %r" % name)
    parts = [x for x in p.split("/") if x not in ("", ".")]
    if ".." in parts:
        raise Refused("archive entry escapes the archive (..): %r" % name)
    if WINDOWS and any(WINDOWS_BAD_NAME.search(x) for x in parts):
        raise Refused("archive entry has a name Windows cannot create safely: %r" % name)
    return parts


def check_sizes(count, total):
    if count > MAX_FILES:
        raise Refused("archive has %d entries (limit %d)" % (count, MAX_FILES))
    if total > MAX_TOTAL_BYTES:
        raise Refused("archive unpacks to %d bytes (limit %d)" % (total, MAX_TOTAL_BYTES))


def check_ratio(name, size, packed):
    if size > RATIO_MIN_BYTES and size > MAX_RATIO * max(packed, 1):
        raise Refused("archive entry %r compresses %d:1 (limit %d:1), refusing a possible "
                      "decompression bomb" % (name, size // max(packed, 1), MAX_RATIO))


def extract_zip(path, dest, warnings):
    with zipfile.ZipFile(path) as zf:
        infos = zf.infolist()
        check_sizes(len(infos), sum(i.file_size for i in infos))
        plan = []
        for i in infos:
            parts = member_parts(i.filename)
            if i.flag_bits & 1:
                raise Refused("archive is password-protected; extract it yourself and import the folder")
            check_ratio(i.filename, i.file_size, i.compress_size)
            if stat.S_ISLNK(i.external_attr >> 16):
                with zf.open(i) as f:
                    target = f.read(4096).decode("utf-8", "replace")
                where = posixpath.normpath(posixpath.join("/r", *parts[:-1], target.replace("\\", "/")))
                if target.startswith(("/", "\\")) or not where.startswith("/r/"):
                    raise Refused("archive symlink %r points outside the archive (%r)" % (i.filename, target))
                warnings.append("skipped symlink %s (links are not exported)" % i.filename)
                continue
            if parts:
                plan.append((i, parts))
        for i, parts in plan:
            out = os.path.join(dest, *parts)
            try:
                if i.is_dir():
                    os.makedirs(out, exist_ok=True)
                    continue
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with zf.open(i) as src, open(out, "xb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
            except FileExistsError:
                raise Refused("archive has two entries for %r (names differ only in case?)" % i.filename)
            except (OSError, NotImplementedError, zipfile.BadZipFile, zlib.error) as e:
                raise Refused("cannot extract %r: %s" % (i.filename, e))


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


def extract_7z(path, dest, kind):
    path = os.path.abspath(path)
    # Dummy -p so an encrypted archive fails instead of prompting on the terminal.
    r = run_7z(["l", "-slt", "-p-", path])
    if r.returncode != 0:
        err = [l.split(" : ")[-1].strip() for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l]
        hint = "; if it opens in another tool, extract it there and import the folder" if kind == "rar" else ""
        raise Refused("7z cannot open %s: %s%s" % (os.path.basename(path), err[0] if err else "exit %d" % r.returncode, hint))
    entries = []
    for block in r.stdout.split("\n----------\n", 1)[-1].split("\n\n"):
        e = dict(l.split(" = ", 1) for l in block.splitlines() if " = " in l)
        if "Path" in e:
            entries.append(e)
    total = 0
    for e in entries:
        member_parts(e["Path"])
        if e.get("Encrypted") == "+":
            raise Refused("archive is password-protected; extract it yourself and import the folder")
        if re.search(r"(^|\s)l[rwx-]{9}", e.get("Attributes", "")) or \
                any("Link" in k and v for k, v in e.items()):
            raise Refused("archive contains links (%r); extract it yourself and import the folder" % e["Path"])
        size = int(e.get("Size") or 0)
        total += size
        if e.get("Packed Size"):
            check_ratio(e["Path"], size, int(e["Packed Size"]))
    check_sizes(len(entries), total)
    check_ratio(os.path.basename(path), total, os.path.getsize(path))  # solid archives
    r = run_7z(["x", "-y", "-bd", "-p-", "-o" + dest, path])
    if r.returncode != 0:
        raise Refused("7z failed to extract %s: %s" % (os.path.basename(path), r.stderr.strip() or r.stdout[-300:]))


def check_tree(root):
    """After extraction: nothing may be a link, and the limits must still hold."""
    count = total = 0
    for dirpath, dirnames, filenames in os.walk(root):
        for n in dirnames + filenames:
            st = os.lstat(os.path.join(dirpath, n))
            if stat.S_ISLNK(st.st_mode):
                raise Refused("archive produced a symlink: %s" % os.path.relpath(os.path.join(dirpath, n), root))
            if stat.S_ISREG(st.st_mode):
                if st.st_nlink > 1:
                    raise Refused("archive produced a hard link: %s" % os.path.relpath(os.path.join(dirpath, n), root))
                count += 1
                total += st.st_size
    check_sizes(count, total)


def unpack(source, tmp, warnings):
    """Returns (root to analyse, source kind)."""
    if os.path.isdir(source):
        return source, "directory"
    low = source.lower()
    if low.endswith(".zip"):
        kind = "zip"
    elif low.endswith((".7z", ".rar")):
        kind = low.rsplit(".", 1)[1]
    else:
        raise Refused("unsupported source %r: give a folder, .zip, .7z or .rar" % source)
    if not os.path.isfile(source):
        raise Refused("%s does not exist" % source)
    try:
        if kind == "zip":
            extract_zip(source, tmp, warnings)
        else:
            extract_7z(source, tmp, kind)
    except (zipfile.BadZipFile, zlib.error) as e:
        raise Refused("cannot read %s: %s" % (os.path.basename(source), e))
    check_tree(tmp)
    return tmp, kind


# ------------------------------------------------------------------ analysis

def has_scripts(d):
    return any(n.lower().endswith(SCRIPT_EXTS) for n in os.listdir(d))


def find_gamedir(root, depth=0):
    """Ren'Py's path_to_gamedir (renpy.py) on a distribution root, descending through
    one wrapper folder or a macOS .app if needed. Returns (basedir, gamedir)."""
    autorun = os.path.join(root, "Contents", "Resources", "autorun")
    if depth < 3 and os.path.isdir(autorun):   # root is a macOS .app
        return find_gamedir(autorun, depth + 1)
    names = [n for n in sorted(os.listdir(root)) if not is_junk(n)]
    isdir = lambda n: os.path.isdir(os.path.join(root, n)) and not os.path.islink(os.path.join(root, n))
    candidates = []
    for n in names:
        stem, ext = os.path.splitext(n)
        if ext.lower() in LAUNCHER_EXTS:
            candidates += [stem] + [stem[i + 1:] for i, c in enumerate(stem) if c in " _"]
    for c in candidates + ["game", "data"]:
        if c and c != "renpy" and isdir(c):
            return root, os.path.join(root, c)
    if has_scripts(root):
        return root, root
    if depth < 3:
        for n in names:
            if n.endswith(".app") and os.path.isdir(os.path.join(root, n, "Contents", "Resources", "autorun")):
                return find_gamedir(os.path.join(root, n), depth + 1)
        dirs = [n for n in names if isdir(n)]
        if len(dirs) == 1:
            return find_gamedir(os.path.join(root, dirs[0]), depth + 1)
    raise Refused("no Ren'Py game directory (game/ with .rpy/.rpyc/.rpa files) found")


def scan(gamedir, top_excludes):
    """Regular files under gamedir as [(posix relpath, size)], plus skipped links."""
    files, skipped = [], []
    for dirpath, dirnames, filenames in os.walk(gamedir):
        top = dirpath == gamedir
        keep = []
        for n in sorted(dirnames):
            p = os.path.join(dirpath, n)
            if is_junk(n) or (top and top_excludes(n)):
                continue
            if os.path.islink(p):
                skipped.append(os.path.relpath(p, gamedir))
            else:
                keep.append(n)
        dirnames[:] = keep
        for n in sorted(filenames):
            p = os.path.join(dirpath, n)
            if is_junk(n) or (top and top_excludes(n)):
                continue
            st = os.lstat(p)
            if stat.S_ISREG(st.st_mode):
                files.append((os.path.relpath(p, gamedir).replace(os.sep, "/"), st.st_size))
            else:
                skipped.append(os.path.relpath(p, gamedir))
    return files, skipped


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def renpy_version(basedir, py_hint):
    """Version string from renpy/vc_version.py (8.1+) or renpy/__init__.py, or None."""
    d = os.path.join(basedir, "renpy")
    text = read_text(os.path.join(d, "vc_version.py")) or ""
    m = re.search(r"^version\s*=\s*['\"]([0-9][0-9.]*)['\"]", text, re.M)
    if m:
        return m.group(1)
    compiled = [os.path.join(d, "vc_version.pyc"), os.path.join(d, "vc_version.pyo")]
    cache = os.path.join(d, "__pycache__")
    if os.path.isdir(cache):
        compiled += [os.path.join(cache, n) for n in sorted(os.listdir(cache)) if n.startswith("vc_version.")]
    for p in compiled:
        try:
            with open(p, "rb") as f:
                m = re.search(rb"\b(\d+\.\d+\.\d+\.\d+)\b", f.read(65536))
        except OSError:
            continue
        if m:
            return m.group(1).decode()
    # 7.x and 8.0: version_tuple = (7, 4, 11, vc_version); 7.5/8.0 share one file
    # with both tuples behind `if PY2:`.
    found = [tuple(int(x) for x in t) for t in re.findall(
        r"version_tuple\s*=\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)", read_text(os.path.join(d, "__init__.py")) or "")]
    if len(found) > 1 and py_hint in ("2", "3"):
        found = [t for t in found if (t[0] >= 8) == (py_hint == "3")] or found
    return ".".join(map(str, max(found))) if found else None


def lib_python(basedir):
    """Python generations hinted by the engine's lib/ folder: a set of '2'/'3'."""
    lib = os.path.join(basedir, "lib")
    hints = set()
    if os.path.isdir(lib):
        for dirpath, dirnames, filenames in os.walk(lib):
            for n in dirnames + filenames:
                low = n.lower()
                if low.startswith("py3-"):
                    hints.add("3")
                elif low.startswith("py2-") or low in ("python2.7", "pythonlib2.7", "python27.dll"):
                    hints.add("2")
    if os.path.exists(os.path.join(basedir, "python27.dll")):
        hints.add("2")
    return hints


def rpyc_python(path):
    """'3' or '2' from the pickle protocol in slot 1 of a .rpyc, None if unreadable.
    Reads two decompressed bytes; never unpickles."""
    try:
        with open(path, "rb") as f:
            head = f.read(46)
            if head[:10] == b"RENPY RPC2":
                slots = [struct.unpack_from("<III", head, 10 + 12 * i) for i in range(3)] if len(head) == 46 else []
                slot1 = [s for s in slots if s[0] == 1]
                if not slot1:
                    return None
                f.seek(slot1[0][1])
            else:
                f.seek(0)   # pre-RPC2 (Ren'Py 6): the whole file is one zlib stream
            out = zlib.decompressobj().decompress(f.read(4096), 2)
    except (OSError, zlib.error, struct.error):
        return None
    if out[:1] == b"\x80" and len(out) == 2:
        return "3" if out[1] >= 3 else "2"
    return "2" if out[:1] in (b"(", b"}", b"]", b"c") else None   # protocol 0/1


def options_title(gamedir):
    text = read_text(os.path.join(gamedir, "options.rpy")) or ""
    m = re.search(r"config\.name\s*=\s*(?:_\(\s*)?[uU]?(['\"])((?:\\.|(?!\1).)*)\1", text)
    return re.sub(r"\\(.)", r"\1", m.group(2)).strip() if m else None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def probe_video(ffprobe, path):
    """{codec, width, height, fps, alpha} of the first video stream, or None."""
    r = run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
             "stream=codec_name,width,height,avg_frame_rate,r_frame_rate:stream_tags=alpha_mode",
             "-of", "json", path])
    try:
        st = json.loads(r.stdout)["streams"][0]
        rate = st.get("avg_frame_rate", "0/0")
        if rate.endswith("/0") or rate.startswith("0/"):
            rate = st.get("r_frame_rate", "0/1")
        num, den = rate.split("/")
        return {"codec": st["codec_name"], "width": int(st["width"]), "height": int(st["height"]),
                "fps": float(num) / float(den), "alpha": st.get("tags", {}).get("alpha_mode") == "1"}
    except (ValueError, KeyError, IndexError, ZeroDivisionError):
        return None


def image_size(path):
    """(width, height) from a PNG, JPEG or WebP header (a path or a binary file), or None."""
    if not isinstance(path, str):
        return _image_size(path)
    with open(path, "rb") as f:
        return _image_size(f)


def _image_size(f):
    head = f.read(32)
    if head[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", head[16:24])
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        kind = head[12:16]
        if kind == b"VP8X":
            return int.from_bytes(head[24:27], "little") + 1, int.from_bytes(head[27:30], "little") + 1
        if kind == b"VP8L":
            bits = int.from_bytes(head[21:25], "little")
            return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
        if kind == b"VP8 ":
            w, h = struct.unpack("<HH", head[26:30])
            return w & 0x3FFF, h & 0x3FFF
        return None
    if head[:2] != b"\xff\xd8":
        return None
    f.seek(2)
    while True:   # JPEG: walk the segments to the frame header (SOFn)
        marker = f.read(2)
        if len(marker) < 2 or marker[0] != 0xFF:
            return None
        length = struct.unpack(">H", f.read(2))[0]
        if 0xC0 <= marker[1] <= 0xCF and marker[1] not in (0xC4, 0xC8, 0xCC):
            h, w = struct.unpack(">xHH", f.read(5))
            return w, h
        f.seek(length - 2, 1)


# Opcodes that only build plain data. Anything else (GLOBAL, REDUCE, BUILD, ...)
# could run code when unpickled, so an index using it is refused, never loaded.
_PLAIN = {"PROTO", "FRAME", "STOP", "MARK", "EMPTY_DICT", "DICT", "EMPTY_LIST", "LIST",
          "APPEND", "APPENDS", "EMPTY_TUPLE", "TUPLE", "TUPLE1", "TUPLE2", "TUPLE3", "SETITEM",
          "SETITEMS", "NONE", "NEWTRUE", "NEWFALSE", "INT", "BININT", "BININT1", "BININT2", "LONG",
          "LONG1", "LONG4", "UNICODE", "BINUNICODE", "SHORT_BINUNICODE", "BINUNICODE8", "STRING",
          "BINSTRING", "SHORT_BINSTRING", "BINBYTES", "SHORT_BINBYTES", "BINBYTES8", "MEMOIZE",
          "PUT", "BINPUT", "LONG_BINPUT", "GET", "BINGET", "LONG_BINGET"}


def plain_pickle(data):
    """The value of a pickle made only of dicts, lists, tuples, strings, bytes and ints,
    interpreted opcode by opcode (pickletools); nothing is imported or called."""
    stack, marks, memo = [], [], {}

    def pop_mark():
        k = marks.pop()
        items = stack[k:]
        del stack[k:]
        return items

    for op, arg, _ in pickletools.genops(data):
        n = op.name
        if n not in _PLAIN:
            raise ValueError("pickle opcode %s is not plain data" % n)
        if n in ("PROTO", "FRAME"):
            continue
        if n == "STOP":
            return stack[-1]
        if n == "MARK":
            marks.append(len(stack))
        elif n in ("EMPTY_DICT", "EMPTY_LIST", "EMPTY_TUPLE", "NONE", "NEWTRUE", "NEWFALSE"):
            stack.append({"EMPTY_DICT": {}, "EMPTY_LIST": [], "EMPTY_TUPLE": (), "NONE": None,
                          "NEWTRUE": True, "NEWFALSE": False}[n])
        elif n in ("TUPLE1", "TUPLE2", "TUPLE3"):
            k = int(n[-1])
            stack[-k:] = [tuple(stack[-k:])]
        elif n in ("TUPLE", "LIST"):
            items = pop_mark()
            stack.append(tuple(items) if n == "TUPLE" else items)
        elif n == "DICT":
            items = pop_mark()
            stack.append(dict(zip(items[::2], items[1::2])))
        elif n == "APPEND":
            v = stack.pop()
            stack[-1].append(v)
        elif n == "APPENDS":
            items = pop_mark()
            stack[-1].extend(items)
        elif n == "SETITEM":
            v, key = stack.pop(), stack.pop()
            stack[-1][key] = v
        elif n == "SETITEMS":
            items = pop_mark()
            stack[-1].update(zip(items[::2], items[1::2]))
        elif n == "MEMOIZE":
            memo[len(memo)] = stack[-1]
        elif n in ("PUT", "BINPUT", "LONG_BINPUT"):
            memo[arg] = stack[-1]
        elif n in ("GET", "BINGET", "LONG_BINGET"):
            stack.append(memo[arg])
        else:   # ints, strings, bytes
            stack.append(arg)
    raise ValueError("pickle has no STOP")


def rpa_index(path):
    """{name: (offset, length, prefix)} of a standard RPA-3.0 or RPA-2.0 archive (Ren'Py's
    own format, read as renpy/loader.py does), None for any other format."""
    with open(path, "rb") as f:
        head = f.read(40)
        if head.startswith(b"RPA-3.0 "):
            offset, key = int(head[8:24], 16), int(head[25:33], 16)
        elif head.startswith(b"RPA-2.0 "):
            offset, key = int(head[8:24], 16), 0
        else:
            return None
        f.seek(offset)
        raw = zlib.decompressobj().decompress(f.read(), MAX_RPA_INDEX)
    index, size, out = plain_pickle(raw), os.path.getsize(path), {}
    for name, parts in index.items():
        if not isinstance(name, str) or not parts:
            raise ValueError("bad index entry %r" % (name,))
        o, n = parts[0][0] ^ key, parts[0][1] ^ key
        prefix = parts[0][2] if len(parts[0]) > 2 else b""
        prefix = prefix.encode("latin-1") if isinstance(prefix, str) else prefix
        if o < 0 or n < 0 or o + n > size:
            raise ValueError("entry %r lies outside the archive" % name)
        out[name.replace("\\", "/")] = (o, n, prefix)
    return out


def packable(rel):
    """True for a non-ASCII name that the export puts into a PACKED archive. The Switch SD
    parses names as Shift-JIS and lists none of them (hardware run 20); Ren'Py reads
    names inside an archive itself. It loads no scripts or archives from an archive, so
    those stay loose."""
    return not rel.isascii() and not rel.lower().endswith((".rpy", ".rpym", "_ren.py", ".rpa"))


class Rpa:
    """Writes an RPA-3.0 archive (Ren'Py's own format, key 0), one member at a time."""

    def __init__(self, path):
        self.path, self.index, self.f = path, {}, open(path, "wb")
        self.f.write(b"RPA-3.0 %016x %08x\n" % (0, 0))

    def add(self, name, source):
        start = self.f.tell()
        with open(source, "rb") as src:
            shutil.copyfileobj(src, self.f, 1 << 20)
        self.index[name] = [(start, self.f.tell() - start)]

    def close(self):
        offset = self.f.tell()
        self.f.write(zlib.compress(pickle.dumps(self.index, 2)))
        self.f.seek(0)
        self.f.write(b"RPA-3.0 %016x %08x\n" % (offset, 0))
        self.f.close()


def archive_media(gamedir, files, workdir, warnings):
    """Media inside the game's .rpa archives, as {rel: (archive, offset, length, prefix)},
    plus the archives whose contents cannot be checked."""
    loose = {r for r, _ in files}
    media, locked = {}, []
    for rpa in [r for r in sorted(loose) if r.lower().endswith(".rpa")]:
        try:
            index = rpa_index(os.path.join(gamedir, rpa))
        except (ValueError, zlib.error, OSError, KeyError, IndexError, TypeError) as e:
            locked.append("%s (index not plain data: %s)" % (rpa, e))
            continue
        if index is None:
            locked.append("%s (custom format)" % rpa)
            continue
        for name, entry in index.items():
            try:
                member_parts(name)
            except Refused:
                warnings.append("archive %s has an unsafe name, skipped: %r" % (rpa, name))
                continue
            if name.lower().endswith(VIDEO_EXTS + IMAGE_EXTS) and name not in loose:
                media[name] = (rpa,) + entry
    return media, locked


def read_member(gamedir, member, limit=None):
    rpa, offset, length, prefix = member
    with open(os.path.join(gamedir, rpa), "rb") as f:
        f.seek(offset)
        return prefix + f.read(length if limit is None else min(length, limit))


def extract_member(gamedir, member, target):
    rpa, offset, length, prefix = member
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(os.path.join(gamedir, rpa), "rb") as src, open(target, "wb") as dst:
        dst.write(prefix)
        src.seek(offset)
        left = length
        while left:
            chunk = src.read(min(left, 1 << 20))
            if not chunk:
                break
            dst.write(chunk)
            left -= len(chunk)


def performance(gamedir, files, warnings, optimize, workdir=None):
    """What would load the Switch. Returns (remaining issues, {video: codec} to re-encode,
    {archived video: extracted copy} to export loose, asset coverage, the videos that
    --optimize would re-encode if it were on)."""
    issues, convert, extra = [], {}, {}
    archived, locked = archive_media(gamedir, files, workdir, warnings)
    images = [(r, os.path.join(gamedir, r)) for r, _ in files if r.lower().endswith(IMAGE_EXTS)]
    images += [(r + " (in %s)" % m[0], io.BytesIO(read_member(gamedir, m, 256 << 10)))
               for r, m in sorted(archived.items()) if r.lower().endswith(IMAGE_EXTS)]
    big_images = []
    for label, source in images:
        try:
            size = image_size(source)
        except (OSError, struct.error):
            size = None
        if size and max(size) > MAX_TEXTURE:
            big_images.append("%s %dx%d" % (label, size[0], size[1]))
    if big_images:
        issues.append("%d images exceed the Switch GPU's %dx%d texture limit and may not display "
                      "(%s); shrink them by hand, the game's layout depends on their size"
                      % (len(big_images), MAX_TEXTURE, MAX_TEXTURE, examples(big_images, 2)))
    videos = [(r, os.path.join(gamedir, r), None) for r, _ in files if r.lower().endswith(VIDEO_EXTS)]
    videos += [(r, None, m) for r, m in sorted(archived.items()) if r.lower().endswith(VIDEO_EXTS)]
    coverage = {"loose_media": sum(1 for r, _ in files if r.lower().endswith(VIDEO_EXTS + IMAGE_EXTS)),
                "archived_media": len(archived), "locked_archives": locked}
    if locked:
        issues.append("%d archives could not be checked (%s): their images and videos may not suit "
                      "the Switch" % (len(locked), examples(locked, 2)))
    ffprobe = find_tool(FFPROBE)
    if videos and not ffprobe:
        warnings.append("%d videos not checked: install FFmpeg (ffprobe) or choose its folder "
                        "(--tools) to check their size and frame rate for the Switch" % len(videos))
        return issues, convert, extra, coverage, []
    fast, large, huge, heavy, unfixable, would_fix = [], [], [], [], [], []
    for rel, path, member in videos:
        if member:   # probe a temporary copy; keep it only if it gets re-encoded
            path = os.path.join(workdir or tempfile.gettempdir(), "rpa-media", *rel.split("/"))
            extract_member(gamedir, member, path)
        v = probe_video(ffprobe, path)
        if not v:
            continue
        can_fix = v["fps"] > MAX_VIDEO_FPS + 0.5 and v["codec"] in ENCODERS and not v["alpha"]
        fixable = optimize and can_fix
        # Label what the export will hold: a re-encoded video is MAX_VIDEO_FPS.
        fps = "%d fps (was %.0f)" % (MAX_VIDEO_FPS, v["fps"]) if fixable else "%.0f fps" % v["fps"]
        label = "%s%s %dx%d %s %s" % (rel, " (in %s)" % member[0] if member else "",
                                      v["width"], v["height"], v["codec"], fps)
        if member and fixable:
            extra[rel] = path
        elif member:
            os.remove(path)
        if v["fps"] > MAX_VIDEO_FPS + 0.5:
            if fixable:
                convert[rel] = v["codec"]
            else:
                (unfixable if optimize else fast).append(label)
            if can_fix and not optimize:
                would_fix.append(rel)
        if v["width"] * v["height"] > DOCKED_SCREEN[0] * DOCKED_SCREEN[1]:
            huge.append(label)
        elif v["width"] * v["height"] > SWITCH_SCREEN[0] * SWITCH_SCREEN[1]:
            large.append(label)
        if v["codec"] in HEAVY_CODECS:
            heavy.append(label)
    if fast:
        issues.append("%d videos above %d fps (%s): they cost CPU and heat; --optimize re-encodes "
                      "them at %d fps, same size" % (len(fast), MAX_VIDEO_FPS, examples(fast, 2), MAX_VIDEO_FPS))
    if unfixable:
        issues.append("%d videos above %d fps that --optimize cannot re-encode (alpha or codec): %s"
                      % (len(unfixable), MAX_VIDEO_FPS, examples(unfixable, 2)))
    if huge:
        issues.append("%d videos above 1920x1080 (%s): too heavy to decode smoothly on the Switch; "
                      "not shrunk automatically, because the game may show them at their own size"
                      % (len(huge), examples(huge, 2)))
    if large:
        issues.append("%d videos above the 1280x720 handheld screen (%s): about twice the decoding "
                      "work of 720p" % (len(large), examples(large, 2)))
    if heavy:
        issues.append("%d videos in HEVC/AV1 (%s): the most expensive to decode in software"
                      % (len(heavy), examples(heavy, 2)))
    if convert:
        warnings.append("--optimize re-encodes %d videos at %d fps (same size, constant quality): %s"
                        % (len(convert), MAX_VIDEO_FPS, examples(convert, 3)))
    if extra:
        warnings.append("%d of them come from archives and are exported as loose files, which Ren'Py "
                        "loads before the archive copy" % len(extra))
    return issues, convert, extra, coverage, would_fix


def reencode(source, target, codec):
    """Re-encode a video at MAX_VIDEO_FPS with the same codec family and size; audio is
    copied. Returns an error string, or None."""
    ffmpeg = find_tool(FFMPEG)
    if not ffmpeg:
        return "FFmpeg not found"
    tmp = target + ".part" + os.path.splitext(target)[1]
    r = run([ffmpeg, "-v", "error", "-y", "-i", source, "-map", "0:v:0", "-map", "0:a?",
             "-vf", "fps=%d" % MAX_VIDEO_FPS] + ENCODERS[codec] + ["-c:a", "copy", tmp])
    if r.returncode != 0:
        if os.path.exists(tmp):
            os.remove(tmp)
        return (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:120]
    os.replace(tmp, target)
    return None


def examples(items, n=3):
    items = sorted(items)
    return ", ".join(items[:n]) + (" and %d more" % (len(items) - n) if len(items) > n else "")


def recompile_files(files, warnings, decompile=False):
    """--recompile: drop the Python 2 .rpyc so Ren'Py 8.5.3 compiles the .rpy sources
    with Python 3. Refuses when scripts could be missing their source, unless they are to
    be decompiled. Returns (files to export, compiled scripts to decompile)."""
    names = {r for r, _ in files}
    if any(r.lower().endswith(".rpa") for r in names):
        raise Refused("--recompile: .rpa archives may hold compiled scripts that cannot be recompiled")
    compiled = [r for r in names if r.lower().endswith((".rpyc", ".rpymc"))]
    orphans = sorted(r for r in compiled if r[:-1] not in names)
    outside = [r for r in orphans if not r.startswith("tl/")]
    if outside and not decompile:
        raise Refused("--recompile: %d compiled scripts have no .rpy source (%s); add --decompile to "
                      "rebuild them with unrpyc" % (len(outside), examples(outside)))
    if orphans and not decompile:
        warnings.append("--recompile left out %d compiled translation files with no source (%s); "
                        "those translations may be missing" % (len(orphans), examples(orphans)))
    warnings.append("recompiled: Ren'Py 8.5.3 compiles the .rpy sources with Python 3 on the first "
                    "launch (slow once). Python 2 code can behave differently under Python 3 without "
                    "any error (for example, / on integers gives a float), so check the game's logic; "
                    "saves from the original game will not load")
    return recompiled(files), orphans if decompile else []


def recompiled(files):
    """What --recompile exports: no compiled scripts, no cache/."""
    return [(r, s) for r, s in files if not r.lower().endswith((".rpyc", ".rpymc")) and not r.startswith("cache/")]


def find_unrpyc():
    """unrpyc.py, or None. It needs a Python to run on, which a packaged importer is not."""
    return UNRPYC if os.path.isfile(UNRPYC) and not getattr(sys, "frozen", False) else None


def decompile_scripts(gamedir, compiled, tool, workdir, warnings, rpycdec_python=None):
    """--decompile: rebuild the .rpy of compiled scripts with tool, "unrpyc" or "rpycdec"
    (DECOMPILERS). Both unpickle them, so they run in their own process on copies in
    workdir, never in the importer. A script the tool cannot decompile is refused, or under
    tl/ left out with a warning, as recompile_files() does with sourceless ones.
    rpycdec_python: an interpreter that has rpycdec, else rpycdec on PATH.
    Returns {decompiled .rpy: path}."""
    work = tempfile.mkdtemp(prefix="decompile-", dir=workdir)
    copies = os.path.join(work, "game")
    env = None
    if tool == "unrpyc":
        if not find_unrpyc():
            raise Refused("--decompile: unrpyc not found; fetch it with third_party/unrpyc/fetch.sh and "
                          "run the importer with Python 3.9+ (the packaged program cannot run it)")
        # --no-init-offset: its init offset guess is only safe for Ren'Py 8 (the output is the same game).
        cmd = [sys.executable, UNRPYC, "--no-init-offset", copies]
    else:
        cmd = [rpycdec_python, "-m", "rpycdec"] if rpycdec_python else [find_tool(("rpycdec",))]
        if not cmd[0]:
            raise Refused("--decompile: rpycdec not found on PATH; install it with `pip install rpycdec` "
                          "(Python 3.10+) or give its interpreter with --rpycdec-python")
        cmd, env = cmd + ["decompile", copies], dict(os.environ, RPYCDEC_NO_WARNING="1")
    for rel in compiled:
        target = os.path.join(copies, *rel.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(os.path.join(gamedir, *rel.split("/")), target)
    try:
        # cwd holds only the copies: `python -m` puts it on sys.path.
        r = run(cmd, cwd=work, timeout=DECOMPILE_TIMEOUT, env=env)
    except subprocess.TimeoutExpired:
        raise Refused("--decompile: %s did not finish in %d s" % (tool, DECOMPILE_TIMEOUT))
    except OSError as e:
        raise Refused("--decompile: cannot run %s: %s" % (cmd[0], e))
    if r.returncode:   # rpycdec stops at the first script it cannot read; unrpyc exits 0 then
        err = [l for l in r.stderr.splitlines() if l.strip()] or ["exit %d" % r.returncode]
        why = next((l for l in err if l.startswith("ERROR:")), err[-1]).replace(copies + os.sep, "")
        raise Refused("--decompile: %s failed: %s" % (tool, why[:300]))
    # unrpyc goes on, logging each failure as "Error while decompiling <resolved path>:" and a traceback.
    errors = {os.path.normcase(p) for p in re.findall(r"^Error while decompiling (.+):$", r.stdout, re.M)}
    out, failed = {}, []
    for rel in compiled:
        src = os.path.join(copies, *rel.split("/"))
        if os.path.normcase(os.path.realpath(src)) in errors or not os.path.isfile(src[:-1]):
            failed.append(rel)
        else:
            out[rel[:-1]] = src[:-1]
    cause = re.search(r"^Traceback \(most recent call last\):\n(?:[ \t].*\n)*(.+)$", r.stdout, re.M)
    cause = " (first error: %s)" % cause.group(1)[:200] if cause else ""
    outside = [rel for rel in failed if not rel.startswith("tl/")]
    if outside:
        raise Refused("--decompile: %s could not decompile %d of %d scripts (%s)%s; the game needs "
                      "them" % (tool, len(outside), len(compiled), examples(outside), cause))
    if failed:
        warnings.append("%s could not decompile %d translation files (%s)%s; they are left out, so "
                        "those translations may be missing" % (tool, len(failed), examples(failed), cause))
    mark = {"unrpyc": "<<<COULD NOT DECOMPILE", "rpycdec": "# <unrecognized:"}[tool]
    partial = [rel for rel, p in out.items() if mark in read_text(p)]
    if out:
        warnings.append("decompiled %d scripts that had no .rpy source with %s (%s). Decompiled "
                        "scripts may differ from the originals: comments and formatting are lost and "
                        "statements can be misread, so play the whole game to check"
                        % (len(out), tool, examples(out)))
    if partial:
        warnings.append("%s could not decompile some statements in %s; they are left as comments "
                        "(search for %r) and the game may break there" % (tool, examples(partial), mark))
    return out


def analyze(root, kind, source, game_id=None, extract_warnings=(), recompile=False, optimize=False,
            workdir=None, decompile=None, rpycdec_python=None):
    """Returns (manifest, gamedir, files, {exported rel: path outside gamedir}).
    decompile: None, or the DECOMPILERS tool to rebuild sourceless scripts with; it
    implies recompile."""
    recompile = recompile or bool(decompile)
    warnings = list(extract_warnings)
    reasons = []
    basedir, gamedir, files, skipped = game_files(root)
    if skipped:
        warnings.append("skipped %d links or special files: %s" % (len(skipped), examples(skipped)))
    exts = {}
    for rel, size in files:
        ext = os.path.splitext(rel)[1].lower()
        exts[ext] = exts.get(ext, 0) + 1
    if not any(exts.get(e) for e in SCRIPT_EXTS):
        raise Refused("no Ren'Py scripts (.rpy/.rpyc/.rpa) in %s" % gamedir)

    # Python generation: engine lib/ first, then the .rpyc pickles.
    hints = {}
    lib = lib_python(basedir)
    if lib:
        hints["lib/"] = "/".join(sorted(lib))
    rpyc = {rpyc_python(os.path.join(gamedir, rel)) for rel, _ in files if rel.lower().endswith(".rpyc")} - {None}
    if rpyc:
        hints[".rpyc"] = "/".join(sorted(rpyc))
    py_hint = next((h for h in (hints.get("lib/"), hints.get(".rpyc")) if h in ("2", "3")), None)

    version = renpy_version(basedir, py_hint)
    parent = os.path.dirname(os.path.abspath(basedir))
    if version is None and kind == "directory" and os.path.isdir(os.path.join(parent, "renpy")):
        version = renpy_version(parent, py_hint)
        if version:
            warnings.append("Ren'Py version read from the enclosing %r folder's renpy/ (an SDK "
                            "project or a game/ folder given directly)" % os.path.basename(parent))
    vt = tuple(int(x) for x in version.split(".")[:3]) if version else None

    python = ("3" if vt[0] >= 8 else "2") if vt else (py_hint or "unknown")
    seen = set("/".join(hints.values()).split("/")) | {python}
    if len(seen - {"", "unknown"}) > 1:
        warnings.append("conflicting Python hints: %s; using %s" % (
            ", ".join("%s says %s" % kv for kv in sorted(hints.items())), python))

    if vt and vt[0] == 8:
        if vt[:2] < RUNTIME_VERSION[:2]:
            warnings.append("built with Ren'Py %s; only 8.5.x has run on hardware, and whether "
                            "8.5.3 loads older 8.x .rpyc is untested (docs/COMPATIBILITY.md)" % version)
        elif vt > RUNTIME_VERSION:
            warnings.append("built with Ren'Py %s, newer than the runtime (8.5.3)" % version)
    elif vt and vt[0] > 8:
        reasons.append("Ren'Py %s: no runtime for Ren'Py %d yet" % (version, vt[0]))
    elif python == "3":
        warnings.append("Ren'Py version unknown (no renpy/ folder); the Python 3 .rpyc files "
                        "mean Ren'Py 8.x")
    elif python != "2":
        reasons.append("cannot tell the Ren'Py version or Python generation (no renpy/, lib/ "
                       "or .rpyc files)")
    if recompile and python != "2":
        raise Refused("--recompile is only for Python 2 games (Ren'Py 7 or older)")
    decompiled = {}
    suggest = {}   # importer options that would fix something found here, with why
    if recompile:
        files, orphans = recompile_files(files, warnings, decompile)
        if orphans:
            if decompile == "unrpyc" and vt and vt < (6, 18):
                raise Refused("--decompile: unrpyc decompiles Ren'Py 6.18 and newer, not %s" % version)
            decompiled = decompile_scripts(gamedir, orphans, decompile, workdir, warnings, rpycdec_python)
            files += [(r, os.path.getsize(p)) for r, p in sorted(decompiled.items())]
    elif python == "2":
        reasons.append("%s uses Python 2; there is no Python 2 runtime yet (try --recompile if it "
                       "ships its .rpy sources, --decompile if not)" % ("Ren'Py " + version if vt else "this game"))
        try:
            orphans = recompile_files(files, [], decompile=True)[1]
        except Refused:
            orphans = None   # .rpa archives: the reason above says what to try
        if orphans is not None and all(r.startswith("tl/") for r in orphans):
            suggest["recompile"] = ("a Python 2 game that ships all its .rpy sources: Ren'Py 8.5.3 can "
                                    "compile them with Python 3, so it can be exported (an experiment)")
        if orphans:
            suggest["decompile"] = ("%d compiled scripts were never decompiled: they have no .rpy source "
                                    "(%s). unrpyc or rpycdec can rebuild them, then Ren'Py 8.5.3 compiles "
                                    "the game with Python 3 (an experiment)" % (len(orphans), examples(orphans)))
    exts = {}
    for rel, _ in files:
        ext = os.path.splitext(rel)[1].lower()
        exts[ext] = exts.get(ext, 0) + 1

    native = [r for r, _ in files if r.lower().endswith(NATIVE_EXTS)]
    if native:
        warnings.append("native code that cannot load on the Switch: %s; the game fails if it "
                        "imports it" % examples(native))
    if os.path.isdir(os.path.join(gamedir, "python-packages")):
        warnings.append("game/python-packages present: must be pure Python 3.12 and avoid ctypes, "
                        "ssl, sqlite3, mmap, subprocess, multiprocessing")
    pys = [r for r, _ in files if r.endswith(".py") and not r.endswith("_ren.py")
           and not r.startswith("python-packages/")]
    if pys:
        warnings.append("%d Python modules in game/ (%s): same limits as python-packages" % (
            len(pys), examples(pys)))
    rpa_bad = []
    for rel, _ in files:
        if rel.lower().endswith(".rpa"):
            with open(os.path.join(gamedir, rel), "rb") as f:
                if f.read(8) not in (b"RPA-3.0 ", b"RPA-2.0 "):
                    rpa_bad.append(rel)
    if rpa_bad:
        warnings.append("archives with a non-standard header: %s; the game needs its own "
                        "loader for them (untested)" % examples(rpa_bad))

    media = {}
    for rel, size in files:
        ext = os.path.splitext(rel)[1].lower()
        if ext in AUDIO_EXTS or ext in VIDEO_EXTS or ext in NO_PLAY_EXTS:
            media[ext] = media.get(ext, 0) + 1
    midi = [r for r, _ in files if r.lower().endswith(NO_PLAY_EXTS)]
    if midi:
        warnings.append("Ren'Py cannot play MIDI: %s" % examples(midi))
    issues, convert, extra, coverage, would_fix = performance(gamedir, files, warnings, optimize, workdir)
    if would_fix:
        suggest["optimize"] = ("%d videos above %d fps (%s): re-encoding them at %d fps, same size, "
                               "saves the Switch CPU work and heat" % (len(would_fix), MAX_VIDEO_FPS,
                                                                       examples(would_fix, 2), MAX_VIDEO_FPS))
    extra.update(decompiled)
    big =[r for r, s in files if s >= LARGE_FILE_BYTES]
    if big:
        warnings.append("files of 2 GiB or more (%s): FAT32 SD cards cannot hold files of 4 GiB "
                        "or more; use exFAT" % examples(big))
    packed = [r for r, _ in files if packable(r)]
    if packed:
        warnings.append("%d files with non-ASCII names (%s) go into %s.rpa: the Switch SD cannot list "
                        "such names (hardware run 20)" % (len(packed), examples(packed), PACKED))
    loose = [r for r, _ in files if not r.isascii() and not packable(r)]
    if loose:
        warnings.append("scripts or archives with non-ASCII names (%s): Ren'Py cannot load them from an "
                        "archive, and the Switch SD cannot list them; the game may not find them" % examples(loose))

    title, default_id = title_and_id(root, kind, source, basedir, gamedir)
    game_id = default_id if game_id is None else game_id
    status = "unsupported" if reasons else "experimental"
    manifest = {
        "format": MANIFEST_FORMAT,
        "version": 1,
        "id": game_id,
        "title": title,
        "source": {"type": kind, "name": os.path.basename(os.path.normpath(source))},
        "renpy_version": version,
        "python": python,
        "runtime": RUNTIME if status == "experimental" else None,
        "status": status,
        "reasons": reasons,
        "warnings": warnings,
        "files": {"count": len(files), "total_bytes": sum(s for _, s in files),
                  "rpa": exts.get(".rpa", 0), "rpy": exts.get(".rpy", 0),
                  "rpyc": exts.get(".rpyc", 0), "rpym": exts.get(".rpym", 0)},
        "media": dict(sorted(media.items())),
        "recompile": recompile,
        "suggested_options": suggest,
        "decompiled": sorted(decompiled),
        "performance": {"issues": issues, "converted": convert, "coverage": coverage},
        "sha256": script_hashes(gamedir, files),
    }
    return manifest, gamedir, files, extra


def game_files(root):
    """The game directory and the files an export takes from it: (basedir, gamedir,
    [(rel, size)], skipped links)."""
    basedir, gamedir = find_gamedir(root)
    flat = gamedir == basedir   # scripts sit in basedir: leave engine/launchers out
    excludes = (lambda n: n == "saves" or n in ("renpy", "lib") or n.lower().endswith(
        (".exe", ".app", ".sh"))) if flat else (lambda n: n == "saves")
    return (basedir, gamedir) + scan(gamedir, excludes)


def title_and_id(root, kind, source, basedir, gamedir):
    """config.name or the folder/archive name, and the id made from it."""
    if basedir == root and kind != "directory":
        folder = os.path.splitext(os.path.basename(source))[0]
    else:
        folder = os.path.basename(os.path.normpath(basedir))
        if gamedir == basedir and folder == "game":   # a game/ folder given directly
            folder = os.path.basename(os.path.dirname(os.path.abspath(basedir)))
        app = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(basedir))))
        if folder == "autorun" and app.endswith(".app"):   # <Name>.app/Contents/Resources/autorun
            folder = os.path.basename(app)[:-4]
    title = options_title(gamedir) or folder
    return title, make_id(title) if make_id(title) != "game" else make_id(folder)


def script_hashes(gamedir, files):
    return {rel: sha256(os.path.join(gamedir, rel)) for rel, _ in files if rel.lower().endswith((".rpa", ".rpyc"))}


def load(source, tmp, game_id=None, recompile=False, optimize=False, decompile=None, rpycdec_python=None):
    """unpack() then analyze(): (manifest, gamedir, files, extra). An archive is unpacked
    into tmp, which must stay until export() is done."""
    warnings = []
    root, kind = unpack(source, tmp, warnings)
    return analyze(root, kind, source, game_id, warnings, recompile, optimize, tmp, decompile, rpycdec_python)


# -------------------------------------------------------------------- export

def is_ftp(out):
    return out.lower().startswith("ftp://")


def games_dir(out):
    """<out>/switch/renpy-loader/games. A bare drive ("E:") means its root; on Windows
    "E:" alone is drive E's current folder."""
    if is_ftp(out):
        return "%s/%s/games" % (out.rstrip("/"), LIBRARY)
    if re.fullmatch(r"[A-Za-z]:", out):
        out += os.sep
    return os.path.join(out, *LIBRARY.split("/"), "games")


class Local:
    """An export root on disk (the SD card in a reader). Paths are relative to
    switch/renpy-loader and use '/'; Ftp has the same calls."""

    def __init__(self, out):
        self.root = os.path.dirname(games_dir(out))

    def path(self, p):
        return os.path.join(self.root, *p.split("/"))

    def entries(self, p):
        """{name: is_dir}; {} when p is missing."""
        d = self.path(p)
        return {n: os.path.isdir(os.path.join(d, n)) for n in os.listdir(d)} if os.path.isdir(d) else {}

    def read(self, p):
        try:
            with open(self.path(p), "rb") as f:
                return f.read()
        except OSError:
            return None

    def rmtree(self, p):
        rmtree(self.path(p))
        if os.path.lexists(self.path(p)):
            raise OSError("could not delete all of %s" % self.path(p))

    def close(self):
        pass


class Ftp:
    """The Switch's SD card over FTP: sphaira's server, anonymous, ftp://HOST[:PORT], where
    / is the SD card root. It runs only while sphaira shows it, and its address can change."""

    def __init__(self, out):
        u = urllib.parse.urlsplit(out.strip())
        try:
            host, port = u.hostname, u.port or FTP_PORT
        except ValueError:
            host = None
        if not host:
            raise Refused("Switch address %r: use HOST:PORT, for example 192.168.1.20:%d" % (out, FTP_PORT))
        self.ftp, self.made = ftplib.FTP(), set()
        try:
            self.ftp.connect(host, port, FTP_TIMEOUT)
            self.ftp.login()
        except ftplib.all_errors as e:
            self.ftp.close()
            raise Refused("cannot reach the Switch at %s:%d (%s). Turn on sphaira's FTP server and "
                          "check the address it shows; it can change." % (host, port, e))

    def path(self, p):
        return "/%s/%s" % (LIBRARY, p)

    def entries(self, p):
        """{name: is_dir}; {} when p is missing."""
        try:
            return {posixpath.basename(n): f["type"].lower() == "dir" for n, f in self.ftp.mlsd(self.path(p))
                    if f.get("type", "").lower() in ("dir", "file")}
        except (ftplib.error_perm, ftplib.error_temp):
            try:
                self.ftp.cwd(self.path(p))   # servers answer a missing folder with 450, 501 or 550
            except (ftplib.error_perm, ftplib.error_temp):
                return {}
            raise

    def read(self, p):
        buf = io.BytesIO()
        try:
            self.ftp.retrbinary("RETR " + self.path(p), buf.write)
        except (ftplib.error_perm, ftplib.error_temp):
            return None
        return buf.getvalue()

    def mkdirs(self, p):
        parts = self.path(p).split("/")
        for i in range(2, len(parts) + 1):
            d = "/".join(parts[:i])
            if d not in self.made:
                try:
                    self.ftp.mkd(d)
                except ftplib.error_perm:
                    pass   # it exists
                self.made.add(d)

    def put(self, local, p, tick=lambda fraction: None):
        """Uploads a file; tick(fraction sent) runs after each block and may raise to stop."""
        self.mkdirs(posixpath.dirname(p))
        size, sent = max(os.path.getsize(local), 1), [0]

        def block(data):
            sent[0] += len(data)
            tick(sent[0] / size)
        with open(local, "rb") as f:
            self.ftp.storbinary("STOR " + self.path(p), f, FTP_BLOCK, block)

    def write(self, p, data):
        self.ftp.storbinary("STOR " + self.path(p), io.BytesIO(data))

    def rename(self, a, b):
        self.ftp.rename(self.path(a), self.path(b))

    def rmtree(self, p):
        self.made = {d for d in self.made if not d.startswith(self.path(p))}
        try:
            return self.ftp.rmd(self.path(p))   # a server may remove a whole folder at once
        except ftplib.error_perm:
            pass
        for name, is_dir in self.entries(p).items():
            if is_dir:
                self.rmtree(p + "/" + name)
            else:
                self.ftp.delete(self.path(p + "/" + name))
        self.ftp.rmd(self.path(p))

    def close(self):
        self.ftp.close()


def library(out):
    """The export root out as Local or Ftp."""
    return Ftp(out) if is_ftp(out) else Local(out)


def exported_games(out):
    """[(id, manifest)] for every games/<id>/ under an export root; manifest is None when
    there is no readable manifest.json (a game copied by hand)."""
    found = []
    with contextlib.closing(library(out)) as lib:
        names = lib.entries("games")
        for name in sorted(names):
            if name.startswith(".") or not names[name]:
                continue   # staging folders, stray files
            try:
                manifest = json.loads(lib.read("games/%s/manifest.json" % name) or b"null")
            except ValueError:
                manifest = None
            found.append((name, manifest if isinstance(manifest, dict) else None))
    return found


def delete_game(out, game_id, saves=False):
    """Removes games/<id>/, its game/saves included: Ren'Py writes every save to saves/<id>
    as well (savelocation.py, MultiLocation.save). saves=True also removes saves/<id>/."""
    with contextlib.closing(library(out)) as lib:
        if game_id.startswith(".") or game_id not in lib.entries("games"):
            raise Refused("%s/%s is not there" % (games_dir(out), game_id))
        lib.rmtree("games/" + game_id)
        if saves and game_id in lib.entries("saves"):
            lib.rmtree("saves/" + game_id)


COMPARE_ORDER = ("different", "missing", "only", "unknown", "unreadable", "same")


def source_name(manifest):
    try:
        return manifest["source"]["name"]
    except (KeyError, TypeError):
        return None


def differences(manifest, gamedir, files):
    """Why an export no longer matches its source, by what its manifest recorded from that
    source; [] when it matches."""
    if manifest.get("recompile"):
        files = recompiled(files)
    decompiled = manifest.get("decompiled") or []   # rebuilt .rpy: counted, but their sizes are unrpyc's
    count, total = len(files) + len(decompiled), sum(s for _, s in files)
    why = []
    if count != manifest["files"]["count"]:
        why.append("%d files in the folder, %d in the library" % (count, manifest["files"]["count"]))
    delta = total - manifest["files"]["total_bytes"]
    if not decompiled and delta:
        n = abs(delta)
        why.append("%s %s in the folder" % ("%.1f MB" % (n / 1e6) if n >= 1e6 else "{:,} byte{}".format(n, "s" * (n != 1)),
                                            "more" if delta > 0 else "less"))
    if not why:   # same files and sizes: compare the scripts themselves
        hashes, old = script_hashes(gamedir, files), manifest["sha256"]
        changed = sorted(r for r in set(hashes) | set(old) if hashes.get(r) != old.get(r))
        if changed:
            why.append("%d scripts or archives differ: %s" % (len(changed), examples(changed)))
    return why


def compare(collection, out, progress=None):
    """Read-only: every game in collection (game folders and .zip/.7z/.rar files) against
    the library at out. Returns rows {state, source, id, title, why}, state being one of
    COMPARE_ORDER: missing = not in the library, only = only in the library, unknown = in
    the library without a manifest, unreadable = not an importable game. Archives are
    unpacked into a temp folder, one at a time. progress(done, total, name) may raise to stop."""
    progress = progress or (lambda *a: None)
    exported = dict(exported_games(out))
    by_source = {source_name(m): i for i, m in exported.items() if source_name(m)}
    names = sorted(n for n in os.listdir(collection) if not n.startswith(".") and not is_junk(n) and (
        os.path.isdir(os.path.join(collection, n)) or n.lower().endswith((".zip", ".7z", ".rar"))))
    rows, matched = [], set()
    for done, name in enumerate(names):
        progress(done, len(names), name)
        row = {"source": name, "id": None, "title": name, "why": []}
        rows.append(row)
        tmp = tempfile.mkdtemp(prefix="rsl-compare-")
        try:
            source = os.path.join(collection, name)
            # ponytail: unpacks every archive on every compare (minutes for GBs); recording the archive's
            # size and sha256 in the manifest would let an unchanged archive skip it.
            root, kind = unpack(source, tmp, [])
            basedir, gamedir, files, _ = game_files(root)
            row["title"], gid = title_and_id(root, kind, source, basedir, gamedir)
            row["id"] = match = by_source.get(name) or (gid if gid in exported else None)
            if match is None:
                row["id"], row["state"] = gid, "missing"
                continue
            matched.add(match)
            m = exported[match]
            if m is None:
                row["state"], row["why"] = "unknown", ["no manifest.json in the library (copied by hand)"]
                continue
            if source_name(m) not in (None, name):
                row["why"].append("the library copy came from %s" % source_name(m))
            try:
                diff = differences(m, gamedir, files)
            except (KeyError, TypeError, AttributeError):
                row["state"], row["why"] = "unknown", ["the library's manifest.json is from an older or edited importer"]
                continue
            row["state"] = "different" if diff else "same"
            row["why"] = diff + row["why"]
        except (Refused, OSError) as e:
            row["state"], row["why"] = "unreadable", [str(e)]
        finally:
            rmtree(tmp)
    progress(len(names), len(names), "")
    for i, m in exported.items():
        if i not in matched:
            rows.append({"state": "only", "source": None, "id": i, "title": (m or {}).get("title") or i, "why": []})
    return sorted(rows, key=lambda r: (COMPARE_ORDER.index(r["state"]), str(r["title"]).lower()))


def manifest_json(manifest):
    return json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"


def copy_files(manifest, gamedir, files, extra, progress, put, workdir):
    """Passes every exported file to put(local path, rel, tick); a video to re-encode is
    written into workdir first, and packable() files go into PACKED archives built there.
    tick(fraction) reports progress within a file."""
    convert = manifest["performance"]["converted"]
    extra = extra or {}
    todo = list(dict.fromkeys([r for r, _ in files] + sorted(extra)))   # decompiled .rpy are in both
    recode = [r for r in todo if r in convert]
    rpa, count = None, 0

    def finish(done):
        rpa.close()
        rel = PACKED + ("-%d" % count if count > 1 else "") + ".rpa"
        progress(done, len(todo), rel, "")
        put(rpa.path, rel, lambda fraction: progress(done, len(todo), rel, ""))
        os.remove(rpa.path)

    try:
        for done, rel in enumerate(todo):
            source = extra.get(rel) or os.path.join(gamedir, *rel.split("/"))
            if rel in convert:
                progress(done, len(todo), rel, "re-encoding %d/%d" % (recode.index(rel) + 1, len(recode)))
                target = os.path.join(workdir, "reencoded" + os.path.splitext(rel)[1])
                error = reencode(source, target, convert[rel])
                if error:
                    del convert[rel]
                    manifest["performance"]["issues"].append("%s: re-encoding failed (%s); copied unchanged" % (rel, error))
                else:
                    source = target
            else:
                progress(done, len(todo), rel, "")
            if not packable(rel):
                put(source, rel, lambda fraction, done=done, rel=rel: progress(done + fraction, len(todo), rel, ""))
                continue
            if rpa and rpa.index and rpa.f.tell() + os.path.getsize(source) > PACKED_MAX:
                finish(done)
                rpa = None
            if rpa is None:
                count += 1
                rpa = Rpa(os.path.join(workdir, "packed.rpa"))
            rpa.add(unicodedata.normalize("NFC", rel), source)   # as a script spells it; macOS may hand back NFD
        if rpa:
            finish(len(todo))
    except BaseException:
        if rpa:
            rpa.f.close()   # workdir is removed by the caller, also on Windows
        raise
    progress(len(todo), len(todo), "", "")


def export(manifest, gamedir, files, out, replace, extra=None, progress=None):
    """Writes games/<id>/game and manifest.json. Refuses to overwrite unless replace;
    keeps game/saves (Ren'Py writes saves there too) and never touches saves/<id>.
    progress(done, total, rel, note) is called before each file, note being "re-encoding
    1/2" or ""; if it raises, the staged copy is removed and the old export stays as it was.
    Over FTP it is also called during an upload, with done + the fraction sent."""
    if manifest["status"] == "unsupported":
        raise Refused("not exported: no runtime can run this game")
    progress = progress or (lambda *a: None)
    if is_ftp(out):
        return ftp_export(manifest, gamedir, files, out, replace, extra, progress)
    games = games_dir(out)
    dest = os.path.join(games, manifest["id"])
    if os.path.lexists(dest) and not replace:
        raise Refused("%s already exists; pass --replace to update its game/ and manifest.json "
                      "(saves are kept)" % dest)
    os.makedirs(games, exist_ok=True)
    stage = tempfile.mkdtemp(prefix=".rsl-import-", dir=games)   # same volume: renames are atomic
    try:
        new = os.path.join(stage, "game")

        def put(source, rel, tick):
            target = os.path.join(new, *rel.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(source, target)
        copy_files(manifest, gamedir, files, extra, progress, put, stage)
        os.makedirs(new, exist_ok=True)
        with open(os.path.join(stage, "manifest.json"), "w", encoding="utf-8") as f:
            f.write(manifest_json(manifest))
        os.makedirs(dest, exist_ok=True)
        old = os.path.join(dest, "game")
        if os.path.isdir(os.path.join(old, "saves")):
            os.rename(os.path.join(old, "saves"), os.path.join(new, "saves"))
        if os.path.lexists(old):
            os.rename(old, os.path.join(stage, "old-game"))
        os.rename(new, old)
        os.replace(os.path.join(stage, "manifest.json"), os.path.join(dest, "manifest.json"))
    finally:
        rmtree(stage)
    return dest


def ftp_export(manifest, gamedir, files, out, replace, extra, progress):
    """export() to the Switch: uploads into games/.uploading-<id>, then renames it into
    place. The previous version waits in games/.old-<id> until its game/saves is moved over."""
    gid = manifest["id"]
    dest, stage, old = "games/" + gid, "games/.uploading-" + gid, "games/.old-" + gid
    with contextlib.closing(Ftp(out)) as ftp:
        have = ftp.entries("games")
        if gid in have and not replace:
            raise Refused("%s/%s already exists; replace it to update its game/ and manifest.json "
                          "(saves are kept)" % (games_dir(out), gid))
        if ".old-" + gid in have:
            raise Refused("%s/.old-%s is left from an update that stopped half way and may hold saves "
                          "(game/saves); move what you need out of it, delete it, then try again"
                          % (games_dir(out), gid))
        if ".uploading-" + gid in have:
            ftp.rmtree(stage)   # a cancelled or failed upload; it never holds saves
        try:
            work = tempfile.mkdtemp(prefix="rsl-import-")
            try:
                copy_files(manifest, gamedir, files, extra, progress,
                           lambda source, rel, tick: ftp.put(source, stage + "/game/" + rel, tick), work)
            finally:
                rmtree(work)
            ftp.mkdirs(stage + "/game")
            ftp.write(stage + "/manifest.json", manifest_json(manifest).encode("utf-8"))
        except BaseException:
            ftp.close()   # it may be mid-transfer: clean up over a new connection
            with contextlib.suppress(*ERRORS), contextlib.closing(Ftp(out)) as again:
                again.rmtree(stage)
            raise
        try:
            if gid in have:
                ftp.rename(dest, old)
            ftp.rename(stage, dest)
            if gid in have:
                if "saves" in ftp.entries(old + "/game"):
                    ftp.rename(old + "/game/saves", dest + "/game/saves")
                ftp.rmtree(old)
        except ftplib.all_errors as e:
            raise Refused("the upload is complete, but moving it into place stopped (%s). In %s, "
                          ".uploading-%s is the new copy and .old-%s the previous one with its game/saves"
                          % (e, games_dir(out), gid, gid))
    return games_dir(out) + "/" + gid


def report(m):
    lines = ["%s  (id: %s)" % (m["title"], m["id"]),
             "  Source:  %s %s" % (m["source"]["type"], m["source"]["name"]),
             "  Ren'Py:  %s   Python: %s" % (m["renpy_version"] or "unknown", m["python"]),
             "  Status:  %s" % (m["status"].upper() + (" on runtime " + m["runtime"] if m["runtime"] else "")),
             "  Files:   %d in game/, %.1f MB (rpa %d, rpy %d, rpyc %d, rpym %d)" % (
                 m["files"]["count"], m["files"]["total_bytes"] / 1e6, m["files"]["rpa"],
                 m["files"]["rpy"], m["files"]["rpyc"], m["files"]["rpym"]),
             "  Media:   %s" % (", ".join("%s %d" % kv for kv in m["media"].items()) or "none")]
    for key in ("reasons", "warnings"):
        if m[key]:
            lines.append("  %s:" % ("Why unsupported" if key == "reasons" else "Warnings"))
            lines += ["    - " + w for w in m[key]]
    cov = m["performance"]["coverage"]
    lines.append("  Checked: %d loose + %d archived images/videos; %d locked archives" % (
        cov["loose_media"], cov["archived_media"], len(cov["locked_archives"])))
    issues = m["performance"]["issues"]
    lines.append("  Switch performance: %s" % ("%d issue(s), shown in the loader" % len(issues) if issues else "OK"))
    lines += ["    ! " + i for i in issues]
    lines += ["  Suggested: --%s: %s" % kv for kv in m.get("suggested_options", {}).items()]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Analyse a Ren'Py game and export it for the Switch loader.")
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
        print(report(manifest), flush=True)
        if a.out:
            print("Exported to %s" % export(manifest, gamedir, files, a.out, a.replace, extra, show))
        return 0
    except ERRORS as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    finally:
        rmtree(tmp)


if __name__ == "__main__":
    sys.exit(main())
