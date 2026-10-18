#!/usr/bin/env python3
"""Ren'Py Switch Loader importer (Milestone 5).

Analyses a DRM-free Ren'Py game (folder, .zip, .7z or .rar), writes a compatibility
report and, with --out, an SD-ready tree:

    <out>/switch/renpy-loader/games/<id>/game/...
    <out>/switch/renpy-loader/games/<id>/manifest.json

Standard library only; runs on macOS /usr/bin/python3 (3.9). It never executes game
code, never unpickles anything and never writes to the source. See docs/IMPORTER.md.

usage: python3 importer/rsl_import.py SOURCE [--out DIR] [--id ID] [--replace]
"""
import argparse
import hashlib
import io
import json
import os
import pickletools
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
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
SEVEN_ZIP = ("7z", "/opt/homebrew/bin/7z", "/usr/local/bin/7z")

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
JUNK = {".DS_Store", "Thumbs.db", "desktop.ini", "__MACOSX"}
LAUNCHER_EXTS = (".exe", ".sh", ".py", ".app")


class Refused(Exception):
    """The source cannot be imported; the message says why."""


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
                where = os.path.normpath(os.path.join("/r", *parts[:-1], target))
                if target.startswith("/") or not where.startswith("/r/"):
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


def seven_zip():
    for exe in SEVEN_ZIP:
        found = shutil.which(exe)
        if found:
            return found
    raise Refused("7z not found; install it with `brew install p7zip`")


def run_7z(args):
    env = dict(os.environ, LC_ALL="en_US.UTF-8")
    return subprocess.run([seven_zip()] + args, stdin=subprocess.DEVNULL, capture_output=True,
                          text=True, errors="surrogateescape", env=env)


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


def find_tool(names):
    return next((shutil.which(n) for n in names if shutil.which(n)), None)


def probe_video(ffprobe, path):
    """{codec, width, height, fps, alpha} of the first video stream, or None."""
    r = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                        "stream=codec_name,width,height,avg_frame_rate,r_frame_rate:stream_tags=alpha_mode",
                        "-of", "json", path], capture_output=True, text=True, stdin=subprocess.DEVNULL)
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
    {archived video: extracted copy} to export loose, asset coverage)."""
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
        warnings.append("%d videos not checked: install FFmpeg (ffprobe) to check their size and "
                        "frame rate for the Switch" % len(videos))
        return issues, convert, extra, coverage
    fast, large, huge, heavy, unfixable = [], [], [], [], []
    for rel, path, member in videos:
        if member:   # probe a temporary copy; keep it only if it gets re-encoded
            path = os.path.join(workdir or tempfile.gettempdir(), "rpa-media", *rel.split("/"))
            extract_member(gamedir, member, path)
        v = probe_video(ffprobe, path)
        if not v:
            continue
        fixable = optimize and v["fps"] > MAX_VIDEO_FPS + 0.5 and v["codec"] in ENCODERS and not v["alpha"]
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
    return issues, convert, extra, coverage


def reencode(source, target, codec):
    """Re-encode a video at MAX_VIDEO_FPS with the same codec family and size; audio is
    copied. Returns an error string, or None."""
    ffmpeg = find_tool(FFMPEG)
    if not ffmpeg:
        return "FFmpeg not found"
    tmp = target + ".part" + os.path.splitext(target)[1]
    r = subprocess.run([ffmpeg, "-v", "error", "-y", "-i", source, "-map", "0:v:0", "-map", "0:a?",
                        "-vf", "fps=%d" % MAX_VIDEO_FPS] + ENCODERS[codec] + ["-c:a", "copy", tmp],
                       capture_output=True, text=True, stdin=subprocess.DEVNULL)
    if r.returncode != 0:
        if os.path.exists(tmp):
            os.remove(tmp)
        return (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:120]
    os.replace(tmp, target)
    return None


def examples(items, n=3):
    items = sorted(items)
    return ", ".join(items[:n]) + (" and %d more" % (len(items) - n) if len(items) > n else "")


def recompile_files(files, warnings):
    """--recompile: drop the Python 2 .rpyc so Ren'Py 8.5.3 compiles the .rpy sources
    with Python 3. Refuses when scripts could be missing their source."""
    names = {r for r, _ in files}
    if any(r.lower().endswith(".rpa") for r in names):
        raise Refused("--recompile: .rpa archives may hold compiled scripts that cannot be recompiled")
    compiled = [r for r in names if r.lower().endswith((".rpyc", ".rpymc"))]
    orphans = [r for r in compiled if r[:-1] not in names]
    outside = [r for r in orphans if not r.startswith("tl/")]
    if outside:
        raise Refused("--recompile: %d compiled scripts have no .rpy source (%s)" % (len(outside), examples(outside)))
    if orphans:
        warnings.append("--recompile left out %d compiled translation files with no source (%s); "
                        "those translations may be missing" % (len(orphans), examples(orphans)))
    warnings.append("recompiled: Ren'Py 8.5.3 compiles the .rpy sources with Python 3 on the first "
                    "launch (slow once). Python 2 code can behave differently under Python 3 without "
                    "any error (for example, / on integers gives a float), so check the game's logic; "
                    "saves from the original game will not load")
    return [(r, s) for r, s in files if not r.lower().endswith((".rpyc", ".rpymc")) and not r.startswith("cache/")]


def analyze(root, kind, source, game_id=None, extract_warnings=(), recompile=False, optimize=False,
            workdir=None):
    """Returns (manifest, gamedir, files) for the game under root."""
    warnings = list(extract_warnings)
    reasons = []
    basedir, gamedir = find_gamedir(root)
    flat = gamedir == basedir   # scripts sit in basedir: leave engine/launchers out
    excludes = (lambda n: n == "saves" or n in ("renpy", "lib") or n.lower().endswith(
        (".exe", ".app", ".sh"))) if flat else (lambda n: n == "saves")
    files, skipped = scan(gamedir, excludes)
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
    if recompile:
        files = recompile_files(files, warnings)
    elif python == "2":
        reasons.append("%s uses Python 2; there is no Python 2 runtime yet (try --recompile if it "
                       "ships its .rpy sources)" % ("Ren'Py " + version if vt else "this game"))
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
    issues, convert, extra, coverage = performance(gamedir, files, warnings, optimize, workdir)
    big = [r for r, s in files if s >= LARGE_FILE_BYTES]
    if big:
        warnings.append("files of 2 GiB or more (%s): FAT32 SD cards cannot hold files of 4 GiB "
                        "or more; use exFAT" % examples(big))
    nonascii = [r for r, _ in files if not r.isascii()]
    if nonascii:
        warnings.append("%d files with non-ASCII names (%s): creating such names on the Switch SD "
                        "failed in hardware run 4; the game may not find them" % (
                            len(nonascii), examples(nonascii)))

    if basedir == root and kind != "directory":
        folder = os.path.splitext(os.path.basename(source))[0]
    else:
        folder = os.path.basename(os.path.normpath(basedir))
        if flat and folder == "game":   # a game/ folder given directly
            folder = os.path.basename(os.path.dirname(os.path.abspath(basedir)))
        app = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(basedir))))
        if folder == "autorun" and app.endswith(".app"):   # <Name>.app/Contents/Resources/autorun
            folder = os.path.basename(app)[:-4]
    title = options_title(gamedir) or folder
    if game_id is None:
        game_id = make_id(title) if make_id(title) != "game" else make_id(folder)
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
        "performance": {"issues": issues, "converted": convert, "coverage": coverage},
        "sha256": {rel: sha256(os.path.join(gamedir, rel)) for rel, _ in files
                   if rel.lower().endswith((".rpa", ".rpyc"))},
    }
    return manifest, gamedir, files, extra


# -------------------------------------------------------------------- export

def export(manifest, gamedir, files, out, replace, extra=None):
    """Writes games/<id>/game and manifest.json. Refuses to overwrite unless replace;
    keeps game/saves (Ren'Py writes saves there too) and never touches saves/<id>."""
    games = os.path.join(out, "switch", "renpy-loader", "games")
    dest = os.path.join(games, manifest["id"])
    if os.path.lexists(dest) and not replace:
        raise Refused("%s already exists; pass --replace to update its game/ and manifest.json "
                      "(saves are kept)" % dest)
    os.makedirs(games, exist_ok=True)
    stage = tempfile.mkdtemp(prefix=".rsl-import-", dir=games)   # same volume: renames are atomic
    try:
        new = os.path.join(stage, "game")
        convert = manifest["performance"]["converted"]
        extra = extra or {}
        for rel, _ in list(files) + [(r, 0) for r in sorted(extra)]:
            source = extra.get(rel) or os.path.join(gamedir, *rel.split("/"))
            target = os.path.join(new, *rel.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if rel in convert:
                print("  re-encoding %s (%d/%d)" % (rel, list(convert).index(rel) + 1, len(convert)), flush=True)
                error = reencode(source, target, convert[rel])
                if error:
                    del convert[rel]
                    manifest["performance"]["issues"].append("%s: re-encoding failed (%s); copied unchanged" % (rel, error))
                    shutil.copyfile(source, target)
            else:
                shutil.copyfile(source, target)
        os.makedirs(new, exist_ok=True)
        with open(os.path.join(stage, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
            f.write("\n")
        os.makedirs(dest, exist_ok=True)
        old = os.path.join(dest, "game")
        if os.path.isdir(os.path.join(old, "saves")):
            os.rename(os.path.join(old, "saves"), os.path.join(new, "saves"))
        if os.path.lexists(old):
            os.rename(old, os.path.join(stage, "old-game"))
        os.rename(new, old)
        os.replace(os.path.join(stage, "manifest.json"), os.path.join(dest, "manifest.json"))
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    return dest


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
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Analyse a Ren'Py game and export it for the Switch loader.")
    ap.add_argument("source", help="game folder, .zip, .7z or .rar")
    ap.add_argument("--out", help="export root, e.g. the SD card; writes switch/renpy-loader/games/<id>")
    ap.add_argument("--id", help="game id (folder name); default from the title")
    ap.add_argument("--replace", action="store_true", help="update an existing export (saves are kept)")
    ap.add_argument("--optimize", action="store_true",
                    help="re-encode videos above 30 fps at 30 fps (same size, needs FFmpeg)")
    ap.add_argument("--recompile", action="store_true",
                    help="Python 2 game with .rpy sources: export without .rpyc for Ren'Py 8 to recompile")
    a = ap.parse_args(argv)
    if a.id is not None and make_id(a.id) != a.id:
        ap.error("--id must be lowercase a-z, 0-9 and '-', at most %d chars (try %r)" % (ID_MAX, make_id(a.id)))
    tmp = tempfile.mkdtemp(prefix="rsl-import-")
    try:
        extract_warnings = []
        root, kind = unpack(a.source, tmp, extract_warnings)
        manifest, gamedir, files, extra = analyze(root, kind, a.source, a.id, extract_warnings, a.recompile,
                                                  a.optimize, tmp)
        print(report(manifest), flush=True)
        if a.out:
            if manifest["status"] == "unsupported":
                raise Refused("not exported: no runtime can run this game")
            print("Exported to %s" % export(manifest, gamedir, files, a.out, a.replace, extra))
        return 0
    except (Refused, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
