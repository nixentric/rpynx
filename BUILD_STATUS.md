# Build status

Last updated: 2026-10-18

## Current milestone

**Milestones 1 (toolchain) and 2 (input system): code complete, HARDWARE UNVERIFIED.**
No Ren'Py game can be launched yet. Next up: Milestone 3 (Python runtime).

## Implemented

- Pinned, containerised Switch build (`scripts/build.sh`), native arm64 on Apple Silicon.
- `renpy-loader.nro`: text-console loader with main menu, game folder listing, controller
  settings, controller test, diagnostics (applet type, docked/handheld, memory, log tail).
- Input pipeline (libnx HID → Nintendo-label normaliser → mapping engine → logical actions):
  - 16 logical actions with press, hold and repeat behaviour;
  - D-pad and analog navigation, plus a virtual cursor with deadzone, speed and acceleration;
  - touch readout, letterbox coordinate mapping;
  - presets: default, left-handed mirror, one-handed right.
- Remapping UI with button capture, conflict marking, reset to default, presets, global and
  per-game profiles, and live apply on save.
- Versioned JSON profiles with strict validation. Saves are atomic (`.tmp` → `.bak` swap),
  with `.bak` fallback on load and refusal to save conflicting or incomplete maps.
- Emergency recovery combo (hold + and − for 3 s) that no mapping can disable.
- Input reset on focus change, resume from sleep, and controller connect/disconnect/style change.
- Line-flushed log at `sdmc:/switch/renpy-loader/logs/loader.log`.

## Compilation results (actual)

| Item | Result |
|---|---|
| Host | macOS 15.7.9, Apple M-series (arm64), Colima 0.10.3 (vz) + Docker CLI 29.9.1 |
| Toolchain image | `devkitpro/devkita64:20260219` @ `sha256:1fc388c3…0282` (arm64 variant `645416b7…`) |
| Toolchain contents | devkitA64 r29.2, gcc 15.2.0, newlib 4.6.0.20260123, libnx 4.12.0, switch-tools 1.13.1 |
| Build | `scripts/build.sh`: success, 0 warnings with `-Wall -Wextra` |
| Artifact | `dist/renpy-loader.nro`, 354,501 bytes, `NRO0` magic at 0x10 |
| SHA-256 | `ee7b0861e1d112d4336231971eeec481c4b535a32ea989d7881b195c0e3827e6` |
| Reproducibility | two clean builds produced byte-identical `.nro` files |
| libnx layout check | `_Static_assert`s in `input_switch.c` confirm the HidNpadButton bit layout the normaliser assumes |

Other relevant packages in the image (for Milestones 3–4): switch-sdl2 2.28.5,
switch-sdl2_image 2.0.4, switch-sdl2_ttf 2.22.0, switch-sdl2_mixer 2.0.4, switch-mesa 20.1.0,
switch-ffmpeg 7.1, switch-freetype 2.13.3, switch-harfbuzz 10.0.1, switch-libfribidi 1.0.12,
switch-libpng 1.6.48, switch-libjpeg-turbo 2.1.2, switch-libwebp 1.0.2, switch-zlib 1.3.1,
switch-bzip2 1.0.8, switch-liblzma 5.8.2. The container itself has Python 3.11.2 and Debian 12.

## Test results (actual)

| Test | Environment | Expected | Actual | Status |
|---|---|---|---|---|
| `scripts/test.sh` (151 checks) | macOS host, Apple clang, ASan + UBSan | all pass | 151/151 pass | PASS |

Coverage of the host suite:
- Presets: no conflicts, all essential actions bound; Nintendo A→confirm, B→back.
- Normaliser: every libnx bit, no A/B swap, SL/SR folded, digital stick bits ignored.
- Bind and conflict detection; completeness.
- Press, hold, repeat timing, including a stall that produces one repeat rather than a burst.
- Navigation stick deadzone and axis dominance; cursor movement, clamping and capped acceleration.
- Emergency combo: timing, single fire, re-arm, survives an all-unbound config, suppresses +/−.
- Reset after resume: held buttons are ignored until released.
- Stick normalisation and letterbox maths; sanitising NaN and out-of-range values.
- JSON round trip for every preset, and 11 malformed or hostile inputs rejected.
- Shipped sample profiles match the presets.
- Atomic save: `.bak` creation, torn-write and missing-file fallback, missing directory, read size cap.

## Hardware verification

**HARDWARE UNVERIFIED.** The `.nro` has not been run on a Switch or an emulator. Every
on-device behaviour is untested:
- launch, rendering, button and stick reading, touch;
- SD writes, sleep/resume, controller reconnect, docked mode.

The checklist to run is [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) (H1–H20).

## Known bugs and limitations

- The loader UI is a text console. The graphical library UI is Milestone 7.
- The `overlay` action has no effect yet; the in-game overlay needs the runtime.
- The emergency combo needs both + and −, so a single detached Joy-Con cannot trigger it.
- The libnx console redraws the whole screen every frame. Unmeasured; possible flicker on hardware.
- The launcher's UI state machine (`app/launcher/main.c`) has no automated test; only the
  portable engine underneath it is tested. It needs libnx to run.
- The project license is not chosen yet.

## Next engineering tasks

1. Run H1–H20 on a console and record the results.
2. Milestone 3:
   - cross-compile CPython 3.12.8 statically for `aarch64-none-elf` (libnx/newlib), modules built in;
   - run a Python script from the `.nro`;
   - measure memory in applet and application mode;
   - document the required native modules.
3. Milestone 4: compile Ren'Py 8.5.3's Cython modules statically and bring up gl2 on
   switch-mesa; bridge logical actions into Ren'Py events with SDL's own controller handling
   disabled; run a minimal open-source Ren'Py demo.
