# S4.0 "The encounter" — Valus in the arena — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Valus arena stops being empty: colossus #1 stands in it — the converted `c01_minotaur_a.glb` playing its decoded `minotaur_a_stand` idle — placed with recorded provenance, approachable (a collision proxy stops Wander at his legs), and framable (`T`), with a dedicated encounter manifest and acceptance script.

**Architecture:** The converter reuses the S2 `--clips`/writer path (no new emission code); the milestone's work is validation (minotaur bank/mapping/pose) plus the app's move from one global rig to per-rig records: `Scene.rigs` (skin, clips, mesh range), one palette buffer per rig, one `Playback` per rig, a manifest `entity` directive, an entity-aware heightmap, a capsule collision module, and a target-framing camera state. Placement is a timeboxed data-mining pull with a labeled builder-pick fallback; the encounter manifest derives from S3's composition (one source for the arena instances).

**Tech Stack:** .NET 10 / xUnit / SharpGLTF (converter + tests); C++20 / SDL3 / cgltf / doctest / ctest (app); Python 3 (RE probes).

**Spec:** `docs/superpowers/specs/2026-10-19-s4-encounter-design.md`
**Recon:** `docs/notes/s4-recon.md`

## Global Constraints

- SDL runtime; windowed; the cursor is never captured.
- Offline-first: the app consumes converted glbs + manifests only; no original containers at runtime.
- Static-only RE: no game/port runs; the dev-only port is not used; no Valus capture exists.
- Full-res Valus only (LOD1 is a measurement control, not shippable).
- Game assets and derived glbs stay uncommitted (`out/` is gitignored).
- S3's manifest, script, and walk stay valid; the encounter is a separate manifest + acceptance path.
- Tests that need the game tree/library fail fast when `SOTC_GAME_ROOT`/`SOTC_LIB` is unset (never skip silently).
- Verification: `dotnet test tests/BpConvert.Tests -v q`; `ctest --test-dir app/build --output-on-failure`.
- Typed commits (`feat:`, `fix:`, `test:`, `docs:`, `chore:`).
- Apple Silicon; GPL-3.0.

## Review Focus

- **Wrong minotaur mapping/pose → a folded or mirrored Valus.** Pinned by Task 1 (58/58 map, world-pose geometry vs the RE tool's frame-0 values) and Task 6 (stills).
- **Palette bleed between rigs → Valus deformed by Wander's animation.** Pinned by Task 4 (two palettes, mesh→rig binding) and Task 6 (idle pixel guard: only Valus's region changes when his time advances).
- **Floating/sunk Valus.** Pinned by Task 6 (`s4-status feet=ok`; the lowest posed vertex at the bowl floor) and the stills.
- **Placement mislabeled as data.** Pinned by Task 2 (the acceptance bar) and Task 6 (the manifest header + `s4-status` provenance).
- **S3 regression.** Pinned by Task 6 (the S3 acceptance script runs green unchanged).
- **Camera disorientation on `T`.** Pinned by Task 5 (movement basis = the active camera's yaw; state restore) and the user walk.

## File Structure

Converter — modified (no new emission code):
- `tests/BpConvert.Tests/SkbReaderTests.cs` — minotaur bank pins.
- `tests/BpConvert.Tests/TrackMapTests.cs` — minotaur 58/58 pins.
- `tests/BpConvert.Tests/AnimWriterTests.cs` — minotaur stand shape + world-pose pins.
- `tools/re/anb/pose_dump.py` — generalized to an arbitrary glb/clip/bank (minotaur xval).

RE:
- `tools/re/stage/spawn_probe.py` (new, optional) + `docs/notes/re4-placement.md`.
- `docs/notes/s4-pose.md` (minotaur pose validation record).

App — modified:
- `app/src/scene.h` — `RigCpu`, `Scene.rigs`, `MeshCpu.rig`.
- `app/src/manifest.h` / `manifest.cpp` — `entity` directive.
- `app/src/asset_io.h` / `asset_io.cpp` — entity loading, rig ranges, heightmap sources.
- `app/src/anim.h` / `anim.cpp` — per-rig playback helpers.
- `app/src/renderer.h` / `renderer.cpp` — per-rig palette buffers/binding.
- `app/src/app.cpp` — wiring, `s4-status`, `--s4-selftest`, `--entity-time`, `--camera-target`.
- `app/src/camera.h` / `camera.cpp` — target-framing state.
- `app/src/collision.h` / `collision.cpp` (new) — capsule push-out.
- `app/tests/test_manifest.cpp`, `test_asset_io.cpp`, `test_anim.cpp`, `test_camera.cpp`, `test_player.cpp` — tests.

Tools/scripts:
- `tools/re/stage/probe.py` (or a new composer) — the encounter manifest.
- `scripts/s4-acceptance.sh` (new).

Docs:
- `docs/notes/s4-results.md` (Task 7).

---

### Task 1: Converter — bake Valus's idle + minotaur mapping/pose validation

**Files:**
- Modify: `tests/BpConvert.Tests/SkbReaderTests.cs`, `tests/BpConvert.Tests/TrackMapTests.cs`, `tests/BpConvert.Tests/AnimWriterTests.cs`
- Modify: `tools/re/anb/pose_dump.py` (only if it is hard-wired to wander — read it first)
- Create: `docs/notes/s4-pose.md`
- Artifact (gitignored): `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb`

**Interfaces:**
- Consumes: the S2 `--clips` path (`docs/notes/s2-results.md` §1), `AnbReader`, `SkbReader`, `TrackMap`, `GlbWriter`.
- Produces: a glb with exactly one animation, `minotaur_a_stand`; the pose record in `docs/notes/s4-pose.md` (the frame-0 world heights the app test pins).

- [ ] **Step 1: Re-convert with the clip** (single-file `--under`; `--force`; the S2 documented form):
  ```bash
  export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
  dotnet publish src/BpConvert.Cli -c Release -o out/bin
  dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
    --under models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh \
    --clips minotaur_a_stand --force
  ```
  Record: the catalog entry (`ok`), the new glb size/sha256, and — by reloading it — `animations` = 1, name, channel count, duration. Expected: 26 frames at rate 0.5 → 15 fps → duration **1.6667 s**; **58 rotation channels** (21 static, 37 keyed) plus a translation channel iff the reader's `Root` series is non-empty for this clip. Pin the actual count after the first load; a mismatch with 58 is a finding, not a test edit.

- [ ] **Step 2: Extend the minotaur tests (new coverage of existing behavior — expected to pass; a failure is a product finding).**
  ```csharp
  // SkbReaderTests: minotaur_a.skb -> 73 names (58 record-bearing + 15 OptBone_*);
  // parent links sane; first record's name recorded.
  // TrackMapTests: minotaur_a_stand, 58 tracks -> 58 distinct joints in the 238-joint
  // colossus glb, 0 unresolved; the 13 0xD tracks included.
  // AnimWriterTests: minotaur_a_stand -> 1 animation, 26 keys per animated channel,
  // last time 25/15 s; 21 constant (static) rotation channels; no matrix on animated nodes.
  ```
  Run `dotnet test tests/BpConvert.Tests -v q --filter "SkbReader|TrackMap|AnimWriter"` → green. If anything fails, stop and report the measured mismatch (do not adjust the pin to fit).

- [ ] **Step 3: Minotaur pose validation.** Point `pose_dump.py` at `c01_minotaur_a.glb` + `minotaur_a_stand` + `minotaur_a.skb` (add `--glb/--bank/--clip` if it is hard-wired to wander; keep its player defaults working). Run its `verify`/`residuals`/`pose` modes; record in `docs/notes/s4-pose.md`:
  - the bind-vs-bridged residual band per bone (the player's precedent: `re2-xval.md` §2–§3);
  - the frame-0 **world** heights of `Bone_KOSI`, `Bone_MUNE`, `Bone_KUBI`, `Bone_KAO`, and the lowest foot bone — the values the C# world-pose test pins (±0.02 m);
  - a rendered still (the tool's own path) and the observation that the pose reads upright (head above pelvis, feet lowest).
  If the pose is folded/mirrored, stop: this is the milestone's central risk — report the evidence with `DONE_WITH_CONCERNS`; do not attempt a convention change here.

- [ ] **Step 4: Add the C# world-pose pin** (column-vector composition, the S2 `AnimWriterTests` pattern) using the values from Step 3: same glb reload, `Bone_KAO.y > Bone_KOSI.y + <measured margin>`, feet lowest, no NaN; cross-check SharpGLTF's `GetWorldMatrix`.

- [ ] **Step 5: Full converter suite, then commit**
  ```bash
  dotnet test tests/BpConvert.Tests -v q
  git add tests/BpConvert.Tests tools/re/anb/pose_dump.py docs/notes/s4-pose.md
  git commit -m "test: bake and validate Valus idle (minotaur_a_stand)"
  ```

**Parallel:** Task 2 touches disjoint files; it may run concurrently with Task 1.

---

### Task 2: RE — bounded MinA placement pull

**Files:**
- Create: `docs/notes/re4-placement.md`
- Create (only if a probe is needed): `tools/re/stage/spawn_probe.py`
- Read: `docs/notes/re-stage.md`, `tools/re/stage/probe.py`, the vendored CGPR parser (`third_party/PSO2-Aqua-Library/AquaModelLibrary.Data/BluePoint/CGPR/`)

**Interfaces:**
- Consumes: the arena's compiled scripts `scripts/shadow/world/arena_01/_cmn/arena_01.cgpr`, `arena_01_shared.cgpr`, `arena_01.csml`; `cp3shadow/path/pathdata/minotaur.path`; the `.fs`/`.cwks` families; the analyzed `eboot` names.
- Produces: a **positive** record (file, offset, type/name, field layout, world point) or a **documented negative**. No app/converter changes.

- [ ] **Step 1: Read the stage notes and the CGPR structure parser** to know what is already decoded vs open (`re-stage.md` §1.3/§4.3; the parser's component/property types).
- [ ] **Step 2: Probe the candidates, in this order:** the arena `.cgpr` property graph (the MinA relay/spawn/body references); the `.csml` scene/instances; the `.cwk` workspace records; the `.fs` hash tables (only if a hash→name namespace is available). For each hit, capture the raw bytes around it and test whether a plausible transform (3 floats + quaternion/yaw) follows.
- [ ] **Step 3: Apply the acceptance bar** (design §7.3): a record counts only if it is named/typed as the MinA spawn/placement **and** its world point is inside the arena bowl (x≈237–431, z≈297–390), near the floor (y≈28–31 if world), and within ~100 m of the player start (353, 345). A bare float triple is a negative. Record the cross-check numbers for every candidate, including rejected ones.
- [ ] **Step 4: Write `docs/notes/re4-placement.md`** — method, candidates, measurements, verdict (`data` or `negative`), and the exact fields the composer (Task 6) should consume if positive. If negative, state the builder-pick fallback and its suggested spot (bowl floor, facing the spawn) for the plan's Task 6.
- [ ] **Step 5: Commit** — `docs: S4 MinA placement probe` (add the tool if one was written).

---

### Task 3: App — rig records, `entity` directive, entity-aware heightmap

**Files:**
- Modify: `app/src/scene.h`, `app/src/manifest.h`/`manifest.cpp`, `app/src/asset_io.h`/`asset_io.cpp`, `app/src/app.cpp` (load wiring)
- Test: `app/tests/test_manifest.cpp`, `app/tests/test_asset_io.cpp`, `app/tests/test_heightmap.cpp`

**Interfaces:**
- Consumes: the S1/S2/S3 loaders, the Task 1 glb.
- Produces:
  ```cpp
  // scene.h
  struct MeshCpu { /* ... */ int rig = -1; };           // -1 = static/unskinned
  struct RigCpu {
      SkinCpu skin;
      std::vector<AnimationCpu> animations;
      std::size_t meshBegin, meshEnd;                  // half-open range in Scene.meshes
      bool dynamic;                                    // the player rig
  };
  struct Scene { /* ... */ std::vector<RigCpu> rigs; std::size_t playerRig = 0; };
  // manifest.h
  struct ManifestEntity { ManifestInstance placement; std::string clip; };
  struct SceneManifest { /* ... */ std::vector<ManifestEntity> entities; };
  // directive: entity <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName>
  ```
  `compose_scene` order: static instances → the player glb (`includeRig=true`, rig 0) → entities (each `includeRig=true`, one rig, exact clip name; a missing clip is a load error naming the glb and clip). `rigs[0]` is the player; `playerMeshBegin/End` become `rigs[0]`'s range (keep an alias or update call sites). The heightmap builds from `mesh.rig < 0` only; its bounds behaviour is unchanged (S3 parity).

- [ ] **Step 1: Write failing parser tests** — `entity` valid line parses (transform + clip); wrong token count / non-finite / unknown directive fail with `<file>:<line>:`; a manifest without `entity` is unchanged; `spawn` still works.
- [ ] **Step 2: ctest → FAIL.**
- [ ] **Step 3: Implement the directive + rig records.** `compose_scene` records each rig's mesh range; entity meshes get `rig = 1..`; static meshes keep `-1`; the player's tail range is preserved for `apply_player_transform`.
- [ ] **Step 4: Integration test with the real glbs** — compose a temp manifest with `wander.glb` (player, `--model`) and `c01_minotaur_a.glb` as an entity (clip `minotaur_a_stand`): 2 rigs, joint counts 348/238, disjoint mesh ranges, the entity's clip found. (Library-gated: fail fast without `SOTC_LIB`.)
- [ ] **Step 5: Heightmap test** — build the heightmap over the same scene; assert no cell is sourced from the entity's meshes (e.g. the entity placed over a cell changes nothing) and the static ground still samples.
- [ ] **Step 6: ctest green; commit** — `feat(app): rig records + entity manifest directive`.

---

### Task 4: App — per-rig palettes + entity playback

**Files:**
- Modify: `app/src/renderer.h`/`renderer.cpp`, `app/src/anim.h`/`anim.cpp`, `app/src/app.cpp`
- Test: `app/tests/test_anim.cpp`, `app/tests/test_asset_io.cpp`

**Interfaces:**
- Consumes: Task 3's rigs.
- Produces:
  - `Renderer` keeps one pose buffer **per rig** (sized by that rig's joint count) and binds the mesh's rig palette before the draw; static meshes may bind rig 0 (zero weights pass through).
  - `update_pose(scene, rigIndex, playback, palette)` and `upload_pose(rigIndex, palette)`; the player uses rig 0; the entity loops its clip.
  - `--entity-time <sec>` pins the entity's clip time (captures); `--anim`/`--anim-time` keep their player/single-model meaning.
  - **Entity grounding:** `effective_rig_lift(scene, rig, clip)` — 0 when the clip carries a translation channel on `Bone_root` (the clip owns the ground height, the S2 rule), else the rig's origin-to-feet lift (the lowest bind vertex; **8.739 m** measured for Valus). The entity's model transform adds it; the manifest's y is the ground sample.
  - `s2-anim`/`s4-status` report per-rig clip names and `procedural=0`.

- [ ] **Step 1: Failing tests** — two `Playback`s from one scene produce different palettes; the mesh→rig mapping selects the right palette; an entity with a missing clip fails at load with a clear message; `effective_rig_lift` returns 0 for a root-carrying clip and 8.739 for the minotaur bind.
- [ ] **Step 2: ctest → FAIL.**
- [ ] **Step 3: Implement.** Keep the player's path as rig 0; upload each rig's palette per frame; no shader change (per-draw buffer binding). `--entity-time` clamps to the clip duration. Apply the entity lift at the model transform.
- [ ] **Step 4: Run the single-model path unchanged** (Wander + Agro still frame/play) and the S3 selftest — green.
- [ ] **Step 5: ctest green; commit** — `feat(app): per-rig palettes + entity playback`.

---

### Task 5: App — entity collision + target-framing camera

**Files:**
- Create: `app/src/collision.h`/`collision.cpp`
- Modify: `app/src/camera.h`/`camera.cpp`, `app/src/player.h` (capsule fields if needed), `app/src/app.cpp`
- Test: `app/tests/test_player.cpp`, `app/tests/test_camera.cpp`, `app/tests/test_anim.cpp`

**Interfaces:**
- Consumes: the per-rig posed palette (Task 4), the player's `PlayerState`.
- Produces:
  ```cpp
  // collision.h
  struct Capsule { Vec3 a, b; float r; };
  // Player capsule: radius 0.35 m, height 1.7 m, feet at position.y (builder values; pin).
  // Entity capsules from the posed skeleton: both legs (hip->knee->ankle->toe) and the torso
  // chain; radii measured from the body/collision bounds so the player cannot enter the limb
  // silhouette. resolve_player_collision(PlayerState&, const std::vector<Capsule>&) pushes the
  // player out horizontally (2 iterations, no vertical change).
  // camera.h
  // A target-framing state: target = the entity's chest joint world position; distance
  // clamp widened to fit the 17.7 m body (builder value ~22-30 m), pitch clamp widened;
  // drag orbits the target; T toggles; movement basis = the ACTIVE camera's yaw; T-off
  // restores the follow offsets without a snap.
  ```
- [ ] **Step 1: Failing collision tests** — a player inside/overlapping a capsule is pushed out along the shortest horizontal axis; two overlapping capsules resolve; a far player is untouched; the resolved position stays on the heightmap (ground sample reapplied).
- [ ] **Step 2: Failing camera tests** — `T` toggles; target mode fits a 17.7 m subject (target/distance/pitch); restore returns the saved follow offsets; the movement basis follows the active camera's yaw.
- [ ] **Step 3: ctest → FAIL; implement; hook the collision after `player_update` at the three loop sites** (`app.cpp` selftest / screenshot / interactive).
- [ ] **Step 4: Integration test with the real Valus pose (Task 1 glb):** place the player inside each leg capsule at t=0 → pushed out; the collision module receives the palette's bone world positions.
- [ ] **Step 5: ctest green; commit** — `feat(app): entity collision + target framing camera`.

---

### Task 6: Encounter manifest + acceptance

**Files:**
- Modify: `tools/re/stage/probe.py` (or a new `compose_encounter` step) — derive the encounter manifest
- Create: `scripts/s4-acceptance.sh`
- Artifacts (gitignored): `out/re/stage/encounter.manifest`, `out/re/capture/s4/*.png`, `out/s4-acceptance-run.log`
- Test: `app/tests/test_asset_io.cpp` (the encounter manifest parse/compose), plus the script's own checks

**Interfaces:**
- Consumes: Task 2's placement verdict (data or builder pick), the S3 manifest (regenerated if absent: `python3 tools/re/stage/probe.py compose --library <out/library> --out out/re/stage/valus.manifest` + `check-manifest`), Tasks 3–5.
- Produces: `encounter.manifest` = the S3 30 instances + one `entity` + a new `spawn`, with a header recording `placement=data|builder-pick`, the spawn `data-position+builder-yaw`, and the entity clip. `s4-status` reports: instances, rigs, entity clip/joints, placement provenance, spawn provenance, feet-on-floor, position, frames. `--s4-selftest` walks the player toward Valus and asserts the collision stop + feet.

- [ ] **Step 1: Compose the manifest.** Extend the composer (or add a small step) that reads the S3 manifest, rewrites the `spawn` to the data position (353, ground-sampled, builder yaw facing Valus), appends the `entity` line (placement from Task 2 or the labeled builder pick on the bowl floor, feet on the ground, facing the spawn), and writes a new provenance header (S3's header is not inherited). Add `check-encounter` assertions: 30 instances, 1 entity, 1 spawn, provenance present, coordinates in-bounds.
- [ ] **Step 2: `--s4-selftest`.** Load the encounter manifest + wander player; pin the entity's time at 0; print `s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=<...> spawn=<...> feet=ok pos=<x,y,z> frames=...`; simulate a forward walk into Valus and assert the player stops before the leg capsule (a bounded penetration), staying grounded.
- [ ] **Step 3: `scripts/s4-acceptance.sh`** (mirror `s3-acceptance.sh`): build; both suites; ensure/regenerate the S3 manifest; compose/check the encounter manifest; run `--s4-selftest` and parse `s4-status`; captures:
  - `s4-wide.png` (spawn view, `--camera-target player`, entity time 0),
  - `s4-approach.png` (walked near the legs),
  - `s4-portrait.png` (`--camera-target entity`, framing the full body),
  - `s4-idle-t0.png` / `s4-idle-t08.png` (same camera, entity time 0 vs 0.8 — differ only in Valus's screen region; the wave-guard analog);
  pixel checks (PNG magic, size, the entity region non-background, the idle-pair diff inside the entity region only); measured load + marginal frame cost; then `scripts/s3-acceptance.sh` (must stay green).
- [ ] **Step 4: Run it; record the numbers in the run log.** A failure is a finding — fix in the owning task, do not paper over.
- [ ] **Step 5: Commit** — `feat: S4 encounter manifest + acceptance`.

---

### Task 7: Acceptance results + docs

**Files:**
- Create: `docs/notes/s4-results.md`
- Modify: `AGENTS.md`, `app/README.md`
- Plan: add a completion note

- [ ] **Step 1: Write `docs/notes/s4-results.md`** — the environment, the exact commands, the manifest sha256 + provenance, the pose validation, the `s4-status` line, the stills table and what each shows, the idle-pair numbers, the perf/load numbers, the S3 regression run, known gaps (club/knee absent, shell fur absent, banner opaque, no encounter camera, no AI), and the user's walk checklist:
  1. spawn in the bowl; Valus visible ahead, idling;
  2. walk to him; the collision stops you at his legs (no pass-through);
  3. `T` frames the full body; `T` again returns; the camera is not disorienting while moving;
  4. feet on the ground; no float/sink; scale reads right;
  5. fps in the title ≥60 class; cursor free; Escape quits.
  Mark the verdict PENDING (the user's).
- [ ] **Step 2: Docs** — `app/README.md`: the `entity` directive, `--entity-time`, `--camera-target`, `--s4-selftest`, the `T` key; `AGENTS.md`: S4.0 state and the next milestone (user's call).
- [ ] **Step 3: Commit** — `docs: S4 encounter acceptance results`.

---

## Self-Review Notes

- Spec coverage: Goals 1–6 map to Tasks 1 (bake/pose), 2 (placement), 3–4 (two rigs/playback), 5 (collision/camera), 6 (acceptance), 7 (results).
- Interface consistency: `RigCpu`/`Scene.rigs`/`MeshCpu.rig`/`ManifestEntity`/`resolve_player_collision`/`--entity-time`/`--camera-target`/`s4-status` are used with the same names across tasks.
- Values pinned: Valus glb 38,049,000 B (pre-bake), 12 meshes, 238 joints, 523,865 verts, feet −8.739; `minotaur_a_stand` 26 frames / 58 tracks / 15 fps / 1.6667 s / 21 statics; minotaur bank 73 names (58 records); 58/58 mapping; encounter 30 instances + 1 entity; spawn (353, ground, 345) + builder yaw; S3 acceptance unchanged.
- The plan's one open value is the minotaur frame-0 world-pose set — Task 1 measures it with the RE tool and pins it in the same commit (the S2 precedent: the player's 0.7429/0.8343/1.0462/1.1165).

*Personal-use fan project. No game assets are distributed from this repository.*
