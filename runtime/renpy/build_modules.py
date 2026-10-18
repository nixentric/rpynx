"""Compile Ren'Py's native modules for the Switch into one static library.

Reads the module list from Ren'Py's own src/Setup, compiles every source once,
renames each Cython init function to a unique name, and generates the
inittab that registers the modules under their dotted names. Same scheme as
renpy-build's librenpy task (MIT), reimplemented without its framework.

usage: build_modules.py <renpy dir> <gen dir> <out dir>
env:   CC, AR, CFLAGS (complete compile flags), JOBS
"""
import concurrent.futures
import os
import shlex
import subprocess
import sys

# Modules that cannot exist on the Switch, and why. Each has a Python-level
# replacement or is optional in Ren'Py (see runtime/compatibility).
EXCLUDE = {
    "renpy.gl2.assimp": "assimp (C++ 3D importer) is not available for the Switch",
    "renpy.gl2.live2dmodel": "Live2D Cubism Core is proprietary and not redistributable",
}


def read_setup(path):
    modules = []
    for line in open(path):
        line = line.partition("#")[0].strip()
        if line:
            name, *sources = line.split()
            modules.append((name, sources))
    return modules


def main():
    renpy, gen, out = (os.path.abspath(p) for p in sys.argv[1:4])
    cc, ar = os.environ["CC"], os.environ["AR"]
    cflags = shlex.split(os.environ["CFLAGS"]) + [
        "-std=gnu99", "-Wno-unused-function",
        "-I" + os.path.join(renpy, "src"), "-I" + gen,
    ]
    os.makedirs(out, exist_ok=True)

    modules, jobs, seen = [], [], set()
    for name, sources in read_setup(os.path.join(renpy, "src", "Setup")):
        if name in EXCLUDE:
            print("skip %-32s %s" % (name, EXCLUDE[name]))
            continue
        modules.append(name)
        short, mangled = name.rpartition(".")[2], name.replace(".", "_")
        for src in sources:
            if "tmp/gen/" in src:  # generated module source lives in our gen dir
                src = os.path.join(gen, name + ".c")
            else:
                src = os.path.join(renpy, "src", src)
            obj = os.path.join(out, os.path.basename(src)[:-2] + ".o")
            if obj in seen:  # shared helper (e.g. ftsupport.c) compiled once
                continue
            seen.add(obj)
            jobs.append([cc, *cflags, "-DPyInit_%s=PyInit_%s" % (short, mangled), "-c", src, "-o", obj])

    inittab = os.path.join(out, "librenpy_inittab.c")
    with open(inittab, "w") as f:
        f.write('#include "Python.h"\n\n')
        for m in modules:
            f.write("PyMODINIT_FUNC PyInit_%s(void);\n" % m.replace(".", "_"))
        f.write("\nstatic struct _inittab inittab[] = {\n")
        for m in modules:
            f.write('    { "%s", PyInit_%s },\n' % (m, m.replace(".", "_")))
        f.write("    { NULL, NULL },\n};\n\n")
        f.write("/* Call before Py_Initialize. Returns -1 on failure. */\n")
        f.write("int rsl_init_librenpy(void)\n{\n    return PyImport_ExtendInittab(inittab);\n}\n")
    jobs.append([cc, *cflags, "-c", inittab, "-o", inittab[:-2] + ".o"])

    failed = []
    with concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", "4"))) as ex:
        for cmd, res in zip(jobs, ex.map(lambda c: subprocess.run(c, capture_output=True, text=True), jobs)):
            if res.returncode:
                failed.append(cmd[-3])
                sys.stderr.write("FAILED %s\n%s\n" % (cmd[-3], res.stderr[-4000:]))
            elif res.stderr.strip():
                sys.stderr.write("warnings in %s:\n%s\n" % (cmd[-3], res.stderr[-1500:]))
    if failed:
        sys.exit("%d of %d compile jobs failed" % (len(failed), len(jobs)))

    lib = os.path.join(out, "librenpy.a")
    if os.path.exists(lib):
        os.remove(lib)
    objs = sorted(cmd[-1] for cmd in jobs)
    subprocess.run([ar, "rcsD", lib, *objs], check=True)
    print("%s: %d modules, %d objects" % (lib, len(modules), len(objs)))


if __name__ == "__main__":
    main()
