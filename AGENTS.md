# AGENTS.md — sotc-native workspace

Personal-use, offline-first native rebuild of *Shadow of the Colossus* for Apple Silicon:
C++20 + SDL3 + SDL_GPU/Metal for the viewer, a .NET 10 converter for the game's assets.
This file is workspace guidance and is tracked with the project.

## Status (2026-10-24)

**Milestone S13 "Phaedra" (the shared systems) — implemented; scripted acceptance green, user walk PENDING.**
A systems milestone built in six parallel lanes, proven on the fourth colossus on `arena_04`
(`out/re/stage/arena_04.manifest`, `probe.py compose --stage arena_04`, `make arena04-manifest`: the ruins fed as
**floors-only** `floor` lines so the stair tunnels and the ring corridor are walkable; `encounter kirin`; `make phaedra`
plays it). The systems, all from the game's own data: **all 16 graphs** exported (`make behaviour`, `sotc-scr-graph/0`
with additive keys: `control_defs`, `dprelate`, `attackdata`, the slowstatus fields, edge `ref`/`from_ref`/`speed`/
`blend_out`; KNIGHT 13/53/40/171, KIRIN 15/84/97/314); a **generic control-def evaluator** over the 37 opcodes
(`app/src/scr_cdef.*`, S12's 30-key table deleted); a **shared predicate library** (`app/src/scr_library.*`, Gaius's
attackdata from the export, `kAttack` deleted); the **additive layer** (`app/src/additive.*`, pool of 4, opt-in per
encounter — Phaedra only); **routes** (`make routes` → `<SOTC_LIB>/behaviour/kirin.route.json`, `sotc-route/0`; the
follower kinds 2–9, `app/src/route.*`); **terrain** (`make terrain` → `<SOTC_LIB>/stage/arena_04.terrain.json`,
`sotc-terrain/0`; `app/src/terrain.*`); and **`make coverage`** (`out/re/coverage.md`: per boss, which graph items the
app implements; KNIGHT and KIRIN 0 missing; kind 1 (R-AL) and the foot-IK aim count as gaps, Gaius's other
2 are S12's labelled no-ops). Her values are
`app/src/encounters/kirin.h`, her glue `app/src/kirin_fight.*` (Gaius's core shared, not copied). The fight: dodge her
stomp; hide in a stair tunnel and whistle (`Q`) — she routes to its GP and peeps (deep on the ruins' boss ground); leave
by another tunnel and grab her tail (`E`) while she crouches (`watch_*`); climb tail → back → withers → neck; strike the
neck trigger twice (neck down 1, then 2, additive); climb to the head and strike the sigil (160); her death drops her
root 9.86 → 2.44 m and holds `death_loop`; no return to the shrine; `R` resets. The provenance table (graph / route /
terrain / sidecar / decode / authored / guess), the 16-boss coverage and every ruling R-A…R-AL are
`docs/notes/s13-results.md`. The proof is `--s13-selftest --s13-stills out/s13/stills` and `--s13-e2e` (manifest
arena_04); the acceptance script is `scripts/s13-acceptance.sh` (a cold `scripts/verify --force` over S3…S12 first); `tools/verify/s13.sh`
has blessed goldens; s3…s12 goldens byte-identical (no §13.3 ruling). Everything S13 exports and bakes (her bake,
the 16 graphs with `control_defs`, the route, the terrain) was promoted to the base `out/library` at S13's merge
(`make bake` includes `bake-kirin`, `behaviour` for all 16, `routes` and `terrain`); the `s13` view is deleted. Known gaps: the
rockfall spawners and the proximity latch (labelled no-ops), the stomp foot-IK aim, the hashigo ladders, Hard mode,
silent (D10), **her nav-loop patrol** (locomotion kind 1 stays no-steer, R-AL), her heading constant through the
climb (mode 14's kind 1 is that no-steer), the head shake survived trivially (drain 22.9/s vs regen 25/s), the neck's additive swing not
met on the scripted route, the rockfall feed a guess (R-AJ), Valus and Quadratus still on their S4/S11 code.

**Milestone S12 "Gaius" — implemented; scripted acceptance green, user walk PENDING.**
The third colossus fights on his lake arena from **his own exported tables**: `out/re/stage/arena_03.manifest`
(`probe.py compose --stage arena_03`, `make arena03-manifest`; the `encounter knight` directive selects him;
`make gaius` plays it). His behaviour is his mode graph, exported from `eboot.elf` by
`tools/re/scr/graph_export.py` into `<SOTC_LIB>/behaviour/KNIGHT.graph.json` (`make behaviour`; 13 modes,
53 conditions, 40 transitions, 171 motion edges) and run by a generic, strict interpreter
(`app/src/scr_graph.*`, `scr_predicates.*`: his 24 predicates; it refuses a graph using one it does not know);
his damage, footstep stomps and judge volumes are the converter's objanim sidecar
(`colossus_03_knight.events.json`, `sotc-objanim-events/1`; `app/src/objanim_events.*`). The glue is
`app/src/knight_fight.*`; his values are `app/src/encounters/knight.h`. Dodge his slam (6–7 m off his line);
grab the stuck blade at its low end (`E`, `atk1_3` f9–75); be on its upper half (bandT ≥ 0.5, ruling R-M)
when he pulls it and you ride it up onto the forearm (R-J); climb arm → shoulder → neck → head, strike the
head sigil, hold through the shake-off, take the shoulder's branch (`W`+`D`) down the belly band, strike the
belly sigil; `knight_death` holds its last frame, no return to the shrine; `R` resets. The provenance table
(graph / sidecar / R-decode / authored) is `docs/notes/s12-results.md` §7; the R1–R5 gates are
`docs/notes/s12-recon.md`. The proof is `--s12-selftest --s12-stills out/s12/stills` and `--s12-e2e`
(manifest arena_03); the acceptance script is `scripts/s12-acceptance.sh` (the frozen S11 chain first);
`tools/verify/s12.sh` has blessed goldens. His bake (62 `knight_a_*` clips + the sidecar) and the graph
were promoted to the base `out/library` at S12's merge (R-K; `make bake` includes `bake-knight` and `behaviour`,
R-C); the `s12-gaius` view is deleted. S13 moved him onto the shared library (his outputs unchanged).
Known gaps: no additive layer (no flinch, §13.7), no armour break, opaque water, silent (D10), the blade
grab instead of a walk-on, a 0.05 ride margin, `KNIGHT_PLAYER_OUT` never fires, the y-34 backstop unproven,
the belly route and the else-edge reading authored. **Audio needs a desktop (Aqua) session**: from a
background `tmux` the e2e goldens gain an `audio: no output device` line and verify fails.

**Milestone S11 "Quadratus" — implemented; scripted acceptance green, user walk PENDING.**
The second colossus fights in his own arena: `out/re/stage/arena_02.manifest` (`probe.py compose --stage
arena_02`, `make arena02-manifest`; the `encounter quadratus` directive selects him; `make quadratus`
plays it). Stab a rear hoof while it is lifted (≥ 0.5 m) and he kneels (no damage, 20 s); climb the
kneeling leg's fur (hoof → shin → thigh → rump), strike the back sigil on the rump's rear face, stand on
and walk his back (hold `E`), hold through the shake-off (`refuse_shoulder`, the first 5 s of each 20 s
attached), climb the neck and strike the head sigil; his death clip is held where its root leaves him (no
corpse swap) and there is **no return to the shrine**; `R` resets. Behaviour is data from `eboot.elf`'s
MAMMOTH tables (the 220 of `boss_param[1]`, read as his HP: a guess; the hoof gate, 20 s sprain, 2/10 s
rise, the shake rule, ±30° turn); the rest is measured or authored (`app/src/encounters/quadratus.h`,
`app/src/quadratus_fight.h`). Valus is unchanged.
The proof is `--s11-selftest --s11-stills out/s11/stills` and `--s11-e2e` (manifest arena_02); the
acceptance script is `scripts/s11-acceptance.sh` (the frozen S9 chain first); the runner's
`tools/verify/s11.sh` has blessed goldens. Quadratus's bake (58 `boss_a_*` clips) is in the base
`out/library` (ruling A16; `make bake` includes `bake-quadratus`). Numbers: `docs/notes/s11-results.md`.
Known gaps: his fight is silent (D10), no hit reactions or anger, the 45 s sprain cap is moot, the
tendril window is Valus's, the neck hand-over drops sideways, releasing `E` on the back drops you
through, the 0.6 m blade reach and 0.5 m hoof lift are authored. Backlog (the user: "Fix later"): Valus
from his own eboot tables; the Valus `boss01`/`boss15` bank conflict is recorded, not corrected.

**Milestone S10 "The fast loop" — implemented; no game change, so no user walk.**
Tooling and a refactor, every frozen output unchanged. `scripts/verify` (`make verify`; `tools/verify/`)
re-runs each milestone's selftests once against 83 blessed goldens (`tools/verify/golden/`, `PIN` = the
bless commit; timing fields masked, paths normalised) and skips any step whose content-hash stamp is
unchanged: measured cold 285.7 s, warm 1.4 s, one milestone (`--only s9`) 94.5 s, against 782 s for the
frozen chain (`make verify-long`, which agrees with it). `make libview BRANCH=<name>` gives a branch its
own clone of the library. `make census` rebuilds `tools/re/census/census.json` (the 16 colossi,
`docs/notes/colossi-census.md`). `app/src/app.cpp` (9,275 lines) is split into 24 files (`app.cpp` is 151
lines; file map in `docs/notes/s10-results.md`) and `run_flags` replaces the five run-flag truth tables.
GPU width stays 1: three concurrent viewers read up to +2.05 ms over the 8.22 ms solo marginal.
Numbers: `docs/notes/s10-results.md`.

**Milestone S9 "The road to Valus" — implemented; scripted acceptance green, user walk PENDING.**
Interactive play's plain is now the **road** (`out/re/stage/road.manifest`, `probe.py compose --stage
road`: S8's plain + the arena's six entrance glbs as ground + `arena_01.glb` as a backdrop): the entrance
stairs and corridor are walkable, the corridor's dead end is a door (`road_points::kDoorCorridor`) into
Valus's arena at its spawn with a fresh encounter, and a box on the arena's rim (`kDoorArena`) leads back
onto the corridor floor; Agro's pose is stored when the player leaves the road by either door and she
waits there (`R` clears it). Holding `C` (gamepad left shoulder; the game's R2, remapped) raises
`wander_sword.glb` (Wander's six raise clips at 43…48; mounted only at Stand): in sunlight the eight-line
fan opens around the camera's look and closes as the look nears Valus's chest, locking at `k < 0.01`; a
locked ray (150 m, raw heightmap nodes + a backdrop ray grid) sparks where it lands; in shade (a 300 m
march toward the authored sun) the light fades at 9.0/s. The shimmer loops while raised and the search
cue plays once per lock (`tools/re/audio/s9_map.json`, 36 entries). `--s8-selftest`/`--s8-e2e` still load
`plain.manifest` and never see S9 state. The proof is `--s9-selftest --s9-stills out/s9/stills` (15
`s9-*` lines + four stills) and `--stage shrine --s9-e2e` (shrine → road → mount → gallop → lock →
stairs → corridor door → arena → the lethal hit → death → shrine); the acceptance script is
`scripts/s9-acceptance.sh` (it runs the frozen S8 chain first, then decodes the S9 map itself), and the
recorded numbers are in `docs/notes/s9-results.md`. Valus's chest is 150–157 m from the road's sunlit
points, beyond the 150 m ray, so every road lock lands on the entrance's east bank (ruling B6). Authored:
the 0.9 m × raise camera shoulder shift, the 90° × raise grip standing in for the hoist's missing
additive layer, the sun, the cue timings. Known gaps: the climb, the moving/additive raises, raised
walking, the sky and weak-point beams, the sword glow, skirts drawn opaque, the black sky, the
low-confidence shimmer pick. **S9's bake (Wander 49 clips, the sword PNGs, the 36-entry audio, the 10
cmdl-fix glbs) was promoted to the base `out/library` at S9's merge (the `s9-road` view is deleted).** The user's walk is still the user's to fill in.

**Milestone S8 "Agro" — implemented; scripted acceptance green; the user's walk ACCEPTED (2026-10-23, "make plain is ok").**
The shrine's front door (an authored trigger box, entry only; `app/src/door.h`: FadeOut 1.0 s → Load →
FadeIn 1.5 s) now leads onto the plain: a third stage `plain` (`out/re/stage/plain.manifest`, composed by
`probe.py compose --stage plain`: the `levels/home` ground, 27 cathedral-exterior `backdrop` lines and
Agro as a `horse` record), whose ground refuses holes, drops > 1.5 m and rises > 1.0 m (the cliff edge is
a wall). `Q` whistles Agro over (authored approach, the binary's 10 s stuck warp); `E` beside her mounts
from that side; mounted, a `W` press kicks one gait up through the start clips (the binary's kick buffer),
`S` taps down / held stops through the stop clips / backs up at a stand, `A`/`D` steer (authored
90/60/60/45 °/s), `E` at a stand or walk dismounts (`app/src/ride.h`). Agro moves by the clips' own
measured root travel (walk 1.31, trot 5.29, gallop 14.7 m/s); Wander rides on `OptBone_boy`; every clip
switch on either blends over 0.25 s. The audio slice is `tools/re/audio/s8_map.json` (34 entries: the S6
ten + 24 audition picks). A box on the shrine steps leads back; `R` returns to the arena from anywhere.
The proof is `--s8-selftest --s8-stills out/s8/stills` (16 `s8-*` lines + four 2560×1440 stills) and
`--stage shrine --s8-e2e` (the interactive loop, scripted: door → plain → whistle → mount → gallop →
stop → dismount → door → `R`); the acceptance script is `scripts/s8-acceptance.sh` (it runs the frozen S7
chain first, then decodes the S8 map itself), and the recorded numbers are in `docs/notes/s8-results.md`.
The plain loads in 0.88–0.89 s and draws at 8.37 ms/frame. Known gaps: foliage, the bridle, jumps, mounted
combat, the moving mount, the call voice, music while riding, the arena wall at the entrance stairs,
low-confidence hoof picks.

**Milestone S7 "Return to the Shrine" — implemented; scripted acceptance green, user walk PENDING.**
After the death sequence reaches `Done` the interactive game now returns to the shrine: a transition
machine (`app/src/transition.h`) holds on the corpse (2.0 s), fades to white (1.0 s), loads
`out/re/stage/shrine.manifest` (the cathedral glbs, 16 idols with slot 1 broken, a posed Mono on the
altar), fades in (1.5 s), plays the baked `player_a_home_boss01_cut04`+`cut05` wake (11.669 s) under an
authored two-key camera and hands over control; `R` in the shrine goes back to the arena. The shrine
ground is a layered heightmap (the floor under the feet; `kHeightmapLayers` 64) with a coarse 1 m wall
mask; off-screen meshes are culled. The timings, Mono's placement, the spawn yaw, the probes, the wake
camera and the fade still are **authored** (R2 found no timings in the binary); the idol positions and
the wake clips are data (R1/R3 PASS). The shrine is silent. `--no-return` keeps S6's end state for the
S6 walk. The proof is `--s7-selftest --s7-stills out/s7/stills` (25 `s7-*` lines + five 2560×1440
stills); `--s7-e2e` chains a real lethal hit → the S6 death → the fade → the shrine load → the wake →
the control returning → `R` in one process through the interactive loop (20 `s7e2e-*` lines); the
acceptance script is `scripts/s7-acceptance.sh` (it runs the frozen S6 chain first), and
the recorded numbers are in `docs/notes/s7-results.md`. Known gaps: Dormin's voice and light, the
statue break animation, the cutscene cameras, the bridge and garden walk, the coarse wall mask. The shrine
loads in 1.79–1.90 s (the arena 0.65 s) and the window pumps events during a load. The user's walk is still the user's to fill in.

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
| `app/` | the native viewer (`app/src/`: `app.cpp` is the shell, then `stage_ctx`/`stage_setup`, one `selftest_sN.cpp` per milestone, `interactive`, `encounter_rt`, `capture`, `audio_seam`, `gamepad_input`, `run_flags`), shaders, headless tests, `app/README.md` (controls/options) |
| `src/` + `tests/` | the .NET asset converter and its xUnit suite (`BpConvert.sln`) |
| `scripts/` | milestone acceptance scripts (`s3`, `s4`, `s41`, `s42`, `s5`, `s6`, `s7`, `s8`, `s9`, `s11`, `s12`, `s13`); `s3`…`s12` are invoked unchanged as later milestones' regressions; `scripts/verify` is the runner |
| `tools/verify/` | the runner: `run.py`, `stamps.py`, `masks.py`, one adapter `sN.sh` per milestone (`s3`…`s9`, `s11`, `s12`, `s13`), `libview.sh`, `golden/` (104 goldens + `PIN`), its unit tests (`python3 -I tools/verify/test_*.py`) |
| `tools/re/scr/` | `graph_export.py` (the 16 colossi's mode graphs from `eboot.elf` → `sotc-scr-graph/0`), `coverage.py` (`make coverage`) and their tests |
| `tools/re/path/`, `tools/re/clb/` | the `.path` route exporter (`route_export.py` → `sotc-route/0`) and the `.clb` terrain exporter (`terrain_export.py` → `sotc-terrain/0`, `boss_regions.py`), with tests (S13) |
| `tools/re/census/` | `census.py` and `census.json`: the 16 colossi (`make census`) |
| `docs/notes/` | RE records and per-milestone results notes (`s13-results.md` is this milestone's, `s13-schemas.md` its file formats; `s12-results.md` S12's, `s12-recon.md` its R1–R5 gates; `s11-results.md` S11's, `s10-results.md` S10's; `colossi-census.md` the census; `s9-results.md`/`s9-recon.md` are S9's) |
| `Makefile` | shortcuts for every command below (`make help`); it exports the environment and wraps runs in `caffeinate` |
| `docs/superpowers/specs/` | milestone designs; `docs/superpowers/plans/` the plans |
| `.superpowers/sdd/` | per-task briefs and reports (durable hand-off records) |
| `out/` | gitignored: symlink to the 12 GB converted library, manifests, captures, run logs |

## Environment

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/library
```

**A branch works in a library view, never in `out/library`.** `make libview BRANCH=<name>` clones the
shared library (`cp -cR`: models, textures, audio, coredata, `catalog.json`; ~5 s, ~0 bytes until a file is
rewritten) into `out/libviews/<name>/` and prints `export SOTC_LIB=...`. Export it: the Makefile's
`LIB ?= $(or $(SOTC_LIB),$(CURDIR)/out/library)` then points `make bake`/`audio`/`convert`, the viewer and
the tests at the view, so a branch's re-bakes and audio decodes never touch the base or `main`. Everything
is cloned, no symlinks (the converter rewrites every referenced texture PNG on a bake and would write
through). Delete a view with `rm -rf out/libviews/<name>` when the branch is done.

S9's bake (Wander's 49 clips, the four sword PNGs, the 36-entry audio manifest, the sibling-`.cmdl` fix:
10 sources rebaked, 8 glbs changed) was promoted to the base `out/library` at S9's merge; the `s9-road` view
is deleted. A branch that re-bakes starts a view of its own (`make libview BRANCH=<name>`).

The machine suspends idle background work. **Every viewer run and every acceptance run must be
wrapped in `caffeinate -dimsu`**; an unwrapped 1-frame run has taken 907 s. Never run two
runs sharing an `out/` directory; the runner parallelises within its own `out/verify/<worktree>/`.

## Build and test

**The gate rule.** `make verify` (`scripts/verify`) is the **per-task gate**: run it after every task's
change and before reporting it done. It runs only the steps whose inputs changed (`make verify
ARGS="--quick"` skips the e2e runs for the inner loop, `ARGS="--only s7"` one milestone, `ARGS=--dry-run`
lists the decisions). The **milestone gate**, run before a merge, is `make verify ARGS=--force` (the cold runner,
every milestone) plus `make accept-s13` (whose regression step is itself a cold runner pass over S3…S12).
`make verify-long` (`scripts/verify --long`: the highest `scripts/sN-acceptance.sh` with
`SOTC_REGRESSION=chain`, i.e. s13 on the frozen S12 → … → S3 chain, ~20 min) stays available as the deep
frozen chain. Goldens are re-blessed
(`ARGS=--bless`, clean tree, nothing else running) only for a deliberate output change; a new milestone
adds its `tools/verify/sN.sh` adapter. GPU runs stay serial (width 1). A git worktree needs
`git submodule update --init --recursive` before dotnet or verify works in it.

`make help` lists the shortcuts (`make app-test`, `make run`, `make shrine`, `make plain`, `make
accept`, `make accept-s7`, …). `make bake` rebakes Mono's pose, Wander's 49 clips (the 15 S2–S6 clips,
the two S7 wake cuts, the 26 S8 ride clips, then the six S9 raise clips at 43…48), Agro's 20 (the S4.2
kick at index 0, then the 19 riding clips), the sword's light atlases, Quadratus's 58 `boss_a_*` clips
(byte-sorted; S11, `make bake-quadratus`), Gaius's 62 `knight_a_*` clips with his events sidecar (byte-sorted;
S12, `make bake-knight`), Phaedra's 75 `kirin_a_*` clips with her sidecar (S13, `make bake-kirin`), the 16 exported
graphs (`make behaviour`, from `out/re/eboot.elf`), the route graphs (`make routes`) and arena_04's terrain
(`make terrain`) into `$SOTC_LIB`
(the view, when exported) with `--force`; `make audio` decodes the S9 map (36 entries); `make run`/`make accept` compose
the shrine, plain and road manifests first. The underlying commands:

```bash
dotnet build BpConvert.sln
dotnet test tests/BpConvert.Tests -v q
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j
ctest --test-dir app/build --output-on-failure
# the curated audio slice into <SOTC_LIB>/audio/ — 8 cues for S5, 10 for S6, 34 for S8, 36 for S9
dotnet publish src/BpConvert.Cli -c Release -o out/bin
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" \
  --out "$SOTC_LIB" --map tools/re/audio/s9_map.json --force
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

After the death sequence the game returns to the shrine (`--no-return` keeps the S6 end state;
`--stage shrine` starts there; `--shrine-manifest PATH` overrides `out/re/stage/shrine.manifest`).
The shrine load takes 1.79–1.90 s and the window keeps pumping events during it. The S7 proof:

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s7-selftest --s7-stills out/s7/stills    # the S7 return timeline + the stills
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s7-e2e                                   # the interactive loop, scripted: hit -> shrine -> R
```

In the shrine, walk out of the front door onto the plain (`--stage plain` starts there;
`--plain-manifest PATH` overrides `out/re/stage/plain.manifest`). On the plain: `Q` (gamepad north)
whistles for Agro, `E` (south) beside her mounts; mounted, a `W` press (stick-forward press) kicks one gait
up, a tap of `S` lowers one, holding `S` stops her, `S` at a stand backs up, `A`/`D` steer, `E` at a
stand or walk dismounts. The shrine steps lead back into the shrine; `R` returns to the arena. An S7
acceptance leaves the S6 ten in the audio manifest: re-run `make audio` (34 entries) before any
`--s8-*` run. The S8 proof:

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s8-selftest --s8-stills out/s8/stills    # the S8 ride timeline + the stills
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --stage shrine --s8-e2e                    # the interactive loop, scripted: door -> ride -> door -> R
```

In interactive play the plain is the road (`--road-manifest PATH` overrides
`out/re/stage/road.manifest`; `make road` starts there): ride to the arena's entrance stairs, up them and
into the corridor; its dead end fades into Valus's arena, and a box on the arena's rim above it fades
back onto the corridor floor, where Agro waits where she was left. Hold `C` (gamepad left shoulder) to
raise the sword — on foot it stops you (`W`/`A`/`S`/`D` turn), mounted only at a stand; release lowers
it. An S8 acceptance leaves S8's 34 in the audio manifest: re-run `make audio` (36 entries) before any
`--s9-*` run. The S9 proof:

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s9-selftest --s9-stills out/s9/stills    # the raise, the lock, the shade, both doors + the stills
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --stage shrine --s9-e2e                    # the interactive loop, scripted: shrine -> road -> Valus -> shrine
```

Quadratus fights in arena_02 (`make quadratus`; the controls are the arena's; `J` under a lifted rear hoof
kneels him; `R` resets; no return to the shrine). The S11 proof:

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/arena_02.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s11-selftest --s11-stills out/s11/stills  # hoof -> kneel -> fur -> back sigil -> back -> shake -> head -> death -> R
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/arena_02.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s11-e2e                                   # the same fight through the interactive loop
```

Gaius fights on arena_03 (`make gaius`; the controls are the arena's; `E` at the stuck blade's low end grabs
it; `W`+`D` at the shoulder's top takes the belly route; `R` resets; no return to the shrine). The S12 proof:

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/arena_03.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s12-selftest --s12-stills out/s12/stills  # slam -> dodge -> blade -> ride -> head sigil -> shake -> belly sigil -> death -> R -> hit
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/arena_03.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s12-e2e                                   # the same fight through the interactive loop
```

Phaedra fights on arena_04 (`make phaedra`; the controls are the arena's; `Q` whistles her to your tunnel; `E` at her
tail while she peeps grabs it; `J` on the nape (the neck trigger) lowers her neck; `R` resets; no return to the
shrine). `make coverage` writes `out/re/coverage.md`. The S13 proof:

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/arena_04.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s13-selftest --s13-stills out/s13/stills  # stomp -> tunnel -> peep -> tail -> neck x2 -> head -> shake -> sigil -> death -> R
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/arena_04.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s13-e2e                                   # the same fight through the interactive loop
```

## Acceptance

```bash
export SOTC_LIB=<repo>/out/library
caffeinate -dimsu ./scripts/s13-acceptance.sh  # a cold scripts/verify over S3 … S12 (+ dotnet, ctest, audio), then S13
SOTC_REGRESSION=chain caffeinate -dimsu ./scripts/s13-acceptance.sh  # the deep frozen chain (= make verify-long)
```

Never modify `scripts/s3-acceptance.sh` … `scripts/s12-acceptance.sh` (s3, s4, s41, s42, s5, s6, s7, s8,
s9, s11, s12) for an S13 change: they are the deep chain (s12 runs s11, which runs s9 and the other eight)
and the runner's adapters copy their gates. A change to `scripts/s13-acceptance.sh` moves `tools/verify/golden/PIN`
(the runner refuses a frozen script that differs from the pin).
Assets, derived glbs and the decoded audio are gitignored and never committed; results notes quote
measured values, not adjectives.
