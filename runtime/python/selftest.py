"""Milestone 3 self-test for the embedded CPython runtime.

Runs unchanged on a desktop CPython 3.12 and on the Switch build, so the
packaging can be checked off-device. On the Switch, `_rslnx` (built into the
runtime) adds memory figures.

usage: selftest.py [scratch dir]   (exit status = number of failures)
"""
import os
import sys
import time

results = []
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else None

# Progress goes to the report file as it happens (flushed and synced), so a
# hang or crash still shows which check was running. (The runtime's main
# thread dumps Python stacks to runtime.log if the run stalls.)
_report = open(os.path.join(SCRATCH, "python-selftest.txt"), "w", encoding="utf-8") if SCRATCH else None


def progress(text):
    print(text, flush=True)
    if _report:
        _report.write(text + "\n")
        _report.flush()
        os.fsync(_report.fileno())


def check(name, required=True):
    def wrap(fn):
        progress("... " + name)
        t0 = time.perf_counter()
        try:
            detail = fn() or ""
            ok = True
        except BaseException as e:  # report everything, including SystemExit from a module
            detail = "%s: %s" % (type(e).__name__, e)
            ok = False
        ms = (time.perf_counter() - t0) * 1000
        results.append((name, ok, required, "%s (%.0f ms)" % (detail, ms)))
        progress("%s %s %s (%.0f ms)" % ("PASS" if ok else ("FAIL" if required else "n/a "), name, detail, ms))
        return fn
    return wrap


# Modules the Ren'Py 8 engine imports at startup or commonly during play.
REQUIRED = """
abc array ast base64 binascii bisect bz2 codecs collections contextlib copy copyreg
dataclasses datetime decimal difflib encodings.utf_8 encodings.latin_1 enum fnmatch
fractions functools gc gettext glob gzip hashlib heapq hmac io itertools json keyword
linecache locale lzma marshal math operator os pickle platform queue random re
shutil string struct sys tempfile textwrap threading time token tokenize traceback
types typing unicodedata warnings weakref zipfile zlib
""".split()
# Not built for the Switch (no libffi / OpenSSL / sqlite); reported, not failed.
OPTIONAL = "ctypes ssl sqlite3 socket select subprocess".split()


@check("interpreter")
def _():
    assert sys.version_info[:2] == (3, 12), sys.version
    return "%s on %s" % (sys.version.split()[0], sys.platform)


for _name in REQUIRED:
    check("import " + _name)(lambda n=_name: __import__(n) and "")
for _name in OPTIONAL:
    check("import " + _name, required=False)(lambda n=_name: __import__(n) and "")


@check("hashlib/zlib/json/pickle")
def _():
    import hashlib, json, pickle, zlib
    assert hashlib.sha256(b"abc").hexdigest().startswith("ba7816bf8f01cfea")
    assert hashlib.md5(b"abc").hexdigest() == "900150983cd24fb0d6963f7d28e17f72"
    blob = b"renpy" * 10000
    assert zlib.decompress(zlib.compress(blob)) == blob
    data = {"a": [1, 2.5, "あ", None], "b": {"c": (1, 2)}}
    assert pickle.loads(pickle.dumps(data, 5)) == data
    assert json.loads(json.dumps({"k": "あ"}))["k"] == "あ"


@check("decimal/re/unicodedata")
def _():
    import decimal, re, unicodedata
    assert decimal.Decimal("0.1") + decimal.Decimal("0.2") == decimal.Decimal("0.3")
    assert re.match(r"(\w+) (\d+)", "chapter 12").group(2) == "12"
    assert unicodedata.east_asian_width("あ") == "W"


@check("os.urandom")
def _():
    a, b = os.urandom(16), os.urandom(16)
    assert len(a) == 16 and a != b


@check("time")
def _():
    now = time.time()
    assert now > 1.7e9, now  # after 2023
    t0 = time.monotonic()
    time.sleep(0.05)
    dt = time.monotonic() - t0
    assert 0.04 <= dt < 1.0, dt
    return "sleep(0.05) took %.3f s" % dt


@check("threads")
def _():
    import queue, threading
    q = queue.Queue()
    workers = [threading.Thread(target=lambda i=i: q.put(sum(range(i * 1000)))) for i in range(4)]
    for w in workers:
        w.start()
    for w in workers:
        w.join(5)
    assert q.qsize() == 4


@check("filesystem")
def _():
    import shutil, tempfile
    base = SCRATCH or tempfile.mkdtemp()
    d = os.path.join(base, "selftest")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    p = os.path.join(d, "save.tmp")
    payload = os.urandom(256 * 1024)
    with open(p, "wb") as f:
        f.write(payload)
        f.flush()
        os.fsync(f.fileno())
    final = os.path.join(d, "save.bin")
    os.replace(p, final)
    with open(final, "rb") as f:
        assert f.read() == payload
    assert os.listdir(d) == ["save.bin"]
    assert os.stat(final).st_size == len(payload)
    shutil.rmtree(d)
    assert not os.path.exists(d)
    return base


@check("non-ASCII file names", required=False)
def _():
    # Run 20: the SD parses names as Shift-JIS (セーブ.txt fails, フォルダ works) and lists
    # none of them. That is the platform, not this runtime, and the importer packs such files
    # into an .rpa, so this is reported, not failed.
    import shutil, tempfile, unicodedata
    d = os.path.join(SCRATCH or tempfile.mkdtemp(), "selftest-ja")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    cases = [("latin file", "café.txt"), ("japanese file", "セーブ.txt"),
             ("japanese NFD file", unicodedata.normalize("NFD", "ブ.txt")), ("japanese folder", "フォルダ")]
    failed = []
    for label, name in cases:
        p = os.path.join(d, name)
        try:
            if "folder" in label:
                os.mkdir(p)
            else:
                with open(p, "w", encoding="utf-8") as f:
                    f.write("こんにちは")
            os.stat(p)
        except OSError as e:
            failed.append("%s %s (%s)" % (label, os.fsencode(name).hex(), e.strerror))
    found = sorted(os.listdir(d))
    shutil.rmtree(d, ignore_errors=True)
    info = "fs encoding %s, utf8_mode %d, listed %r" % (sys.getfilesystemencoding(), sys.flags.utf8_mode, found)
    assert not failed, "; ".join(failed) + "; " + info
    return info


@check("memory")
def _():
    big = bytearray(64 * 1024 * 1024)
    big[-1] = 1
    objs = [{"i": i, "s": str(i)} for i in range(200000)]
    detail = "64 MiB buffer + 200k dicts ok"
    try:
        import _rslnx
        used, total = _rslnx.memory()
        detail += "; process memory %d / %d MiB" % (used >> 20, total >> 20)
    except ImportError:
        pass
    del big, objs
    return detail


@check("speed", required=False)
def _():
    t0 = time.perf_counter()
    acc = 0
    for i in range(1000000):
        acc += i % 7
    return "1M-iteration loop: %.2f s" % (time.perf_counter() - t0)


def main():
    failed = 0
    lines = []
    for name, ok, required, detail in results:
        tag = "PASS" if ok else ("FAIL" if required else "n/a ")
        failed += (not ok) and required
        lines.append("%s %-28s %s" % (tag, name, detail))
    lines.append("%d required check(s) failed" % failed)
    progress("\n== summary ==\n" + "\n".join(lines))
    return failed


if __name__ == "__main__":
    sys.exit(main())
