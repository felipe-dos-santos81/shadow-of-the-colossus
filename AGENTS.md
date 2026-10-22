# AGENTS.md — sotc-native workspace

Personal-use, offline-first native rebuild of *Shadow of the Colossus* for Apple Silicon:
C++20 + SDL3 + SDL_GPU/Metal for the viewer, a .NET 10 converter for the game's assets.
This file is workspace guidance and is tracked with the project.

## Status (2026-10-21)

**Milestone S5 "The sound of the forbidden land" — implemented; scripted acceptance green, user
listening walk PENDING.**
The encounter now has the game's own voice: `bp-convert audio` decodes the seven attributed cues
plus `Boss 1` from the SXDF/SXDS banks at build time into `out/library/audio/` (48 kHz WAV +
`audio.manifest`), and the viewer mixes them (32 voices, distance attenuation and bearing pan for
world sounds, `M` mute). Music starts on the first landed hit, loops, and stops at the `R` reset.
The deterministic proof is `--s5-selftest --audio-mixdown out/s5/mix.wav` (18 ordered `s5-event`
lines over the unchanged S4.2 steps, the `s5-mix` line, `s5-status`); the acceptance script is
`scripts/s5-acceptance.sh`, and the recorded numbers are in `docs/notes/s5-results.md`.
S4.2/S4.1/S4/S3 acceptance regressions are green; the listening checklist (music start/loop/stop,
Valus's telegraph/impact/hurt, Wander's swing/hit/hurt/death, the swing/hit sync, `M`, clipping,
fps) is still the user's to fill in. Deferred cues (silent, not placeholders): Valus's death, the
club whoosh, the player's fall, ambience, grab/climb and footsteps.

S4.2 "He fights back" is implemented and scripted green: damage only lands on the two shipped
shell weakpoints (`weakpoint_03` forehead, `weakpoint_04` right upper arm), the body is climbable
around bands (`A`/`D`) and through the chest→shoulder→arm branch (`W+D`), Valus turns, stomps and
swings his club, and Wander has health, hurt/death clips and the full `R` soft reset. Its numbers
are in `docs/notes/s42-results.md`; S4.1/S4/S3 regressions are green.

## Layout

| Path | Contents |
|---|---|
| `app/` | the native viewer (`app/src/`), shaders, headless tests, `app/README.md` (controls/options) |
| `src/` + `tests/` | the .NET asset converter and its xUnit suite (`BpConvert.sln`) |
| `scripts/` | milestone acceptance scripts (`s3`, `s4`, `s41`, `s42`, `s5`); `s3`/`s4`/`s41`/`s42` are invoked unchanged as later milestones' regressions |
| `docs/notes/` | RE records and per-milestone results notes (`s5-results.md` is this milestone's; `s5-audio-recon.md` and `s5-audio-attribution.md` are its gates) |
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
# the curated audio slice (8 cues: seven + Boss 1) into <SOTC_LIB>/audio/
dotnet publish src/BpConvert.Cli -c Release -o out/bin
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" \
  --out "$SOTC_LIB" --map tools/re/audio/s5_map.json --force
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
`R` soft-resets the encounter, `M` mutes the mixer, `Escape` quits. Gamepad:
left stick move, right stick camera, RT grip, west/X/square strike, Start pause.

The fight's audio loads from `<SOTC_LIB>/audio/audio.manifest` (convert it with the `bp-convert
audio` command above); without it the game is silent with one warning, and an encounter that is
missing a curated cue fails loudly.

## Acceptance

```bash
caffeinate -dimsu ./scripts/s5-acceptance.sh   # S5 + S4.2 (which runs S4.1 → S4 → S3)
```

Never modify `scripts/s42-acceptance.sh`, `scripts/s41-acceptance.sh` or `scripts/s4-acceptance.sh`
for an S5 change: they are the regression. Assets, derived glbs and the decoded audio are
gitignored and never committed; results notes quote measured values, not adjectives.
