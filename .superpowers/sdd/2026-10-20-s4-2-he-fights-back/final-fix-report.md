# S4.2 whole-branch review — final fix report

**Status: DONE — Critical fixed, tests added (RED then GREEN), all named minors folded in, acceptance
re-run to `PASS`.**

Commits (on top of `af36c66 docs: S4.2 acceptance and results`):

| commit | subject |
|---|---|
| `9c5a9ff` | `fix(app): measure attack damage to the player capsule` |
| `248713f` | `test(app): pin the attack ranges and shockwave` |
| `abcbdfe` | `docs: S4.2 results follow-up` |

No assets, no commit of `AGENTS.md` (untracked by design), and `scripts/s4-acceptance.sh` /
`scripts/s41-acceptance.sh` are byte-unchanged. `git status --porcelain -uno` is clean.

---

## 1 · The bug

`app/src/app.cpp` (`apply_valus_impact`) built the closest point on the player's capsule from the
damage joint's **own XZ**:

```cpp
const float top = player.position.y + kPlayerCapsuleHeight;
const Vec3 nearest{point.x, std::clamp(point.y, player.position.y, top), point.z};
const float dx = point.x - nearest.x;   // always 0
const float dz = point.z - nearest.z;   // always 0
const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
```

Only the vertical separation was measured. Consequences:

- the club's `OptBone_hit` sits ~1.9 m up at the impact pose, inside the capsule's vertical span, so
  the measured distance was ~0 and the club dealt its full 45 to any grounded player the selection
  admitted into the 14 m front cone;
- the stomp foot is near ground height, so the stomp also measured ~0 and dealt 35; the 15-damage
  2.5–6 m shockwave band was unreachable and the 2.5 m direct ranges did nothing.

`--s42-selftest` could not see it because every target was placed at the joint's own XZ.

## 2 · RED evidence (against the old geometry)

### 2.1 Unit tests (`app/tests/test_valus.cpp`), old body in `player_capsule_distance`

```
/Users/felipe.dos.santos/.local/share/opencode/worktree/548673/s4-2-he-fights-back/app/tests/test_valus.cpp:252: ERROR: CHECK( player_capsule_distance(feet, beside) == doctest::Approx(3.0f) ) is NOT correct!
  values: CHECK( 0 == Approx( 3 ) )
/Users/.../test_valus.cpp:253: ERROR: CHECK_FALSE( player_capsule_distance(feet, beside) <= kValusClubHitRangeMeters ) is NOT correct!
  values: CHECK_FALSE( 0 <= 2.5 )
/Users/.../test_valus.cpp:264: ERROR: CHECK_FALSE( player_capsule_distance(feet, Vec3{2.51f, 0.0f, 0.0f}) <= kValusClubHitRangeMeters ) is NOT correct!
  values: CHECK_FALSE( 0 <= 2.5 )
/Users/.../test_valus.cpp:270: ERROR: CHECK_FALSE( player_capsule_distance(feet, Vec3{2.51f, 0.0f, 0.0f}) <= kValusStompDirectRangeMeters ) is NOT correct!
  values: CHECK_FALSE( 0 <= 2.5 )
/Users/.../test_valus.cpp:274: ERROR: CHECK_FALSE( player_capsule_distance(feet, Vec3{6.01f, 0.0f, 0.0f}) <= kValusStompWaveRangeMeters ) is NOT correct!
  values: CHECK_FALSE( 0 <= 6 )
/Users/.../test_valus.cpp:285: ERROR: CHECK( player_capsule_distance(feet, high) == doctest::Approx(expected) ) is NOT correct!
  values: CHECK( 0.8 == Approx( 3.10484 ) )
/Users/.../test_valus.cpp:286: ERROR: CHECK( player_capsule_distance(feet, high) > 3.0f ) is NOT correct!
  values: CHECK( 0.8 >  3 )
/Users/.../test_valus.cpp:287: ERROR: CHECK( player_capsule_distance(feet, high) > 0.8f ) is NOT correct!
  values: CHECK( 0.8 >  0.8 )

[doctest] test cases:   215 |   212 passed | 3 failed | 0 skipped
[doctest] assertions: 14750 | 14742 passed | 8 failed |
[doctest] Status: FAILURE!
```

The helper body was temporarily reverted to the exact old inline geometry for this capture, then
fixed; the test code did not change between RED and GREEN.

### 2.2 End-to-end (`--s42-selftest`), old geometry rebuilt into the viewer

```
s42-stomp damage=35 health=100->65 mode=hurt
sotc-viewer: s42-selftest: the club hit a target 4.000000 m from its tip (damage 45, mode hurt)
exit=1
```

So the new selftest probe independently rejects the old behaviour: it exits 1 before the `s42-miss`
line is printed, which fails the acceptance at the selftest step (and the anchored literal could
never match a probe that reported the club as a 45-damage hit).

## 3 · The fix

One shared helper, `player_capsule_distance(Vec3 feet, Vec3 point)`, in `app/src/collision.h` /
`app/src/collision.cpp`:

```cpp
float player_capsule_distance(Vec3 feet, Vec3 point) {
    // The closest point on the player's own segment, not on the point's XZ:
    // the horizontal offset is the reach that makes an attack miss.
    const Vec3 closest{feet.x, std::clamp(point.y, feet.y, feet.y + kPlayerCapsuleHeight),
                       feet.z};
    const float dx = point.x - closest.x;
    const float dy = point.y - closest.y;
    const float dz = point.z - closest.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
```

- `apply_valus_impact` now uses `player_capsule_distance(player.position, point)` and keeps its
  finite check, damage switch, ranges, windows and early gates exactly as they were.
- `strike_reaches_shell` (`app/src/player_state.cpp`) now returns
  `player_capsule_distance(feet, center) <= radius` (same finite/radius gates), so the weakpoint
  gate and the attack gate cannot measure different shapes. Its behaviour is bit-identical: it
  already clamped the shell centre's y and preserved x/z.
- `collision.h` is already included by `player_state.h`, so there is no new cycle.
- No damage number, range, window or selection rule moved.

## 4 · New tests

`app/tests/test_valus.cpp` (doctest), four cases / 14 assertions:

- **`attack range is measured to the player's capsule, not to its height`** — a point at foot
  height 3 m to the side: distance 3, club miss (`> 2.5`), stomp shockwave (`<= 6`).
- **`the club hit radius and the stomp cuts are capsule distances`** — 2.49 m vs 2.51 m for the
  club and the direct stomp 2.5 m cut; 5.99 m vs 6.01 m for the shockwave cut.
- **`a point above the feet is measured to the capsule top`** — 2.5 m up and 3 m aside is
  `sqrt(3² + 0.8²) = 3.10484`, not 3 and not 0.8.
- **`a club tip directly above a grounded player is a hit at 0.2 m`** — the accepted hit case
  (0 lateral, 1.9 m up) must stay a hit.

`ctest` (both suites): **2/2 passed**, `unit_tests` 215 cases / 14,750 assertions all green.

## 5 · Selftest line

`--s42-selftest` club/stomp phase, after the club impact pose is resolved, adds two probes through
the **production** `apply_valus_impact` (the HP/mode/i-frame fix-ups are scaffolding only):

- target 4 m from `OptBone_hit` → **0** damage (measured distance ≈ 4.005 m, outside 2.5 m);
- target 3 m from `Bone_TUMASAKI_L` at the impact pose → **15** damage (the 2.5–6 m shockwave),
  player left `Hurt`.

New stable line, printed between `s42-stomp` and `s42-club`:

```text
s42-miss club=0 stomp=15
```

`scripts/s42-acceptance.sh` asserts it anchored and verbatim:

```bash
if ! miss_line="$(one_line '^s42-miss club=0 stomp=15$' "s42-miss")"; then
  exit 1
fi
```

and prints it in the summary (`range split  s42-miss club=0 stomp=15`). Every previous literal is
unchanged: `stomp 35 health=100->65`, `club 45 health=65->20`, `arm 34`, `gate 0/34`, `reset`,
`stamina 39`. The status line is unchanged (not asked to gain the probe).

## 6 · Acceptance re-run (verbatim summary)

`caffeinate -dimsu ./scripts/s42-acceptance.sh` — exit 0.

```text
2026-10-21T15:52:52Z build: BpConvert.sln
Aprovado!  – Com falha:     0, Aprovado:   119, Ignorado:     0, Total:   119, Duração: 50 s - BpConvert.Tests.dll (net10.0)
1/2 Test #1: unit_tests .......................   Passed    9.81 sec
2/2 Test #2: windowed_selftest ................   Passed    0.20 sec
100% tests passed out of 2
2026-10-21T15:54:24Z timeline purity: s42-* event lines identical with and without stills
2026-10-21T15:54:44Z perf: load=8.443s frames240=10.631s marginal=9.15ms (~109.3 fps)
s42-acceptance: proxy        s42-proxy weakpoint=0 radius=1.955
s42-acceptance: proxy        s42-proxy weakpoint=1 radius=2.165
s42-acceptance: gate         s42-gate foot=0 head=34
s42-acceptance: arm          s42-arm band=right_arm weakpoint=1 damage=34
s42-acceptance: stomp        s42-stomp damage=35 health=100->65 mode=hurt
s42-acceptance: club hit     s42-club damage=45 health=65->20
s42-acceptance: range split  s42-miss club=0 stomp=15
s42-acceptance: death        s42-death clip=player_a_fall_damage_to_death mode=dead
s42-acceptance: reset        s42-reset health=100 stamina=100 mode=grounded valus=100
s42-acceptance: stamina      s42-stamina from_y=28.425 to_y=43.484 remaining=39
s42-acceptance: status       s42-status proxy=2 gate_foot=0 gate_head=34 arm_band=right_arm arm_weakpoint=1 arm_damage=34 stomp_damage=35 stomp_health=65 club_damage=45 club_health=20 death_clip=player_a_fall_damage_to_death reset_health=100 reset_stamina=100 reset_mode=grounded reset_valus=100 stamina_remaining=39 defeated=0
s42-acceptance: stills       out/s42/stomp-telegraph.png|8986294|254 out/s42/stomp-impact.png|9021505|254 out/s42/club-swing.png|8948574|255 out/s42/hurt.png|8981222|255 out/s42/death.png|10036130|255 out/s42/arm-weakpoint.png|9599465|255
s42-acceptance: perf         load 8.443 s, 240 frames 10.631 s, marginal 9.15 ms (~109.3 fps)
s42-acceptance: s41 status   s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
s42-acceptance: s4 status    s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=993,237,1485,751 pos=378.187,28.430,369.028 frames=423
s42-acceptance: s3 status    s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
s42-acceptance: PASS — build, both suites, s42 timeline (weakpoint proxies 1.955 m and 2.165 m, gate foot 0 / head 34, arm weakpoint 1 damage 34, stomp 35 health 100->65 hurt, club 45 health 65->20 hurt, range split club 0 at 4 m / stomp 15 at 3 m, death player_a_fall_damage_to_death, reset 100/100/grounded/Valus 100, stamina 28.425 -> 43.484 m leaving 39 of 100), six stills (2560x1440, span >= 30, > 5000 B), marginal 9.15 ms/frame (~109.3 fps), S4.1+S4+S3 regressions green
```

The S4.1 regression (which runs S4, which runs S3) printed its own
`s41-acceptance: PASS — …` and the nested `s4-status` / `s3-status` are byte-identical to the
recorded ones. The six still bytes are identical to the pre-fix recording (the fix does not move
the rendered timeline).

## 7 · `--s41-selftest`

Run directly under `caffeinate -dimsu`, exit 0:

```text
s41-grab frames=522 band=left_foot pos=380.454,28.425,369.175
s41-strike player_px=1147,661,273,93
s41-strike swing=1 damage=34 health=100->66 weakpoint=0
s41-strike swing=2 damage=20 health=66->46 weakpoint=0
s41-strike swing=3 damage=34 health=34->0 weakpoint=0
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

34 / 20 / 34, exit 0, as required.

## 8 · Minors folded in

1. **S4.1 no-entity control / club** — `render_still` and `capture_control` gained
   `includeClub` (default true); `defeat-noentity` passes `false`, so `update_all_poses` skips
   `update_club_model` and the caller's `+100000` hide offset stands. Comment corrected.
   *Note:* the claimed visible symptom did not reproduce before the change — `update_club_model`
   places the club relative to `scene.meshes[rig.meshBegin].model`, which the control had already
   offset, so the club was already carried off-screen. Evidence: the S4.1 `defeat presence` stayed
   **42.229 %** (the recorded value) after the change. The explicit skip removes the reliance on
   that incidental ordering, which is the durable fix.
2. **Unread `PlayerClips::hurtHeavy` / `hurtHeavyEnd`** — fields and initialisers removed; the load
   gate still requires `player_a_damage_h_fly1` / `player_a_damage_h_fly_end` by name.
3. **`--s42-stills` help** — now lists six captures: stomp telegraph, stomp impact, club, hurt,
   death, arm-weakpoint.
4. **`app/README.md` club paragraph** — a missing clip or club glb is a load error (exit 2); a rig
   without `OptBone_sword` degrades to one stderr line and no club (behaviour unchanged, described
   accurately).
5. **`docs/notes/s42-results.md`** — the head/neck band retune is attributed to **S4.1's R10**; a
   review-follow-up paragraph records the fix, the new `s42-miss` literal and points here.
6. **`AGENTS.md`** (untracked, not committed) — the controls line now names the `W+D` chest→shoulder
   branch.

Deferred untouched as instructed: `valus_attack_cancelled`, the `playback.pinned` clearing in
`update_combat_clips`, the stale `s41-results.md` phase numbers, and the remaining ledger minors.

## 9 · Files changed

```
app/src/collision.h              +10   player_capsule_distance
app/src/collision.cpp            +11   player_capsule_distance body
app/src/player_state.cpp          ±6   strike_reaches_shell uses the helper
app/src/app.cpp                  +99/-16  fix, PlayerClips, control club, selftest probes
app/tests/test_valus.cpp         +63   the four range cases
scripts/s42-acceptance.sh        ±17   new anchored literal + summary/comment
app/src/main.cpp                  ±4   six stills in --s42-stills help
app/README.md                    ±19   club degradation + range split + event block
docs/notes/s42-results.md        ±17   S4.1 R10 attribution + follow-up
```

Acceptance log: `out/s42-acceptance-run.log` (gitignored, ends with the PASS line above).

## 10 · Concerns / notes

- **The selftest probe is scaffolding around the production gate, by design.** It sets
  HP/mode/i-frames before each `apply_valus_impact` call and restores the 65 HP post-stomp baseline
  after; the damage decision itself is the same function the interactive loop calls. Moving any
  damage/range/window number would still move the pinned literals.
- **The results note's recorded run predates this fix.** The note's §1–§7 measured values (perf,
  run stamps) are the original recording; the new follow-up paragraph names the fix and the new
  literal, and the README's verbatim event block was updated to the post-fix output. A full
  re-recording of the note was deliberately not done in this wave.
- **`s42-miss` is not a `s42-status` field.** The status literal was left untouched as required;
  the acceptance asserts the new line directly.
- **The club probe's measured distance is ~4.005 m, not exactly 4.0**, because the club tip sits
  above the capsule top by ~0.2 m. The assertion is a 0-vs-45 damage check, so the margin is large.
- **Minor 1's premise was inaccurate** (see §8.1); the change is still an improvement and no
  rendered bytes changed.
- The helper is a free function on the capsule axis (distance to the axis, not the surface). Both
  gates already compared to a radius; no radius is subtracted twice.
