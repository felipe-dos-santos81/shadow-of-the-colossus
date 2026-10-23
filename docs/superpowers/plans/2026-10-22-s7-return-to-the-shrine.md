# S7 "Return to the Shrine" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After Valus's death sequence, fade to white, load the Shrine of Worship composed from the shipped `cathedral` stage, wake Wander in front of the altar (Mono lying on it, sixteen idols, the first broken), and let the player walk the shrine with floors and walls; `R` returns to the arena.

**Architecture:** Three RE gates (W2) answer the idol positions, the post-defeat flow and the wake clip; the stage probe gains per-stage configs and composes `shrine.manifest` with a new `posed` record for Mono (W3); the converter bakes Mono's pose and Wander's wake clip (W4); the app gains a layered ground with a wall mask, per-frame frustum culling, a re-uploadable renderer, a stage loop around `run_app`'s per-stage section, a pure `transition.h` machine and the wiring (W1); `--s7-selftest --s7-stills` and `scripts/s7-acceptance.sh` prove it after the frozen S6 chain.

**Tech Stack:** C++20, SDL3/SDL_GPU/Metal, doctest, .NET 10 (BpConvert), Python 3 (stdlib), Ghidra 12.1.4 + Ghidra-MCP, REA 6.3.0.

**Spec:** `docs/superpowers/specs/2026-10-22-s7-return-to-the-shrine-design.md`

## Global Constraints

- Apple Silicon arm64, C++20, SDL3/SDL_GPU/Metal; no new runtime dependency; windowed, the cursor never captured.
- Offline-first: the runtime never parses game containers; the probe/converter run at build time; the game tree is read-only; assets/derived files gitignored, never committed.
- Static-only RE; evidence in `docs/notes/s7-recon.md` as addresses/counts/sizes, no game content.
- Frozen and invoked unchanged: `scripts/s6-acceptance.sh`, `s5-`, `s42-`, `s41-`, `s4-`, `s3-acceptance.sh`; `s5_map.json`, `s6_map.json`; the existing `out/re/stage/encounter.manifest` (never recomposed); the output of `--selftest` and `--s3`…`--s6-selftest`.
- No frozen selftest ever steps the return machine or requests a stage switch.
- Every viewer/acceptance run under `caffeinate -dimsu`; never two acceptances concurrently; never a bake (`bp-convert convert … --force`) while an acceptance runs; `out/library` is **shared by every branch and worktree** — a bake must leave `main`'s suite green (Task 8 lands the relaxed clip pins on `main` before baking); scripts bash 3.2; tests fail fast when `SOTC_GAME_ROOT`/`SOTC_LIB` are unset.
- The shrine is silent in S7 (a recorded gap, not a placeholder).
- Builder-authored values (camera keys, timings unless R2 answers them, fallback idol/spawn/Mono placement) carry `source: authored` and are labelled in the notes and the walk.
- ≤ 4 concurrent agents; W1 (app) and W2 (RE) never share files — findings cross as `s7-recon.md` and `tools/re/s7/shrine_placements.json`.

## Review Focus

1. **`R` pressed mid-fade or during `Load`.** The arena must come back in its S6 opening state with no half-loaded shrine. Test: `test_transition.cpp` "reset from any phase"; Task 9's selftest injects `R` during `FadeOut` once and expects the `s7-reset` line.
2. **Walking into a pillar, an idol, a wall or off the building.** The player must stop or slide, never pass through or drop to the exterior ground. Test: `test_heightmap.cpp` wall-mask cases (Task 2); Task 9's `s7-wall` probe.
3. **Standing under a roof or on stairs.** The ground must be the floor under the feet. Test: `test_heightmap.cpp` two-storey and stair fixtures (Task 2); Task 9's `s7-ground` probes.
4. **The arena after the ground/culling/stage-loop changes.** Every frozen number must hold, and the moving player must never be culled. Test: `test_heightmap.cpp` "unlayered floor equals sample" and "unlayered map is always walkable"; `test_renderer_cull` cases (Task 3); the frozen S6 chain after Task 6.
5. **Repeated arena → shrine → arena switches and a missing shrine asset.** No leak, no stale scene, equal mesh counts each time; a manifest naming a missing glb or a `posed` clip the glb lacks fails loudly with the path. Test: Task 5's `posed` error cases; Task 9's two `s7-load stage=shrine` lines.

---

## File Structure

- `tools/re/s7/` — `idols.py`, `wake_clip.py`, `shrine_placements.json` (the gates' output, every value with a `source`).
- `docs/notes/s7-recon.md` — the R1/R2/R3 evidence, pass/fail lines, the bake commands.
- `tools/re/stage/probe.py` — `STAGES` configs, `--stage`, the shrine compose.
- `docs/notes/scene-manifest.md` — the `posed` record.
- `app/src/manifest.h/.cpp`, `app/src/asset_io.cpp` — parse `posed`; CPU-skin it at compose.
- `app/src/heightmap.h/.cpp`, `app/src/player.cpp`, `app/src/collision.cpp`, `app/src/player_state.cpp` — the layered ground, the wall mask, the switched ground calls.
- `app/src/scene.h`, `app/src/math3d.h`, `app/src/renderer.h/.cpp`, `app/src/hud.h/.cpp` — local AABBs, culling, `release_scene`, the fade quad.
- `app/src/transition.h/.cpp` — the return phase machine.
- `app/src/app.cpp`, `app/src/app.h`, `app/src/main.cpp`, `app/src/camera.h/.cpp` — the stage loop, the wiring, `--stage`, `--no-return`, `--s7-selftest`, `--s7-stills`, `--shrine-manifest`.
- `app/tests/test_asset_io.cpp` — the Wander clip-count pins.
- `scripts/s7-acceptance.sh`, `docs/notes/s7-results.md`, `app/README.md`, `AGENTS.md`.

**Lanes:** Task 0 (W2) ‖ Task 1 (W3) ‖ Tasks 2, 3, 4, 5 (W1, in order). Then Task 6 (W1, the stage loop — alone, it runs the frozen chain). Task 7 (W3) needs 0, 1, 5. Task 8 (W4, alone) needs R3. Task 9 (W1) needs 2–8. Task 10 (W4) needs 9.

### Task 0: The RE gates (W2)

**Files:**
- Create: `tools/re/s7/idols.py`, `tools/re/s7/wake_clip.py`, `tools/re/s7/shrine_placements.json`, `docs/notes/s7-recon.md`

**Interfaces:**
- Produces `shrine_placements.json` (positions in the cathedral glbs' frame, metres, yaw in degrees about +Y):
  ```json
  {"idols": [{"slot": 1, "glb": "models/.../mino_a_statue.glb", "p": [0,0,0], "yaw_deg": 0, "source": "binary|authored"}],
   "broken": {"slot": 1, "glbs": ["models/.../mino_a_statue_broke.glb", "models/.../mino_a_statue_broke_skirt.glb"]},
   "mono": {"glb": "models/shadow/characters/mono/_cmn/mono.glb", "clip": "girl_a_death_loop", "seconds": 0.0, "p": [0,0,0], "yaw_deg": 0, "source": "authored"},
   "spawn": {"p": [0,0,0], "yaw_deg": 0, "source": "data|authored"},
   "wake": {"clips": ["player_a_home_boss01_cutNN"], "seconds": 0.0, "source": "data|authored"},
   "flow": {"hold_s": 2.0, "fade_out_s": 1.0, "fade_in_s": 1.5, "colour": [1,1,1], "source": "binary|authored"},
   "probes": {"altar_step": [x,z], "hall": [x,z], "stair": [x,z], "wall": {"from": [x,z], "to": [x,z]}}}
  ```
  (`idols` has 16 entries, slots 1–16.)

- [ ] **Step 1: R1 — idol placement.** Find the code references of the `BossStatue_Whole_*`/`BossStatus_*` strings (`idols.py`, reusing the recon's xref/rela scan in the session scratchpad's `recon-re/`, or Ghidra-MCP on a **copy** of `out/re/ghidra`) and the code that positions the statues. Record addresses and 16 transforms; or record `R1: FALLBACK` and fit the 16 positions to the pedestal tops measured from `cathedral_interior.glb` (`source: authored`).
- [ ] **Step 2: R2 — the post-defeat flow.** Decompile `0x7df820` and the callers of `0x5707e0` on the `DEAD_MINOTOUR_A` (45) → `MINOTOUR_A_HOME_END` (540) path; record the fade/load/idol/wake order and any fade colour/duration constants at the `CMOLdrFadeToColor`/`FadeTransition` call sites. Unanswered values keep the design defaults with `source: authored`.
- [ ] **Step 3: R3 — the wake, the spawn, the probes.** `wake_clip.py` reads each `player_a_home_boss01_cut01..11` via `tools/re/anb/pose_dump.py` (or a scratch `--out` bake) and reports the root height curve and the end pose; the wake clips are the cut(s) whose root rises from lying (≤ 0.4 m) to standing. The `playerstartpositions.txt` home/cathedral records (lines 19–21, 40) sit in another frame (x ≈ 300, z ≈ −299): derive the transform to the cathedral glbs' frame or author a spawn 3 m in front of the altar facing it; the floor at the spawn must be y 9–11. Choose the four probe points (`altar_step`, `hall`, `stair`, and a `wall` walk segment that ends inside a wall or pillar) from the cathedral geometry.
- [ ] **Step 4: Write `s7-recon.md`** — one section per gate with `R1|R2|R3: PASS|FALLBACK`, addresses, counts and provenance; a `python3 -I -c` one-liner in the note validates the JSON (16 idols, slot 1 broken, Mono, spawn, wake, flow, probes).
- [ ] **Step 5: Commit** — `git add tools/re/s7 docs/notes/s7-recon.md && git commit -m "chore(re): the S7 shrine gates"`

### Task 1: The per-stage probe (W3)

**Files:**
- Modify: `tools/re/stage/probe.py` (`ARENA_*` pins → `STAGES["arena_01"]`; `compose`/`check-manifest` gain `--stage`)

**Interfaces:**
- Produces: `probe.py compose --stage arena_01|shrine --library <dir> --out <file>`; `check-manifest --stage <name> --library <dir> <file>`; no `--stage` means `arena_01`. `STAGES["shrine"]` with `cwks_match = "levels/cathedral/"` and the D3 exclusions.

- [ ] **Step 1: Record the baseline** — `compose --library "$SOTC_LIB" --out <scratch>/valus.before` and `sha256sum` it (the reviewer measured `088e6218…` for the current `valus.manifest`; record the full value you get); run `check-cwks`, `check-manifest`, `check-encounter` → all exit 0.
- [ ] **Step 2: Move the pins** into `STAGES["arena_01"]` (`cwks_match`, `count`, `cwks_sha256`, `sets`, `instances`, `spawn`, `bounds_evidence`) with no value changed; thread `--stage` through `compose`, `check-manifest` and `cwks --match`.
- [ ] **Step 3: Verify the arena** — the same compose hashes to Step 1's value; the three checks exit 0. Do not run `compose-encounter`.
- [ ] **Step 4: Add `STAGES["shrine"]`** — cwks order; exclude hash-suffix stubs (`_[0-9a-f]{8}\.`), `_lod[0-9]`, `_collision`, the five occluders, the four shadow casters/blockers, `emitter_arch_lightray_long`, and any `*_low`/`*intlow` glb with a full-detail sibling (D3 lists the names); keep the garden; `instances`, `meshes`, `sha256` = `None` until Task 7 pins them.
- [ ] **Step 5: Commit** — `git commit -m "refactor(re): stage configs in the probe"`

### Task 2: The layered ground and the wall mask (W1)

**Files:**
- Modify: `app/src/heightmap.h`, `app/src/heightmap.cpp`, `app/src/player.cpp:89`, `app/src/collision.cpp:109`, `app/src/player_state.cpp:450`
- Test: `app/tests/test_heightmap.cpp`

**Interfaces:**
```cpp
// heightmap.h
inline constexpr int kHeightmapLayers = 8;
inline constexpr float kFloorStep = 0.6f;          // step up
inline constexpr float kFloorDrop = 3.0f;          // max step down for a horizontal move
inline constexpr float kBodyBandLow = 0.6f, kBodyBandHigh = 1.8f, kWallSteepness = 0.5f;
struct Heightmap { /* existing fields */
    int maxLayers = 0;                  // 0 = unlayered
    std::vector<float> layers;          // node-major, maxLayers per node, ascending, NaN-padded
    std::vector<uint8_t> blocked;       // same layout: 1 = a wall in that layer's body band
};
Heightmap build_heightmap(const Scene& scene, float cellSize, bool layered = false);
float heightmap_floor(const Heightmap& hm, Vec3 p, float step);      // == heightmap_sample when maxLayers == 0
bool heightmap_walkable(const Heightmap& hm, Vec3 from, Vec3 to);    // always true when maxLayers == 0
```

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("an unlayered map's floor is its sample and every move is walkable") {
    CHECK(heightmap_floor(hm, {x, -50, z}, kFloorStep) == heightmap_sample(hm, {x, 0, z}));
    CHECK(heightmap_floor(hm, {x, 500, z}, kFloorStep) == heightmap_sample(hm, {x, 0, z}));
    CHECK(heightmap_walkable(hm, {x, 0, z}, {x + 5, 0, z}));
}
TEST_CASE("a layered two-storey column answers the floor under the feet") {
    // a floor quad at y=9 and a roof quad at y=60 over the same XZ
    CHECK(heightmap_floor(hm, {0, 9.2f, 0}, kFloorStep) == doctest::Approx(9.0f));
    CHECK(heightmap_floor(hm, {0, 61.0f, 0}, kFloorStep) == doctest::Approx(60.0f));
    CHECK(heightmap_floor(hm, {0, 5.0f, 0}, kFloorStep) == doctest::Approx(9.0f));
    CHECK(heightmap_sample(hm, {0, 0, 0}) == doctest::Approx(60.0f));
}
TEST_CASE("a step within kFloorStep is climbed") {
    // floor quads at y=9.0 and y=9.5 side by side, a walkway at y=12 over the second
    CHECK(heightmap_floor(hm, {stepX, 9.0f, 0}, kFloorStep) == doctest::Approx(9.5f));
}
TEST_CASE("a wall in the body band blocks, a ceiling above it does not") {
    // a floor at y=9 and a vertical quad x=3, y 9..13 spanning z; a roof at y=20
    CHECK_FALSE(heightmap_walkable(hm, {1, 9, 0}, {3.5f, 9, 0}));
    CHECK(heightmap_walkable(hm, {1, 9, 0}, {1, 9, 2}));
}
TEST_CASE("a move with no floor within kFloorDrop below is refused") {
    // a floor at y=9 ending at x=4, then the exterior ground at y=0
    CHECK_FALSE(heightmap_walkable(hm, {3, 9, 0}, {5, 9, 0}));
}
```
- [ ] **Step 2: Run** — `cmake --build app/build -j && ctest --test-dir app/build -R unit_tests --output-on-failure` → compile failure.
- [ ] **Step 3: Implement** — the layered build inserts each non-steep triangle's node heights into the node's ascending list (merge within 0.05 m; keep the highest `kHeightmapLayers` when full); steep triangles (|n.y| < `kWallSteepness`) mark `blocked` on every layer whose body band they overlap, over the cells their XZ footprint (conservatively, the segment's cells) touches. `heightmap_floor` interpolates, per corner, the highest layer ≤ `p.y + step` (per `heightmap_sample`'s NaN rules; the lowest layer when none is below). `heightmap_walkable` is false when the target's selected layer is blocked or lies more than `kFloorDrop` below / `kFloorStep` above the source floor. Switch the three interactive call sites to `heightmap_floor(…, kFloorStep)`, and in `player.cpp` apply `heightmap_walkable` per axis (x then z) before the ground snap. Leave the eight `app.cpp` sites untouched.
- [ ] **Step 4: Run** the unit tests → green, every existing heightmap/player case unchanged.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): a layered ground with floors and walls"`

### Task 3: Culling, `release_scene` and the fade quad (W1)

**Files:**
- Modify: `app/src/scene.h` (`MeshCpu::localMin/localMax`), `app/src/asset_io.cpp` (fill them from the vertices before any `model` is applied), `app/src/math3d.h`, `app/src/renderer.h/.cpp`, `app/src/hud.h/.cpp`
- Test: `app/tests/test_camera.cpp`, `app/tests/test_hud.cpp`, `app/tests/test_asset_io.cpp`

**Interfaces:**
```cpp
// math3d.h — true when the box (in the space `mvp` maps from) is entirely outside one clip plane
bool aabb_outside_frustum(const Mat4& mvp, Vec3 mn, Vec3 mx);
// scene.h MeshCpu
Vec3 localMin{}, localMax{};       // in the mesh's own vertex space; the draw transforms by `model`
// renderer.h
void release_scene();              // waits for the GPU, frees upload()'s scene resources; upload() may run again
int drawn_last_frame() const;
int culled_last_frame() const;
// hud.h — the full-window fade quad: the window rect, alpha = clamp(fade, 0, 1); zero alpha draws nothing
struct HudFade { HudRect rect; float alpha; };
HudFade hud_fade_rect(float fade);   // in the HUD's 1280x720 layout space
```
`HudState` gains `float fade = 0.0f; std::array<float, 3> fadeColour{1, 1, 1};` — the renderer draws the fade quad last, **outside** the `hud_.visible` gate.

- [ ] **Step 1: Write the failing tests** — `aabb_outside_frustum` with `mat4_mul(mat4_perspective(…), mat4_look_at(…))`: a box in front → false; behind the eye → true; straddling the near plane → false; far to the side → true; the same box moved by a model matrix into view → false (proves per-frame transform). `compose_scene` on the existing two-instance fixture: `localMin/localMax` equal the raw vertex bounds. `hud_fade_rect(0).alpha == 0`, `hud_fade_rect(1)` covers 0,0–1280,720 with alpha 1, `hud_fade_rect(2).alpha == 1`.
- [ ] **Step 2: Run** → compile failure.
- [ ] **Step 3: Implement** — `render_frame` skips a mesh when `rig < 0` and `aabb_outside_frustum(viewProj * mesh.model, localMin, localMax)`; rigged meshes always draw; count drawn/culled. `release_scene` frees the scene buffers/textures and clears `uploaded_`.
- [ ] **Step 4: Run** the unit tests → green; then, alone, `caffeinate -dimsu ./scripts/s42-acceptance.sh` → PASS (culling removes only off-screen static draws).
- [ ] **Step 5: Commit** — `git commit -m "feat(app): frustum culling, a releasable scene and the fade quad"`

### Task 4: The return phase machine (W1)

**Files:**
- Create: `app/src/transition.h`, `app/src/transition.cpp`, `app/tests/test_transition.cpp`
- Modify: `app/CMakeLists.txt` (viewer sources; `sotc-app-tests` sources)

**Interfaces:**
```cpp
enum class ReturnPhase { Idle, Hold, FadeOut, Load, FadeIn, Wake, Free };
struct ReturnState {
    ReturnPhase phase = ReturnPhase::Idle;
    float t = 0.0f;        // seconds in the phase
    float fade = 0.0f;     // 0 clear .. 1 opaque
    bool inputLocked = false;
};
struct ReturnTimings { float hold = 2.0f, fadeOut = 1.0f, fadeIn = 1.5f, wake = 0.0f; };
ReturnState step_return(ReturnState s, float dt, const ReturnTimings& tm, bool deathDone, bool loadDone);
ReturnState reset_return();
bool return_wants_load(const ReturnState& before, const ReturnState& after);  // true on the step that enters Load
const char* return_phase_name(ReturnPhase phase);
```

- [ ] **Step 1: Write the failing tests** — Idle stays Idle while `!deathDone`; `deathDone` → Hold (unlocked, fade 0); after `hold` → FadeOut (locked, fade linear 0→1 over `fadeOut`); → Load (fade 1; `return_wants_load` true on exactly that step; stays Load while `!loadDone` for any `dt`); `loadDone` → FadeIn (fade 1→0 over `fadeIn`); → Wake (locked, fade 0, `wake` seconds; `wake = 0` passes straight to Free); → Free (unlocked; further steps return it unchanged); a negative or non-finite `dt` advances nothing; `reset_return()` from every phase equals `ReturnState{}`; `return_phase_name` returns the enum's spelling.
- [ ] **Step 2: Run** → `transition.h` not found.
- [ ] **Step 3: Implement** — pure, no I/O/SDL/clock, `death.cpp`'s `non_negative_dt` idiom; one long `dt` may cross several timed phases but never `Load`.
- [ ] **Step 4: Run** → green.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): the return-to-shrine phase machine"`

### Task 5: The `posed` manifest record (W1)

**Files:**
- Modify: `app/src/manifest.h/.cpp` (`SceneManifest::posed`), `app/src/asset_io.cpp` (`compose_scene` CPU-skins it), `docs/notes/scene-manifest.md`
- Test: `app/tests/test_manifest.cpp`, `app/tests/test_asset_io.cpp`

**Interfaces:**
```cpp
// manifest.h
struct ManifestPosed { std::string glb; Vec3 position; Quat rotation; Vec3 scale; std::string clip; float seconds; };
// SceneManifest gains: std::vector<ManifestPosed> posed;
```
Record: `posed <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName> <seconds>`. `compose_scene` samples `clip` at `seconds` (the app's pose sampler, clamped like a pinned clock), skins every vertex (positions and normals) on the CPU, applies the TRS, and appends the result as static meshes (`rig = -1`, the glb's materials/textures). A missing glb or a clip the glb lacks throws a `std::runtime_error` naming the path and the clip.

- [ ] **Step 1: Write the failing tests** — the parser accepts a well-formed line and refuses 13 tokens, a non-finite value or a negative `seconds` (the existing error format); composing a fixture with a two-joint skinned quad and a clip that rotates joint 1 by 90° at t = 1 yields static meshes whose vertices equal the hand-computed skinned positions at t = 1; a clip name the glb lacks throws with both names in the message.
- [ ] **Step 2: Run** → failures.
- [ ] **Step 3: Implement** — reuse the existing joint-palette code that `update_all_poses` uses for the palette, then skin on the CPU; document `posed` in `scene-manifest.md`.
- [ ] **Step 4: Run** → green; all existing manifest/asset cases unchanged.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): posed static props in the scene manifest"`

### Task 6: The stage loop (W1, behaviour-preserving)

**Files:**
- Modify: `app/src/app.cpp` (`run_app`), `app/src/app.h`

**Interfaces:**
```cpp
enum class StageId { Arena, Shrine };
struct StageRequest { bool quit = true; StageId next = StageId::Arena; };
struct StageCarry { ReturnState ret{}; int s7Step = 0; bool reloadedShrine = false; };  // what crosses an iteration
```
`run_app` keeps the window, GPU device, renderer, audio and options outside an outer `for (;;)`; everything from the manifest load (`compose_scene`, the club/corpse append, `build_heightmap`, the rigs, `player_rig_record`, `playbacks`, `entityRig`, `valus_behavior`, the encounter, the selftest blocks and the main loop) moves inside it. The iteration ends with a `StageRequest`; `quit` leaves the loop; otherwise `renderer.release_scene()` and the next iteration loads `next`. In this task nothing requests a switch yet, and the per-stage section is moved, not changed (re-indentation plus the request).

- [ ] **Step 1: Record the baseline** — run, alone, `caffeinate -dimsu ./scripts/s6-acceptance.sh` on the branch before the change → PASS; keep its log.
- [ ] **Step 2: Restructure** as above; the manifest path for an iteration is `opts.manifestPath` for `Arena` (the shrine path arrives in Task 9).
- [ ] **Step 3: Verify** — `cmake --build app/build -j && ctest --test-dir app/build --output-on-failure` → green; alone, `caffeinate -dimsu ./scripts/s6-acceptance.sh` → PASS with the same `s6-*`, `s5-*`, `s42-*`, `s41-*`, `s4-*`, `s3-*` lines as Step 1's log (diff the selftest outputs, not the timings).
- [ ] **Step 4: Commit** — `git commit -m "refactor(app): run each stage inside a stage loop"`

### Task 7: The shrine manifest (W3)

**Files:**
- Modify: `tools/re/stage/probe.py` (the shrine compose; its pins), `docs/notes/scene-manifest.md` (a shrine example)

**Interfaces:**
- Consumes: Task 0's JSON; Task 1's `STAGES["shrine"]`; Task 5's `posed` record.
- Produces: `out/re/stage/shrine.manifest` — the D3 `instance` lines in cwks order, 17 statue `instance` lines (slots 2–16 intact, slot 1 the broken glb + skirt), one `posed` Mono line, one `spawn`; a provenance header naming the JSON's sources and no absolute paths.

- [ ] **Step 1: Compose** — `compose --stage shrine --library "$SOTC_LIB" --out out/re/stage/shrine.manifest`.
- [ ] **Step 2: Measure and pin** — record the instance count, the mesh/vertex totals (from the library glbs) and the manifest's sha256 into `STAGES["shrine"]`; `check-manifest --stage shrine` asserts: every glb exists, the count, 17 statue lines with slot 1 broken, one `posed` Mono line naming `girl_a_death_loop`, a spawn, no excluded name, the sha256.
- [ ] **Step 3: Verify** — `check-manifest --stage shrine` exits 0; Task 1's arena compose hash and the three arena checks still pass.
- [ ] **Step 4: Commit** — `git commit -m "feat(re): compose the shrine manifest"`

### Task 8: The shrine bakes (W4, alone)

**Files:**
- Modify: `app/tests/test_asset_io.cpp:499, 938, 1007` (the Wander clip-count pins), `docs/notes/s7-recon.md` (the bake commands)

**Interfaces:**
- Consumes: Task 0's `wake.clips`.
- Produces: `out/library/models/shadow/characters/mono/_cmn/mono.glb` carrying `girl_a_death_loop`; `wander.glb` carrying its current 15 clips in their current order, then the wake clips.

- [ ] **Step 0: Relax the pins on `main` first** — `out/library` is shared, so a 15-exact pin on `main` would fail the moment Wander is re-baked. Change the three pins from `== 15` to `>= 15` plus a check that clips 0–14 carry the 15 S2–S6 names in their current order (list them from `wander.clips.json`); `ctest` green; commit as `test(app): pin Wander's S2–S6 clips by name, not count` on a branch off `main`, and land it on `main` (fast-forward) **with the user's OK** before Step 2; merge `main` into `s7-shrine`.
- [ ] **Step 1: List** Wander's current clips in order from `out/library/models/shadow/characters/wander/_cmn/wander.clips.json`.
- [ ] **Step 2: Bake** (no acceptance running) — `dotnet publish src/BpConvert.Cli -c Release -o out/bin`; `caffeinate -dimsu dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out "$SOTC_LIB" --under models/shadow/characters/mono/_cmn/mono.cmsh --clips girl_a_death_loop --force`; the same for `models/shadow/characters/wander/_cmn/wander.cmsh` with `--clips <the 15 in order>,<wake clips>`. Check both `clips.json` files list the clips in that order.
- [ ] **Step 3: Check** `ctest --test-dir app/build --output-on-failure` → green on `s7-shrine` **and** on `main` (the Step 0 pins accept the appended clips); add one S7 case asserting the wake clips follow at index 15+.
- [ ] **Step 4: Verify** — alone, `caffeinate -dimsu ./scripts/s6-acceptance.sh` → PASS (the clip indices the S2–S6 paths use are unchanged).
- [ ] **Step 5: Commit** — `git commit -m "test(app): Wander carries the wake clips"` (the bakes are gitignored; the commands are in `s7-recon.md`; the shared library change is recorded there too).

### Task 9: The return, the shrine and the S7 selftest (W1)

**Files:**
- Modify: `app/src/app.cpp`, `app/src/app.h` (`AppOptions::stage`, `noReturn`, `s7Selftest`, `s7StillsPath`, `shrineManifestPath` = `out/re/stage/shrine.manifest`), `app/src/main.cpp` (`--stage arena|shrine`, `--no-return`, `--s7-selftest`, `--s7-stills <dir>`, `--shrine-manifest <file>`), `app/src/camera.h/.cpp`
- Test: `app/tests/test_camera.cpp`

**Interfaces:**
- Consumes: Tasks 2–8 (`heightmap_floor`/`heightmap_walkable`, culling + `release_scene` + `HudState::fade`, `step_return`, `posed`, `StageRequest`/`StageCarry`, `shrine.manifest`, the wake clips).
- Produces: `Camera camera_wake_pose(Vec3 spawn, float yawDeg, float t, float duration)` — two eased keys: 1.2 m up and 2.5 m in front of the spawn looking at Wander at t = 0; 1.8 m up and 3.5 m behind, looking along the yaw toward the altar, at t = duration.
- The S7 lines, exact keys: `s7-phase phase=<name> t=%.3f fade=%.3f lock=%d`; `s7-load stage=<arena|shrine> instances=%d meshes=%d ms=%.1f`; `s7-ground probe=<spawn|altar_step|hall|stair> floor=%.3f column_max=%.3f`; `s7-wall from=%.2f,%.2f to=%.2f,%.2f stopped=%.2f,%.2f blocked=1`; `s7-idols count=16 broken_slot=1`; `s7-mono clip=girl_a_death_loop present=1`; `s7-perf frames=240 frame_ms=%.3f drawn=%d culled=%d`; `s7-status stage=shrine phase=Free lock=0`; `s7-reset stage=arena phase=Idle death=Idle valus=100 player=100`.

- [ ] **Step 1: Write the failing test** — `camera_wake_pose` at t = 0 and t = duration equals the two keys (eye and target within 1e-4) and is finite at 0.5·duration.
- [ ] **Step 2: Run** → compile failure.
- [ ] **Step 3: Implement the shrine iteration** — `StageId::Shrine` composes `opts.shrineManifestPath`, builds `build_heightmap(scene, kHeightmapCellSize, /*layered=*/true)`, places the player at the manifest spawn with Y from `heightmap_floor`; with no `entity` it runs no encounter, no club/corpse, no Valus HUD; `--stage shrine` starts here in `Free`. Print `s7-load` per iteration.
- [ ] **Step 4: Implement the return** — in interactive play only (not `--no-return`, and never under `--selftest`/`--s3`…`--s6-selftest`), step `step_return` each frame with `deathDone = death.phase == DeathPhase::Done` and timings from the manifest provenance or `ReturnTimings{}` defaults (wake = the wake clip's duration); set `HudState::fade`/`fadeColour`; lock input per `inputLocked`; when `return_wants_load` fires, end the iteration with `StageRequest{false, StageId::Shrine}` carrying the state, and the shrine iteration starts with `loadDone = true`; `Wake` plays the first wake clip once on the player rig at the spawn with `camera_wake_pose`; `R` anywhere → `StageRequest{false, StageId::Arena}` when not in the arena, then the existing soft reset, `reset_death()`, `reset_return()`; `M`/`Escape` live throughout.
- [ ] **Step 5: Implement `--s7-selftest`** (requires `--manifest` and `--model` like S6) — in the arena iteration: seed the encounter defeated and `death` at `Done` (via `step_death` with `kDeathEnd`), show the corpse through the same helpers the interactive loop uses, step `step_return` at 1/60 s printing `s7-phase` on each change, inject `R` once during the first `FadeOut` (expect `s7-reset`, then re-seed and continue), and request the shrine at `Load`. In the shrine iteration: finish `FadeIn`/`Wake`/`Free`; print the four `s7-ground` probes from `shrine_placements.json` (`probes`, plus the spawn), the scripted `s7-wall` walk through `player_step` with forward input from `probes.wall.from` toward `.to`, `s7-idols`, `s7-mono`, the 240-frame `s7-perf`, `s7-status`; press `R` (→ arena, `s7-reset`), then request the shrine once more (a second `s7-load stage=shrine`) and exit 0. With `--s7-stills`: `fade.png` (FadeOut at fade ≈ 0.5), `wake.png`, `altar.png`, `idols.png`, `hall.png` via `with_clocks_restored`/`still_png_path`.
- [ ] **Step 6: Run** — unit tests green; `caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest --model models/shadow/characters/wander/_cmn/wander.glb --s7-selftest --s7-stills out/s7/stills` → exit 0 with every line above; then, alone, `caffeinate -dimsu ./scripts/s6-acceptance.sh` → PASS.
- [ ] **Step 7: Commit** — `git commit -m "feat(app): the return to the shrine, wired and proven"`

### Task 10: The S7 acceptance and the results (W4)

**Files:**
- Create: `scripts/s7-acceptance.sh`, `docs/notes/s7-results.md`
- Modify: `app/README.md` (`--stage`, `--no-return`, `--s7-selftest`, the transition), `AGENTS.md` (the S7 status; the frozen list gains `s6-acceptance.sh`)

- [ ] **Step 1: Write the script** (bash 3.2, the S6 script's structure) — build + both suites; the **frozen S6 acceptance** first; the probe's arena compose hash (Task 1's value) and `check-cwks`/`check-manifest`/`check-encounter`; `check-manifest --stage shrine`; `--s7-selftest --s7-stills`; assert: the phase order `Idle Hold FadeOut` (R) `Idle Hold FadeOut Load FadeIn Wake Free`; the fade 1.000 at `Load`, 0.000 at `Wake`; three `s7-load stage=shrine|arena` lines with the two shrine mesh counts equal and `ms ≤ 30000`; `s7-ground` floor < column_max − 1.0 for `altar_step`/`hall`/`stair` and 8.5 ≤ floor ≤ 12.5 at `spawn`; `s7-wall blocked=1` with `stopped` short of `to`; `s7-idols count=16 broken_slot=1`; `s7-mono present=1`; `s7-perf frame_ms ≤ 16.7` and `culled > 0`; `s7-status` and `s7-reset` exact; `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0`; the five stills 2560×1440, PNG magic, > 5000 B, span ≥ 30; `fade.png` mean luma ≥ 180; `PASS —` with the measured values.
- [ ] **Step 2: Run** — alone, `caffeinate -dimsu ./scripts/s7-acceptance.sh` → exit 0; save the log to `out/s7-acceptance-run.log`.
- [ ] **Step 3: Write `s7-results.md`** — the S6 note's sections; measured values only; the RE gates' outcomes; the authored values labelled; the known gaps (Dormin voice/light, the break animation, the cutscene cameras, the bridge, silence, the coarse wall mask); the walk checklist (design §11) as unchecked boxes, plus "`--no-return` keeps the S6 end state for the S6 walk".
- [ ] **Step 4: Commit** — `git commit -m "docs: S7 acceptance and results"`

## Self-Review

- **Spec coverage:** G1 → Tasks 4, 9; G2 → Tasks 1, 7; G3 → Task 2; G4 → Tasks 3, 10; G5 → Tasks 0 (R3), 8, 9; G6 → Tasks 0 (R1), 5, 7, 8; G7 → Tasks 9, 10. D2 → Task 1; D3 → Tasks 1, 7; D4/D12 → Task 2; D5 → Task 3; D6 → Tasks 3, 6; D7 → Task 4; D8 → Task 0; D9 → Tasks 0, 9; D10 → Task 10 note; D11/D13 → Task 9; D14 → Tasks 5, 7.
- **Frozen paths:** the eight `app.cpp` `heightmap_sample` sites stay; unlayered maps are identical by construction; rigged meshes are never culled; Task 6 is proven by the frozen chain before any S7 behaviour lands; no frozen selftest steps the return or requests a switch; `encounter.manifest` is never recomposed; the Wander re-bake keeps the 15 clips in order.
- **Names:** `heightmap_floor`, `heightmap_walkable`, `kFloorStep`, `kFloorDrop`, `build_heightmap(…, layered)`, `aabb_outside_frustum`, `localMin/localMax`, `release_scene`, `drawn_last_frame`/`culled_last_frame`, `hud_fade_rect`/`HudState::fade`, `ReturnPhase`/`ReturnTimings`/`step_return`/`reset_return`/`return_wants_load`/`return_phase_name`, `ManifestPosed`, `StageId`/`StageRequest`/`StageCarry`, `camera_wake_pose`, `AppOptions` — consistent across Tasks 2–10.
