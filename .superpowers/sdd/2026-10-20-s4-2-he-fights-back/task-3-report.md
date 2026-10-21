# Task 3 report — Gate damage on the weakpoint sphere

**Status:** COMPLETE
**Commit:** `aeea28a feat(app): gate strike damage on the weakpoint shell`
**Branch:** `s4-2-he-fights-back`

## Disclosure: pre-existing working-tree change

When this session started, the worktree already contained an uncommitted, complete
version of this change (`app/src/app.cpp`, `app/src/player_state.cpp`,
`app/src/player_state.h`, `app/tests/test_player_state.cpp`). I diffed it against
`HEAD` (`3cd105d`), reviewed it against the brief and design §7.2, reproduced the
red/green cycle, and committed it. No partial or foreign edits were discarded.

## What I implemented

Damage on a strike release is now gated on the weakpoint shell (design §7.2):

- `app/src/player_state.h` — added `using WeakpointProxyLookup =
  std::function<bool(int weakpoint, Vec3& center, float& radius)>` and
  `PlayerFrame::weakpointProxy`; documented that the hit point is the closest
  point on the player's capsule segment (`position.y` to
  `position.y + kPlayerCapsuleHeight`). Added `<functional>`.
- `app/src/player_state.cpp` — new file-local `strike_reaches_shell(feet,
  weakpoint, lookup)`. On release the damage event and `strikeHit` are emitted
  only when the band's weakpoint resolves to a shell sphere that contains the
  capsule's closest point. `detach(combat, events, /*brokeGrip=*/true,
  tuning.catchSeconds)` stays outside the gate, so a whiff still reports
  `gripBroke`, enters `Falling`, and opens the catch window.
- `app/src/app.cpp` — `player_frame_of` binds `frame.weakpointProxy` to
  `posed_weakpoint_proxy(scene, entityRig, palettes[entityRig], material,
  center, radius)` inside the existing `entityRig < palettes.size()` guard.
  `weakpoint_material_name()` is null-checked before `std::string` construction
  (it answers `nullptr` for ids other than 0/1). Added `#include "grab.h"`.
- `app/tests/test_player_state.cpp` — `head_frame` gained a default valid proxy
  for weakpoint 0 (centre `(0, 8.5, 0)`, radius 2.5, which contains the head
  band's attach point at `(2.1, 8, 0)`). New tests:
  - far/small sphere on the head band → 0 damage, `strikeHit == false`,
    `gripBroke == true`, `Falling` + 0.6 s catch window;
  - no lookup on the head band → 0 damage, `strikeHit == false`, grip break
    still reported;
  - capsule-segment-vs-feet discriminator: a sphere at `(0, 9.8, 0)` r = 2.2
    reaches the capsule top `(2.1, 9.7, 0)` (2.10 m) but not the feet
    `(2.1, 8, 0)` (2.77 m) → 20 damage, so a feet-only hit test would fail.
  - the waist/chest strike test now expects 0 (non-weakpoint band) and asserts
    `strikeHit == false`.

Climb bands, the charge model (0.9 s / 75 %), and the grip-break timing are
unchanged. `scripts/s41-acceptance.sh` is untouched.

## RED evidence

To reproduce red, I temporarily wrote `HEAD:app/src/player_state.cpp` over the
working file (leaving the new tests and `PlayerFrame` interface in place),
rebuilt, and ran the focused tests. The ungated release damage made exactly the
gate assertions fail:

```
TEST CASE:  a strike outside the weakpoint shell deals no damage but still breaks the grip
test_player_state.cpp:386: ERROR: CHECK( e.damageDealt == 0 ) is NOT correct!
  values: CHECK( 20 == 0 )
test_player_state.cpp:387: ERROR: CHECK_FALSE( combat.strikeHit ) is NOT correct!
  values: CHECK_FALSE( true )

TEST CASE:  a strike with no weakpoint lookup deals no damage
test_player_state.cpp:412: ERROR: CHECK( e.damageDealt == 0 ) is NOT correct!
  values: CHECK( 20 == 0 )
test_player_state.cpp:413: ERROR: CHECK_FALSE( combat.strikeHit ) is NOT correct!
  values: CHECK_FALSE( true )

TEST CASE:  a strike from a climb shakes the player off too
test_player_state.cpp:886: ERROR: CHECK( hit.damageDealt == 0 ) is NOT correct!
  values: CHECK( 20 == 0 )
test_player_state.cpp:887: ERROR: CHECK_FALSE( combat.strikeHit ) is NOT correct!
  values: CHECK_FALSE( true )

[doctest] test cases:  12 |   9 passed | 3 failed | 164 skipped
[doctest] assertions: 156 | 150 passed | 6 failed |
[doctest] Status: FAILURE!
```

The restored implementation was then rebuilt from the saved file.

## GREEN evidence

Focused (`--test-case="*strike*,*hit point*,*weakpoint*"`):

```
[doctest] test cases:  12 |  12 passed | 0 failed | 164 skipped
[doctest] assertions: 156 | 156 passed | 0 failed |
[doctest] Status: SUCCESS!
```

Full `ctest --test-dir app/build --output-on-failure`:

```
1/2 Test #1: unit_tests .......................   Passed    9.59 sec
2/2 Test #2: windowed_selftest ................   Passed    0.18 sec

100% tests passed, 0 tests failed out of 2
```

S4.1 regression (the exact command from the brief):

```
s41-strike swing=1 damage=34 health=100->66 weakpoint=0
s41-strike swing=2 damage=20 health=66->46 weakpoint=0
s41-strike swing=3 damage=34 health=34->0 weakpoint=0
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

The three head-strike damage values are **34 / 20 / 34**, unchanged.

## Exact hit-test math shipped

```
closestY = clamp(center.y, feet.y, feet.y + 1.7)
closest  = (feet.x, closestY, feet.z)
hit      = |center - closest|² <= radius²
```

Guards before the distance test: `combat.weakpoint >= 0`; the lookup is
non-empty; the lookup returns true; feet, centre, and radius are finite; radius
is non-negative. `feet` is `player.position` (the band attach point while
attached), exactly the convention the design pins.

Worked values used by the tests:

- Fixture head proxy: feet `(2.1, 8, 0)`, centre `(0, 8.5, 0)`, r = 2.5 →
  closest `(2.1, 8.5, 0)`, distance 2.10 ≤ 2.5 → hit.
- Segment-vs-feet discriminator: centre `(0, 9.8, 0)`, r = 2.2 → capsule top
  2.10 m (hit), feet 2.77 m (miss).
- Real S4.1 head strike: feet ≈ 2.14 m, capsule top ≈ 1.27 m, shell radius
  ≈ 1.85 m → hit, so 34/20/34 are preserved.

## Files changed

- `app/src/player_state.h` — `WeakpointProxyLookup`, `PlayerFrame::weakpointProxy`,
  updated state-machine contract comment.
- `app/src/player_state.cpp` — `strike_reaches_shell` and the gated release path.
- `app/src/app.cpp` — `player_frame_of` binds the proxy; null-guarded material
  lookup; `grab.h` include.
- `app/tests/test_player_state.cpp` — default head proxy, two gate tests, the
  capsule/feet discriminator, waist/chest expectation 20 → 0.

## Self-review findings

- Null guard is in place: `weakpoint_material_name` is checked before
  `std::string(material)`, so ids outside 0/1 return false.
- The grip break is structurally outside the gate; a whiff still reports
  `gripBroke`, enters `Falling`, and opens the 0.6 s catch window (pinned by
  both whiff tests).
- The gate runs before this frame's `write_surface`, using the attach point
  written by the previous attached step — the same pose the strike was charged
  against. No band movement is processed in `Striking`.
- `weakpointProxy` is only bound when `entityRig < palettes.size()`, matching
  the existing `jointWorld` guard; the lambda's capture convention matches that
  lambda and the frame is used within the caller's scope.
- `strikeHit` is only consumed inside `player_state.cpp`; `app.cpp` routes on
  `damageDealt > 0`, so a whiff also plays no reaction.
- No changes to the charge/full-charge threshold, climb movement, catch timing,
  or S4.1 script.
- `git diff --check` clean; the build is warning-clean.

## Concerns

- The capsule/feet discriminator passes under an ungated implementation too
  (unconditional damage always gives 20); it guards against a *feet-only* gate
  rather than serving as red evidence. The two gate tests supply the red.
- `radius * radius` is evaluated in float; a pathological, enormous-but-finite
  radius could overflow to `inf` and accept any point. Assets only ever produce
  shell radii near 1.85 m, so this is theoretical.
- `player.position` is the attach point treated as the feet; that is the design
  convention (the attach point is where the climber's feet rest on the
  surface), not an independent capsule simulation.
