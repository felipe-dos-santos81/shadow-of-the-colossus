# S7 — "Return to the Shrine": the loop closes

**Status:** Proposed — builder call (the user skipped the question pass and asked the builder to pick
and plan the milestone, 2026-10-22). Design dated 2026-10-22; revised the same day after a builder
check and a code fact-check (walls, the stage loop, Mono as a posed prop, the arena hash gates, the
exact shrine set). The binding milestone after S6 "The
fall". S6/S5/S4.2/S4.1/S4/S3 stay green and unchanged.

## 1 · Intent

S6 ends with Valus's corpse on its dirtmound and control handed back in an empty arena — the game
does not go anywhere. In the game, the black tendrils take Wander and he wakes in the **Shrine of
Worship**: Mono lies on the altar, the first of the sixteen idols has broken, and Dormin's light
falls through the roof. This milestone closes that loop: after the death sequence the screen fades,
the shrine loads, Wander wakes in front of the altar, and the player walks the shrine.

**Why this milestone (builder's pick, from four parallel recons on 2026-10-22).** The candidates were
the shrine return, the second colossus (Quadratus), Agro riding, and the sword's light beam:

- The shrine is **data-answerable**: the 118 `cathedral` models, the 146 idol models, Mono, and the
  `home_boss01` player clips are all converted or decodable today; the game's post-defeat flow is
  hard-coded in the binary (no script layer to port), so the app owns the sequence.
- It builds the **engine capabilities every later milestone needs**: a second stage composed from
  data, a runtime stage switch, ground that picks the floor under the player in a multi-level
  building, and view culling for a scene ~32× the arena's draw count. Agro needs the `level_01` plain
  (a new stage); Quadratus needs `arena_02` (a new stage) — both stand on this.
- The second colossus is S4.1-sized (25–35 commits: the app is Valus-specific in ~167 places) and
  Agro is ~1.3–1.5× S6 with a new stage; both are better as S8+ on top of S7's stage layer.

## 2 · Goals

- **G1 — The transition.** When the S6 sequence reaches `Done`, a hold, then a fade to white, the
  shrine stage loads behind the fade, and the fade clears on the shrine. The transition is a pure,
  testable phase machine (`app/src/transition.h/.cpp`), mirroring `death.h`.
- **G2 — The shrine, composed from data.** The `cathedral` level (the original's "home") as a scene
  manifest produced by the stage probe from `shadow.cwks`, the same way S3 composed `arena_01`; the
  arena's manifest stays byte-identical.
- **G3 — Ground and walls in a building.** The player walks the shrine's floors and stairs: the
  ground query returns the floor under the player, not the roof above it, and walls, pillars, the
  altar and the idols stop the player instead of being walked through. The arena's ground and
  movement are unchanged.
- **G4 — The frame budget.** ≥ 60 fps class at 2560×1440 in the shrine (~3,550 meshes, ~2.1M
  vertices) via per-mesh view-frustum culling; the arena's frozen perf gates stay green.
- **G5 — The wake.** Wander wakes in front of the altar on the game's own `home_boss01` player clips
  (the wake/rise portion), with an authored camera; the input is locked during the wake and returns
  after it.
- **G6 — The altar and the idols.** Mono lies on the altar (`mono.glb` posed once by
  `girl_a_death_loop`, a static prop — D14); the sixteen idols stand in the hall, Valus's (`mino_a_statue`) replaced by its
  shipped broken model (`mino_a_statue_broke` + skirt). The break itself is a static swap, labelled.
- **G7 — The proof.** `--s7-selftest --s7-stills out/s7/stills` starts from the S6 `Done` state (S6
  already proves the death itself), drives the transition, the shrine load and the wake, asserts the phases/timings/scene/ground/idols, measures
  the frame cost, and captures stills; `scripts/s7-acceptance.sh` runs it after the frozen S6
  acceptance (which runs S5 → S4.2 → S4.1 → S4 → S3).

## 3 · Non-goals

- The full `home_boss01` cutscene (11 cuts, 978 frames) with its cameras: the
  `camera_home_boss01_cut*` and `objanim_*` clips are the undecoded variant (`AnbReader.cs` rejects
  them); only the wake portion of the player clips plays, under an authored camera.
- The statue break animation (`c01statue_break.cani` is Oodle-compressed with undecoded key
  sampling) and the `bossstatue01` particles; the shadow figures; the tendrils striking Wander.
- Dormin's light shafts as particles (`cathedral_dormin_spirit_light`, `cathedral_lightrays_*`),
  Dormin's voice (`str_boss.sxd1` `homeboss01_*` — attribution and decode untested), the shrine
  ambience/music — a later presentation/audio slice; the shrine is **silent** in S7 (a recorded gap,
  not a placeholder).
- Mono's cloth (`mono_cloth.cmsh` is not in the converted library) and any Mono animation; she is a
  posed static prop.
- The bridge (`levels/bridge` pieces are in local coordinates; their placement is unknown), walking out
  into the garden (drawn as the hall's roof, not a walk target), the overworld, Agro, the sword's light, the second colossus, save, menus.
- Going back to the arena by walking: the shrine is a destination; `R` is the only way back.

## 4 · Anti-goals

- **A1 — A fake shrine.** A hand-placed approximation of the hall would pass a still and miss the
  point. The geometry must come from the shipped stage composition (`shadow.cwks` + the converted
  `cathedral` glbs) and the idols from the shipped statue models; anything placed by the builder
  (idol positions if the RE gate fails, the camera, the spawn) is labelled as such.
- **A2 — Breaking the arena.** The arena manifest, its ground, its numbers and every frozen
  acceptance (S3…S6) must stay identical; S7 adds `s7-*` lines and stills only.
- **A3 — A stuck or soft-locked loop.** Every phase ends; `R` returns to the arena's opening state
  from anywhere (including mid-fade and in the shrine), `M` and `Escape` stay live.
- **A4 — Overclaiming fidelity.** The note separates the original assets from the authored
  camera/timings/positions and the undecoded clips.
- **A5 — Cursor capture or a fullscreen window.** The viewer stays windowed; the cursor is never
  captured.

## 5 · Constraints

- Apple Silicon, C++20, SDL3/SDL_GPU/Metal, arm64 only; no new runtime dependency.
- Offline-first: the runtime never parses game containers; the stage probe and the converter run at
  build time; the game tree is read-only; assets/derived files are gitignored, never committed.
- Static-only RE (Ghidra 12.1.4 + Ghidra-MCP, REA 6.3.0, offline scans); evidence lives in
  `docs/notes/s7-recon.md` and the SDD records.
- Bash 3.2 scripts; every viewer/acceptance run under `caffeinate -dimsu`; never two acceptances
  concurrently.
- Frozen: `scripts/s6-acceptance.sh`, `s5-acceptance.sh`, `s42-acceptance.sh`, `s41-acceptance.sh`,
  `s4-acceptance.sh`, `s3-acceptance.sh`, the arena manifest (`out/re/stage/encounter.manifest` and
  its source composition), `s5_map.json`/`s6_map.json`. The S6 selftest output must not change.
- ≤ 4 concurrent agents (W1 app, W2 RE/binary, W3 data/composition, W4 pipeline/acceptance); W1 and
  W2 never share files — findings cross as notes.

## 6 · What the game does (evidence — the 2026-10-22 recons)

**The flow is code, not data.** The eboot carries the original game's flag enum (399 `G_FLAG_*`
names, table at `0x1604d00`): `DEAD_MINOTOUR_A` = 45, `HOME_ITEM{1..16}_LIGHT` 165–180,
`HOME_ITEM*_START_CAM` 191–206, `HOME_ITEM*_GET_EVENT` 217–232 (the idols), `<boss>_HOME_END`
540–554, `NEXT_<boss>` 555–569. No flag/event name appears under `scripts/`, `misc/`, `cp3shadow/data/`
or `workspaces/`. Candidate code: `0x7df820` reads flag 45 then the `HOME_ITEM*_GET_EVENT` flags
(the dead-boss → idol logic); `0x5707e0` sets flags. Fades: `CMOLdrFadeToColor`, `FadeTransition`
(S6 recon). Unverified addresses are guesses until the RE gate (§8.7) decompiles them.

**The shrine stage.** `models/shadow/levels/cathedral/`: 118 `.cmsh`, all converted `ok` (68 with
geometry, 50 hash-suffix stubs, 6 collision). `workspaces/_ps4/shadow.cwks` lists exactly the 112
non-collision models (the arena's pattern) plus 24 cathedral particle files. The cathedral glbs are
world-placed, centred near the origin (x ≈ −21…21). Of the 62 non-stub, non-collision glbs, 17 are
`_lod1–3`; the remaining **45 highest-detail** glbs (the recon's scratch set: **3,550 meshes / 2.08M
vertices / 116 MB**; arena: 110 meshes, ~0.39M vertices) include the three `cathedral_garden*`
glbs, five occluders (`occluder1`, `occluderint1`, `occluderintlow`, `occluderstairs1`,
`cathedral_sg_occluder`), four shadow casters/blockers (`cathint_shadow`, `cathedral_sg_shadow`,
`shadowcaster1`, `cath_int_shadowblocker_pool`), `emitter_arch_lightray_long`, and `_low`
duplicates beside full models (e.g. `cathedral_interior_low`). `tools/re/stage/probe.py compose` is
pinned to `arena_01` (`probe.py:121–189`) and lists the `ARENA_SETS` directories, not cwks order; its
only sha256 pin (`CWKS_ARENA_SHA256`, `probe.py:123`) hashes the cwks **path list**, not a composed
manifest; `check-manifest` (11/11) and `check-encounter` (22/22) are structural and exit 0 today;
`encounter.manifest`'s header embeds an absolute worktree path, so it cannot be recomposed
byte-identically. `misc/playerstartpositions.txt` names `world/home/home.sgpr` and
`world/cathedral/cathedral_{ext,int,garden}.sgpr` start records (lines 19–21, 40) — at x ≈ 300,
z ≈ −299, **not** the cathedral glbs' frame; `misc/zoneinfo/_cmn/shadowzones.cxml` has the `home`
zone (`#ffff0000`). The camera
table `cp3shadow/camdata/stage45.csv` has `HOME_BOSS01_CUT1..11` rows.

**The ground problem.** The app's heightmap (`app/src/heightmap.h`) keeps the **maximum** Y per 1 m
node. Inside the shrine that is a roof: at (0, 0) the floor is y = 9.0 and the highest surface
y = 60.4 (the garden). The shipped player-floor meshes exist
(`cathedral_*_collision_stairs_player_collision.glb`, floor y 9–11); the `bp_sanctuary_col_div16_*`
glbs appear to sit in another frame (unverified). The player has **no horizontal collision** at all
(`app/src/player.h:9`): movement is clamped to the scene's XZ bounds and snapped to the ground every
frame. Interactive play reaches the ground through `player_step` → `player.cpp:89`,
`collision.cpp:109` and the fall at `player_state.cpp:450`; the eight `heightmap_sample` sites in
`app.cpp` (2624…5263) are all inside the frozen S3–S4.2 scripted selftests (11 call sites in all).

**The idols.** `models/shadow/objects/actors/statues/`: 146 `.cmsh`, all converted — 16 intact
statues; Valus's set is `_cmn/mino_a_statue.glb`, `colossus_01_minotaur_a/_cmn/
mino_a_statue_broke.glb` (3 meshes) + `_skirt` + `_cracks_emitter`, and a skinned
`_cmn/c01statue_break.glb` (217 joints). The statue glbs are origin-centred; **no placement record
was found in data** — the names `BossStatue_Whole_*` appear only in the eboot.

**The altar and Mono.** The altar is part of `cathedral_interior.glb` (material `lyr_temple_altar`).
`characters/mono/_cmn/mono.glb` (317 joints; the cloth glb is not converted); all 75 `girl_a.skb`
bones exist by name; `girl_a_death_loop.anb` (46 frames, 75 tracks) is the converter-readable
variant — the lying pose.

**The wake.** `player_a_home_boss01_cut01..11` (29 tracks, 978 frames total) and the head clips are
converter-readable; the camera/objanim clips are not. Which cut(s) carry the wake/rise is not yet
identified (RE gate §8.7). `bp-convert inspect` prints only mesh/vertex counts; the clip analysis
uses `tools/re/anb/pose_dump.py` or a scratch-`--out` bake.

**The app today (what S7 must work around).** `run_app` builds one stage for its lifetime:
`player_rig_record` (`app.cpp:2124`) and `playback` (2183) are references into the scene, `entityRig`
and `valus_behavior` (2179, 2357) are fixed, and the club/corpse meshes are appended before the one
`upload` (2086–2096; `Renderer::upload` is one-shot). Any manifest `entity` is treated as **Valus**:
`entities[0]` must carry every reaction/death/stomp/club clip or the app exits 2 (2263–2287), and its
presence turns on the club, the corpse, the HUD (2154) and the encounter. The HUD draws only when
`hud_.visible` (`renderer.cpp:954`). Mesh `model` matrices move every frame for the player
(`apply_player_transform`), the entity and the club, so bounds fixed at compose time are wrong for
them. `wander.glb` carries 15 clips and `app/tests/test_asset_io.cpp:499, 938, 1007` pin that count;
every frozen acceptance runs that suite. The S6 selftest's frame helpers (`render_death_frame`, its
timeline) are local to its `if (opts.s6Selftest)` block; only `soft_reset_encounter` is shared.

## 7 · Decisions

| # | Decision | By | Why / cost if wrong |
|---|---|---|---|
| D1 | S7 = the shrine return (not Quadratus/Agro/sword light) | Builder | §1. Cost: the next playable gameplay (riding, a new fight) waits one milestone |
| D2 | The stage probe gains a **per-stage config** (`--stage arena_01 \| shrine`). The arena is proven unchanged by **new** hash gates: the sha256 of a fresh `valus.manifest` compose equals the pre-refactor value, `check-cwks`/`check-manifest`/`check-encounter` still exit 0, and the existing `encounter.manifest` is never recomposed | Builder | One composer, no fork. Cost: a refactor of a gated RE tool; the hash gate is new evidence, not an existing check |
| D3 | The shrine set = the 45 highest-detail glbs **minus** the five occluders, the four shadow casters/blockers, `emitter_arch_lightray_long`, and every `_low`/`intlow` glb that has a full-detail sibling; the garden glbs **stay** (they are the roof the hall stands under). Count, mesh/vertex totals and the manifest's sha256 are measured and pinned on first compose. Bridge pieces are not in this directory and stay out | Builder | Occluders/shadow proxies would draw opaque and be read as floors; `_low` duplicates double the draw. Cost: if a kept glb is still a proxy, the stills show it and the list grows |
| D4 | A **layered** ground, built only for stages that ask for it (the shrine): every surface per node (a small sorted list); `heightmap_floor` returns the highest surface at or below the probe height + a step (default 0.6 m). An unlayered map (the arena) answers `heightmap_floor` exactly as `heightmap_sample`. The three interactive ground sites (`player.cpp:89`, `collision.cpp:109`, `player_state.cpp:450`) switch; the eight frozen-selftest sites in `app.cpp` stay | Builder — flagged for veto | Picks the floor in a building without new collision assets; the arena's ground is identical by construction. Cost: memory (~1.15M cells is fine) and a heightmap API extension |
| D5 | Per-mesh **frustum culling** in `render_frame`: each mesh keeps a **local** AABB, transformed by its **current** `model` every frame; meshes with `rig >= 0` are never culled; no LOD/occlusion | Builder | ~3,550 draws; culling is the cheapest win; per-frame transform keeps the moving player/entity/club correct. Cost: if the measured shrine misses 60 fps, LOD selection is the next step (recorded, not built) |
| D6 | **The stage loop.** `run_app`'s per-stage section (scene → rigs → encounter → main loop) runs inside an outer loop; the main loop ends with a `StageRequest` (quit, or load a stage), the window, GPU device, audio and options persist, `Renderer::release_scene()` runs between iterations, and a small `StageCarry` struct carries what crosses (the return state, the selftest's progress). Every reference into the scene is rebuilt per iteration by construction. The frozen selftests never request a switch | Builder | The cheapest correct way to rebuild a run that holds references into its scene; a blocking load behind a white screen is the game's own pattern. Cost: one structural refactor of `run_app` (re-indentation + the request), proven by the frozen chain |
| D7 | The transition is a **pure phase module** (`transition.h/.cpp`: `Hold → FadeOut → Load → FadeIn → Wake → Free`), driven by the S6 `Done` and the app's load completion | Builder | Same shape as `death.h`; headless-testable. Cost: one module and tests |
| D8 | The idol positions come from an **RE gate** (the `BossStatue_Whole_*` references / the shrine's object table); if the gate fails, the builder places them on the pedestals measured from `cathedral_interior.glb`, **labelled** | Builder — flagged for veto | No placement record in data. Cost: authored positions if the binary does not yield them |
| D9 | The wake plays the identified `player_a_home_boss01_cut*` portion in place at an authored spawn in front of the altar; the camera is authored (the clip cameras are undecoded) | Builder — flagged for veto | Uses the game's own motion; the cameras cannot play. Cost: an authored camera |
| D10 | The shrine is silent in S7 (no voice, no ambience, no music) | Builder | Dormin's voice attribution/decode is untested; a guessed cue would be a placeholder (S5 rule). Cost: a silent shrine until the audio slice |
| D11 | `R` from the shrine (or mid-transition) reloads the arena and restores the S6 opening state; `--stage shrine` starts the viewer in the shrine directly (dev and walk aid) | Builder | A guaranteed way back (A3) and a fast walk loop. Cost: the reverse switch path |
| D12 | **Walls in layered stages:** a per-(node, layer) *blocked* bit, set where a steep triangle (|n.y| < 0.5) crosses the node's XZ cell within the body band [floor + 0.6 m, floor + 1.8 m] of that layer; the player's horizontal move is rejected per axis (slide) when the target's selected floor layer is blocked or has no floor within 0.6 m up / 3 m down. Unlayered maps carry no mask (the arena moves exactly as today) | Builder — flagged for veto | Without it the walk passes through pillars, idols and walls and leaves the building. Cost: a coarse 1 m mask (thin gaps may block; the altar/idol bases block by their sides) |
| D13 | The return fires **automatically** after `Done` + `Hold` in interactive play (the game's behaviour); `--no-return` keeps S6's end state for the pending S6 walk | Builder | The S6 walk still needs the walkable corpse state. Cost: one flag |
| D14 | Mono is a new manifest record **`posed <glb> <px py pz> <qx qy qz qw> <sx sy sz> <clip> <seconds>`**: `compose_scene` samples the clip once and CPU-skins the glb into **static** meshes (`rig = -1`). The statues are plain `instance` lines (17: slot 1 is the broken glb + its skirt) | Builder | An `entity` is Valus to the app (exit 2 without his clips); a lying pose needs no rig. Cost: a CPU-skinning path at compose time and a manifest record (+ parser test) |

## 8 · The design

### 8.1 The stage probe (`tools/re/stage/probe.py`, W3)

- A per-stage config table: `arena_01` (today's sets, the count of 30, the sha256, the spawn — moved,
  not changed) and `shrine` (the cwks prefix `levels/cathedral/`, the exclusion rules: hash-suffix
  stubs, `_lod*`, collision, the D3 proxies and `_low` duplicates — the garden stays; the expected instance count measured on first run and then
  pinned; the spawn from the RE gate or the builder).
- `compose --stage <name> --library <dir> --out <file>` and `check-manifest --stage <name>`; the
  no-`--stage` form is `arena_01`. Before the refactor the sha256 of a fresh `valus.manifest` compose
  is recorded; after it, the same compose must hash the same and `check-cwks`, `check-manifest`,
  `check-encounter` must exit 0. `compose-encounter` is not re-run (its header carries an absolute
  path).
- Output: `out/re/stage/shrine.manifest` (gitignored): the D3 instances, 17 statue `instance` lines,
  one `posed` Mono line (D14), the `spawn`; no absolute paths in its header, so its sha256 is
  pinnable. `scene-manifest.md` documents `posed`.

### 8.2 Ground (`app/src/heightmap.h/.cpp`, W1)

- `build_heightmap(scene, cellSize, layered)` — `layered = false` (the default, the arena) is today's
  build, byte for byte; `layered = true` also keeps, per node, every distinct surface height (merged
  within ε, capped at a small N, default 8 — the cap is measured against the shrine). `heights`
  (max-Y) is filled either way.
- `heightmap_floor(hm, p, step)` returns, on a layered map, the highest surface ≤ `p.y + step` with
  the same bilinear / nearest-valid rules as `heightmap_sample` (the lowest surface when none is
  below); on an unlayered map it **is** `heightmap_sample`. The player's ground calls switch to it;
  a unit test pins the unlayered identity.
- The spawn's Y is the floor at the spawn, not the column max.
- **Walls (D12):** the layered build also fills the per-(node, layer) blocked bits from steep
  triangles; `heightmap_walkable(hm, from, to)` answers whether a horizontal move may land on `to`,
  and `player_step` applies it per axis. On an unlayered map it is always true.

### 8.3 Culling and the stage switch (`app/src/renderer.*`, `scene.h`, W1)

- Each mesh carries a **local** AABB (computed at scene build); `render_frame` transforms it by the
  mesh's current `model` and skips it when it is outside the view-projection's frustum; `rig >= 0`
  meshes are always drawn. The frozen perf numbers are re-measured by the frozen scripts; they must
  stay within their gates.
- `Renderer::release_scene()` frees the uploaded scene resources so `upload()` can run again. The
  stage loop (D6) owns the switch: the transition's `Load` phase and `R` end the iteration with a
  `StageRequest`, the next iteration composes, builds the ground (layered for the shrine), uploads and
  prints `s7-load stage=… instances=… meshes=… ms=…`. A stage with no `entity` runs no encounter
  (no club, corpse, Valus HUD or AI) — the shrine has none.

### 8.4 The transition (`app/src/transition.h/.cpp`, W1)

```cpp
enum class ReturnPhase { Idle, Hold, FadeOut, Load, FadeIn, Wake, Free };
struct ReturnState { ReturnPhase phase; float t; float fade; bool inputLocked; };
struct ReturnTimings { float hold, fadeOut, fadeIn, wake; };
ReturnState step_return(ReturnState s, float dt, const ReturnTimings& tm, bool deathDone, bool loadDone);
```

- `Idle` until the S6 machine reaches `Done`; `Hold` (default 2.0 s — the corpse beat), `FadeOut`
  (default 1.0 s, fade → 1, colour white — RE gate may refine the colour/time from the fade calls),
  `Load` (fade = 1, waits for `loadDone`; the app loads the shrine on entry), `FadeIn` (default 1.5 s),
  `Wake` (the wake clip's duration), `Free` (input unlocked; terminal until `R`).
- Input is locked from `FadeOut` through `Wake`; `R`/`M`/`Escape` stay live in every phase. The fade
  is a full-screen quad drawn last, **outside** the HUD's `visible` gate (a pure `hud_fade_rect`
  helper gives its rect/alpha). `--no-return` (D13) disables the machine in interactive play.
- The S6 machine and its selftest are untouched: S6 `Done` stays stable and walkable for the
  duration of `Hold`, and the S6 selftest's timeline ends before `Hold` expires (the S7 machine only
  runs in interactive play or under `--s7-selftest`; no frozen selftest — `--s3` … `--s6-selftest`,
  including S4.1's real defeat — ever advances it). The S6 acceptance output is the regression.

### 8.5 The wake, the altar and the idols (W1 + W4)

- **Bakes (W4):** `mono.glb` with `girl_a_death_loop`, and `wander.glb` re-baked with its existing 15
  clips **first, in their current order**, then the identified `player_a_home_boss01_cut*` clips
  (single-model scope, `--clips`, `--force`, per `AGENTS.md`); the three `test_asset_io.cpp` clip-count
  pins move to the new count in the same commit. The bake runs alone (no acceptance running). The
  converter's `ClipMotion` label is provenance only and is not touched.
- **Placement:** Mono on the altar (the altar's top from `cathedral_interior.glb`, yaw along the
  hall — builder call, labelled) as a `posed` prop; the 16 statues at the RE-gate positions or the
  labelled pedestal fit (D8); slot 1 = `mino_a_statue_broke` + `_skirt` instead of `mino_a_statue`.
- **Wake:** Wander spawns in front of the altar; the wake clip plays once in place (root x/z
  zeroed, as every clip in the app); the camera is an authored two-key path (low on Wander → behind
  him facing the altar), eased; then the follow camera and control return.

### 8.6 The proof (W4)

- `--s7-selftest --s7-stills out/s7/stills`: seeds the S6 machine at `Done` (the encounter defeated,
  the corpse shown through the same helpers the interactive loop uses — S6 already proves the death),
  then the transition → the shrine load → the wake → `Free`, a scripted walk into a wall, then `R`
  back to the arena and a second shrine load. Lines: `s7-phase` (phase, t,
  fade, lock), `s7-load` (stage, instances, meshes, ms), `s7-ground` (named floor probes: the
  spawn, the altar step, a mid-hall point, a stair — floor Y vs the column max, proving the floor is
  picked), `s7-wall` (the scripted walk stops short of the named wall), `s7-idols` (count 16,
  broken slot 1), `s7-mono` (present, pose clip), `s7-perf`
  (240 frames in the shrine: frame ms, culled/drawn counts), `s7-status`, `s7-reset` (back to the
  arena opening state).
- Stills (2560×1440, span/size checks + crop-local pixel guards like S6): `fade.png` (mid-fade,
  near-white), `wake.png` (Wander at the altar), `altar.png` (Mono on the altar), `idols.png` (the hall
  with the broken first idol), `hall.png` (a wide).
- `scripts/s7-acceptance.sh`: build + both suites; the **frozen S6 acceptance** (its chain runs
  S5 → S4.2 → S4.1 → S4 → S3); the probe's `check-manifest --stage shrine` and the arena's unchanged
  gate; `--s7-selftest`; the line/still/pixel/perf assertions (≥ 60 fps class in the shrine; the load
  time recorded with a generous ceiling); `PASS —`.

### 8.7 The RE gates (W2, before the dependent W1 tasks)

- **R1 — idol placement:** the `BossStatue_Whole_*` / `BossStatus_*` references → the code that
  places the statues (a position table, a cgpr/sgpr load, or a transform array). Output: 16 positions
  + yaws with addresses, or "not found" (then D8's fallback).
- **R2 — the post-defeat flow:** decompile `0x7df820` and `0x5707e0`'s callers; the fade calls on the
  `DEAD_MINOTOUR_A` → `MINOTOUR_A_HOME_END` path: the order (fade colour/time, stage load, idol event,
  wake). Output: the timings S7 adopts or the "authored" label.
- **R3 — the wake and the spawn:** which `home_boss01` cut(s) contain the rise (root/pose analysis
  of the decoded clips — data, not binary), the spawn/yaw from `home.sgpr`/`cathedral_int.sgpr`
  start records or the binary; the frame of the cathedral glbs vs those records.
- Evidence goes to `docs/notes/s7-recon.md` (addresses, counts, sizes — no game content), each gate
  with a pass/fail line; a failed gate selects the labelled fallback, it never blocks the milestone.

## 9 · The builder's calls (left open, with defaults)

- The hold/fade/fade-in durations (2.0/1.0/1.5 s) and the fade colour (white), unless R2 answers them.
- The floor-selection step (0.6 m), the per-node surface cap (8), the wall mask's steepness (0.5) and
  body band (0.6–1.8 m).
- The shrine instance set's exact exclusions; the spawn and yaw if R3 fails; Mono's placement; the
  pedestal fit if R1 fails; Mono's pose time within `girl_a_death_loop`.
- The wake camera's two keys and easing; the stills' framing and pixel thresholds.
- The perf gate's form (the S6 pattern: frame ms + a measured-noise marginal) and the load-time
  ceiling (default 30 s).

## 10 · Risks

| Risk | Severity | Mitigation |
|---|---|---|
| The shrine misses 60 fps even with culling (2.1M verts, 3,550 meshes) | medium | Measure first (Task: a culled 240-frame run); LOD/instancing is the recorded next step; the scene set (D3) can drop to the hall interior |
| Floor selection misreads stairs/ramps (the step too small or large) | medium | Named floor probes in the selftest; the stair probe; the step is a builder knob |
| Idol positions not recoverable from the binary | medium | D8's labelled pedestal fit |
| The wake cut cannot be identified or looks wrong in place | medium | R3's root/pose analysis; fallback: Wander spawns standing (labelled) |
| The stage switch leaks GPU resources or stalls | medium | `release_scene` + a selftest that loads arena → shrine → arena; the load time is printed |
| Culling or the heightmap change perturbs the frozen S3–S6 numbers | high | The arena answers identically (unit test); culling only removes off-screen draws; the frozen acceptances are the regression |
| The probe refactor changes the arena manifest | high | The new before/after sha256 of `valus.manifest` + the three existing structural checks; `encounter.manifest` is never recomposed |
| The stage-loop refactor perturbs a frozen selftest | high | A behaviour-preserving task of its own, proven by the frozen S6 chain before any S7 wiring lands |
| The wall mask blocks doorways or misses thin geometry | medium | A 1 m grid, the body band and the `s7-wall` probe; doorways are ≥ 2 m in the hall; the band/threshold are builder knobs |

## 11 · The walk (the user's eye, PENDING after implementation)

- Defeat Valus (or `--stage shrine`): the hold on the corpse, the fade to white, the shrine fades in,
  Wander wakes in front of the altar; control returns.
- Mono lies on the altar; sixteen idols, the first one broken; the hall reads as the game's shrine.
- Walk the hall, the altar steps and the stairs: no falling through, no standing on the roof; walls,
  pillars, the altar and the idols stop Wander.
- `R` returns to the arena; no clipping, the window stays and the cursor is free, the fps holds.
- Deferred/known: Dormin's voice and light, the statue break animation, the cutscene cameras, the
  bridge, the garden walk, the shrine's sound, the coarse (1 m) wall mask.

## 12 · Acceptance

Controls/windowed/cursor unchanged; the S6 (→ S5 → S4.2 → S4.1 → S4 → S3) scripts green and
unchanged; `scripts/s7-acceptance.sh` green with the stills; `docs/notes/s7-recon.md` records the RE
gates; `docs/notes/s7-results.md` quotes measured values; assets not committed; the S7 walk is
added to the user's pending walks.
