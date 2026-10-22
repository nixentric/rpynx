"""Writing an analysed game into the library, on disk or over FTP."""
import contextlib
import ftplib
import json
import os
import shutil
import tempfile
import unicodedata

from .common import ERRORS, Refused
from .library import Ftp, games_dir, in_game_folder, is_ftp, library_game, open_library
from .media import ART, convert_art, reencode
from .rpa import packable, PACKED, Rpa
from .tools import rmtree


PACKED_MAX = 2 << 30               # bytes per archive, well under FAT32's 4 GiB


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


def keep_chosen(manifest, art, old):
    """--replace: a cover the user chose before (in the window, with --cover or on the
    Switch) stays, unless this export brings a chosen one too. Updates manifest and art;
    returns the file names to keep from the old export."""
    keep = []
    for key in ART:
        name = old.get(key) if isinstance(old, dict) and old.get(key + "_custom") else None
        if in_game_folder(name) and "/" not in name and not manifest.get(key + "_custom"):
            manifest[key], manifest[key + "_custom"] = old[key], True
            art.pop(key, None)
            keep.append(old[key])
    return keep


def export(manifest, gamedir, files, out, replace, extra=None, progress=None, art=None):
    """Writes games/<id>/game, manifest.json and the pictures in art ({key: path}, from
    make_art()). Refuses to overwrite unless replace; keeps game/saves (Ren'Py writes saves
    there too) and the pictures the user chose, and never touches saves/<id>.
    progress(done, total, rel, note) is called before each file, note being "re-encoding
    1/2" or ""; if it raises, the staged copy is removed and the old export stays as it was.
    Over FTP it is also called during an upload, with done + the fraction sent."""
    if manifest["status"] == "unsupported":
        raise Refused("not exported: no runtime can run this game")
    progress = progress or (lambda *a: None)
    manifest, art = dict(manifest), dict(art or {})   # what --replace keeps is this export's only
    if is_ftp(out):
        return ftp_export(manifest, gamedir, files, out, replace, extra, progress, art)
    games = games_dir(out)
    dest = os.path.join(games, manifest["id"])
    if os.path.lexists(dest) and not replace:
        raise Refused("%s already exists; pass --replace to update its game/ and manifest.json "
                      "(saves are kept)" % dest)
    try:
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            keep_chosen(manifest, art, json.load(f))   # the kept files stay where they are
    except (OSError, ValueError):
        pass
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
        for key, path in art.items():
            shutil.copyfile(path, os.path.join(stage, manifest[key]))
        with open(os.path.join(stage, "manifest.json"), "w", encoding="utf-8") as f:
            f.write(manifest_json(manifest))
        os.makedirs(dest, exist_ok=True)
        old = os.path.join(dest, "game")
        if os.path.isdir(os.path.join(old, "saves")):
            os.rename(os.path.join(old, "saves"), os.path.join(new, "saves"))
        if os.path.lexists(old):
            os.rename(old, os.path.join(stage, "old-game"))
        os.rename(new, old)
        for key in art:
            os.replace(os.path.join(stage, manifest[key]), os.path.join(dest, manifest[key]))
        os.replace(os.path.join(stage, "manifest.json"), os.path.join(dest, "manifest.json"))
    finally:
        rmtree(stage)
    return dest


def ftp_export(manifest, gamedir, files, out, replace, extra, progress, art):
    """export() to the Switch: uploads into games/.uploading-<id>, then renames it into
    place. The previous version waits in games/.old-<id> until its game/saves and chosen
    pictures are moved over."""
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
        keep = keep_chosen(manifest, art, library_game(ftp, gid)) if gid in have else []
        try:
            work = tempfile.mkdtemp(prefix="rsl-import-")
            try:
                copy_files(manifest, gamedir, files, extra, progress,
                           lambda source, rel, tick: ftp.put(source, stage + "/game/" + rel, tick), work)
            finally:
                rmtree(work)
            for key, path in art.items():
                ftp.put(path, stage + "/" + manifest[key])
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
                for name in keep:
                    if name in ftp.entries(old):
                        ftp.rename(old + "/" + name, dest + "/" + name)
                ftp.rmtree(old)
        except ftplib.all_errors as e:
            raise Refused("the upload is complete, but moving it into place stopped (%s). In %s, "
                          ".uploading-%s is the new copy and .old-%s the previous one with its game/saves"
                          % (e, games_dir(out), gid, gid))
    return games_dir(out) + "/" + gid


def set_art(out, game_id, key, image):
    """A cover the user chose for a game already in the library: image made
    into games/<id>/<key>.jpg and named in its manifest.json as chosen, so that an export
    with --replace keeps it."""
    work = tempfile.mkdtemp(prefix="rsl-art-")
    try:
        path = os.path.join(work, key + ".jpg")
        error = convert_art(image, path, ART[key])
        if error:
            raise Refused("%s: %s" % (os.path.basename(image), error))
        with contextlib.closing(open_library(out)) as lib:
            manifest = library_game(lib, game_id)
            if manifest is None:
                raise Refused("%s has no manifest.json, which names the pictures; export it with "
                              "the importer first" % game_id)
            with open(path, "rb") as f:
                lib.write("games/%s/%s.jpg" % (game_id, key), f.read())
            manifest[key], manifest[key + "_custom"] = key + ".jpg", True
            lib.write("games/%s/manifest.json" % game_id, manifest_json(manifest).encode("utf-8"))
    finally:
        rmtree(work)
