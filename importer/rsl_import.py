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
import json
import os
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
# What Ren'Py documents and the runtime's FFmpeg is expected to decode.
MEDIA_OK = {".ogg", ".opus", ".mp3", ".wav", ".flac", ".webm", ".ogv", ".mkv"}
MEDIA_ODD = {".mp4", ".m4v", ".m4a", ".aac", ".avi", ".wmv", ".wma", ".mpg", ".mpeg",
             ".mov", ".flv", ".mp2", ".mid", ".midi"}
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
            autorun = os.path.join(root, n, "Contents", "Resources", "autorun")
            if n.endswith(".app") and os.path.isdir(autorun):
                return find_gamedir(autorun, depth + 1)
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


def examples(items, n=3):
    items = sorted(items)
    return ", ".join(items[:n]) + (" and %d more" % (len(items) - n) if len(items) > n else "")


def analyze(root, kind, source, game_id=None, extract_warnings=()):
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
    elif vt:
        reasons.append("Ren'Py %s uses Python 2; there is no Python 2 runtime yet" % version)
    elif python == "3":
        warnings.append("Ren'Py version unknown (no renpy/ folder); the Python 3 .rpyc files "
                        "mean Ren'Py 8.x")
    elif python == "2":
        reasons.append("Python 2 game (Ren'Py 7 or older); there is no Python 2 runtime yet")
    else:
        reasons.append("cannot tell the Ren'Py version or Python generation (no renpy/, lib/ "
                       "or .rpyc files)")

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
        if ext in MEDIA_OK or ext in MEDIA_ODD:
            media[ext] = media.get(ext, 0) + 1
    odd = sorted(e for e in media if e in MEDIA_ODD)
    if odd:
        warnings.append("media formats Ren'Py may not play on the Switch: %s" % ", ".join(odd))
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
        "sha256": {rel: sha256(os.path.join(gamedir, rel)) for rel, _ in files
                   if rel.lower().endswith((".rpa", ".rpyc"))},
    }
    return manifest, gamedir, files


# -------------------------------------------------------------------- export

def export(manifest, gamedir, files, out, replace):
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
        for rel, _ in files:
            target = os.path.join(new, *rel.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.join(gamedir, *rel.split("/")), target)
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
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Analyse a Ren'Py game and export it for the Switch loader.")
    ap.add_argument("source", help="game folder, .zip, .7z or .rar")
    ap.add_argument("--out", help="export root, e.g. the SD card; writes switch/renpy-loader/games/<id>")
    ap.add_argument("--id", help="game id (folder name); default from the title")
    ap.add_argument("--replace", action="store_true", help="update an existing export (saves are kept)")
    a = ap.parse_args(argv)
    if a.id is not None and make_id(a.id) != a.id:
        ap.error("--id must be lowercase a-z, 0-9 and '-', at most %d chars (try %r)" % (ID_MAX, make_id(a.id)))
    tmp = tempfile.mkdtemp(prefix="rsl-import-")
    try:
        extract_warnings = []
        root, kind = unpack(a.source, tmp, extract_warnings)
        manifest, gamedir, files = analyze(root, kind, a.source, a.id, extract_warnings)
        print(report(manifest), flush=True)
        if a.out:
            if manifest["status"] == "unsupported":
                raise Refused("not exported: no runtime can run this game")
            print("Exported to %s" % export(manifest, gamedir, files, a.out, a.replace))
        return 0
    except (Refused, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
