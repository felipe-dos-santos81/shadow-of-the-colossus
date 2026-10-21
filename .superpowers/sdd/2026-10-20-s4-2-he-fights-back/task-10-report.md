# Task 10 report — S4.2 acceptance, stills, results

## Status

**DONE.** The stills hook is in, `scripts/s42-acceptance.sh` ran green end to end under
`caffeinate -dimsu`, the six stills are on disk, the S4.1→S4→S3 regression is green, the results
note and README are written, and the three typed commits are in. The user's interactive walk is
the only remaining gate and is recorded as PENDING.

## What I implemented

### `--s42-stills DIR` (ruling R11)

Mirrors `--s41-stills`:

- `AppOptions::s42StillsDir` (`app/src/app.h`), `--s42-stills DIR` parsed in `main.cpp`, a
  usage line, and the same precondition style: `--s42-stills needs --s42-selftest`.
- Six capture points in the S4.2 timeline, each printing `s42-still phase=<name> path=<path>`
  then a 2560×1440 PNG:
  1. `stomp-telegraph` — the `StompL` clip at `attackSeconds ≈ 0.467 s`, five 1/60 s steps before
     the `[0.55, 0.70]` window;
  2. `stomp-impact` — immediately after the stomp impact (`Hurt`, 65 HP);
  3. `club-swing` — the club clip at `attackSeconds ≈ 0.367 s`, five steps before `[0.45, 0.65]`;
  4. `hurt` — immediately after the club impact (`Hurt`, 20 HP);
  5. `death` — after the death clip has been asserted pinned **and** played to its last frame
     (the stronger of the two "pinned" assertions);
  6. `arm-weakpoint` — right after the arm-strike assertion, before the reaction wait.
- Rendering reuses the S4.1 pattern: `update_combat_clips` with no input,
  `update_all_poses(..., 0.0f, club)`, `apply_player_transform`,
  `renderer.set_hud(hud_state(true))`, `renderer.render_frame(..., true)`, `write_screenshot`.
- Purity: every rig's playback clock (`index`, `time`, `playing`, `pinned`) is snapshotted before
  the capture and restored after the render, so no capture can move the timeline. A still whose
  telegraph point was missed would throw when stills were requested.
- No assertion, damage number, window or gate was changed.

### `scripts/s42-acceptance.sh`

433 lines, the S4.1 house pattern (timestamped steps, `fail` with the `s42-acceptance:` prefix,
tee'd run log). Steps:

1. `dotnet build BpConvert.sln`, `cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release`,
   `cmake --build app/build -j`.
2. `dotnet test tests/BpConvert.Tests -v q`, `ctest --test-dir app/build --output-on-failure`.
3. Clean `out/s42/`; run `--s42-selftest --s42-stills out/s42` under `caffeinate -dimsu`; assert
   exit 0; parse every line to literals:
   - two `s42-proxy` lines, weakpoints 0 and 1, radii in (0.5, 5.0);
   - `s42-gate foot=0 head=34`, `s42-arm band=right_arm weakpoint=1 damage=34`;
   - `s42-stomp damage=35 health=100->65 mode=hurt`, `s42-club damage=45 health=65->20`;
   - `s42-death clip=player_a_fall_damage_to_death mode=dead`;
   - `s42-reset health=100 stamina=100 mode=grounded valus=100`;
   - `s42-stamina from_y=28.425 to_y=43.484 remaining=<n>` with `0 < n < 100`;
   - one `s42-status` with every field cross-checked against the event lines.
   Then it re-runs the selftest **without** stills and diffs the `s42-*` lines (still lines
   removed): the diff must be empty.
4. Six stills: PNG magic, > 5000 B, 2560×1440 from the `s1-screenshot` line, span ≥ 30 from the
   `s1-pixels`/`s1-screenshot` pair; exactly one `s42-still` line per phase and six total.
5. Perf: `--frames 1` vs `--frames 240` under `caffeinate`, assert the 240-frame run is slower,
   both wrote a capture, and the marginal is ≤ 16.7 ms/frame.
6. `caffeinate -dimsu ./scripts/s41-acceptance.sh`, assert its final `s41-acceptance: PASS —`,
   and quote the nested S4 and S3 statuses. Neither frozen script was modified.
7. Summary block with the glb sha256s, every event line, the stills/bytes/spans, perf and the
   regressions, ending in `s42-acceptance: PASS —`.

## The `s42-*` output, with and without stills

Command (the script's step 3):

```bash
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s42-selftest [--s42-stills out/s42]
```

The ten event/status lines are byte-identical. `diff` of the filtered outputs produced **no
differences** (`out/s42/events.diff` is empty):

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
s42-status proxy=2 gate_foot=0 gate_head=34 arm_band=right_arm arm_weakpoint=1 arm_damage=34 stomp_damage=35 stomp_health=65 club_damage=45 club_health=20 death_clip=player_a_fall_damage_to_death reset_health=100 reset_stamina=100 reset_mode=grounded reset_valus=100 stamina_remaining=39 defeated=0
```

The only additions in the with-stills log are the six `s42-still` lines and three `s2-anim`
lines: `update_combat_clips` in the capture selects the current mode clip one timeline step
earlier than the next `step_frame` would (the S4.1 one-frame clip lag); the clocks are restored
and the next step makes the same selection, so state and every `s42-*` value are unchanged. This
is recorded in the results note.

## The six stills

All under `out/s42/`, 2560×1440, PNG, HUD visible:

| path | bytes | span | phase |
|---|---:|---:|---|
| `out/s42/stomp-telegraph.png` | 8,986,294 | 254 | 5 steps before the stomp window; amber bar 100 % |
| `out/s42/stomp-impact.png` | 9,021,505 | 254 | after the stomp; amber bar 65 % |
| `out/s42/club-swing.png` | 8,948,574 | 255 | 5 steps before the club window; amber 100 % |
| `out/s42/hurt.png` | 8,981,222 | 255 | after the club; amber 20 % |
| `out/s42/death.png` | 10,036,130 | 255 | pinned death last frame; amber absent (0 HP) |
| `out/s42/arm-weakpoint.png` | 9,599,465 | 255 | right-arm shell rune after the strike; stamina 43 |

An out-of-band pixel count of the amber bar (design rect 240×8 at x=24, y=652) measured 480 px
(100 %), 312 px (65 %), 96 px (20 %) and 0 px in the four relevant stills — the HUD numbers match
the timeline. The stomp/club stills are close-ups at the ruled 1 m standoff, so Wander is largely
occluded by the foot/club; the HUD carries the damage evidence and the user's walk judges the
telegraph.

## The acceptance's final summary, verbatim

```text
s42-acceptance: PASS — build, both suites, s42 timeline (weakpoint proxies 1.955 m and 2.165 m, gate foot 0 / head 34, arm weakpoint 1 damage 34, stomp 35 health 100->65 hurt, club 45 health 65->20 hurt, death player_a_fall_damage_to_death, reset 100/100/grounded/Valus 100, stamina 28.425 -> 43.484 m leaving 39 of 100), six stills (2560x1440, span >= 30, > 5000 B), marginal 9.46 ms/frame (~105.7 fps), S4.1+S4+S3 regressions green
```

Key recorded numbers:

- dotnet: **119 passed**, 0 failed (49 s); ctest: **2/2 passed** (10.14 s).
- perf: `--frames 1` **8.866 s**, `--frames 240` **11.127 s**, marginal **9.46 ms/frame**
  (~105.7 fps) — inside the 16.7 ms budget.
- S4.1 regression: `s41-acceptance: PASS —` (its own marginal 6.39 ms), `s41-status
  grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1`.
- S4: `s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick
  spawn=data-position+builder-yaw feet=ok entity_bbox=993,237,1485,751
  pos=378.187,28.430,369.028 frames=423`; S3: `s3-status instances=30
  spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240`.
- Run window: `2026-10-21T14:55:48Z`–`15:02:48Z`; exit 0.

## Files changed

| Commit | File | Change |
|---|---|---|
| `1bc3297` feat(app): S4.2 acceptance stills | `app/src/app.h` | `AppOptions::s42StillsDir` |
| | `app/src/main.cpp` | `--s42-stills` usage/parse/precondition |
| | `app/src/app.cpp` | the six capture points, `kS42TelegraphLeadFrames`, snapshot/restore helper |
| `99cb152` test: S4.2 acceptance script | `scripts/s42-acceptance.sh` | new, 433 lines, executable |
| `af36c66` docs: S4.2 acceptance and results | `docs/notes/s42-results.md` | new results note |
| | `app/README.md` | S4.2 status, sdl3_ttf, controls, club, `--s42-*`, event lines |

`AGENTS.md` was created at the workspace root with the S4.2 status and left **untracked**
(`git status` shows it; it was absent before, so I created it rather than edited it). No assets
are committed. `scripts/s41-acceptance.sh`, `scripts/s4-acceptance.sh` and
`scripts/s3-acceptance.sh` are byte-unchanged relative to `965f8c1`.

## Self-review findings

1. **Purity is proven by construction and by diff.** The capture restores every rig's
   `index/time/playing/pinned`; the acceptance independently re-runs without stills and diffs the
   filtered event lines (empty). The three extra `s2-anim` lines are log-only and documented.
2. **Still timing.** The stomp/club telegraph points land 5 frames before their windows; if a
   future window retune made the point unreachable, the run fails with a named error instead of
   silently writing no still.
3. **Death still point.** I capture after the last-frame pin assertion, not the immediate pin;
   that is the later and stronger reading of "asserted pinned", and it shows the corpse rather
   than the first death frame. The brief's other five points are literal.
4. **Script pins literals.** All damage/health/window/route values are literals in the script,
   not read from `tuning.*`, closing Task 9's deferred gap. The status line is cross-checked
   field by field.
5. **Caffeinate everywhere.** The selftest, both perf runs and the whole S4.1 regression run under
   `caffeinate -dimsu`; the outer run was also under `caffeinate` for the dotnet work.
6. **Build equivalence.** The acceptance built the source at the recorded commit content; after
   writing the docs I rebuilt the viewer (only the usage-string edit was reverted, so the content
   is identical) and re-checked both usage guards (exit 2 messages).
7. **No frozen-script diffs.** `git diff 965f8c1..HEAD -- scripts/s41-acceptance.sh
   scripts/s4-acceptance.sh scripts/s3-acceptance.sh` is empty.
8. **Docs consistency.** The s41 PASS line, s41/s4/s3 statuses and every still byte count quoted
   in `s42-results.md` were checked against `out/s42-acceptance-run.log` verbatim.

## Concerns

1. **The attack stills are close-ups.** At 1.0 m the raised foot and the club fill the frame and
   Wander is small/occluded. The HUD proves 65 %/20 %/absent; telegraph readability is the user's
   walk. This is a consequence of the ruled 1 m standoff, not a capture bug.
2. **The selftest drives the attacks.** Autonomous selection is unit-tested in `test_valus.cpp`
   but not exercised by the timeline; the target placement is scaffolding (capsules cleared for
   those steps). The results note states this.
3. **The ARM still is post-strike.** As specified, `arm-weakpoint` is captured after the strike,
   so Wander is already Falling and the rune is the evidence; it is not a "clinging at the arm"
   portrait.
4. **A prior-note inconsistency, not fixed.** `docs/notes/s41-results.md` §5 records
   `s41-hud-bars health_red=19200`, while the current (and self-consistent) regression prints
   `health_red=12672` — the 66 % Valus bar, matching its own `s41-reaction-hud
   health_changed=12672`. The S4.2 task did not touch the S4.1 note; the S4.1 acceptance guard
   (≥ 500) passes either way. Worth a one-line correction in a future docs pass.
5. **Gamepad hardware remains unverified**; the west-not-south mapping is unit-tested only.
6. **`AGENTS.md` did not exist.** The brief said to update it as an untracked file; I created it
   with the S4.2 state and left it untracked. If a canonical older version lives outside this
   worktree, it should be merged rather than replaced.

## Verdict

The S4.2 scripted acceptance is green end to end, the stills are deterministic observations of
that timeline, the S4.1/S4/S3 regressions are unchanged and green, and the remaining gate is the
user's interactive walk.
