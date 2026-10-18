# Ren'Py 8.5.3 for the Nintendo Switch

Builds the official Ren'Py engine into `dist/renpy-runtime.nro` alongside the static CPython
from `runtime/python`.

```bash
scripts/build.sh python
```

```bash
scripts/build.sh renpy
```

```bash
scripts/build.sh runtime
```

## Pinned inputs (sha256-verified by `build.sh`)

| Input | Version | Source |
|---|---|---|
| Ren'Py source | 8.5.3 (`vc_version` 8.5.3.26051504) | `renpy-8.5.3-source.tar.bz2`, renpy.org; sha256 from the PGP-signed `checksums.txt` |
| Ren'Py ARM SDK | 8.5.3 | `renpy-8.5.3-sdkarm.tar.bz2`. Only `renpy/common/*.rpyc` is used, after digest checks |
| Cython | 3.2.4 (the version in Ren'Py's `uv.lock`) | PyPI sdist, run in pure-Python mode |

Ren'Py is MIT-licensed. Its `LICENSE.txt` notes that parts are derived from LGPL code, and the
runtime also links FFmpeg statically. A distributed `.nro` must therefore meet the LGPL
(offer the objects for relinking). This is not done yet; see "Known limitations".

## How the build works (`build.sh`)

1. **Generate C.** Ren'Py's own `setup.py generate` runs Cython into `tmp/gen3`. A
   two-class `setuptools` stand-in (`hoststub/`) lets it run on the bare host CPython.
2. **Compile the native modules.** `build_modules.py` reads the module list from Ren'Py's
   `src/Setup` and compiles every source with devkitA64. Each Cython init function is
   renamed with `-DPyInit_<name>=PyInit_<dotted_name>`. It then generates an inittab that
   registers the 60 modules under their dotted names. The scheme matches renpy-build's
   `librenpy` task (MIT), reimplemented. Output: `build-renpy/dist/lib/librenpy.a`.
3. **Stage the engine for RomFS.** `stage_engine.py` copies `renpy.py` and `renpy/`, then
   the overlays (`runtime/compatibility`, `runtime/renpy/boot`). It writes unchecked-hash
   `.pyc` files and adds the SDK's precompiled `common/*.rpyc` after checking each
   embedded digest against our `.rpy`, which is the same check Ren'Py makes at load.
   Without them, the engine scripts would be recompiled on every launch because RomFS is
   read-only. `make_cursor.py` draws the virtual-cursor PNG.

## Upstream modifications

| Change | Why |
|---|---|
| `patches/0001-libhydrogen-switch-random.diff` | libhydrogen (Ren'Py's crypto) has no Switch random source. Use its getentropy backend; getentropy is backed by libnx `randomGet` |
| `renpy.gl2.assimp` excluded; `runtime/compatibility/renpy/gl2/assimp.py` | assimp (C++) has no Switch port. Ren'Py imports the module unconditionally. The replacement makes the 2D hooks no-ops and makes `GLTFModel` raise a clear error |
| `renpy.gl2.live2dmodel` excluded | The Live2D Cubism Core is proprietary. Ren'Py already treats it as optional |
| `posix_memalign` shim (`runtime/python/nx_shims.c`) | Used by `src/ffmedia.c`; newlib does not provide it |
| CPython `posixpath` patch (`runtime/python/patches/0005`) | `sdmc:/…` and `romfs:/…` must count as absolute, otherwise `renpy.py`'s `abspath(__file__)` breaks |

None of Ren'Py's own `.py` files are modified.

## Boot and controller bridge (`boot/`)

- **`switch_main.py`** is run instead of `renpy.py`. It loads `renpy.py` unchanged, points
  `path_to_logdir` at `sdmc:/switch/renpy-loader/logs/<id>` (so `log.txt` and
  `traceback.txt` never land in the game folder), installs the bridge, and starts Ren'Py.
  The runtime passes `--savedir sdmc:/switch/renpy-loader/saves/<id>` and selects the
  `gles2` renderer.
- **`rsl_bridge.py`** turns the C mapping engine's logical actions into Ren'Py input on
  Ren'Py's own thread. It wraps `renpy.pygame.event.wait/poll`, which read the controllers
  every 16 ms while idle:

| Logical action (default button) | Ren'Py input |
|---|---|
| confirm (A), back (B), hide UI (X), rollback (L), roll forward (R), game menu (+), D-pad / left stick | pad controls `a`, `b`, `y`, `leftshoulder`, `rightshoulder`, `start`, `dpup/down/left/right` via `controller.post_event`. The game's `config.pad_bindings` decide the effect, as with a real pad |
| skip (hold ZR) | Ren'Py's `skip` event: press, then release |
| auto-forward (ZL) | Ren'Py's `toggle_afm` event |
| history (Y), quick menu (−), loader overlay (L-stick press) | `rsl_*` events handled by a Keymap in `config.underlay` |
| virtual cursor (right stick), click (R-stick press) | `draw.set_mouse_pos` and mouse button events; a software `MouseDisplayable` cursor (Switch SDL draws none) |

Ren'Py's own SDL game-controller support is turned off. Every press would otherwise
arrive twice, and with SDL's position-based A/B naming.

## Verified

- [Certain] All 60 native modules compile, and the runtime links with no unresolved symbols.
- [Certain] **Desktop integration test, 24/24 PASS**
  (`tests/integration/renpy_desktop/run.sh`). Setup:
  - the official Ren'Py 8.5.3 Linux ARM SDK, run headless with Xvfb + Mesa llvmpipe and the
    `gles2` renderer;
  - started through `switch_main.py` and `rsl_bridge.py`;
  - a scripted `_rslnx` plays "The Question" with every default control: cursor click,
    dialogue, rollback/forward, hide UI, history, game menu, quick menu, auto, hold-skip,
    D-pad menu choice, save to the per-game folder, emergency notification.

  This verifies the Python boot code and the bridge against the real engine. It runs no
  Switch code.
- **HARDWARE UNVERIFIED:** everything native to the Switch, i.e. engine start-up, GLES on
  nouveau, audio, video, performance and memory. See `docs/HARDWARE_TESTS.md` H26–H35.

## Known limitations

- Live2D and 3D models (`GLTFModel`) are unsupported.
- Ren'Py sees the Switch as Linux (`renpy.linux`); screens use the `pc` variant.
- If a game ships `.rpy` files whose `.rpyc` are missing or stale, Ren'Py writes new `.rpyc`
  files into the game folder, as it does on desktop.
- The loader overlay (L-stick press) only shows a notice; it is Milestone 6.
- The virtual cursor stays visible during D-pad play until Ren'Py's mouse-hide timeout (30 s).
- LGPL relinking materials are not produced yet.
