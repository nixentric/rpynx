# CPython 3.12.8 for the Nintendo Switch

Static CPython for `aarch64-none-elf` (devkitA64 newlib + libnx), embedded by
`runtime/main.c` into `dist/renpy-runtime.nro`. 3.12.8 is the version renpy-build
uses for Ren'Py 8.5.3.

```bash
scripts/build.sh python
```

```bash
scripts/build.sh runtime
```

`build.sh` (run inside the container) does the following:

1. Builds a host CPython 3.12.8. A cross build needs a build interpreter of the same version.
   It is cached in `build-python/host`.
2. Extracts a pristine source tree and applies `patches/*.diff`.
3. Configures with `config.site`, `MODULE_BUILDTYPE=static`, `--disable-shared`, and the
   devkitPro portlib environment (`switchvars.sh`).
4. Builds `libpython3.12.a` and merges CPython's helper archives into it (libmpdec, expat,
   HACL SHA-2).
5. Compiles the stdlib with the host interpreter into sourceless, hash-based `.pyc` files
   inside a deterministic `python312.zip` (`pack_stdlib.py`).

Output goes to `build-python/dist/{include/python3.12, lib/libpython3.12.a, lib/python312.zip}`.

Source: python.org `Python-3.12.8.tar.xz`, sha256
`c909157bb25ec114e5869124cc2a9c4a4d4c1e957ca4ff553f1edc692101154e`. That matches
python.org's published MD5 `d46e5bf9f2e596a3ba45fc0b3c053dd2` and renpy-build's copy.

## Upstream modifications

| Patch | Why |
|---|---|
| `0001-configure-horizon-host.diff` | Accept host `aarch64-*-elf` as system `Horizon`, so `sys.platform == "horizon"` |
| `0002-posixmodule-statvfs-without-fstatvfs.diff` | `os.statvfs` used a helper compiled only when `fstatvfs` exists; newlib has `statvfs` only. Keeps free-space queries |
| `0003-timemodule-newlib-timezone.diff` | newlib declares `_timezone`/`_daylight`, not `timezone`/`daylight` |
| `0004-subprocess-horizon-no-fork.diff` | Horizon cannot spawn processes; `subprocess` imports, and `Popen` raises instead of failing to import |
| `0005-posixpath-horizon-device-paths.diff` | `os.path`: `sdmc:/…` and `romfs:/…` are absolute (`isabs`, `join`, `realpath`) |
| `0006-importlib-horizon-device-paths.diff` | importlib's private `_path_isabs`, used by `spec_from_file_location` and `FileFinder`, gets the same rule. Found on hardware (run 3) |

Configuration-only changes:

- **`config.site` answers:**
  - no `dlopen`;
  - `getentropy` provided (see below);
  - no `sys/resource.h` (newlib's copy has no rlimit API);
  - `chroot`, `fchdir`, `fdatasync` and `setgroups` are declared by newlib but not
    implemented by libnx (configure's probes only compile, so they cannot tell);
  - `_posixsubprocess` is disabled;
  - no `poll`: it never returns on Horizon without the socket service, and `selectors`
    probes it at import (found on hardware, run 3).
- **`build.sh`:** appends `#define SSIZE_MAX __LONG_MAX__` to `pyconfig.h`, because newlib
  does not define `SSIZE_MAX`.
- **Runtime shim (`nx_shims.c`):** newlib's `getentropy()` calls `_getentropy_r`, which libnx
  leaves undefined. The shim implements it with libnx `randomGet()` (CSPRNG). Without it,
  `Py_Initialize` cannot seed hash randomisation and `os.urandom` fails.

Prior art consulted, not copied: PyNX (ISC) and SnepOMatic/RenPy-Switch (no license) used
similar config.site answers for 3.13.

## Native modules

Built in (from `Modules/config.c`): `array _asyncio _bisect _contextvars _csv _heapq _json
_lsprof _opcode _pickle _queue _random _struct _zoneinfo audioop math cmath _statistics
_datetime _decimal binascii _bz2 _lzma zlib _md5 _sha1 _sha2 _sha3 _blake2 pyexpat
_elementtree _codecs_{cn,hk,iso2022,jp,kr,tw} _multibytecodec unicodedata fcntl select
_socket _multiprocessing atexit faulthandler posix _signal _tracemalloc _codecs _collections
errno _io itertools _sre _thread time _typing _weakref _abc _functools _locale _operator
_stat _symtable _imp _ast _tokenize gc _string`, plus `_rslnx` from the runtime
(`memory()`, `applet_type()`).

Not available, and what it means for games:

| Missing | Reason | Effect |
|---|---|---|
| `_ctypes` | no libffi port for libnx | games or libraries using `ctypes` fail |
| `_ssl`, `_hashlib` | no OpenSSL in devkitPro | no HTTPS; `hashlib` uses the built-in md5/sha1/sha2/sha3/blake2 |
| `_sqlite3` | not packaged | `sqlite3` unavailable |
| `mmap` | Horizon has no `mmap` | `mmap` unavailable |
| `_posixsubprocess` | no processes | `subprocess.Popen` raises |
| `resource`, `grp`, `pwd`, `termios`, `readline`, `_curses` | no OS support | unavailable |
| `_multiprocessing` | built, but there is no `fork` | `multiprocessing` unusable |

## Verified

- [Certain] Cross build and static link of `renpy-runtime.nro` succeed; there are no
  unresolved symbols.
- [Certain] With only `python312.zip` on the path, `selftest.py` passes on the identical
  host CPython 3.12.8 (Linux arm64). That validates the zip and the test, not Horizon.
- **HARDWARE UNVERIFIED:**
  - interpreter start-up on the console;
  - threads, the filesystem on `sdmc:`, `os.urandom`, time;
  - memory in applet and application mode.

  See `docs/HARDWARE_TESTS.md` H21–H25.
