"""The loader's FTP server (platform/transfer/ftpd.c) on the host, driven by the importer's own FTP code:
exports, replaces and deletes a game the way the desktop importer does with a Switch.

usage: /usr/bin/python3 -m unittest tests/unit/test_ftpd.py   (after scripts/test.sh built build/host/ftpd_host)
"""
import ftplib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from test_importer import imp, listing, make_game, write  # noqa: E402

SERVER = os.path.join(HERE, "..", "..", "build", "host", "ftpd_host")


class LoaderFtpTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rsl-ftpd-test-")
        self.addCleanup(shutil.rmtree, self.tmp)
        self.sd = os.path.join(self.tmp, "sd")
        self.root = os.path.join(self.sd, "switch", "renpy-loader")
        self.games = os.path.join(self.root, "games")
        os.makedirs(self.games)
        write(os.path.join(self.sd, "atmosphere", "config.ini"), "keep")   # outside the jail
        self.server = subprocess.Popen([SERVER, self.sd], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.addCleanup(self.stop)
        ready = self.server.stdout.readline().split()
        self.assertEqual(ready[0], "ready")
        self.url = "ftp://127.0.0.1:%s" % ready[1]
        self.port = int(ready[1])

    def stop(self):
        if self.server.poll() is None:
            out, _ = self.server.communicate("", timeout=10)
            self.summary = out.strip()

    def ftp(self):
        f = ftplib.FTP()
        f.connect("127.0.0.1", self.port, 10)
        f.login()
        self.addCleanup(f.close)
        return f

    def test_importer_export_replace_delete(self):
        src = make_game(os.path.join(self.tmp, "FakeQuest"))

        def export(replace=False):
            m, gamedir, files, extra = imp.analyze(src, "directory", src)
            return imp.export(m, gamedir, files, self.url, replace, extra, None)

        self.assertEqual(export(), self.url + "/switch/renpy-loader/games/fake-quest")
        self.assertEqual(listing(os.path.join(self.games, "fake-quest")),
                         ["game", "game/music.ogg", "game/options.rpy", "game/script.rpy", "game/script.rpyc",
                          "manifest.json"])
        with open(os.path.join(src, "game", "script.rpy"), "rb") as a, \
                open(os.path.join(self.games, "fake-quest", "game", "script.rpy"), "rb") as b:
            self.assertEqual(a.read(), b.read())
        self.assertEqual([(i, m["title"]) for i, m in imp.exported_games(self.url)], [("fake-quest", "Fake Quest")])

        runtime = os.path.join(self.games, "fake-quest", "game", "saves", "auto-1.save")
        write(runtime, b"runtime save")
        write(os.path.join(src, "game", "new.rpy"), "label new:\n")
        export(replace=True)
        self.assertTrue(os.path.isfile(os.path.join(self.games, "fake-quest", "game", "new.rpy")))
        with open(runtime, "rb") as f:
            self.assertEqual(f.read(), b"runtime save", "game/saves moves into the new game/")
        self.assertEqual(os.listdir(self.games), ["fake-quest"], "no .uploading- or .old- folder left")

        imp.delete_game(self.url, "fake-quest", saves=True)
        self.assertEqual(os.listdir(self.games), [])
        self.stop()
        self.assertRegex(self.summary, r"^received \d+ file\(s\), \d+ bytes; expecting 0; done fake-quest$",
                         "the upload announced its size, and moving it into place finished it")

    def test_curl_as_switch_ftp_sh_uses_it(self):
        local = os.path.join(self.tmp, "renpy-loader.nro")
        write(local, os.urandom(300000))
        base = "ftp://127.0.0.1:%d/switch/renpy-loader" % self.port
        subprocess.run(["curl", "-sf", "--connect-timeout", "5", "--ftp-create-dirs", "-T", local, base + "/"],
                       check=True)
        got = subprocess.run(["curl", "-sf", base + "/renpy-loader.nro"], check=True, capture_output=True).stdout
        with open(local, "rb") as f:
            self.assertEqual(got, f.read())
        self.assertEqual(listing(self.root), ["games", "renpy-loader.nro"], "no switch/renpy-loader inside it")

    def test_progress_announced(self):
        self.ftp().voidcmd("ALLO 123456789012")
        self.stop()
        self.assertEqual(self.summary, "received 0 file(s), 0 bytes; expecting 123456789012; done -")

    def test_nothing_outside_the_jail(self):
        f = self.ftp()
        self.assertEqual(f.pwd(), "/", "the SD card's root, as with sphaira")
        self.assertEqual(dict(f.mlsd("/")), {"switch": {"type": "dir", "size": "0"}}, "only the way down")
        self.assertEqual([n for n, _ in f.mlsd("/switch")], ["renpy-loader"])
        f.cwd("switch")
        f.cwd("renpy-loader")
        self.assertEqual(f.pwd(), "/switch/renpy-loader")
        for cmd in ("STOR /switch/evil.txt", "STOR /switch/renpy-loader/../evil.txt", "STOR ../../atmosphere/x",
                    "STOR /switch/renpy-loader-x/evil.txt"):
            with self.assertRaises(ftplib.error_perm, msg=cmd):
                f.storbinary(cmd, io.BytesIO(b"x"))
        for call in (lambda: f.retrbinary("RETR /atmosphere/config.ini", print),
                     lambda: f.retrbinary("RETR ../../atmosphere/config.ini", print),
                     lambda: f.delete("/atmosphere/config.ini"), lambda: f.mkd("/atmosphere/new"),
                     lambda: f.rename("/atmosphere/config.ini", "games/config.ini"),
                     lambda: f.rename("games", "/switch/games"), lambda: f.cwd("/atmosphere"),
                     lambda: list(f.mlsd("/atmosphere")), lambda: f.mkd("/switch/new"),
                     lambda: f.retrbinary("RETR /switch", print)):
            with self.assertRaises(ftplib.error_perm):
                call()
        self.assertEqual(listing(self.sd), ["atmosphere", "atmosphere/config.ini", "switch", "switch/renpy-loader",
                                            "switch/renpy-loader/games"])

    def test_big_file_and_listings(self):
        data = os.urandom(5 << 20)
        f = self.ftp()
        f.cwd("switch/renpy-loader/games")
        f.mkd("big")
        f.storbinary("STOR big/blob.bin", io.BytesIO(data), 64 << 10)
        self.assertEqual(f.size("big/blob.bin"), len(data))
        got = io.BytesIO()
        f.retrbinary("RETR /switch/renpy-loader/games/big/blob.bin", got.write)
        self.assertEqual(got.getvalue(), data)
        self.assertEqual(dict(f.mlsd("big")), {"blob.bin": {"type": "file", "size": str(len(data))}})
        self.assertEqual(f.nlst("big"), ["blob.bin"])
        lines = []
        f.retrlines("LIST -la big", lines.append)
        self.assertEqual(len(lines), 1)
        self.assertTrue(lines[0].startswith("-rw-r--r--") and lines[0].endswith(" blob.bin"), lines[0])
        f.cwd("..")
        self.assertEqual(f.pwd(), "/switch/renpy-loader")
        start = time.monotonic()
        self.stop()   # B on the Switch, with the importer still connected
        self.assertLess(time.monotonic() - start, 3)
        self.assertEqual(self.summary, "received 1 file(s), %d bytes; expecting 0; done -" % len(data))


if __name__ == "__main__":
    unittest.main()
