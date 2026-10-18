# Compatibility matrix

Statuses: **Verified** = passed the defined tests on real hardware. **Experimental** = loads
but tests are incomplete or issues are known. **Unsupported** = cannot run reliably.

## Engine generations

| Ren'Py | Python | Status | Notes |
|---|---|---|---|
| 8.5.x | 3.12 | Unsupported (runtime not built) | Phase 1 target: 8.5.3 + CPython 3.12.8 |
| 8.0–8.4 | 3.9 / 3.12 | Unsupported | Phase 2: test whether 8.5.3 loads their `.rpyc` |
| 7.x | 2.7 | Unsupported | Phase 3: separate Python 2.7 runtime, if feasible |
| ≤ 6.x | 2.x | Unsupported | Not planned |

## Games

No game has been tested. Rows are added only with a test record from `docs/HARDWARE_TESTS.md`.

| Game | Source | Ren'Py | Status | Tested on | Notes |
|---|---|---|---|---|---|

## Loader features

| Feature | Status |
|---|---|
| Controller normalisation, mapping, remapping, persistence | Implemented, host-tested, **hardware unverified** |
| Virtual cursor, touch readout | Implemented, host-tested (cursor math), **hardware unverified** |
| Emergency recovery combo | Implemented, host-tested, **hardware unverified** |
| Suspend / resume / reconnect input reset | Implemented, host-tested (engine), **hardware unverified** |
| Embedded Python 3.12.8 runtime + self-test | Builds and links; packaging host-tested, **hardware unverified** |
| Ren'Py engine, game launch, saves | Not implemented |
| In-game overlay | Not implemented (needs the runtime) |
| Importer | Not implemented (Milestone 5) |
