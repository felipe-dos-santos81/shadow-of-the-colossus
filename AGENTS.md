# AGENTS.md — sotc-native workspace

Personal-use, offline-first native rebuild of *Shadow of the Colossus* for Apple Silicon:
C++20 + SDL3 + SDL_GPU/Metal for the viewer, a .NET 10 converter for the game's assets.
This file is workspace guidance and is tracked with the project.

## Status (2026-10-22)

**Milestone S6 "The fall" — implemented; scripted acceptance green, user walk PENDING.**
Valus's death is now a sequence instead of a freeze: his health 0 starts a phase machine
(`app/src/death.h`) that plays `minotaur_a_death_front` (6.166877 s), erupts the black tendrils
from the two weakpoint shells (the game's own `death_tentacle_veins` and the three `.cptx` smoke
atlases, a **labeled reconstruction** — the `.csdr` simulator is not ported), swaps the live rig
for the shipped dead body + dirtmound at the collapse, and hands the camera back at 8.0 s. The
player's input is locked through the motion and the collapse; `R`, `M` and `Escape` stay live.
The proof is `--s6-selftest --s6-stills out/s6/stills` (13 `s6-*` lines: the four phases, the five
events, the perf line, the death/status/reset) plus four 2560×1440 stills and a control frame;
the acceptance script is `scripts/s6-acceptance.sh`, and the recorded numbers are in
`docs/notes/s6-results.md`. The S5/S4.2/S4.1/S4/S3 acceptance regressions are green; the user's
walk (the death motion, the tendrils, the corpse, the camera, the death roar and the club whoosh,
the music stop, the control's return, `R`, no clipping, windowed/cursor, fps) is still the user's
to fill in. Known gaps: the `.csdr` particle motion, the type-36 death-camera clips (the camera is
authored), no ragdoll, and no per-segment stagger.

**Milestone S5 "The sound of the forbidden land" — implemented; scripted acceptance green, user
listening walk PENDING.**
The encounter now has the game's own voice: `bp-convert audio` decodes the seven attributed cues
plus `Boss 1` from the SXDF/SXDS banks at build time into `out/library/audio/` (48 kHz WAV +
`audio.manifest`), and the viewer mixes them (32 voices, distance attenuation and bearing pan for
world sounds, `M` mute). Music starts on the first landed hit, loops, and stops at the `R` reset.
The deterministic proof is `--s5-selftest --audio-mixdown out/s5/mix.wav` (20 ordered `s5-event`
lines over the unchanged S4.2 steps, the `s5-mix` line, `s5-status`); the acceptance script is
`scripts/s5-acceptance.sh`, and the recorded numbers are in `docs/notes/s5-results.md`.
S4.2/S4.1/S4/S3 acceptance regressions are green; the listening checklist (music start/loop/stop,
Valus's telegraph/impact/hurt, Wander's swing/hit/hurt/death, the swing/hit sync, `M`, clipping,
fps) is still the user's to fill in. Deferred cues (silent, not placeholders): the player's fall,
ambience, grab/climb and footsteps — Valus's death and the club whoosh shipped in S6.

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
| `scripts/` | milestone acceptance scripts (`s3`, `s4`, `s41`, `s42`, `s5`, `s6`); `s3`/`s4`/`s41`/`s42`/`s5` are invoked unchanged as later milestones' regressions |
| `docs/notes/` | RE records and per-milestone results notes (`s6-results.md` is this milestone's; `s6-recon.md` is its asset gate) |
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
# the curated audio slice into <SOTC_LIB>/audio/ — 8 cues for S5, 10 for S6
dotnet publish src/BpConvert.Cli -c Release -o out/bin
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" \
  --out "$SOTC_LIB" --map tools/re/audio/s6_map.json --force
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

When Valus's health reaches 0 the death sequence takes over: his fall plays once, the black tendrils
erupt from the two weakpoint shells, the camera cuts to a **12 m close, low angle on the body** (the
key is a distance *from Valus*, not a pull-back from the follow pose — the follow camera sits ≈94 m
away, so this cuts in; `docs/notes/s6-results.md` §10), the live rig becomes the shipped dead body
on its dirtmound, and the control returns at 8 s. The input is locked through the motion and the
collapse; `R`, `M` and `Escape` stay live throughout.

The fight's audio loads from `<SOTC_LIB>/audio/audio.manifest` (convert it with the `bp-convert
audio` command above); without it the interactive game warns once and runs silent, and an
encounter that is missing a curated cue warns once and is silent (`--s5-selftest` and
`--s6-selftest` are fatal).

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s6-selftest --s6-stills out/s6/stills    # the S6 death timeline + the stills
```

## Acceptance

```bash
caffeinate -dimsu ./scripts/s6-acceptance.sh   # S6 + S5 (which runs S4.2 → S4.1 → S4 → S3)
```

Never modify `scripts/s5-acceptance.sh`, `scripts/s42-acceptance.sh`, `scripts/s41-acceptance.sh`
or `scripts/s4-acceptance.sh` for an S6 change: they are the regression. Assets, derived glbs and
the decoded audio are gitignored and never committed; results notes quote measured values, not
adjectives.
