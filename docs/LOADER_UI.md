# Loader UI (Milestone 7)

The graphical game library that replaced the libnx text console of the loader.

Status (2026-10-22):
- Mockups approved; phase 2 built; first hardware test done (screenshots and `ui` log lines).
- Second round, from the user's review of that test: fonts measured so nothing is cut off, icons centred
  on the text, bigger covers, a controller-shaped test screen, pages for the library layout
  and the accent colour, How to play as an illustrated guide, the menu slides in.
- Third round, from the user's review on the Switch: details keep Launch (with a play icon) at the bottom right
  and scroll their facts; one Layout page for the layout and the accent colour; everything works by
  touch as well.
- Covers (2026-10-22): the importer makes them from the game's menu art or a chosen
  picture, in the desktop window too; on the Switch, *Change picture* on the details turns an Album
  screenshot into either (see "Covers"). Host mockups `13a` to `13d`; not run on hardware yet.
- Builds with 0 warnings; `.nro` 764 KB (363 KB with the text console). Second hardware test pending.
- Code: `app/launcher/` (main.c, actions.c, library.c, controls.c, art.c) and `app/ui/`: ui.c (the
  screen frame, building and updating screens), ui_style.c (fonts, styles, accent), ui_widgets.c
  (building blocks, touch), ui_art.c (covers), and one file per screen group:
  ui_library.c, ui_detail.c, ui_picture.c, ui_controls.c, ui_pages.c.

First hardware run (handheld, application mode, 4 games, no covers):
- first frame 80 ms after `main()`;
- cores 0-2 at 1 % when idle, SoC 36-38 °C;
- frames 11-17 ms on average, full-screen redraws (fades, layout changes) 20-42 ms;
- heap 20 MB in use.

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
| Library (home), Row layout (default) | Like the Switch HOME menu: one line of 220×330 portrait covers that scrolls sideways. The focused cover is 12 % bigger; below the row are its title, status, Ren'Py version, runtime, and the reason it cannot start (or its number of performance issues) |
| Library, Grid layout | Five portrait covers per row with titles, scrolling up and down |
| Library, Row layout | One line of portrait covers along the bottom, scrolling sideways; the focused game's big white title (48 px) and its tags above them, no header line and no game count. The focused cover is 12 % larger. Mockups `1a`, `1b`, `1d` |
| Library, all layouts | Badges: red `!` (cannot start), amber `!` (performance issues). Header: game count, Handheld/Docked, an amber "Applet mode" chip. Red/amber banners for a read-only SD folder or a controller file loaded from backup. Empty library: where to copy games |
| Menu (+ on the library) | Panel that slides in from the right: Controller settings, Controller test, Layout (its subtitle: the layout and accent in use), How to play, Diagnostics, Exit to the Homebrew Menu. The items scroll when they do not fit; the help (A, B) stays at the bottom under a rule. Moving the focus scrolls every list it is in; on a list's first line, all the way up so its heading shows |
| How to play | Shown once on the first start, then from the + menu. An illustrated guide in five cards that scrolls (up/down): the buttons in a game (from the user's current mapping), starting with full memory, adding games, the + menu, the emergency combo. Each card has a small drawing made of shapes. A or B closes it |
| Layout (+ menu) | One scrolling page. Library: Row and Grid side by side, each a white wireframe preview with a short description, "In use" on the current one; A uses the focused one. Colour: eight presets and a custom hue strip (left/right, live); a hue within 25° of the error red shows a warning beside the heading. The page follows a new accent live, without a rebuild. B goes back to the library, menu closed, like every page the + menu opens. Mockups `11`, `12a`, `12b`, `12c` |
| Game details (A on a cover) | Big cover; title, folder, Ren'Py version, status, runtime and whether it is installed; notes for Ren'Py 7 recompiled games, the reason a game cannot start, and up to three performance issues; these scroll (up/down) when they run longer than the room. Buttons, always at the bottom right: Change picture, Controls for this game, then Launch with a play icon (Can't launch when there is a problem). Mockups `3`, `3b`, `3c`, `4` |
| Picture (Change picture on the details) | One Album screenshot at a time over the whole screen, newest first, with "3 of 11" and when it was taken. A frame (2:3, full height) marks the part that becomes the cover; the rest is shaded. A writes it as the game's cover. Mockups `13a`; the result `13c`, `13d` |
| Applet-mode notice | Launch in applet mode (the loader opened from the Album) starts nothing. The notice says why (memory available; games crash the whole console there, hardware runs 8 and 26) and how to start with full memory (close the loader, hold R while opening a game). One button, OK; no "Launch anyway". The library header keeps the amber "Applet mode" chip |
| Controller settings | Global or per-game. One row per action with its buttons as chips, conflicts in red, "Press a button ... 5 s" while assigning; then deadzone, cursor speed and acceleration, repeat delay and interval, sticks, cursor visibility. Header: profile, preset, Unsaved |
| Controller test | The buttons laid out as on the controller (shoulder buttons on top, D-pad and A/B/X/Y as crosses, the sticks between), lit in the accent while pressed; the logical actions; touch, cursor and the last 6 events |
| Diagnostics | Applet type, display, memory bar, SD folder, controller file, runtime; the recent log; Run Python self-test |

The status line (left of the footer) shows the last message from the logic: red for errors, green for success. Screens with no message show a note there (controller settings: the emergency combo).

## Navigation

Every key goes through the mapping engine (`platform/input`), so a remap applies to the loader as well. The help line shows the button currently mapped to each action, and updates as soon as a new mapping is saved.

| Action (default button) | Library, Row | Library, Grid | Details | Settings | Test | Diagnostics | Menu / notice |
|---|---|---|---|---|---|---|---|
| left / right (D-pad, stick) | previous / next game | move in the grid | move between buttons | adjust a value | - | - | - |
| up / down (D-pad, stick) | - | move in the grid | scroll the facts | rows | - | - | move (menu) |
| confirm (A) | details | details | press button | assign / toggle | - | self-test | select / OK |
| back (B) | - | - | library | back (twice if unsaved) | hold 1 s | library | close |
| hide_ui (X) | game controls | game controls | - | reset to default | - | - | - |
| history (Y) | - | - | - | next preset | - | - | - |
| game_menu (+) | menu | menu | - | save | - | - | close menu |

- Every screen opens with its first item focused. The library keeps the focused game when you come back from a game's details.
- The row and the grid stop at their edges; lists wrap around, as in the console UI.
- How to play opens by itself on the first start; `config/loader.json` remembers that it was seen (`"help_seen": true`).
- `config/loader.json` holds the loader's own settings: `{"library_layout": "row", "accent": "#ff5fa8", "accent_hue": 330, "help_seen": true}`; `library_layout` is `row` or `grid`.
- Library layout: Row by default (what the user described first). The + menu switches it; it is saved in `config/loader.json` (`{"library_layout": "row"}`, written atomically like the controller files), not in the controller files, whose parser rejects unknown keys.
- Held directions repeat with the user's own repeat delay and interval.
- Picture page: left/right the newer/older screenshot, L/R (rollback/roll forward) move the cover's frame by 80 px, and so does a finger on the screenshot (the frame's middle follows it), confirm opens the choice (up/down, confirm uses it), back returns to the details. A screenshot is loaded 120 ms after the focus stops on it.
- Hold + and − for 3 s anywhere: default controls, controller settings open (unchanged).
- Touch: the touchscreen is an LVGL pointer (`main.c`). Dragging scrolls every list and row, and on the picture page moves the cover's frame; a tap on an item focuses it and confirms it, like A (a How to play card is only focused); a tap on a footer hint does that hint's action, on the second icon of a two-button hint (← →) the second one; a tap beside the + menu or the applet notice closes it. The controller test is left by holding B, so there a tap on its hint leaves at once. `tests/ui/mockups.c` checks a tap on Launch and on the Back hint.

Why there is no LVGL keypad input device: the mapping engine already turns buttons into actions, with the repeat timing from the user's settings. A keypad device would add LVGL's own key repeat on top, and it has no keys for hide_ui, history or game_menu. The logic in `main.c` moves `g.sel` on each action event, as the console UI does, and `ui.c` shows that as focus. The requirement holds either way: no key reaches the UI without passing through the user's mapping.

## Look

OLED dark with a pink accent, as the user asked ("lebih dark mode OLED", "warna birunya jadi warna pink").

- **Background:** pure black `#000000`. On an OLED Switch, black pixels are off.
- **Surfaces:**
  - cards and panels are near-black `#121215`, with a 1 px hairline `#26262d`, so their edges show on black;
  - rows, buttons and test chips are `#1f1f25`.
- **Text:** `#f0f0f3`; secondary text `#9b9ba4`, readable on black.
- **Focus:** a pink (`#ff5fa8`) outline that grows in over 120 ms (`FOCUS_FADE_MS`). Covers also scale up, 4 % in the grid and 12 % in the Row layout.
  - Focused text is pink too: menu entries, settings rows, buttons, the focused game's title.
  - Focused rows and buttons get a dark pink fill inside: 10 % accent mixed into `#121215`. Stronger fills (20 % and 40 % into `#1f1f25`) were tried and rejected: the pink text disappeared into them.
  - The same pink marks lit controller-test chips and the profile chip.
  - A disabled button ("Can't launch") keeps grey text even when focused.
- **Accent colour setting:** any colour; eight presets (Pink default, Blue `#25d0f2`, Violet, Indigo, Teal, Lime, Sky, White) or a custom hue, chosen on the Layout page and saved in `config/loader.json` (`"accent": "#ff5fa8"`, `"accent_hue"`).
  - Only the accent changes. Background, cards, text, amber, green and the orange-red errors are the same in both, so the error/focus separation holds for both.
  - In `ui.c` the accent is one value picked by the setting (`ACCENT`), not a second palette.
  - Mockup: `1d-library-row-blue.png`.
- **Status colours:**
  - errors are orange-red `#ff4b2b`, moved away from pink so that "selected" and "problem" never look alike (a focused game with a red `!`, a focused conflicting row);
  - amber `#ffb547`, green `#52d48a`.
- Covers have well-rounded corners (20 px), baked into the decoded thumbnails as transparency (ARGB, edge pixels partly), so a focused cover's outline stays whole. Baked in black, they hid the middle of the outline's arcs (hardware run, 2026-10-22). Cards, rows and buttons keep 14 / 10 / 12 px.
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
- Fonts: 18 / 22 / 28 / 36 / 48 px. The console's font reports a line box too tight for its own glyphs (descenders were cut, wrapped lines overlapped on the first hardware run), so `fit_lines()` measures real glyphs (Á, g, j, 夏) and sets each size's line box from them with some air. Running text gets 8 px between lines.
- Button icons are sized to 26 px and centred in their own line box (`icon_font()`), so they line up with the text beside them. A stick direction shows the stick's icon; the D-pad arrows are the console's own. No LVGL symbol glyphs are used: the console's font answers for their code points with an empty box.
- Effects, all light: the focus transition, a 150 ms fade between screens, a 120 ms fade for the menu, notice and status line, and animated scrolling to the focused item.

## Covers

Portrait 2:3, like the box art in GOG Galaxy, in both library layouts. The user first chose 16:9
cards taken from the main menu art, then asked for portrait covers like GOG. They also asked for
a custom cover option in the desktop app.

The catch: Ren'Py games ship no cover, only 16:9 menu art. An automatic cover is the middle third
of that art, so it often shows part of a background, not the game's title. The custom cover is
where a real poster comes in, for example a 600×900 image from SteamGridDB or a Steam capsule.

- **Manifest:** `"cover": "cover.jpg"`, a path relative to the game's folder; `"cover_custom": true` when the user chose it (the importer's `--replace` keeps it).
- **File:**
  - baseline JPEG, 300×450 (the detail page size), written by the importer;
  - or a 24-bit BMP of the same size, written by *Change picture* on the Switch (`cover.bmp`);
  - the loader also accepts other sizes up to 600×900 and cuts other aspect ratios to their middle;
  - a progressive JPEG, a larger picture or an unreadable file gets the placeholder and one line in `loader.log`.
- **Placeholder:** a cover coloured from the folder name, with the title's first letter. Used for games without a cover, such as games copied by hand.
- **Cost on the Switch:**
  - LVGL's JPEG decoder (TJpgDec) keeps nothing between draws, so each cover is decoded once, scaled to a 220×330 thumbnail (290 KB; about 19 MB for 64 games);
  - all layouts use the same thumbnails. The row shows the focused cover 12 % larger by scaling that thumbnail;
  - covers are decoded one per frame after the library first appears, so start-up does not wait for them;
  - the detail page decodes its 300×450 cover when it opens;
  - LVGL's decoder logs one warning per decode when it reads past the last row of blocks. The picture is complete.

### Importer side (built 2026-10-22; docs/IMPORTER.md "Cover")

- **Automatic cover:**
  - read `gui.main_menu_background` from `gui.rpy` when it is a plain string literal; otherwise use `gui/main_menu.png`. Its middle becomes the cover;
  - the image can be a loose file or inside an `.rpa` archive (`rpa_index()` already reads the index);
  - nothing found, or not readable: no cover, plus one line in the report. The placeholder stays.

- **Custom cover:** `--cover IMAGE`, and *Change cover…* in the desktop window, on the Import page and for games already on the SD card or the Switch. It overrides the automatic one and goes through the same conversion. The manifest marks it `"cover_custom"`, and an export with `--replace` keeps it (also one chosen on the Switch).
- **Conversion:**
  - ffmpeg, which the importer already finds on macOS, Linux and Windows (`find_tool(FFMPEG)`, `--tools`). No new dependency, and no macOS-only tool such as `sips`. The importer otherwise uses the Python standard library only;
  - `scale=300:450:force_original_aspect_ratio=increase,crop=300:450`, one frame, `yuvj420p`: a baseline JPEG;
  - PNG, JPEG and WebP sources all work through ffmpeg;
  - without ffmpeg: no cover, and the report says ffmpeg is needed for covers.

### Switch side: Change picture (built 2026-10-22, on a Switch in run 28)

The user's choices (2026-10-22): pictures from the Album, saved as BMP, a third button on the details, the cover's frame movable.

- **Source:** the Album, through the system's Album service (`capsa`, libnx): screenshots on the SD card and in the system memory, newest first. `capsaLoadAlbumScreenShotImage` returns the decoded 1280×720 RGBA, which the loader turns into XRGB8888 in place.
  - Why not the files under `Nintendo/Album/`: every screenshot checked (11, from the user's Switch) is a baseline JPEG with 4:4:0 sampling (Y 1×2), which TJpgDec refuses (it reads 4:4:4, 4:2:2 and 4:2:0). The Album also holds screenshots in the system memory.
  - Videos are left out.
- **Storage:** the loader has no JPEG encoder, so it writes a 24-bit bottom-up BMP (`rsl_write_bmp`, `platform/filesystem/fs.c`): `cover.bmp` 300×450 (405 KB) from the framed part. LVGL's BMP decoder (`LV_USE_BMP`) reads it through the same `render_cover()`. The desktop window shows BMP too.
  - Written with `rsl_write_atomic`, pictures first, then `manifest.json` (cJSON): `"cover": "cover.bmp", "cover_custom": true`. If the manifest cannot be written, the game keeps the cover it had.
  - A game without a `manifest.json` (copied by hand) gets a message instead: the manifest names the cover, and a manifest without the importer's fields would stop the game from starting.
- **Memory:** the screenshot buffer (3.7 MB) exists only while the page is open. The page's image drops it before the 150 ms fade to the next screen, and the Album service is closed when the page is left (also after the emergency combo).
- **Afterwards:** the game's thumbnail is decoded again (`ui_art_changed`), so the library shows the new cover at once.

## Phase 2 (built; hardware test pending)

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
3. **Bounded cover memory.** The library holds at most `MAX_GAMES` (64) games, so the thumbnails take at most about 19 MB, the same for every layout.
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
