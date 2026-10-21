#!/usr/bin/env python3
"""Builds the importer window as a program for people without Python (PyInstaller).

usage: python -m pip install pyinstaller pywebview
       python importer/build_app.py

Windows: dist/importer/RSL Importer.exe (one file). macOS: dist/importer/RSL Importer.app.
Build on the system you build for; PyInstaller does not cross-compile. FFmpeg and 7-Zip
are not bundled (their licences); the program finds them on the user's system, see
docs/IMPORTER.md.
"""
import os
import sys

import PyInstaller.__main__

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(here)
PyInstaller.__main__.run([
    os.path.join(here, "rsl_import_gui.py"),
    "--name", "RSL Importer",
    "--add-data", os.path.join(here, "rsl_import_gui.html") + os.pathsep + ".",
    "--windowed",                                              # no console window
    "--onefile" if sys.platform == "win32" else "--onedir",    # macOS: a .app bundle
    "--noconfirm",
    "--distpath", os.path.join(repo, "dist", "importer"),
    "--workpath", os.path.join(repo, "build", "test", "pyinstaller"),
    "--specpath", os.path.join(repo, "build", "test", "pyinstaller"),
])
