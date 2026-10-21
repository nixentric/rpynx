"""Tests for importer/rsl_import_gui.py: the Api the window's page calls, driven without a
window (pywebview is not needed), and the page's calls into it.

usage: /usr/bin/python3 -m unittest tests/unit/test_importer_gui.py
"""
import os
import re
import shutil
import sys
import tempfile
import unittest
from unittest import mock

here = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [here, os.path.join(here, "..", "..", "importer")]
from test_importer import FakeFTP, make_game, write  # noqa: E402
import rsl_import_gui as gui  # noqa: E402


class FakeWindow:
    def __init__(self, on_js=None):
        self.js, self.on_js = [], on_js

    def evaluate_js(self, script):
        self.js.append(script)
        if self.on_js:
            self.on_js()


class ApiTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rsl-gui-test-")
        self.addCleanup(shutil.rmtree, self.tmp)
        self.api = gui.Api()
        self.addCleanup(self.api._drop_tmp)
        self.sd = os.path.join(self.tmp, "sd")
        self.games = os.path.join(self.sd, "switch", "renpy-loader", "games")

    def test_analyze_export_library(self):
        self.api._window = FakeWindow()
        r = self.api.analyze(make_game(os.path.join(self.tmp, "FakeQuest")))
        self.assertEqual(r["manifest"]["status"], "experimental")
        self.assertIn("EXPERIMENTAL on runtime renpy-8.5.3", r["report"])

        r = self.api.export(self.sd)
        self.assertEqual(r["dest"], os.path.join(self.games, "fake-quest"))
        self.assertTrue(os.path.isfile(os.path.join(r["dest"], "game", "script.rpyc")))
        self.assertEqual(self.api._window.js[-1], 'progress(...[4, 4, "", ""])')
        self.assertIn("already exists", self.api.export(self.sd)["error"])

        lib = self.api.library(self.sd)
        self.assertEqual([(g["id"], g["manifest"]["title"]) for g in lib["games"]], [("fake-quest", "Fake Quest")])
        self.assertIn("EXPERIMENTAL", lib["games"][0]["report"])

    def test_refusals_come_back_as_errors(self):
        self.assertIn("try 'bad-id'", self.api.analyze(self.tmp, game_id="Bad Id")["error"])
        self.assertIn("no Ren'Py", self.api.analyze(self.tmp)["error"])
        self.assertEqual(self.api.export(self.sd), {"error": "Analyze a game first."})
        r = self.api.analyze(make_game(os.path.join(self.tmp, "OldGame"), py=2))
        self.assertEqual(r["manifest"]["status"], "unsupported")
        self.assertIn("recompile", r["manifest"]["suggested_options"])   # the page shows it as an alert
        self.assertIn("Suggested: --recompile", r["report"])
        self.assertIn("no runtime can run this game", self.api.export(self.sd)["error"])
        os.remove(os.path.join(self.tmp, "OldGame", "game", "script.rpy"))   # sourceless: suggest Decompile
        r = self.api.analyze(os.path.join(self.tmp, "OldGame"))
        self.assertEqual(list(r["manifest"]["suggested_options"]), ["decompile"])
        with mock.patch.object(gui.imp, "UNRPYC", os.path.join(self.tmp, "none.py")):
            self.assertIn("unrpyc not found", self.api.analyze(os.path.join(self.tmp, "OldGame"),
                                                               decompile="unrpyc")["error"])
            self.assertFalse(self.api.tools()["unrpyc"])

    def test_cancel_leaves_sd_unchanged(self):
        self.api._window = FakeWindow(on_js=self.api.cancel)   # Cancel pressed during the first file
        self.api.analyze(make_game(os.path.join(self.tmp, "FakeQuest")))
        self.assertIn("cancelled", self.api.export(self.sd)["error"])
        self.assertEqual(os.listdir(self.games), [], "the staged copy is removed")

    def test_library_lists_hand_copied_and_broken_games(self):
        write(os.path.join(self.games, "by-hand", "game", "script.rpy"), "label start:\n")
        write(os.path.join(self.games, "broken", "manifest.json"), "{not json")
        write(os.path.join(self.games, "old", "manifest.json"), '{"title": "Old", "status": "experimental"}')
        os.makedirs(os.path.join(self.games, ".rsl-import-x1"))   # a staging folder
        games = {g["id"]: g for g in self.api.library(self.sd)["games"]}
        self.assertEqual(sorted(games), ["broken", "by-hand", "old"])
        self.assertIsNone(games["by-hand"]["manifest"])
        self.assertIn("No manifest.json", games["broken"]["report"])
        self.assertIn('"title": "Old"', games["old"]["report"], "a manifest report() cannot read is shown raw")
        self.assertEqual(self.api.library(os.path.join(self.tmp, "empty"))["games"], [])

    def test_switch_over_ftp_and_delete(self):
        url = "ftp://192.168.1.20:5000"
        os.makedirs(self.sd)   # the FTP server's /
        self.api._window = FakeWindow()
        self.api.analyze(make_game(os.path.join(self.tmp, "FakeQuest")))
        with mock.patch.object(gui.imp.ftplib, "FTP", lambda: FakeFTP(self.sd)):
            self.assertEqual(self.api.export(url)["dest"], url + "/switch/renpy-loader/games/fake-quest")
            self.assertTrue(os.path.isfile(os.path.join(self.games, "fake-quest", "manifest.json")))
            lib = self.api.library(url)
            self.assertEqual(lib["root"], url + "/switch/renpy-loader/games")
            self.assertEqual([g["id"] for g in lib["games"]], ["fake-quest"])
            self.assertEqual(self.api.delete(url, "fake-quest"), {})
            self.assertIn("is not there", self.api.delete(url, "fake-quest")["error"])
        with mock.patch.object(gui.imp.ftplib, "FTP", lambda: FakeFTP(self.sd, refuse=True)):
            self.assertIn("cannot reach the Switch", self.api.library(url)["error"])
            self.assertIn("cannot reach the Switch", self.api.export(url)["error"])

    def test_compare(self):
        col = os.path.join(self.tmp, "collection")
        make_game(os.path.join(col, "FakeQuest"))
        self.assertEqual(self.api.compare(col, None), {"error": "Choose a target first."})
        self.api._window = FakeWindow()
        rows = self.api.compare(col, self.sd)["rows"]
        self.assertEqual([(r["state"], r["id"]) for r in rows], [("missing", "fake-quest")])
        self.assertEqual(self.api._window.js[-1], 'compareProgress(...[1, 1, ""])')
        self.api._window = FakeWindow(on_js=self.api.cancel)
        self.assertEqual(self.api.compare(col, self.sd), {"error": "Comparison cancelled."})

    def test_page_calls_only_what_exists(self):
        with open(gui.PAGE, encoding="utf-8") as f:
            page = f.read()
        called = set(re.findall(r'call\("(\w+)"', page))
        self.assertEqual(called - {n for n in dir(gui.Api) if not n.startswith("_")}, set())
        self.assertGreaterEqual(called, {"analyze", "export", "library", "pick", "cancel", "delete", "compare"})
        for option in ("optimize", "recompile", "decompile"):   # alerts for rsl_import's suggested_options
            self.assertIn('id="hint-%s"' % option, page)
            self.assertIn('id="%s"' % option, page)
        self.assertEqual(set(re.findall(r'name="decompiler" value="(\w+)"', page)), set(gui.imp.DECOMPILERS))
        for function in ("progress", "dropped", "compareProgress"):   # what Python calls on the page
            self.assertIn("window.%s = " % function, page)


if __name__ == "__main__":
    unittest.main()
