"""Checks the Horizon device-path patches (runtime/python/patches/0005 posixpath,
0006 importlib) on the patched sources, loaded as if sys.platform were "horizon".
Hardware run 3 showed why both are needed: importlib keeps its own isabs().

usage: python3.12 test_horizon_paths.py <patched CPython Lib dir>
"""
import importlib.util
import os
import sys


def load(path, platform):
    real = sys.platform
    sys.platform = platform
    try:
        spec = importlib.util.spec_from_file_location("posixpath_" + platform, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    finally:
        sys.platform = real


def main():
    lib = sys.argv[1]
    path = os.path.join(lib, "posixpath.py")
    hz = load(path, "horizon")
    lx = load(path, "linux")
    checks = 0

    def check(cond, what):
        nonlocal checks
        checks += 1
        assert cond, what

    for p in ["sdmc:/switch/x", "romfs:/renpy-base/renpy.py", b"sdmc:/a", "/abs"]:
        check(hz.isabs(p), "isabs %r on horizon" % (p,))
    for p in ["game/script.rpy", "", "a:b/c", ":/x", "sdmc:", "renpy.py"]:
        check(not hz.isabs(p), "not isabs %r on horizon" % (p,))
    # Unchanged elsewhere.
    check(not lx.isabs("sdmc:/x"), "linux keeps posix semantics")
    check(lx.join("sdmc:/a", "romfs:/b") == "sdmc:/a/romfs:/b", "linux join unchanged")

    check(hz.join("sdmc:/games", "x", "game") == "sdmc:/games/x/game", "join relative")
    check(hz.join("sdmc:/a", "romfs:/b") == "romfs:/b", "join resets on device path")
    check(hz.join(b"sdmc:/a", b"b") == b"sdmc:/a/b", "join bytes")
    check(hz.abspath("romfs:/renpy-base/renpy.py") == "romfs:/renpy-base/renpy.py", "abspath keeps device path")
    check(hz.abspath("romfs:/a/../b/./c") == "romfs:/b/c", "abspath normalises")
    check(hz.dirname("romfs:/renpy-base/renpy.py") == "romfs:/renpy-base", "dirname")
    check(hz.realpath("sdmc:/switch/../switch/x") == "sdmc:/switch/x", "realpath without symlinks")
    check(hz.relpath("sdmc:/a/b/c", "sdmc:/a") == "b/c", "relpath")
    check(hz.normpath("sdmc:/a//b/") == "sdmc:/a/b", "normpath")
    cwd = os.getcwd()
    check(hz.abspath("rel") == hz.join(cwd, "rel"), "relative still uses cwd")
    # importlib's private copies (spec_from_file_location, FileFinder).
    bx = load(os.path.join(lib, "importlib", "_bootstrap_external.py"), "horizon")
    bl = load(os.path.join(lib, "importlib", "_bootstrap_external.py"), "linux")
    for p in ["romfs:/renpy-base", "sdmc:/switch/x.py", "/abs"]:
        check(bx._path_isabs(p), "importlib isabs %r on horizon" % p)
    for p in ["renpy", "", ":/x", "a:b/c"]:
        check(not bx._path_isabs(p), "importlib not isabs %r on horizon" % p)
    check(bx._path_abspath("romfs:/renpy-base/renpy.py") == "romfs:/renpy-base/renpy.py",
          "importlib abspath keeps device path (hardware run 3 failure)")
    check(not bl._path_isabs("romfs:/x"), "importlib unchanged on linux")
    print("horizon paths: %d checks passed" % checks)


if __name__ == "__main__":
    main()
