"""Analysing a game: game directory, Ren'Py version, Python generation, checks, the manifest."""
import os
import re
import stat
import struct
import zlib

from .archives import unpack
from .common import examples, is_junk, make_id, read_text, Refused, sha256, VIDEO_EXTS
from .media import MAX_VIDEO_FPS, performance
from .recompile import decompile_scripts, recompile_files
from .rpa import packable, PACKED


LARGE_FILE_BYTES = 2 << 30      # warn: FAT32 SD cards stop at 4 GiB per file
RUNTIME = "renpy-8.5.3"
RUNTIME_VERSION = (8, 5, 3)
MANIFEST_FORMAT = "renpy-switch-loader/manifest"
SCRIPT_EXTS = (".rpy", ".rpyc", ".rpa", ".rpym", ".rpymc")
NATIVE_EXTS = (".pyd", ".dll", ".so", ".dylib")
AUDIO_EXTS = (".ogg", ".opus", ".mp3", ".wav", ".flac", ".m4a", ".aac", ".wma", ".mp2")
NO_PLAY_EXTS = (".mid", ".midi")   # the runtime's FFmpeg decodes the rest; Ren'Py has no MIDI
LAUNCHER_EXTS = (".exe", ".sh", ".py", ".app")


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


def report(m):
    lines = ["%s  (id: %s)" % (m["title"], m["id"]),
             "  Source:  %s %s" % (m["source"]["type"], m["source"]["name"]),
             "  Ren'Py:  %s   Python: %s" % (m["renpy_version"] or "unknown", m["python"]),
             "  Status:  %s" % (m["status"].upper() + (" on runtime " + m["runtime"] if m["runtime"] else "")),
             "  Files:   %d in game/, %.1f MB (rpa %d, rpy %d, rpyc %d, rpym %d)" % (
                 m["files"]["count"], m["files"]["total_bytes"] / 1e6, m["files"]["rpa"],
                 m["files"]["rpy"], m["files"]["rpyc"], m["files"]["rpym"]),
             "  Media:   %s" % (", ".join("%s %d" % kv for kv in m["media"].items()) or "none")]
    pictures = ["%s%s" % (m[k], " (chosen)" if m.get(k + "_custom") else "") for k in ("cover", "backdrop") if m.get(k)]
    lines.append("  Pictures: %s" % (", ".join(pictures) or "none; the loader shows a placeholder"))
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
