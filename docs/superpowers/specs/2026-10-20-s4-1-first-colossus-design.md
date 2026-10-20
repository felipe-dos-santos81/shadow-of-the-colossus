# S4.1 — "The first colossus": grab, climb, strike, defeat (design)

**Date:** 2026-10-20 · **Status:** design (pre-plan) · **Completes:** the SDL design's S4 row
(`2026-10-16-sdl-rebuild-design.md:132`) · **Supersedes:** the S4.1/S4.2 split recorded in
`2026-10-19-s4-encounter-design.md:216` (merged by the user, 2026-10-20)

## 1 · Intent

S4.0 put Valus in the arena and let you walk up to him. He is present but inert: you can look at
him, not act on him. This milestone makes the encounter something you **do** — grab his fur, climb
him, spend stamina to stay on, reach a weakpoint, strike it, get shaken off, re-grip, and bring him
down.

It completes the S4 done-line — *"first colossus defeatable: controller, climb, stamina, grab +
encounter; reference-tuned"* — and it is the moment the project exists for: not a diorama of SOTC,
but SOTC doing what SOTC does.

The S4.0 design split that row into S4.1 "the climb" and S4.2 "the fight". **The user merged them**
(2026-10-20): a climb with no consequence does not deliver the moment, and a fight you cannot reach
is not a fight. This design replaces both.

Two facts make it cheaper than it looks. The RE decode gate is **MET**, so more clips are a
converter invocation, not research. And the game ships **real Wander grip/attack/damage clips** —
so both sides of the encounter can be decoded animation rather than invented poses.

## 2 · Goals (the done-line)

1. **Grab and release.** Standing at Valus's body and holding `E` attaches Wander to the nearest
   climband; releasing `E` detaches. The grab is a commitment: it persists while the key is held
   and stamina remains, and the colossus can break it.
2. **Climb.** While attached, `W/S` move up/down the band and `A/D` move around it, on Wander's
   baked grip/loop/move clips. Attachment is to the **posed skeleton**, so the climb follows the
   idle and every reaction. The player is drawn on the surface, oriented to it, never inside it.
3. **Stamina is the clock.** A pool drains while gripping (more while climbing, most while
   striking), regenerates on the ground, and empties into a forced fall. The HUD shows it.
4. **Weakpoints you must earn.** Two weakpoints — the forehead sigil and the right upper arm —
   derived from the game's own `weakpoint_03` / `weakpoint_04` material shells and their bone
   weights. Both are reachable only by climbing; neither can be struck from the ground.
5. **Strike and reaction.** At a weakpoint, holding `J` charges and releasing plunges, on Wander's
   baked attack clip. A landed hit damages Valus, plays a decoded reaction clip, and **shakes the
   player off** unless they re-grip in time. Damage has a declared model (§7.3), not a magic
   number.
6. **Defeat.** Valus reaches zero, plays a decoded death clip, ends inert, and the app prints and
   shows a victory state. The loop is completable in one sitting.
7. **A real HUD.** Stamina and Valus's remaining health, drawn with SDL_ttf and uploaded to an
   SDL_GPU texture. No menus, no text beyond these.
8. **Gamepad.** The same actions on a standard SDL3 gamepad (left stick, right stick camera,
   RT grip, X strike, Start pause), hot-plugged. Keyboard + mouse stay first-class.
9. **Acceptance.** A runnable artifact plus `scripts/s41-acceptance.sh`, deterministic selftest
   lines, stills, measured load/perf, `docs/notes/s41-results.md`, and the user's walk sign-off.
   S3 and S4.0 stay green.

## 3 · Non-goals (this pass)

- **Autonomous behavior**: pursuit, patrol, club/stomp attacks, encounter scripting, aggro. Valus
  reacts; he does not hunt. Attacks are the next milestone and the club arrives with them.
- **Player health, damage, death** — the failure state is losing the grip, not dying.
- Cloth/banner simulation, shell fur, eye glow, weakpoint decals beyond what the meshes provide.
- Event/encounter cameras (types 36/37 remain undecoded); the camera work is app-side.
- Other colossi, Agro in the encounter, wide-map traversal/streaming.
- Root-motion locomotion, animation blending beyond the single grip/idle cross-fade in §7.2.
- Audio, menus, save, subtitles.

## 4 · Anti-goals (a failure even if it runs)

- **A climb that is free-fly with extra steps**: unattached to the body, free of cost, unbreakable.
- **A weakpoint reachable from the ground**, or a hit Valus does not visibly answer.
- **A stamina bar that never empties** — the clock must force a decision.
- A Valus who holds the same pose through a stab, or a death that is a clip cut to black.
- Regressing S3 or S4.0: their manifests, scripts, selftests, and walks stay valid and green.
- Provenance fraud: a builder-picked weakpoint or stamina constant presented as decoded.
- Clipping: the player inside the mesh, the camera inside the colossus.
- Docs that disagree (design / plan / results).
- Dropping below the ≥60 fps class on the reference machine.

## 5 · Constraints (inherited, fixed)

- **SDL3 + SDL_GPU/Metal**, C++20, Apple Silicon; **windowed**; **the cursor is never captured**
  (user, 2026-10-16). `SDL_HINT_MOUSE_AUTO_CAPTURE=0`, no relative mode, no grab — pinned by
  `windowed_selftest`.
- Offline-first: the app consumes converted glbs, manifests, and a small sidecar at most; no
  original containers at runtime.
- **Static-only RE**: no game or port runs for RE.
- Personal use, no redistribution; assets and derived glbs stay uncommitted.
- .NET 10 converter; tests fail fast without `SOTC_GAME_ROOT` / `SOTC_LIB`; bash 3.2 scripts; typed
  commits; **≤4 concurrent agents**.
- The encounter stays a **separate manifest + acceptance path**; S3's and S4.0's manifests and
  scripts are not modified.
- Tools: Context7 for SDL3/SDL_ttf API facts, Ghidra + Ghidra-MCP and REA for the decode work.

## 6 · Recon facts (2026-10-20)

**Valus (verified).** `c01_minotaur_a.glb`: 12 meshes, 1 skin / **238 joints**, 476,666 composed
vertices, one baked clip (`minotaur_a_stand`, 26 frames, 58 rotation tracks + `Bone_root`
translation). Bind AABB 34.96 × 17.67 × 9.81 m; origin-to-feet 8.739 m; placed at
`(382, 28.369, 370)`, yaw −130.763605° (builder pick). Club (`_weapon`) and knee (`_hiza`) are
separate **unskinned** glbs. Eight hard-coded collision capsules span the legs and torso
(`app/src/collision.cpp:116-125`).

**Clips (verified).** 135 `minotaur_*` clips parse (0 errors), 82 `minotaur_a`, every one 58 tracks
resolving 58/58, including 28 `minotaur_a_add_*_damage` reactions keyed by body part. Wander's bank
ships **grip, attack, damage, and death** clips by name — `player_a_grip_stand_crouch`,
`player_a_grip_loop`, `player_a_grip_move_f1/f2`, `player_a_grip_float_u_hi_attack`,
`player_a_atk1`, `player_a_damage_m_fly2`, `player_a_fall_damage_to_death` (86 grip/climb clips in
all). Only the three locomotion clips are baked today.

**Weakpoints (verified).** Valus's weakpoints exist as named material shells, not bones: Mesh_8 /
`weakpoint_03` on the forehead (730 verts, `Bone_KAO` 98.4 %), and Mesh_4 / `weakpoint_04` on the
right upper arm (204 verts, `Bone_JOWA_R` 87 % / `Bone_UDE_R` 13 %). A third named shell,
`trigger_02`, sits on the left shin (123 verts, `Bone_SUNE_L` 66 %); the bank also holds
`minotaur_a_add_leg_l_weakpoint_damage`, so whether it is a third weakpoint or a climb trigger is
**open** — this design treats it as a trigger and records the ambiguity. No hurt volumes exist: the
shells are surfaces, so the strike proxy is invented (§7.3).

**Grip surface (verified).** The fur is a shader shell, not a mesh, but the rig carries the joints a
climber would use: `Bone_Thigh_hbar_01…04_L/R`, `Bone_Spine_ring_01…03_*`, `Bone_KOSI_ring_*`, and
the `Bone_*_hbar_*` bars — named, weighted, and animated by the idle.

**Arena (verified).** 30 static instances (931,601 vertices, 128 materials, 163 textures) composed
from `out/re/stage/encounter.manifest`; ground is a 1 m max-Y heightmap built from static meshes
only; load **25.649 s**; marginal frame **8.95 ms** (~111.7 fps) on the M3 Max.

**App (verified).** Player 348 joints; third-person follow camera (2–12 m, pitch ±60°) plus a `T`
target camera (22–30 m); collision is ground-follow + Valus's capsules; `Capsule` has **no
identity**, so "the limb I am holding" is currently unrepresentable; no HUD, no gamepad, no
SDL_ttf linked; V-Sync paced; interactive `dt` is wall-clock; acceptance takes ~7.5 min.

**Open at design time — resolved by the design, not by hope:**

- *Reaction and death clips.* Which `minotaur_a_*` clips read as hit/death is settled by baking and
  eyeballing stills; the chosen names go in the clip manifest. A staged-pose fallback is labeled,
  never presented as animation.
- *Stamina/HP constants.* Shipped values may not be recoverable cheaply; §7.3 pins provenance
  either way.

## 7 · The how (the decided shape)

### 7.1 Converter — the clips and the data

- **Valus**: re-convert with `--clips` carrying idle plus the body-part damage reactions that match
  the weakpoints (`minotaur_a_add_head_damage`, `minotaur_a_add_ude_r_damage`,
  `minotaur_a_add_mune_r_damage`, …) and a death clip. **Wander**: re-convert with the eight
  grip/attack/damage clips above. The existing contract holds — every track must map, unmapped fails loudly — and the
  S2 column-vector convention is unchanged.
- Add a **clip manifest** next to each glb (converter output, not hand-written): clip name, frame
  count, fps, track count, and a one-line description. The app loads clips by name from this list;
  nothing hard-codes a frame count.
- Root-motion check: any baked clip whose `Bone_root` translation moves the body off the attach
  point is either neutralized for climb use or excluded — recorded in the clip manifest, not
  discovered at runtime.
- **Weakpoint binding — closed negative (2026-10-20).** The `.cgpr` pull came back empty: Valus's
  entity script carries `weak1_Spot` and `weak2_Spot`, but the two records are byte-identical except
  for the GUID, share one transform `(0.423, 0, 14.337)`, and there is no third record. They cannot
  bind the forehead and arm shells. The `derived-mesh` path stands and no time is spent here.
  `.cchr` is also out: it is a name→ID dictionary (85 motion + 45 sound/effect entries) with no
  numeric parameters, so it holds no HP or stamina.
- **Stamina/HP pull (bounded, same workstream).** The eboot has the vocabulary and the addresses
  (`WanderHealthBarMax` `0x010183cd` → xref `0x54ed85`; `WanderGripBarMax` `0x010183f4` → xref
  `0x54ebb5`; stat registration `0x551bb1–0x551c56`), but the only numbers seen so far are debug-menu
  bounds (100/570 HP, 100/700 grip), not gameplay values. Timebox 0.5 agent-day: a confirmed base
  value replaces §7.3's, or the pull records a negative and the builder-tuned values stand.

### 7.2 App — the climb

**Player state machine** (one enum, one update function, replacing the implicit "grounded or not"):

`Grounded → Grabbing → Climbing → Striking → Staggered/Falling → Grounded`

Every transition is explicit and unit-tested; `player_step` replaces the four scattered call
sites in the interactive path only. The S3/S4 scripted call sites stay byte-identical so their
numbers cannot move.

**Attachment.** A climband is a **bone pair** (e.g. `Bone_SUNE_L`→`Bone_MOMO_L`) plus a normalized
parameter `u ∈ [0,1]` and an angle around it. Each frame the attach point is recomputed from **this
frame's palette** — `axis = lerp(boneA, boneB, u)`, `position = axis + normal × (radius + 0.35)` —
so the player rides the idle and every reaction. Bands are declared in one table with radii
measured from the posed mesh, preferring the rig's own fur joints (`Bone_Thigh_hbar_*`,
`Bone_*_ring_*`) over invented anchors. Two routes exist: **leg → thigh fur → waist → chest →
neck → head** (forehead weakpoint) and **waist → chest → shoulder → upper arm, approached from
behind** (arm weakpoint). The right arm is posed differently from the left, so the table is
asymmetric by design.

**Movement.** `W/S` change `u` at ~1.2 m/s in bone space; `A/D` change the angle. At a band end the
adjacent band is entered via a hand-authored adjacency table (five entries, not a mesh walk). The
player faces the surface. Clips: `player_a_grip_stand_crouch` on grab, `player_a_grip_loop` at
rest, `player_a_grip_move_f1` while moving, `player_a_grip_float_u_hi_attack` on strike,
`player_a_damage_m_fly2` on a forced fall.

**Grab rules.** Requires: not falling, a band surface within 0.9 m of the player capsule, and
stamina > 0. Grab is **held**, not toggled. Release is immediate; stamina regenerates on the ground
after a 1 s delay. The S4 walk already stops the player at 2.05 m from the foot axis — i.e. touching
it — so the grab point is guaranteed reachable from the approach.

**Stamina.** Pool **0–100** (normalized for the HUD). Drains: hanging 2.5/s, climbing 4.0/s,
charging 8.0/s. Regen on the ground 25/s after a 1 s delay. One bar covers the canonical route with
margin: foot → forehead at 1.2 m/s is ~13.2 s ≈ 53 units, plus two full strikes (~8 each) leaves
~30. Empty → forced release, `Falling`, land, short `Staggered`. Constants live in one block the
selftest can override; these are the **design values**, and acceptance records the measured margin
at the sigil.

**Strike and grip break.** Only while attached and within the weakpoint's reach. Hold `J` to charge
over 0.9 s; early release is weak, full charge is a plunge. One damage event per swing, in a pinned
window (0.25–0.45 of the attack clip). A hit starts the reaction and **detaches the player into
`Falling`**; a 0.6 s catch window opens, in which a **fresh `E` press** (release, then press) catches
the nearest band within 2.5 m at its nearest `u`. No catch → the ground, `Staggered`, climb again.

**Collision while attached.** Today `resolve_player_collision` would eject the climber (the held
capsule pushes to exactly the surface distance) and the heightmap overload would snap him to the
floor. The resolver gains **capsule identity** (`rig`, `segment`); attached modes exclude the held
segment ±1 and never call the ground-write path.

**Camera.** New `CameraClimb`: target = attach point, distance 5–8 m seeded **outward** on grab,
pitch ±45°, **user-orbited** by drag / right stick, `F` recenters outward. No obstruction test
(labeled). The follow and `T` cameras are untouched.

**HUD.** SDL_ttf → `TTF_RenderText_Blended` → surface → `SDL_UploadToGPUTexture` → one quad pass.
Stamina bottom-left once the encounter starts; Valus's health as a thin top bar after the first
hit. The window-title fps readout stays.

**Gamepad.** `SDL_OpenGamepad` + `SDL_GetGamepadButton/Axis`, polled once per frame, hot-plug via
`SDL_EVENT_GAMEPAD_ADDED/REMOVED`; left stick → climb/move, right stick → camera, RT → grip,
X → strike, Start → pause.

### 7.3 Damage and provenance

- **Model.** Valus has 100 HP. A weak hit deals 20, a full plunge 34 — three clean plunges, or a
  messier mix, ends him. Builder-tuned and declared; decoded values replace them if the pull lands.
- **Weakpoints (data-derived).**

  | Weakpoint | Shell | Verts | Weights | Frame-0 centroid | Route |
  |---|---|---|---|---|---|
  | forehead | `weakpoint_03` (Mesh_8) | 730 | `Bone_KAO` 98.4 % | (−0.91, 15.79, 3.90) | leg → torso → neck → head |
  | right upper arm | `weakpoint_04` (Mesh_4) | 204 | `Bone_JOWA_R` 87 % | (−2.76, 10.61, −3.40) | leg → torso → right arm, from behind |

  Provenance is `derived-mesh`: the material name plus the bone weights, not a hand-placed point.
  The strike proxy is an invented sphere at the shell's **posed** centroid, radius from its bbox —
  labeled `builder-tuned`, because the glb carries no hurt volume. The game does ship
  `cp3shadow/col/minotaur_a_weak_coli.clb` (11,288 B, two objects) whose layout is undecoded; it is
  the named upgrade path for a later milestone, not a dependency here. `trigger_02` (left shin) is
  treated as a climb trigger; `minotaur_a_add_leg_l_weakpoint_damage` is recorded as an unexplained
  data hint, not a third weakpoint.
- **Reaction.** On hit: reaction clip once, whole-rig shake, grip broken. The clip name is recorded.
- **Death.** The killing blow **supersedes the reaction**: the player is detached and thrown
  (`Falling`), so nobody rides the collapse, and the camera returns to follow. Valus plays the death
  clip once and freezes on its final frame; the HUD reads defeated; the app prints
  `s41-status ... defeated=1`. No ragdoll; the player can still walk after the kill.

### 7.4 Acceptance

- `scripts/s41-acceptance.sh` (new; S3/S4 scripts untouched): build, both suites, S3 + S4.0
  regressions, a deterministic `--s41-selftest` with fixed 1/60 s steps and no input device, stills
  (grab, mid-climb, strike, reaction, defeat) each with a pixel guard, HUD presence, load and
  marginal frame cost, and a **forced-fall** check.
- The selftest reuses S4's spawn/approach (which ends touching the foot), then: grab → assert bone
  and finite position; climb → assert mode, segment advance, a pinned rise, monotonic and NaN-free;
  strike twice → assert one damage event per swing, detachment, a successful catch inside the
  window, and the loop closes; a deliberately missed catch → assert ground contact; force stamina to
  zero → assert fall + land; drive health to zero → assert the death clip and `defeated=1`.
- `docs/notes/s41-results.md`: the loop, provenance, clip names, constants, performance, known gaps,
  and the user's walk checklist.
- **The user's walk:** walk to Valus, grab, climb, watch stamina, reach the sigil, strike, get
  shaken, re-grip, kill him. fps ≥60 class, cursor free, HUD legible, Escape quits.
- **Regressions:** `scripts/s3-acceptance.sh` and `scripts/s4-acceptance.sh` stay green. The
  wander glb's clip-count pin (`test_asset_io.cpp:399-402`) is updated deliberately and the new
  sha256 recorded.

### 7.5 Execution shape (≤4 agents)

Four parallel workstreams, coordinated by the plan:

1. **Data + converter**: clip bakes, clip manifests, bounded weakpoint/stamina pull.
2. **Climb core**: state machine, bands, attachment, stamina, falling — headless unit tests.
3. **Encounter**: damage, reaction, death, HUD, gamepad.
4. **Verification**: acceptance script, stills, regressions, results note.

2 and 3 touch `app.cpp`; the plan pins them to one owner at a time or a strict order.

## 8 · Risks

| Risk | Level | Mitigation |
|---|---|---|
| Attachment feels like free-fly, not climbing | high | bone-anchored bands, surface offset, real grip clips, stamina cost, forced fall; the user's walk judges |
| Player is ejected or floor-snapped while attached | high | capsule identity + attached-mode exclusion; token test on a known foot surface |
| Reaction/death clips do not read correctly | medium | clip manifest + stills; staged-pose fallback labeled |
| Climb clips carry root motion | medium | root check at bake; neutralize or exclude; recorded |
| Camera inside the colossus while climbing | medium | dedicated `CameraClimb`; labeled no-occlusion limit |
| Strike proxy size/placement wrong | medium | sphere from the shell's posed bounds; values recorded; the user's walk judges |
| HUD/text pipeline (SDL_ttf → GPU) | low–medium | Context7-documented path; text smoke test first |
| Perf: 25 s load, more clips, HUD | medium | measure; no new geometry; HUD is one quad |
| Stamina tuning makes the route unwinnable | medium | closed math in §7.2 plus a measured margin at the sigil in acceptance |
| Scope: a fight is much more than a climb | high | non-goals are hard: no AI, no player health; the milestone ends at "he falls" |

## 9 · Left to the builder

- Band table numbers (bone pairs, radii, adjacency), climb speed, camera distance.
- Exact clip names for reaction/death, strike window, grip-break timing.
- HUD layout, font, colour.
- Sidecar format for weakpoint/stamina provenance.
- Selftest scenario timing, still moments, pixel thresholds.

## 10 · Decisions recorded

| Decision | Who | Date |
|---|---|---|
| S4.1 and S4.2 merge into one milestone: climb + fight, completing S4 | user ("merge") | 2026-10-20 |
| Hybrid data: real data where cheap, labeled hand-tuned where it resists; no blocking on RE-5 | user ("merge") | 2026-10-20 |
| The milestone is a fight, not a climb demo: defeat is in scope | user ("yes") | 2026-10-20 |
| Valus reacts and dies; he does not pursue or attack (no AI, no player health) | builder call (flagged for veto) | 2026-10-20 |
| Gamepad arrives now, behind the same input struct | builder call (flagged for veto) | 2026-10-20 |
| Climb is bone-band + `u` parameter, not mesh-surface collision | builder call (flagged for veto) | 2026-10-20 |
| Wander's real grip/attack/damage clips are baked and used | builder call (flagged for veto) | 2026-10-20 |
| Two weakpoints — forehead (`weakpoint_03`) and right upper arm (`weakpoint_04`); `trigger_02` treated as a climb trigger | builder call (flagged for veto) | 2026-10-20 |
| Strike key is `J` (E grab, F/T/Space/R/brackets already taken) | builder call (flagged for veto) | 2026-10-20 |
| SDL, windowed, cursor never captured; offline-first; static-only RE | user (inherited) | 2026-10-16 |

## 11 · Builder-check resolutions (2026-10-20)

A cold-read planner raised six questions. All six are resolved here as builder calls (flagged for
veto) — no user round was needed:

1. **Stamina math** → the original numbers did not close: one bar fell short of the sigil. Pool
   rescaled to 0–100 with drains 2.5 / 4.0 / 8.0 and a ~30-unit margin at the forehead; acceptance
   records the measured margin (§7.2).
2. **Grip break** → a hit detaches into `Falling`; a 0.6 s catch window accepts a **fresh** `E`
   press to the nearest band within 2.5 m; a miss means the ground and a re-climb (§7.2).
3. **Death** → the killing blow supersedes the reaction; the player is thrown, the camera returns to
   follow, Valus freezes on the death clip's last frame, and the player can still walk (§7.3).
4. **Climb camera** → user-orbited around the attach point, seeded outward on grab, `F` recenters;
   the right stick is added to the gamepad map (§7.2).
5. **Gamepad verification** → the mapping is unit-tested headlessly; hardware verification is on the
   user's walk **if a pad is available**, otherwise the results note says "hardware unverified" —
   never claimed.
6. **Reference capture** → none. S4.0 excluded the dev port and no Valus capture exists; the user's
   walk is the reference, and the results note labels tuning `builder/user-tuned`, not
   `reference-tuned`, to keep provenance honest.

*Personal-use fan project. No game assets are distributed from this repository.*
