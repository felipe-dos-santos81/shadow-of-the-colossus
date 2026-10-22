# S7 "Return to the Shrine" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After Valus's death sequence, fade to white, load the Shrine of Worship composed from the shipped `cathedral` stage, wake Wander in front of the altar (Mono on it, sixteen idols, the first broken), and let the player walk the shrine; `R` returns to the arena.

**Architecture:** Three RE gates (W2) answer the idol positions, the post-defeat flow and the wake clip; the stage probe gains a per-stage config and composes `shrine.manifest` (W3); the converter bakes Mono's lying pose and Wander's wake clip (W4); the app gains a layered ground, per-mesh frustum culling, a re-uploadable renderer, a pure `transition.h` phase machine and the stage switch (W1); `--s7-selftest --s7-stills` and `scripts/s7-acceptance.sh` prove it after the frozen S6 chain.

**Tech Stack:** C++20, SDL3/SDL_GPU/Metal, doctest, .NET 10 (BpConvert), Python 3 (stdlib), Ghidra 12.1.4 + Ghidra-MCP, REA 6.3.0.

**Spec:** `docs/superpowers/specs/2026-10-22-s7-return-to-the-shrine-design.md`

## Global Constraints

- Apple Silicon arm64, C++20, SDL3/SDL_GPU/Metal; no new runtime dependency; windowed, the cursor never captured.
- Offline-first: the runtime never parses game containers; the probe/converter run at build time; the game tree is read-only; assets/derived files gitignored, never committed.
- Static-only RE; evidence in `docs/notes/s7-recon.md` as addresses/counts/sizes, no game content.
- Frozen and invoked unchanged: `scripts/s6-acceptance.sh`, `s5-`, `s42-`, `s41-`, `s4-`, `s3-acceptance.sh`; the arena manifest and its composition (the probe's existing sha256 gate); `s5_map.json`, `s6_map.json`; the `--s6-selftest` output.
- Every viewer/acceptance run under `caffeinate -dimsu`; never two acceptances concurrently; scripts bash 3.2; tests fail fast when `SOTC_GAME_ROOT`/`SOTC_LIB` are unset.
- The shrine is silent in S7 (no voice/ambience/music — a recorded gap, not a placeholder).
- Builder-authored values (camera keys, timings unless R2 answers them, fallback idol/spawn/Mono placement) are labelled "authored" in the notes and the walk.
- ≤ 4 concurrent agents; W1 (app) and W2 (RE) never share files — findings cross as `s7-recon.md` and `tools/re/s7/shrine_placements.json`.

## Review Focus

1. **`R` pressed mid-fade or during `Load`.** The arena must come back in its S6 opening state (Valus 100, HP 100, death Idle, transition Idle, fade 0, input unlocked) with no half-loaded shrine. Test: `test_transition.cpp` "reset from any phase"; Task 6's `s7-reset` line after an `R` injected in `FadeOut`.
2. **A player standing under a roof or on stairs in the shrine.** The ground must be the floor under the feet, not the roof above or the hall below the stairs. Test: `test_heightmap.cpp` two-storey fixture; Task 6's `s7-ground` stair and altar-step probes (floor < column max).
3. **The arena's ground and frame cost after the layered/culling changes.** Every frozen number must hold. Test: `test_heightmap.cpp` "unlayered floor equals sample"; the frozen S6 chain in Task 7.
4. **A missing shrine asset (manifest glb, Mono clip, wake clip).** `--stage shrine` and the transition must fail loudly naming the path, never draw an empty hall. Test: Task 6's `load_stage` throws on a manifest naming a missing glb (a `test_asset_io.cpp` case); `--s7-selftest` is fatal on it.
5. **Repeated arena → shrine → arena switches.** No GPU leak, no stale heightmap, no double upload. Test: Task 6's selftest runs the switch twice and prints `s7-load` for each with equal mesh counts; `Renderer::release_scene` then `upload` in a `test_*`-free windowed run (the selftest).

---

## File Structure

- `tools/re/s7/` — `idols.py` (R1 scan helpers), `wake_clip.py` (R3 root/pose analysis), `shrine_placements.json` (the gates' output: 16 idol transforms, Mono's transform, the spawn, each with a `source` of `binary`/`data`/`authored`).
- `docs/notes/s7-recon.md` — the R1/R2/R3 evidence and pass/fail lines.
- `tools/re/stage/probe.py` — per-stage config (`arena_01`, `shrine`), `--stage`.
- `docs/notes/scene-manifest.md` — unchanged format (`instance`/`entity`/`spawn` cover S7); a `# stage shrine` provenance example.
- `src/BpConvert.Core/Pipeline/ClipMotion.cs` — the `girl_a_`/`player_a_home_` prefixes.
- `app/src/heightmap.h/.cpp` — the layered build + `heightmap_floor`.
- `app/src/scene.h`, `app/src/asset_io.cpp`, `app/src/renderer.h/.cpp` — mesh world AABBs, frustum culling, `release_scene`, the fade quad.
- `app/src/transition.h/.cpp` — the pure return phase machine.
- `app/src/app.cpp`, `app.h`, `main.cpp`, `camera.h/.cpp` — `load_stage`, the wiring, the wake camera, `--stage`, `--s7-selftest`, `--s7-stills`.
- `scripts/s7-acceptance.sh`, `docs/notes/s7-results.md`, `app/README.md`, `AGENTS.md`.

Parallel lanes: Task 0 (W2) ‖ Tasks 2, 3, 4 (W1) ‖ Task 1a (W3). Task 1b needs Task 0; Task 5 (W4) needs R3; Task 6 needs 1b, 2–5; Task 7 needs 6.

### Task 0: The RE gates (W2)

**Files:**
- Create: `tools/re/s7/idols.py`, `tools/re/s7/wake_clip.py`, `tools/re/s7/shrine_placements.json`, `docs/notes/s7-recon.md`

**Interfaces:**
- Produces: `shrine_placements.json`:
  ```json
  {"idols": [{"slot": 1, "glb": "...", "p": [x,y,z], "yaw_deg": a, "source": "binary|authored"}, ...16],
   "broken": {"slot": 1, "glbs": ["...mino_a_statue_broke.glb", "...mino_a_statue_broke_skirt.glb"]},
   "mono": {"glb": "models/shadow/characters/mono/_cmn/mono.glb", "clip": "girl_a_death_loop", "p": [..], "yaw_deg": a, "source": "..."},
   "spawn": {"p": [..], "yaw_deg": a, "source": "..."},
   "wake": {"clips": ["player_a_home_boss01_cutNN", ...], "seconds": s, "source": "data|authored"},
   "flow": {"hold_s": 2.0, "fade_out_s": 1.0, "fade_in_s": 1.5, "colour": [1,1,1], "source": "binary|authored"}}
  ```
  Every value carries its `source`; positions are in the cathedral glbs' world frame.

- [ ] **Step 1: R1 — idol placement.** From the `BossStatue_Whole_*`/`BossStatus_*` strings, find their code references (`tools/re/s7/idols.py`, reusing the recon's xref/rela scan approach, or Ghidra-MCP `get_xrefs_to` + `decompile_function` on an opened copy of the project) and the code that positions the statues. Record addresses and the 16 transforms, or record "not found" and fit the 16 positions to the pedestal tops measured from `cathedral_interior.glb` (`source: authored`).
- [ ] **Step 2: R2 — the post-defeat flow.** Decompile `0x7df820` and the callers of `0x5707e0` on the `DEAD_MINOTOUR_A`(45) → `MINOTOUR_A_HOME_END`(540) path; record the order of fade/load/idol-event/wake and any fade colour/duration constants (`CMOLdrFadeToColor`/`FadeTransition` call sites). Unanswered values keep the design's defaults with `source: authored`.
- [ ] **Step 3: R3 — the wake clip and the spawn.** `wake_clip.py`: for each `player_a_home_boss01_cut01..11` (decoded via the converter's clip export or `bp-convert inspect`), report the root height curve and the end pose; the wake clips are the cut(s) whose root rises from lying (≤ 0.4 m) to standing. Resolve the spawn/yaw from the `home.sgpr`/`cathedral_int.sgpr` records in `misc/playerstartpositions.txt` (lines 19–21, 40) and check their frame against the cathedral glbs (the floor at the spawn must be y 9–11); otherwise author a spawn 3 m in front of the altar facing it.
- [ ] **Step 4: Write `s7-recon.md`** — one section per gate with `R1: PASS|FALLBACK`, the addresses, counts, and the JSON's provenance; validate the JSON parses and has 16 idols, one broken slot, Mono, the spawn, the wake, the flow.
- [ ] **Step 5: Commit** — `git add tools/re/s7 docs/notes/s7-recon.md && git commit -m "chore(re): the S7 shrine gates"`

### Task 1a: The per-stage probe (W3)

**Files:**
- Modify: `tools/re/stage/probe.py` (the `ARENA_*` pins → a `STAGES` table; `compose`/`check-manifest` gain `--stage`)
- Test: the existing gates

**Interfaces:**
- Produces: `python3 tools/re/stage/probe.py compose --stage arena_01|shrine --library <dir> --out <file>`; `check-manifest --stage <name> --library <dir> <file>`; no `--stage` means `arena_01`.

- [ ] **Step 1: Record the baseline** — run `check-cwks`, `compose` and `check-manifest` for the arena; save the arena manifest's sha256.
- [ ] **Step 2: Move the pins** into `STAGES["arena_01"]` (`cwks_match`, `count`, `sha256`, `sets`, `instances`, `spawn`, `bounds_evidence`) with no value changed; thread `--stage` through `compose`/`check-manifest`/`cwks --match`.
- [ ] **Step 3: Verify the arena is byte-identical** — the same three commands exit 0 and the arena manifest's sha256 equals Step 1's.
- [ ] **Step 4: Add `STAGES["shrine"]`** — `cwks_match = "levels/cathedral/"`; exclusions: hash-suffix stubs (`_[0-9a-f]{8}\.`), `_lod`, `_collision`, `levels/bridge/`, `cathedral_garden`; `instances` left `None` until measured.
- [ ] **Step 5: Commit** — `git commit -m "refactor(re): stage configs in the probe"`

### Task 1b: The shrine manifest (W3)

**Files:**
- Modify: `tools/re/stage/probe.py` (the shrine compose reads `tools/re/s7/shrine_placements.json`), `docs/notes/scene-manifest.md` (a shrine example)

**Interfaces:**
- Consumes: Task 0's JSON; Task 1a's `--stage`.
- Produces: `out/re/stage/shrine.manifest` — the cathedral `instance` lines, 16 statue `instance` lines (slot 1 = the broken glbs), `entity models/shadow/characters/mono/_cmn/mono.glb … girl_a_death_loop`, `spawn x y z yaw`.

- [ ] **Step 1: Compose** — `compose --stage shrine` emits the instances in cwks order, then the statues by slot, Mono, the spawn; the provenance header names the JSON's sources.
- [ ] **Step 2: Measure and pin** — record the instance count (recon: 45) and the manifest's sha256 into `STAGES["shrine"]`; `check-manifest --stage shrine` asserts: every glb exists in the library, count, 16 statues with slot 1 broken, one Mono entity with its clip, a spawn, the sha256.
- [ ] **Step 3: Verify** — `check-manifest --stage shrine` exits 0; the arena's gate still exits 0.
- [ ] **Step 4: Commit** — `git commit -m "feat(re): compose the shrine manifest"`

### Task 2: The layered ground (W1)

**Files:**
- Modify: `app/src/heightmap.h`, `app/src/heightmap.cpp`, `app/src/player.cpp:89`, `app/src/player_state.cpp:450`, `app/src/collision.cpp:109`
- Test: `app/tests/test_heightmap.cpp`

**Interfaces:**
```cpp
// heightmap.h
struct Heightmap { /* existing fields */ int maxLayers = 0; std::vector<float> layers; /* node-major, maxLayers per node, NaN-padded, ascending */ };
Heightmap build_heightmap(const Scene& scene, float cellSize, bool layered = false);
float heightmap_floor(const Heightmap& hm, Vec3 p, float step);  // == heightmap_sample when !layered
inline constexpr int kHeightmapLayers = 8;
inline constexpr float kFloorStep = 0.6f;
```

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("an unlayered map's floor is its sample") {
    // the existing single-quad fixture; any p.y
    CHECK(heightmap_floor(hm, {x, -50, z}, kFloorStep) == heightmap_sample(hm, {x, 0, z}));
    CHECK(heightmap_floor(hm, {x, 500, z}, kFloorStep) == heightmap_sample(hm, {x, 0, z}));
}
TEST_CASE("a layered two-storey column answers the floor under the feet") {
    // fixture: a floor quad at y=9 and a roof quad at y=60 over the same XZ
    Heightmap hm = build_heightmap(scene, 1.0f, true);
    CHECK(heightmap_floor(hm, {0, 9.2f, 0}, kFloorStep) == doctest::Approx(9.0f));
    CHECK(heightmap_floor(hm, {0, 61.0f, 0}, kFloorStep) == doctest::Approx(60.0f));
    CHECK(heightmap_floor(hm, {0, 5.0f, 0}, kFloorStep) == doctest::Approx(9.0f));  // below all: lowest
    CHECK(heightmap_sample(hm, {0, 0, 0}) == doctest::Approx(60.0f));               // max kept
}
TEST_CASE("a stair step within kFloorStep is climbed, a ledge above it is not") {
    // floor quads at y=9.0 and y=9.5 side by side; a walkway at y=12 over the second
    CHECK(heightmap_floor(hm, {stepX, 9.0f, 0}, kFloorStep) == doctest::Approx(9.5f));
}
```
- [ ] **Step 2: Run** — `cmake --build app/build -j && ctest --test-dir app/build -R unit --output-on-failure` → fails to compile (`heightmap_floor`).
- [ ] **Step 3: Implement** — the layered build rasterizes as today, inserting each triangle's node height into the node's ascending list (merge within 0.05 m; when full, keep the highest `kHeightmapLayers`); `heightmap_floor` interpolates per corner the highest layer ≤ `p.y + step` (falling back per the `heightmap_sample` rules). Switch the three player/collision call sites to `heightmap_floor(…, kFloorStep)`; leave the `app.cpp` sites (frozen paths) as they are.
- [ ] **Step 4: Run** the unit tests → green, all existing heightmap cases unchanged.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): a layered ground that finds the floor"`

### Task 3: Culling and the re-uploadable renderer (W1)

**Files:**
- Modify: `app/src/scene.h` (`MeshCpu::worldMin/worldMax`), `app/src/asset_io.cpp` (fill them), `app/src/math3d.h` (`aabb_outside_frustum`), `app/src/renderer.h/.cpp` (cull in `render_frame`, `release_scene`, `drawn_last_frame`)
- Test: `app/tests/test_camera.cpp` (the frustum predicate), `app/tests/test_asset_io.cpp` (AABBs)

**Interfaces:**
```cpp
// math3d.h — true when the box is entirely outside one clip plane of viewProj
bool aabb_outside_frustum(const Mat4& viewProj, Vec3 mn, Vec3 mx);
// scene.h MeshCpu
Vec3 worldMin{}, worldMax{};   // static: world-space; rigged: bind pose padded by 25 %
// renderer.h
void release_scene();          // frees upload()'s buffers/textures; upload() may run again
int drawn_last_frame() const;  // meshes drawn by the last render_frame (after culling)
```

- [ ] **Step 1: Write the failing tests** — `aabb_outside_frustum`: a box in front of a `mat4_perspective × mat4_look_at` camera → false; behind the eye → true; straddling the near plane → false; far to the side → true. `compose_scene` on the existing two-instance fixture fills `worldMin/worldMax` equal to the transformed vertex bounds.
- [ ] **Step 2: Run** → compile failure.
- [ ] **Step 3: Implement** — clip-space test of the 8 corners against each of the 6 planes; `render_frame` skips meshes for which it is true; `release_scene` waits for the GPU idle and frees the per-scene resources, resetting `uploaded_`.
- [ ] **Step 4: Run** the unit tests → green; run the frozen S4.2 perf check (`caffeinate -dimsu ./scripts/s42-acceptance.sh`) → PASS (culling only removes draws).
- [ ] **Step 5: Commit** — `git commit -m "feat(app): frustum culling and a re-uploadable scene"`

### Task 4: The return phase machine (W1)

**Files:**
- Create: `app/src/transition.h`, `app/src/transition.cpp`, `app/tests/test_transition.cpp`
- Modify: `app/CMakeLists.txt` (viewer + test sources)

**Interfaces:**
```cpp
enum class ReturnPhase { Idle, Hold, FadeOut, Load, FadeIn, Wake, Free };
struct ReturnState {
    ReturnPhase phase = ReturnPhase::Idle;
    float t = 0.0f;       // seconds in the phase
    float fade = 0.0f;    // 0 clear .. 1 opaque
    bool inputLocked = false;
};
struct ReturnTimings { float hold = 2.0f, fadeOut = 1.0f, fadeIn = 1.5f, wake = 0.0f; };
ReturnState step_return(ReturnState s, float dt, const ReturnTimings& tm, bool deathDone, bool loadDone);
ReturnState reset_return();
bool return_wants_load(const ReturnState& s);  // true on the step that enters Load
```

- [ ] **Step 1: Write the failing tests** — Idle stays Idle while `!deathDone`; `deathDone` → Hold (unlocked, fade 0); after `hold` → FadeOut (locked, fade rises linearly to 1 over `fadeOut`); → Load (fade 1, `return_wants_load` true exactly once, stays while `!loadDone`); `loadDone` → FadeIn (fade 1→0 over `fadeIn`); → Wake (locked, fade 0, `wake` s); → Free (unlocked, terminal: further steps return it unchanged); a long `dt` cannot skip `Load` without `loadDone`; non-finite/negative `dt` advances nothing; `reset_return()` from every phase equals `ReturnState{}`.
- [ ] **Step 2: Run** → `transition.h` not found.
- [ ] **Step 3: Implement** — pure, no I/O/SDL/clock, the `death.cpp` style (`non_negative_dt`).
- [ ] **Step 4: Run** → green.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): the return-to-shrine phase machine"`

### Task 5: The shrine bakes (W4)

**Files:**
- Modify: `src/BpConvert.Core/Pipeline/ClipMotion.cs` (prefixes `girl_a_` and `player_a_home_boss01_` → the classification the app's in-place playback expects), `tests/BpConvert.Tests/` (one `ClipMotion` case per prefix)

**Interfaces:**
- Consumes: Task 0's `wake.clips`.
- Produces: `out/library/models/shadow/characters/mono/_cmn/mono.glb` carrying `girl_a_death_loop`; `wander.glb` re-baked with the wake clips **in addition to** every clip it already carries (the S2–S6 set — list them from `wander.clips.json` before baking).

- [ ] **Step 1: Write the failing test** — `ClipMotion.Classify("girl_a_death_loop")` and `("player_a_home_boss01_cut01")` return the in-place class.
- [ ] **Step 2: Run** `dotnet test tests/BpConvert.Tests -v q` → the two cases fail.
- [ ] **Step 3: Implement** the two prefix arms.
- [ ] **Step 4: Bake** — `dotnet publish src/BpConvert.Cli -c Release -o out/bin`; `caffeinate -dimsu dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out "$SOTC_LIB" --under models/shadow/characters/mono/_cmn/mono.cmsh --clips girl_a_death_loop --force`; the same for `wander.cmsh` with `<existing clips>,<wake clips>`. Verify both `clips.json` list the clips and the frozen S6 acceptance still passes (it re-checks Wander's clips).
- [ ] **Step 5: Commit** — `git commit -m "feat(convert): classify the shrine clips"` (the bakes are gitignored; record the commands in `s7-recon.md`).

### Task 6: The stage switch, the wake and the S7 selftest (W1)

**Files:**
- Modify: `app/src/app.cpp`, `app/src/app.h` (`Options::stage`, `s7Selftest`, `s7StillsPath`), `app/src/main.cpp` (`--stage arena|shrine`, `--s7-selftest`, `--s7-stills <dir>`, `--shrine-manifest <file>`, default `out/re/stage/shrine.manifest`), `app/src/camera.h/.cpp` (`camera_wake_pose`), `app/src/renderer.cpp` + `hud.h` (`HudState::fade`, `fadeColour` — a full-screen quad drawn last)
- Test: `app/tests/test_camera.cpp` (wake camera keys), `app/tests/test_asset_io.cpp` (missing glb names its path), `app/tests/test_hud.cpp` (fade 0 draws nothing)

**Interfaces:**
- Consumes: Tasks 1b, 2, 3, 4, 5.
- Produces (app.cpp-local): `void load_stage(StageId id)` — compose the manifest → `Scene`, `release_scene` + `upload`, `build_heightmap(scene, kHeightmapCellSize, id == StageId::Shrine)`, the spawn, the rigs; prints `s7-load stage=<arena|shrine> instances=N meshes=M ms=T`. `Camera camera_wake_pose(Vec3 spawn, float yawDeg, float t, float duration)` — two eased keys (low, 1.2 m up, 2.5 m in front of Wander looking at him → 3.5 m behind and 1.8 m up looking at the altar).
- The S7 lines, exact keys: `s7-phase phase=<P> t=%.3f fade=%.3f lock=%d`; `s7-load …`; `s7-ground probe=<spawn|altar_step|hall|stair> floor=%.3f column_max=%.3f`; `s7-idols count=16 broken_slot=1`; `s7-mono clip=girl_a_death_loop present=1`; `s7-perf frames=240 frame_ms=%.3f drawn=%d culled=%d`; `s7-status stage=shrine phase=Free lock=0`; `s7-reset stage=arena phase=Idle death=Idle valus=100 player=100`.

- [ ] **Step 1: Write the failing tests** — the wake camera at t=0 and t=duration matches its two keys and is finite between; `compose_scene` with a manifest naming a missing glb throws a message containing that path; `HudState{fade=0}` adds no fade quad and `fade=1` adds one covering the window.
- [ ] **Step 2: Run** → failures.
- [ ] **Step 3: Implement the stage switch** — `load_stage`; `--stage shrine` starts there (spawn, `Free`, no death); `R` in any stage or phase → `load_stage(Arena)` if not in the arena, then the existing soft reset, `reset_death()`, `reset_return()`.
- [ ] **Step 4: Implement the wiring** — step the return machine only in interactive play and `--s7-selftest` — never under `--selftest`/`--s3`…`--s6-selftest` (S4.1's selftest defeats Valus for real; every frozen output stays unchanged); on `return_wants_load` → `load_stage(Shrine)`, `loadDone` true after it returns; `Wake` plays the first wake clip on the player rig in place at the spawn with `camera_wake_pose`; input locked per `inputLocked`; the HUD's Valus bar hidden in the shrine; `R`/`M`/`Escape` live throughout.
- [ ] **Step 5: Implement `--s7-selftest`** — reuse the S6 scripted lethal event and death timeline (the shared `soft_reset_encounter`/`render_death_frame` helpers), then step the return machine at 1/60 s through `Free`, printing the lines above; the ground probes from `shrine_placements.json`'s spawn plus three named points (`s7-recon.md` lists them); the 240-frame perf run in the shrine; `R` (printing `s7-reset`), then a second arena → shrine load to prove the re-upload (two `s7-load stage=shrine` lines with equal `meshes`). With `--s7-stills`: `fade.png` (FadeOut at fade≈0.5), `wake.png`, `altar.png`, `idols.png`, `hall.png`, each via `with_clocks_restored`/`still_png_path`.
- [ ] **Step 6: Run** the unit tests, then `caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest --model models/shadow/characters/wander/_cmn/wander.glb --s7-selftest --s7-stills out/s7/stills` → exit 0 with every line above; then `caffeinate -dimsu ./scripts/s6-acceptance.sh` → PASS (the S6 output is unchanged).
- [ ] **Step 7: Commit** — `git commit -m "feat(app): the return to the shrine, wired and proven"`

### Task 7: The S7 acceptance and the results (W4)

**Files:**
- Create: `scripts/s7-acceptance.sh`, `docs/notes/s7-results.md`
- Modify: `app/README.md` (`--stage`, `--s7-selftest`, the transition), `AGENTS.md` (S7 status, the frozen list gains `s6-acceptance.sh`)

- [ ] **Step 1: Write the script** (bash 3.2, the S6 script's structure) — build + both suites; the **frozen S6 acceptance** first; `probe.py check-manifest` for the arena and `--stage shrine`; `--s7-selftest --s7-stills`; assert: the phase order `Idle Hold FadeOut Load FadeIn Wake Free`, the fade at FadeOut's end = 1.000 and at Wake = 0.000; `s7-load stage=shrine` meshes > 1000 and ms ≤ 30000; every `s7-ground` floor < column_max − 1.0 for `stair`/`altar_step`/`hall` and the floor within the cathedral floor band 8.5–12.5 at `spawn`; `s7-idols count=16 broken_slot=1`; `s7-mono present=1`; `s7-perf frame_ms ≤ 16.7` and `culled > 0`; `s7-reset` exact; two equal `s7-load stage=shrine` mesh counts; `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0`; the five stills 2560×1440, PNG magic, > 5000 B, span ≥ 30; `fade.png` mean luma ≥ 180; `PASS —` with the measured values.
- [ ] **Step 2: Run** — `caffeinate -dimsu ./scripts/s7-acceptance.sh` → exit 0; save the log to `out/s7-acceptance-run.log`.
- [ ] **Step 3: Write `s7-results.md`** — the S6 note's sections: measured values only, the RE gates' outcomes, the authored values labelled, the known gaps (Dormin voice/light, the break animation, the cutscene cameras, the bridge/garden, silence), and the user's walk checklist (design §11) as unchecked boxes.
- [ ] **Step 4: Commit** — `git commit -m "docs: S7 acceptance and results"`

## Self-Review

- **Spec coverage:** G1 → Tasks 4, 6; G2 → Tasks 1a, 1b; G3 → Task 2; G4 → Tasks 3, 7; G5 → Tasks 0 (R3), 5, 6; G6 → Tasks 0 (R1), 1b, 5; G7 → Tasks 6, 7; D1–D11 → the tasks above (D6 `release_scene` Task 3; D11 `R`/`--stage` Task 6); §8.7 gates → Task 0.
- **Frozen paths:** the `app.cpp` `heightmap_sample` sites stay; no frozen selftest (`--s3`…`--s6-selftest`) steps the return machine; the probe's arena sha256 gate proves the composition.
- **Names:** `heightmap_floor`, `kFloorStep`, `build_heightmap(…, layered)`, `aabb_outside_frustum`, `release_scene`, `drawn_last_frame`, `ReturnPhase`/`step_return`/`reset_return`/`return_wants_load`, `load_stage`, `camera_wake_pose`, `HudState::fade` — used consistently across Tasks 2–7.
