# Ren'Py Switch Loader

A Nintendo Switch homebrew loader (`.nro`) meant to run legally obtained, DRM-free Ren'Py
games from the SD card, with a first-class remappable controller system.

**Status: Ren'Py 8.5.3 runs on a real Switch.** "The Question" starts, renders with GLES 3.2
and quits cleanly (hardware run 5), and games exported by the macOS importer launch from the
loader's library (run 9). Audio plays (run 21). Saves, suspend and touch are still unverified on hardware.
See [BUILD_STATUS.md](BUILD_STATUS.md) for what is built, tested and hardware-verified.

## Build (macOS Apple Silicon or Linux)

Requires Docker. On macOS without Docker Desktop:

```bash
brew install colima docker
```

```bash
colima start --cpu 4 --memory 4 --vm-type vz
```

Then:

```bash
scripts/build.sh
```

Output: `dist/renpy-loader.nro`. The runtime (static CPython; first build ~10 min):

```bash
scripts/build.sh python
```

```bash
scripts/build.sh runtime
```

Add the Ren'Py engine (between `python` and `runtime`; ~5 min first time):

```bash
scripts/build.sh renpy
```

Output: `dist/renpy-runtime.nro`. Desktop integration test (official Ren'Py SDK + our bridge):

```bash
tests/integration/renpy_desktop/run.sh
```

The toolchain is the pinned `devkitpro/devkita64:20260219`
image (digest in `scripts/build.sh`). No Nintendo SDK files are used or needed.

Host unit tests (Apple clang or gcc, no Docker):

```bash
scripts/test.sh
```

## Documentation

- [docs/INSTALL.md](docs/INSTALL.md): installing on the Switch, SD card layout, controls
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): layers, design decisions, pinned versions, runtime plan
- [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md): real-console test checklist
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md): compatibility matrix

## Legal

This repository contains no game assets and no Nintendo SDK material, and it does not
circumvent DRM. Use only games you own in DRM-free form.

The project's own source code is under the MIT License ([LICENSE](LICENSE)).

Third-party code in this repository: cJSON 1.7.18 (MIT, `third_party/cjson/LICENSE`).

The built `.nro` files also contain third-party code under its own licenses, which MIT does
not replace:
- `renpy-loader.nro`: libnx (ISC) and cJSON (MIT).
- `renpy-runtime.nro`: CPython (PSF), Ren'Py (MIT, with LGPL-derived parts),
  SDL2 (zlib), Mesa (MIT), libnx (ISC) and others.
  - It also statically links devkitPro's switch-ffmpeg, which is **built with `--enable-gpl`**.
  - So the runtime binary as a whole is covered by the **GPL (version 2 or later)**.
  - Anyone who distributes it must provide the complete corresponding source, or a written
    offer for it. Personal use is not affected.
  - To make it LGPL instead, build FFmpeg without `--enable-gpl`. LGPL relinking materials
    would still be needed, because it is linked statically.
