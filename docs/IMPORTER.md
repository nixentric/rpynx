# Importer

`importer/rsl_import.py` checks a DRM-free Ren'Py game and copies it into the layout the
Switch loader expects. `importer/rsl_import_gui.py` is a window over the same code (see
"Desktop window"). Both need only Python 3.9+ and its standard library, on Windows, macOS
(`/usr/bin/python3` for the command line) or Linux. Two optional tools are found on the
system (see "Tools"):
- 7-Zip, for `.7z` and `.rar` files;
- FFmpeg (`ffprobe`, `ffmpeg`), to check videos, for `--optimize`, and to make the cover and
  backdrop the loader shows (see "Cover and backdrop").

People without Python can use the built app, `RSL Importer.exe` or `RSL Importer.app` (see
"Building the app").

The code is the `importer/rsl/` package; `rsl_import.py` is only its command line and
`rsl_import_gui.py` its window. `rsl/__init__.py` lists what both use. The modules, in the
order a game goes through them:

| Module | What it does |
|---|---|
| `archives` | Unpacks a `.zip`, `.7z` or `.rar` safely ("Archive safety"). |
| `analysis` | Finds the game, its Ren'Py version and Python, runs the checks, builds the manifest and the report. |
| `rpa` | Reads standard `.rpa` indexes as plain data; writes the non-ASCII archive. |
| `media` | What would load the Switch, `--optimize` ("Switch performance"), the cover and backdrop ("Cover and backdrop"). |
| `recompile` | `--recompile` and `--decompile`. |
| `exporter` | Writes a game into the library: an SD card folder, or the Switch over FTP. |
| `library` | Lists, deletes and compares what a library holds. |
| `tools` | Finds and runs 7-Zip, FFmpeg and the decompilers ("Tools"). |
| `common` | Errors, ids and small helpers. |

The importer never runs game code and never unpickles anything. It never changes the source
folder or archive. It does not decrypt or unpack `.rpa` archives. The optional `--decompile`
passes copies of `.rpyc` files to an external decompiler, unrpyc or rpycdec, which runs in
its own process (see "Decompile").

## Usage

```bash
python3 importer/rsl_import.py SOURCE                    # report only
python3 importer/rsl_import.py SOURCE --out /Volumes/SD  # report + export (macOS)
py importer\rsl_import.py SOURCE --out E:\               # report + export (Windows)
python3 importer/rsl_import.py SOURCE --out ftp://192.168.1.20:5000   # report + upload to the Switch
```

`SOURCE` can be:
- a game folder (the distribution root, an SDK project, or the `game/` folder itself);
- a `.zip`, `.7z` or `.rar` file.

| Option | Meaning |
|---|---|
| `--out DIR` | Export to `DIR/switch/renpy-loader/games/<id>/` (`game/` + `manifest.json`). `ftp://HOST:PORT` uploads to the Switch instead (see "Switch over FTP"). |
| `--id ID` | Choose the folder name yourself. It must already be in normalised form (see below). |
| `--replace` | Update an existing export. Without it, the importer refuses if `games/<id>` exists. |
| `--optimize` | Re-encode videos above 30 fps at 30 fps, at the same size and constant quality. Needs FFmpeg. See "Switch performance". |
| `--cover IMAGE` | The game's cover in the loader, from any picture FFmpeg reads (a 2:3 poster suits it best). Default: the middle of the game's menu art. See "Cover and backdrop". |
| `--backdrop IMAGE` | The picture behind the loader's detail page and Cinematic layout (16:9). Default: the game's menu art. |
| `--recompile` | Ren'Py 7 / Python 2 games only. Exports without `.rpyc`, so Ren'Py 8.5.3 compiles the `.rpy` sources with Python 3 (see below). |
| `--decompile` | Rebuilds a `.rpy` for each `.rpyc` that has none, then recompiles (implies `--recompile`). See "Decompile". |
| `--decompiler TOOL` | With `--decompile`: `unrpyc` (default) or `rpycdec`. "Decompile" compares them. |
| `--tools DIR` | A folder holding `ffmpeg`/`ffprobe` or `7z`, searched before `PATH` (see "Tools"). |
| `--rpycdec-python PYTHON` | With `--decompiler rpycdec`: a Python 3.10+ interpreter that has rpycdec installed. Default: the `rpycdec` command on `PATH`. |

Exit status: `0` OK; `1` refused (unsafe archive, not a Ren'Py game, unsupported game,
or the export already exists); `2` bad arguments.

Example, on the Ren'Py demo shipped in this repo:

```bash
python3 importer/rsl_import.py build/test/sdk/the_question --out /tmp/sd
```

```text
The Question  (id: the-question)
  Ren'Py:  8.5.3.26051504   Python: 3
  Status:  EXPERIMENTAL on runtime renpy-8.5.3
```

## Desktop window

The window is a web page (`importer/rsl_import_gui.html`) shown by pywebview in the
system's own web view: WebKit on macOS, Edge WebView2 on Windows. The page calls the `Api`
class in `importer/rsl_import_gui.py`, which only calls the `rsl` package; every check and copy
is the importer's. pywebview is the only package the window needs; the command line needs
none.

```bash
python3 -m pip install pywebview
```

```bash
python3 importer/rsl_import_gui.py
```

On Windows: `py -m pip install pywebview`, then `py importer\rsl_import_gui.py`. Or use the
built app, which needs no Python at all (see "Building the app").

- **Library** lists the games already on the target (`switch/renpy-loader/games/<id>/`),
  read from each `manifest.json`. Click a card for its report and *Delete…* (see "Deleting a
  game"). *Compare with folder…* checks a folder of games on the PC against it (see
  "Comparing with a folder of games"). The status chip is:
  - *Experimental*: no warnings and no Switch performance issues;
  - *Review*: experimental, with warnings or performance issues to read first;
  - *Unsupported*: the manifest's `reasons` say why;
  - *No manifest*: copied by hand, or not by this importer.

  Each card shows the game's cover as the loader does (the placeholder letter without one;
  over Wi-Fi they arrive one by one after the grid). The game's card also shows its
  background, with *Change cover…* and *Change background…*: pick a picture, and it is
  converted and written to the SD card or the Switch at once, as a chosen picture (see
  "Cover and backdrop"). Games without a `manifest.json` cannot have pictures.
- **Import**:
  1. Drop a game folder, `.zip`, `.7z` or `.rar` on the window, or use *Choose folder…* /
     *Choose archive…*. It is analyzed at once.
  2. Options: *Optimize* is `--optimize`, *Recompile* is `--recompile`, *Decompile* is
     `--decompile` with the decompiler chosen under it (`--decompiler`; each shows its pros
     and cons), *Game id* is `--id` (blank: made from the title). Changing one clears the
     report; press *Analyze* again. When the analysis finds something an option fixes (videos
     above 30 fps; a Ren'Py 7 game with its `.rpy` sources; one with scripts that were never
     decompiled), an alert under that option says why, and its *Turn on* button ticks the
     option and analyzes again. The importer decides this (`suggested_options`).
  3. *Tools* shows whether 7-Zip, `ffprobe`, `ffmpeg`, unrpyc and rpycdec were found.
     *Tools folder…* is `--tools`.
  4. The report is the command line's: status, Ren'Py version, the pictures, warnings,
     Switch performance issues, and the `Checked:` line (loose, archived and locked assets).
     Above it, the cover and background made from the game's menu art, with *Change cover…*
     and *Change background…* (`--cover`, `--backdrop`); a chosen picture is written on
     export.
  5. *Replace* is `--replace`. *Export* copies file by file with a progress bar. Re-encoding a
     video can take minutes, and the status line says so. *Cancel* stops after the current
     file: the half-made copy is removed, and an existing export stays as it was. Closing the
     window during an export does the same before the program quits.
- **Target**, where the library is read from and games go:
  - *SD card*: choose its top folder, `E:\` on Windows or `/Volumes/<name>` on macOS. A
    mounted drive that already has a `switch/` folder is offered, and chosen if it is the
    only one.
  - *Switch over Wi-Fi (FTP)*: type the address sphaira shows, for example
    `192.168.1.20:5000`, and press *Connect*. The library is read from the Switch, and
    *Export* uploads to it, with the same progress bar (the status line also shows how much
    of a large file is sent) and *Cancel*, which stops within the current file.

Export is disabled for `unsupported` games. `--rpycdec-python` is only on the command line.

Text from a game (title, report, paths) is put on the page as plain text, never as HTML: a
title in `options.rpy` cannot inject script into a page that can export.

On macOS's `/usr/bin/python3` (3.9), `pip` may pick a yanked `pyobjc` 12.0 and compile it,
which needs the Xcode Command Line Tools. It worked here, but the built app or Python 3.10+
from python.org avoids that.

## Windows setup

1. Either use `RSL Importer.exe` (no Python needed), or install Python 3.9+ from
   python.org and then `py -m pip install pywebview`. The window uses Microsoft Edge
   WebView2, which Windows 11 has; on Windows 10, install the "Evergreen" WebView2 Runtime
   from Microsoft if the window does not open.
2. 7-Zip, from 7-zip.org. It is found in `C:\Program Files\7-Zip` without further setup.
3. FFmpeg. `winget install Gyan.FFmpeg` puts it on `PATH`. Or unzip a build linked from
   ffmpeg.org and choose its `bin` folder with *Tools folder…* (`--tools C:\ffmpeg\bin`).
4. Put the SD card in the PC and choose its drive, for example `E:\`. On the command line a
   bare `--out E:` also means the drive's top folder, not its current folder.

What differs on Windows:
- Archive entries that Windows cannot create under their own name are refused: device names
  (`CON`, `NUL.txt`, `COM1`, …), `:` (it would write an alternate data stream), `<>"|?*`, and
  names that end in a dot or a space (Windows strips those, so `.. ` would become `..`).
- 7-Zip, FFmpeg, unrpyc and rpycdec run without a console window popping up.
- 7-Zip lists names in UTF-8 (`-sccUTF-8`), not in the console code page.
- Read-only files from an archive are deleted with the temp folder.
- Report text the console code page cannot show (a Japanese title in a redirected
  `cp1252` console, for example) prints as `?` instead of stopping the importer.
- Paths longer than 260 characters need Windows' long-path setting (`LongPathsEnabled`);
  the importer does not work around it.

## Tools

`7z`, `ffprobe` and `ffmpeg` are searched for in this order:
1. the `--tools` folder (in the window: *Tools folder…*);
2. `PATH` (on Windows, with `.exe` added);
3. the usual install places: `/opt/homebrew/bin`, `/usr/local/bin`, and
   `C:\Program Files\7-Zip`. An app started from the macOS Finder only gets
   `/usr/bin:/bin:/usr/sbin:/sbin` as `PATH`, so Homebrew tools are found through these.

Missing 7-Zip only matters for `.7z`/`.rar` files. Missing `ffprobe` leaves videos unchecked,
with a warning. Missing `ffmpeg` means `--optimize` copies videos unchanged and notes it.

The tools are not bundled with the app. FFmpeg (LGPL or GPL, depending on the build) and
7-Zip (LGPL, plus the unRAR licence terms) have their own licences, and installing them
separately keeps those terms off the app and lets users update them.

## Building the app

For people without Python, PyInstaller turns the window into one program:

```bash
python -m pip install pyinstaller pywebview
```

```bash
python importer/build_app.py
```

- On Windows: `dist\importer\RSL Importer.exe`, a single file.
- On macOS: `dist/importer/RSL Importer.app` (24 MB; built with Python 3.12, PyInstaller and
  pywebview 6.2.1, launched on macOS 15.7).
- The page, `rsl_import_gui.html`, is bundled as data; PyInstaller's pywebview hook adds
  pywebview's own scripts.
- PyInstaller does not cross-compile: build the `.exe` on Windows and the `.app` on a Mac.
- Build files go to `build/test/pyinstaller/`. Both folders are git-ignored.
- The app is not signed. Windows SmartScreen shows "Windows protected your PC": click *More
  info*, then *Run anyway*. On macOS, right-click the app and choose *Open* the first time.

## What gets exported

Only the game directory is copied, and it is always named `game/` in the export. It goes to
`switch/renpy-loader/games/<id>/game/`, with `manifest.json` next to it, and `cover.jpg` and
`backdrop.jpg` when there are pictures (see "Cover and backdrop").

These are not copied:
- `renpy/`, `lib/` and the `.exe`/`.app`/`.sh` launchers (the Switch runtime has its own engine);
- `game/saves/` (the developer's local saves);
- symlinks, `.DS_Store`, `._*` and `__MACOSX`.

How `--replace` works:
- The new `game/` is staged next to the export and swapped in by rename.
- An existing `game/saves/` is moved into the new `game/`. Ren'Py on the console writes saves
  there as well as to `saves/<id>`.
- A cover or backdrop chosen earlier (`"cover_custom": true` in the old manifest: in the window,
  with `--cover`, or on the Switch) is kept, unless this export chooses one too.
- `switch/renpy-loader/saves/` is never touched.

## Cover and backdrop

What the loader shows for a game (docs/LOADER_UI.md "Covers"):

| File | Size | Where the loader shows it |
|---|---|---|
| `cover.jpg` | 300×450 (2:3) | The library (all layouts) and the detail page |
| `backdrop.jpg` | 640×360 (16:9) | Behind the detail page and the Cinematic layout |

- Both are baseline JPEGs (the Switch's decoder reads no progressive JPEG), made with FFmpeg:
  `scale=W:H:force_original_aspect_ratio=increase,crop=W:H`, one frame, `yuvj420p`. Any
  picture FFmpeg reads works as a source (PNG, JPEG, WebP, BMP); another shape is cut from
  the middle.
- Automatic: the game's main menu art, both pictures from it. That is `gui.main_menu_background`
  in `gui.rpy` when it is a plain string naming an image, else `gui/main_menu.png`, loose or
  inside a standard `.rpa`. Nothing found: no pictures, and the report says `Pictures: none`;
  the loader shows a placeholder. FFmpeg missing: no pictures, and a warning says so.
- Chosen: `--cover IMAGE` / `--backdrop IMAGE`, *Change cover…* / *Change background…* in the
  window (on the Import page before export, or on a game in the Library at once), or *Change
  picture* on the Switch (a screenshot from the Album, written as `cover.bmp` / `backdrop.bmp`).
  The manifest marks them `"cover_custom": true` / `"backdrop_custom": true`, and `--replace`
  keeps them. A chosen picture that FFmpeg cannot read is refused with its error.
- The report's `Pictures:` line names the files, `(chosen)` for chosen ones.

## Switch over FTP

The window's *Switch over Wi-Fi* target and `--out ftp://HOST:PORT` use sphaira's FTP
server on the Switch (anonymous; port 5000 unless you give another). Turn it on in sphaira
first. It stops while the loader or a game runs, and its address comes from the router, so
it can change. `/` on that server is the SD card. Only Python's `ftplib` is used.

How an upload works:
1. If `games/<id>` exists and *Replace* (`--replace`) is off, nothing is sent.
2. Every file goes to the hidden folder `games/.uploading-<id>/`; `manifest.json` last. The
   loader skips names that start with a dot. A `.uploading-<id>` left by an earlier upload
   is removed first: it never holds saves.
3. On *Cancel*, a lost connection or any error, `.uploading-<id>` is removed over a new
   connection, and the Switch is as it was. If that fails too, the next upload removes it.
4. Then it is renamed into place: the old `games/<id>` becomes `games/.old-<id>`,
   `.uploading-<id>` becomes `games/<id>`, the old `game/saves/` is moved into the new
   `game/`, so are the pictures chosen before (see "Cover and backdrop"), and `.old-<id>` is
   deleted.
5. If a rename in step 4 fails, the error names both folders. A `.old-<id>` left over may
   hold saves, so the next upload of that game refuses until it is moved or deleted by hand.

`switch/renpy-loader/saves/` is never touched. Unlike an SD card export, which only swaps
`game/` and `manifest.json`, an upload replaces the whole `games/<id>/` folder.

Uploads of thousands of small files run at about 1–2 MB/s (one file at a time).

## Deleting a game

*Delete…* on a game in the Library asks first, then removes `games/<id>/`, from the SD card
or over FTP. Its saves stay in `switch/renpy-loader/saves/<id>/` unless *Also delete its
saves* is ticked. The copy in `games/<id>/game/saves/` goes with the game: Ren'Py writes
every save to all of its save folders (`MultiLocation.save` in `renpy/savelocation.py`), so
`saves/<id>/` holds the same saves. Logs in `switch/renpy-loader/logs/<id>/` are kept.
There is no command-line option for it.

## Comparing with a folder of games

*Compare with folder…* in the Library takes a folder of game sources on the PC: game folders
and `.zip`, `.7z` or `.rar` files directly inside it (other files are ignored). It compares
each one with the library on the target (SD card or FTP) and changes nothing on either side.

| State | Meaning |
|---|---|
| *Different* | The library copy no longer matches the source. Details say why: file count, total size, or which `.rpa`/`.rpyc` files differ. |
| *Not on the Switch* | No game in the library matches it. The row shows the id an import would give it. |
| *Only on the Switch* | A game in the library that no source in the folder matches. |
| *Can't compare* | A match without a usable `manifest.json` (copied by hand, or an edited manifest). |
| *Not a game* | The importer cannot read it as a Ren'Py game; the reason is shown. |
| *Same* | Matches what the manifest recorded from its source. |

How a source is matched and checked:
- It is matched to the export whose `manifest.json` names the same source (`source.name`, the
  folder or archive name). Otherwise, to the export with the id an import would give it, from
  `config.name` or the folder name. A match by id says which source the library copy came
  from, for example an older `Game-1.0.zip` next to `Game-1.1.zip`. A game exported with its
  own `--id` and then renamed matches neither way.
- The same rules as an import pick the game folder and its files. A `--recompile` export is
  compared without `.rpyc` files; for a `--decompile` export the total size is skipped,
  because the decompiler wrote some of the exported files.
- The file count and total size are compared first. If both match, every `.rpa` and `.rpyc`
  is hashed (SHA-256) and compared with the manifest's `sha256`. A change in other files
  (images, audio, `.rpy`) that keeps the file count and total size is not found.
- Archives are unpacked into a temporary folder, one at a time, as an import does. A folder of
  large archives takes minutes. *Cancel* stops after the current game.

## Game id and title

- **Title:** `config.name` from `game/options.rpy`. If that is missing, the folder name (or
  the archive name).
- **Id:** made from the title, or from the folder name if the title has no ASCII letters or
  digits.
  - It is lowercased; `a-z` and `0-9` are kept, and every other run of characters becomes
    one `-`.
  - It is trimmed and capped at **63** characters. The loader skips folder names of 64 bytes
    or more (`app/launcher/main.c`).
  - If nothing is left, it becomes `game`.
  - Examples: `The Question` → `the-question`; `ドキドキ` → `game`.

The id is the folder name, and so it is the save and log folder name too. If two games end up
with the same id, use `--id` for the second one.

## Checks

| Check | How | Effect |
|---|---|---|
| Game directory | Ren'Py's own `path_to_gamedir` rule: launcher names (`<Name>.exe/.sh/.py/.app`, also split at spaces and `_`), then `game`, then `data`, then the folder itself if it holds scripts. It descends through one wrapper folder or `<Name>.app/Contents/Resources/autorun`. | No `.rpy/.rpyc/.rpa` found → refused |
| Script counts | `.rpa`, `.rpy`, `.rpyc`, `.rpym` under `game/` | Manifest `files` |
| Ren'Py version | `renpy/vc_version.py` (`version = '8.5.3…'`, 8.1+). Then a compiled `vc_version.pyc`. Then `version_tuple = (7, 4, 11, …)` in `renpy/__init__.py`; the 7.5/8.0 file has both tuples, and the Python hint picks one. For a folder with no `renpy/`, the parent's `renpy/` is used (SDK projects), with a warning. | Decides the status |
| Python generation | Version first (8 → 3, ≤7 → 2). Otherwise `lib/py3-*` vs `lib/py2-*`, `python2.7`, `pythonlib2.7`, `python27.dll`. Otherwise the `.rpyc` files: `RENPY RPC2` header, then the pickle protocol of slot 1 (≥3 → Python 3; 2 or older → Python 2). Only 2 bytes are decompressed. | Conflicting hints → warning |
| Custom Python | `game/python-packages/`, `*.py` in `game/` (not `*_ren.py`) | Warning: pure Python 3.12 only; no `ctypes`, `ssl`, `sqlite3`, `mmap`, `subprocess` or `multiprocessing` on the Switch |
| Native code | `*.pyd`, `*.dll`, `*.so`, `*.dylib` in `game/` | Warning: cannot load on the Switch |
| Archive format | `.rpa` header is `RPA-3.0` or `RPA-2.0` | Other header → warning: a custom format that needs the game's own loader |
| Media | Counts audio and video files by extension. The runtime's FFmpeg decodes practically every format (WebM/VP9, MP4/H.264, HEVC, AV1, AVI, WMV, MPEG and others); only MIDI is refused, because Ren'Py cannot play it. | Warning for `.mid`. Files ≥ 2 GiB → warning (FAT32 cannot hold files ≥ 4 GiB). Decoding cost: see "Switch performance" |
| Non-ASCII names | Any exported path with non-ASCII characters | The Switch SD parses names as Shift-JIS and lists none of them (hardware run 20). So the export packs these files into `game/rsl-nonascii.rpa` (a new archive every 2 GiB: `rsl-nonascii-2.rpa`, …), with names in NFC, and Ren'Py reads them from there. Scripts (`.rpy`, `.rpym`, `_ren.py`) and `.rpa` files stay loose, because Ren'Py loads none of them from an archive: warning |

## Status

| Detected | `status` | `runtime` |
|---|---|---|
| Ren'Py 8.x | `experimental` | `renpy-8.5.3` |
| Ren'Py 8.0–8.4 | `experimental`, plus a warning (8.5.3 loading older `.rpyc` is untested) | `renpy-8.5.3` |
| Version unknown, Python 3 `.rpyc` | `experimental`, plus a warning | `renpy-8.5.3` |
| Ren'Py ≤ 7.x, or Python 2 | `unsupported` (no Python 2 runtime yet) | `null` |
| Ren'Py ≤ 7.x with `--recompile` | `experimental`, plus warnings | `renpy-8.5.3` |
| Ren'Py 9+, or nothing identifiable | `unsupported` | `null` |

The importer never writes `verified`. Only a hardware test can do that (`docs/HARDWARE_TESTS.md`).
It does not export `unsupported` games, because no runtime could start them.

## Manifest (`manifest.json`, version 1)

```json
{
  "format": "renpy-switch-loader/manifest", "version": 1,
  "id": "the-question", "title": "The Question",
  "source": {"type": "directory", "name": "the_question"},
  "renpy_version": "8.5.3.26051504", "python": "3",
  "runtime": "renpy-8.5.3", "status": "experimental",
  "reasons": [], "warnings": ["..."],
  "files": {"count": 172, "total_bytes": 10637851, "rpa": 0, "rpy": 52, "rpyc": 52, "rpym": 1},
  "media": {".opus": 1},
  "recompile": false, "decompiled": [],
  "suggested_options": {"optimize": "1 videos above 30 fps (gui/splash.webm): re-encoding them …"},
  "performance": {"issues": ["..."], "converted": {"gui/splash.webm": "vp9"},
                  "coverage": {"loose_media": 3190, "archived_media": 0, "locked_archives": []}},
  "sha256": {"script.rpyc": "…"},
  "cover": "cover.jpg", "backdrop": "backdrop.jpg"
}
```

- `reasons` explains why a game is `unsupported`.
- `sha256` lists every `.rpa` and `.rpyc`, by path relative to `game/`.
- `recompile` is `true` for a `--recompile` export.
- `suggested_options` names the importer options that would fix something found, each with
  why. `optimize`: videos above 30 fps that `--optimize` can re-encode (not alpha, a codec it
  has an encoder for). `recompile`: a Python 2 game whose every `.rpy` source is there and
  that has no `.rpa` (what `--recompile` needs). `decompile`: a Python 2 game without `.rpa`
  whose compiled scripts have no `.rpy` source. It is empty once the option is on. The
  report prints it as `Suggested: --optimize: …`, and the window shows it as an alert under
  that option.
- `decompiled` lists the `.rpy` files that `--decompile` rebuilt (paths relative to `game/`).
- `cover` and `backdrop` name the pictures, relative to the game's folder; absent without one.
  `cover_custom` / `backdrop_custom` are `true` for a picture the user chose (see "Cover and
  backdrop"). The loader's *Change picture* sets `.bmp` files there and rewrites the
  manifest with cJSON (tabs, the keys moved to the end); the rest stays as it was.
- The loader reads `status`, `runtime`, `renpy_version`, `recompile`, `reasons[0]`, `title`,
  `cover`, `backdrop` and `performance.issues`:
  - its Game Library shows each game's Ren'Py version, status, runtime, and whether that
    runtime is installed;
  - Launch starts `runtimes/<runtime>.nro`. `renpy-8.5.3` falls back to
    `runtimes/renpy-runtime.nro`;
  - it refuses `unsupported` games, and games whose runtime is missing, with the reason;
  - a game without a manifest (copied by hand) uses the default runtime.

## Switch performance

The Switch decodes video in software, on a ~1 GHz Cortex-A57. Its screen is 1280×720 in
handheld mode and 1920×1080 docked, and its GPU's largest texture is 4096×4096. The
importer flags what loads the console. The findings go into `performance.issues` in the
manifest, and the loader marks such games with `!` and shows the reasons.

Checks (videos need `ffprobe`; images are read from their headers). They cover loose files
**and** the contents of standard `.rpa` archives:
- RPA-3.0 and RPA-2.0, Ren'Py's own formats, read as `renpy/loader.py` reads them;
- the index (a pickle) is interpreted opcode by opcode with `pickletools`. Only plain data
  (dicts, lists, tuples, strings, bytes, ints) is accepted, and an index that would import or
  call anything is refused. Nothing is unpickled or executed.

Archives in any other format count as **locked**. Their contents are not checked, and the
importer does not try to open them: no decryption, no format cracking. The report lists
them, and the loader shows them as an issue. The `Checked:` line in the report (and
`performance.coverage` in the manifest) gives:
- the number of loose and archived images/videos checked;
- the locked archives.

| Finding | Why it matters | Fixed by `--optimize` |
|---|---|---|
| Video above 30 fps | Every extra frame costs CPU and heat; the screen gains little | Yes: re-encoded at 30 fps, same size. From an archive, the result is exported as a loose file; Ren'Py loads files on disk before archives, and the archive is copied unchanged |
| Video above 1920×1080 | Too heavy to decode smoothly | No (see below) |
| Video above 1280×720 | About twice the decoding work of 720p | No (see below) |
| HEVC or AV1 video | The most expensive codecs to decode in software | No |
| Image above 4096 pixels on a side | Over the GPU texture limit; may not display | No |
| Locked archive | Its assets cannot be checked | No |

How `--optimize` re-encodes (FFmpeg on the desktop; the Switch only plays the result):
- It keeps the codec family, because the game refers to the file by name. VP9 → libvpx-vp9
  at CRF 31 with tiles (`-tile-columns 2`), so the Switch can decode on several cores. VP8 →
  libvpx. H.264 → libx264 at CRF 20, profile high.
- Constant quality (CRF), not a fixed bitrate, so the picture stays sharp. Audio is copied
  unchanged.
- It skips videos with an alpha channel, and codecs it has no encoder for, and reports them.
- It changes only the export. The source is never touched.

Why sizes are not shrunk automatically: Ren'Py draws a `Movie` without `size=`, and any
image, at the file's own pixel size. Halving a 1080p splash to 720p would show it at two
thirds of the screen. Shrinking needs a per-game check, so the importer only reports it.

## Recompile mode (`--recompile`)

For Ren'Py 7.x games (Python 2) that ship their `.rpy` sources. The export leaves out every
`.rpyc`/`.rpymc` (Python 2 pickles that Ren'Py 8 cannot load) and `game/cache/`. Ren'Py 8.5.3
then compiles the sources with Python 3 on the first launch, which is slow once. The source
folder is not changed.

The importer refuses recompile mode when:
- the game is not Python 2;
- it has `.rpa` archives, which may hold compiled scripts;
- a `.rpyc` outside `tl/` has no `.rpy` source, unless `--decompile` is given. The refusal
  says to add it.

Without `--decompile`, sourceless `.rpyc` under `tl/` are left out with a warning: those
translations may be missing.

### Decompile (`--decompile`)

For Ren'Py 7 games that ship only `.rpyc` (some or all scripts have no `.rpy`).
`--decompile` rebuilds a `.rpy` for every `.rpyc`/`.rpymc` that has no source, including
those under `tl/`, and then exports as `--recompile` does (it implies `--recompile`). The
analysis suggests it (`suggested_options`) when a Python 2 game without `.rpa` has
compiled scripts that were never decompiled. Two decompilers can do it; `--decompiler`
picks one, and the window offers both under *Decompile*:

| | unrpyc (default) | rpycdec |
|---|---|---|
| Project | [CensoredUsername/unrpyc](https://github.com/CensoredUsername/unrpyc) v2.0.4, MIT. The long-standing Ren'Py decompiler (since 2012). | [cnfatal/rpycdec](https://github.com/cnfatal/rpycdec), MIT. Younger and smaller. |
| Install | Not on pip: `third_party/unrpyc/fetch.sh` (pinned, checksummed) puts it in `build/unrpyc/unrpyc/`. | `pip install rpycdec`. |
| Python | The importer's own, 3.9+ (`/usr/bin/python3` on macOS works). | 3.10+, so not `/usr/bin/python3`; give it with `--rpycdec-python PYTHON` (command line only) or put `rpycdec` on `PATH`. |
| Ren'Py | Made for Ren'Py 8; reads 7, and 6 from 6.18. Older games are refused. | Ren'Py 7 and 8, tested on 7.6.3, 8.2.3, 8.4.1 and 8.5.3 by recompiling its output and comparing. |
| A script it cannot read | It names the script and decompiles the rest. | It stops there: the rest are not decompiled, and the import is refused. |
| A statement it cannot read | Left as `pass # <<<COULD NOT DECOMPILE: …>>>`. | Left as `# <unrecognized: …>`. |
| Side effects | None outside its folder. | Its package adds stand-in `renpy` and `store` modules to the Python it is installed in. |
| Packaged app | Cannot run it (it needs a Python); run `importer/rsl_import_gui.py`. | Runs if `rpycdec` is on `PATH` or in the *Tools folder…*. |

So: unrpyc first. If it cannot read a script, try rpycdec, and the other way round.

```bash
sh third_party/unrpyc/fetch.sh            # once, for unrpyc
python3 importer/rsl_import.py OldGame --decompile --out /Volumes/SD
python3 importer/rsl_import.py OldGame --decompile --decompiler rpycdec --rpycdec-python ~/py312/bin/python
```

On Windows without `sh`, unpack the v2.0.4 source zip from unrpyc's releases page into
`build\unrpyc\unrpyc\` so that `build\unrpyc\unrpyc\unrpyc.py` exists.

How it is kept apart from the importer:
- Both unpickle the `.rpyc` files with restricted unpicklers, and rpycdec's README warns that
  no safeguard is perfect. So neither runs inside the importer. The importer copies only the
  sourceless `.rpyc` into its temp folder and runs the decompiler on that folder as a
  separate process (stdin closed, `DECOMPILE_TIMEOUT` = 600 s). The working directory holds
  only the copies.
- unrpyc runs with `--no-init-offset`: its guess at `init offset` statements is only safe for
  Ren'Py 8, and without it the output is the same game, only more verbose.
- The source game is never touched. The rebuilt `.rpy` are exported, and every `.rpyc` is
  still left out.
- `.rpyc` that have a `.rpy` source are not decompiled.
- A script the decompiler cannot read is named. Outside `tl/` the import is refused (the game
  needs it); under `tl/` it is left out with a warning (that translation may be missing).
  The import is also refused if the decompiler does not finish in time.
- Only scripts are decompiled. Archives are not touched: recompile mode still refuses any game
  with `.rpa` archives. There is no decryption, and no other tool of either project is used
  (rpycdec's save re-signing, unrpyc's `--try-harder` deobfuscation).

Decompiled scripts may differ from the originals. Comments and formatting are lost, and
statements can be misread. The manifest lists the rebuilt files under `decompiled`, and a
warning says the same and names the decompiler. A second warning names files with
statements it could not read; it leaves those as comments. Play the whole game to check.

It is an experiment, not support:
- Python 2 code can behave differently under Python 3 **without any error**. For example,
  Ren'Py 7 scripts use Python 2 division unless they declare `rpy python 3`, so `7 / 2` is `3`
  there and `3.5` here. Check the game's logic.
- Saves from the original game do not load.
- Proper support for Ren'Py 7 is a separate Python 2.7 runtime (`docs/COMPATIBILITY.md`,
  phase 3).

## Archive safety

Every archive is checked before anything is written. It is then unpacked into a fresh temp
folder, which is deleted afterwards. The importer refuses an archive that has:

- an absolute path, a drive letter, or a `..` component;
- on Windows, a name Windows cannot create as written (see "Windows setup");
- a password;
- a symlink that points outside the archive. A zip symlink inside the archive is skipped with
  a warning. A `.7z`/`.rar` file with any link is refused, because 7z gives no link target
  before extracting;
- more than `MAX_FILES` entries (200,000);
- more than `MAX_TOTAL_BYTES` uncompressed (32 GiB);
- an entry over 1 MiB that compresses better than `MAX_RATIO` (200:1). For solid 7z/RAR,
  the whole archive is checked against its file size.

After unpacking, the tree is walked again. Any symlink, hard link or limit breach found then
refuses the import.

The limits are constants at the top of `importer/rsl/archives.py`.

## Limits

- No `.tar.bz2` (Ren'Py's Linux format) or `.dmg`. Unpack these first and import the folder.
- `.7z`/`.rar` depend on the installed `7z`. If 7z cannot open a RAR, the importer says so;
  unpack it with another tool and import the folder.
- The 7z size limits trust 7z's listing. A lying header is caught only after extraction.
- Scripts inside `.rpa` are not inspected; only images and videos are. A game that ships
  only `.rpa` and no `renpy/` folder may have an unknown Python generation, which makes it
  `unsupported`.
- `--recompile` needs `.rpy` sources. For Ren'Py 7 games that ship only loose `.rpyc`, add
  `--decompile` (needs unrpyc or rpycdec). Scripts inside `.rpa` archives cannot be recompiled at all.
- The Python modules in `game/` are not scanned for imports of modules the Switch lacks.
- The media check only looks at file extensions. The runtime's FFmpeg codec list is not
  confirmed on hardware.
- Windows support is tested on macOS with Windows faked (`WINDOWS`, `ntpath`, a `cp1252`
  console, mocked tool calls), not yet on a Windows machine.
- The window has run on macOS only. On Windows, its WebView2 backend (via pythonnet) and the
  one-file `.exe` are untested.
- Run the tests with `/usr/bin/python3 -m unittest tests/unit/test_importer.py
  tests/unit/test_importer_gui.py`. The window tests drive `Api` without a window, so they
  need no pywebview. On Windows:
  `py -m unittest tests\unit\test_importer.py tests\unit\test_importer_gui.py`.
