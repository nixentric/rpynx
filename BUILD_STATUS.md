# Build status

Last updated: 2026-10-21

## Current milestone

| Milestone | State |
|---|---|
| 1 Toolchain | Code complete, builds. On a Switch: the loader and runtime `.nro` start and chain-load each other both ways (run 1) |
| 2 Input system | Code complete, host-tested. On a Switch: handheld controls drive the loader's menus (run 1); in games the D-pad, A and the virtual cursor pick buttons (runs 16, 18). Remapping, profiles, the emergency combo, touch, docked play and controller reconnect: **HARDWARE UNVERIFIED** |
| 3 Python runtime | Static CPython 3.12.8 cross-builds; runtime `.nro` links. On a Switch the self-test finishes in 2 s with 66/66 required imports, threads and `os.urandom`; one check fails: non-ASCII file names on `sdmc:` (runs 4, 20). The SD side parses names as Shift-JIS, below libnx: a name is created only if its UTF-8 bytes are also valid Shift-JIS, and listings leave out every non-ASCII entry ([run 20](tests/hardware/results/2026-10-20-run20.md)) |
| 4 Ren'Py engine | Ren'Py 8.5.3 builds, links and runs on a Switch: The Question (run 5), and Forbidden Fantasy, a Ren'Py 7 game recompiled on the console (run 13). Quit returns to the loader (run 7); three games in a row run in one process (run 12). The controller bridge passes 26/26 desktop checks against the official engine. Music and sound play, also from an `.rpa` ([run 21](tests/hardware/results/2026-10-21-run21.md)). Saves, suspend and touch: see Milestone 6. Applet mode crashed the whole console (runs 8 and 26), so games are refused there (loader and runtime) |
| 5 Importer | macOS importer: folder/ZIP/7z/RAR → compatibility report, `manifest.json`, SD export. Host-tested (25 tests). Its export of The Question launched on a Switch (run 9) |
| 6 Gameplay reliability | **Done** (run 27). Verified on a Switch: quitting and starting another game no longer crashes (a stuck Ren'Py audio decode thread, runs 22–24); a game error shows the system error dialog and returns to the library (run 25); sleep and resume (runs 23–24); website links and folder buttons show a notice instead of failing (run 25); the D-pad reaches buttons whose boxes overlap (run 25); The Question's preferences with the SDK fonts (run 25). Desktop-tested only: game menus start on their side menu; no cursor after touch. Also verified (run 26): a remap used in a game (`input: confirm (X)`), save/load back to the same line (`script.rpy:72`), a custom screen to Done, HOME. The in-game emergency combo saves the defaults for the loader too, and games are refused in applet mode, the loader started from the Album, which crashed the whole console in runs 8 and 26 ([run 27](tests/hardware/results/2026-10-21-run27.md)). Still desktop-only: no cursor after touch; the full refusal text (status buffer fixed after run 27) |
| 7 Game library | The LVGL loader ran on a Switch (first hardware test, 2026-10-22): first frame 80 ms after start, cores at 1 % when idle, SoC 36-38 °C, frames 11-17 ms. The user's review led to a second round (fonts measured so nothing is cut off, centred icons, bigger covers, a controller-shaped test screen, layout and accent pages, an illustrated How to play), built with 0 warnings (`.nro` 731 KB) and pushed for the second hardware test. Covers wait for the importer |
| Transfer (planned: after 7, before 8) | Built-in FTP server in the loader (the desktop importer's FTP client exists already), then USB transfer (libnx `usbComms` on the Switch, libusb on the desktop; tens of MB/s against ~1–2 MB/s over Wi-Fi). MTP is not planned: macOS has no MTP support, and an MTP responder such as haze is GPL, which would make the MIT loader GPL |
| 8 Release validation | Not started |

**Ren'Py 8.5.3 runs on a real Switch** (2026-10-18, run 5): "The Question" starts, renders
with GLES 3.2 on nouveau, shows dialogue and quits cleanly. Audio plays (run 21). Saves, suspend and touch
are still unverified on hardware. See
[tests/hardware/results](tests/hardware/results/).

## Implemented

**Toolchain and build**
- Pinned, containerised Switch build (`scripts/build.sh`, `docker/Dockerfile`), native arm64
  on Apple Silicon.

**Loader (`renpy-loader.nro`)**
- Graphical UI with LVGL 9.6 (`app/ui/ui.c`, `docs/LOADER_UI.md`), drawn on the CPU into the
  libnx framebuffer, only when something changed:
  - library in three layouts (Row, Grid, Cinematic), game details, + menu, applet-mode notice,
    controller settings, controller test, diagnostics, how to play (shown on the first start);
  - OLED dark theme, pink or blue accent; the console's own font and button icons;
  - settings in `config/loader.json`;
  - `ui` lines in `loader.log`: start time, a summary every 60 s (frames, render time, core
    load, SoC temperature, heap, covers), rate-limited warnings, an exit line.
- Diagnostics → A chain-loads the runtime's Python self-test, which returns to the loader afterwards.
- Game Library reads each game's `manifest.json`:
  - it shows the Ren'Py version, status, runtime, and whether that runtime is installed;
  - Launch starts that runtime;
  - it refuses `unsupported` games, and games whose runtime is missing.

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
- Static CPython 3.12.8 (6 small patches, documented in `runtime/python/README.md`) with 69
  built-in extension modules.
- Stdlib compiled to sourceless `.pyc` in RomFS.
- Python runs on an 8 MiB-stack thread while the main thread keeps servicing applet events.
- `_rslnx` module for memory and applet facts; `--return` chain-loads back to the loader.
- Ren'Py 7 compat (`rsl_bridge`): for games with `script_version` ≤ 7.5.3/8.0.3, a language
  change does not re-run `define gui.*`. Ren'Py 8.5.3 does, which erased gui values the game
  set in `init python`.
  - Test: `tests/integration/renpy_desktop/compat.sh`.
- 30 fps power saver, global (`config/power-saver`) or per game (a `power-saver` file in
  the game's folder).
- Switch keyboard: the full system keyboard (`_rslnx.keyboard`, libnx `swkbdShow`). It opens
  once per text field, with its own text box and the field's current text.
  - OK puts the text into the field; nothing moves on by itself.
  - Y, or A with nothing selected, reopens it.
  - A presses Enter only on a screen with no buttons at all (A cannot submit a Ren'Py
    input).
  - SDL's inline keyboard is no longer used.
- B goes back (rollback) in the game; in menus it still closes the menu.
- Button selection (`rsl_bridge`): when a new screen with buttons appears and nothing (or
  only a pause) is focused, its first button is selected. Examples: New Game, the first
  choice, the first card.
  - It considers only buttons of the content screen (never `config.overlay_screens`, such as
    the quick menu).
  - It skips dialogue and active text fields.
  - It applies to every game.
- The virtual cursor stays hidden until the stick moves, and hides again on button presses.
  Its image (`runtime/renpy/make_cursor.py`) is a black arrow with a white outline and
  rounded, anti-aliased corners, in the style of Bibata Modern Classic but drawn from
  scratch (Bibata is GPL-3.0). Hotspot at the black tip (4, 3).
- `tests/integration/renpy_desktop/compat.sh`: 14 checks on synthetic games, with plain-8.5.3
  repros and an emulated Switch keyboard. The Question desktop scenario: 26 checks (B in game
  added).

**Importer (`importer/rsl_import.py`, macOS, standard library only)** — see `docs/IMPORTER.md`
- Inputs: a game folder, `.zip`, or `.7z`/`.rar` through the `7z` CLI.
- Safe extraction into a temp folder: refuses absolute paths, `..`, links pointing outside,
  passwords and decompression bombs (file-count, total-size and ratio limits).
- Detects the game folder (Ren'Py's `path_to_gamedir` rule), the Ren'Py version, Python 2
  or 3, custom Python, Windows native code, uncommon media and non-ASCII names.
- Writes `games/<id>/game` + versioned `manifest.json` (`experimental` or `unsupported`,
  never `verified`). Refuses to overwrite without `--replace`, and keeps saves.
- `--recompile` (experiment): a Ren'Py 7 game with `.rpy` sources is exported without
  `.rpyc`, for Ren'Py 8.5.3 to compile with Python 3.
- Switch performance checks:
  - videos above 30 fps, above 720p/1080p, or in HEVC/AV1;
  - images over the 4096 texture limit;
  - `--optimize` re-encodes videos above 30 fps at 30 fps, same size and codec family
    (FFmpeg).
  - The checks include the media inside standard `.rpa` archives. The index is read
    without unpickling.
  - Archives in other formats are reported as locked, and are not opened.
- The loader marks games with performance issues (`!`).

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
| `tests/unit/test_horizon_paths.py` | Linux arm64 container, host CPython 3.12.8 | patched posixpath and importlib | 31 checks | 31 pass | PASS (host only) |
| `tests/integration/renpy_desktop/run.sh` | Linux arm64 container, Xvfb + Mesa llvmpipe 22.3.6, GLES 3.2 | **official** Ren'Py 8.5.3 SDK + our `switch_main.py`/`rsl_bridge.py`, scripted `_rslnx` | 26 checks | 26 pass; screenshots in `build/test/out/` | PASS (desktop only) |
| `tests/unit/test_importer.py` (25 tests, part of `scripts/test.sh`) | macOS host, `/usr/bin/python3` 3.9.6 | n/a | all pass | 25/25 | PASS |

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
    Run 10: PASS, the loader stayed up 76 s after the game.

**Runs 10–19** (recorded in the [run 9](tests/hardware/results/2026-10-19-run9.md),
[run 13](tests/hardware/results/2026-10-19-run13.md) and
[run 14](tests/hardware/results/2026-10-19-run14.md) files):
- PASS: every pthread is closed at runtime exit, so three games in a row and Exit to the
  Homebrew Menu run in one process without a crash (run 12).
- PASS: Forbidden Fantasy (Ren'Py 7.5.3, exported with `--recompile`) compiles its scripts on
  the console and renders (run 13).
- PASS: cards and choices are picked with the D-pad and A (run 18). The run 19 fix to
  button selection is on the console but has no hardware result yet.

**HARDWARE UNVERIFIED** (no result in any run):
- controls and console: remapping, profiles and the emergency combo (H9–H14, H35), touch (H7, H29),
  sleep/resume (H16, H25, H32), controller reconnect (H17), docked mode (H18);
- Ren'Py: save/reload (H31), smooth video playback (run 13), applet mode after
  the run 8 crash (H34), the full Switch keyboard (run 17 build).

Checklist: [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md), H1–H35.

## Known bugs and limitations

- The graphical loader has not run on a Switch yet (Milestone 7 hardware test pending).
- The in-game loader overlay is not built; L-stick press only shows a notice.
- Live2D and 3D models are unsupported.
- Non-ASCII file names on the SD card (run 20): about half of Japanese names cannot be created,
  and none show in directory listings. The importer packs such files into `game/rsl-nonascii.rpa`;
  images and sound load from it on a Switch ([run 21](tests/hardware/results/2026-10-21-run21.md)). Scripts and archives with such names stay loose and are
  still affected.
- devkitPro's switch-ffmpeg is built with `--enable-gpl` and linked statically, so a
  distributed `renpy-runtime.nro` is covered by the GPL (v2 or later): ship the source with
  it. Building FFmpeg as LGPL would need its own build and relinking materials.
- The runtime `.nro` is 55 MB; no memory or load-time measurement on hardware yet.
- The emergency combo needs both + and −, so a single detached Joy-Con cannot do it.
- The launcher UI state machine has no automated test (it needs libnx); the engine under it is tested.
- The runtime self-test writes console text from a second thread. The libnx console is not
  thread-safe, so text may tear (cosmetic).
- Python is missing `ctypes` (no libffi), `ssl`/`_hashlib` (no OpenSSL), `sqlite3`, `mmap`,
  subprocess spawning and `multiprocessing`. Games that need these will fail. See
  `runtime/python/README.md`.
- Chain-loading needs the loader to be started from the Homebrew Menu.
- License: the project's source is MIT (`LICENSE`); see README "Legal" for the binaries.
- Crash after returning from a game (run 9): SDL's timer thread from the runtime keeps
  running after the runtime's code is unloaded, then crashes the loader. Fixed in runtime
  `fed4dc1b…`, confirmed on the console (run 10).
- Exit to the Homebrew Menu after a game crashes sphaira (run 10).
  - [Likely] cause: libnx has no `pthread_detach`, so the stacks of CPython's exited threads
    stayed mapped.
  - Fix in runtime `35508472…` (run 11): Exit after a game no longer crashed, and
    6 detached threads were closed. 3 thread stacks (396 KiB) still stay mapped in some
    sessions. The next launch in that process then crashed in `memset` on a new SDL surface.
  - Fixed in runtime `9914bf86…`: every pthread is tracked, and at exit every exited one is
    closed. Run 12 passed: 3 games in a row in one process and Exit, `borrowed` stayed at
    8548 KiB, no crash. The 3 leftovers per game were Ren'Py's `ffmedia` decode threads.
  - Workaround: close the software from HOME.

## Next engineering tasks

1. Milestone 7: hardware test of the LVGL loader (`docs/LOADER_UI.md`); then covers in the
   importer (automatic from the menu art, custom cover in the desktop app), and the
   step-by-step "how to play" guide in `docs/` with screenshots from the console.
2. Transfer: the loader's own FTP server, then USB.
3. Milestone 8: release validation.
