# Architecture

## Layers

```
Physical Switch input (libnx HID: npad buttons, analog sticks, touchscreen)
        │  platform/input/input_switch.c        the only libnx-dependent input file
        ▼
Device input normaliser  rsl_normalize_npad()  Nintendo labels, never SDL gamepad names
        ▼
Controller mapping engine  rsl_input_update()  bindings, deadzone, repeat/hold, cursor, emergency combo
        ▼
Logical actions (confirm, back, rollback, skip, ...)
        ▼
Consumer: the loader UI today; the Ren'Py event bridge from Milestone 4
```

The mapping engine (`platform/input/input.c`, `input_config.c`) and the filesystem helpers
(`platform/filesystem/fs.c`) are portable C with no libnx dependency, so the host test
suite (`scripts/test.sh`) runs the same code that ships in the `.nro`. The libnx bit layout
the normaliser relies on is checked against libnx's own constants by `_Static_assert`s in
`input_switch.c`, so a libnx change breaks the build rather than silently swapping buttons.

| Path | Role |
|---|---|
| `app/launcher/main.c` | Loader UI (text console for now): library list, controller settings, controller test, diagnostics |
| `app/diagnostics/log.c` | Line-flushed log at `sdmc:/switch/renpy-loader/logs/loader.log` plus an in-memory tail |
| `platform/input/` | Normaliser, mapping engine, JSON persistence, libnx glue |
| `platform/filesystem/` | Atomic writes with `.bak` fallback, bounded reads, `mkdir -p` |
| `config/profiles/` | Sample controller profiles (identical to the built-in presets, checked by tests) |
| `third_party/cjson/` | cJSON 1.7.18, MIT, vendored unmodified |
| `tests/unit/` | Host unit tests |
| `scripts/` | `build.sh` (pinned container build), `test.sh` (host tests) |

| `runtime/main.c` | Runtime `.nro`: embeds CPython, runs a script (later: a Ren'Py game) |
| `runtime/python/` | Static CPython 3.12.8 cross build: patches, `config.site`, stdlib packer, self-test ([README](../runtime/python/README.md)) |
| `docker/Dockerfile` | Pinned devkitA64 image plus host headers needed to build the host CPython |

## Process model

The loader and the runtime are separate `.nro` files. To start a game, the loader calls
libnx `envSetNextLoad(runtime, "runtime --return <loader> <args>")` and exits. The Homebrew
loader then starts the runtime as a fresh process. On exit, the runtime calls
`envSetNextLoad(<loader>)`, which brings the user back to the library.

Reasons:
- Every game gets a clean process and address space. SnepOMatic reports Mesa/nouveau
  faults when initialised twice in one process.
- A crashed game cannot take the loader's state with it.
- Per-game runtime selection is just a different file in `runtimes/`.

The cost is a few seconds of `.nro` load time per launch. Not measured yet.

Directories from the target layout (`runtime/`, `importer/`, `platform/graphics`, …) are
created when the milestone that fills them starts, not before.

## Key decisions

**Input does not go through SDL's gamepad layer.** SDL's GameController API names buttons by
position (Xbox layout), so Nintendo A becomes SDL "B". Reading libnx HID directly and mapping
Nintendo labels avoids the swap entirely. Consequence for Milestone 4: Ren'Py's own
controller handling must be disabled (or SDL's joystick subsystem not initialised) so each
press is not delivered twice. The bridge will turn logical actions into Ren'Py events.

**Controller config is versioned JSON, loaded strictly.** Unknown actions or buttons, wrong
types, versions newer than the loader understands, and configs that leave an essential
action (confirm, back, game_menu, overlay, up/down/left/right) unbound are rejected. Actions
missing from a file keep their default, so adding actions later needs no migration. A
rejected file falls back to `<file>.bak`, then to defaults. Saving refuses conflicting or
incomplete mappings.

**Emergency recovery cannot be remapped away.** Holding + and − together for 3 s is checked
on raw buttons before any mapping. It restores the default controls in memory and opens the
controller settings; the user saves explicitly. Limitation: a single detached Joy-Con has
only one of + / −, so it cannot perform the combo.

**Atomic writes.** Horizon's `rename` does not replace an existing file, so a save is:
write `x.tmp` → fsync → move `x` to `x.bak` → move `x.tmp` to `x`. Any interruption leaves
`x` or `x.bak` readable, and the loader reads `x.bak` when `x` is missing or damaged.

**Resume / reconnect.** On focus regain, resume from sleep and controller style changes, the
engine drops all held actions and ignores buttons that are still physically down until they
are released. This prevents stuck skip/rollback and repeated input after resume.

## Pinned versions

| Component | Version | Source |
|---|---|---|
| devkitA64 / libnx container | `devkitpro/devkita64:20260219`, index digest `sha256:1fc388c3…0282` | Docker Hub, arm64 + amd64 |
| cJSON | 1.7.18 | github.com/DaveGamble/cJSON tag v1.7.18 |
| Ren'Py target (Phase 1) | 8.5.3 (latest stable, 2026-05-16) | planned, Milestone 4 |
| CPython target | 3.12.8 (what renpy-build uses for 8.5.3) | planned, Milestone 3 |

Package versions inside the container are recorded in `BUILD_STATUS.md` from `dkp-pacman -Q`.

## Runtime plan (Milestones 3–4), from reading existing ports

None of the existing ports is a clean base:

- **renpy-build has no Switch target.** Its master branch has moved to SDL3, which devkitPro
  does not package; 8.5.3 still uses SDL2 2.0.20 with pygame_sdl2 merged into `renpy.pygame`.
- **SnepOMatic/RenPy-Switch** (Ren'Py 8.5.3) is the most current port, but it has no license
  file, so its code cannot be reused. It runs CPython 3.13 instead of 3.12, which forced
  workarounds in Ren'Py itself. The general technique is public knowledge and will be
  re-implemented independently:
  - static CPython (`--disable-shared`, no dlopen/mmap, `Setup.local` with every module `*static*`);
  - Cython-generated Ren'Py modules linked into one ELF;
  - a `PyImport_ExtendInittab` table with dotted module names;
  - gl2 on switch-mesa GLES.
- **jvrcruzGAMES/RenpySwitch** has no license (upstream MIT dropped), one game per NRO, and a
  CPython 3.14 / 3.9-stdlib mismatch.
- **uyjulian/renpy-switch** (MIT, archived 2022) targets Ren'Py 7.3.5 / Python 2.7 and is the
  reference for Phase 3.

Planned dependencies from devkitPro portlibs: switch-sdl2, switch-sdl2_image, switch-sdl2_ttf,
switch-mesa, switch-freetype, switch-harfbuzz, switch-libfribidi, switch-ffmpeg, switch-libpng,
switch-libjpeg-turbo, switch-libwebp, switch-zlib, switch-bzip2, switch-liblzma. Not packaged
by devkitPro, so they must be built here: CPython, libffi (only if ctypes is needed; Ren'Py
core does not require it).
