# Ren'Py Switch Loader

A Nintendo Switch homebrew loader (`.nro`) meant to run legally obtained, DRM-free Ren'Py
games from the SD card, with a first-class remappable controller system.

**Status: Milestones 1–2 (toolchain, input system). It cannot run games yet.** The embedded
Python runtime and Ren'Py engine are Milestones 3–4. See [BUILD_STATUS.md](BUILD_STATUS.md)
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

Output: `dist/renpy-loader.nro`. The toolchain is the pinned `devkitpro/devkita64:20260219`
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
