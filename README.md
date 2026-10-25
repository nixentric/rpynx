<p align="center">
  <img src="app/launcher/icon.jpg" width="160" alt="rpynx">
</p>

<h1 align="center">rpynx</h1>

<p align="center">
  <b>Play Ren'Py games on Switch</b><br>
  <sub>Unofficial homebrew. Not affiliated with Ren'Py or Nintendo.</sub>
</p>

A Nintendo Switch homebrew loader (`.nro`) meant to run legally obtained, DRM-free Ren'Py
games from the SD card, with a first-class remappable controller system.

**Status: a preview.** Ren'Py 8.5.3 games run on a real Switch: they render with GLES 3.2,
play audio, save and load, and survive sleep (hardware runs 5–27). The graphical library with
covers (run 28) and sending games over Wi-Fi from the importer (run 29) are tested on a Switch
too. Docked play and controller reconnect are not. See [BUILD_STATUS.md](BUILD_STATUS.md) for
what is built, tested and hardware-verified.

## Download

From the [latest release](https://github.com/nixentric/rpynx/releases/latest); no need to build
anything yourself.

| File | What it is |
|---|---|
| [renpy-loader.nro](https://github.com/nixentric/rpynx/releases/latest/download/renpy-loader.nro) | The loader: copy it to `switch/renpy-loader/` on the SD card |
| [renpy-runtime.nro](https://github.com/nixentric/rpynx/releases/latest/download/renpy-runtime.nro) | Python and Ren'Py: copy it to `switch/renpy-loader/runtimes/` |
| [rpynx-importer-windows.exe](https://github.com/nixentric/rpynx/releases/latest/download/rpynx-importer-windows.exe) | The importer for Windows |
| [rpynx-importer-macos-arm64.dmg](https://github.com/nixentric/rpynx/releases/latest/download/rpynx-importer-macos-arm64.dmg) | The importer for Macs with Apple silicon: drag it to Applications |
| [rpynx-importer-python.zip](https://github.com/nixentric/rpynx/releases/latest/download/rpynx-importer-python.zip) | The importer as Python, for Linux and Intel Macs: `python3 rsl_import.py` |

Every release has every file; parts that did not change are the previous release's files, as
its notes say. `SHA256SUMS.txt` in each release has the checksums. The importer apps are not
signed: macOS asks to confirm (right-click, Open), Windows shows SmartScreen (More info, Run
anyway). How to install and import a game: [rpynx.nixentric.com](https://rpynx.nixentric.com/getting-started/install/).

Releases are made by [.github/workflows/release.yml](.github/workflows/release.yml) when a tag
`v<APP_VERSION>` is pushed.

## Build it yourself (macOS Apple Silicon, Linux, or Windows with WSL 2)

Requires Docker. On macOS without Docker Desktop:

```bash
brew install colima docker
```

```bash
colima start --cpu 4 --memory 4 --vm-type vz
```

On Windows, build inside WSL 2 with Ubuntu: the scripts are POSIX shell and want a Linux file
system. Not tried yet on Windows; the same steps work on Linux.

```powershell
wsl --install -d Ubuntu
```

In the Ubuntu shell, Docker comes either from Docker Desktop for Windows (Settings → Resources
→ WSL integration: on for Ubuntu) or from Ubuntu itself:

```bash
sudo apt install docker.io git && sudo usermod -aG docker $USER
```

Clone the repository into the Linux home (`~/rpynx`), not under `/mnt/c/`: files there are slow
to build from and lose their executable bits.

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

Host unit tests (Apple clang or gcc, no Docker; the screen mockups in it need macOS's `sips`):

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
LVGL 9.6.0 (MIT) is not stored here: `third_party/lvgl/fetch.sh` downloads it, checksummed.

The built `.nro` files also contain third-party code under its own licenses, which MIT does
not replace:
- `renpy-loader.nro`: libnx (ISC), cJSON (MIT), LVGL (MIT) with its bundled stb_truetype
  (MIT or public domain), TJpgDec (ChaN's free licence) and the Montserrat font (SIL OFL 1.1).
  The text font and button icons are the console's own, read from its shared memory at run
  time; nothing of them is in the `.nro`.
- `renpy-runtime.nro`: CPython (PSF), Ren'Py (MIT, with LGPL-derived parts), the Ren'Py SDK's
  fonts (Source Han Sans Lite and Vazirmatn, SIL OFL 1.1),
  SDL2 (zlib), Mesa (MIT), libnx (ISC) and others.
  - It also statically links devkitPro's switch-ffmpeg, which is **built with `--enable-gpl`**.
  - So the runtime binary as a whole is covered by the **GPL (version 2 or later)**.
  - Anyone who distributes it must provide the complete corresponding source, or a written
    offer for it. Personal use is not affected. Each release of this repository attaches
    FFmpeg's source (`ffmpeg-7.1.tar.xz`, devkitPro's switch-ffmpeg 7.1) next to the runtime;
    the rest is this repository at the release's tag, built in the pinned devkitPro image.
  - To make it LGPL instead, build FFmpeg without `--enable-gpl`. LGPL relinking materials
    would still be needed, because it is linked statically.
