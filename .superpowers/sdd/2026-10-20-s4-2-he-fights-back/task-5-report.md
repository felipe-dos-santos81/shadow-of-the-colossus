# Task 5 report — player health, hurt, death, reset

Status: **DONE**. Commit `48f178e feat(app): player health, hurt and death`.

## What I implemented

**State machine (`app/src/player_state.h/.cpp`).**

- `PlayerMode` gains the trailing modes `Hurt` and `Dead`;
  `player_mode_name` returns `"hurt"` / `"dead"`.
- `PlayerCombat` gains `health = 100`, `invulnSeconds`, `heavyHit`,
  `pendingDeath`.
- `CombatTuning` gains `maxHealth`, `invulnSeconds`, `stompDamage`,
  `stompWaveDamage`, `clubDamage`, `mediumHurtSeconds`, `heavyHurtSeconds`.
- `player_take_damage(combat, damage, heavy, tuning)` is the single damage
  entry point. It ignores `Dead`, an active i-frame window, and a non-positive
  or non-finite amount; it returns true only when a hit landed.
- `player_step` ticks the i-frame clock every frame (with a 1e-4 s float-drift
  snap to zero), runs the Hurt clock, treats Dead as terminal, and resolves a
  lethal airborne landing to Dead instead of Staggered.
- `reset_player_combat(player, combat, spawnPosition, spawnYawDeg, tuning)` is
  the player half of the R soft reset.

**Clips (`app/src/anim.h/.cpp`).**

- `Hurt` medium → `player_a_damage_m_fly2`.
- `Hurt` heavy → `player_a_damage_h_fly1` while `modeSeconds` is inside that
  clip's own duration, then `player_a_damage_h_fly_end`.
- `Dead` → `player_a_fall_damage_to_death`.
- No fallback chain: a missing named clip still answers `-1`.

**App (`app/src/app.h/.cpp`).**

- `reset_encounter(Encounter&)` (inline in `app.h`) restores Valus's HP,
  `defeated`, reaction clip and reaction clock. Task 7 can extend this one
  helper with its attack fields.
- The player's clip selector now pins the death clip and keeps it playing:
  `update_pose` advances it once and `playback_time` clamps on the last frame.
  The pin is cleared on the next live-mode selection.
- The load gate now also requires the two heavy-hurt clips and the player death
  clip, so a manifest missing them fails with a named-clip error.
- `R` in manifest/follow mode performs the full soft reset: player HP/stamina/
  regen/i-frames/mode/position/yaw to spawn, encounter `reset_encounter`, entity
  playback back to its manifest stand clip (unpinned, playing, t = 0), climb and
  target camera state cleared, follow offsets/distance reset and the camera
  snapped behind the spawn. In single-model mode `R` keeps its old
  "restart the clip" meaning.

## Shipped damage table and transition rules

| Constant | Value |
|---|---|
| `maxHealth` | 100 |
| `invulnSeconds` | 1.0 s |
| `stompDamage` | 35 |
| `stompWaveDamage` | 15 |
| `clubDamage` | 45 |
| `mediumHurtSeconds` | 0.40 s (`player_a_damage_m_fly2`) |
| `heavyHurtSeconds` | 1.23 s (`h_fly1` 0.60 s → `h_fly_end` 0.63 s) |
| death clip | `player_a_fall_damage_to_death`, once, pinned on its last frame |

Transitions:

1. `Dead` or `invulnSeconds > 0` → ignored (`false`, no HP change).
2. Any landed hit: `health = clamp(health - damage, 0, maxHealth)`,
   `invulnSeconds = tuning.invulnSeconds`.
3. `Falling` + non-lethal → stays `Falling`; HP and i-frames still change.
4. `Falling` + lethal → stays `Falling`, health 0, `pendingDeath = true`; on
   ground contact → `Dead` (no Staggered).
5. Ground modes (`Grounded`, `Grabbing`, `Climbing`, `Striking`, `Staggered`) +
   non-lethal → `Hurt`, `modeSeconds = 0`, `heavyHit = heavy`; attached state
   (band, weakpoint, charge, strikeHit, catch window, fall velocity) cleared.
6. Ground + lethal (including Staggered) → `Dead`, health 0, every attachment
   and pending field cleared.
7. `Hurt` expires after the medium or heavy window → `Grounded`.
8. `Dead` ignores input and stays until the R soft reset.

## RED evidence

**Stage 1 — tests written, interface absent (compile red):**

```
app/tests/test_player_state.cpp:995:38: error: no member named 'Hurt' in 'PlayerMode'
app/tests/test_player_state.cpp:1007:12: error: no member named 'heavyHit' in 'PlayerCombat'
app/tests/test_anim.cpp:628:12: error: no member named 'heavyHit' in 'PlayerCombat'
app/tests/test_anim.cpp:636:31: error: no member named 'Dead' in 'PlayerMode'
fatal error: too many errors emitted, stopping now [-ferror-limit=]
gmake[2]: *** [CMakeFiles/sotc-app-tests.dir/build.make:191:
              CMakeFiles/sotc-app-tests.dir/tests/test_player_state.cpp.o] Error 1
```

**Stage 2 — implementation in place, first behaviour run (2 test cases):**

```
TEST CASE: select_mode_clip_index maps every combat mode by name
test_anim.cpp:634: CHECK( select_mode_clip_index(demo, combat, still) == 8 )
    values: CHECK( 7 == 8 )

TEST CASE: i-frames tick down in player_step and then a hit lands again
test_player_state.cpp:993: CHECK( player_take_damage(...) ) is NOT correct!
    values: CHECK( false )
test_player_state.cpp:994: CHECK( combat.health == Approx(30.0f) )
    values: CHECK( 65 == Approx( 30 ) )
test_player_state.cpp:995: CHECK( combat.mode == PlayerMode::Hurt )
    values: CHECK( 0 == 6 )
```

The heavy-clip failure was the synthetic fixture giving every clip a 1.0 s
duration (the test now sets the baked 0.60 s). The i-frame failure was real:
60 × 1/60 left `2.79e-7` s in the window and blocked the next hit; the clock
now snaps sub-0.1 ms residue to zero.

## GREEN evidence

Focused covering cases:

```
./app/build/sotc-app-tests --test-case="damage subtracts health, starts i-frames and enters hurt,damage while falling keeps the fall; a non-lethal hit still lands in stagger,lethal damage while falling sets pendingDeath and resolves to dead on landing,damage while staggered cancels the stagger into hurt,damage while attached drops the grip and clears its fight state,zero health on the ground enters dead and ignores further damage,the R soft reset restores the player and Valus,select_mode_clip_index maps every combat mode by name"
[doctest] test cases:  7 |  7 passed | 0 failed | 184 skipped
[doctest] assertions: 72 | 72 passed | 0 failed
```

Full unit suite:

```
./app/build/sotc-app-tests
[doctest] test cases:   191 |   191 passed | 0 failed | 0 skipped
[doctest] assertions: 14528 | 14528 passed | 0 failed
```

Full ctest with `SOTC_LIB`:

```
1/2 Test #1: unit_tests .......................   Passed   10.06 sec
2/2 Test #2: windowed_selftest ................   Passed    0.25 sec
100% tests passed, 0 tests failed out of 2
```

Regression beyond ctest: `--s41-selftest` against
`out/re/stage/encounter.manifest` still prints `s41-status ... strikes=3
entity_health=0 ... defeated=1`, exits 0, and writes all stills (including the
defeat and its two controls). The S4.1 fight is unchanged.

## Files changed

- `app/src/player_state.h` — modes, health fields, tuning fields,
  `player_take_damage`, `reset_player_combat`, state diagram/contracts.
- `app/src/player_state.cpp` — damage entry point, Hurt/Dead transitions,
  landing death, i-frame tick, mode names, reset helper.
- `app/src/anim.h` / `app/src/anim.cpp` — Hurt medium/heavy and Dead mapping.
- `app/src/app.h` — inline `reset_encounter`.
- `app/src/app.cpp` — death clip pin, load gate for the new clips, R soft
  reset (player, Valus, entity playback, camera).
- `app/tests/test_player_state.cpp` — nine new cases, tuning pin, mode names,
  reset coverage, `app.h` include.
- `app/tests/test_anim.cpp` — Hurt/Dead clip mapping and no-fallback coverage.

## Self-review findings

1. **No duplicated heavy-clip split constant.** The switch to `h_fly_end`
   uses the `h_fly1` clip's own baked duration (0.60 s), so a re-bake cannot
   silently desynchronise it from `heavyHurtSeconds`.
2. **Death pin is app-owned.** `player_step` only owns the mode; the app sets
   `pinned = true, playing = true` while `Dead`, so the clip advances once and
   clamps. `update_combat_clips` clears the pin for every live mode, and the R
   handler also clears it explicitly before restarting the clip.
3. **All stale fight state is cleared on damage and on reset.** `enter_hurt`
   and `enter_dead` clear band/weakpoint/charge/strike/catch/fall velocity;
   `reset_player_combat` additionally clears stamina, regen delay, input edge
   memory and health.
4. **The load gate is complete.** A manifest whose Wander glb lacks the heavy
   or death clips now fails before the loop instead of selecting `-1`.
5. **R preserves single-model behaviour.** The new soft reset is gated on
   `follow_mode`; the standalone clip browser still restarts its clip.
6. **No camera math changed.** `camera.h/.cpp` are untouched; the reset only
   resets the follow state and snaps it with an existing `camera_follow` call.
7. **S4.1 regression checked**, not just assumed: the full selftest still
   passes with the same 34/20/34 strike line and defeat.

## Concerns

- **Unreachable defensive path:** damage while attached (`Grabbing`,
  `Climbing`, `Striking`) enters `Hurt` per the binding rule, but attacks never
  target an attached player (design §7.4), so it is not a reachable fight
  state. If it ever fires, the player returns to `Grounded` at the band's
  surface and the next ground step snaps him to the heightmap.
- **Pending death can coexist with a catch:** the catch window still works
  while `pendingDeath` is set, because the binding only resolves on landing.
  This is unreachable today (attacks only target a `Grounded` player) but is a
  latent state if a future attack hits a falling player.
- **No unit-level test of the death pin** because `app.cpp` is not linked into
  the doctest target. The plan assigns that assertion to the `--s42-selftest`
  (Task 9), which will exercise the real app path; the reset helper itself is
  unit-tested.
- `reset_encounter` is inline in `app.h` so the doctest target can call it
  without linking `app.cpp`. Task 7 should extend that one function with its
  attack fields (ruling R2).

---

# Fix — ruling R7: a lethal fall cannot be caught

Status: **DONE**. Commit `59ae79a fix(app): a lethal fall cannot be caught`
(on top of `48f178e feat(app): player health, hurt and death`).

## What changed

1. **The catch window closes when a fall turns lethal
   (`app/src/player_state.cpp`).** In `player_take_damage`'s `Falling` branch,
   a hit that brings health to zero now sets both `pendingDeath = true` and
   `catchSecondsLeft = 0.0f`. The landing branch is unchanged and resolves the
   pending death to `Dead`; no grab press can re-attach and hide the land
   resolution. This closes the escape the review found (and supersedes the
   original report's "pending death can coexist with a catch" concern).
2. **Removed the unconsumed assignment.** The `Falling` branch no longer writes
   `heavyHit = heavy`; neither the landing's `Dead` entry nor the `Staggered`
   entry reads it, and `Staggered` + damage overwrites it through `enter_hurt`.
   The new test asserts `heavyHit` stays false after a heavy lethal fall hit.
3. **One attachment sweep.**
   `clear_fight_attachment(PlayerCombat&)` now clears band, `bandT`,
   weakpoint, charge, `strikeHit`, catch window and fall velocity once.
   `enter_hurt`, `enter_dead` and `reset_player_combat` all call it, so a new
   field cannot be missed in one copy. The reset test still passes unchanged.
4. **One encounter health constant.** `app.h` adds
   `inline constexpr float kEncounterMaxHealth = 100.0f;`; both
   `Encounter::health`'s default and `reset_encounter` use it.

No damage-table value, transition, clip mapping or reset semantics changed.

## Covering test

`app/tests/test_player_state.cpp`:
`a lethal fall cannot be caught: the grab press is ignored and the landing kills`

The fixture grabs the head band, drops the skeleton to force a real grip break
(`catchSecondsLeft == 0.6`), refills stamina, then applies a lethal heavy club
hit while `Falling`. It asserts health 0, stays `Falling`, `pendingDeath`,
`catchSecondsLeft == 0`, and `heavyHit` false. It then releases the held grab,
presses fresh inside the old window and asserts no `grabbed` event, still
`Falling`, no band; finally it falls to the ground and asserts `Dead` with
`landed`. The existing reset case and the previous lethal-fall landing case
were re-run alongside it.

### RED evidence (the fix temporarily removed, new test added)

```
TEST CASE: a lethal fall cannot be caught: the grab press is ignored and the landing kills
test_player_state.cpp:1113: CHECK( combat.catchSecondsLeft == Approx(0.0f) )
    values: CHECK( 0.6 == Approx( 0 ) )
test_player_state.cpp:1122: CHECK_FALSE( press.grabbed )
    values: CHECK_FALSE( true )
test_player_state.cpp:1123: CHECK( combat.mode == PlayerMode::Falling )
    values: CHECK( 1 == 4 )      # Grabbing
test_player_state.cpp:1124: CHECK( combat.band == ClimbBandId::Count )
    values: CHECK( 6 == 9 )      # Head
test_player_state.cpp:1132: CHECK( combat.mode == PlayerMode::Dead )
    values: CHECK( 1 == 7 )      # still Grabbing, never landed
test_player_state.cpp:1133: CHECK( last.landed )
    values: CHECK( false )
[doctest] test cases: 1 | 0 passed | 1 failed | 191 skipped
```

### GREEN evidence

Focused:

```
./app/build/sotc-app-tests --test-case="a lethal fall cannot be caught: the grab press is ignored and the landing kills,the R soft reset restores the player and Valus"
[doctest] test cases: 2 | 2 passed | 0 failed | 190 skipped
[doctest] assertions: 42 | 42 passed | 0 failed
```

Required command:

```
SOTC_LIB="$(pwd)/out/library" ctest --test-dir app/build -R unit_tests --output-on-failure
Test project .../app/build
    Start 1: unit_tests
1/1 Test #1: unit_tests .......................   Passed    9.70 sec

100% tests passed, 0 tests failed out of 1
```

Full ctest (also run):

```
1/2 Test #1: unit_tests .......................   Passed    9.72 sec
2/2 Test #2: windowed_selftest ................   Passed    0.22 sec
100% tests passed, 0 tests failed out of 2
```

The whole doctest suite reports 192 cases / 14,544 assertions passing.

## Files changed by the fix

- `app/src/player_state.cpp` — R7 catch-window clear, removed `heavyHit`
  assignment, shared `clear_fight_attachment`.
- `app/src/app.h` — `kEncounterMaxHealth` shared by the default and reset.
- `app/tests/test_player_state.cpp` — the covering catch-escape test.

