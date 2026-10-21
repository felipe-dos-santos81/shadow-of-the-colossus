# AGENTS.md — sotc-native workspace

Personal-use, offline-first native rebuild of *Shadow of the Colossus* for Apple Silicon:
C++20 + SDL3 + SDL_GPU/Metal for the viewer, a .NET 10 converter for the game's assets.
This file is workspace guidance and is tracked with the project.

## Status (2026-10-21)

**Milestone S4.2 "He fights back" — implemented; scripted acceptance green, user walk PENDING.**
The encounter is now a fight: damage only lands on the two shipped shell weakpoints
(`weakpoint_03` forehead, `weakpoint_04` right upper arm), the body is climbable around bands
(`A`/`D`) and through the chest→shoulder→arm branch (`W+D`), Valus turns, stomps and swings his
club, and Wander has health, hurt/death clips and the full `R` soft reset. The deterministic
timeline is `--s42-selftest` (six HUD stills with `--s42-stills DIR`), the acceptance script is
`scripts/s42-acceptance.sh`, and the recorded numbers are in `docs/notes/s42-results.md`.
S4.1/S4/S3 acceptance regressions are green; the interactive checklist (feel, scale, telegraph
readability, cursor, gamepad hardware, fps) is still the user's to fill in.

## Layout

| Path | Contents |
|---|---|
| `app/` | the native viewer (`app/src/`), shaders, headless tests, `app/README.md` (controls/options) |
| `src/` + `tests/` | the .NET asset converter and its xUnit suite (`BpConvert.sln`) |
| `scripts/` | milestone acceptance scripts (`s3`, `s4`, `s41`, `s42`); `s3`/`s4`/`s41` are frozen for S4.2 |
| `docs/notes/` | RE records and per-milestone results notes (`s42-results.md` is this milestone's) |
| `docs/superpowers/specs/` | milestone designs; `docs/superpowers/plans/` the plans |
| `.superpowers/sdd/` | per-task briefs and reports (durable hand-off records) |
| `out/` | gitignored: symlink to the 12 GB converted library, manifests, captures, run logs |

## Environment

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/library
```

The machine suspends idle background work. **Every viewer run and every acceptance run must be
wrapped in `caffeinate -dimsu`**; an unwrapped 1-frame run has taken 907 s. Never run two
acceptances concurrently.

## Build and test

```bash
dotnet build BpConvert.sln
dotnet test tests/BpConvert.Tests -v q
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j
ctest --test-dir app/build --output-on-failure
```

Dependencies: SDL3 and SDL3_ttf via Homebrew (`brew install sdl3 sdl3_ttf`).

## Running the game

```bash
app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb
```

Windowed, cursor never captured. `WASD` move (Shift runs), `E` grab (hold), `J` strike (hold to
charge), `W`/`S` climb, `A`/`D` shimmy, `W+D` at the chest top branches to the right shoulder,
`R` soft-resets the encounter, `Escape` quits. Gamepad:
left stick move, right stick camera, RT grip, west/X/square strike, Start pause.

## Acceptance

```bash
caffeinate -dimsu ./scripts/s42-acceptance.sh   # S4.2 + S4.1 (which runs S4 → S3)
```

Never modify `scripts/s4-acceptance.sh` or `scripts/s41-acceptance.sh` for an S4.2 change: they
are the regression. Assets and derived glbs are gitignored and never committed; results notes
quote measured values, not adjectives.
