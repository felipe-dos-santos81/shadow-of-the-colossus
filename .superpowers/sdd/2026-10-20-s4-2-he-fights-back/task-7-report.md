# Task 7 report — Valus attacks: stomp and club

**Status:** complete.
**Commit:** `1db413f feat(app): Valus turns, stomps and swings`
**Branch:** `s4-2-he-fights-back` (worktree)

## What I implemented

### The pure decision module (`app/src/valus.h`, `app/src/valus.cpp`)

Ruling R8's attack decision, separate from the app loop:

- `ValusAttack { None, StompL, StompR, Club }`.
- `ValusAttackInput` with R8's fields plus `stompLeftNext` — the one bit of
  memory alternation needs, carried in the input so the selector stays pure.
- `select_valus_attack(const ValusAttackInput&)`.
- `attack_impact_due(ValusAttack, float clipSeconds, bool alreadyHit)`.
- Supporting pure helpers exported for the app and the tests:
  `valus_attack_cooldown_seconds`, `valus_attack_cancelled`,
  `entity_yaw_deg`, `valus_yaw_slew`.
- The pinned numbers are named constants (ranges, cooldowns, window bounds,
  yaw rate, damage reaches) so the app and tests read one source.

### App wiring (`app/src/app.h`, `app/src/app.cpp`)

- `Encounter` gains the attack state (`attack`, `attackSeconds`,
  `cooldownSeconds`, `attackHit`, `pendingCooldownSeconds`, `stompLeftNext`)
  and the R8 entity transform (`entityPosition`, `entityScale`,
  `entityBaseYawDeg`, `entityYawDeg`, `entityLift`, `entityTransformSet`).
- `EntityClips` resolves `minotaur_a_atk_leg_l`, `minotaur_a_atk_leg_r` and
  `minotaur_a_atk_weapon2_a` once at load; the encounter load gate now refuses
  a manifest whose entity lacks them.
- `advance_valus_attack` owns selection, the attack clock, clip end, the
  return to stand and the cooldowns, and blocks new attacks while a reaction
  plays or after defeat. The attack clock is owned by `attackSeconds` exactly
  like the reaction clock (`playback.playing` stays false; the app writes the
  sampled time). This keeps the drawn pose and the impact window on one clock.
- Impact is checked after the player's step, so a landed strike on the same
  frame cancels the attack and its pending impact before it can fire.
  `attackHit` is set on the first in-window frame, so the window is once-only
  even when `player_take_damage` ignores the hit (Dead/i-frames).
- `apply_valus_impact` measures from the posed `Bone_TUMASAKI_L/R` (stomp) or
  `OptBone_hit` (club) to the player's capsule axis, the same shape the
  weakpoint gate uses. Attached players take nothing; Dead and i-frames are
  `player_take_damage`'s own gates. Stomps are non-heavy, the club is heavy.
- A landed weakpoint strike (the only path with `damageDealt > 0`) cancels an
  in-progress attack through `valus_attack_cancelled` + `cancel_valus_attack`;
  the cooldown is parked and starts when the reaction clip ends. A whiff does
  not cancel.
- Entity yaw: the manifest transform is captured at setup, the model is
  recomposed each interactive frame from `entityYawDeg`, and the yaw slews at
  30 deg/s on the shortest path only while the player is exactly `Grounded`
  and Valus is alive. The club is placed by `update_club_model` after the
  entity model rebuild, so it turns with the body.
- `reset_encounter` clears the attack, clock, cooldown, hit mark, pending
  cooldown and alternation seed and restores the manifest yaw; the R handler
  still returns the playback to `minotaur_a_stand` and now recomposes the body.

## Selection table as shipped

| Condition (checked in order) | Result |
|---|---|
| not `PlayerMode::Grounded`, attached, defeated, or `cooldownSeconds > 0` (NaN blocks) | `None` |
| player within **4.5 m** (horizontal) of a foot | `StompL` / `StompR`: the reachable foot; when both are reachable, `stompLeftNext` alternates |
| otherwise within **14 m** and `dot(entityForward, toPlayer) > 0.2` | `Club` |
| otherwise | `None` |

## Impact windows and damage as shipped

| Attack | Window (clip seconds) | Damage point | Damage |
|---|---|---|---|
| Stomp L/R | **[0.55, 0.70]** inclusive | `Bone_TUMASAKI_L` / `Bone_TUMASAKI_R` | 35 within 2.5 m; 15 within 6.0 m |
| Club | **[0.45, 0.65]** inclusive | `OptBone_hit` | 45 within 2.5 m |

Cooldowns start when the clip ends: **4 s** stomp, **5 s** club. The `--entity-time`
pin, the S3/S4/S4.1 scripted timelines, and an uninhabited manifest disable the
autonomous behavior; it runs in the interactive loop (Task 9's timeline will
drive it directly).

## RED / GREEN evidence

RED (stub `valus.cpp` returning None/false/0, before the implementation):

```
[doctest] test cases:   209 |   198 passed | 11 failed | 0 skipped
[doctest] assertions: 14714 | 14680 passed | 34 failed |
[doctest] Status: FAILURE!
```

All 11 failures were the new `test_valus.cpp` selection/window/cooldown/cancel/yaw
cases. Implementing `valus.cpp` then exposed three test-fixture mistakes (a
hand-built left foot that was itself inside stomp range for the right-foot case,
and a club probe that stood on a baseline foot); the fixtures were corrected, not
the rules.

GREEN:

```
[doctest] test cases:   209 |   209 passed | 0 failed | 0 skipped
[doctest] assertions: 14721 | 14721 passed | 0 failed | 0 skipped
[doctest] Status: SUCCESS!
```

CTest:

```
1/2 Test #1: unit_tests ........... Passed
2/2 Test #2: windowed_selftest .... Passed
100% tests passed, 0 tests failed out of 2
```

S4.1 regression (run once, as requested):

```
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

S4 regression (extra check that the recomposed entity transform is exact):

```
s4-entity clip=minotaur_a_stand joints=238 lowest=28.297 ground=28.368 delta=-0.070 bbox=1123,237,1485,751
s4-status ... feet=ok ... frames=423
```

## Files changed

- `app/src/valus.h` — new: attack enum, input, selector/window/cooldown/cancel/yaw helpers.
- `app/src/valus.cpp` — new: pure implementations.
- `app/tests/test_valus.cpp` — new: 12 test cases covering the selection matrix,
  range/facing boundaries, alternation, impact windows, once-only, cooldowns,
  the cancel rule, entity yaw and the 30 deg/s shortest-path slew.
- `app/src/app.h` — `Encounter` attack + entity transform state; `reset_encounter`.
- `app/src/app.cpp` — clip names/load gate, `update_entity_transform`, attack
  state machine, impact/damage, yaw slew, reset and interactive-loop wiring.
- `app/tests/test_player_state.cpp` — the existing reset test now asserts the
  new `Encounter` fields and the yaw restore.
- `app/CMakeLists.txt` — `src/valus.cpp` in both targets, `tests/test_valus.cpp`
  in the test target.

## Self-review findings

1. **`playback.playing` is false during an attack.** The brief says "time 0,
   playing"; the attack clock is instead owned by `attackSeconds` like the
   reaction clock, and the app writes `playback.time` each frame. With
   `playing=true` the playback would advance a second time in `update_pose`
   and the drawn pose would drift one frame ahead of the impact window. The
   visible behavior is the clip playing from t = 0; the flag difference is
   deliberate and documented on `start_valus_attack`.
2. **Autonomous behavior is interactive-only.** The scripted S3/S4/S4.1
   timelines pin the entity and assert exact events; a stomp would race them.
   `valus_behavior` excludes those and `--entity-time`. The screenshot path
   also stays deterministic (it does not call the behavior step). Task 9's
   `--s42-selftest` will need to opt in with fixed steps.
3. **Non-yaw manifest rotations are reduced to yaw.** R8 stores yaw, and the
   encounter manifest's entity rotation is yaw-only `(0, -0.909104, 0,
   0.416570)`; a future manifest with pitch/roll would lose it. Documented in
   `entity_yaw_deg`.
4. **One entity.** `Encounter` holds one transform and the load gate/behavior
   target rig `playerRig + 1`; a multi-entity manifest still gets the old
   static placement for the rest. S4.0 ships one entity; this matches the
   existing S4.1 assumptions.
5. **Hit reach is capsule-axis distance.** A club tip 2.0 m above the player's
   feet still hits (the capsule top is 1.7 m), which is the same generous
   shape the weakpoint gate uses. The design's "within 2.5 m" does not say
   point-to-feet; this is the more physical reading.
6. **The app-level state machine is not unit tested.** The decision and window
   are pure and covered; cooldown end, alternation flip and the cancel's
   pending cooldown are wired in `app.cpp` and will be exercised by Task 9's
   timeline. I checked the ordering by inspection: behavior → poses → player
   step → combat events/cancel → impact.
7. **The entity model rebuild is equivalent at the manifest yaw.** The S4
   selftest still reports `feet=ok`, the same bbox, and the same stop clearance
   after the rebuild, confirming `entity_yaw_deg` reproduces the baked yaw.

## Concerns / deferred

- Club range (14 m) selects the swing but damage needs the player within 2.5 m
  of `OptBone_hit`; a distant club is a deliberate whiff. If the acceptance
  stills or playtest read it as "swings at nothing", the selection range is the
  tuning knob, not the hit test.
- A stomp is a 3.73 s clip with the impact at 0.55–0.70 s; the telegraph is long
  by design and may need feel tuning in Task 9.
- `advance_encounter_reaction` still uses the raw frame dt while the attack clock
  is clamped to 0.05 s; a rare long hitch could skip a reaction frame (existing
  behavior, not introduced here).
