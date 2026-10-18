# Installing

Requires a Switch that can run homebrew (Atmosphère + Homebrew Menu).

1. Copy `dist/renpy-loader.nro` to `sdmc:/switch/renpy-loader/renpy-loader.nro`.
2. Launch it from the Homebrew Menu. On first start it creates:

```
sdmc:/switch/renpy-loader/
├── renpy-loader.nro
├── games/                 one folder per game (each must contain game/)
├── config/controller/
│   ├── global.json        written when you save controller settings
│   └── games/<name>.json  per-game controller profiles
└── logs/loader.log
```

**Memory:** started from the album, homebrew runs as a library applet with limited memory.
Ren'Py games will need the full application memory. Start the Homebrew Menu by holding R
while launching an installed game (title override). The Diagnostics screen shows which mode
you are in.

## Games

Not runnable yet (Milestones 3–5). The library already lists folders under `games/` and flags
those without a `game/` subfolder.

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
restored immediately and the settings screen opens; press + to save them. No setting can
disable this. A single detached Joy-Con has only one of + / −, so it cannot do this.

Profiles are plain JSON (format in `config/profiles/default.json`). A damaged file is ignored:
the loader falls back to the previous version (`.bak`), then to defaults.
