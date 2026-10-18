# Build status

Last updated: 2026-10-18

## Current milestone

| Milestone | State |
|---|---|
| 1 Toolchain | Code complete, builds. **HARDWARE UNVERIFIED** |
| 2 Input system | Code complete, host-tested. **HARDWARE UNVERIFIED** |
| 3 Python runtime | Static CPython 3.12.8 cross-builds; runtime `.nro` links and runs a self-test. **HARDWARE UNVERIFIED** |
| 4 Ren'Py engine | Not started |

**No Ren'Py game can be launched yet.**

## Implemented

**Toolchain and build**
- Pinned, containerised Switch build (`scripts/build.sh`, `docker/Dockerfile`), native arm64
  on Apple Silicon.

**Loader (`renpy-loader.nro`)**
- Text-console UI: main menu, game folder listing, controller settings, controller test,
  diagnostics (applet type, docked/handheld, memory, runtime presence, log tail).
- Diagnostics → A chain-loads the runtime's Python self-test, which returns to the loader afterwards.

**Input pipeline** (libnx HID → Nintendo-label normaliser → mapping engine → logical actions)
- 16 logical actions with press, hold and repeat behaviour.
- D-pad and analog navigation, plus a virtual cursor with deadzone, speed and acceleration.
- Touch readout, letterbox coordinate mapping.
- Presets: default, left-handed mirror, one-handed right.
- Remapping UI: button capture, conflict marking, reset, presets, global and per-game
  profiles, live apply on save.
- Versioned JSON profiles with strict validation. Saves are atomic (`.tmp` → `.bak` swap),
  with `.bak` fallback on load and refusal to save conflicting or incomplete maps.
- Emergency recovery combo (hold + and − for 3 s) that no mapping can disable.
- Input reset on focus change, resume and controller connect/disconnect/style change.

**Runtime (`renpy-runtime.nro`)**
- Static CPython 3.12.8 (4 small patches, documented in `runtime/python/README.md`) with 69
  built-in extension modules.
- Stdlib compiled to sourceless `.pyc` in RomFS.
- Python runs on an 8 MiB-stack thread while the main thread keeps servicing applet events.
- `_rslnx` module for memory and applet facts; `--return` chain-loads back to the loader.

**Other**
- Line-flushed logs: `logs/loader.log`, `logs/runtime.log`.

## Compilation results (actual)

| Item | Result |
|---|---|
| Host | macOS 15.7.9, Apple Silicon (arm64), Colima 0.10.3 (vz) + Docker CLI 29.9.1 |
| Base image | `devkitpro/devkita64:20260219` @ `sha256:1fc388c3…0282` (arm64 variant `645416b7…`) |
| Toolchain | devkitA64 r29.2, gcc 15.2.0, newlib 4.6.0.20260123, libnx 4.12.0, switch-tools 1.13.1 |
| Loader build | success, 0 warnings (`-Wall -Wextra`) |
| `dist/renpy-loader.nro` | 354,501 bytes, sha256 `28f853d2d53292565080b430c77aa0ee7d7bd2ec7e229d387a09eab722ed373e` |
| CPython | `Python-3.12.8.tar.xz` sha256 `c909157b…154e` (python.org MD5 and renpy-build copy match) |
| CPython build | `scripts/build.sh python`: configure + `libpython3.12.a` + 511-module stdlib zip, success |
| Runtime build | success, 0 warnings, static link with no unresolved symbols |
| `dist/renpy-runtime.nro` | 13,096,745 bytes, sha256 `010a42dc209a0aacdfc83f11575f5b1d9bff9f05319d2aa609f08094d08b9fd7` |
| Reproducibility | loader: identical across two clean builds. Runtime `.nro` and stdlib zip: identical across two full CPython + runtime rebuilds |
| libnx layout check | `_Static_assert`s in `input_switch.c` pass |

Other packages in the image relevant to Milestone 4: switch-sdl2 2.28.5, switch-sdl2_image
2.0.4, switch-sdl2_ttf 2.22.0, switch-sdl2_mixer 2.0.4, switch-mesa 20.1.0, switch-ffmpeg 7.1,
switch-freetype 2.13.3, switch-harfbuzz 10.0.1, switch-libfribidi 1.0.12, switch-libpng 1.6.48,
switch-libjpeg-turbo 2.1.2, switch-libwebp 1.0.2, switch-libavif 1.0.1, switch-zlib 1.3.1,
switch-bzip2 1.0.8, switch-liblzma 5.8.2.

## Test results (actual)

| Test | Environment | Runtime | Expected | Actual | Status |
|---|---|---|---|---|---|
| `scripts/test.sh` (151 checks) | macOS host, Apple clang, ASan + UBSan | n/a | all pass | 151/151 | PASS |
| `runtime/python/selftest.py` against `python312.zip` only | Linux arm64 container, host CPython 3.12.8 | CPython 3.12.8 | 0 required failures | 0 failures (66 required checks) | PASS (host only) |

The host input suite covers:
- presets, and Nintendo A/B orientation;
- the npad normaliser;
- bind/conflict/completeness;
- press/hold/repeat timing incl. stalls;
- nav stick deadzone; cursor movement and acceleration;
- emergency combo; reset after resume;
- stick and letterbox maths; sanitising;
- JSON round trip plus 11 hostile inputs; shipped profiles;
- atomic save with torn-write and missing-file fallback.

The self-test run proves the stdlib zip, zipimport of sourceless `.pyc` files, and the test
itself. It does not prove anything about Horizon.

## Hardware verification

**HARDWARE UNVERIFIED.** Neither `.nro` has run on a Switch or an emulator. Untested on device:
- launch, rendering, button and stick reading, touch;
- SD writes, sleep/resume, controller reconnect, docked mode;
- chain-loading;
- Python start-up, threads, `os.urandom`, filesystem, memory limits.

Checklist: [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md), H1–H25.

## Known bugs and limitations

- Loader UI is a text console. The graphical library is Milestone 7.
- The `overlay` action has no effect yet; it needs the Ren'Py runtime.
- The emergency combo needs both + and −, so a single detached Joy-Con cannot do it.
- The launcher UI state machine has no automated test (it needs libnx); the engine under it is tested.
- The runtime self-test writes console text from a second thread. The libnx console is not
  thread-safe, so text may tear (cosmetic).
- Python is missing `ctypes` (no libffi), `ssl`/`_hashlib` (no OpenSSL), `sqlite3`, `mmap`,
  subprocess spawning and `multiprocessing`. Games that need these will fail. See
  `runtime/python/README.md`.
- Chain-loading needs the loader to be started from the Homebrew Menu.
- The project license is not chosen yet.

## Next engineering tasks

1. Run H1–H25 on a console. H22/H23 matter most: they show whether CPython starts on Horizon
   and how much memory each launch mode gives.
2. Milestone 4:
   - fetch Ren'Py 8.5.3 (MIT) and build its Cython/C modules statically (renpy-build's
     static/inittab approach for iOS and web is the upstream reference);
   - link SDL2, mesa GLES, freetype, harfbuzz, fribidi and ffmpeg from portlibs;
   - bridge logical actions into Ren'Py events with SDL's controller handling off;
   - run the Ren'Py tutorial or an MIT-licensed demo game.
