# Compatibility matrix

Statuses: **Verified** = passed the defined tests on real hardware. **Experimental** = loads
but tests are incomplete or issues are known. **Unsupported** = cannot run reliably.

## Engine generations

| Ren'Py | Python | Status | Notes |
|---|---|---|---|
| 8.5.x | 3.12 | **Experimental** | Ren'Py 8.5.3 runs on the Switch (GLES 3.2 on nouveau); first game launched 2026-10-18 |
| 8.0–8.4 | 3.9 / 3.12 | **Experimental** | Untested on hardware. The importer accepts them with a warning; Phase 2: test whether 8.5.3 loads their `.rpyc` |
| 7.x | 2.7 | Unsupported | Phase 3: separate Python 2.7 runtime, if feasible. Experiment: the importer's `--recompile` exports a game with `.rpy` sources for Ren'Py 8.5.3 (experimental; Python 3 semantics) |
| ≤ 6.x | 2.x | Unsupported | Not planned |

## Games

No game has been tested. Rows are added only with a test record from `docs/HARDWARE_TESTS.md`.

| Game | Source | Ren'Py | Status | Tested on | Notes |
|---|---|---|---|---|---|
| The Question | bundled with the Ren'Py SDK | 8.5.3 | **Experimental** | Switch, handheld, application mode, 2026-10-18 (run 5): starts, renders on GPU, dialogue, menus, clean quit | Also launched from the importer's export (`games/the-question`, run 9). The loader then crashed after the return (SDL timer thread, see run 9). Music plays without stutter (run 21). Saves, suspend, touch not yet checked on hardware |

## Loader features

| Feature | Status |
|---|---|
| Controller normalisation, mapping, remapping, persistence | Implemented, host-tested, **hardware unverified** |
| Virtual cursor, touch readout | Implemented, host-tested (cursor math), **hardware unverified** |
| Emergency recovery combo | Implemented, host-tested, **hardware unverified** |
| Suspend / resume / reconnect input reset | Implemented, host-tested (engine), **hardware unverified** |
| Embedded Python 3.12.8 runtime + self-test | Builds and links; packaging host-tested, **hardware unverified** |
| Ren'Py 8.5.3 engine in the runtime | Builds and links, **hardware unverified** |
| Controller bridge into Ren'Py | Desktop test 24/24 with the official engine, **hardware unverified** |
| Game launch from the library, per-game saves and logs | Implemented, **hardware unverified** |
| Live2D, 3D models (GLTFModel) | Unsupported (no Switch libraries) |
| In-game loader overlay | Not implemented (Milestone 6); the button shows a notice |
| Importer (`importer/rsl_import.py`, macOS) | Implemented, host-tested (25 tests); its export of The Question launched on hardware (run 9). See `docs/IMPORTER.md` |
