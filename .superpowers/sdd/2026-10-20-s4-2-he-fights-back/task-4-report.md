# Task 4 report — lateral climb and the chest→shoulder branch

Status: **DONE**. Commit `bd58fe7 feat(app): lateral climb and the arm branch`.

## What I implemented

**Band table (`app/src/grab.h`, `app/src/grab.cpp`).** `ClimbBand` gains a
trailing `ClimbBandId branch = ClimbBandId::Count`. Only the Chest row sets it
(`RightShoulder`); every other row omits it and takes the default, so the
existing six-field aggregate initialisers (including the `Count` empty band)
keep compiling. Radii, `up`/`down`, and weakpoints are untouched.

**Lateral climb (`app/src/player_state.cpp`).**

- `lateral = (right ? 1 : 0) - (left ? 1 : 0)`; any lateral input (with or
  without forward/back) selects `Climbing`, so it drains the climb stamina
  rate.
- While Grabbing/Climbing (never Striking), `orbit_about_band` rotates the
  current attach point about the band's own axis (`boneA → boneB`, normalised)
  through `boneA` by

  `angle = -lateral * climbSpeed * dt / (radius + kPlayerCapsuleRadius)`

  using the Rodrigues form
  `v' = v cos a + (axis × v) sin a + axis (axis·v)(1 − cos a)`.
  The rotated point is then the radial hint for the existing `write_surface`,
  which re-projects it onto the band; `bandT` is not touched.
- Branch rule: in the upward boundary case (`t >= 1`), `next = band.up`; when
  `lateral != 0` and `band.branch != Count`, `next = band.branch`. If `next`
  exists the player enters it at `t = 0`, otherwise `bandT` clamps to 1.
- Guards: a band that cannot be formed, a non-positive/non-finite surface
  radius, or a non-finite angle leaves the hint untouched; `write_surface`
  then breaks the grip through the existing path. No `CombatTuning` field was
  added.

**Docs.** `player_state.h`'s attached-mode contract and the `grab.h` band
comments now describe the lateral orbit and the branch.

## Tests (TDD)

New/updated doctest cases:

- `test_grab.cpp`
  - band-table pin extended with `expected_branch` (Chest →
    `RightShoulder`, all others `Count`);
  - `only the chest branches, and its branch rejoins the chain`: every row,
    `climb_band(Chest).branch == RightShoulder`,
    `climb_band(branch).down == Chest`, `up == RightArm`, and a hand-written
    six-field aggregate proves the trailing default is `Count`.
- `test_player_state.cpp`
  - `forward plus lateral at the chest top takes the shoulder branch`: chest
    `t = 0.99`, one `forward+right` step → `RightShoulder` at `t = 0`; then
    forward to the top of the shoulder → `RightArm` at `t = 0`,
    `weakpoint == 1`, still `Climbing`.
  - `forward alone at the chest top follows up to the neck` (the contrast that
    pins "W alone follows `up`").
  - `lateral input orbits the attach point around the band axis`: foot band,
    12 `right` steps → recovered rotation angle `-atan2(z, x)` equals
    `-N * climbSpeed*dt/(1.7+0.35)` (≈ −0.117073 rad), `bandT == 0`, `y == 0`,
    radius preserved, mode `Climbing`; 12 `left` steps return to +X.

### RED evidence

Stage 1 — tests written, interface absent (compile red):

```
app/tests/test_grab.cpp:138: error: no member named 'branch' in 'ClimbBand'
... 5 errors generated.
gmake[2]: *** [CMakeFiles/sotc-app-tests.dir/build.make:177:
             CMakeFiles/sotc-app-tests.dir/tests/test_grab.cpp.o] Error 1
```

Stage 2 — `branch` field and chest row added, movement not implemented
(behaviour red, 2 test cases / 5 assertions):

```
TEST CASE: forward plus lateral at the chest top takes the shoulder branch
test_player_state.cpp:726: CHECK( combat.band == ClimbBandId::RightShoulder )
    values: CHECK( 5 == 7 )      # stayed on Chest (branch absent)
test_player_state.cpp:737: CHECK( combat.band == ClimbBandId::RightArm )
    values: CHECK( 5 == 8 )
test_player_state.cpp:739: CHECK( combat.weakpoint == 1 )
    values: CHECK( -1 == 1 )
TEST CASE: lateral input orbits the attach point around the band axis
test_player_state.cpp:789: CHECK( combat.mode == PlayerMode::Climbing )
    values: CHECK( 1 == 2 )      # stayed Grabbing
test_player_state.cpp:792: right_angle == Approx(-0.117073)
    values: CHECK( -0 == Approx( -0.117073 ) )
[doctest] test cases: 180 | 178 passed | 2 failed
```

### GREEN evidence

Focused run:

```
./app/build/sotc-app-tests --test-case="*branch*,*orbit*" --success
[doctest] test cases:  7 |  7 passed | 0 failed | 173 skipped
[doctest] assertions: 75 | 75 passed | 0 failed
```

Full suite (`ctest --test-dir app/build --output-on-failure`):

```
1/2 Test #1: unit_tests .......................   Passed    9.15 sec
2/2 Test #2: windowed_selftest ................   Passed    0.20 sec
100% tests passed, 0 tests failed out of 2
```

The pre-existing straight-climb, strike, dt-spike, catch and stagger cases all
still pass; they never press lateral.

## Files changed

- `app/src/grab.h` — `ClimbBand::branch`, comments.
- `app/src/grab.cpp` — Chest row branch, table comment.
- `app/src/player_state.cpp` — Rodrigues helper, `orbit_about_segment`, lateral
  mode selection, branch at the band top.
- `app/src/player_state.h` — attached-movement contract.
- `app/tests/test_grab.cpp` — branch table pin + branch connectivity test.
- `app/tests/test_player_state.cpp` — `torso_arm_frame` fixture + route,
  no-lateral, and orbit tests.

## Shipped rules (exact)

- Rotation sign (**superseded by ruling R6 — see the fix section at the end**):
  the original implementation used `angle = -lateral * climbSpeed * dt /
  (band.radius + kPlayerCapsuleRadius)` about `normalize(boneB − boneA)`.
  R6 immediately orients the axis upward and uses `+lateral`; the shipped code
  is the R6 form.
- Branch: only in the `t >= 1` (upward) boundary case.
  `next = up`; if `lateral != 0 && branch != Count` then `next = branch`.
  Entering `next` selects it at `t = 0`; a missing `next` clamps `bandT = 1`.
  Lateral alone never moves `bandT` and never crosses a boundary.

## Self-review findings

1. Orbit runs before the vertical/branch move, so a branch frame orbits the
   hint around the band the step began on and then projects it onto the
   shoulder. Deterministic and covered by the route test.
2. All orbit failure modes are non-destructive: the hint is left alone and the
   existing `write_surface` grip-break reports the unresolved band. No NaN can
   reach `player.position` from a bad tuning speed or a degenerate band.
3. No new state fields: `PlayerCombat` and `CombatTuning` are unchanged.
4. `Striking` is excluded from lateral movement, as the design states; the
   strike/charge/release model is byte-for-byte unchanged.
5. The trailing default is proven by a legacy six-field aggregate rather than
   only by the table rows.
6. `git diff` touches no radii, no `climbSpeed`, and no strike constants.

## Concerns

- `climb_surface_point` models the surface with a *horizontal* radial. The
  orbit's per-step angle is therefore exact on vertical bands (the foot band
  test), while on a strongly tilted band the projection back onto the band
  distorts the orbit. **Correction (review R6):** on a horizontal band — the
  real right-shoulder band, whose axis is the clavicle — the horizontal radial
  is parallel to the axis, so the radial reproject makes lateral input there
  effectively a **no-op** (the hint rotates but the surface point does not move
  sideways). The arm's lateral direction likewise depends on the real
  `boneA -> boneB` order; R6's upward orientation is a no-op for a horizontal
  axis (its `y` is 0), so the clavicle's bone order decides which way D reads
  there. Both are inherited from the S4.1 surface model (radii untouchable
  here); the rotation itself follows R6 and the route still reaches
  `weakpoint 1`.
- The route test seeds `combat` in `Grabbing` on the Chest at `t = 0.99`
  instead of pressing grab, because the hand-built fixture's nearest-band
  choice at chest height is ambiguous (waist/chest surfaces tie). The branch
  and weakpoint transitions themselves are exercised end to end.
- Per the design's literal "W + lateral" rule, `W + left` at the chest top also
  takes the branch; only `W + right` is covered by a test.

---

# Fix — ruling R6: orbit D along the player's right

Commit `88e8182 fix(app): orbit D along the player's right` (on top of
`bd58fe7`).

## What changed

1. **Orbit sign and axis orientation (`app/src/player_state.cpp`).** The
   orbit helper now orients the band axis upward before rotating and uses a
   positive lateral angle:

   ```cpp
   Vec3 axis = vec3_normalize(Vec3{b.x - a.x, b.y - a.y, b.z - a.z});
   if (axis.y < 0.0f) {
       axis = Vec3{-axis.x, -axis.y, -axis.z};
   }
   const float angle = lateral * speed * dt / surface_radius;
   ```

   `surface_radius = band.radius + kPlayerCapsuleRadius`, still a Rodrigues
   rotation through `a` (the band's `boneA`). Rationale: the player faces the
   band (`forward = −normal`) and `right = forward × +Y`
   (`app/src/player.cpp:50-59`), so with `normal = +X` right is `−Z`, and a
   positive rotation about `+Y` takes `+X` toward `−Z`: D now moves along
   screen-right. The branch rule, the route test, radii, `climbSpeed` and the
   strike model are untouched (`player_step`'s branch code is unchanged apart
   from the shared segment resolution).

2. **Segment resolved once (`app/src/player_state.cpp`).** The Grabbing/
   Climbing block now resolves `climb_band_segment` once and passes `a`, `b`,
   `band.radius` to the orbit, instead of resolving it inside the orbit and
   again in the climb. An idle hang (`moving == false`) still skips the lookup
   and leaves `write_surface` to resolve the band exactly as before; an
   unresolved segment skips both the orbit and the climb.

3. **Tests (`app/tests/test_player_state.cpp`).** The existing orbit test now
   expects the positive angle (`right_angle == +N * climbSpeed*dt/(r+0.35)`,
   ≈ +0.117073 rad; `left` still returns to +X). Two new cases:
   - `lateral right moves the climber along the player's right`: foot band,
     one right step, `dot(newPos − oldPos, right) > 0` with
     `right = cross(−normal, +Y)` computed from `climb_surface_point`;
     `z` decreases for `normal = +X`.
   - `a downward bone order still orbits right along the player's right`: the
     foot fixture with `boneA = (0,2,0)`, `boneB = (0,0,0)` (segment points
     downward); D must still satisfy `dot(moved, right) > 0` and keep the
     attach height.

4. **Minor.** `app/src/grab.cpp` table comment now says "W plus any lateral
   input at its top (W+A too)" instead of "W+D"; the table-pin comment in
   `test_grab.cpp` matches. `app/src/player_state.h` documents the upward axis,
   `+lateral`, and the player-right convention. The earlier report's
   `−lateral` rule and tilted-band concern are superseded/corrected above.

## RED evidence (old sign, new tests)

With `.y` kept upward but the sign temporarily restored to `−lateral`:

```
TEST CASE: lateral input orbits the attach point around the band axis
test_player_state.cpp:793: CHECK( right_angle == Approx(+0.117073) )
    values: CHECK( -0.117073 == Approx( 0.117073 ) )
TEST CASE: lateral right moves the climber along the player's right
test_player_state.cpp:835: CHECK( vec3_dot(moved, right) > 0.0f )
    values: CHECK( -0.0199997 >  0 )
test_player_state.cpp:836: CHECK( player.position.z < before.z )
    values: CHECK( 0.0199997 <  0 )
TEST CASE: a downward bone order still orbits right along the player's right
test_player_state.cpp:869: CHECK( vec3_dot(moved, right) > 0.0f )
    values: CHECK( -0.0199997 >  0 )
test_player_state.cpp:870: CHECK( player.position.z < before.z )
    values: CHECK( 0.0199997 <  0 )
[doctest] test cases: 10 | 7 passed | 3 failed | 172 skipped
[doctest] assertions: 74 | 69 passed | 5 failed
```

## GREEN evidence

Command (as required):

```bash
export SOTC_LIB="$(pwd)/out/library"
ctest --test-dir app/build -R unit_tests --output-on-failure
```

Output:

```
Test project /Users/felipe.dos.santos/.../s4-2-he-fights-back/app/build
    Start 1: unit_tests
1/1 Test #1: unit_tests .......................   Passed    9.99 sec

100% tests passed, 0 tests failed out of 1
```

Full suite (also run):

```
1/2 Test #1: unit_tests .......................   Passed    9.90 sec
2/2 Test #2: windowed_selftest ................   Passed    0.23 sec
100% tests passed, 0 tests failed out of 2
```

Focused covering cases:

```
./app/build/sotc-app-tests --test-case="*orbit*,*branch*,*downward*"
[doctest] test cases:  8 |  8 passed | 0 failed | 174 skipped
[doctest] assertions: 81 | 81 passed | 0 failed
```

## Remaining concerns after R6

- Horizontal bands: as corrected above, lateral input on the real right
  shoulder is effectively a no-op under the S4.1 horizontal-radial surface
  model, and the clavicle's joint order decides the arm's lateral reading.
  Reported, not changed (radii/surface model constraints).
- `docs/superpowers/specs/2026-10-20-s4-2-he-fights-back-design.md` §7.3 still
  prints `−lateral`; review ruling R6 (upward axis, `+lateral`) governs the
  shipped code. The spec was not edited by this task.
