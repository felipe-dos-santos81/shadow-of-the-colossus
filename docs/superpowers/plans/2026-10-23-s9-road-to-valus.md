# S9 "The road to Valus" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the first colossus's loop on the map: the arena's entrance stairs and corridor become part of the plain (the `road`), the corridor's dead end is a door into Valus's arena (and the arena has one back), Agro waits where she was left, and the raised sword's light — the game's eight-line fan, focus/width curves, lock and ray, on its own atlases and cues — points to Valus.

**Architecture:** An RE/data gate (W2) places the corridor and arena doors, their arrival spawns, Valus's target, the sword tip and the selftest probes on the app's unlayered maps; the probe composes `road.manifest` with its own composer (W3); the converter decodes the sword atlases, bakes Wander's six raise clips and the S9 audio slice (W4); the app gains a pure sword module, raw-node rays and a backdrop ray grid, a mixer stop path, a separate sword effect pass, the road stage with both arena doors and Agro's carried pose, and the raise input (W1); `--s9-selftest`, `--s9-e2e` and `scripts/s9-acceptance.sh` prove it after the frozen S8 acceptance.

**Tech Stack:** C++20, SDL3/SDL_GPU/Metal, doctest, .NET 10 (BpConvert), Python 3 (stdlib), Ghidra 12.1.4 + Ghidra-MCP, REA 6.3.0.

**Spec:** `docs/superpowers/specs/2026-10-23-s9-road-to-valus-design.md`

**Starting point:** S9 branches from `main` after S8's merge (S8 Tasks 8–10, `scripts/s8-acceptance.sh` and its final review). This plan was written in the `s9-road` worktree while its app code was S8 up to Task 7, so it is written against the S8 design/plan's interfaces, not this tree's: `StageId::Plain`, `AppOptions::plainManifestPath`, `step_door`/`door_wants_load`/`box_entered`/`reset_door`, `ride_points::Door` (`kDoorShrine`, `kDoorPlain`), `StageCarry{ret, door, …}`, `reset_ride()`, `rig_bone_world`, `CameraFollow::headHeight`, the S8 ride cue table (`RideSfx`, `ride_cue_clip`), `s8_wired = (interactive_run && !opts.s7E2e) || opts.s8Selftest`, the `AudioSeam` wrapper, and `--s8-e2e`'s scripted-input seam. Wave A (Tasks 0–5) may start before the merge; `git rebase main` on `s9-road` **before Task 6 at the latest**, then re-read those names in the merged code; a renamed interface is fixed in this plan first.

**Library view:** S9 never writes the shared base `out/library` (`main` pins Wander at `== 43` clips and an audio manifest of 8/10/34 entries; `AGENTS.md`: a branch works in a library view). Task 6 Step 0 runs `make libview BRANCH=s9-road` (`tools/verify/libview.sh`), and every lane from Task 6 on — and `scripts/s9-acceptance.sh` — exports `SOTC_LIB` at that view (`out/libviews/s9-road`). Promoting the bake to the base library happens at S9's merge, after `main`'s pins are relaxed with the user's OK (recorded then).

**Concurrency:** the user approved up to 8 concurrent agents (2026-10-23); that is why Wave A's lanes exceed design §5's cap of 4. Acceptances and bakes still run alone.

## Global Constraints

- Apple Silicon arm64, C++20, SDL3/SDL_GPU/Metal; no new runtime dependency; windowed, the cursor never captured.
- Offline-first; the game tree is read-only; assets/derived files gitignored, never committed; RE notes carry addresses/counts/sizes, no game content.
- Frozen and invoked unchanged: `scripts/s8-acceptance.sh` … `s3-acceptance.sh`; `out/re/stage/plain.manifest` and its probe pins (`STAGES["plain"]`); the arena/encounter/shrine manifests; `s5_map.json`, `s6_map.json`, `s8_map.json`; the output of `--selftest`, `--s3`…`--s8-selftest`, `--s7-e2e`, `--s8-e2e`.
- No frozen run (`--s8-selftest`/`--s8-e2e` included) loads `road.manifest`, steps the sword, the corridor or arena doors, carries Agro's pose, or plays an S9 cue: `s9_wired = (interactive_run && !opts.s7E2e && !opts.s8E2e) || opts.s9Selftest || opts.s9E2e`. The Plain stage loads `road.manifest` only when `s9_wired`, else `plain.manifest`.
- The sword is raised and drawn on the road only (the Plain stage under `s9_wired`); the arena and the shrine never load `wander_sword.glb` or the sword pass, so their mesh counts stay as frozen.
- The S9 audio manifest holds **36** entries (S8's 34 + `sword.shimmer` + `sword.search`); `s8-acceptance.sh` leaves S8's 34, so every `--s9-*` run after an S8 acceptance first runs `make audio`.
- S9 bakes and decodes only into its library view `out/libviews/s9-road` (the header): Wander's 43 clips keep their indices there; the six raise clips are appended at **43…48**; no other glb is re-baked; the base `out/library` is untouched until S9's merge.
- Every viewer/acceptance run under `caffeinate -dimsu`; never two acceptances concurrently; never a bake while an S9 acceptance or an S9 lane's `ctest` reads the view; scripts bash 3.2; tests fail fast when `SOTC_GAME_ROOT`/`SOTC_LIB` are unset.
- Authored values (the door boxes/spawns, the raise key, the aim reading, the tip offset, the raise blend, the sun direction and shade length, the fade/width step unit, the sprite sizes, the search cue's trigger) carry `source: authored` and are labelled; S9 cue rows are `method: audition`.
- Labelled rulings (each becomes an entry in `s9-results.md`): **Agro's pose is stored when the player leaves the road by either door** — beyond D4, which names the arena door only (a labelled deviation: a stale arena-door pose must never override a later ride); **the fan opens around the aim** (the camera's look), not the blade axis (authored); **the cue timing** — the shimmer loops from the raise until lowering begins, the search cue plays once per lock (authored).
- ≤ 8 concurrent agents (the user's approval, header), each in its own worktree on disjoint files; **one `app.cpp` writer at a time**; W1 and W2 never share files — findings cross as `docs/notes/s9-recon.md` and `tools/re/s9/placements.json`.

## Review Focus

1. **The curves at their edges.** `dot` = 0.6 (width 0, k 0.2), 0.979 (not locked), 0.981 (locked — 0.98 sits on the float boundary of the strict `k < 0.01`), 0.99 (locked), −1 (k 1, lean 75°); the width and light steps are frame-rate independent. Test: `test_sword.cpp` "curves at the edges", "width smoothing is step-size independent", "light fades at 9.0/s".
2. **A ray that reads filled or clamped ground.** The beam and shade rays must use raw nodes: a hole or out-of-bounds is no hit, never the nearest node. Test: `test_heightmap.cpp` "ray_march passes over a hole", "out of bounds is no hit", "heightmap_node does not fill"; Task 9's `s9-lock` hit distance.
3. **The return machine after a door entry.** Entering the arena through the corridor door must reset the S7 return machine, so Valus's death returns to the shrine even when the loop began in the shrine (`ret` at its terminal `Free`). Test: Task 9's `s9e2e-return to=shrine` after `s9e2e-door from=plain to=arena`.
4. **S8 under the manifest switch.** `--s8-selftest`/`--s8-e2e` must still load `plain.manifest` and never see S9 state. Test: the frozen S8 acceptance in Tasks 7, 8 and 10; `s9-acceptance.sh`'s flag-refusal checks (exit 2).
5. **The shimmer that never stops.** Lowering and `R` stop the looping voice through `AudioSeam::stop_sword`; a stage switch already stops every voice (`new_stage` → `engine.reset()`). Test: `test_audio.cpp` "stop(name) ends a looping voice"; Task 9's `s9-audio shimmer_stopped=1` and `s9-reset`.
6. **`R` mid-raise, mounted-raised, mid-door with a carried horse.** The arena comes back at its opening state, sword Lowered, no carried pose. Test: `test_sword.cpp` "reset from every phase"; Task 9's `s9-reset` line.

---

## File Structure

- `tools/re/s9/placements.py`, `tools/re/s9/placements.json`, `tools/re/s9/road_points.py`, `docs/notes/s9-recon.md` — the gate and the header generator.
- `tools/re/stage/probe.py`, `docs/notes/scene-manifest.md` — `STAGES["road"]` and its composer.
- `tools/re/audio/s9_map.json`, `tools/re/audio/s5_audition.py` — the S9 audio slice.
- `Makefile` — `bake` (raise clips + the four sword textures), `audio` (s9 map), `road-manifest`, `accept: accept-s9`.
- `app/src/sword.h/.cpp` — the pure sword module (raise state, the beam's curves, the light) and `kSunDir`.
- `app/shaders/mesh.metal` — a comment naming `kSunDir` as its mirror (no code change).
- `app/src/heightmap.h/.cpp` — `heightmap_node`, `ray_march`, `build_backdrop_grid`.
- `app/src/audio.h/.cpp`, `app/src/audio_map.h` — `AudioEngine::stop(name)`, the S9 cue table.
- `app/src/sword_fx.h/.cpp`, `app/src/renderer.h/.cpp` — the sword pass and its hook (`effect.h/.cpp` untouched).
- `app/src/road_points.h` — the S9 placements, generated by `tools/re/s9/road_points.py` (the S8 `ride_points.py` pattern).
- `app/src/app.cpp`, `app.h`, `main.cpp` — the road stage, the doors, the carried horse, the raise input, `AudioSeam::play_sword`/`stop_sword`, the flags.
- `scripts/s9-acceptance.sh`, `docs/notes/s9-results.md`, `app/README.md`, `AGENTS.md`, `README.md`.

**Lanes:** Wave A, concurrent in separate worktrees, no lane waiting on another: Task 0 Steps 1–3 and 5 (W2; it gets the entrance list from Task 1's interface below, not from Task 1's code) ‖ Task 1 (W3) ‖ Task 2 (W1 sword) ‖ Task 3 (W1 heightmap) ‖ Task 4 (W1 audio) ‖ Task 5 (W1 sword pass; texture paths are its caller's input). Tasks 2 and 5 each append their own lines to `app/CMakeLists.txt` (a trivial merge). Task 0 Step 4 (the probes, which march the backdrop ray grid) runs after Task 3 lands. Rebase on `main` (S8 merged), then Task 6 (W4, the bake into the S9 library view) after Task 0, **alone** with respect to S9's lanes. Wave B, one `app.cpp` writer, in order: Task 7 (needs 0, 1, 6) → Task 8 (needs 2–7) → Task 9 (needs 8) → Task 10 (W4, needs 9). From Task 6 on every lane exports `SOTC_LIB=out/libviews/s9-road`.

### Task 0: The S9 gate (W2)

**Files:**
- Create: `tools/re/s9/placements.py`, `tools/re/s9/placements.json`, `docs/notes/s9-recon.md`

**Interfaces:**
- Produces `placements.json` (world frame; yaw degrees about +Y; every value with `source`):
  ```json
  {"door_corridor": {"stage": "plain", "min": [x,y,z], "max": [x,y,z],
                     "arrive_arena": {"p": [353, 27.951, 345], "yaw_deg": 49.24}, "source": "authored|data"},
   "door_arena":    {"stage": "arena", "min": [...], "max": [...],
                     "arrive_road": {"p": [x,y,z], "yaw_deg": a}, "source": "authored"},
   "valus": {"feet": [382, 28.369, 370], "chest_lift": 10.93, "source": "data"},
   "sun": {"dir": [0.4, 0.8, 0.6], "normalized": [0.371391, 0.742781, 0.557086], "shade_m": 300, "source": "authored"},
   "sword": {"glb": "models/.../wander_sword.glb", "bone": "OptBone_sword", "tip": [0, 0, -0.58], "source": "measured|authored"},
   "textures": {"lines": "textures/.../vertical_solid_beams_01_pfx.png", "overlay": "…/vertical_beams_wave_01_pfx.png",
                "flare": "…/soft_glow_01_falloff_pfx.png", "hit": "…/<sword_reaction's texture>.png",
                "sources": {"lines": "<…cptx>", "...": "..."}, "frame": [cols, rows]},
   "clips": {"player_a_hoist_sword_start": 30, "player_a_hoist_sword_loop": 50, "player_a_hoist_sword_end": 29,
             "player_a_uma_hoist_sword_start": 35, "player_a_uma_hoist_sword_loop": 60, "player_a_uma_hoist_sword_end": 25, "fps": 30},
   "probes": {"sunlit": {"p": [x,y,z], "yaw_away_deg": a}, "shade": {"p": [x,y,z]},
              "stairs_foot": [x,y,z], "entrance_top": [x,y,z], "agro_stairs": {"p": [x,y,z], "yaw_deg": a},
              "e2e_route": [[x,z], "..."]},
   "road_reach": {"nodes": n, "z_max": z, "corridor": {"x": [300, 308], "z": [280, 296], "y": [2.7, 5.6]}}}
  ```
- [ ] **Step 1: Reach and the doors.** Build the road's ground as the app will — the S8 plain set + the six entrance glbs `arena_01_entrance`, `_furwall`, `_ground`, `_skirt`, `_skirt_02`, `_stairs` (each `models/shadow/levels/arena_01_entrance/_cmn/<name>.glb`), unlayered, holes refused at 1.5 m drop / 1.0 m rise — with `tools/re/s8/reach.cpp plain` (it takes the glbs on its command line; no wait on Task 1) from the plain spawn (301, −7.14, −210); record `road_reach` (expect the corridor floor x 300–308, z 280–296, y 2.7–5.6 and no *reached* node at the (315.5, 291) overhang). Place `door_corridor` inside the reached corridor floor at its dead end (≈ x 300–308, z 286–296; y spanning the floor + 2.5 m); its arrival is the arena's own spawn. Walk the **arena's** own map by the arena's rule — unlayered, **no** hole refusal (every in-bounds move is walkable there; not `reach.cpp`'s plain rule) — over the encounter manifest's ground as the app builds it, from (353, 27.951, 345) down the bowl's slope toward the entrance; place `door_arena` there, ≥ 10 m from the spawn and clear of Valus's walk area; its arrival `arrive_road` on the reached corridor floor ≥ 3 m outside `door_corridor`, facing −Z (down the stairs).
- [ ] **Step 2: Valus and the sun.** Confirm Valus's feet against `out/re/stage/encounter.manifest`'s entity line and the chest lift against `docs/notes/s4-results.md`; confirm the sun literal at `app/shaders/mesh.metal:93`.
- [ ] **Step 3: The sword.** Confirm `wander_sword.glb` in the library (else record the `convert --under …/wander_sword.cmsh` command), `OptBone_sword` in Wander's skin, and which blade end (local z −0.58 or +0.12) is the tip with the sword in the hand at the `hoist_sword_loop` pose (the tip is the end farther from the hand bone); record the four textures' `.cptx` sources from `sword_line.cprt` and `sword_reaction.cprt` (the flare uses `soft_glow_01_falloff_pfx`, labelled), their atlas grid, and the six clip frame counts.
- [ ] **Step 4: Probes (after Task 3 lands — they march its backdrop ray grid).** With raw-node marches (Task 3's `heightmap_node`/`ray_march`/`build_backdrop_grid`, built into a scratch harness like `reach.cpp`; no fill, no clamp) through the road's ground and the backdrop ray grid: a `sunlit` point within ~40 m of the stairs whose shade march toward the sun is clear for 300 m and whose line from a 2 m-high tip to Valus's chest meets ground or backdrop within 150 m; a `shade` point whose shade march is blocked (the corridor under the cliff or the overhang's foot); `stairs_foot`, `entrance_top` (on the reached corridor floor), `agro_stairs` (Agro's start for the selftest's mounted raise and the e2e's lock, ≤ 10° slope, sunlit, its 150 m line to Valus's chest hitting ground or backdrop); `e2e_route` — waypoints from the shrine door's plain arrival to `stairs_foot` along walkable < 10° ground.
- [ ] **Step 5: `s9-recon.md`** (Steps 1–3 first; Step 4's numbers appended when it runs) — each gate PASS/FALLBACK with counts and the march numbers (hit distances, shade lengths), and a `python3 -I -c` validation of the JSON (both doors and arrivals, Valus, sun, sword, four textures, six clips, every probe).
- [ ] **Step 6: Commit** — `git commit -m "chore(re): the S9 gate"`

### Task 1: The road manifest (W3)

**Files:**
- Modify: `tools/re/stage/probe.py`, `docs/notes/scene-manifest.md`

**Interfaces:**
- Produces `out/re/stage/road.manifest`: S8's plain lines (instances, the 27 shrine backdrops at (300, 0.071, −300), the `horse` line, `spawn 301 -7.14 -210 0`), then the six entrance glbs as `instance` lines at identity, then `backdrop models/shadow/levels/arena_01/…/arena_01.glb 0 0 0 0 0 0 1 1 1 1`.
- [ ] **Step 1: Baseline** — the arena compose hash `088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0`; a fresh `compose --stage plain` into a scratch path `cmp`-equal to `out/re/stage/plain.manifest`; `check-manifest --stage plain|shrine` and the encounter check exit 0.
- [ ] **Step 2: `STAGES["road"]` and its own composer** — `cmd_compose`/`cmd_check_manifest` dispatch `road` to new `compose_road`/`road_checks` (the plain helpers read one set and one backdrop offset; do not generalise them): the plain lines exactly as `compose --stage plain` writes them, the encounter manifest's 18 instances under `levels/arena_01_entrance/` minus the 8 hash-suffixed 72-byte stubs (`exclude_stub`), `_stairs_lod1` (`exclude_lod`), `arena_01_entrance_low1` (excluded **by name**: it survives both `_lod[0-9]` and the `_low` suffix rule), `arena_01_entrance_demoblocker` and `arena01e_occluder` — exactly the six `arena_01_entrance`, `_furwall`, `_ground`, `_skirt`, `_skirt_02`, `_stairs` (each `models/shadow/levels/arena_01_entrance/_cmn/<name>.glb`) (the horse-ramp collision glb is not in that set); one identity `backdrop` for `arena_01.glb`; `parse_manifest_text` with `backdrop`/`horse` allowed (the S8 pattern). Pin `instances`, `entrance: 6` (the list above, in order), `backdrops` (28), `meshes`, `vertices`, `sha256`, spawn.
- [ ] **Step 3: `check-manifest --stage road`** — every glb exists; the plain prefix byte-equal to a fresh plain compose; the six entrance glbs by name, the exclusions (no stub, `_lod1`, `_low1`, demoblocker or occluder); exactly one `horse`; 27 backdrops at the shrine offset and one at identity naming `arena_01.glb`; the spawn.
- [ ] **Step 4: Verify** — the arena hash unchanged; the plain scratch compose still `cmp`-equal; `check-manifest --stage road` exits 0; the S8 `STAGES["plain"]` block is byte-unchanged (`git diff` shows no line in it).
- [ ] **Step 5: Commit** — `git commit -m "feat(re): compose the road manifest"`

### Task 2: The sword module (W1)

**Files:** Create `app/src/sword.h/.cpp`, `app/tests/test_sword.cpp`; Modify `app/CMakeLists.txt` (`sword.cpp` in both source lists, `test_sword.cpp`), `app/shaders/mesh.metal` (comment only)

**Interfaces:**
```cpp
// The sun (D6, authored): mesh.metal's key_light literal, mirrored here; mesh.metal's comment names kSunDir.
inline const Vec3 kSunDir = normalize(Vec3{0.4f, 0.8f, 0.6f});
inline constexpr float kShadeRayMetres = 300.0f, kBeamRayPerRaise = 150.0f;
inline constexpr float kLightRatePerSecond = 9.0f;        // 0.3 per 1/30 s step (unit unverified, labelled)
inline constexpr float kWidthStepFraction = 0.1f;          // per 1/30 s step: w += (t - w) * (1 - 0.9^(30 dt))
inline constexpr float kMaxLineWidth = 1.35f, kLeanMaxDeg = 75.0f, kLockK = 0.01f, kSparkCooldown = 0.5f;
enum class SwordPhase { Lowered, Raising, Raised, Lowering };
struct SwordClips { const char* start; const char* loop; const char* end; float startS, loopS, endS; };
inline constexpr SwordClips kSwordFoot{"player_a_hoist_sword_start", "player_a_hoist_sword_loop", "player_a_hoist_sword_end", 30/30.f, 50/30.f, 29/30.f};
inline constexpr SwordClips kSwordMounted{"player_a_uma_hoist_sword_start", "player_a_uma_hoist_sword_loop", "player_a_uma_hoist_sword_end", 35/30.f, 60/30.f, 25/30.f};
struct SwordInput { bool held = false; bool mounted = false; bool allowed = true; };  // allowed: on foot, or mounted at Stand
struct SwordBeam { float dot, k, leanDeg, widthTarget, rayLength; bool locked; Vec3 axis; std::array<Vec3, 8> lines; };
struct SwordState {
    SwordPhase phase = SwordPhase::Lowered; bool mounted = false;
    const char* clip = nullptr; float clipTime = 0, raise = 0, light = 0, width = 0, sparkCooldown = 0;
    int sparks = 0;
};
SwordBeam sword_beam(Vec3 tip, Vec3 aim, Vec3 target, float raise);
// sunlit: the shade march found nothing within kShadeRayMetres; hitMetres: the locked ray's hit (NaN = none).
SwordState step_sword(SwordState, float dt, const SwordInput&, const SwordBeam&, bool sunlit, float hitMetres);
float sword_intensity(const SwordState&);   // raise * light
bool sword_locks_movement(const SwordState&);  // on foot and not Lowered (turning stays)
SwordState reset_sword();
const char* sword_phase_name(SwordPhase);
```
Values (verbatim from design §6/D5/D6): `dot = normalize(target − tip) · normalize(aim)`; `k = clamp01(90·(1 − dot)/180)`; `leanDeg = k·75`; `widthTarget = clamp01((dot − 0.6)/0.4)·1.35`; `locked = k < 0.01`; `rayLength = raise·150`; line `i` (θ = i·45°) = `cos(lean)·axis + sin(lean)·(cos θ·v + sin θ·u)` with `axis = normalize(aim)`, `u = normalize(cross(axis, up))` (`cross(axis, X)` when `|axis·up| > 0.999`), `v = cross(u, axis)`; raise rises linearly over the start clip and falls linearly over the end clip from its value at release; light moves toward `sunlit ? 1 : 0` at 9.0/s; a locked hit with the cooldown spent counts a spark and restarts 0.5 s.
- [ ] **Step 1: Failing tests** — curves at the edges: dot 1 → k 0, lean 0, width 1.35, locked; 0.8 → k 0.1, lean 7.5°, width 0.675; 0.6 → k 0.2, lean 15°, width 0; 0.99 and 0.981 → locked; 0.979 → not locked (0.98 sits on the float boundary of `k < 0.01` — not tested); −1 → k 1, lean 75°, width 0 (all ±1e-5); each line's angle to the axis = lean (±1e-4), adjacent radial azimuths 45° apart, k 0 → all eight equal the axis, a vertical aim uses the X fallback (finite lines); `rayLength` 75 at raise 0.5; width smoothing: one 1/30 s step from 0 toward 1.35 → 0.135, two 1/60 s steps → 0.135 (±1e-5); light 1 → 0 in shade reaches 0 after 7 steps of 1/60 s (0.1 after 6) and stays in [0, 1]; phases: held → Raising on the start clip, Raised on the loop clip at `startS`, release → Lowering on the end clip → Lowered at `endS` (foot and mounted durations), release mid-raise lowers from the reached raise; `allowed = false` never leaves Lowered; sparks: 2.0 s locked with a hit → 4 sparks (first at once, then every 0.5 s), none unlocked or without a hit; `sword_locks_movement` true on foot Raising/Raised/Lowering, false mounted and Lowered; `reset_sword()` from every phase; a non-finite or negative `dt` advances nothing.
- [ ] **Step 2–4:** run → fail; implement (pure; no I/O/SDL/clock; `death.cpp` idioms); run → green.
- [ ] **Step 5:** the `mesh.metal:93` comment — `// mirrored as kSunDir in app/src/sword.h (S9 D6); change both`. No other shader edit.
- [ ] **Step 6: Commit** — `git commit -m "feat(app): the sword's raise, light and beam curves"`

### Task 3: Raw nodes, rays and the backdrop ray grid (W1)

**Files:** Modify `app/src/heightmap.h/.cpp`; Test `app/tests/test_heightmap.cpp`

**Interfaces:**
```cpp
// The nearest node's raw height: NaN when p is outside the bounds or the node is empty. Never clamps, never fills.
float heightmap_node(const Heightmap&, Vec3 p);
// The first t in [step, maxLen] (steps of `step`) where heightmap_node(o + d t) > (o + d t).y; NaN = no hit.
float ray_march(const Heightmap&, Vec3 origin, Vec3 dir, float maxLen, float step);
// An unlayered grid over the static meshes with MeshCpu::ground == false (the backdrops) only, bounds = theirs;
// an empty Heightmap (width 0) when the scene has none. The scene's own bounds and ground map are untouched.
Heightmap build_backdrop_grid(const Scene&, float cellSize);
```
- [ ] **Step 1: Failing tests** — `heightmap_node` returns NaN over a hole where `heightmap_sample` fills, NaN out of bounds where `heightmap_sample` clamps, the node's value elsewhere; `ray_march` hits a raised block at its face (± step), passes over a hole, returns NaN beyond `maxLen`, NaN when it leaves the bounds, NaN on an empty map; `build_backdrop_grid` rasterizes a `ground = false` quad and ignores `ground = true`/rigged meshes, is empty with no backdrops, and leaves `build_heightmap`'s output for the same scene byte-identical.
- [ ] **Step 2–4:** run → fail; implement (reuse the rasterizer behind `build_heightmap` with a mesh filter; no change to existing functions' behaviour); run → green, existing cases unchanged.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): raw-node rays and the backdrop ray grid"`

### Task 4: The mixer's stop path and the S9 cues (W1)

**Files:** Modify `app/src/audio.h/.cpp`, `app/src/audio_map.h`; Test `app/tests/test_audio.cpp`, `app/tests/test_audio_events.cpp`

**Interfaces:**
```cpp
// AudioEngine: stops every SFX voice playing `name` (music/ambience slots untouched); a no-op when none does.
void stop(const std::string& name);
// audio_map.h, the S8 RideSfx pattern:
enum class SwordSfx { Shimmer, Search, Count };
struct SwordCue { const char* clip; float gain; };   // {"sword.shimmer", 0.6f}, {"sword.search", 0.7f} — authored gains
const SwordCue& sword_cue(SwordSfx);
const char* sword_sfx_name(SwordSfx);   // "shimmer", "search"
```
Behaviour (authored, labelled): the shimmer (`loop=1` in the manifest) starts when the sword leaves Lowered and stops when it reaches Lowering; the search cue plays once on each rising edge of `locked`.
- [ ] **Step 1: Failing tests** — a looping clip played twice and `stop(name)` → `is_playing(name)` false, `active_voices()` 0; another clip's voice and the music survive `stop`; `stop` of a name not playing changes nothing; the S9 cue table covers every `SwordSfx` and names `sword.shimmer`/`sword.search`; the S5/S6/S8 tables are unchanged.
- [ ] **Step 2–4:** run → fail; implement (`stop_slot` per matching voice); run → green.
- [ ] **Step 5: Commit** — `git commit -m "feat(app): stop a looping cue; the sword's cue table"`

### Task 5: The sword effect pass (W1)

**Files:** Create `app/src/sword_fx.h/.cpp`, `app/tests/test_sword_fx.cpp`; Modify `app/src/renderer.h/.cpp`, `app/CMakeLists.txt` (`sword_fx.cpp` in both lists, the test). `effect.h/.cpp` and `tendril.metal` are not edited.

**Interfaces:**
```cpp
struct SwordFxFrame {
    bool active = false; Vec3 tip{}; std::array<Vec3, 8> lines{};
    float length = 0, width = 0, alpha = 0;   // alpha = sword_intensity
    float flareSize = 0.6f;                   // authored
    bool hit = false; Vec3 hitPoint{}; float hitSize = 2.0f;  // authored
};
// Vertex order: [0,32) the 8 line quads (tip -> tip + line*length, camera-facing, `width` wide);
// [32,36) the flare at the tip; [36,40) the hit sprite (only when hit). Inactive or alpha 0 -> empty.
std::vector<EffectVertex> build_sword_vertices(const SwordFxFrame&, const Mat4& viewProj);
// rel (library-relative PNG paths, supplied by the caller — Task 8 passes road_points.h's kSwordTextures):
// [0] vertical_solid_beams_01_pfx, [1] vertical_beams_wave_01_pfx, [2] soft_glow_01_falloff_pfx, [3] sword_reaction's texture.
std::array<TextureCpu, 4> load_sword_textures(const std::string& libraryRoot, const std::array<std::string, 4>& rel);
// SwordFxFrame also carries `int atlasCols = 1, atlasRows = 1;` (road_points.h's kSwordAtlasGrid); the lines sample frame 0.
class SwordFxRenderer {   // EffectRenderer's shape over tendril.metal's additive pipeline; 40-vertex buffer, one static index buffer
public: bool create(SDL_GPUDevice*, const std::array<TextureCpu, 4>&, SDL_GPUTextureFormat);
        void update(const SwordFxFrame&, const Mat4& viewProj); void draw(SDL_GPUCommandBuffer*, SDL_GPURenderPass*, const Mat4&);
        void release(); };
// Renderer: bool upload_sword_textures(const std::array<TextureCpu, 4>&);
//           render_frame(..., const EffectFrame* effects = nullptr, const SwordFxFrame* sword = nullptr);
// The lines draw twice (texture 0, then the overlay 1), the flare with 2, the hit with 3; after the tendril pass.
```
- [ ] **Step 1: Failing tests** — an inactive frame and an alpha-0 frame build 0 vertices; a hit-less active frame 36, with a hit 40; each line quad's far-edge midpoint is `tip + line·length` (±1e-4) and its two edges are `width` apart; the vertex alpha equals `alpha`; the same frame and camera twice are byte-identical; `load_sword_textures` reads exactly the given paths and throws naming a missing one; a 2×2 atlas grid maps the line quads' UVs onto frame 0 (u, v ∈ [0, 0.5]).
- [ ] **Step 2–4:** run → fail; implement; run → green.
- [ ] **Step 5:** alone, `caffeinate -dimsu ./scripts/s6-acceptance.sh` → PASS (the tendrils and every frozen pixel stat unchanged with the new `render_frame` parameter defaulted).
- [ ] **Step 6: Commit** — `git commit -m "feat(app): the sword's light pass"`

### Task 6: The S9 bakes and audio (W4, alone)

**Files:** Modify `Makefile` (`WANDER_CLIPS` += the six raise clips in design D9's order; `bake` adds the four `bp-convert texture` lines from Task 0's `textures.sources` and, if Task 0 found it missing, the `wander_sword.cmsh` convert; `audio` → `s9_map.json`, help text "36 entries"; `road-manifest` composing `out/re/stage/road.manifest` when absent), `tools/re/audio/s9_map.json` (create, `milestone: "s9"`), `tools/re/audio/s5_audition.py` (`CORE_EVENTS_BY_MILESTONE["s9"]` = the S8 set + `sword.shimmer`, `sword.search`), `app/tests/test_asset_io.cpp`, `app/tests/test_audio.cpp`, `docs/notes/s9-recon.md` (the commands)

**Interfaces:**
- Produces, in the library view `out/libviews/s9-road` only: `wander.glb` = its 43 clips + `player_a_hoist_sword_{start,loop,end}`, `player_a_uma_hoist_sword_{start,loop,end}` at 43…48 (`wander.clips.json`); the four sword PNGs under `textures/…`; `audio/audio.manifest` with S8's 34 + `sword.shimmer` (`com_obj`/`sword_test`, `"loop": true`) + `sword.search` (`com_obj`/`sword_boss_test`) = **36**, `method: audition`, confidences from the audition.
- [ ] **Step 0: Preconditions** — `s9-road` is rebased on `main` with S8 merged (`scripts/s8-acceptance.sh` exists). `make libview BRANCH=s9-road` and `export SOTC_LIB=<the printed view>` (`out/libviews/s9-road`) — in this lane and every lane after it. `grep -n "s8_map.json" scripts/s8-acceptance.sh` shows the S8 script decoding its own map with the CLI (not `make audio`); if it calls `make audio`, stop and raise it with the user (the script is frozen).
- [ ] **Step 1: Bake** (no S9 lane building or running `ctest` against the view) — `make bake LIB="$SOTC_LIB"` (extended); check `wander.clips.json` order (indices 0…42 unchanged, 43…48 the raise clips) and the four PNGs' size and dimensions; the base `out/library` is untouched (its `wander.clips.json` still lists 43).
- [ ] **Step 2: Audio** — audition `sword_test`/`sword_boss_test` waves with `s5_audition.py --root "$SOTC_GAME_ROOT"` into `out/s9/audition`; write `s9_map.json` (the S8 rows verbatim + the two S9 rows); `s5_audition.py --root "$SOTC_GAME_ROOT" --check` on the S5, S6 and S8 maps (unchanged results) and on the S9 map (passes); `make audio LIB="$SOTC_LIB"` → **36** entries in the view.
- [ ] **Step 3: Tests (on `s9-road`)** — `test_asset_io.cpp`: Wander's exact count pin (`== 43`, main's line ~564) becomes 49 with the six raise clips pinned by name at 43…48; `test_audio.cpp`'s manifest case (main's line ~644, 8/10/34) also accepts 36; ctest green against the view. `main`'s pins are not touched now: relaxing them (with the user's OK, recorded then) and promoting the view's bake to the base library happen at S9's merge.
- [ ] **Step 4: Verify** — alone, `caffeinate -dimsu ./scripts/s8-acceptance.sh` with `SOTC_LIB` at the view → PASS (it decodes S8's 34 itself; S8 unchanged by the appended clips).
- [ ] **Step 5: Commit** — `git commit -m "feat(convert): bake the raise clips and the sword's light; decode the S9 audio slice"`

### Task 7: The road stage and the arena doors (W1)

**Files:** Create `tools/re/s9/road_points.py` (stdlib; writes `app/src/road_points.h` from `tools/re/s9/placements.json` verbatim with provenance comments — the S8 `tools/re/s8/ride_points.py` pattern) and the generated `app/src/road_points.h`; Modify `app/src/app.cpp`, `app/src/app.h` (`AppOptions::roadManifestPath` = `out/re/stage/road.manifest`, `s9Selftest`, `s9E2e`, `s9StillsPath`; `StageCarry` gains `StageId doorFrom = StageId::Arena` and `bool horseCarried = false; Vec3 horsePos{}; float horseYawDeg = 0;`), `app/src/main.cpp` (`--road-manifest`)

**Interfaces:**
```cpp
namespace road_points {   // ride_points::Door reused
inline constexpr ride_points::Door kDoorCorridor{…};   // on the road; arrives at the arena spawn (353, 27.951, 345), 49.24°
inline constexpr ride_points::Door kDoorArena{…};      // in the arena; arrives on the corridor floor
inline constexpr float kValusFeet[3]{382.0f, 28.369f, 370.0f}; inline constexpr float kValusChestLift = 10.93f;
inline constexpr float kSwordTip[3]{0.0f, 0.0f, -0.58f};   // in OptBone_sword's frame (Task 0's sign)
// the probes: kProbeSunlit (+ yaw away), kProbeShade, kStairsFoot, kEntranceTop, kAgroStairs, kE2eRoute[]
inline const std::array<std::string, 4> kSwordTextures{…};   // Task 0's `textures` (library-relative PNGs)
inline constexpr int kSwordAtlasGrid[2]{cols, rows};
}
```
- Produces: the Plain stage's manifest is `s9_wired ? roadManifestPath : plainManifestPath` (both error hints name their compose command); the doors are a lookup by `(here, door)`: on the Plain stage `kDoorPlain` → Shrine and `kDoorCorridor` → Arena, in the Arena `kDoorArena` → Plain (S8's Shrine door unchanged); the S8 door code fires them on foot, not riding, not input-locked, through `step_door` unchanged; the firing stage writes `carry.doorFrom = here`; the arrival spawn is chosen by `(doorFrom, target)`, not `here` alone (the road has two arrivals); the corridor and arena doors step only when `s9_wired`; S8's gate becomes `s8_wired = (interactive_run && !opts.s7E2e) || opts.s8Selftest || opts.s9Selftest` (S8 app.cpp ~4873) so the selftest can whistle, mount and ride, and `--s9-selftest` starts on the Plain stage (road manifest) the way `--s8-selftest` does (app.cpp ~2288); leaving the road by either door stores Agro's position/yaw in `carry` (the horse stays where she was left — a stale arena-door pose never overrides a later ride); the next road load places Agro there instead of the manifest's `horse` line (on her floor); a load of the Arena with `carry.door` arriving from the Plain sets `carry.ret = reset_return()` before the stage's machines are built (fresh encounter by the new iteration); `R` (`press_reset`) also clears `horseCarried`.
- [ ] **Step 1: Failing tests** — `test_door.cpp`: each arrival spawn in `road_points.h` lies outside the door box of the stage it lands on (a spawn never re-fires), and every corridor/arena box has positive extent. The flag rules (`--s9-selftest`/`--s9-stills` join the frozen exclusions like `--s8-selftest`; `--s9-e2e` is interactive like `--s8-e2e`, refused with any selftest, capture or `--no-return`, allowed with `--stage shrine`; `--road-manifest` parses) live in `main.cpp`, which `sotc-app-tests` does not build — they are asserted as exit-2 refusals in `s9-acceptance.sh` (Task 10). `python3 -I tools/re/s9/road_points.py` regenerates `road_points.h` byte-identically.
- [ ] **Step 2–4:** run → fail; implement; run → green.
- [ ] **Step 5:** interactive smoke: `caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest --model models/shadow/characters/wander/_cmn/wander.glb --stage plain` loads the road (its `s7-load` line names the road's instance count) without errors (quit with Escape).
- [ ] **Step 6:** alone, `caffeinate -dimsu ./scripts/s8-acceptance.sh` (view) → PASS; then `make audio LIB="$SOTC_LIB"` (36).
- [ ] **Step 7: Commit** — `git commit -m "feat(app): the road, the arena doors and Agro left behind"`

### Task 8: The sword, wired (W1)

**Files:** Modify `app/src/app.cpp`, `app/src/gamepad_map.h` (`kRaiseGamepadButton = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER`), `app/README.md` (controls)

**Interfaces:**
- Produces, on the road under `s9_wired` only: `wander_sword.glb` appended (the `load_club` pattern: **after** `build_heightmap` and `build_backdrop_grid` — it is static with `ground = true` by default and must never rasterize into either — and before `renderer.upload`) and placed every frame at `rig_bone_world(playerRig, "OptBone_sword")`; the tip = that matrix × `kSwordTip`; the backdrop ray grid built once at load (`build_backdrop_grid(scene, kHeightmapCellSize)`, right after `build_heightmap`); the sword textures loaded with `load_sword_textures(lib, road_points::kSwordTextures)` and uploaded (`upload_sword_textures`; a failure warns once and the raise works without light, fatal under `--s9-selftest`/`--s9-e2e`); hold `C` / left shoulder = `SwordInput::held`, `allowed` on foot or riding at `Gait::Stand`; while the sword is not Lowered the player's clip is the sword's (foot) or the rider's clip is overridden by `kSwordMounted` (Stand), kicks/mount/dismount are ignored (release `C` to ride), and on foot `sword_locks_movement` turns the move input into a facing with zero speed; per frame: `beam = sword_beam(tip, camera forward, Valus chest, raise)`, `sunlit = isnan(ray_march(ground, tip, kSunDir, 300, 1))` and the same through the ray grid, the locked ray `std::fmin(ray_march(ground, tip, axis, rayLength, 0.5), ray_march(raygrid, …))` (`fmin` returns the other operand when one is NaN — `std::min` would not), `step_sword(…)`, the `SwordFxFrame` (lines clipped to the hit when locked) to `render_frame`; cues through the app's `AudioSeam` wrapper, which gains `play_sword(SwordSfx, Vec3)` and `stop_sword()` (→ `engine.stop("sword.shimmer")`): the shimmer starts on leaving Lowered and `stop_sword()` runs on entering Lowering and on `R` (a stage switch already stops every voice: `new_stage` → `engine.reset()`); the search cue once per lock; `R` → `reset_sword()`.
- [ ] **Step 1: Failing tests** — none new beyond Tasks 2–5 (the wiring is proven by Task 9); `ctest` green.
- [ ] **Step 2:** interactive smoke on the road: `C` raises (the start → loop clips), the fan draws, the shimmer loops and stops on release; quit with Escape.
- [ ] **Step 3:** alone, `caffeinate -dimsu ./scripts/s8-acceptance.sh` (view) → PASS; then `make audio LIB="$SOTC_LIB"`.
- [ ] **Step 4: Commit** — `git commit -m "feat(app): raise the sword; its light points to Valus"`

### Task 9: `--s9-selftest`, `--s9-e2e` (W1)

**Files:** Modify `app/src/app.cpp`, `app/src/app.h` (`StageCarry` gains `int s9Step = 0;` — the selftest's and the e2e's progress across the stage switches, the `s7Step`/`e2eStep` pattern), `app/src/main.cpp` (usage text)

**Interfaces — exact lines:**
`s9-load stage=plain manifest=road instances=%d backdrops=%d meshes=%d raygrid=%dx%d ms=%.1f`; `s9-sun dir=%.6f,%.6f,%.6f`; `s9-raise clip=player_a_hoist_sword_start then=player_a_hoist_sword_loop raise=%.3f`; `s9-fan dot=%.3f k=%.3f lean_deg=%.2f width=%.3f` (facing away); `s9-partial dot=%.3f k=%.3f lean_deg=%.2f width=%.3f` (camera ~37° off Valus); `s9-lock k=%.4f locked=1 ray_m=%.1f hit=1 hit_m=%.1f`; `s9-spark count=%d seconds=2.0`; `s9-shade light_from=%.3f light=%.3f seconds=%.3f`; `s9-mounted clip=player_a_uma_hoist_sword_loop gait=Stand kick_ignored=1`; `s9-entrance top=%.2f,%.2f,%.2f`; `s9-door from=plain to=arena spawn=%.2f,%.2f,%.2f yaw=%.2f ret=Idle`; `s9-door from=arena to=plain spawn=%.2f,%.2f,%.2f agro_moved=%.3f`; `s9-audio shimmer_started=1 shimmer_stopped=1 search=%d`; `s9-perf frames=240 frame_ms=%.3f drawn=%d culled=%d`; `s9-reset stage=arena ride=OnFoot door=Idle sword=Lowered horse=none valus=100 player=100`; e2e: `s9e2e-door from=shrine to=plain`, `s9e2e-mount side=<L|R>`, `s9e2e-lock k=%.4f locked=1 hit=<0|1> hit_m=%.1f`, `s9e2e-ride gallop_m=%.1f`, `s9e2e-stairs mounted=1 top=%.2f,%.2f,%.2f`, `s9e2e-door from=plain to=arena`, `s9e2e-death phase=Done`, `s9e2e-return to=shrine`, `s9e2e-done frames=%d`.
- [ ] **Step 1: `--s9-selftest`** (requires `--manifest`/`--model`; fixed 1/60 s, no window input; starts on the road) — at `kProbeSunlit` facing away: raise (expect 1.0 s to Raised) → `s9-raise`, `s9-fan` (k ≥ 0.95, width 0.000); the camera to dot 0.80 ± 0.01, 2 s → `s9-partial` (k 0.100 ± 0.005, lean 7.50 ± 0.40, width 0.675 ± 0.01) and the `fan.png` still; the camera onto Valus's chest → `s9-lock` (k < 0.01, `hit_m` < 150), 2 s → `s9-spark` (4), `locked.png`; teleport to `kProbeShade` → `s9-shade` (light 0.000 within 0.120 s); lower; Agro at `kAgroStairs`, mount, raise at Stand → `s9-mounted`, a kick ignored, lower, dismount; walk from `kStairsFoot` up the stairs → `s9-entrance` (y 2.7–5.6, z ≥ 280, x 300–308), `entrance.png`; into `kDoorCorridor` → the arena → `s9-door …to=arena` (spawn = (353, 27.951, 345) ± 0.05, yaw 49.24, ret Idle); into `kDoorArena` → the road → `s9-door …to=plain` (spawn = the arrival ± 0.05 on its floor, Agro within 0.010 m of where she was left), `arena_door.png` on arrival; `s9-audio`; 240 perf frames on the road → `s9-perf`; `R` while raised with a carried horse → `s9-reset`. Stills under `--s9-stills`.
- [ ] **Step 2: `--s9-e2e`** — the interactive loop at fixed 1/60 s with scripted input (the `--s8-e2e` seam; its own script state; the camera yaw scripted for the lock): `--stage shrine` → walk into `kDoorShrine` → the road; whistle, mount; kick to Gallop and steer along `kE2eRoute` to `kStairsFoot` (the open plain is ~600 m from Valus, beyond the 150 m ray — the lock is done near the stairs); rein to Stand near `kAgroStairs`, raise at Stand, aim at Valus until locked (`locked=1` asserted; `hit` printed, 1 expected at `kAgroStairs` per Task 0), lower; kick to Walk, ride up the stairs to the corridor floor, hold `S` to stop, dismount; walk into `kDoorCorridor`; 1 s in the arena, the production lethal hit (the S7 e2e path); the death to Done, the return to the shrine; exit 0 one second into Free. Each join asserted with a timeout (exit 1 naming the step).
- [ ] **Step 3: Run** — with `SOTC_LIB` at the view, `make audio LIB="$SOTC_LIB"` first (36); both exit 0 with every line; look at the stills (`sips -Z 1024` copies) and report what they show; then alone `caffeinate -dimsu ./scripts/s8-acceptance.sh` → PASS.
- [ ] **Step 4: Commit** — `git commit -m "feat(app): prove the road and the sword (--s9-selftest, --s9-e2e)"`

### Task 10: The S9 acceptance and the results (W4)

**Files:** Create `scripts/s9-acceptance.sh`, `docs/notes/s9-results.md`; Modify `app/README.md`, `AGENTS.md` (S9 status; the frozen list gains `s8-acceptance.sh`), `README.md`, `Makefile` (`accept: accept-s9`, a `road` run target)

- [ ] **Step 1: Write the script** (bash 3.2, the S7/S8 scripts' structure) — `SOTC_LIB` exported at the S9 library view (`make libview BRANCH=s9-road` prints it; the script refuses the base `out/library`); build + both suites; the **frozen S8 acceptance** first (it runs S7 → … → S3 and decodes S8's own map); the S9 decode `bp-convert audio --map tools/re/audio/s9_map.json --force` (36 entries) and `s5_audition.py --root "$SOTC_GAME_ROOT" --map tools/re/audio/s9_map.json --check`; the manifests: the arena hash, a scratch plain compose `cmp`-equal to `plain.manifest`, `check-manifest --stage plain|shrine|road`, a scratch road compose `cmp`-equal to `road.manifest`; `tools/re/s9/road_points.py` regenerates `app/src/road_points.h` byte-identically (into a scratch path, `cmp`); the flag refusals exit 2 (`--s9-e2e` with `--s9-selftest`, with `--screenshot`, with `--no-return`; `--s9-selftest` without `--manifest`); Wander's `wander.clips.json` 43…48 names and the four sword PNGs exist (PNG magic, Task 0's dimensions); `--s9-selftest --s9-stills out/s9/stills` and `--s9-e2e`; assert every line above with its values/tolerances, `s9-sun` = 0.371391,0.742781,0.557086, `frame_ms ≤ 16.7`, the road load ≤ 15000 ms, the exact reset line, the e2e join order; the stills (2560×1440, PNG magic, > 5000 B, span ≥ 30); `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0`; `PASS —` with the measured values.
- [ ] **Step 2: Run** — alone, `caffeinate -dimsu ./scripts/s9-acceptance.sh > out/s9-acceptance-run.log 2>&1` → exit 0.
- [ ] **Step 3: Write `s9-results.md`** — measured values only; the gate outcomes; every authored value labelled (the `C`/left-shoulder remap of R2, the aim as the camera look, the tip offset, the linear raise blend, the sun and the 300 m shade, the step unit, the sprite sizes, the search trigger, the door boxes/spawns); the known gaps (the climb, the moving and additive raises, raised walking, the sky beam, the weak-point beam, the sword glow, `touch`, `Sword_reflection`, the subtractive reaction layer, the raise only on the road, low-confidence cue picks); the three labelled rulings (Global Constraints: Agro's pose on either door — a deviation beyond D4; the fan around the aim; the cue timing); the user's walk (design §11) as unchecked boxes.
- [ ] **Step 4: Commit** — `git commit -m "docs: S9 acceptance and results"`

## Self-Review

- **Spec coverage:** G1 → Tasks 0, 1, 7, 9 (`s9-entrance`, `s9e2e-stairs`); G2 → Tasks 0, 7, 9 (both `s9-door` lines, `agro_moved`); G3 → Tasks 0, 2, 3, 5, 6, 8, 9; G4 → Tasks 4, 6, 8; G5 → Tasks 9, 10. D1 → all; D2 → 1, 7; D3 → 0, 7, 9 (`carry.ret = reset_return()`, Review Focus 3); D4 → 7; D5 → 0, 2, 3, 8; D6 → 2, 3, 8; D7 → 5, 8; D8 → 2, 8; D9 → 6, 8; D10 → 4, 6, 10; D11 → 9. §8.1 gate items → Task 0; §9 builder calls → Tasks 0, 2, 4, 5, 9, 10 (each labelled in `s9-results.md`).
- **Frozen paths:** `STAGES["plain"]`, `plain.manifest` and `s8_map.json` untouched (Task 1 Step 4, Task 10's `cmp`); `effect.*`/`tendril.metal` untouched (the S6 chain in Task 5); `render_frame`'s new parameter defaults to the old call; `s9_wired` excludes `--s8-selftest`/`--s8-e2e`/`--s7-e2e`; the sword and its mesh live only on the road; Wander's 43 indices kept; every S9 bake/decode lands in `out/libviews/s9-road`, never the base library, and `main`'s pins (`== 43`, 8/10/34) are relaxed only at S9's merge with the user's OK; `make audio` moves only after S8's script is confirmed to decode its own map (Task 6 Step 0); the frozen S8 acceptance runs in Tasks 6–10.
- **Names:** `kSunDir`, `SwordPhase`, `SwordClips`, `kSwordFoot`, `kSwordMounted`, `SwordInput`, `SwordBeam`, `SwordState`, `sword_beam`, `step_sword`, `sword_intensity`, `sword_locks_movement`, `reset_sword`, `heightmap_node`, `ray_march`, `build_backdrop_grid`, `AudioEngine::stop`, `SwordSfx`, `sword_cue`, `SwordFxFrame`, `build_sword_vertices`, `load_sword_textures`, `SwordFxRenderer`, `upload_sword_textures`, `road_points::kDoorCorridor`/`kDoorArena`/`kSwordTextures`/`kSwordAtlasGrid`, `road_points.py`, `AudioSeam::play_sword`/`stop_sword`, `roadManifestPath`, `StageCarry::doorFrom`/`horseCarried`/`s9Step`, `s9_wired`, `s9Selftest`, `s9E2e`, `s9StillsPath` — consistent across Tasks 2–10.
