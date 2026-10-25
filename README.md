# Shadow of the Colossus (PS4, 2018) — Native Rebuild ~20%

Native Apple-Silicon rebuild of **Shadow of the Colossus** (Bluepoint Games /
SIE Japan Studio, PS4, 2018) from the game's own extracted data: a **C++20 /
SDL3 / SDL_GPU (Metal) viewer** and a **.NET 10 asset converter** (`bp-convert`).
The viewer runs windowed and never captures the mouse cursor.

It is a rebuild, not a recompilation. The PS4 executable (`eboot.elf`) is a
reference, read in Ghidra for timings, rules and data tables. The runtime never
parses the original containers: the converter turns them into glTF models, PNG
textures, WAV audio and stage manifests ahead of time (offline-first, design §8).

## Game assets

The game files are **not included** in this repository and are required to
build, run or reverse-engineer anything here. Extract your own copy of the PS4
release (CUSA08809) into `../SOTC-extract/extracted/game/` (130,155 files,
~14.4 GB; `GAME_ROOT=` overrides the path). That tree is read-only input: the
tools and tests never write to it. The converted library (~12 GB) lives in the
git-ignored `out/` tree.

## Status

Four colossi are playable end to end:
- **Valus:** walk his arena, climb him, strike his weak points, hear the game's
  own audio, watch his death and the black tendrils, then wake in the Shrine of
  Worship.
- **Journey to his arena:** walk out of the shrine's front door onto the plain,
  whistle for Agro and ride her to the arena's entrance, with the raised
  sword's light pointing the way.
- **Quadratus:** in his own arena, stab a lifted hoof to make him kneel, climb
  his fur, walk his back, and strike the back and head sigils.
- **Gaius:** on his lake arena, dodge his sword slam, grab the stuck blade and
  ride it up as he pulls it, climb his arm to the head sigil, then the belly
  sigil. His behaviour runs from his own mode graph, exported from `eboot.elf`,
  and his attacks from the game's per-clip event tracks.
- **Phaedra:** on her hill of ruins, dodge her stomp, hide in a tunnel and
  whistle; she walks to it and peeps in; leave by another tunnel, grab her tail
  while she crouches, strike her neck twice to lower it, and climb to the head
  sigil. She runs on the shared systems S13 built from the game's data: her
  routes, the arena's terrain classes and an additive motion layer.

Milestones S1–S13 are implemented, each with its measured results in
`docs/notes/s*-results.md`. Only S8 has been played and signed off by hand; the
other play-throughs are pending. Two claims back the number:

- **Rebuild — ~20% (estimate).** Method: a hand-weighed share of the game's
  content across the four areas below; S13 moved colossi from 3 to 4 of 16 and
  arenas from three to four (~18% → ~20%); its shared systems count under the
  colossi, not separately. By content:
  - colossi: 4 of 16 fightable, Valus, Quadratus, Gaius and Phaedra, with 12
    still to do; `make coverage` lists, per colossus, what its graph still needs;
  - world: the shrine, the plain around it, the road to Valus and four arenas,
    out of the 71 areas the world is built from;
  - Wander: walk, run, climb, strike, the sword's light and riding Agro; swimming,
    jumping, the bow and the moving mount are not done;
  - not started: cutscenes, Dormin, menus and save.

  The asset pipeline is largely complete: the 16,782-source model batch
  converts, with its known failure classes listed in `docs/notes/m0-results.md`,
  along with clips, audio and stage composition. The S10 census
  records all 16 colossi, and the S11 behaviour RE found each colossus's state
  machine stored as **data tables inside `eboot.elf`** (21 modes for Valus and
  Quadratus, 13 for Gaius), with
  timers, angles, health and anger. S12 reads them: a graph exporter and a
  generic interpreter run Gaius from his tables, and the converter exports each
  clip's damage and event tracks. S13 exports all 16 graphs and adds the shared
  systems (a control-def evaluator, a predicate library, the additive layer,
  route and terrain exporters), leaving a short labelled list of authored and
  guessed values (`docs/notes/s13-results.md` §7).
- **Verification — goldens plus a frozen chain.** `scripts/verify` re-runs each
  milestone's scripted selftest and compares it with blessed goldens
  (`tools/verify/golden/`; timing fields masked). Content-hash stamps skip
  unchanged steps:
  - cold: about 8.7 min (521 s with S13, `--force`);
  - warm: about 2 s;
  - one milestone after an app change: about 95 s.

  The milestone gate is the frozen acceptance chain
  `scripts/s13-acceptance.sh`, which runs S12 (and S11 → S9 → … → S3 inside it)
  first and then Phaedra; it passes in 1,165 s. Behind both sit 664 app unit cases and
  the converter's xUnit suite.

The detailed, continuously-updated status — every milestone, its rulings, its
authored-vs-data values and its known gaps — is in
**[`AGENTS.md`](AGENTS.md)** and the per-milestone `docs/notes/s*-results.md`.

## Layout

| Path | What |
|---|---|
| `AGENTS.md` | Running status per milestone, the controls, the working rules |
| `app/` | The viewer: `src/` (one file per selftest, `stage_setup`, `interactive`, `encounters/`), shaders, headless tests; [`app/README.md`](app/README.md) has the controls and options |
| `src/`, `tests/` | The `bp-convert` converter and its xUnit suite (`BpConvert.sln`) |
| `tools/re/` | Reverse-engineering probes, stage composer (`stage/probe.py`), census, the graph/route/terrain exporters and the coverage report, generated-table scripts (stdlib Python) |
| `tools/verify/` | The regression runner: `run.py`, `stamps.py`, `masks.py`, one adapter `sN.sh` per milestone, `golden/`, `libview.sh` |
| `scripts/` | Milestone acceptance scripts `s3`…`s13`; the older ones are frozen as later milestones' regressions; `verify` |
| `docs/superpowers/` | Milestone designs (`specs/`) and plans (`plans/`) |
| `docs/notes/` | RE records and per-milestone results notes |
| `third_party/PSO2-Aqua-Library` | Pinned GPL-3.0 submodule, used parse-only behind the converter's adapter |
| `out/` | Git-ignored: the converted library, library views per branch, stage manifests, captures, run logs |
| `Makefile` | `make help` — build, convert, bake, run, verify and acceptance targets |

## Toolchain

* **Xcode command-line tools** and **CMake 4.4** for the C++20 viewer; **SDL3
  3.4** and **SDL3_ttf 3.2** from Homebrew (`brew install sdl3 sdl3_ttf`).
* **.NET 10** SDK for the converter.
* **Python 3** (stdlib only) for the RE tools, the stage composer and the
  regression runner.
* **Ghidra 12** (headless, read-only over `out/re/ghidra`), plus Ghidra-MCP and
  REA for interactive follow-up; the `eboot.bin` SELF → ELF unwrap and the
  baseline are in [`docs/notes/re-toolchain.md`](docs/notes/re-toolchain.md).

## Build and run

```bash
export SOTC_GAME_ROOT=$PWD/../SOTC-extract/extracted/game
make help                           # every target
make convert                        # the asset library into out/library (resumable, ~33 min)
make bake                           # the clips: Mono, Wander's 49, Agro's 20, the sword's light, Quadratus's 58, Gaius's 62, Phaedra's 75; the 16 graphs, routes, terrain
make audio                          # the curated audio slice (36 cues)
make run                            # play the Valus encounter
make shrine                         # start in the Shrine of Worship
make road                           # start on the plain: ride to Valus's arena, C raises the sword
make quadratus                      # fight Quadratus in his arena
make gaius                          # fight Gaius on his lake arena
make phaedra                        # fight Phaedra on her hill of ruins (Q whistles her to your tunnel)
make coverage                       # per colossus: which graph items the interpreter implements (out/re/coverage.md)
```

`make run`, `make shrine` and `make road` compose the stage manifests they
need when they are missing. Run the published CLI as
`dotnet exec out/bin/bp-convert.dll`; the bare apphost needs `DOTNET_ROOT` on
this machine ([`docs/notes/aqua-core-api.md`](docs/notes/aqua-core-api.md) §0).
A branch that re-bakes works in its own clone of the library:
`make libview BRANCH=<name>`, then export the `SOTC_LIB` it prints.

**Controls.** `WASD` move (Shift runs), `E` grab (hold), `J` strike (hold to
charge), `W`/`S` climb, `A`/`D` shimmy, `R` soft-reset, `M` mute, `Escape`
quit. On the plain: `Q` whistles for Agro, `E` beside her mounts. Mounted, a
`W` press kicks one gait up, `S` reins in (held: stop; at a stand: back up),
`A`/`D` steer and `E` dismounts. Hold `C` to raise the sword. A gamepad works
too; the full mapping is in [`app/README.md`](app/README.md).

### Verify

```bash
make verify                         # per-task gate: re-runs only what changed, against the goldens
make verify-long                    # milestone gate: the frozen acceptance chain (~20 min)
caffeinate -dimsu ./scripts/s13-acceptance.sh
```

Every viewer and acceptance run is wrapped in `caffeinate -dimsu`, because
this machine suspends idle background work. GPU runs stay serial: measured, three
concurrent viewers add up to 2 ms to the 8.2 ms frame. Goldens are re-blessed
(`make verify ARGS=--bless`, clean tree, nothing else running) only for a
deliberate output change.

## Third-party

- **PSO2-Aqua-Library** (GPL-3.0), a pinned submodule at
  `third_party/PSO2-Aqua-Library`, used parse-only behind
  `src/BpConvert.Core/Aqua/AquaModelAdapter.cs`.
- **RAD Oodle** decoder (`tools/native/liboo2core_8_win64_.dylib`), vendored to
  read the game's compressed data, and **SixLabors.ImageSharp** in the
  converter. Both are **personal use only — never redistribute**
  ([`tools/native/NOTICE.md`](tools/native/NOTICE.md)).
- **cgltf**, **stb_image**, **stb_image_write** and **doctest** are vendored
  single headers in `app/third_party/` (versions in `VERSIONS.md`).

GPL-3.0 project. Personal-use fan project: no game assets, and nothing derived
from them, are distributed in this repository.
