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
| H1 | Cold launch | Start from hbmenu | Main menu shows version, Handheld/Docked, applet type. `loader.log` gets "rpynx 0.1.0 …" |
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

## Milestone 4 tests (Ren'Py engine)

Setup:
- Install the Milestone 3 files, using the new `renpy-runtime.nro`.
- Copy the `the_question` folder from the official Ren'Py 8.5.3 SDK (renpy.org) to
  `sdmc:/switch/renpy-loader/games/the_question/`.
- Start the loader via title override (application mode) unless the test says otherwise.

| # | Test | Steps | Expected |
|---|---|---|---|
| H26 | Cold launch | Library → the_question → A | Ren'Py main menu within ~30 s. `logs/the_question/log.txt` shows "Initializing gles2 renderer" and the GPU vendor |
| H27 | Dialogue and choices | Start, advance with A, reach the first menu, pick with D-pad + A | Text renders correctly; the choice is taken |
| H28 | Controls | Try L, R, X, Y, B, +, −, ZL, hold ZR | Rollback, roll forward, hide UI, history, back, game menu, quick menu, auto-forward, skip while held |
| H29 | Virtual cursor and touch | Move the right stick, press it on a button; tap a button on the touchscreen | Arrow cursor moves smoothly and clicks; touch works |
| H30 | Audio | Listen through the opening | Music and sound play, without stutter |
| H31 | Save / reload | Save in slot 1, quit to the library, relaunch, load slot 1 | Same scene restored; the save is in `saves/the_question/` |
| H32 | HOME and sleep | Press HOME mid-dialogue, return; sleep for more than 1 minute, wake | Game resumes; audio and picture back; no stuck skip; no dialogue skipped |
| H33 | Return to library | Main menu → Quit | The loader comes back |
| H34 | Applet mode | Start the loader from the album, Library → the_question → A | The loader refuses with a message (full memory needed, hold R); no game starts, no console crash |
| H35 | Emergency combo | Hold + and − for 3 s in game, then quit to the loader | "Controls reset to default and saved" notice; the loader also uses the defaults; `runtime.log` shows "emergency reset saved to …global.json: ok" |

Record `logs/the_question/log.txt` and `logs/runtime.log` with every result.

## Milestone 7 tests (graphical loader, covers, Change picture)

Setup:
- Push `dist/renpy-loader.nro` with `scripts/switch-ftp.sh HOST:PORT push-loader`; it reads the
  file back and compares.
- At least one game with pictures and one without, so a real cover and the placeholder are both
  on screen. The pictures come from the importer: a re-export with `--replace`, or
  *Change cover…* / *Change background…* in the desktop window, both with `--out ftp://HOST:PORT`.
- Application mode (hold R while starting a game) for every test that launches a game.

| # | Test | Steps | Expected |
|---|---|---|---|
| H36 | Title and first line | Start from hbmenu | hbmenu shows "rpynx"; `loader.log` starts with "rpynx 0.1.0, …" |
| H37 | How to play | First start, with no how-to-play flag in `config/loader.json` | The guide opens by itself; leaving it returns to the library and the next start goes straight there |
| H38 | Covers in the library | Open the library | Games with `cover.jpg`/`cover.bmp` show it; games without get the coloured placeholder with the title's first letter; covers fill in one per frame after the first frame, so start-up does not wait for them |
| H39 | Row layout | Move the focus along the row | The focused cover is 12 % larger and nothing is cut off |
| H40 | Grid layout | + menu → Layout → Grid | Every cover is drawn; scrolling stays smooth |
| H41 | Cinematic layout | Switch to Cinematic, move the focus, then hold one direction | The backdrop is the focused game's `backdrop.jpg`, cover-fit and dark toward the bottom; a game without one gets the glow in its cover's colour; holding a direction does not decode on every step (150 ms settle) |
| H42 | Game details | A on a cover | Backdrop from the menu art, big cover, long notes scroll, and three buttons at the bottom right: Change picture, Controls for this game, Launch |
| H43 | Back from Layout | + menu → Layout, then B | The library comes back, not the + menu |
| H44 | Picture page | Details → Change picture | Album screenshots newest first, with "3 of 11" and when it was taken; the 2:3 frame is full height and the rest is shaded; videos are left out |
| H45 | Move the frame | D-pad and stick, then drag the frame with a finger | Both move the frame; the touch drag follows the finger and stops at the picture's edges |
| H46 | Use the picture | A → Use as cover / Use as background / Use for both | `cover.bmp` 300×450 and/or `backdrop.bmp` 640×360 written; `manifest.json` gets `"cover_custom": true` / `"backdrop_custom": true`; the library shows the new cover at once |
| H47 | Game without a manifest | Copy a game folder to `games/` by hand, Change picture on it | A message instead; no picture and no manifest written |
| H48 | Memory | Note the heap before the picture page, while it is open, and after leaving | The 3.7 MB screenshot buffer is gone after leaving; `loader.log` shows the Album service closed |
| H49 | Launch still works | Library → a game → Launch, then Quit | The game starts and the loader comes back (regression check after the app/ui split) |
| H50 | Numbers | Read the `ui` summary line after a minute | first frame, frame time, full redraws, core load, SoC temperature, heap, covers; compare with 2026-10-22: 80 ms, 11–17 ms, 36–38 °C, 20–24 MB |

Fetch the logs with `scripts/switch-ftp.sh HOST:PORT logs` and record the `ui` lines with every
result. LVGL logs one warning per JPEG decode, because it reads past the last row of blocks; the
picture is complete. A cover line about a progressive JPEG or an oversized picture is a real
failure.

Later milestones add the gameplay tests from the project brief (new game, dialogue,
choices, save/load loops, rollback, suspend during gameplay, soak tests).
