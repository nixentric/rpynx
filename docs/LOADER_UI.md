# Loader UI (Milestone 7)

The graphical game library that replaces the libnx text console in `app/launcher/main.c`.

Status: waiting for design approval. The screens exist and render on the Mac
(`scripts/mockups.sh`); the Switch side (framebuffer, input, `main.c`) is the next phase.

## Library: LVGL 9.6.0

| | |
|---|---|
| Licence | MIT (`build/lvgl/lvgl/LICENCE.txt`). Bundled parts used: tiny_ttf (stb_truetype, MIT / public domain), TJpgDec (ChaN, free use with notice), Montserrat 14 (SIL OFL) |
| Release | v9.6.0, 2026-09-16, the latest release |
| Fetch | `third_party/lvgl/fetch.sh`: GitHub tag tarball, SHA-256 pinned, like `runtime/python/build.sh` does for CPython. The tarball is 111 MB (tests, docs, demos), so it is not vendored; only `src/`, `include/`, `lvgl.h` and the licence are extracted to `build/lvgl/lvgl` |
| Build | `third_party/lvgl/Makefile` makes `liblvgl.a` with any compiler (the devkitPro template cannot build LVGL directly: two LVGL files share a name) |
| Config | `third_party/lvgl/lv_conf.h`: only what differs from LVGL's defaults. XRGB8888, libc malloc, software renderer with 4 pixel formats, NEON on the Switch, no theme, only label / image / bar / canvas widgets |

## Rendering

- LVGL's software renderer draws into the libnx framebuffer on the CPU. No Mesa, no GPU in the loader.
- XRGB8888 is B,G,R,X in memory, the Switch framebuffer's BGRA layout.
- Retained mode: a screen is built when `g.screen` changes. `ui_update()` changes only what differs from the last frame, so LVGL redraws only those areas. A frame where nothing happened draws nothing.
- Text uses the console's own font (shared font "Standard", from the `pl` service) through tiny_ttf. The `.nro` carries no font file. It covers Latin and Japanese. Chinese or Korean titles show boxes; the other shared fonts can be chained as fallbacks if that is ever needed. Montserrat 14 is the default font and the fallback for LVGL's arrow symbols.

## Code layout

| Path | Role |
|---|---|
| `app/launcher/loader.h` | The loader state `g`: screen, focus, games and their manifest data, controller settings being edited, console facts. No libnx types |
| `app/ui/ui.c` | Every screen, built from `g`. Two functions: `ui_init()` and `ui_update()` |
| `app/launcher/main.c` | Logic and libnx: scan, manifest, remap, launch, framebuffer, input (next phase) |
| `tests/ui/mockups.c`, `scripts/mockups.sh` | Render each screen with sample data on the Mac, through the same `ui.c` and `lv_conf.h`, under ASan/UBSan |

## Screens

Mockups: `scripts/mockups.sh` writes `build/host/mockups/*.png` (1280×720, the Switch resolution).

| Screen | Content |
|---|---|
| Library (home), Row layout (default) | Like the Switch HOME menu: one line of portrait covers that scrolls sideways. The focused cover is 25 % bigger; below the row are its title, status, Ren'Py version, runtime, and the reason it cannot start (or its number of performance issues) |
| Library, Grid layout | Six portrait covers per row with titles, scrolling up and down |
| Library, Cinematic layout | The Row layout over the focused game's own menu art, spread across the whole screen and darkened toward the text and the row. No header line, no game count: a big pink title (48 px), its tags, and one line of covers along the bottom (focused cover +4 %). Mockups `1e`, `1f` |
| Library, all layouts | Badges: red `!` (cannot start), amber `!` (performance issues). Header: game count, Handheld/Docked, an amber "Applet mode" chip. Red/amber banners for a read-only SD folder or a controller file loaded from backup. Empty library: where to copy games |
| Menu (+ on the library) | Panel on the right: Controller settings, Controller test, Diagnostics, Library layout (A: next of Row, Grid, Cinematic), Accent colour (A switches Pink ↔ Blue), How to play, Exit to the Homebrew Menu |
| How to play | Shown once on the first start, then from the + menu. Left: what each button does in a game, drawn from the user's current mapping, and which stick moves the cursor. Right: start with full memory (hold R; from the Album games do not start), adding games (desktop importer, then `switch/renpy-loader/games/`), the emergency combo. A or B closes it |
| Game details (A on a cover) | Big cover; title, folder, Ren'Py version, status, runtime and whether it is installed; notes for Ren'Py 7 recompiled games, the reason a game cannot start, and up to three performance issues. Buttons: Launch (Can't launch when there is a problem), Controls for this game |
| Applet-mode notice | Launch in applet mode (the loader opened from the Album) starts nothing. The notice says why (memory available; games crash the whole console there, hardware runs 8 and 26) and how to start with full memory (close the loader, hold R while opening a game). One button, OK; no "Launch anyway". The library header keeps the amber "Applet mode" chip |
| Controller settings | Global or per-game. One row per action with its buttons as chips, conflicts in red, "Press a button ... 5 s" while assigning; then deadzone, cursor speed and acceleration, repeat delay and interval, sticks, cursor visibility. Header: profile, preset, Unsaved |
| Controller test | Physical buttons and logical actions light up; both sticks (after the deadzone); touch, cursor, the last 8 events |
| Diagnostics | Applet type, display, memory bar, SD folder, controller file, runtime; the recent log; Run Python self-test |

The status line (left of the footer) shows the last message from the logic: red for errors, green for success. Screens with no message show a note there (controller settings: the emergency combo).

## Navigation

Every key goes through the mapping engine (`platform/input`), so a remap applies to the loader as well. The help line shows the button currently mapped to each action, and updates as soon as a new mapping is saved.

| Action (default button) | Library, Row / Cinematic | Library, Grid | Details | Settings | Test | Diagnostics | Menu / notice |
|---|---|---|---|---|---|---|---|
| left / right (D-pad, stick) | previous / next game | move in the grid | move between buttons | adjust a value | - | - | - |
| up / down (D-pad, stick) | - | move in the grid | - | rows | - | - | move (menu) |
| confirm (A) | details | details | press button | assign / toggle | - | self-test | select / OK |
| back (B) | - | - | library | back (twice if unsaved) | hold 1 s | library | close |
| hide_ui (X) | game controls | game controls | - | reset to default | - | - | - |
| history (Y) | - | - | - | next preset | - | - | - |
| game_menu (+) | menu | menu | - | save | - | - | close menu |

- Every screen opens with its first item focused. The library keeps the focused game when you come back from a game's details.
- The row and the grid stop at their edges; lists wrap around, as in the console UI.
- How to play opens by itself on the first start; `config/loader.json` remembers that it was seen (`"help_seen": true`).
- `config/loader.json` holds the loader's own settings: `{"library_layout": "row", "accent": "pink", "help_seen": true}`; `library_layout` is `row`, `grid` or `cinematic`.
- Library layout: Row by default (what the user described first). The + menu switches it; it is saved in `config/loader.json` (`{"library_layout": "row"}`, written atomically like the controller files), not in the controller files, whose parser rejects unknown keys.
- Held directions repeat with the user's own repeat delay and interval.
- Hold + and − for 3 s anywhere: default controls, controller settings open (unchanged).
- Touch: not in this milestone.

Why there is no LVGL keypad input device: the mapping engine already turns buttons into actions, with the repeat timing from the user's settings. A keypad device would add LVGL's own key repeat on top, and it has no keys for hide_ui, history or game_menu. The logic in `main.c` moves `g.sel` on each action event, as the console UI does, and `ui.c` shows that as focus. The requirement holds either way: no key reaches the UI without passing through the user's mapping.

## Look

OLED dark with a pink accent, as the user asked ("lebih dark mode OLED", "warna birunya jadi warna pink").

- **Background:** pure black `#000000`. On an OLED Switch, black pixels are off.
- **Surfaces:**
  - cards and panels are near-black `#121215`, with a 1 px hairline `#26262d`, so their edges show on black;
  - rows, buttons and test chips are `#1f1f25`.
- **Text:** `#f0f0f3`; secondary text `#9b9ba4`, readable on black.
- **Focus:** a pink (`#ff5fa8`) outline that grows in over 120 ms. Covers also scale up, 4 % in the grid and in the Cinematic row, 25 % in the Row layout.
  - Focused text is pink too: menu entries, settings rows, buttons, the focused game's title.
  - Focused rows and buttons get a dark pink fill inside: 10 % accent mixed into `#121215`. Stronger fills (20 % and 40 % into `#1f1f25`) were tried and rejected: the pink text disappeared into them.
  - The same pink marks lit controller-test chips and the profile chip.
  - A disabled button ("Can't launch") keeps grey text even when focused.
- **Accent colour setting:** Pink (default) or Blue (`#25d0f2`), switched in the + menu and saved in `config/loader.json`.
  - Only the accent changes. Background, cards, text, amber, green and the orange-red errors are the same in both, so the error/focus separation holds for both.
  - In `ui.c` the accent is one value picked by the setting (`ACCENT`), not a second palette.
  - Mockup: `1d-library-row-blue.png`.
- **Status colours:**
  - errors are orange-red `#ff4b2b`, moved away from pink so that "selected" and "problem" never look alike (a focused game with a red `!`, a focused conflicting row);
  - amber `#ffb547`, green `#52d48a`.
- Covers have well-rounded corners (20 px), baked into the decoded thumbnails, so drawing them stays a plain copy. Cards, rows and buttons keep 14 / 10 / 12 px.
- Tags ("Experimental", "Ren'Py 8.5.3", "Handheld", "Missing", the profile and preset chips, banners) share one style: a full pill (`LV_RADIUS_CIRCLE`), 16 px × 7 px padding, the colour's text on a 20 % tint of it.
- **Controller buttons:** on the Switch, the console's own button icons, the ones the HOME menu, hbmenu and sphaira use.
  - They come from the system's shared font "Nintendo Extended" (`plGetSharedFontByType(PlSharedFontType_NintendoExt)`), drawn through tiny_ttf next to the text font.
  - The font is loaded from the console at run time, as hbmenu and sphaira do. Nothing of Nintendo's is copied into the `.nro` or the repository.
  - Code points, as sphaira's `nvg_util.cpp` uses them:

    | Button | A | B | X | Y | L | R | ZL | ZR | ↑ | ↓ | ← | → | + | − | LS click | RS click |
    |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
    | Code point | E0E0 | E0E1 | E0E2 | E0E3 | E0E4 | E0E5 | E0E6 | E0E7 | E0EB | E0EC | E0ED | E0EE | E0EF | E0F0 | E104 | E105 |

  - hbmenu's dark theme uses the filled set for A, B, X, Y (E0A0 to E0A3) and for + and − (E0B3, E0B4). Pick the set on hardware.
  - SL, SR and the stick directions keep the drawn chips, which are also the fallback when the shared font cannot be loaded.
  - The mockups on the Mac show the drawn chips (dark glyphs on light discs) as stand-ins: Nintendo's font is not on the Mac, and it is not copied here.
- Fonts: 18 / 22 / 28 / 36 px.
- Effects, all light: the focus transition, a 150 ms fade between screens, a 120 ms fade for the menu, notice and status line, and animated scrolling to the focused item.

## Covers

Portrait 2:3, like the box art in GOG Galaxy, in both library layouts. The user first chose 16:9
cards taken from the main menu art, then asked for portrait covers like GOG. They also asked for
a custom cover option in the desktop app.

The catch: Ren'Py games ship no cover, only 16:9 menu art. An automatic cover is the middle third
of that art, so it often shows part of a background, not the game's title. The custom cover is
where a real poster comes in, for example a 600×900 image from SteamGridDB or a Steam capsule.

- **Manifest:** `"cover": "cover.jpg"`, a path relative to the game's folder.
- **File:**
  - baseline JPEG, 300×450 (the detail page size), written by the importer;
  - the loader also accepts other sizes up to 600×900 and cuts other aspect ratios to their middle;
  - a progressive JPEG, a larger picture or an unreadable file gets the placeholder and one line in `loader.log`.
- **Placeholder:** a cover coloured from the folder name, with the title's first letter. Used for games without a cover, such as games copied by hand.
- **Cost on the Switch:**
  - LVGL's JPEG decoder (TJpgDec) keeps nothing between draws, so each cover is decoded once, scaled to a 176×264 thumbnail (186 KB; about 12 MB for 64 games);
  - both layouts use the same thumbnails. The row shows the focused cover 25 % larger by scaling that thumbnail, so it is a little soft. If that looks bad on the Switch, decode the focused cover from its 300×450 file instead;
  - covers are decoded one per frame after the library first appears, so start-up does not wait for them;
  - the detail page decodes its 300×450 cover when it opens;
  - Cinematic layout:
    - the backdrop is the focused game's `backdrop.jpg` (640×360), cover-fit to the screen, with the darkening baked into one 1280×720 buffer (3.7 MB). Drawing it afterwards is a copy;
    - it is redone 150 ms after the focus stops moving, so holding a direction never decodes on every step;
    - games without a backdrop get their placeholder colour, darkened the same way;
    - the loader decodes JPEGs up to 600×900 worth of pixels (540 000), so 640×360 fits.
  - LVGL's decoder logs one warning per decode when it reads past the last row of blocks. The picture is complete.

### Importer side (not built yet; after Milestone 6)

- **Automatic cover:**
  - read `gui.main_menu_background` from `gui.rpy` when it is a plain string literal; otherwise use `gui/main_menu.png`. Its middle becomes the cover;
  - the image can be a loose file or inside an `.rpa` archive (`rpa_index()` already reads the index);
  - nothing found, or not readable: no cover, plus one line in the report. The placeholder stays.
- **Backdrop** (Cinematic layout): the same menu art, written as `backdrop.jpg`, 640×360 baseline JPEG, `"backdrop": "backdrop.jpg"` in the manifest. It stays the menu art even when the user picks a custom cover.
- **Custom cover:** a CLI option (for example `--cover IMAGE`) and a picker in the desktop window, also for games already on the SD card. It overrides the automatic cover and goes through the same conversion.
- **Conversion:**
  - ffmpeg, which the importer already finds on macOS, Linux and Windows (`find_tool(FFMPEG)`, `--tools`). No new dependency, and no macOS-only tool such as `sips`. The importer otherwise uses the Python standard library only;
  - `scale=300:450:force_original_aspect_ratio=increase,crop=300:450`, one frame, `yuvj420p`: a baseline JPEG; the backdrop the same with `640:360`;
  - PNG, JPEG and WebP sources all work through ffmpeg;
  - without ffmpeg: no cover, and the report says ffmpeg is needed for covers.

## Next phase (after approval)

- `main.c`: keep its logic (scan, manifest table and refusals, applet warning and confirmation, per-game controls, diagnostics, self-test, emergency combo).
  - Replace the `draw_*` functions with `ui_update()`.
  - Add the library ↔ details ↔ menu navigation from the table above.
  - Status messages use the friendly action names and a real arrow, e.g. "History → X: also used by Hide dialogue" (now "history -> X conflicts with: hide_ui"). On the Switch the button can be its NintendoExt icon in the same line.
  - In applet mode, Launch opens the notice and starts nothing. `main.c` already refuses (no second-press override); the runtime refuses as well. The Python self-test still runs in applet mode.
- Display: libnx framebuffer, LVGL in direct mode on its linear buffer.
- Font: `pl` shared font.
- Build: the Makefile builds and links `liblvgl.a`. Check the size and start-up time of the loader `.nro`.
- Tests: `scripts/test.sh` also renders every screen under ASan/UBSan.
- Guide: once the UI runs on the console, a step-by-step "how to play" page in `docs/` with Switch screenshots.

### Light on the Switch

A UI bug must not keep the console busy or hot. Each item stays small.

1. **Idle means no work.**
   - With no input, no running animation and no pending cover decode, the loop does not render and does not call `framebufferEnd`.
   - It sleeps until the next input poll, for as long as `lv_timer_handler()` allows, and only while `lv_anim_count_running()` is 0.
   - It presents a frame only when LVGL flushed an area.
   - Target: core busy at most 5 % two seconds after the last input on the library screen, measured like the runtime telemetry (`IdleTickCount`).
2. **Regression test against redrawing every frame.** `scripts/test.sh` runs `tests/ui/mockups.c`. For every screen, and for both library layouts, it checks that:
   - a second `ui_update()` with `g` unchanged invalidates nothing;
   - once the animations finish, further refresh cycles flush nothing.

   The test counts flush calls, so it catches `set_text` with the same text, style resets and similar bugs.
3. **Bounded cover memory.** The library holds at most `MAX_GAMES` (64) games, so the thumbnails take at most about 12 MB, the same for both layouts.
   - Raising `MAX_GAMES` also means keeping only the visible rows, plus one row ahead, decoded.
   - Until then, re-decoding while scrolling would only add stutter.
   - The exit line logs the heap peak (`mallinfo`).
4. **Logging** to `logs/loader.log`.
   - Every line starts with `ui`, so the log can be fetched over FTP (`scripts/switch-ftp.sh HOST logs`) and grepped.
   - `rsl_log()` flushes each line to the SD card, so the lines are rate-limited.
   - At start: ms from `main()` to the first frame, applet type, heap in use.
   - Every 60 s while the loader is open, never per frame:
     - frames drawn, average and maximum render ms, full-screen frames;
     - core busy %, SoC temperature;
     - heap in use and peak, covers decoded and failed.
   - `log_telemetry()` (temperature, `IdleTickCount`, battery) moves from `runtime/main.c` to `app/diagnostics/procinfo.c`, and both programs use it.
   - Warnings, at most one per kind per minute:
     - a frame over 33 ms, with the screen and the dirty area;
     - "redrawing while idle": frames still drawn 2 s after the last input with no animation running, with the screen;
     - a cover that cannot be decoded.
   - LVGL's own log goes through the same function. The TJpgDec "read past the last row" warning is dropped; it would otherwise add a line for every cover.
   - At exit: one summary line before `chain_load`, next to the existing heap audit.
5. **Host timing.** `scripts/mockups.sh` prints the render time of each screen, labelled host-only: it is not Switch timing.
