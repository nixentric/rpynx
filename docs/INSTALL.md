# Installing

Requires a Switch that can run homebrew (Atmosphère + Homebrew Menu).

1. Copy `dist/renpy-loader.nro` to `sdmc:/switch/renpy-loader/renpy-loader.nro`.
   Copy `dist/renpy-runtime.nro` to `sdmc:/switch/renpy-loader/runtimes/renpy-runtime.nro`.
2. Launch it from the Homebrew Menu. On first start it creates:

```
sdmc:/switch/renpy-loader/
├── renpy-loader.nro
├── runtimes/renpy-runtime.nro   embedded Python (Ren'Py engine from Milestone 4)
├── games/                 one folder per game (each must contain game/)
├── config/controller/
│   ├── global.json        written when you save controller settings
│   └── games/<name>.json  per-game controller profiles
├── cache/                 scratch space (self-test report)
└── logs/                  loader.log, runtime.log
```

**Memory:** started from the album, homebrew runs as a library applet with limited memory.
Ren'Py games need the full application memory: start the Homebrew Menu by holding R while
launching an installed game (title override). The Diagnostics screen shows which mode you are
in. **The loader does not start games in applet mode:** there, a game crashed the whole console
twice (hardware runs 8 and 26). The Python self-test still runs.

**Python self-test:** Diagnostics → A starts the runtime, runs the Python self-test, and
returns to the loader when you press +. This needs the loader to be started from the
Homebrew Menu, because chain-loading goes through the Homebrew loader.

## Games

Add games with the macOS importer (`importer/rsl_import.py`, see [IMPORTER.md](IMPORTER.md)).
It checks a DRM-free Ren'Py game and exports `games/<id>/game/` with a `manifest.json` to the
SD card. Ren'Py 8 games are `experimental`; Ren'Py 7 / Python 2 games are `unsupported` unless
exported with `--recompile`.

The Game Library shows each game's Ren'Py version, status and runtime, and marks games with
Switch performance issues (`!`). Launch starts the game's runtime. The loader refuses
`unsupported` games, games whose runtime is missing, and folders without a `game/` subfolder.
A game copied by hand without a manifest uses the default runtime.

## Controls

The loader's own menus use the same mapping as games will. Defaults:

| Button | Action | Button | Action |
|---|---|---|---|
| A | Confirm / next | ZL | Toggle auto-forward |
| B | Back / cancel | ZR (hold) | Skip |
| X | Hide dialogue UI | + | Game menu |
| Y | History | − | Quick menu |
| L | Rollback | D-pad / left stick | Navigate |
| R | Roll forward | Right stick | Virtual mouse cursor |
| Left stick press | Loader overlay (in games, not built yet) | Right stick press | Mouse click |

**Controller Settings** (main menu): select an action, press A, then press the new button.
Conflicting assignments are marked and cannot be saved until resolved. Y cycles presets
(default, left-handed mirror, one-handed right), X resets to default, + saves. The deadzone,
cursor speed and acceleration, repeat timing, stick roles and cursor visibility are at the bottom.

**Per-game profile:** in the Game Library, select a game and press X.

**Emergency recovery:** hold **+ and − together for 3 seconds** anywhere. Default controls are
restored immediately. In the loader the settings screen opens; press + to save them. In a game
they are saved at once, to the game's profile and to the loader's `global.json`, so the loader
uses them too; each previous file is kept as `.bak`. No setting can disable this. A single detached Joy-Con has only one of + / −, so it cannot do this.

Profiles are plain JSON (format in `config/profiles/default.json`). A damaged file is ignored:
the loader falls back to the previous version (`.bak`), then to defaults.
