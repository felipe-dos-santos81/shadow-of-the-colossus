# sotc-native

Native Apple-Silicon rebuild of **Shadow of the Colossus (2018)** from the extracted PS4 game data:
a C++20 / SDL3 / Metal viewer and a .NET 10 asset converter. Windowed; the cursor is never captured.

**Status:** the first colossus is playable end to end. Walk the Valus arena, climb him, strike his
weakpoints, hear the game's own audio, watch his death and the black tendrils, wake in the
Shrine of Worship, walk out of its front door onto the plain, whistle for Agro and ride her (S1–S8;
each milestone's measured results are in `docs/notes/s*-results.md`). The user walks for S4.1–S8
are still pending. [`AGENTS.md`](AGENTS.md) has the full status, the
controls and the working rules.

## Quick start

```bash
brew install sdl3 sdl3_ttf          # plus .NET 10, cmake, python3
make help                           # every target
make convert                        # the asset library into out/library (resumable, ~33 min)
make bake                           # bake the clips: Mono's pose, Wander's 43, Agro's 20 (S2–S8)
make audio                          # the curated audio slice (34 cues)
make accept-s4                      # composes and gates the encounter manifest
make run                            # play the Valus encounter
make shrine                         # start in the Shrine of Worship
make plain                          # start on the plain with Agro
make accept                         # the S8 acceptance (runs S7 → S6 → S5 → S4.2 → S4.1 → S4 → S3)
```

`make run`, `make shrine`, `make plain` and `make accept` compose the shrine and plain manifests
(`make shrine-manifest`, `make plain-manifest`) when they are missing. Without the shrine manifest,
or without the wake clips from `make bake`, `make run` warns once and plays without the return to
the shrine (`--no-return` does the same on purpose).

The game data is read from `../SOTC-extract/extracted/game/` (`GAME_ROOT=` overrides it) and is never
modified. Converted assets live in the gitignored `out/` tree.

## Riding (S8)

On the plain: `Q` (gamepad north) whistles for Agro; `E` (south) beside her mounts from that side.
Mounted, each `W` press (stick-forward press) kicks her up one gait — walk, trot, gallop; a tap of
`S` lowers one gait, holding `S` stops her, and `S` at a stand backs her up; `A`/`D` (left stick)
steer; `E` at a stand or walk dismounts. Walk into the shrine's front door to reach the plain, and
into the shrine steps to go back. `R` returns to the arena from anywhere.

## Layout

- `app/` — the viewer ([`app/README.md`](app/README.md): controls and options)
- `src/`, `tests/` — the `bp-convert` converter and its tests (`BpConvert.sln`)
- `tools/re/` — reverse-engineering probes and gates (stdlib Python)
- `scripts/` — milestone acceptance scripts
- `docs/` — designs and plans (`docs/superpowers/`), RE notes and results (`docs/notes/`)

## Notes

- Run the published CLI as `dotnet exec out/bin/bp-convert.dll`; the bare apphost needs `DOTNET_ROOT`
  on this machine ([`docs/notes/aqua-core-api.md`](docs/notes/aqua-core-api.md) §0).
- `third_party/PSO2-Aqua-Library` is a pinned GPL-3.0 submodule used parse-only.
- `tools/native/liboo2core_8_win64_.dylib` is the RAD Oodle decoder, vendored to read the game's
  compressed data. **Personal use only — never redistribute**
  ([`tools/native/NOTICE.md`](tools/native/NOTICE.md)).

Personal-use fan project. No game assets are distributed in this repository.
