"""Stage Ren'Py's Python side for the Switch runtime's RomFS.

usage: stage_engine.py <renpy src> <sdk renpy/common dir> <out dir> <overlay dir>...

- Copies renpy.py and the renpy/ package (Python, common/ scripts and data),
  skipping Cython sources, type stubs and caches.
- Copies the overlay directories on top: runtime/compatibility (replacements
  for modules that cannot be built for the Switch) and runtime/renpy/boot
  (the Switch launcher script and controller bridge).
- Adds the official SDK's compiled common/*.rpyc and *.rpymc after checking
  each embedded digest against our source, the same check Ren'Py makes at
  load. Without them the console would recompile the engine scripts on every
  launch, because RomFS is read-only.
- Writes unchecked-hash .pyc files into __pycache__ for every .py (no
  recompilation on the console; tracebacks still show source).
"""
import ast
import hashlib
import importlib.util
import os
import py_compile
import re
import shutil
import sys

SKIP_EXT = {".pyx", ".pxd", ".pxi", ".pyi"}
ROMFS_BASE = "romfs:/renpy-base"


def copy_tree(src, dst):
    for root, dirs, files in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d != "__pycache__")
        rel = os.path.relpath(root, src)
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in sorted(files):
            if os.path.splitext(f)[1] not in SKIP_EXT:
                shutil.copy2(os.path.join(root, f), os.path.join(dst, rel, f))


def source_for(compiled):
    """common/x.rpyc -> x.rpy or x_ren.py; x.rpymc -> x.rpym."""
    base = compiled[:-1]
    if compiled.endswith(".rpyc") and not os.path.exists(base):
        return compiled[:-5] + "_ren.py"
    return base


def main():
    src, sdk_common, out, overlays = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    script = open(os.path.join(src, "renpy", "script.py")).read()
    magic = re.search(r'^RPYC_MAGIC = (b".*")$', script, re.M).group(1)
    magic = ast.literal_eval(magic)

    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    shutil.copy2(os.path.join(src, "renpy.py"), out)
    copy_tree(os.path.join(src, "renpy"), os.path.join(out, "renpy"))
    for overlay in overlays:
        copy_tree(overlay, out)

    common = os.path.join(out, "renpy", "common")
    added = 0
    for name in sorted(os.listdir(sdk_common)):
        if not name.endswith((".rpyc", ".rpymc")):
            continue
        ours = source_for(os.path.join(common, name))
        if not os.path.exists(ours):
            sys.exit("SDK %s has no matching source in this Ren'Py tree" % name)
        with open(ours, "rb") as f:
            want = hashlib.md5(f.read() + magic).digest()
        with open(os.path.join(sdk_common, name), "rb") as f:
            data = f.read()
        if data[-16:] != want:
            sys.exit("SDK %s was compiled from a different %s" % (name, os.path.basename(ours)))
        with open(os.path.join(common, name), "wb") as f:
            f.write(data)
        added += 1

    compiled = 0
    for root, dirs, files in os.walk(out):
        dirs.sort()
        for f in sorted(files):
            if f.endswith(".py"):
                path = os.path.join(root, f)
                rel = os.path.relpath(path, out).replace(os.sep, "/")
                py_compile.compile(
                    path, cfile=importlib.util.cache_from_source(path),
                    dfile=ROMFS_BASE + "/" + rel, doraise=True,
                    invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
                compiled += 1
    print("%s: %d .py compiled, %d common scripts precompiled (digests verified)" % (out, compiled, added))


if __name__ == "__main__":
    main()
