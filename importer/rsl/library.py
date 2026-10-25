"""The library on an SD card or on the Switch over FTP: list, delete, compare with a folder."""
import contextlib
import ftplib
import io
import json
import os
import posixpath
import re
import tempfile
import urllib.parse

from .analysis import game_files, script_hashes, title_and_id
from .archives import unpack
from .common import examples, is_junk, Refused
from .recompile import recompiled
from .tools import rmtree


LIBRARY = "switch/renpy-loader"   # games/, saves/ and logs/ under the SD card root
FTP_PORT = 5000                   # the loader's FTP server, and sphaira's
FTP_TIMEOUT = 10                  # seconds, per network operation
FTP_BLOCK = 64 << 10


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

    def write(self, p, data):
        part = self.path(p) + ".part"
        with open(part, "wb") as f:
            f.write(data)
        os.replace(part, self.path(p))

    def rmtree(self, p):
        rmtree(self.path(p))
        if os.path.lexists(self.path(p)):
            raise OSError("could not delete all of %s" % self.path(p))

    def close(self):
        pass


class Ftp:
    """The Switch's SD card over FTP, anonymous, ftp://HOST[:PORT], where / is the SD card root:
    the loader's own server (+ > Add games over Wi-Fi, which reaches only switch/renpy-loader/)
    or sphaira's. Each runs only while its page shows, and the address can change."""

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
            raise Refused("cannot reach the Switch at %s:%d (%s). In the loader, press + and open Add games "
                          "over Wi-Fi (or turn on sphaira's FTP server), and "
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

    def announce(self, size):
        """Tells the server how many bytes an upload will send (ALLO), for the loader's progress
        bar. Servers without one, like sphaira's, refuse it, which changes nothing."""
        with contextlib.suppress(ftplib.Error):
            self.ftp.voidcmd("ALLO %d" % size)

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


def open_library(out):
    """The export root out as Local or Ftp."""
    return Ftp(out) if is_ftp(out) else Local(out)


def exported_games(out):
    """[(id, manifest)] for every games/<id>/ under an export root; manifest is None when
    there is no readable manifest.json (a game copied by hand)."""
    found = []
    with contextlib.closing(open_library(out)) as lib:
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


def in_game_folder(name):
    """A file name a manifest may give its cover: inside the game's folder, as
    the loader's game_file() checks it."""
    return isinstance(name, str) and 0 < len(name) <= 100 and not name.startswith(("/", "\\")) \
        and ".." not in name and ":" not in name


def library_game(lib, game_id):
    """The manifest.json of games/<id>/ in an open library, None if it has none."""
    if game_id.startswith(".") or game_id not in lib.entries("games"):
        raise Refused("%s is not in the library" % game_id)
    try:
        manifest = json.loads(lib.read("games/%s/manifest.json" % game_id) or b"null")
    except ValueError:
        return None
    return manifest if isinstance(manifest, dict) else None


def read_art(out, game_id):
    """{key: (file name, data)} of the cover a game in the library has."""
    found = {}
    with contextlib.closing(open_library(out)) as lib:
        manifest = library_game(lib, game_id) or {}
        for key in ("cover",):
            name = manifest.get(key)
            data = lib.read("games/%s/%s" % (game_id, name)) if in_game_folder(name) else None
            if data:
                found[key] = (name, data)
    return found


def delete_game(out, game_id, saves=False):
    """Removes games/<id>/, its game/saves included: Ren'Py writes every save to saves/<id>
    as well (savelocation.py, MultiLocation.save). saves=True also removes saves/<id>/."""
    with contextlib.closing(open_library(out)) as lib:
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
