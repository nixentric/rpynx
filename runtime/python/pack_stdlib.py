"""Compile the CPython stdlib to .pyc and pack it into a zip for zipimport.

Must run under the same CPython version as the target (bytecode magic).
Output is deterministic: sorted entries, fixed timestamps, hash-based pycs.

usage: pack_stdlib.py <Lib dir> <out.zip> [extra.py ...]
"""
import importlib.util
import os
import sys
import zipfile

# Not needed at runtime on the console, or unusable there (GUI, venv, tests).
SKIP_DIRS = {
    "test", "idlelib", "tkinter", "turtledemo", "ensurepip", "venv", "lib2to3",
    "pydoc_data", "site-packages", "__pycache__", "__phello__",
}


def pyc(source, path):
    code = compile(source, path, "exec", dont_inherit=True, optimize=0)
    return importlib._bootstrap_external._code_to_hash_pyc(
        code, importlib.util.source_hash(source), checked=False)


def add(zf, arcname, data):
    info = zipfile.ZipInfo(arcname, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    zf.writestr(info, data)


def main():
    lib, out, extra = sys.argv[1], sys.argv[2], sys.argv[3:]
    entries = {}
    for root, dirs, files in os.walk(lib):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for f in files:
            if f.endswith(".py"):
                path = os.path.join(root, f)
                rel = os.path.relpath(path, lib)
                entries[rel[:-3] + ".pyc"] = path
    for path in extra:
        entries[os.path.basename(path)[:-3] + ".pyc"] = path

    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w") as zf:
        for arcname in sorted(entries):
            with open(entries[arcname], "rb") as fh:
                source = fh.read()
            add(zf, arcname.replace(os.sep, "/"), pyc(source, arcname[:-1]))
    os.replace(tmp, out)
    print("%s: %d modules" % (out, len(entries)))


if __name__ == "__main__":
    main()
