"""Tests for importer/rsl_import.py. Fixtures are fake games built in a temp dir.

usage: /usr/bin/python3 -m unittest tests/unit/test_importer.py
"""
import contextlib
import io
import json
import os
import pickle
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "importer"))
import rsl_import as imp  # noqa: E402


def rpyc(protocol):
    """A minimal RPC2 .rpyc: slot 1 holds a zlib'd pickle of the given protocol."""
    body = zlib.compress(pickle.dumps({"version": 5003000, "key": "unlocked"}, protocol=protocol))
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
            code = imp.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def analyze_dir(self, base):
        return imp.analyze(base, "directory", base)

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
        old = imp.MAX_FILES
        imp.MAX_FILES = 1
        try:
            self.assertRefused(z, "entries (limit 1)")
        finally:
            imp.MAX_FILES = old

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
        code, stdout, err = self.run_cli(src, "--out", self.path("sd"))
        self.assertEqual(code, 1)
        self.assertIn("UNSUPPORTED", stdout)
        self.assertFalse(os.path.exists(self.path("sd")))

    def test_py2_rpyc_without_engine_is_unsupported(self):
        write(self.path("g", "game", "script.rpyc"), rpyc(2))
        m, _, _ = self.analyze_dir(self.path("g"))
        self.assertEqual((m["python"], m["status"]), ("2", "unsupported"))

    def test_windows_native_code_and_non_ascii_warn(self):
        src = make_game(self.path("FakeQuest"))
        write(os.path.join(src, "game", "python-packages", "fast.pyd"), b"MZ")
        write(os.path.join(src, "game", "images", "セーブ.png"), b"\x89PNG")
        write(os.path.join(src, "game", "intro.wmv"), b"")
        m, _, _ = self.analyze_dir(src)
        self.assertEqual(m["status"], "experimental", "warnings do not block")
        text = "\n".join(m["warnings"])
        self.assertIn("native code that cannot load on the Switch: python-packages/fast.pyd", text)
        self.assertIn("python-packages present", text)
        self.assertIn("1 files with non-ASCII names", text)
        self.assertIn(".wmv", text)

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

    def test_bad_id_rejected(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            imp.main([self.tmp, "--id", "Bad Id"])

    def test_not_a_game(self):
        write(self.path("docs", "readme.txt"), "hi")
        code, _, err = self.run_cli(self.path("docs"))
        self.assertEqual(code, 1)
        self.assertIn("no Ren'Py", err)

    @unittest.skipUnless(shutil.which("7z"), "7z not installed")
    def test_7z_archive(self):
        src = make_game(self.path("src", "FakeQuest"))
        archive = self.path("FakeQuest.7z")
        subprocess.run(["7z", "a", "-bd", archive, src], check=True, stdout=subprocess.DEVNULL)
        code, stdout, err = self.run_cli(archive)
        self.assertEqual(code, 0, err)
        self.assertIn("EXPERIMENTAL", stdout)


if __name__ == "__main__":
    unittest.main()
