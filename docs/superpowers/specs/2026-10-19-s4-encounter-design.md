# S4.0 — "The encounter": Valus in the arena (design)

**Date:** 2026-10-19 · **Status:** design (pre-plan) · **Recon:** `docs/notes/s4-recon.md`

## 1 · Intent

The arena is alive but empty. This milestone puts its colossus in it: **Valus (colossus #1)**
stands in the Valus arena — converted from his own `.cmsh`/`.cskl`, alive with his decoded
`minotaur_a_stand` clip — and you can walk up to him.

It is the first slice of the SDL design's **S4 "The slice"** row ("first colossus defeatable",
`2026-10-16-sdl-rebuild-design.md:132`). That row is months of combat work; this design **re-ladders
it**: **S4.0 "The encounter"** (this milestone: presence, placement, approach), **S4.1 "The climb"**
(collision/climb/grab), **S4.2 "The fight"** (weak points, stamina, defeat). Nothing in S4.0 claims
a fight; the moment it delivers is the one before it — *he is there, and he is moving.*

## 2 · Goals (the done-line)

1. **Valus carries his own clip.** `c01_minotaur_a.glb` is re-converted with
   `minotaur_a_stand` (26 frames, 58 tracks) through the existing S2 `--clips` path; track→joint
   mapping is 58/58 against the 238-joint rig (`minotaur_a.skb`'s 58 records; 73 bank names all
   present in the glb). The pose is validated by the RE tool's minotaur xval + a writer world-pose
   sanity test — the S2 column-vector convention applies unchanged.
2. **Two independent rigs in the arena.** Wander (the player, 348 joints) and Valus (an entity,
   238 joints) each keep their own skeleton, clips, palette, and animation state. The player's
   locomotion and the entity's idle run simultaneously; neither deforms the other. The existing
   S3 manifest keeps working unchanged.
3. **Placement has provenance.** The encounter manifest places Valus at the game's own transform
   if a **bounded data-mining pull** finds the MinA spawn record; otherwise at a **labeled builder
   pick** on the bowl floor. The manifest header and the results note say which — a builder pick is
   never presented as data. A data record is accepted only with a type/name match **and** a
   positional cross-check (inside the bowl, consistent with the player start); x/z/yaw are used,
   y is ground-sampled (the app's feet-on-floor rule). The player spawn uses the **data-derived
   position** (`misc/playerstartpositions.txt`: `353 29 345`), ground-sampled, with a
   **builder-picked yaw** facing Valus — labeled separately.
4. **You can approach him.** A minimal collision proxy (player capsule vs Valus's posed leg/torso
   capsules) stops Wander walking through him; the arena's ground-follow and edge clamp are
   unchanged. No climbing, no grab.
5. **The camera can hold him.** A target toggle frames the full 17.7 m body (target = chest joint,
   widened distance/pitch); the normal follow camera is unchanged otherwise. The deterministic
   capture path can select the same framing.
6. **Acceptance.** A runnable artifact plus `scripts/s4-acceptance.sh`, stills (wide, approach,
   low-angle), a measured perf/load line, `docs/notes/s4-results.md`, and the user's walk sign-off.

## 3 · Non-goals (this pass)

- Colossus behavior/AI, patrol, attacks (club/stomp/peek), weak points, damage, death — S4.2.
- Climbing, grab, stamina, riding Valus's fur/banner — S4.1.
- The **club** and **knee** meshes (separate unskinned glbs; the club bone's hierarchy/attachment
  is open) — deferred to the attack work and labeled in the results.
- Shell fur, banner alpha/cloth simulation (the banner renders opaque/single-sided), eyes glow
  effects — labeled fidelity gaps.
- Event/encounter cameras: camera clip playback and camera types 36/37 remain undecoded; the
  camera toggle is an app-side aid, not the game's `st17` camera.
- Gamepad (arrives with S4.2's combat), on-screen HUD (the title-only fps readout stays), health.
- Multi-colossus, the horse in the encounter, wide-map traversal/streaming, root-motion locomotion.

## 4 · Anti-goals (a failure even if it runs)

- A Valus that is the wrong pose, wrong scale, or floating/sunk: the feet must sit on the bowl
  floor, and the idle must read as breathing, not as a broken rig.
- A second rig that borrows Wander's palette (deformed) or destabilizes the player path.
- A placement presented as data-derived when it is a builder pick.
- Regressing S3: the S3 manifest, script, and user walk stay valid and green.
- Clipping through Valus — presence is the point.
- A static statue passed off as "alive": the decoded idle must play.
- Docs that disagree (design/plan/results).

## 5 · Constraints (inherited, fixed)

- **SDL**, windowed; **the cursor is never captured** (user).
- Offline-first: the app consumes converted glbs + manifests only; no original containers at
  runtime. All decode in the converter.
- **Static-only RE**: no game/port runs for RE; the dev-only port is not used in this milestone
  (no Valus capture exists; the Shrine capture set does not cover the arena).
- Personal use, no redistribution; game assets and derived glbs stay uncommitted.
- Apple Silicon; .NET 10 converter; C++20/SDL3 app; ≤4 concurrent agents; typed commits; tests
  fail fast without `SOTC_GAME_ROOT`; bash 3.2 scripts.
- The encounter is a **separate manifest + acceptance path**; S3's manifest and script are not
  modified.

## 6 · Recon facts (2026-10-19; sources in `docs/notes/s4-recon.md`)

- **Asset.** `models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh`
  (6.74 MB) converts to a **38.0 MB glb**: 12 meshes, 523,865 vertices, 1 skin / **238 joints**,
  bind AABB **34.96 × 17.67 × 9.81 m**, lowest vertex **y = −8.739**. LOD1 1.76 MB; collision glb
  229 meshes / 5,329 verts; club (`_weapon`) and knee (`_hiza`) are separate **unskinned** glbs.
- **Animation.** All **135** `minotaur_*` clips are **flag-clear** and parse (0 errors); 82 are
  `minotaur_a`. `minotaur_a.skb` = 73 names (58 record-bearing + 15 `OptBone_*`); every `minotaur_a_*`
  clip has 58 tracks and all resolve into the glb (0 unresolved). `minotaur_a_stand` = 26 frames at
  ~15 fps, types `0x00/03/04/05/06/0D`. Stand/walk `check-curves` = 16/16. **No pose validation for
  the minotaur rig exists yet** (the S2 xval validated `player_a_stand` only), and no Valus capture
  exists.
- **Placement.** No colossus spawn record in the `.fs`/`.cwks` families; the arena scripts
  (`arena_01.cgpr`, `arena_01_shared.cgpr`) reference the MinA relay/spawn events and the body
  script, but `.cgpr`/`.csml` semantics are undecoded; `minotaur.path`'s coordinate frame is
  unresolved; `camdata` carries camera parameters, not transforms. The only verified transform is
  the player start above (ground 27.95 m at that column).
- **App.** The scene holds exactly **one** skin/clip list/palette — the player's. A skinned glb as a
  manifest instance is loaded rig-skipped but its weights are still copied, so it would deform with
  Wander's palette. The renderer binds one palette; the follow camera clamps distance 2–12 m and
  pitch ±60°; the heightmap is built from every instance mesh except the player; there is no
  collision beyond ground-follow + edge clamp; no gamepad, no on-screen HUD.
- **Tooling.** Headless Ghidra 12.1.4 with the analyzed project is available; Ghidra-MCP is blocked
  by an unrelated instance; REA/Hopper is shallow in the sampler region. Static only.

## 7 · The how (the decided shape)

### 7.1 Converter — Valus's clip

- Re-convert the colossus with `--clips minotaur_a_stand` and `--force` (single-file `--under`), the
  S2 `--clips` contract (every track must map; unmapped fails loudly).
- Extend the tests to the minotaur bank: `SkbReader` 73 names / 58 records, `TrackMap` 58/58 against
  the 238-joint glb, the stand clip's shape (26 frames, 58 tracks, statics included), and the
  writer's world-pose sanity (no NaN; feet lowest; head highest; root at the clip's ground).
- **Pose validation** uses the existing RE tool: point `pose_dump.py` at the colossus glb and the
  minotaur clips (bind vs bridged residual band + a rendered still). This is the same bridge that
  was proven on Wander; the milestone's risk is the *mapping*, not the convention, and the mapping
  is measured 58/58.

### 7.2 App — two rigs

- **Scene records.** Each loaded glb keeps its own rig: `SkinCpu`, clip list, mesh range, placement,
  and a dynamic flag. The player is rig 0; the entity is a second rig. Static arena meshes carry no
  rig.
- **Manifest.** One new directive for a placed, animated entity — `entity <glbRelPath> <transform…>
  <clipName>` (exact token order pinned by the plan). Unknown directives still fail loudly; existing
  `instance`/`spawn` files parse unchanged.
- **Renderer.** One palette buffer per rig, bound per mesh by the mesh's rig (the shader is unchanged;
  unskinned vertices already pass through). The player's upload path stays rig 0.
- **Animation.** One `Playback` per rig: the player keeps S2 locomotion; the entity loops its baked
  clip. `--anim`/`--anim-time` keep their global override semantics for single-model captures.
- **Grounding.** The entity's feet-on-floor rule mirrors the player's: a clip with a root
  translation carries the ground height; otherwise the origin-to-feet lift (8.739 m) applies.
  Placement validity = the lowest posed vertex at the bowl floor.
- **Collision.** A small module: player capsule vs the entity's posed leg/torso bone capsules
  (`Bone_SUNE_*`, `Bone_MOMO_*`, `Bone_TOE_*`, torso chain), horizontal push-out, resolved after
  `player_update`; no vertical stepping, no physics loop. Walking between the legs is allowed where
  the gap exists.
- **Heightmap.** Built from static (rig-less) instances only — never from a rigged entity or the
  player. The encounter spawn is ground-sampled.
- **Camera.** `T` toggles a **camera-only** framing mode: target the entity's chest joint, widen
  distance (builder-clamped) and pitch so the full body fits. It is not a lock-on and not a control
  change — movement stays relative to the **active camera's yaw** (the same rule as today), the
  player may walk out of frame, and `T` restores the follow state without a snap. The capture path
  selects the same framing deterministically.

### 7.3 Placement — bounded pull, labeled fallback

- A **timeboxed** data-mining pull (one workstream): find the MinA spawn transform in the arena's
  `.cgpr`/`.csml` (or another stage family), using the vendored CGPR structure parser and/or a
  targeted probe; deliver `docs/notes/re4-placement.md` with either the record and its field layout
  or a documented negative.
- **Acceptance bar for a data record** (pinned): it must (a) be named/typed as the MinA
  spawn/placement (not a sensor, relay, camera, or prop), (b) resolve to a point inside the arena
  bowl and within a plausible distance of the data-derived player start, and (c) carry x/z + yaw
  without requiring an unknown frame origin. A float triple that only *looks* like a spawn is a
  negative, not a data hit. The timebox ends at a documented positive or negative; it never spins.
- **Placement math** (pinned): x/z/yaw from the record; **y is ground-sampled** at that column (the
  feet-on-floor rule); the model's origin-to-feet lift (8.739 m, or the clip's root series) applies
  on top. The record's own y is recorded in the note but not used as the model origin.
- **Provenance values** (pinned): `data` (x/z/yaw from the record, y ground-sampled),
  `builder-pick` (x/z/yaw chosen), and for the spawn `data-position` + `builder-yaw`. The manifest
  header and `s4-status` carry the value; the results note explains it.
- **If the data transform and the encounter staging disagree** (out of the bowl, too far to see
  from the spawn): the positional cross-check rejects it → builder pick. Data never wins over
  "inside the arena, visible from the spawn".
- **If not found:** a **labeled builder pick** on the bowl floor — feet on the ground, facing the
  player's data-derived spawn — recorded in the manifest header as `builder-pick`. `minotaur.path`
  is not used without an anchor for its frame.
- **Manifest source** (pinned): the encounter manifest is **derived from the S3 composition** —
  one source for the 30 instances, no duplicated list — by a composer step that rewrites the
  `spawn`, appends the `entity`, and writes a **new provenance header** (S3's header is not
  inherited; its spawn is no longer the file's). If the S3 manifest is absent, the S4 script
  regenerates it with the documented command (`docs/notes/re-stage.md` §3.4) before deriving.
- The pull never blocks the milestone: the fallback is part of the design, and the user's walk
  judges whether the spot reads right.

### 7.4 Acceptance

- `scripts/s4-acceptance.sh` (new; S3's script untouched): build, both suites, encounter self-test
  (`s4-status` with instance/entity counts, placement + spawn provenance, feet-on-floor, camera
  framing), stills (wide / approach / low-angle), a pixel sanity check that Valus is actually in
  frame, and measured load + marginal frame cost (≥60 fps class).
- `docs/notes/s4-results.md`: evidence, provenance, pose validation, performance, known gaps
  (club/knee, fur, banner alpha), and the user's walk checklist.
- **The user's walk:** spawn in the bowl, Valus visible and idling ahead; walk to him; the collision
  stops you at his legs; `T` frames the full body; fps ≥60 class; cursor free; Escape quits. The
  verdict is the user's.

## 8 · Risks

| Risk | Level | Mitigation |
|---|---|---|
| Placement pull finds nothing | medium (does not block) | labeled builder pick by design; provenance recorded; user signs off on the spot |
| Minotaur pose mapping wrong (58 tracks) | medium | measured 58/58; RE xval residual band + writer world-pose sanity; the user's walk is the final eye |
| Second-rig regressions (palette/loader) | medium | per-rig palette buffers; tests for two simultaneous rigs; S3 acceptance stays green |
| Scale/grounding (origin −8.739, root series) | medium | mirror the S2 grounding rule; assert feet-on-floor in `s4-status` and stills |
| Performance/load (+523k verts, 38 MB) | low–medium | measure; **full-res is required** — LOD1 is an untextured 1-material mesh, a measurement control, not a shippable fallback; if the class misses, optimize the draw (frustum culling) rather than downgrade |
| Fidelity gaps (club, fur, banner alpha) | low (labeled) | explicit non-goals; noted in results; the club/knee arrive with the attack work |
| Collision feel (push-out, tunneling) | low–medium | capsules from the posed palette, capped dt, unit tests; no physics engine |

## 9 · Left to the builder

- The directive's exact name/token order and the manifest's provenance header format.
- Per-rig palette mechanism (separate buffers vs a `jointBase` uniform).
- Collision capsule dimensions and the leg/torso bone set; the camera toggle's key and clamps.
- The placement probe's method and timebox; the exact builder-pick coordinates/yaw.
- Manifest/tool naming, still times, test fixtures.

## 10 · Decisions recorded

| Decision | Who | Date |
|---|---|---|
| S4 splits into S4.0 encounter / S4.1 climb / S4.2 fight; this milestone is S4.0 | builder call (flagged for veto) | 2026-10-19 |
| Valus is animated (idle), not a static instance | builder call (flagged for veto) | 2026-10-19 |
| A bounded placement pull first; labeled builder pick as fallback | builder call (flagged for veto) | 2026-10-19 |
| Club/knee attachments deferred to the attack work and labeled | builder call (flagged for veto) | 2026-10-19 |
| Gamepad deferred to S4.2; keyboard/mouse for this milestone | builder call (flagged for veto) | 2026-10-19 |
| SDL, windowed, cursor never captured; offline-first; static-only RE | user (inherited) | 2026-10-16 |

## 11 · Builder-check resolutions (2026-10-19)

A cold-read planner raised five questions; all five are resolved here as builder calls (flagged for
veto) — no user round was needed (the user opted out of the question pass):

1. **Placement bar / conflict** → a data record needs a type/name match **and** a positional
   cross-check; x/z/yaw are used, y is ground-sampled; an out-of-bowl or too-far record is rejected
   → builder pick. Provenance: `data` / `builder-pick`; spawn `data-position` + `builder-yaw`
   (§7.3).
2. **Manifest source** → derived from the S3 composition by a composer step (one source for the 30
   instances); S3's manifest is regenerated if absent; the new header carries S4's provenance, not
   S3's (§7.3).
3. **LOD1** → not shippable (untextured, 1 material); **full-res is required**; LOD1 is a
   measurement control only; a perf miss is fixed by culling, not by downgrading (§8).
4. **Camera contract** → a **camera-only overlay**, not a lock-on and not a control change; movement
   stays relative to the active camera's yaw; the player may leave the frame; `T` restores the
   follow state without a snap (§7.2).
5. **Spawn yaw** → builder-picked to face Valus, labeled separately from the data-derived position;
   `s4-status` reports both (§2, §7.3).

*Personal-use fan project. No game assets are distributed from this repository.*
