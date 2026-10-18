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
| Media | Counts audio and video by extension. `.ogg .opus .mp3 .wav .flac .webm .ogv .mkv` are expected to work. `.mp4 .m4v .m4a .aac .avi .wmv .wma .mpg .mpeg .mov .flv .mp2 .mid .midi` are not. | Warning for the second group. Files ≥ 2 GiB → warning (FAT32 cannot hold files ≥ 4 GiB) |
| Non-ASCII names | Any exported path with non-ASCII characters | Warning: creating such names on `sdmc:` failed in hardware run 4 |

## Status

| Detected | `status` | `runtime` |
|---|---|---|
| Ren'Py 8.x | `experimental` | `renpy-8.5.3` |
| Ren'Py 8.0–8.4 | `experimental`, plus a warning (8.5.3 loading older `.rpyc` is untested) | `renpy-8.5.3` |
| Version unknown, Python 3 `.rpyc` | `experimental`, plus a warning | `renpy-8.5.3` |
| Ren'Py ≤ 7.x, or Python 2 | `unsupported` (no Python 2 runtime yet) | `null` |
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
  "sha256": {"script.rpyc": "…"}
}
```

- `reasons` explains why a game is `unsupported`.
- `sha256` lists every `.rpa` and `.rpyc`, by path relative to `game/`.
- The loader does not read the manifest yet; it still uses the folder name.

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
- Scripts inside `.rpa` are not inspected. A game that ships only `.rpa` and no `renpy/` folder
  may have an unknown Python generation, which makes it `unsupported`.
- The Python modules in `game/` are not scanned for imports of modules the Switch lacks.
- The media check only looks at file extensions. The runtime's FFmpeg codec list is not
  confirmed on hardware.
- Run the tests with `/usr/bin/python3 -m unittest tests/unit/test_importer.py`.
