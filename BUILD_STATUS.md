# Build status

Last updated: 2026-10-19

## Current milestone

| Milestone | State |
|---|---|
| 1 Toolchain | Code complete, builds. **HARDWARE UNVERIFIED** |
| 2 Input system | Code complete, host-tested. **HARDWARE UNVERIFIED** |
| 3 Python runtime | Static CPython 3.12.8 cross-builds; runtime `.nro` links and runs a self-test. **HARDWARE UNVERIFIED** |
| 4 Ren'Py engine | Ren'Py 8.5.3 builds and links into the runtime. Library → Launch is wired. The controller bridge passes 24/24 checks on desktop against the official engine. **HARDWARE UNVERIFIED** |
| 5 Importer | macOS importer: folder/ZIP/7z/RAR → compatibility report, `manifest.json`, SD export. Host-tested (16 tests). Its export of The Question launched on a Switch (run 9) |

**Ren'Py 8.5.3 runs on a real Switch** (2026-10-18, run 5): "The Question" starts, renders
with GLES 3.2 on nouveau, shows dialogue and quits cleanly. Audio, saves, suspend, touch
and the cursor are still unverified on hardware. See
[tests/hardware/results](tests/hardware/results/).

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
- Ren'Py 8.5.3 (official source, 60 native modules, engine `.pyc` and precompiled common
  scripts in RomFS). Game mode: `--game <folder>`. Per-game saves (`saves/<id>`), logs
  (`logs/<id>`) and controller profile; GLES2 renderer. See `runtime/renpy/README.md`.
- Controller bridge: logical actions → Ren'Py pad events or named events. The virtual
  cursor drives Ren'Py's mouse with a software cursor. Ren'Py's own SDL pad support is off.
- Static CPython 3.12.8 (5 small patches, documented in `runtime/python/README.md`) with 69
  built-in extension modules.
- Stdlib compiled to sourceless `.pyc` in RomFS.
- Python runs on an 8 MiB-stack thread while the main thread keeps servicing applet events.
- `_rslnx` module for memory and applet facts; `--return` chain-loads back to the loader.

**Importer (`importer/rsl_import.py`, macOS, standard library only)** — see `docs/IMPORTER.md`
- Inputs: a game folder, `.zip`, or `.7z`/`.rar` through the `7z` CLI.
- Safe extraction into a temp folder: refuses absolute paths, `..`, links pointing outside,
  passwords and decompression bombs (file-count, total-size and ratio limits).
- Detects the game folder (Ren'Py's `path_to_gamedir` rule), the Ren'Py version, Python 2
  or 3, custom Python, Windows native code, uncommon media and non-ASCII names.
- Writes `games/<id>/game` + versioned `manifest.json` (`experimental` or `unsupported`,
  never `verified`). Refuses to overwrite without `--replace`, and keeps saves.

**Other**
- Line-flushed logs: `logs/loader.log`, `logs/runtime.log`.

## Compilation results (actual)

| Item | Result |
|---|---|
| Host | macOS 15.7.9, Apple Silicon (arm64), Colima 0.10.3 (vz) + Docker CLI 29.9.1 |
| Base image | `devkitpro/devkita64:20260219` @ `sha256:1fc388c3…0282` (arm64 variant `645416b7…`) |
| Toolchain | devkitA64 r29.2, gcc 15.2.0, newlib 4.6.0.20260123, libnx 4.12.0, switch-tools 1.13.1 |
| Loader build | success, 0 warnings (`-Wall -Wextra`) |
| CPython | `Python-3.12.8.tar.xz` sha256 `c909157b…154e` (python.org MD5 and renpy-build copy match) |
| CPython build | `scripts/build.sh python`: configure + `libpython3.12.a` + 511-module stdlib zip, success |
| Runtime build | success, 0 warnings, static link with no unresolved symbols |
| Ren'Py build | `scripts/build.sh renpy`: 60 modules / 73 objects in `librenpy.a`; 169 engine modules staged; 63 SDK common scripts digest-verified |
| `dist/renpy-runtime.nro` | 55,302,753 bytes, sha256 `b1f1e678d72680e2434b5851ae148b78012d5c70a84892b0fa17eb032caf7f1e` |
| `dist/renpy-loader.nro` | sha256 `17e49406500f7d0cedde61080c351066377f964553518b603f798207ef734204` (Launch wired) |
| Reproducibility | loader: identical across two clean builds. Milestone 3 runtime: identical across two full rebuilds. Not re-checked for the Ren'Py runtime |
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
| `tests/unit/test_posixpath_horizon.py` | Linux arm64 container, host CPython 3.12.8 | patched posixpath | 22 checks | 22 pass | PASS (host only) |
| `tests/integration/renpy_desktop/run.sh` | Linux arm64 container, Xvfb + Mesa llvmpipe 22.3.6, GLES 3.2 | **official** Ren'Py 8.5.3 SDK + our `switch_main.py`/`rsl_bridge.py`, scripted `_rslnx` | 24 checks | 24 pass; screenshots in `build-test/out/` | PASS (desktop only) |
| `tests/unit/test_importer.py` (16 tests, part of `scripts/test.sh`) | macOS host, `/usr/bin/python3` 3.9.6 | n/a | all pass | 16/16 | PASS |

The desktop run found and fixed two bridge bugs before any hardware test:
- the launcher module was named `renpy_*`, so Ren'Py tried to pickle it at startup;
- bridge event names were missing from `config.keymap`, so Ren'Py raised
  "Invalid key specifier" on mouse events.

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

**First console run, 2026-10-18** ([tests/hardware/results/2026-10-18-run1.md](tests/hardware/results/2026-10-18-run1.md)):
- PASS: loader launch, folders, controller detection, runtime found, chain-load both
  ways, CPython 3.12.8 initialises on the console.
- FAIL: the self-test stalled with no visible output, and Ren'Py exited with status 1
  immediately. Both errors were invisible because CPython dropped stdout/stderr on the
  libnx console; that is fixed for run 2.

**Runs 2–3** ([run 2](tests/hardware/results/2026-10-18-run2.md),
[run 3](tests/hardware/results/2026-10-18-run3.md)):
- PASS: all 66 required stdlib imports on the console.
- Causes found:
  - `select.poll()` hangs on Horizon;
  - importlib's own `isabs` breaks `romfs:/` paths. This is what stopped Ren'Py.

  Both are fixed in the runtime for run 4 (pushed over Wi-Fi FTP).

**Run 9, 2026-10-19** ([tests/hardware/results/2026-10-19-run9.md](tests/hardware/results/2026-10-19-run9.md)):
- PASS: the importer's export of The Question (`games/the-question`), uploaded over FTP
  (173/173 files read back identical), launches from the library and quits with status 0.
- FAIL: 21 s after returning, the loader's main menu crashes ("The software was closed
  because an error occurred").
  - Cause: SDL's timer thread from the runtime outlives it. `SDL_Quit()` does not stop a
    timer thread that `SDL_AddTimer` started without `SDL_INIT_TIMER`.
  - Fix in runtime `fed4dc1b…`: `quit_sdl()` takes a timer reference so SDL joins the thread.
    It is on the console, not retested yet.

Everything else below remains unverified.

**HARDWARE UNVERIFIED.** Neither `.nro` has run on a Switch or an emulator. Untested on device:
- launch, rendering, button and stick reading, touch;
- SD writes, sleep/resume, controller reconnect, docked mode;
- chain-loading;
- Python start-up, threads, `os.urandom`, filesystem, memory limits;
- Ren'Py start-up, GLES on nouveau, audio, video, saves, suspend during play.

Checklist: [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md), H1–H35.

## Known bugs and limitations

- Loader UI is a text console. The graphical library is Milestone 7.
- The in-game loader overlay is not built; L-stick press only shows a notice.
- Live2D and 3D models are unsupported. FFmpeg is linked statically, so LGPL relinking
  materials are still to be produced before distributing builds.
- The runtime `.nro` is 55 MB; no memory or load-time measurement on hardware yet.
- The emergency combo needs both + and −, so a single detached Joy-Con cannot do it.
- The launcher UI state machine has no automated test (it needs libnx); the engine under it is tested.
- The runtime self-test writes console text from a second thread. The libnx console is not
  thread-safe, so text may tear (cosmetic).
- Python is missing `ctypes` (no libffi), `ssl`/`_hashlib` (no OpenSSL), `sqlite3`, `mmap`,
  subprocess spawning and `multiprocessing`. Games that need these will fail. See
  `runtime/python/README.md`.
- Chain-loading needs the loader to be started from the Homebrew Menu.
- The project license is not chosen yet.
- Crash after returning from a game (run 9): SDL's timer thread from the runtime keeps
  running after the runtime's code is unloaded, then crashes the loader. Fixed in runtime
  `fed4dc1b…`, confirmed on the console (run 10).
- Exit to the Homebrew Menu after a game crashes sphaira (run 10).
  - [Likely] cause: libnx has no `pthread_detach`, so the stacks of CPython's exited threads
    stayed mapped.
  - Fix in runtime `35508472…` (on the console), not retested yet.
  - Workaround: close the software from HOME.

## Next engineering tasks

1. Run H1–H35 on a console. H22 (Python starts) and H26 (Ren'Py starts, GLES on nouveau)
   decide what comes next. Send back `logs/runtime.log` and `logs/the_question/log.txt`.
2. Run 11 with runtime `35508472…` (detached-thread clean-up):
   - play a game, quit, and wait on the loader for a minute;
   - relaunch a game (the run 7 `malloc` crash);
   - exit to the Homebrew Menu.
3. Milestone 6, after hardware feedback: save/load and suspend reliability, the in-game
   overlay, fixes for whatever H26–H35 expose.
