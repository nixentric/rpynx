# Ren'Py Switch Loader

A Nintendo Switch homebrew loader (`.nro`) meant to run legally obtained, DRM-free Ren'Py
games from the SD card, with a first-class remappable controller system.

**Status: Milestones 1–3 done; Milestone 4 (Ren'Py 8.5.3 engine) builds and passes a desktop
integration test. Nothing has run on a Switch yet.** See [BUILD_STATUS.md](BUILD_STATUS.md)
for what is built, tested and hardware-verified (currently: nothing hardware-verified).

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

Third-party code: cJSON 1.7.18 (MIT, `third_party/cjson/LICENSE`). The project's own
license has not been chosen yet.
