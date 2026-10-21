# Task 9 report — `--s42-selftest`

## Status

**DONE.** The deterministic S4.2 acceptance timeline is implemented, builds clean, prints
`s42-status`, and exits 0. The S4.1 selftest still exits 0 with its recorded numbers, the unit
suites pass, and S4/S3 still pass.

## What I implemented

`--s42-selftest` composes the encounter manifest, pins the entity to one stand frame, and runs the
whole S4.2 loop through fixed 1/60 s steps with no window input. The autonomous Valus selection is
off for this run (`valus_behavior` excludes `s42Selftest`); the timeline starts the stomp and club
itself through the same `advance_valus_attack` / `attack_impact_due` / `apply_valus_impact` path the
interactive loop uses.

The timeline:

1. **`s42-proxy`** — resolves both shell proxies (`posed_weakpoint_proxy` on the entity's own mesh
   range) and asserts finite centre/radius, printed as two lines.
2. **`s42-gate`** — attaches on the foot band, holds J to a full 0.9 s charge and releases: 0
   damage (no weakpoint); re-attaches on the forehead and repeats: 34 damage, `100 -> 66`.
3. **`s42-arm`** — attaches at the chest top, holds `W+D` to take the chest branch, confirms the
   route passed `RightShoulder`, releases the lateral input and climbs the `RightArm` band to its
   far end (where the arm shell is reachable), then strikes: 34 damage, `weakpoint=1`.
4. **`s42-stomp`** — places the grounded player 1.0 m from the precomputed impact foot
   (`Bone_TUMASAKI_L` at the window pose), starts `StompL`, steps to the `[0.55, 0.70]` window:
   35 damage and `Hurt`. Re-applies the same impact on the same frame and asserts the i-frame
   window ignores it.
5. **`s42-club`** — places the player under the precomputed `OptBone_hit` at the window pose, starts
   the club, steps to `[0.45, 0.65]`: 45 damage, `65 -> 20`, `Hurt`.
6. **`s42-death`** — waits out the i-frames, sets HP to 45, lands one more club: `Dead` at 0 HP,
   the player death clip is selected pinned, plays once to its end, and holds its last frame.
7. **`s42-reset`** — runs the R-key sequence (`reset_player_combat` + `reset_encounter` + entity
   stand + camera snap) and asserts HP 100, stamina 100, `Grounded`, Valus 100, `defeated=0`.
8. **`s42-stamina`** — after the reset, walks from the spawn into the nearest leg capsule, walks
   the fixed foot-band leg, grabs `left_foot`, and climbs `grab+forward` to the top of `Head` with
   no teleport. Prints the real start/end heights and the remaining pool.
9. **`s42-status`** — one field per phase above plus `defeated`.

Any failed invariant throws `std::runtime_error("s42-selftest: ...")`, which the existing
`run_app` catch prints as `sotc-viewer: s42-selftest: ...` and returns 1. Missing manifest/entity
or a missing fight clip is a usage/load error (exit 2), as in S4.1.

### Defect found by the timeline (fixed)

The first run of the timeline found that the **Waist band is zero-length**: `Bone_KOSI` and
`Bone_HARA` share one posed position in the stand clip (and at every sampled time), so
`climb_surface_point` cannot form the band and the climber was detached at the thigh top. The full
foot→head route was therefore impossible in production, not just in the test.

I fixed it in `player_state.cpp` (separate commit): a chain transition now skips a band whose
segment cannot be formed, in the direction of travel, instead of dropping the grip. Every
resolvable band is returned unchanged; the band table, the chain, the radii, the weakpoint IDs and
the pinned S4.1 tests are untouched. The S4.1 climb (foot → left shin) and its numbers are identical,
and the new full climb reaches the head.

## Full `s42-*` output, verbatim

Command:

```bash
export SOTC_LIB="$(pwd)/out/library"
./app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s42-selftest
```

```text
s42-proxy weakpoint=0 radius=1.955
s42-proxy weakpoint=1 radius=2.165
s42-gate foot=0 head=34
s42-arm band=right_arm weakpoint=1 damage=34
s42-stomp damage=35 health=100->65 mode=hurt
s42-club damage=45 health=65->20
s42-death clip=player_a_fall_damage_to_death mode=dead
s42-reset health=100 stamina=100 mode=grounded valus=100
s42-stamina from_y=28.425 to_y=43.484 remaining=39
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
s42-status proxy=2 gate_foot=0 gate_head=34 arm_band=right_arm arm_weakpoint=1 arm_damage=34 stomp_damage=35 stomp_health=65 club_damage=45 club_health=20 death_clip=player_a_fall_damage_to_death reset_health=100 reset_stamina=100 reset_mode=grounded reset_valus=100 stamina_remaining=39 defeated=0
```

Exit code **0**. A second run diffed every `s42-*` line: identical.

## Test commands and results

```bash
export SOTC_LIB="$(pwd)/out/library"
cmake --build app/build -j 8                     # clean, no warnings
ctest --test-dir app/build --output-on-failure   # 2/2 Passed (unit_tests 9.7 s, windowed_selftest)
```

```bash
./app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb --s42-selftest
# exit 0; output above
```

```bash
./app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb --s41-selftest
# exit 0
```

S4.1 event/status output (unchanged shape and numbers):

```text
s41-grab frames=522 band=left_foot pos=380.454,28.425,369.175
s41-climb frames=240 from_y=28.425 to_y=33.199 band=left_shin
s41-strike player_px=1147,661,273,93
s41-strike swing=1 damage=34 health=100->66 weakpoint=0
s41-strike swing=2 damage=20 health=66->46 weakpoint=0
s41-strike swing=3 damage=34 health=34->0 weakpoint=0
s41-death clip=minotaur_a_death_front defeated=1
s41-settle frames=177 grounded=1
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

S4 and S3 regressions (run for the climb-state change):

```text
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=993,237,1485,751 pos=378.187,28.430,369.028 frames=423
s3-status instances=30 spawn=353.000,27.951,345.000 pos=368.146,27.918,358.059 ground=ok frames=240
```

Both exit 0.

Usage guard:

```bash
./app/build/sotc-viewer --library "$SOTC_LIB" --s42-selftest
# exit 2: sotc-viewer: --s42-selftest needs --manifest
```

## Files changed

| File | Change |
|---|---|
| `app/src/app.h` | `AppOptions::s42Selftest` with the timeline's contract |
| `app/src/main.cpp` | `--s42-selftest` parsing, usage text, `--manifest` requirement |
| `app/src/app.cpp` | S4.2 constants, `valus_behavior` exclusion, the full timeline |
| `app/src/player_state.cpp` | skip a degenerate climb band at a chain transition (defect fix) |

Commits:

```text
0b22fe3 fix(app): skip degenerate climb bands
965f8c1 feat(app): S4.2 deterministic selftest
```

## Self-review findings

- **Gate path is the production path.** The gate/arm strikes go through `player_step` and
  `apply_combat_events`; the proxy line calls the same `posed_weakpoint_proxy` the frame binds.
  No damage, window or gate number changed. The foot strike is a real clang (0), the head and arm
  are real 34s.
- **Attacks are driven, not selected.** The stomp/club/death phases call `start_valus_attack` and
  then the same `advance_valus_attack` + `attack_impact_due` + `apply_valus_impact` the interactive
  loop uses. Selection is not exercised, by design (the brief says the selftest drives the state
  itself).
- **Target placement is scaffolding, clearly scoped.** The stomp/club/death phases precompute the
  attack pose at the window, place the grounded player there, and clear the entity collision
  capsules for those steps so the target is not pushed out before the impact. The damage functions
  and tuning are untouched. Only the stamina phase is teleport-free, as required.
- **i-frame check.** The "second immediate hit" is the same stomp impact applied again on the same
  frame, which isolates the i-frame gate from the once-only `attackHit` mark. It is intentionally
  not a second full attack.
- **Arm margin.** The first W+D leg takes the chest branch; after `RightShoulder` the timeline drops
  the lateral input, so the arm climb does not orbit away from the shell. The strike lands with the
  capsule-to-shell distance ~1.36 m against the 2.165 m measured radius (development measurement;
  the committed output prints only the asserted damage).
- **Death pin.** The death clip is stepped to its own duration and then asserted still `Dead`,
  pinned, and clamped at the last frame — not just selected.
- **Stamina.** The full climb is the real state machine: walk → foot-band walk → grab → hold
  `E+W`. It rises 15.059 m and leaves 39 of the pool. The 0.79 m `left_thigh` band and the skipped
  zero-length waist explain the route length; no teleport is used.
- **Determinism.** Two consecutive runs produced byte-identical `s42-*` lines.
- **No stale debug.** The temporary band/waist/arm probes used during development were removed;
  `git diff --check` is clean.

## Concerns

1. **Scope expansion (the climb fix).** Task 9 was scoped to the selftest; the timeline exposed a
   real zero-length band in the S4.1 climb table. The fix is a separate commit, 24 lines,
   direction-aware, and leaves every resolvable band and all pinned tests unchanged. It does not
   touch damage numbers, windows, or the gate. A future cleanup could remove `Waist` from the chain
   table, but that would break the pinned S4.1 chain expectations and is out of scope here.
2. **The zero-length band still exists in the table.** The runtime now skips it; the table still
   lists it. That is deliberate (the S4.1 unit test pins the chain), but a reviewer should know the
   skip is what makes the route work.
3. **i-frame evidence is a re-application.** It proves the `player_take_damage` gate, not a second
   attack's timing. If the acceptance wants a second full attack blocked, a longer setup would be
   needed; the current check is the brief's "second immediate hit".
4. **No stills.** The selftest renders one final frame but writes no PNG. Task 10 owns the six
   acceptance stills; the frame here only keeps the render path live.
5. **Status schema is new.** `s42-status` uses distinct keys per phase (`proxy`, `gate_foot`,
   `gate_head`, `arm_band`, `arm_weakpoint`, `arm_damage`, `stomp_damage`, `stomp_health`,
   `club_damage`, `club_health`, `death_clip`, `reset_health`, `reset_stamina`, `reset_mode`,
   `reset_valus`, `stamina_remaining`, `defeated`). Task 10 should parse those names.

## Verdict

The acceptance timeline is green end to end, the S4.1/S4/S3 regressions are green, and the one
production defect it found is fixed and isolated in its own commit.
