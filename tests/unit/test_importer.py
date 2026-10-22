"""Tests for the importer: importer/rsl/ and its command line, importer/rsl_import.py. Fixtures are fake games built in a temp dir.

usage: /usr/bin/python3 -m unittest tests/unit/test_importer.py
"""
import contextlib
import ftplib
import io
import json
import ntpath
import os
import pickle
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import types
import unicodedata
import unittest
import zipfile
import zlib
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "importer"))
import rsl as imp  # noqa: E402
import rsl_import  # noqa: E402
from rsl import archives, exporter, library, media, recompile, tools  # noqa: E402  (what tests patch)


def rpyc(protocol):
    """A minimal RPC2 .rpyc: slot 1 holds a zlib'd pickle of the given protocol."""
    body = zlib.compress(pickle.dumps({"version": 5003000, "key": "unlocked"}, protocol=protocol))
    return b"RENPY RPC2" + struct.pack("<III", 1, 46, len(body)) + bytes(24) + body


def py2_script(label, line):
    """A Ren'Py 7-style .rpyc of `label <label>: "<line>"`: slot 1 holds a protocol 2 pickle
    of (data, statements). Ren'Py 7's nodes have __slots__, so each pickles as
    (None, {slot: value}); stand-in renpy.ast classes produce the same opcodes."""
    ast = types.ModuleType("renpy.ast")
    slots = ("name", "filename", "linenumber", "next", "block", "parameters", "hide", "who", "what",
             "with_", "expression")
    for name in ("Label", "Say", "Return"):
        setattr(ast, name, type(name, (object,), {"__module__": "renpy.ast", "__slots__": slots}))

    def node(cls, **state):
        n = cls()
        for k, v in dict(state, filename="game/script.rpy", linenumber=1, next=None).items():
            setattr(n, k, v)
        return n

    with mock.patch.dict(sys.modules, {"renpy": types.ModuleType("renpy"), "renpy.ast": ast}):
        say = node(ast.Say, who=None, what=line, with_=None)
        stmts = [node(ast.Label, name=label, block=[say], parameters=None, hide=False),
                 node(ast.Return, expression=None)]
        body = zlib.compress(pickle.dumps(({"version": 5003000, "key": "unlocked"}, stmts), 2))
    return b"RENPY RPC2" + struct.pack("<III", 1, 46, len(body)) + bytes(24) + body


def write(path, data=b""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data.encode() if isinstance(data, str) else data)


def make_game(base, py=3):
    """A Ren'Py distribution layout: launchers, renpy/, lib/, game/."""
    write(os.path.join(base, "game", "options.rpy"), 'define config.name = _("Fake Quest")\n')
    write(os.path.join(base, "game", "script.rpy"), 'label start:\n    "Hello."\n')
    write(os.path.join(base, "game", "script.rpyc"), rpyc(5 if py == 3 else 2))
    write(os.path.join(base, "game", "music.ogg"), b"OggS")
    write(os.path.join(base, "game", "saves", "1-1-LT1.save"), b"dev save")
    write(os.path.join(base, "FakeQuest.exe"), b"MZ")
    write(os.path.join(base, "FakeQuest.sh"), "#!/bin/sh\n")
    if py == 3:
        write(os.path.join(base, "renpy", "vc_version.py"), "version = '8.5.3.26051504'\n")
        os.makedirs(os.path.join(base, "lib", "py3-mac-universal"))
    else:
        write(os.path.join(base, "renpy", "__init__.py"), "version_tuple = (7, 4, 11, vc_version)\n")
        os.makedirs(os.path.join(base, "lib", "py2-windows-i686"))
    return base


def make_rpa(path, members, protocol=5, key=0x42424242, index=None):
    """A Ren'Py RPA-3.0 archive (renpy/loader.py's format): data, then a zlib'd pickle
    index of {name: [(offset ^ key, length ^ key, prefix)]} at the offset in the header.
    Ren'Py 8's archiver pickles with protocol 5 and a b"" prefix; Ren'Py 7 (Python 2) with
    protocol 2 and a "" str, which Python 3 can only imitate with a text prefix."""
    body = bytearray(b"RPA-3.0 %016x %08x\n" % (0, key))
    entries = {}
    for name, data in members.items():
        entries[name] = [(len(body) ^ key, len(data) ^ key, b"" if protocol >= 3 else "")]
        body += data
    offset = len(body)
    body += zlib.compress(pickle.dumps(entries if index is None else index, protocol))
    body[8:24] = b"%016x" % offset
    write(path, bytes(body))


def find_7z():
    try:
        return imp.seven_zip()
    except imp.Refused:
        return None


HAVE_FFMPEG = shutil.which("ffmpeg") and shutil.which("ffprobe")


def picture(path, color, size="1280x720"):
    """A one-colour PNG made with FFmpeg."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "color=%s:size=%s" % (color, size),
                    "-frames:v", "1", path], check=True)
    return path


def colour(path):
    """The (r, g, b) of a picture's middle pixel, decoded by FFmpeg."""
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-vf", "crop=2:2", "-frames:v", "1",
                        "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], check=True, capture_output=True)
    return tuple(r.stdout[:3])


def loader_jpeg(test, path, size):
    """What the loader's TJpgDec reads (docs/LOADER_UI.md "Covers"): baseline (SOF0), not
    progressive, 4:2:0 sampling, the given size."""
    with open(path, "rb") as f:
        data = f.read()
    sof = data.find(b"\xff\xc0")
    test.assertGreater(sof, 0, "baseline")
    test.assertNotIn(b"\xff\xc2", data, "not progressive")
    test.assertEqual(data[sof + 11], 0x22, "4:2:0")
    test.assertEqual(imp.image_size(path), size)


def listing(root):
    return sorted(os.path.relpath(os.path.join(d, n), root)
                  for d, ds, fs in os.walk(root) for n in ds + fs)


class ImporterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rsl-test-")
        self.addCleanup(shutil.rmtree, self.tmp)

    def path(self, *p):
        return os.path.join(self.tmp, *p)

    def run_cli(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rsl_import.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def analyze_dir(self, base):
        return imp.analyze(base, "directory", base)[:3]

    def zip_of(self, entries):
        """entries: [(name, data)] or [(ZipInfo, data)]."""
        z = self.path("in.zip")
        with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
            for name, data in entries:
                zf.writestr(name, data)
        return z

    def assertRefused(self, source, match):
        dest = tempfile.mkdtemp(dir=self.tmp)
        with self.assertRaises(imp.Refused) as cm:
            imp.unpack(source, dest, [])
        self.assertIn(match, str(cm.exception))
        return dest

    # ---------------------------------------------------------------- tests

    def test_py3_game_dir_end_to_end(self):
        src = make_game(self.path("FakeQuest-1.0-pc"))
        before = listing(src)
        out = self.path("sd")
        code, stdout, err = self.run_cli(src, "--out", out)
        self.assertEqual(code, 0, err)
        self.assertIn("EXPERIMENTAL on runtime renpy-8.5.3", stdout)
        self.assertEqual(listing(src), before, "source must not change")

        dest = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest")
        self.assertEqual(sorted(os.listdir(dest)), ["game", "manifest.json"])
        self.assertEqual(listing(os.path.join(dest, "game")),
                         ["music.ogg", "options.rpy", "script.rpy", "script.rpyc"])
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        self.assertEqual(m["format"], "renpy-switch-loader/manifest")
        self.assertEqual(m["version"], 1)
        self.assertEqual((m["id"], m["title"]), ("fake-quest", "Fake Quest"))
        self.assertEqual(m["renpy_version"], "8.5.3.26051504")
        self.assertEqual((m["python"], m["runtime"], m["status"]), ("3", "renpy-8.5.3", "experimental"))
        self.assertEqual(m["reasons"], [])
        self.assertEqual(m["suggested_options"], {})
        self.assertEqual(m["source"], {"type": "directory", "name": "FakeQuest-1.0-pc"})
        self.assertEqual(m["files"]["rpyc"], 1)
        self.assertEqual(m["files"]["rpy"], 2)
        self.assertEqual(list(m["sha256"]), ["script.rpyc"])
        self.assertEqual(m["media"], {".ogg": 1})

    def test_zip_with_wrapper_folder(self):
        src = make_game(self.path("src", "FakeQuest-1.0-pc"))
        z = self.path("FakeQuest-1.0-pc.zip")
        with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
            for d, _, fs in os.walk(self.path("src")):
                for n in fs:
                    p = os.path.join(d, n)
                    zf.write(p, os.path.relpath(p, self.path("src")))
        code, stdout, err = self.run_cli(z, "--out", self.path("sd"))
        self.assertEqual(code, 0, err)
        self.assertIn("Ren'Py:  8.5.3.26051504   Python: 3", stdout)
        self.assertTrue(os.path.isfile(self.path("sd", "switch", "renpy-loader", "games", "fake-quest",
                                                 "game", "script.rpyc")))

    def test_zip_traversal_rejected(self):
        z = self.zip_of([("game/script.rpy", "label start:\n"), ("../evil.txt", "x")])
        dest = self.assertRefused(z, "escapes the archive")
        self.assertEqual(os.listdir(dest), [], "nothing is written before validation")
        self.assertFalse(os.path.exists(self.path("evil.txt")))

    def test_zip_absolute_rejected(self):
        self.assertRefused(self.zip_of([("/tmp/evil.txt", "x")]), "absolute path")
        self.assertRefused(self.zip_of([("C:\\evil.txt", "x")]), "absolute path")

    def test_zip_bomb_rejected(self):
        dest = self.assertRefused(self.zip_of([("game/zeros.bin", bytes(20 << 20))]), "decompression bomb")
        self.assertEqual(os.listdir(dest), [])

    def test_zip_limits_rejected(self):
        z = self.zip_of([("game/a.rpy", "a"), ("game/b.rpy", "b")])
        old = archives.MAX_FILES
        archives.MAX_FILES = 1
        try:
            self.assertRefused(z, "entries (limit 1)")
        finally:
            archives.MAX_FILES = old

    def test_zip_symlink_outside_rejected(self):
        link = zipfile.ZipInfo("game/link")
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.assertRefused(self.zip_of([(link, "../../etc/passwd")]), "points outside")

    def test_py2_game_unsupported_and_not_exported(self):
        src = make_game(self.path("OldGame"), py=2)
        m, _, _ = self.analyze_dir(src)
        self.assertEqual((m["renpy_version"], m["python"]), ("7.4.11", "2"))
        self.assertEqual((m["status"], m["runtime"]), ("unsupported", None))
        self.assertTrue(m["reasons"])
        self.assertEqual(list(m["suggested_options"]), ["recompile"], "it ships its .rpy sources")
        code, stdout, err = self.run_cli(src, "--out", self.path("sd"))
        self.assertEqual(code, 1)
        self.assertIn("UNSUPPORTED", stdout)
        self.assertFalse(os.path.exists(self.path("sd")))

    def test_recompile_py2_game(self):
        src = make_game(self.path("OldGame"), py=2)
        write(os.path.join(src, "game", "cache", "bytecode.rpyb"), b"py2 cache")
        write(os.path.join(src, "game", "tl", "french.rpyc"), rpyc(2))   # stale, no source
        out = self.path("sd")
        code, stdout, err = self.run_cli(src, "--out", out, "--recompile")
        self.assertEqual(code, 0, err)
        dest = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest")
        self.assertEqual(listing(os.path.join(dest, "game")),
                         ["music.ogg", "options.rpy", "script.rpy"])
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        self.assertEqual((m["python"], m["status"], m["runtime"], m["recompile"]),
                         ("2", "experimental", "renpy-8.5.3", True))
        self.assertEqual((m["reasons"], m["sha256"], m["files"]["rpyc"]), ([], {}, 0))
        text = "\n".join(m["warnings"])
        self.assertIn("/ on integers gives a float", text)
        self.assertIn("1 compiled translation files with no source (tl/french.rpyc)", text)

    def test_recompile_refusals(self):
        py3 = make_game(self.path("NewGame"))
        self.assertEqual(self.run_cli(py3, "--recompile")[0], 1)
        orphan = make_game(self.path("OldGame"), py=2)
        write(os.path.join(orphan, "game", "secret.rpyc"), rpyc(2))
        code, _, err = self.run_cli(orphan, "--recompile")
        self.assertEqual(code, 1)
        self.assertIn("1 compiled scripts have no .rpy source (secret.rpyc); add --decompile", err)
        m, _, _ = self.analyze_dir(orphan)
        self.assertEqual(list(m["suggested_options"]), ["decompile"], "the page shows it as an alert")
        self.assertIn("1 compiled scripts were never decompiled", m["suggested_options"]["decompile"])
        with mock.patch.object(recompile, "UNRPYC", self.path("none", "unrpyc.py")):
            code, _, err = self.run_cli(orphan, "--decompile")   # --recompile is implied
        self.assertIn("unrpyc not found; fetch it with third_party/unrpyc/fetch.sh", err)
        with mock.patch.dict(os.environ, {"PATH": self.path("empty")}):
            code, _, err = self.run_cli(orphan, "--decompile", "--decompiler", "rpycdec")
        self.assertIn("rpycdec not found on PATH", err)
        code, _, err = self.run_cli(orphan, "--decompile", "--decompiler", "rpycdec",
                                    "--rpycdec-python", self.path("nopython"))
        self.assertIn("--decompile: cannot run", err)
        for args in (["--decompiler", "rpycdec"], ["--decompile", "--rpycdec-python", "python3"]):
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                rsl_import.main([orphan] + args)
        os.remove(os.path.join(orphan, "game", "secret.rpyc"))
        write(os.path.join(orphan, "game", "scripts.rpa"), b"RPA-3.0 ")
        code, _, err = self.run_cli(orphan, "--recompile")
        self.assertIn(".rpa archives", err)
        m, _, _ = self.analyze_dir(orphan)
        self.assertIn("try --recompile", m["reasons"][0])
        self.assertEqual(m["suggested_options"], {}, "--recompile refuses games with .rpa archives")

    def decompilers(self):
        """The --decompiler tools found here; the tests with them run once per tool."""
        found = [t for t, ok in (("unrpyc", imp.find_unrpyc()), ("rpycdec", imp.find_tool(("rpycdec",)))) if ok]
        if not found:
            self.skipTest("no decompiler: run third_party/unrpyc/fetch.sh or pip install rpycdec")
        return found

    def test_decompile_sourceless_py2_game(self):
        for tool in self.decompilers():
            with self.subTest(tool):
                src = make_game(self.path(tool, "OldGame"), py=2)
                os.remove(os.path.join(src, "game", "script.rpy"))
                write(os.path.join(src, "game", "script.rpyc"), py2_script("start", "Hello from a decompiled script."))
                write(os.path.join(src, "game", "chapters", "one.rpyc"), py2_script("one", "Chapter one."))
                before = listing(src)
                out = self.path(tool, "sd")
                code, stdout, err = self.run_cli(src, "--out", out, "--decompile", "--decompiler", tool)
                self.assertEqual(code, 0, err)
                self.assertEqual(listing(src), before, "source must not change")
                game = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest", "game")
                self.assertEqual(listing(game), ["chapters", "chapters/one.rpy", "music.ogg", "options.rpy", "script.rpy"])
                with open(os.path.join(game, "script.rpy"), encoding="utf-8") as f:
                    words = [w for w in f.read().split() if not w.startswith(("#", "unrpyc:", "https:"))]
                self.assertEqual(words[:7], ["label", "start:", '"Hello', "from", "a", "decompiled", 'script."'])
                with open(os.path.join(game, "..", "manifest.json"), encoding="utf-8") as f:
                    m = json.load(f)
                self.assertEqual((m["status"], m["recompile"], m["decompiled"]),
                                 ("experimental", True, ["chapters/one.rpy", "script.rpy"]))
                self.assertEqual((m["files"]["rpy"], m["files"]["rpyc"]), (3, 0))
                self.assertIn("with %s (chapters/one.rpy, script.rpy). Decompiled scripts may differ" % tool,
                              "\n".join(m["warnings"]))

    def test_decompile_failures_are_named(self):
        for tool in self.decompilers():
            with self.subTest(tool):
                src = make_game(self.path(tool, "OldGame"), py=2)
                write(os.path.join(src, "game", "tl", "french", "broken.rpyc"), b"RENPY RPC2 not a script")
                code, stdout, err = self.run_cli(src, "--decompile", "--decompiler", tool)
                if tool == "unrpyc":   # goes on, and a translation it cannot read is left out
                    self.assertEqual(code, 0, err)
                    self.assertIn("unrpyc could not decompile 1 translation files (tl/french/broken.rpyc) "
                                  "(first error: struct.error", stdout)
                else:   # stops at the first script it cannot read
                    self.assertEqual(code, 1)
                    self.assertIn("rpycdec failed: ERROR:rpycdec.decompile:decode file tl/french/broken.rpyc", err)
                write(os.path.join(src, "game", "chapter.rpyc"), b"RENPY RPC2 not a script")
                code, _, err = self.run_cli(src, "--decompile", "--decompiler", tool)
                self.assertEqual(code, 1)
                self.assertIn("chapter.rpyc", err)

    def test_py2_rpyc_without_engine_is_unsupported(self):
        write(self.path("g", "game", "script.rpyc"), rpyc(2))
        m, _, _ = self.analyze_dir(self.path("g"))
        self.assertEqual((m["python"], m["status"]), ("2", "unsupported"))

    def test_windows_native_code_and_non_ascii_warn(self):
        src = make_game(self.path("FakeQuest"))
        write(os.path.join(src, "game", "python-packages", "fast.pyd"), b"MZ")
        write(os.path.join(src, "game", "images", "セーブ.png"), b"\x89PNG")
        write(os.path.join(src, "game", "theme.mid"), b"MThd")
        m, _, _ = self.analyze_dir(src)
        self.assertEqual(m["status"], "experimental", "warnings do not block")
        text = "\n".join(m["warnings"])
        self.assertIn("native code that cannot load on the Switch: python-packages/fast.pyd", text)
        self.assertIn("python-packages present", text)
        self.assertIn("1 files with non-ASCII names", text)
        self.assertIn("Ren'Py cannot play MIDI: theme.mid", text)

    def test_non_ascii_names_are_packed_into_an_archive(self):
        # Hardware run 20: the Switch SD refuses or hides such names; Ren'Py reads them from an .rpa.
        src = make_game(self.path("FakeQuest"))
        write(os.path.join(src, "game", "images", "セーブ.png"), b"\x89PNG jp")
        write(os.path.join(src, "game", "audio", unicodedata.normalize("NFD", "ブ.ogg")), b"OggS jp")
        write(os.path.join(src, "game", "日本.rpy"), "label jp:\n")   # Ren'Py loads no .rpy from an archive
        out = self.path("sd")
        m, gamedir, files, extra = imp.analyze(src, "directory", src)
        text = "\n".join(m["warnings"])
        self.assertIn("2 files with non-ASCII names", text)
        self.assertIn("scripts or archives with non-ASCII names (日本.rpy)", text)
        imp.export(m, gamedir, files, out, False, extra)
        game = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest", "game")
        self.assertEqual(listing(game), ["music.ogg", "options.rpy", "rsl-nonascii.rpa", "script.rpy",
                                         "script.rpyc", "日本.rpy"])
        index = imp.rpa_index(os.path.join(game, "rsl-nonascii.rpa"))
        self.assertEqual(sorted(index), ["audio/ブ.ogg", "images/セーブ.png"], "names in NFC")
        self.assertEqual(imp.read_member(game, ("rsl-nonascii.rpa",) + index["audio/ブ.ogg"]), b"OggS jp")

        with mock.patch.object(exporter, "PACKED_MAX", 40):   # each member alone fills an archive
            imp.export(m, gamedir, files, out, True, extra)
        archives = [n for n in listing(game) if n.endswith(".rpa")]
        self.assertEqual(archives, ["rsl-nonascii-2.rpa", "rsl-nonascii.rpa"])
        self.assertEqual(sum(len(imp.rpa_index(os.path.join(game, a))) for a in archives), 2)

    def test_id_normalisation(self):
        vectors = [
            ("The Question", "the-question"),
            ("the_question", "the-question"),
            ("  Hello,  World!! ", "hello-world"),
            ("Ren'Py 8.5_Test", "ren-py-8-5-test"),
            ("--x--", "x"),
            ("ÆON flux", "on-flux"),
            ("ドキドキ", "game"),
            ("", "game"),
            ("a" * 100, "a" * 63),
            ("a" * 62 + "-b", "a" * 62),
        ]
        for text, want in vectors:
            self.assertEqual(imp.make_id(text), want, text)

    def test_title_fallback_and_id_from_folder(self):
        src = make_game(self.path("My Game"))
        write(os.path.join(src, "game", "options.rpy"), 'define config.name = _("ドキドキ")\n')
        m, _, _ = self.analyze_dir(src)
        self.assertEqual((m["title"], m["id"]), ("ドキドキ", "my-game"))
        os.remove(os.path.join(src, "game", "options.rpy"))
        m, _, _ = self.analyze_dir(src)
        self.assertEqual((m["title"], m["id"]), ("My Game", "my-game"))

    def test_refuse_overwrite_and_replace_keeps_saves(self):
        src = make_game(self.path("FakeQuest"))
        out = self.path("sd")
        root = os.path.join(out, "switch", "renpy-loader")
        user_save = os.path.join(root, "saves", "fake-quest", "1-1-LT1.save")
        write(user_save, b"player save")
        self.assertEqual(self.run_cli(src, "--out", out)[0], 0)
        # Ren'Py on the console also writes saves into game/saves.
        runtime_save = os.path.join(root, "games", "fake-quest", "game", "saves", "auto-1.save")
        write(runtime_save, b"runtime save")
        write(os.path.join(src, "game", "new.rpy"), "label new:\n")

        code, _, err = self.run_cli(src, "--out", out)
        self.assertEqual(code, 1)
        self.assertIn("already exists", err)
        self.assertFalse(os.path.exists(os.path.join(root, "games", "fake-quest", "game", "new.rpy")))

        code, _, err = self.run_cli(src, "--out", out, "--replace")
        self.assertEqual(code, 0, err)
        self.assertTrue(os.path.exists(os.path.join(root, "games", "fake-quest", "game", "new.rpy")))
        with open(runtime_save, "rb") as f:
            self.assertEqual(f.read(), b"runtime save")
        with open(user_save, "rb") as f:
            self.assertEqual(f.read(), b"player save")
        self.assertEqual(sorted(os.listdir(os.path.join(root, "games"))), ["fake-quest"],
                         "no staging folder is left behind")

    def test_macos_app_given_directly(self):
        app = self.path("Fake Quest.app")
        make_game(os.path.join(app, "Contents", "Resources", "autorun"))
        os.remove(os.path.join(app, "Contents", "Resources", "autorun", "game", "options.rpy"))
        for source in (app, self.tmp):   # the .app itself, and a folder holding it
            m, gamedir, _ = self.analyze_dir(source)
            self.assertTrue(gamedir.endswith(os.path.join("autorun", "game")), gamedir)
            self.assertEqual((m["title"], m["id"], m["status"]), ("Fake Quest", "fake-quest", "experimental"))

    def test_image_size_headers(self):
        png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 5000, 10)
        jpeg = (b"\xff\xd8" + b"\xff\xe0" + struct.pack(">H", 4) + b"JF"
                + b"\xff\xc0" + struct.pack(">HBHH", 11, 8, 720, 1280) + b"\x03")
        webp = b"RIFF" + bytes(4) + b"WEBPVP8X" + bytes(8) + (1919).to_bytes(3, "little") + (1079).to_bytes(3, "little")
        for name, data, want in [("a.png", png, (5000, 10)), ("b.jpg", jpeg, (1280, 720)), ("c.webp", webp, (1920, 1080))]:
            write(self.path(name), data)
            self.assertEqual(imp.image_size(self.path(name)), want, name)
        write(self.path("d.png"), b"not an image")
        self.assertIsNone(imp.image_size(self.path("d.png")))

    @unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg not installed")
    def test_performance_and_optimize(self):
        src = make_game(self.path("FakeQuest"))
        vid = os.path.join(src, "game", "video")
        os.makedirs(vid)
        def clip(name, size, rate, *codec):
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=%s:rate=%s" % (size, rate),
                            "-t", "1"] + list(codec) + [os.path.join(vid, name)], check=True)
        clip("fast.webm", "1296x720", "60", "-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "8")
        clip("big.mp4", "1920x1080", "30", "-c:v", "libx264", "-preset", "ultrafast")
        write(os.path.join(src, "game", "huge.png"),
              b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 5000, 10))

        m, _, _ = self.analyze_dir(src)
        text = "\n".join(m["performance"]["issues"])
        self.assertIn("1 videos above 30 fps (video/fast.webm 1296x720 vp9 60 fps)", text)
        self.assertIn("2 videos above the 1280x720 handheld screen (video/big.mp4", text)
        self.assertIn("1 images exceed the Switch GPU's 4096x4096 texture limit", text)
        self.assertEqual(m["performance"]["converted"], {})
        self.assertEqual(list(m["suggested_options"]), ["optimize"])
        self.assertIn("1 videos above 30 fps (video/fast.webm)", m["suggested_options"]["optimize"])

        out = self.path("sd")
        code, stdout, err = self.run_cli(src, "--out", out, "--optimize")
        self.assertEqual(code, 0, err)
        dest = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest")
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        self.assertEqual(m["performance"]["converted"], {"video/fast.webm": "vp9"})
        self.assertEqual(m["suggested_options"], {}, "already optimized")
        self.assertNotIn("above 30 fps", "\n".join(m["performance"]["issues"]))
        self.assertIn("video/fast.webm 1296x720 vp9 30 fps (was 60)", "\n".join(m["performance"]["issues"]))
        v = imp.probe_video(shutil.which("ffprobe"), os.path.join(dest, "game", "video", "fast.webm"))
        self.assertEqual((v["codec"], v["width"], v["height"], round(v["fps"])), ("vp9", 1296, 720, 30))
        self.assertIn("Switch performance: 2 issue(s)", stdout)

    def test_rpa_index_plain_data(self):
        png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 5000, 10)
        for protocol in (2, 5):   # Ren'Py 7 (Python 2) and Ren'Py 8 archives
            rpa = self.path("p%d.rpa" % protocol)
            make_rpa(rpa, {"images/huge.png": png, "script.rpyc": b"x" * 10}, protocol)
            index = imp.rpa_index(rpa)
            self.assertEqual(sorted(index), ["images/huge.png", "script.rpyc"])
            member = ("p%d.rpa" % protocol,) + index["images/huge.png"]
            self.assertEqual(imp.read_member(self.tmp, member), png)
        write(self.path("custom.rpa"), b"MYGAME-1 encrypted")
        self.assertIsNone(imp.rpa_index(self.path("custom.rpa")))

    def test_rpa_index_that_would_run_code_is_refused(self):
        marker = self.path("pwned")

        class Evil:
            def __reduce__(self):
                return (open, (marker, "w"))

        rpa = self.path("evil.rpa")
        make_rpa(rpa, {}, index={"a.png": [(0, 0, b"")], "b": Evil()})
        with self.assertRaises(ValueError):
            imp.rpa_index(rpa)
        self.assertFalse(os.path.exists(marker), "the index must never be unpickled")

    def test_archives_in_report(self):
        src = make_game(self.path("FakeQuest"))
        png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 5000, 10)
        make_rpa(os.path.join(src, "game", "images.rpa"), {"images/huge.png": png})
        write(os.path.join(src, "game", "locked.rpa"), b"XYZ-9.0 custom")
        m, _, _ = self.analyze_dir(src)
        cov = m["performance"]["coverage"]
        self.assertEqual((cov["archived_media"], cov["locked_archives"]), (1, ["locked.rpa (custom format)"]))
        text = "\n".join(m["performance"]["issues"])
        self.assertIn("images/huge.png (in images.rpa) 5000x10", text)
        self.assertIn("1 archives could not be checked (locked.rpa (custom format))", text)

    @unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg not installed")
    def test_optimize_video_inside_archive(self):
        src = make_game(self.path("FakeQuest"))
        clip = self.path("fast.webm")
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=60", "-t", "1",
                        "-c:v", "libvpx-vp9", "-deadline", "realtime", clip], check=True)
        with open(clip, "rb") as f:
            rpa = os.path.join(src, "game", "media.rpa")
            make_rpa(rpa, {"video/fast.webm": f.read()})
        out = self.path("sd")
        code, stdout, err = self.run_cli(src, "--out", out, "--optimize")
        self.assertEqual(code, 0, err)
        game = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest", "game")
        v = imp.probe_video(shutil.which("ffprobe"), os.path.join(game, "video", "fast.webm"))
        self.assertEqual((v["width"], v["height"], round(v["fps"])), (160, 90, 30))
        self.assertEqual(imp.sha256(os.path.join(game, "media.rpa")), imp.sha256(rpa), "archive unchanged")
        self.assertIn("1 of them come from archives", stdout)

    @unittest.skipUnless(HAVE_FFMPEG, "FFmpeg not installed")
    def test_cover(self):
        src = make_game(self.path("FakeQuest"))
        game = os.path.join(src, "game")
        write(os.path.join(game, "gui.rpy"), 'define gui.main_menu_background = "images/Title.png"  # art\n')
        with open(picture(self.path("title.png"), "red"), "rb") as f:
            make_rpa(os.path.join(game, "images.rpa"), {"images/title.png": f.read()})   # case as on Windows
        picture(os.path.join(game, "gui", "main_menu.png"), "lime")   # gui.rpy names another picture
        out = self.path("sd")
        dest = os.path.join(out, "switch", "renpy-loader", "games", "fake-quest")

        code, stdout, err = self.run_cli(src, "--out", out)
        self.assertEqual(code, 0, err)
        self.assertIn("Pictures: cover.jpg", stdout)
        self.assertEqual(sorted(os.listdir(dest)), ["cover.jpg", "game", "manifest.json"])
        loader_jpeg(self, os.path.join(dest, "cover.jpg"), (300, 450))
        self.assertGreater(colour(os.path.join(dest, "cover.jpg"))[0], 200, "from images/title.png")
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        self.assertEqual((m["cover"], "cover_custom" in m), ("cover.jpg", False))

        # A chosen cover (any shape: cut from the middle) replaces it, and later exports keep it.
        poster = picture(self.path("poster.jpg"), "blue", "600x400")
        code, stdout, err = self.run_cli(src, "--out", out, "--replace", "--cover", poster)
        self.assertEqual(code, 0, err)
        self.assertIn("Pictures: cover.jpg (chosen)", stdout)
        self.assertEqual(self.run_cli(src, "--out", out, "--replace")[0], 0)
        loader_jpeg(self, os.path.join(dest, "cover.jpg"), (300, 450))
        self.assertGreater(colour(os.path.join(dest, "cover.jpg"))[2], 200, "the chosen cover is kept")

        # A picture chosen on the Switch (the loader writes BMP) is kept as well.
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        m.update(cover="cover.bmp", cover_custom=True)
        write(os.path.join(dest, "manifest.json"), json.dumps(m))
        write(os.path.join(dest, "cover.bmp"), b"BM switch")
        self.assertEqual(self.run_cli(src, "--out", out, "--replace")[0], 0)
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        self.assertEqual((m["cover"], m["cover_custom"]), ("cover.bmp", True))
        self.assertTrue(os.path.isfile(os.path.join(dest, "cover.bmp")))

    @unittest.skipUnless(HAVE_FFMPEG, "FFmpeg not installed")
    def test_pictures_fallback_and_failures(self):
        src = make_game(self.path("FakeQuest"))
        write(os.path.join(src, "game", "gui.rpy"), 'define gui.main_menu_background = Movie(play="a.webm")\n')
        code, stdout, err = self.run_cli(src)
        self.assertEqual(code, 0, err)
        self.assertIn("Pictures: none; the loader shows a placeholder", stdout)
        self.assertNotIn("Warnings", stdout, "a game without menu art is not a problem")

        picture(os.path.join(src, "game", "gui", "main_menu.png"), "red")
        code, stdout, err = self.run_cli(src, "--cover", self.path("missing.png"))
        self.assertEqual(code, 1)
        self.assertIn("cover %s: " % self.path("missing.png"), err)
        with mock.patch.object(media, "find_tool", return_value=None):
            code, stdout, err = self.run_cli(src)
            self.assertEqual(code, 0, err)
            self.assertIn("no cover from gui/main_menu.png: FFmpeg is needed for covers", stdout)
            self.assertIn("Pictures: none", stdout)
            code, _, err = self.run_cli(src, "--cover", self.path("poster.png"))
            self.assertEqual(code, 1)
            self.assertIn("FFmpeg is needed", err)

    def test_bad_id_rejected(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            rsl_import.main([self.tmp, "--id", "Bad Id"])

    def test_not_a_game(self):
        write(self.path("docs", "readme.txt"), "hi")
        code, _, err = self.run_cli(self.path("docs"))
        self.assertEqual(code, 1)
        self.assertIn("no Ren'Py", err)

    @unittest.skipUnless(find_7z(), "7-Zip not installed")
    def test_7z_archive(self):
        src = make_game(self.path("src", "FakeQuest"))
        archive = self.path("FakeQuest.7z")
        subprocess.run([find_7z(), "a", "-bd", archive, src], check=True, stdout=subprocess.DEVNULL)
        code, stdout, err = self.run_cli(archive)
        self.assertEqual(code, 0, err)
        self.assertIn("EXPERIMENTAL", stdout)

    def test_export_progress_and_cancel_keep_old_export(self):
        src = make_game(self.path("FakeQuest"))
        out = self.path("sd")
        m, gamedir, files, extra = imp.analyze(src, "directory", src)
        calls = []
        imp.export(m, gamedir, files, out, False, extra, lambda *a: calls.append(a))
        self.assertEqual(calls[0][:2], (0, 4))
        self.assertEqual(calls[-1], (4, 4, "", ""))
        write(os.path.join(src, "game", "new.rpy"), "label new:\n")
        m, gamedir, files, extra = imp.analyze(src, "directory", src)

        def cancel(done, total, rel, note):
            if done == 2:
                raise KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):
            imp.export(m, gamedir, files, out, True, extra, cancel)
        games = os.path.join(out, "switch", "renpy-loader", "games")
        self.assertEqual(os.listdir(games), ["fake-quest"], "the staged copy is removed")
        self.assertEqual(listing(os.path.join(games, "fake-quest", "game")),
                         ["music.ogg", "options.rpy", "script.rpy", "script.rpyc"], "old export intact")

    # ------------------------------------------------- Windows, faked where needed

    def test_windows_unsafe_archive_names(self):
        bad = ["game/.. /evil.txt", "game/... /x", "game/script.rpy:stream", "game/CON", "game/nul.txt",
               "game/lpt1", "game/a?.png", "game/what*", "game/trailing.", "game/space "]
        good = ["game/images/bg.png", "game/console.rpy", "game/セーブ.png", "game/nullable.rpy"]
        with mock.patch.object(tools, "WINDOWS", True):
            for name in bad:
                with self.assertRaises(imp.Refused, msg=name):
                    imp.member_parts(name)
            for name in good:
                self.assertTrue(imp.member_parts(name), name)
            z = self.zip_of([("game/script.rpy", "label start:\n"), ("game/.. /evil.txt", "x")])
            self.assertEqual(os.listdir(self.assertRefused(z, "Windows cannot create")), [])
        self.assertEqual(imp.member_parts("game/a?.png"), ["game", "a?.png"], "fine on macOS/Linux")

    def test_windows_tool_discovery(self):
        def tool(path):
            write(path)
            os.chmod(path, 0o755)
            return path
        program_files = self.path("Program Files")
        seven = tool(os.path.join(program_files, "7-Zip", "7z.exe"))
        ffmpeg_bin = self.path("ffmpeg", "bin")
        tool(os.path.join(ffmpeg_bin, "ffprobe.exe" if os.name == "nt" else "ffprobe"))
        empty = self.path("empty")
        os.makedirs(empty)
        with mock.patch.dict(os.environ, {"PATH": empty, "ProgramFiles": program_files}), \
                mock.patch.object(tools, "SEVEN_ZIP", ("7z",)), mock.patch.object(tools, "TOOL_DIRS", []):
            self.assertEqual(imp.seven_zip(), seven)
            self.assertIsNone(imp.find_tool(("ffprobe",)))
            tools.TOOL_DIRS.append(ffmpeg_bin)   # what --tools and the UI's "Tools folder" set
            self.assertEqual(os.path.dirname(imp.find_tool(("ffprobe",))), ffmpeg_bin)
            del os.environ["ProgramFiles"]
            with self.assertRaisesRegex(imp.Refused, "7-zip.org"):
                imp.seven_zip()

    def test_windows_tools_run_without_console(self):
        with mock.patch.object(tools, "NO_WINDOW", 0x08000000), \
                mock.patch.object(tools.subprocess, "run") as run, mock.patch.object(tools, "seven_zip", lambda: "7z.exe"):
            run.return_value.stdout = "{}"
            imp.run_7z(["l", "-slt", "x.7z"])
            imp.probe_video("ffprobe.exe", "v.webm")
        for call in run.call_args_list:
            self.assertEqual(call.kwargs["creationflags"], 0x08000000)
            self.assertEqual((call.kwargs["encoding"], call.kwargs["stdin"]), ("utf-8", subprocess.DEVNULL))
        self.assertEqual(run.call_args_list[0].args[0][:3], ["7z.exe", "l", "-sccUTF-8"])

    def test_windows_bare_drive_is_its_root(self):
        with mock.patch.object(library.os, "path", ntpath), mock.patch.object(library.os, "sep", "\\"):
            for out in ("E:", "E:\\"):
                self.assertEqual(imp.games_dir(out), "E:\\switch\\renpy-loader\\games", out)

    def test_console_that_cannot_encode_the_title(self):
        src = make_game(self.path("FakeQuest"))
        write(os.path.join(src, "game", "options.rpy"), 'define config.name = _("ドキドキ")\n')
        console = io.TextIOWrapper(io.BytesIO(), encoding="cp1252")   # Windows, output redirected
        with contextlib.redirect_stdout(console):
            code = rsl_import.main([src])
        console.flush()
        self.assertEqual(code, 0)
        self.assertIn(b"???? ", console.buffer.getvalue())

    def test_rmtree_removes_read_only_files(self):
        d = self.path("unpacked")
        write(os.path.join(d, "game", "locked.rpy"), "x")
        os.chmod(os.path.join(d, "game", "locked.rpy"), stat.S_IREAD)   # read-only attribute on Windows
        imp.rmtree(d)
        self.assertFalse(os.path.exists(d))



class FakeFTP:
    """ftplib.FTP, as sphaira's server answers it, over a local folder: / is the SD card.
    fail_store=n drops the connection on the n-th upload."""

    def __init__(self, root, fail_store=None, refuse=False):
        self.root, self.fail_store, self.refuse, self.stores = root, fail_store, refuse, 0

    def connect(self, host, port, timeout):
        if self.refuse:
            raise OSError("timed out")
        self.address = (host, port)

    def login(self):
        pass

    def close(self):
        pass

    def local(self, p):
        return os.path.join(self.root, *p.strip("/").split("/"))

    def do(self, func, *args):
        try:
            return func(*args)
        except OSError as e:
            raise ftplib.error_perm("550 %s" % e.strerror)

    def mlsd(self, path):
        d = self.local(path)
        if not os.path.isdir(d):
            raise ftplib.error_perm("501 No such directory.")   # pyftpdlib's answer; not 550
        names = os.listdir(d)
        yield ".", {"type": "cdir"}
        for n in names:
            yield n, {"type": "dir" if os.path.isdir(os.path.join(d, n)) else "file", "size": "0"}

    def retrbinary(self, cmd, callback):
        with self.do(open, self.local(cmd[5:]), "rb") as f:
            callback(f.read())

    def storbinary(self, cmd, fp, blocksize=8192, callback=None):
        self.stores += 1
        if self.stores == self.fail_store:
            raise EOFError()
        with self.do(open, self.local(cmd[5:]), "wb") as f:
            for block in iter(lambda: fp.read(blocksize), b""):
                f.write(block)
                if callback:
                    callback(block)

    def cwd(self, p):
        if not os.path.isdir(self.local(p)):
            raise ftplib.error_perm("550 No such directory.")

    def mkd(self, p):
        self.do(os.mkdir, self.local(p))

    def rename(self, a, b):
        if os.path.lexists(self.local(b)):
            raise ftplib.error_perm("550 target exists")
        self.do(os.rename, self.local(a), self.local(b))

    def delete(self, p):
        self.do(os.remove, self.local(p))

    def rmd(self, p):
        self.do(os.rmdir, self.local(p))


class SwitchFtpTest(unittest.TestCase):
    """--out ftp://HOST:PORT, against FakeFTP: no Switch needed."""
    URL = "ftp://192.168.1.20:5000"

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rsl-ftp-test-")
        self.addCleanup(shutil.rmtree, self.tmp)
        self.sd = os.path.join(self.tmp, "sd")   # the Switch's SD card, as the fake server sees it
        self.root = os.path.join(self.sd, "switch", "renpy-loader")
        self.games = os.path.join(self.root, "games")
        os.makedirs(os.path.join(self.sd, "switch"))   # a card the loader was never installed on
        self.src = make_game(os.path.join(self.tmp, "FakeQuest"))
        self.switch()

    def switch(self, **kw):
        self.connections = []

        def connect():
            self.connections.append(FakeFTP(self.sd, **kw))
            return self.connections[-1]
        patcher = mock.patch.object(library.ftplib, "FTP", connect)
        patcher.start()
        self.addCleanup(patcher.stop)

    def export(self, replace=False, progress=None):
        m, gamedir, files, extra = imp.analyze(self.src, "directory", self.src)
        return imp.export(m, gamedir, files, self.URL, replace, extra, progress)

    def test_export_library_replace_delete(self):
        calls = []
        dest = self.export(progress=lambda *a: calls.append(a))
        self.assertEqual(dest, self.URL + "/switch/renpy-loader/games/fake-quest")
        self.assertEqual(self.connections[0].address, ("192.168.1.20", 5000))
        self.assertEqual(listing(os.path.join(self.games, "fake-quest")),
                         ["game", "game/music.ogg", "game/options.rpy", "game/script.rpy", "game/script.rpyc",
                          "manifest.json"])
        self.assertEqual(calls[-1], (4, 4, "", ""))
        self.assertIn((1.0, 4, calls[0][2], ""), calls, "progress within a file: done + fraction sent")
        with self.assertRaises(imp.Refused) as cm:
            self.export()
        self.assertIn("already exists", str(cm.exception))

        games = imp.exported_games(self.URL)
        self.assertEqual(games, imp.exported_games(self.sd), "the same library over FTP and from the card")
        self.assertEqual([(i, m["title"]) for i, m in games], [("fake-quest", "Fake Quest")])

        player = os.path.join(self.root, "saves", "fake-quest", "1-1-LT1.save")
        runtime = os.path.join(self.games, "fake-quest", "game", "saves", "auto-1.save")
        write(player, b"player save")
        write(runtime, b"runtime save")
        write(os.path.join(self.src, "game", "new.rpy"), "label new:\n")
        self.export(replace=True)
        self.assertTrue(os.path.isfile(os.path.join(self.games, "fake-quest", "game", "new.rpy")))
        with open(runtime, "rb") as f:
            self.assertEqual(f.read(), b"runtime save", "game/saves moves into the new game/")
        self.assertEqual(os.listdir(self.games), ["fake-quest"], "no .uploading- or .old- folder left")

        imp.delete_game(self.URL, "fake-quest")
        self.assertEqual(os.listdir(self.games), [])
        with open(player, "rb") as f:
            self.assertEqual(f.read(), b"player save", "saves/<id> is kept")
        self.export()
        imp.delete_game(self.URL, "fake-quest", saves=True)
        self.assertEqual(os.listdir(os.path.join(self.root, "saves")), [])

    def test_cancel_and_lost_connection_leave_the_switch_as_it_was(self):
        self.export()
        write(os.path.join(self.src, "game", "new.rpy"), "label new:\n")

        def cancel(done, total, rel, note):
            if done > 2:
                raise KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):
            self.export(True, cancel)
        self.assertEqual(os.listdir(self.games), ["fake-quest"], "the upload folder is removed")
        self.assertNotIn("new.rpy", os.listdir(os.path.join(self.games, "fake-quest", "game")))

        self.switch(fail_store=3)
        with self.assertRaises(EOFError):
            self.export(True)
        self.assertEqual(len(self.connections), 2, "cleaned up over a new connection")
        self.assertEqual(os.listdir(self.games), ["fake-quest"])

    def test_leftovers_and_refusals(self):
        write(os.path.join(self.games, ".uploading-fake-quest", "game", "half.rpy"))
        self.export()
        self.assertEqual(os.listdir(self.games), ["fake-quest"], "a cancelled upload's folder is replaced")

        os.makedirs(os.path.join(self.games, ".old-fake-quest", "game", "saves"))
        with self.assertRaises(imp.Refused) as cm:
            self.export(True)
        self.assertIn("may hold saves", str(cm.exception))

        for game_id in (".old-fake-quest", "../saves", "missing"):
            with self.assertRaises(imp.Refused):
                imp.delete_game(self.URL, game_id)
            with self.assertRaises(imp.Refused):
                imp.delete_game(self.sd, game_id)
        self.assertTrue(os.path.isdir(os.path.join(self.games, ".old-fake-quest")))

        with self.assertRaises(imp.Refused) as cm:
            imp.exported_games("ftp://192.168.1.20:port")
        self.assertIn("use HOST:PORT", str(cm.exception))
        self.switch(refuse=True)
        code, err = 0, io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            code = rsl_import.main([self.src, "--out", "ftp://192.168.1.20"])
        self.assertEqual(code, 1)
        self.assertIn("cannot reach the Switch at 192.168.1.20:5000", err.getvalue())

    def test_compare_collection_with_library(self):
        col = os.path.join(self.tmp, "collection")

        def game(name, title, py=3):
            base = make_game(os.path.join(col, name), py)
            write(os.path.join(base, "game", "options.rpy"), 'define config.name = _("%s")\n' % title)
            return base

        def export(source, **kw):
            m, gamedir, files, extra = imp.load(source, tempfile.mkdtemp(dir=self.tmp), **kw)
            imp.export(m, gamedir, files, self.sd, False, extra)

        export(game("Same", "Same Game"))
        export(game("Changed", "Changed Game"))
        rpyc = os.path.join(col, "Changed", "game", "script.rpyc")
        with open(rpyc, "rb") as f:
            data = f.read()
        write(rpyc, data[:-1] + bytes([data[-1] ^ 1]))   # same size, other bytes
        export(game("Grown", "Grown Game"))
        write(os.path.join(col, "Grown", "game", "extra.png"), b"x")
        export(game("Old", "Old Game"))
        os.rename(os.path.join(col, "Old"), os.path.join(col, "Old v2"))   # matched by id, not name
        export(game("Py2", "Py2 Game", py=2), recompile=True)
        export(game("Custom", "Custom Game"), game_id="my-pick")   # --id: matched by the source name
        zipped = game("Zipped", "Zipped Game")
        with zipfile.ZipFile(os.path.join(col, "Zipped.zip"), "w") as zf:
            for d, _, fs in os.walk(zipped):
                for n in fs:
                    zf.write(os.path.join(d, n), os.path.relpath(os.path.join(d, n), col))
        shutil.rmtree(zipped)
        export(os.path.join(col, "Zipped.zip"))
        game("New", "New Game")
        game("By Hand", "By Hand")
        write(os.path.join(self.games, "by-hand", "game", "script.rpy"), "label start:\n")
        write(os.path.join(self.games, "gone", "manifest.json"), '{"title": "Gone Game"}')
        write(os.path.join(col, "Notes", "todo.txt"), "not a game")
        write(os.path.join(col, "readme.txt"), "ignored: not a folder or an archive")

        rows = imp.compare(col, self.sd)
        got = {r["title"]: (r["state"], r["id"]) for r in rows}
        self.assertEqual(got, {
            "Same Game": ("same", "same-game"), "Changed Game": ("different", "changed-game"),
            "Grown Game": ("different", "grown-game"), "Old Game": ("same", "old-game"),
            "Py2 Game": ("same", "py2-game"), "Zipped Game": ("same", "zipped-game"),
            "Custom Game": ("same", "my-pick"),
            "New Game": ("missing", "new-game"), "By Hand": ("unknown", "by-hand"),
            "Gone Game": ("only", "gone"), "Notes": ("unreadable", None)})
        why = {r["title"]: r["why"] for r in rows}
        self.assertEqual(why["Changed Game"], ["1 scripts or archives differ: script.rpyc"])
        self.assertEqual(why["Grown Game"], ["5 files in the folder, 4 in the library", "1 byte more in the folder"])
        self.assertEqual(why["Old Game"], ["the library copy came from Old"])
        self.assertEqual([r["state"] for r in rows][:2], ["different", "different"], "differences first")
        self.assertEqual(imp.compare(col, self.URL), rows, "the same over FTP")

    @unittest.skipUnless(HAVE_FFMPEG, "FFmpeg not installed")
    def test_pictures_for_games_in_the_library(self):
        self.export()
        art = picture(os.path.join(self.tmp, "art.png"), "blue")
        dest = os.path.join(self.games, "fake-quest")
        for target in (self.URL, self.sd):
            imp.set_art(target, "fake-quest", "cover", art)
            got = imp.read_art(target, "fake-quest")
            self.assertEqual(list(got), ["cover"])
            self.assertEqual(got["cover"][0], "cover.jpg")
            loader_jpeg(self, os.path.join(dest, "cover.jpg"), (300, 450))
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        self.assertEqual((m["cover"], m["cover_custom"], m["title"]), ("cover.jpg", True, "Fake Quest"))

        # Chosen on the Switch: the loader writes cover.bmp. An update over FTP keeps it.
        m.update(cover="cover.bmp", cover_custom=True)
        write(os.path.join(dest, "manifest.json"), json.dumps(m))
        write(os.path.join(dest, "cover.bmp"), b"BM switch")
        self.export(replace=True)
        self.assertEqual(sorted(os.listdir(dest)), ["cover.bmp", "game", "manifest.json"])
        self.assertEqual(os.listdir(self.games), ["fake-quest"], "no .old- folder left")
        self.assertEqual(sorted(imp.read_art(self.URL, "fake-quest")), ["cover"])

        write(os.path.join(self.games, "by-hand", "game", "script.rpy"), "label start:\n")
        for game, why in (("by-hand", "no manifest.json"), ("gone", "not in the library"), ("..", "not in the library")):
            with self.assertRaises(imp.Refused) as cm:
                imp.set_art(self.URL, game, "cover", art)
            self.assertIn(why, str(cm.exception))
        with self.assertRaises(imp.Refused):
            imp.set_art(self.URL, "fake-quest", "cover", os.path.join(self.tmp, "missing.png"))

    def test_local_delete_keeps_saves(self):
        m, gamedir, files, extra = imp.analyze(self.src, "directory", self.src)
        imp.export(m, gamedir, files, self.sd, False, extra)
        write(os.path.join(self.root, "saves", "fake-quest", "1-1-LT1.save"), b"player save")
        imp.delete_game(self.sd, "fake-quest")
        self.assertEqual(os.listdir(self.games), [])
        self.assertEqual(os.listdir(os.path.join(self.root, "saves")), ["fake-quest"])
        imp.export(m, gamedir, files, self.sd, False, extra)
        imp.delete_game(self.sd, "fake-quest", saves=True)
        self.assertEqual(os.listdir(os.path.join(self.root, "saves")), [])


if __name__ == "__main__":
    unittest.main()
