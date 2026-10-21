"""Errors, ids and small helpers shared by every part of the importer."""
import ftplib
import hashlib
import re


# The loader skips folder names of 64+ bytes (app/launcher/main.c, games[64]).
ID_MAX = 63
VIDEO_EXTS = (".webm", ".ogv", ".mkv", ".mp4", ".m4v", ".mov", ".avi", ".wmv", ".mpg", ".mpeg", ".flv")
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".webp")
JUNK = {".DS_Store", "Thumbs.db", "desktop.ini", "__MACOSX"}


class Refused(Exception):
    """The source cannot be imported; the message says why."""


ERRORS = (Refused,) + ftplib.all_errors   # what the user can act on: refusals, disk and network


def make_id(text):
    """Folder-safe game id: lowercase ASCII [a-z0-9], other runs -> '-'."""
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:ID_MAX].strip("-") or "game"


def is_junk(name):
    return name in JUNK or name.startswith("._")


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def examples(items, n=3):
    items = sorted(items)
    return ", ".join(items[:n]) + (" and %d more" % (len(items) - n) if len(items) > n else "")
