# AGENTS.md — sotc-native workspace

Personal-use, offline-first native rebuild of *Shadow of the Colossus* for Apple Silicon:
C++20 + SDL3 + SDL_GPU/Metal for the viewer, a .NET 10 converter for the game's assets.
This file is workspace guidance and is tracked with the project.

## Status (2026-10-23)

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
cmdl-fix glbs) lives in the library view `out/libviews/s9-road` until S9's merge promotes it to the
base.** The user's walk is still the user's to fill in.

**Milestone S8 "Agro" — implemented; scripted acceptance green, user walk PENDING.**
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
low-confidence hoof picks. The user's walk is still the user's to fill in.

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
| `app/` | the native viewer (`app/src/`), shaders, headless tests, `app/README.md` (controls/options) |
| `src/` + `tests/` | the .NET asset converter and its xUnit suite (`BpConvert.sln`) |
| `scripts/` | milestone acceptance scripts (`s3`, `s4`, `s41`, `s42`, `s5`, `s6`, `s7`, `s8`, `s9`); `s3`/`s4`/`s41`/`s42`/`s5`/`s6`/`s7`/`s8` are invoked unchanged as later milestones' regressions |
| `docs/notes/` | RE records and per-milestone results notes (`s9-results.md` is this milestone's; `s9-recon.md` is its RE gates) |
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

S9 works in `out/libviews/s9-road` (`export SOTC_LIB=<repo>/out/libviews/s9-road`): its bake (Wander's
49 clips, the four sword PNGs, the 36-entry audio manifest, the sibling-`.cmdl` fix: 10 sources rebaked, 8 glbs changed)
is promoted to the base `out/library` at S9's merge, after `main`'s pins (Wander `== 43`, audio 8/10/34)
are relaxed with the user's OK. Until then `scripts/s9-acceptance.sh` refuses a base library without the
S9 bake.

The machine suspends idle background work. **Every viewer run and every acceptance run must be
wrapped in `caffeinate -dimsu`**; an unwrapped 1-frame run has taken 907 s. Never run two
runs sharing an `out/` directory; the runner parallelises within its own `out/verify/<worktree>/`.

## Build and test

`make help` lists the shortcuts (`make app-test`, `make run`, `make shrine`, `make plain`, `make
accept`, `make accept-s7`, …). `make bake` rebakes Mono's pose, Wander's 49 clips (the 15 S2–S6 clips,
the two S7 wake cuts, the 26 S8 ride clips, then the six S9 raise clips at 43…48), Agro's 20 (the S4.2
kick at index 0, then the 19 riding clips) and the sword's light atlases into `$SOTC_LIB` (the view, when
exported) with `--force`; `make audio` decodes the S9 map (36 entries); `make run`/`make accept` compose
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

## Acceptance

```bash
export SOTC_LIB=<repo>/out/libviews/s9-road   # until S9's merge promotes the bake to out/library
caffeinate -dimsu ./scripts/s9-acceptance.sh   # S9 + S8 (which runs S7 → S6 → S5 → S4.2 → S4.1 → S4 → S3)
```

Never modify `scripts/s8-acceptance.sh`, `scripts/s7-acceptance.sh`, `scripts/s6-acceptance.sh`,
`scripts/s5-acceptance.sh`, `scripts/s42-acceptance.sh`, `scripts/s41-acceptance.sh`,
`scripts/s4-acceptance.sh` or `scripts/s3-acceptance.sh` for an S9 change: they are the regression.
Assets, derived glbs and the decoded audio are gitignored and never committed; results notes quote
measured values, not adjectives.
