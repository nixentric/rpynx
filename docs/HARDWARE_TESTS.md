# Real Nintendo Switch test checklist

Nothing in this project counts as hardware-verified until a row below has an actual
result. Emulator runs go in a separate column and never count as hardware verification.

Record each run in `tests/hardware/results/<date>-<tester>.md` with:
test name, console model + firmware + Atmosphère version, loader version (Diagnostics screen),
launch mode (applet / title override), expected result, actual result, PASS/FAIL, and the
matching part of `sdmc:/switch/renpy-loader/logs/loader.log`.

## Setup

1. Copy `dist/renpy-loader.nro` to `sdmc:/switch/renpy-loader/renpy-loader.nro`.
2. Optional: copy a profile from `config/profiles/` to
   `sdmc:/switch/renpy-loader/config/controller/global.json`.
3. Launch it twice: once from the album (applet mode), once by holding R while starting an
   installed game (title override, application mode). Note the mode shown on the main menu.

## Milestone 1–2 tests (current build)

| # | Test | Steps | Expected |
|---|---|---|---|
| H1 | Cold launch | Start from hbmenu | Main menu shows version, Handheld/Docked, applet type. `loader.log` gets "Ren'Py Switch Loader 0.1.0 …" |
| H2 | Folders created | After H1, browse SD | `logs/`, `config/controller/games/`, `games/` exist under `sdmc:/switch/renpy-loader/` |
| H3 | A/B orientation | Controller Test, press A, then B | A shows `confirm`, B shows `back`. Physical A is the right face button |
| H4 | Every button | Controller Test, press each button incl. ZL/ZR, +/−, stick clicks, D-pad | Each shows its own name under Physical, nothing else lights up |
| H5 | Sticks and deadzone | Controller Test, move both sticks slowly | Raw values change; normalised stays 0.00 inside the deadzone; full tilt is about 1.00 |
| H6 | Virtual cursor | Controller Test, right stick | Cursor moves, speeds up while held, stops at screen edges |
| H7 | Touch | Controller Test, handheld, touch the screen | Touch shows coordinates; cursor jumps to the touch point |
| H8 | Repeat | Main menu, hold D-pad down | One move, ~400 ms pause, then steady repeat |
| H9 | Remap and save | Controller Settings, remap `confirm` to X, `hide_ui` to A, press + | "Saved and applied"; X now selects in menus |
| H10 | Conflict | Remap `confirm` to B | Both rows marked conflict; + refuses to save with a message |
| H11 | Persistence | After H9, exit and relaunch | Mapping from H9 still active; Diagnostics says "loaded" |
| H12 | Emergency combo | Map `confirm` to SL (unusable), save, then hold + and − for 3 s | Default controls restored and settings opened. "Emergency reset" message; + saves |
| H13 | Damaged config | Truncate `global.json` on a PC, relaunch | Main menu warns that the backup was loaded; previous mapping active |
| H14 | Per-game profile | Create `games/test/game/`, Library → X on it, change a binding, + | `config/controller/games/test.json` written; global unchanged |
| H15 | HOME and back | Hold ZR in Controller Test, press HOME, return | No `skip` stuck active; buttons held during HOME are ignored until released |
| H16 | Sleep / resume | Sleep the console for more than 1 minute, wake | Loader responsive, log shows "resumed", no stuck input |
| H17 | Controller reconnect | Detach and reattach Joy-Con; connect a Pro Controller | Log shows "controller change"; input works after each change |
| H18 | Docked | Dock the console | Display mode switches to Docked; log shows it |
| H19 | Exit | Main menu → Exit | Returns to hbmenu; log ends with "exiting normally" and "log closed" |
| H20 | Missing SD write | Lock or remove write access (e.g. full card) | Red "not writable" warning; loader keeps running |

## Milestone 3 tests (embedded Python runtime)

Setup: copy `dist/renpy-runtime.nro` to `sdmc:/switch/renpy-loader/runtimes/renpy-runtime.nro`.

| # | Test | Steps | Expected |
|---|---|---|---|
| H21 | Runtime installed | Loader → Diagnostics | "Runtime: … found" |
| H22 | Self-test, applet mode | Start the loader from the album, Diagnostics → A | Loader exits, runtime prints PASS lines and "0 required check(s) failed". Report at `cache/python-selftest.txt`; `logs/runtime.log` shows "Python 3.12.8 … initialised" |
| H23 | Self-test, application mode | Same, with the loader started via title override (hold R on a game) | As H22. Record the memory line (used / total MiB) from both modes |
| H24 | Return to loader | After H22, press + | Loader starts again on its main menu |
| H25 | HOME and sleep during the self-test | Press HOME or sleep while the PASS lines are printing, then return | Test finishes; no hang; status line printed |

Record the full report text and both memory figures; they set the memory budget for Milestone 4.

Milestone 4 and later add the gameplay tests from the project brief (new game, dialogue,
choices, save/load loops, rollback, suspend during gameplay, soak tests).
