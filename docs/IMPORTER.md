# Importer (macOS)

`importer/rsl_import.py` checks a DRM-free Ren'Py game and copies it into the layout the
Switch loader expects. It needs only the macOS system Python (`/usr/bin/python3`, 3.9+).
It also needs `7z` for `.7z` and `.rar` files (`brew install p7zip`).

The importer never runs game code and never unpickles anything. It never changes the source
folder or archive. It does not decrypt or unpack `.rpa` archives.

## Usage

```bash
python3 importer/rsl_import.py SOURCE                    # report only
python3 importer/rsl_import.py SOURCE --out /Volumes/SD  # report + export
```

`SOURCE` can be:
- a game folder (the distribution root, an SDK project, or the `game/` folder itself);
- a `.zip`, `.7z` or `.rar` file.

| Option | Meaning |
|---|---|
| `--out DIR` | Export to `DIR/switch/renpy-loader/games/<id>/` (`game/` + `manifest.json`). |
| `--id ID` | Choose the folder name yourself. It must already be in normalised form (see below). |
| `--replace` | Update an existing export. Without it, the importer refuses if `games/<id>` exists. |
| `--optimize` | Re-encode videos above 30 fps at 30 fps, at the same size and constant quality. Needs FFmpeg. See "Switch performance". |
| `--recompile` | Ren'Py 7 / Python 2 games only. Exports without `.rpyc`, so Ren'Py 8.5.3 compiles the `.rpy` sources with Python 3 (see below). |

Exit status: `0` OK; `1` refused (unsafe archive, not a Ren'Py game, unsupported game,
or the export already exists); `2` bad arguments.

Example, on the Ren'Py demo shipped in this repo:

```bash
python3 importer/rsl_import.py build-test/sdk/the_question --out /tmp/sd
```

```text
The Question  (id: the-question)
  Ren'Py:  8.5.3.26051504   Python: 3
  Status:  EXPERIMENTAL on runtime renpy-8.5.3
```

## What gets exported

Only the game directory is copied, and it is always named `game/` in the export. It goes to
`switch/renpy-loader/games/<id>/game/`, with `manifest.json` next to it.

These are not copied:
- `renpy/`, `lib/` and the `.exe`/`.app`/`.sh` launchers (the Switch runtime has its own engine);
- `game/saves/` (the developer's local saves);
- symlinks, `.DS_Store`, `._*` and `__MACOSX`.

How `--replace` works:
- The new `game/` is staged next to the export and swapped in by rename.
- An existing `game/saves/` is moved into the new `game/`. Ren'Py on the console writes saves
  there as well as to `saves/<id>`.
- `switch/renpy-loader/saves/` is never touched.

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
| Non-ASCII names | Any exported path with non-ASCII characters | Warning: creating such names on `sdmc:` failed in hardware run 4 |

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
  "recompile": false,
  "performance": {"issues": ["..."], "converted": {"gui/splash.webm": "vp9"},
                  "coverage": {"loose_media": 3190, "archived_media": 0, "locked_archives": []}},
  "sha256": {"script.rpyc": "…"}
}
```

- `reasons` explains why a game is `unsupported`.
- `sha256` lists every `.rpa` and `.rpyc`, by path relative to `game/`.
- `recompile` is `true` for a `--recompile` export.
- The loader reads `status`, `runtime`, `renpy_version`, `recompile` and `reasons[0]`:
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
- a `.rpyc` outside `tl/` has no `.rpy` source.

Sourceless `.rpyc` under `tl/` are left out with a warning: those translations may be missing.

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

The limits are constants at the top of `rsl_import.py`.

## Limits

- No `.tar.bz2` (Ren'Py's Linux format) or `.dmg`. Unpack these first and import the folder.
- `.7z`/`.rar` depend on the installed `7z`. If 7z cannot open a RAR, the importer says so;
  unpack it with another tool and import the folder.
- The 7z size limits trust 7z's listing. A lying header is caught only after extraction.
- Scripts inside `.rpa` are not inspected; only images and videos are. A game that ships
  only `.rpa` and no `renpy/` folder may have an unknown Python generation, which makes it
  `unsupported`.
- `--recompile` needs `.rpy` sources. Ren'Py 7 games that ship only `.rpyc` are refused (a
  decompiler such as rpycdec could be added as an optional step).
- The Python modules in `game/` are not scanned for imports of modules the Switch lacks.
- The media check only looks at file extensions. The runtime's FFmpeg codec list is not
  confirmed on hardware.
- Run the tests with `/usr/bin/python3 -m unittest tests/unit/test_importer.py`.
