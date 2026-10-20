# S4.2 — "He fights back": a real fight with Valus (design)

**Date:** 2026-10-20 · **Status:** design (pre-plan) · **Follows:** S4.1 (`2026-10-20-s4-1-first-colossus-design.md`)
· **Closes:** the S4.1 final review's three Critical findings and the open Important items

## 1 · Intent

S4.1 made the encounter playable, but it is still a climbing puzzle with a passive giant: you can
kill Valus by striking his foot, the right-arm weakpoint cannot be reached by the designed route,
and he never attacks. This milestone makes it a **fight** — the weakpoints are the ones the game
ships, damage only lands on them, and Valus turns, stomps, and swings his club while you climb.

It also closes the provenance gap: the S4.1 results note calls the weakpoints `derived-mesh` while
the runtime uses two hand-assigned band flags. Here the shell meshes are read from the converted
glb, their posed centroid and bbox radius drive the strike, and the note matches the code.

The club is no longer deferred: the grip socket is `OptBone_sword` (identity attachment puts the
club tip 1.5 cm from `OptBone_hit`), and `minotaur_a_atk_weapon2_a` decodes 58/58. The milestone
ships the club.

## 2 · Goals (the done-line)

1. **Weakpoints are shell-derived.** `weakpoint_03` (forehead) and `weakpoint_04` (right upper arm)
   are found by material name in the entity rig; a posed centroid and bbox radius are computed from
   the shell's vertices each strike. The strike proxy is that sphere — not a band flag.
2. **Damage is earned.** A strike deals damage only when the player is attached to a weakpoint band
   **and** inside that shell's sphere. A strike anywhere else clangs (no damage), still costs the
   swing, and still shakes the player off. Valus cannot be killed from the ground.
3. **The body is climbable in two dimensions.** `A/D` (and the left stick's lateral axis) orbit
   around the band; `W+D` at the top of the chest takes the **branch** to the right shoulder, and
   `W` continues to the right arm. The right-arm weakpoint is reachable by the designed route.
4. **Valus turns and attacks.** He yaws toward the player (slew, no clip), **stomps** when the
   player is grounded within 4.5 m of a foot, and **swings the club** when the player is grounded
   within 14 m and in front. Both attacks have a telegraph (the clip), a pinned impact window, and
   a cooldown.
5. **The club is in his hand.** `c01_minotaur_a_weapon.glb` is loaded as a static mesh and drawn
   each frame at `entityModel × palette[OptBone_sword] × bindWorld[OptBone_sword]`, so it follows
   the swing exactly.
6. **Wander can be hurt and killed.** Player HP 100; stomp 35 direct / 15 shockwave; club 45; 1.0 s
   invulnerability; medium/heavy hurt clips; death clip pinned; `R` soft-resets the encounter
   without reloading the 25 s scene.
7. **The HUD tells the truth.** Player health (amber, above stamina), Valus's health only after the
   first hit, and the whole HUD only when an entity is present. Gamepad strike is the **west**
   (X/square) button, with a mapping test.
8. **The records are honest.** The clip record gains a `rootMotion` decision per clip; a clip-free
   re-conversion deletes a stale `.clips.json`; the results note's numbers and provenance match the
   code.
9. **Acceptance.** `scripts/s42-acceptance.sh`, a deterministic `--s42-selftest`, stills, the
   **measured** full-route stamina margin at the sigil, and S4.1/S4/S3 regressions green.

## 3 · Non-goals (this pass)

- Pursuit/walking toward the player, pathfinding, aggro states, patrol — he turns in place and
  attacks from where he stands. `minotaur_a_walk*` is not baked.
- Knockdown/get-up chains, wall slams, fall damage, player ragdoll.
- The knee (`_hiza`) mesh, fur shells, banner cloth, eye glow, event cameras.
- Other colossi, Agro, world streaming, audio, menus, save.
- Cross-bank clips (`minotaur_b/c_*`) — mechanically mappable but authored for other colossi.
- A general manifest `attach` directive; the club attach is a Valus-specific path this pass.

## 4 · Anti-goals (a failure even if it runs)

- **A fight you can win from the foot** — the S4.1 defect this milestone exists to remove.
- **A weakpoint presented as data-derived while the code uses a flag.**
- **An attack with no telegraph**, or damage applied outside its impact window.
- **A Valus who attacks through his own death** or a player who dies without the death clip.
- **A club floating in the wrong hand/frame**, or a club that does not follow the swing.
- Regressing S4.1's acceptance or the S3/S4 walks.
- Docs that disagree (design / plan / results).
- Dropping below the ≥60 fps class.

## 5 · Constraints (inherited, fixed)

- **SDL3 + SDL_GPU/Metal**, C++20, Apple Silicon; **windowed**; **the cursor is never captured**
  (user, 2026-10-16). `windowed_selftest` stays green.
- Offline-first: converted glbs + manifests + clip records only.
- Static-only RE. Personal use, no redistribution; assets and derived glbs uncommitted.
- .NET 10 converter; tests fail fast without `SOTC_GAME_ROOT` / `SOTC_LIB`; bash 3.2; typed commits;
  **≤4 concurrent agents**.
- S3/S4/S4.1 manifests and scripts are not modified.
- Tools: Context7 for SDL3, Ghidra/REA only if a decode is actually needed (none is planned).

## 6 · Recon facts (2026-10-20)

**Weakpoint shells (verified).** `weakpoint_03` (Mesh_8): 730 verts, `Bone_KAO` 98.4 %, local bbox
half-diagonal **≈ 1.52 m** (the *posed* mean-to-corner radius used at runtime is **≈ 1.85 m**).
`weakpoint_04` (Mesh_4): 204 verts, `Bone_JOWA_R` 87 %, local half-diagonal **≈ 1.79 m**. The head
shell's centroid is ≈ 1.23 m from the head band; the arm shell ≈ 1.16 m from the arm band. The
S4.1 attach points sit outside both spheres on the front/side approaches, while the player's
**capsule top** is inside the head sphere at the scripted front pose (≈ 1.27 m from the centre).

**Valus clips (verified).** `minotaur_a_atk_leg_l/r`: 56 frames, 15 fps, 58 tracks, 3.73 s, cues
`MINO_FOOT_ATK` / `MINO_A_LEG_L/R_ATK` — club-free stomps. `minotaur_a_atk_weapon2_a`: 21 frames,
15 fps, 58 tracks, 1.4 s, cues `MINO_WEAPON_ATK`; club tip travels from `(−7.44, 5.85, 2.42)` to
`(−5.71, 7.28, −11.44)` through `(−6.12, 0.26, −7.10)` at frame 12. Every `minotaur_a` clip maps
58/58.

**Club (verified).** `c01_minotaur_a_weapon.glb`: 613,576 B, 1 node / 1 mesh, 9,701 verts,
18,668 tris, **no skin**, material `colossus_01_legs` (same textures already in the library), bbox
3.51 × 3.91 × 9.11 m, origin at the grip. Rig chain: `Bone_root → Bone_HAND_A_R → OptBone_sword →
OptBone_hit` (club tip, local `(0,0,−7.0)`). Identity attachment to `OptBone_sword` puts the tip
**0.015 m** from `OptBone_hit`; `HAND_A_R` identity is 8.56 m off and `OptBone_atk_weapon` is a
root-parented helper 14.44 m off. The weapon `.cgpr` carries no attachment; the main entity `.cgpr`
binds `Club_Attack` to `OptBone_hit` and the death effect to `OptBone_sword`.

**Player clips (verified).** `player_a_fall_damage_to_death` (18 f, 15.45 fps) is already baked and
unused. To add: `player_a_damage_h_fly1` (9 f, 0.60 s), `player_a_damage_h_fly_end` (10 f, 0.63 s),
`player_a_fall_damage_start` (6 f, 0.36 s), `player_a_fall_damage_end` (26 f, 1.70 s). `m_fly2`
(medium) is already baked.

**App (verified).** `posed_bounds` hardcodes the **player's** rig and iterates every mesh — it
cannot serve an entity shell. `PlayerMode` has no hurt/dead state; `PlayerCombat` has no health.
`set_hud` runs for every manifest, and the health bar is full before the first hit. Gamepad strike
is `SOUTH` (A/cross), not the spec's X/square. The clip record writes only when clips exist and
never deletes a stale sidecar.

## 7 · The how (the decided shape)

### 7.1 Weakpoints — shells, not flags

Add to `anim.h`:

```cpp
bool posed_weakpoint_proxy(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette,
                           const std::string& materialName,
                           Vec3& outCenter, float& outRadius);
```

Semantics: iterate the rig's **own** mesh range; select meshes whose material name matches exactly;
transform every vertex with the weighted palette sum (zero-weight passthrough) then `mesh.model`;
`outCenter` = mean, `outRadius` = max distance from the mean to the eight posed bbox corners. Return
`false` without writing on a missing skin, a palette-size mismatch, no matching vertices, or any
non-finite value.

Add `const char* weakpoint_material_name(int weakpoint)` in `grab.h`:
`0 → "weakpoint_03"`, `1 → "weakpoint_04"`, else `nullptr`.

Call it **on strike release only** (204–730 vertex transforms), never per frame.

### 7.2 Damage — earned, not assumed

`PlayerFrame` gains:

```cpp
using WeakpointProxyLookup = std::function<bool(int weakpoint, Vec3& center, float& radius)>;
WeakpointProxyLookup weakpointProxy;
```

The app binds it to `posed_weakpoint_proxy` on the entity rig. On release, damage is emitted only if
`combat.weakpoint >= 0`, the lookup succeeds, and the **closest point on the player's capsule
segment** (`position.y` to `position.y + 1.7`) to the shell centre is within `radius`. Using the
capsule rather than the feet is what makes a head-height weakpoint hittable from a normal climb and
keeps the S4.1 head strike green (feet ≈ 2.14 m, capsule top ≈ 1.27 m against a ≈ 1.85 m radius).
Otherwise `damageDealt = 0` (a clang). The grip break stays **unconditional**, so a whiff still
exits `Striking` and the fight keeps its rhythm.

This makes the foot-strike test's expectation `0` instead of `20`, and adds out-of-sphere and
no-lookup cases.

### 7.3 Climb — around the body, and the branch

`ClimbBand` gains a trailing `ClimbBandId branch = ClimbBandId::Count`. Only `Chest` sets it, to
`RightShoulder`. `RightShoulder` keeps `up = RightArm`.

Movement: `lateral = right − left`. Any lateral input selects `Climbing`. At the top of a band,
`W + lateral` follows `branch` when it exists, otherwise `up`; `W` alone follows `up`. While
attached and not striking, lateral input orbits the attach point around the band axis by
`−lateral × climbSpeed × dt / (radius + kPlayerCapsuleRadius)` using a Rodrigues rotation about the
band's own axis.

So the designed route works: foot → shin → thigh → waist → chest → (`W+D`) shoulder → (`W`) arm,
where `weakpoint 1` is live.

### 7.4 Valus — turn, stomp, swing

**Facing.** Yaw slew toward the player at 30°/s only while the player is `Grounded` and **not
attached**. While the player is on him the body holds still — rotating would slide the climber
around the band and change the route. No turn clip (the authored turns are 3 s poses; a slew reads
better here and is labeled).

**Attack selection** (grounded player only; nothing while the player is attached):

| Condition | Attack | Clip | Cooldown |
|---|---|---|---|
| within 4.5 m of either foot | stomp (alternate L/R) | `minotaur_a_atk_leg_l/r` (3.73 s) | 4 s after clip end |
| within 14 m and in front (`dot > 0.2`) | club swing | `minotaur_a_atk_weapon2_a` (1.4 s) | 5 s after clip end |

**Impact windows** (pinned after the first stills pass; starting values): stomp `[0.55, 0.70]` of
the clip, club `[0.45, 0.65]`. The stomp's damage point is the posed `Bone_TUMASAKI_L/R` of the
attacking foot; the club's is the posed `OptBone_hit`.

**Damage.** Stomp: 35 within 2.5 m of the foot, 15 within 6.0 m. Club: 45 within 2.5 m of
`OptBone_hit`. No damage while the player is attached (he shakes instead). "Grounded" for attack
selection means **`PlayerMode::Grounded` exactly** — he does not stomp a `Hurt`, `Staggered`,
`Falling`, or `Dead` player.

**Interrupts.** A **landed** weakpoint strike cancels Valus's in-progress attack and its pending
impact (the reaction clip replaces it; the cooldown starts after the reaction). A whiff does not
cancel. This makes well-timed hits a real stun, and is deliberate.

**Shake.** The existing strike reaction (reaction clip + grip break) stays. No new refuse clips
this pass.

### 7.5 Player health and death

`PlayerMode` gains `Hurt` and `Dead`; `PlayerCombat` gains `health`, `invulnSeconds`, `heavyHit`,
`pendingDeath`. `CombatTuning` gains `maxHealth`, `invulnSeconds`, `stompDamage`,
`stompWaveDamage`, `clubDamage`, `mediumHurtSeconds`, `heavyHurtSeconds`.

`player_take_damage(combat, damage, heavy, tuning)` ignores `Dead` and invulnerable states, subtracts
HP, starts i-frames, and enters `Hurt` (medium `m_fly2` 0.40 s; heavy `h_fly1` 0.60 s →
`h_fly_end` 0.63 s). Damage taken while **`Falling`** does not cancel the fall: it sets
`pendingDeath` when lethal and resolves on landing (`Dead` or `Staggered`). Damage taken while
`Staggered` cancels the stagger into `Hurt`. At zero HP on the ground it enters `Dead` and plays
`player_a_fall_damage_to_death` once, pinned on the last frame.

`R` is a **full soft reset** without reloading the scene: player HP 100, stamina 100, regen delay 0,
invulnerability 0, mode `Grounded`, position/yaw to the manifest spawn, camera recentred; Valus HP
100, `defeated` false, pose back to `minotaur_a_stand`, reaction cleared, attack cooldown 0; the
Valus health bar hides again until the next first hit.

### 7.6 Club attachment

`JointCpu` gains `Mat4 bindWorld` computed **once at load** by composing the rest chain
(`parentBind × TRS(restT, restR, restS)`) — no matrix inverse is needed, and the rest pose is the
bind pose. The club is loaded as a static mesh (rig `-1`) in the composed scene; each frame its
model matrix is

```
clubModel = entityMeshModel × palette[OptBone_sword] × bindWorld[OptBone_sword]
```

The club's own material is `colossus_01_legs`, already present; no new textures.

### 7.7 HUD, gamepad, records

- `HudState` gains `playerHealth`. New `hud_player_health_rect` (240×8, above stamina, amber).
- HUD visible only when the manifest has an entity; Valus's bar only after the first hit.
- Gamepad strike: `SDL_GAMEPAD_BUTTON_WEST`; a `gamepad_map.h` constant pins it and a unit test
  asserts west-not-south.
- Clip record: add `rootMotion` (`ground-height` | `body-offset`) per clip as a **record-only**
  provenance label — the app keeps its existing lift rule and does not read the sidecar this pass.
  Stills are the check that a body-offset root does not float or sink the new hurt/death clips.
  Delete a stale `.clips.json` when a re-conversion bakes no clips.
- `app/README.md`: add `sdl3_ttf` to the build, the new controls, and the club.

### 7.8 Acceptance

`scripts/s42-acceptance.sh` in the S4.1 house pattern:

- build, both suites, `--s42-selftest`, stills (stomp telegraph, stomp impact, club swing, hurt,
  death, arm weakpoint), pixel/HUD guards, perf.
- selftest asserts: weakpoint proxy resolves for 0 and 1; foot strike = 0; head strike = 34; the
  chest→shoulder→arm route reaches `weakpoint 1`; a stomp damages and staggers; i-frames block a
  second hit; a club hit damages; a lethal hit plays the death clip and pins; `R` resets.
- **Full-route stamina**: climb foot→head with the pinned drains and print
  `s42-stamina margin=<n>` at the sigil.
- Regressions: `scripts/s41-acceptance.sh`, then S4 (which runs S3), unchanged.

### 7.9 Execution shape (≤4 agents)

1. **Data + converter**: bake the 4 player clips and 3 Valus clips, club mesh load path, clip-record
   changes.
2. **Combat core**: weakpoint proxy, gating, lateral/branch, player health/death — headless tests.
3. **Valus**: attack state, impact windows, club attach, HUD/gamepad.
4. **Verification**: `--s42-selftest`, acceptance script, stills, results note.

2 and 3 both touch `app.cpp`; the plan pins the order.

## 8 · Risks

| Risk | Level | Mitigation |
|---|---|---|
| Club attach frame wrong (hand/socket) | high | identity to `OptBone_sword` is measured (1.5 cm tip residual); a still at a known clip time proves the club in hand |
| `bindWorld` composition wrong | high | unit test: a known bind-space point maps to a known world point at the rest pose |
| Attack feel / telegraph too long (3.7 s stomp) | medium | the clip is the telegraph; cooldowns and windows are tuning; the user's walk judges |
| Damage gating blocks the scripted strike | medium | proxy radius and attach distance measured; the selftest pin is the canary |
| Player death/reset leaves stale state | medium | reset clears HP, i-frames, mode, Valus HP/pose; a selftest asserts the reset |
| Scope: attacks + health + club | high | non-goals are hard: no pursuit, no hand swipe, no other colossi |
| Perf (club 9.7k verts, per-frame attach) | low | one extra draw; measure |
| Provenance drift again | medium | the results note quotes the proxy function and its measured radii, not adjectives |

## 9 · Left to the builder

- Attack cooldown values, impact-window frames, yaw slew rate.
- Player damage numbers, i-frame length, hurt durations.
- Club attach constant (bind frame) and the club's draw order.
- HUD layout for the player bar.
- Selftest scenario timings, still moments, pixel thresholds.

## 10 · Decisions recorded

| Decision | Who | Date |
|---|---|---|
| The next milestone is S4.2 "He fights back": close S4.1's criticals and give Valus attacks | user (skipped the question pass; builder call) | 2026-10-20 |
| Weakpoints are shell-derived (`weakpoint_03/04`), damage gated on the sphere | builder call (flagged for veto) | 2026-10-20 |
| The club ships now, attached identity to `OptBone_sword` | builder call (flagged for veto) | 2026-10-20 |
| Two attacks: foot stomp and club swing; no pursuit, no hand swipe | builder call (flagged for veto) | 2026-10-20 |
| Player health/death is in scope; `R` soft-resets without reloading | builder call (flagged for veto) | 2026-10-20 |
| Yaw slew instead of the `turn_l/r` clips | builder call (flagged for veto) | 2026-10-20 |
| SDL, windowed, cursor never captured; offline-first; static-only RE | user (inherited) | 2026-10-16 |

## 11 · Builder-check resolutions (2026-10-20)

A cold-read planner raised six questions; all six are settled here as builder calls (flagged for
veto), no user round needed:

1. **Hit test point** → the closest point on the player's capsule segment, not the feet. The feet
   are ≈ 2.14 m from the head shell's centre and would miss; the capsule top is ≈ 1.27 m inside a
   ≈ 1.85 m posed radius. The S4.1 head strike stays green without touching its script.
2. **Yaw while attached** → no. He turns only when the player is `Grounded` and not attached;
   rotating under a climber would slide the attach point and break the route.
3. **Hurt/Dead vs Falling/Staggered** → damage while `Falling` keeps falling and resolves
   `pendingDeath` on landing; damage while `Staggered` cancels into `Hurt`; "grounded" for attack
   selection is `PlayerMode::Grounded` exactly.
4. **`rootMotion` semantics** → record-only this pass (`ground-height` | `body-offset`); the app's
   existing lift rule is unchanged and does not read the sidecar; stills check the new clips.
5. **`R` reset scope** → full soft reset (HP, stamina, regen, i-frames, mode, spawn position/yaw,
   camera, Valus HP/pose/reaction/cooldown, HUD health visibility).
6. **Strike vs attack** → a landed weakpoint hit cancels the attack and its pending impact; a whiff
   does not. Deliberate stun-lock with good timing.

*Personal-use fan project. No game assets are distributed from this repository.*
