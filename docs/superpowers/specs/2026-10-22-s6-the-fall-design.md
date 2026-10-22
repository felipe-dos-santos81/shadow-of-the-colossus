# S6 — "The fall": Valus's death sequence and the black tendrils

**Status:** Proposed — builder call (the user skipped the question pass on 2026-10-22). Design dated
2026-10-22. The binding milestone after S5 "The sound of the forbidden land". S5/S4.2/S4.1/S4/S3 stay
green and unchanged.

## 1 · Intent

The fight currently ends in a freeze: Valus's health reaches 0, `minotaur_a_death_front` plays once and
holds its last frame, and nothing else happens — no fall, no black tendrils, no camera, no end. The
S5 audio leaves Valus's death and the club whoosh silent (deferred, R15). This milestone gives the
first colossus the game's actual payoff: the death motion, the black tendrils erupting from the
wound, the collapse into the dead body, a cinematic camera, the death roar and the club whoosh, and a
stable encounter end the player can walk away from (or `R` reset).

The milestone is also the project's first **effect** milestone: the renderer has only an opaque mesh
pass and a screen-space HUD pass. The tendrils require the first world-space blended pass, the first
per-frame dynamic geometry, and the first camera-facing billboards — the foundation the later
colossi will reuse.

## 2 · Goals

- **G1 — The death sequence.** On Valus's health 0, a deterministic phase machine plays the death
  motion (`minotaur_a_death_front`, 94 frames / 6.233 s), the tendril burst, the collapse/corpse
  swap, and the encounter end; `R` resets at any point and restores the opening state.
- **G2 — The tendrils.** A labeled reconstruction using the game's own assets: the three emitters
  from `death_tentacle.cprt` (`main_wisp`, `smoke`, `veins`), the `2x2`/`8x8` `.cptx` smoke atlases,
  the `death_tentacle_veins` texture, and the binary's 201-segment × 0.25 m geometry rooted at the
  hit points/wounds. The GCN `.csdr` evaluator scripts are **not** ported; the motion is the
  builder's authored curl/turbulence, labeled as a reconstruction.
- **G3 — The corpse.** At the motion's end the live entity is hidden and the already-converted
  `c01_minotaur_a_dead_body.glb` (+ the dirtmound) is placed at the entity transform; the corpse
  persists; the HUD drops Valus's bar; the player can move again after the cinematic. The dead
  materials' veins layer is carried into the app through D9's emissive slot, so the corpse keeps the
  shipped black-vein look.
- **G4 — The camera.** A reconstructed cinematic death camera (the pull-back/orbit/low-angle path)
  with the player's input locked during the sequence. The shipped `camera_minoa_death_front_camera_01/
  02.anb` clips are type 36 (undecoded) and are **not** played; the note records the gap.
- **G5 — The audio.** `valus.death` (`boss01`/`mavo_death`, waves 75–77) at the tendril burst and
  `valus.club_swing` (`boss01`/`boss01_mvt_swing_add`, waves 55–57) on the club swing; the death
  clears Valus's deferred entry; the score stops at the defeat; the S5 mix/manifest/schema contract
  holds.
- **G6 — The proof.** `--s6-selftest --s6-stills out/s6/stills` drives a scripted lethal event (the
  frozen S4.1 acceptance remains the real three-strike defeat) and the death, asserts the
  phases/timings/state/audio, and captures stills (the motion, the tendril burst, the corpse, the
  camera). `scripts/s6-acceptance.sh` runs it plus the frozen S5/S4.2/S4.1/S4/S3 regressions, the
  pixel/still checks, and the frame cost.

## 3 · Non-goals

- Porting the `.cprt`/`.csdr` particle evaluator or the Orbis GCN shaders; matching the game's
  particle motion frame-for-frame.
- The type-36 camera/object animation clips (they stay undecoded; the camera is reconstructed).
- Ragdoll/physics on the corpse; the dead body is a static mesh.
- The second colossus, the overworld, Agro, the shrine, the Dormin voice (later milestones).
- The still-deferred `player.fall`, ambience, grab/climb/footsteps; the 5.1/3D mix; the exact
  NGS2 PS-ADPCM fidelity.
- A victory screen, save, or transition to another area.

## 4 · Anti-goals

- **A1 — A placeholder tendril.** Generic particles with no relation to the shipped assets would
  pass a smoke test and fail the point. The reconstruction must use the original atlases, the veins
  texture, the emitter names/roles, and the binary's segment geometry, and must say "reconstruction"
  in the note and the walk.
- **A2 — A death that leaves the game stuck.** The sequence must end in a stable, walkable state; `R`,
  `M` and `Escape` must keep working during and after it.
- **A3 — Mutating the frozen numbers.** S4.1/S4.2/S5 scripts, status lines and stills are the
  regression; S6 adds `s6-*` lines and stills only.
- **A4 — Overclaiming fidelity.** The note must distinguish the original assets from the authored
  motion/camera and the undecoded clips.
- **A5 — Cursor capture or a fullscreen window.** The viewer stays windowed; the cursor is never
  captured.

## 5 · Constraints

- Apple Silicon, C++20, SDL3/SDL_GPU/Metal, arm64 only; no new runtime dependency; the effect pass is
  hand-written Metal + SDL_GPU.
- Static-only RE; the game tree is read-only; assets and derived glbs/textures are gitignored and
  never committed; the RE evidence lives in `docs/notes/` and the SDD records.
- S5's audio contract: 48 kHz float32 WAVs, the `audio.manifest` `key=value` lines, the map schema,
  `gain_db` provenance-only, the S5 decoder/gate untouched by the runtime.
- The acceptance scripts are bash 3.2, every viewer/acceptance run under `caffeinate -dimsu`, never
  two acceptances concurrently.
- The frozen `scripts/s42-acceptance.sh`, `s41-acceptance.sh`, `s4-acceptance.sh`, `s3-acceptance.sh`,
  and `s5-acceptance.sh` are invoked unchanged; `s5_map.json` and the S5 manifest stay as S5 shipped.

## 6 · What the game does (evidence)

**The death bundle.** The `MINOTOUR_A_DEATH` motion record (eboot `0x01669e68`, hash `0xf8cc5162`,
5 entries) names `minotaur_a_death_front.anb` (94 f @ 15.0806 fps = 6.233 s, 58 tracks),
`minotaur_a_death_front_loop.anb` (3,532 B, 1 frame, the shipped dead-hold pose),
`objanim_…_death_front.anb` (4,808 B), and `camera_minoa_death_front_camera_01/02.anb` (844/2,244 B,
type 36 — undecoded). The app already bakes/plays the front clip (`app/src/app.cpp:409`,
`kDeathClip`); the loop clip is baked nowhere and the last frame is frozen instead
(`docs/notes/s41-results.md:361`).

**The dead body.** `…/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_body.cmsh`
(2,735,727 B; 6 meshes, 216,272 verts) + the skirt (45,215 B) + the dirtmound (134,740 B) already
convert to `out/library/…/c01_minotaur_a_dead_body.glb` (11,924,836 B, 6 meshes, 0 skin, 6
`colossus_01_*_dead` materials). The `death_tentacle_veins.png` (1024×256 RGBA8) is already in the
library. **Gap:** the game's dead `.cmat` carries the veins layer as its sixth slot, but the app's
`MaterialCpu`/`MaterialData` and `mesh.metal` sample only one base texture, so that layer is dropped
by the converter; S6 must choose a corpse material path (D9).

**The tendril.** `particles/shadow/colossus/death_tentacle/_ps4/death_tentacle.cprt` (2,376 B) has
three emitters — `main_wisp`, `smoke`, `veins` — each with `.csdr` GCN bytecode (1.4–6.2 KB) and one
`.cptx`: `vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.cptx` (492,165 B),
`8x8_smoke_wispy_sharp_1024_pfx.cptx` (780,747 B), `vfx/common/smokeman/_ps4/
2x2_smoke_tendrils_01_pfx.cptx` (47,183 B). The `.cptx` are the same Oodle/CTXR family the converter
already decodes (the `2x2_oakleaf_pfx` proof); the veins texture is `textures/shadow/characters/
common/_ps4/death_tentacle_veins.ctxr`. Wiring: `particles/shadow/_cmn/effect_map.crmp` (5,805 B),
`scripts/shadow/entities/colossi/_cmn/death_tentacle.cgpr` (297 B), `arena_01.cgpr` (the
`death_tentacle_loader`), and the death-body entity scripts. The colossus cgpr carries
`deathfx_r_knee` and the `weak1_Spot`/`weak2_Spot`/`stomach`/`head` anchors.

**The binary.** `CreateDeathTentacle(ISYSObj*)` at `0x008424d0` (string at `0x0128ec66`,
`death_tentacle.cpp` at `0x0128ec0c`; one call site `0x0082d8c5`): a manager (`FUN_00676690`,
0x4e4 B), **201 segments**, `+0x10 = 10`, owner matrix from `*(obj+0x150)`, a callback
`FUN_008426d0`. The callback stores two 201×vec4 arrays (position/direction), loads
`death_tentacle.cgpr`, and uses a **0.25 m** segment spacing (`+0x18 = 0x3e800000`). The roots come
from a global point list (`&DAT_03241368`, count `DAT_032415c0`, stride 0x38), one tendril per
recorded hit point. The trigger is the **motion-change latch** `+0x52c` (set `0x637015`, cleared
`0x636ed8`), not HP directly; the HP→death-mode selection is script-side
(`Minotaur_A_death`). The corpse end state is `ACT_BOSS_DEAD_BODY` / `minotaur_a_dead_body.cgpr`;
`BossStatus_Alive_MinotaurA` / `BossStatue_Whole_MinotaurA` exist. Camera/fade calls
(`CMOLdrFadeToColor`, `FadeTransition`) exist but no basescript death-fade call was found.

**The app today.** The defeat is a single transition in `apply_combat_events`
(`app/src/app.cpp:1263–1279`): health ≤ 0 → `encounter.defeated = true`, the death clip starts
(`start_encounter_one_shot`, 929), and `ValusDefeat` is queued (mapped to null). The one-shot clock is
`advance_encounter_reaction` (945); at the clip's end it pins the last frame
(`reactionSeconds = duration`) with no phase after it. The camera has no defeat branch; the HUD keeps
the player bars and the zero-width Valus bar; the player stays fully controllable; the score stops
every defeated frame (idempotent). The renderer (`Renderer::render_frame`, `renderer.cpp:776`) has
one opaque mesh pass (depth write, no blend) and the screen-space HUD pass (alpha, no depth);
`Renderer::upload` is one-shot; `Scene`/`MaterialCpu` carry no alpha/translucent flag; `render_frame`
takes `const Scene&`. A newshader needs a CMake `LINK_DEPENDS`+copy entry (`app/CMakeLists.txt:22–31`),
and shaders compile at runtime from `SDL_GetBasePath()`.

**The audio.** The S5 audition (2026-10-22, `out/s6/audition/`) found `valus.death` =
`boss01`/`mavo_death` reqd 165 → tone 97 → wave 75, layers `[75,76,77]` (5.278 s, −13.6 dBFS, dark
collapse), identity high (manual chain), fidelity medium; `valus.club_swing` =
`boss01`/`boss01_mvt_swing_add` reqd 123 → tone 77 → wave 55, layers `[55,56,57]` (3.80 s, −17.4 dBFS,
bright whoosh). Both pass the S5 audition gate today with `method: "audition"`;
`walk_play_tone` returns `None` for their SEQD bytecode (no `0x4000` marker), so the rows must stay
audition or the walker needs a direct-scan extension. `player.fall` has no resident wave and stays
deferred.

## 7 · Decisions

| # | Decision | By | Why / cost if wrong |
|---|---|---|---|
| D1 | The tendrils are a **labeled reconstruction** with the original textures/emitters/segment geometry; the `.csdr` sim is not ported | Builder | The GCN evaluator bytecode has unnamed positional params; a faithful port is not feasible this pass. Cost: the motion is an authored approximation, not the game's exact curves |
| D2 | The death camera is **reconstructed**; the type-36 clips stay unused | Builder — flagged for veto | The clips' type 36 is undecoded; playing them is not possible. Cost: the camera is authored, not the game's exact cut |
| D3 | The death is a **pure phase module** (`death.h/.cpp`) mirroring `valus.h/.cpp` | Builder | Keeps the state machine headless/testable; the app wires it. Cost: a new module and tests |
| D4 | The effect pass is a **new blended ribbon/billboard pass** with per-frame dynamic geometry | Builder | The mesh pass is opaque-only and `upload` is one-shot; the tendrils need blend, depth-test-no-write, camera-facing quads and a ribbon. Cost: the first dynamic-buffer renderer work |
| D5 | The corpse swap is the already-converted dead-body glb + dirtmound, hidden entity | Builder | The asset is converted and the veins texture is bound; no new conversion needed. Cost: a static swap, no ragdoll |
| D6 | `valus.death` and `valus.club_swing` are **audition** rows in a new `tools/re/audio/s6_map.json`; the S5 map/manifest stay frozen | Builder | The S5 acceptance asserts its 8 entries; the S6 slice adds two. Cost: two maps to keep consistent |
| D7 | The player's input is locked during P1–P3; `R`/`M`/`Escape` remain; control returns in P4 | Builder | The cinematic needs the camera; the game locks control too. Cost: a lock window the builder tunes |
| D8 | The S6 acceptance runs the frozen S5 acceptance **first**, then the S6 converter (10 entries) and the S6 selftest | Builder | The S5 script's converter regenerates the 8-entry manifest; the S6 run then adds the two. Cost: a longer acceptance (~+10 min) |
| D9 | The corpse's veins layer is a **new emissive/veins texture slot** in `MaterialData`/`mesh.metal`; the converter maps the dead `.cmat` sixth slot into it (other materials use a null emissive) | Builder — flagged for veto | The app drops the slot today; without it the corpse loses the shipped vein look. Cost: a material/shader extension and a converter mapping (small, additive) |

## 8 · The design

### 8.1 The phase machine (`app/src/death.h/.cpp`)

```cpp
enum class DeathPhase { Idle, Motion, Tendrils, Collapse, Done };
struct DeathState {
    DeathPhase phase = DeathPhase::Idle;
    float t = 0.0f;            // seconds in the current phase
    float total = 0.0f;        // seconds since the defeat
    bool inputLocked = false;
    bool corpseVisible = false;
    int  roots = 0;            // 1–2 tendril roots
};
DeathState step_death(DeathState, float dt, bool defeated);
DeathState reset_death();
bool death_locks_input(const DeathState&);
```

- **Idle → Motion** at the defeat transition; `t` starts at 0; `inputLocked = true`;
  `total = 0`.
- **Motion** runs the existing one-shot death clip via the encounter clock; the tendril burst starts
  at `kDeathTendrilStart = 0.4 s` (builder call; the binary's motion-change latch is the evidence).
  Motion lasts the clip duration (6.233 s), then → Collapse.
- **Tendrils** overlaps Motion (the burst runs from 0.4 s to `kDeathTendrilEnd = 3.0 s`), the
  camera pull-back runs 0.4–3.0 s; the death cue fires at 0.4 s; the score stops at the defeat.
- **Collapse** at 6.233 s: hide the live entity, show the corpse; the camera settles; the phase lasts
  until `kDeathEnd = 8.0 s` (builder call).
- **Done** at 8.0 s: `inputLocked = false`; the corpse stays; the player can walk; `R` resets.
- `R` in any phase resets the encounter and the death state to Idle (the existing `reset_encounter`
  path plus `reset_death`).

The machine is pure; the app reads `inputLocked` to gate the player input and drives the camera/
renderer/audio from the phase. The tendril growth is a renderer/effect concern, not a state field.

### 8.2 The tendril reconstruction (`app/src/effect.h/.cpp`, `app/shaders/tendril.metal`)

- **Roots.** Two roots: the hit weakpoint (`weakpoint_03` forehead / `weakpoint_04` right arm) and
  the second weakpoint (the binary's one-tendril-per-hit-point list); the app passes the world
  positions from the existing weakpoint shells. If a hit point is unavailable, the weakpoint shell's
  centre is the root. The builder tunes the root jitter.
- **Ribbon.** Per root: 201 segments × 0.25 m (the binary's values), a triangle strip of 402 verts;
  the world positions are the root + a growing curl (a 3-D noise/curl path with a per-segment twist),
  the width from a 0.05→0.6 m envelope, the UV `v` along the ribbon and `u` across; the veins texture.
  The growth is 201 × 0.25 m over 1.2 s (builder call) with a per-segment stagger.
- **Billboards.** The `smoke` emitter: 24 camera-facing quads along the ribbon using the
  `2x2_smoke_tendrils_01` atlas (a 2×2 grid), soft alpha, 0.4–1.2 m, a 1.2 s life; the `main_wisp`
  emitter: 16 puffs at the root using the `8x8_wispysmoke02`/`8x8_smoke_wispy_sharp` atlases (8×8
  grid), a 0.6–2.0 m envelope; the `veins` emitter: the vein texture's glow on the ribbon (an
  additive pass over the same strip, a 0.3 alpha).
- **Blend.** Premultiplied alpha for the smoke (SRC_ONE, ONE_MINUS_SRC_ALPHA), additive for the
  wisp/veins (SRC_ALPHA, ONE); depth test LESS_EQUAL, depth write off; cull none; the ribbon and
  billboards drawn after the mesh pass, before the HUD.
- **Geometry path.** A new `EffectRenderer` with a persistent dynamic vertex buffer (SDL_GPU
  `TRANSFER_DST`, cycle the buffer per frame; ~ (2 roots × 402 + 40 billboards × 4) verts), a
  `tendril.metal` vertex/fragment pair, the three textures uploaded at load. `Renderer::render_frame`
  gains an `EffectFrame` (the roots, the phase time, the textures) and draws the pass between the
  mesh and HUD passes. The pass is a no-op in Idle/Motion before 0.4 s and after Done.
- **Determinism.** The curl/turbulence seed is fixed per root (the root index); the phase time is the
  scripted clock; the selftest stills are deterministic.

### 8.3 The corpse and the camera

- **Corpse.** At Collapse, the live entity model is hidden; the `c01_minotaur_a_dead_body.glb` (the 6
  meshes) and the dirtmound are placed at the entity transform (the builder tunes the yaw/offset from
  the deadbody cgpr). The dead materials' veins layer is carried through the new emissive slot (D9):
  the material loader sets the veins PNG for the dead-body materials and a null/black emissive for
  every other material; `mesh.metal` adds `emissive * texture` (or the veins as a second sample) with
  a small intensity. The corpse is static (no skin/anim); the entity's collision/weakpoint proxies are
  disabled; the HUD hides Valus's bar.
- **Camera.** A new `CameraDeath` path keyed on the sequence clock: T0 the current follow camera;
  T0.4 a 12 m pull-back/4 m up; T3.0 a low angle on the tendrils; T6.2 a wide on the corpse; T8.0
  back to the follow camera behind the player. The interpolation is eased; the path is in world
  space relative to the Valus transform (so a reset/replay is deterministic). The player's input is
  locked; the camera does not capture the mouse.

### 8.4 The audio (`tools/re/audio/s6_map.json`, `app/src/audio_map.h`)

- `s6_map.json` = the S5 `cues` (8) + `valus.death` (`boss01`/`mavo_death`, wave 75, layers
  `[75,76,77]`, method `audition`, confidence high, fidelity medium) + `valus.club_swing`
  (`boss01`/`boss01_mvt_swing_add`, wave 55, layers `[55,56,57]`, audition, high, medium); the
  `deferred` array loses `valus.death`, keeps the other seven, and the note records `player.fall`/
  ambience.
- The converter (`bp-convert audio --map tools/re/audio/s6_map.json`) writes 10 entries to
  `out/library/audio/audio.manifest`; the WAVs are the S5 converter's float32 48 kHz.
- The app: `Sfx::ClubSwing` (new) → `valus.club_swing`; `Sfx::ValusDefeat` → `valus.death`; the
  `audio_map.h` table gains the two; the `audio_events.h` queue; the audition's core-event check
  derives from the map's cues (excluding `music.boss`), so the S5 map stays valid and the S6 map
  requires all nine; the S5 `s5_map.json` stays frozen.
- The death cue fires at the tendril burst (0.4 s); the whoosh at the club swing's start (the
  `ClubStart` telegraph keeps `valus.telegraph`); the music stops at the defeat; `M` mutes the
  cinematic.

### 8.5 The proof (`--s6-selftest`, `scripts/s6-acceptance.sh`)

- `--s6-selftest --s6-stills out/s6/stills` drives a **scripted lethal event** on the current
  encounter (the s42 timeline's Valus at 20 HP, or a fresh S6 timeline reaching 0) with audio on, then
  the death phases; the three-strike real defeat is already proven by the frozen S4.1 acceptance, and
  S6 proves the death path from the same `apply_combat_events` transition. It prints `s6-phase` lines
  (the phase, the time, the input lock, the corpse, the roots), `s6-event` lines (the death cue, the
  whoosh, the music stop), `s6-death` status (the health 0, the corpse, the camera key), and a final
  `s6-status`.
- Stills: the death motion (`death-motion.png`), the tendril burst (`tendrils.png`), the corpse
  (`corpse.png`), the cinematic camera (`camera.png`); each is a 2560×1440 still with a `span`/size
  check; the tendril still asserts the effect pixels inside the wound bbox (a colour/alpha count),
  the corpse still asserts the dead-body silhouette, the camera still asserts the pull-back framing.
- `scripts/s6-acceptance.sh`: build + publish + both suites; the **frozen S5 acceptance** (which runs
  S4.2→S4.1→S4→S3, 8 entries); the S6 converter (10 entries); the `--s6-selftest`; the event/phase/
  still/pixel assertions; the perf (the effect pass's marginal, the 240-frame run); `PASS —`.
- The frozen scripts are never edited; the S6 script invokes them in order; no two acceptances run
  concurrently; all viewer runs under `caffeinate -dimsu`.

## 9 · The builder's calls (left open, with defaults)

- The tendril burst start (`0.4 s`), the growth time (`1.2 s`), the end (`3.0 s`), the cinematic end
  (`8.0 s`) — tune against the death clip.
- The ribbon width/curl/twist, the billboard counts (24/16), the atlas UVs, the blend weights.
- The camera path keys and easing; the corpse yaw/offset; the root jitter.
- The club-whoosh hook: `Sfx::ClubSwing` at the club attack's swing phase (the
  `advance_valus_attack` swing, not the telegraph; default: the impact time minus the cue's attack
  lead), tuned against the club clip; the user's ear is the check.
- The `s6` still composition and the pixel thresholds.
- Whether to extend `walk_play_tone` with the direct SEQD scan (then the two audio rows become
  `ghidra`); default: stay `audition` (the S5 gate passes today).

## 10 · Risks

| Risk | Severity | Mitigation |
|---|---|---|
| The effect pass's blend/depth ordering looks wrong over the mesh | medium | The pass draws after the opaque mesh with depth-test/no-write; a still + a pixel check |
| The 201×2 ribbon + 40 billboards cost | medium | The 240-frame perf gate; a single dynamic buffer; cull when Idle/Done |
| The roots are wrong (weakpoint vs hit points) | medium | The binary's hit-point list; the weakpoint shells as the fallback; a still check |
| The corpse's materials/orientation look wrong | medium | The dead materials + veins texture; the deadbody cgpr; a still |
| The camera clips cannot be played (type 36) | low | The reconstruction is documented; the camera still |
| The S5 manifest (8) vs S6 (10) | low | The S5 acceptance first, then the S6 converter; both maps |
| The audio gate rejects the new rows | low | The audition rows pass today; the gate's `CORE_EVENTS` add |
| The frozen S4.1/S4.2/S5 numbers change | high | New `s6-*` lines only; the frozen scripts invoked unchanged; the regressions |

## 11 · The walk (the user's ear/eye, PENDING after implementation)

- Defeat Valus: the death motion, the black tendrils from the wound, the collapse into the corpse,
  the camera pull-back, the death roar, the club whoosh, the music stop; the control returns; `R`
  resets.
- The tendrils look like the game's silhouette/colour (the original textures) and erupt from the hit
  weakpoint; the corpse is the dead body, not the live rig frozen.
- No clipping, the window stays and the cursor is free, the fps holds; `M` mutes the cinematic.
- Deferred/known: the exact `.csdr` particle motion, the type-36 camera clips, ragdoll, the other
  colossi.

## 12 · Acceptance

`controls`/windowed/cursor unchanged; the S5/S4.2/S4.1/S4/S3 scripts green; `scripts/s6-acceptance.sh`
green with the stills and the audio; the results note quotes measured values; assets not committed;
the S5 listening walk stays the user's (already recorded); the S6 walk is added.
