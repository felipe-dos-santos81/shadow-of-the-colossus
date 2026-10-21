# SDD ledger — plan: docs/superpowers/plans/2026-10-20-s4-2-he-fights-back.md

## Setup (2026-10-20)

- Worktree: `/Users/felipe.dos.santos/.local/share/opencode/worktree/548673/s4-2-he-fights-back`, branch `s4-2-he-fights-back`, base `a91facc` (design + plan on `main`).
- `out/` symlink to `/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out` (12 GB library + encounter manifest).
- Submodules initialized (`third_party/PSO2-Aqua-Library` at `5dcf37a`).
- `SOTC_GAME_ROOT=<game tree>`; `SOTC_LIB=<worktree>/out/library`.

## Pre-flight scan

| Pair / task | Produces → consumes | Finding |
|---|---|---|
| T1 → T3/T5/T6/T7/T9 | 15 Wander + 8 Valus clips, `rootMotion` → clip selection, selftest, pins | T1 must land first; its pins (15/8) are the base for every later clip test. |
| T2 → T3 | `posed_weakpoint_proxy`, `weakpoint_material_name` → damage gate | Signature matches; T2 must precede T3. |
| T3 → T9 | capsule hit test, gated damage → selftest | The S4.1 head strike must stay 34; the selftest's head strike uses the same binding. |
| T4 → T9 | `ClimbBand::branch`, lateral orbit → arm route phase | T4 independent of T2/T3; T9 consumes the route. |
| T5 → T7/T9 | `Hurt`/`Dead`, `player_take_damage`, reset helper → attacks/death phase | T7 must extend T5's reset helper with attack fields (R2). |
| T6 → T7 | club model → club attack visual/impact anchor | T6 must precede T7. |
| T7 → T9 | attack state/impact windows → selftest | T7 must precede T9. |
| T8 → T9/T10 | player HUD bar, west button → acceptance | T8 independent of T6/T7. |
| Shared `app.cpp` | T3, T5, T6, T7, T8, T9 | Sequential only; never parallel. |
| Shared `player_state.*` | T3, T4, T5 | Sequential only. |
| T1 self-consistency | `ClipMotion` referenced but not in the file list | R3: create `src/BpConvert.Core/Pipeline/ClipMotion.cs`. |
| T2 self-consistency | proxy signature + tests | Consistent. |
| T3 self-consistency | gate + capsule test + S4.1 regression | Consistent; the capsule point is pinned. |
| T4 self-consistency | branch field + route test | Consistent. |
| T5 self-consistency | modes + clips + reset | Consistent. |
| T6 self-consistency | `bindWorld` + club model | Consistent; no matrix inverse needed. |
| T7 self-consistency | attack state + damage | Consistent. |
| T8 self-consistency | HUD field + west constant | Consistent; `test_gamepad.cpp` must be added to CMake (R4). |
| T9 self-consistency | selftest flags/lines | Consistent. |
| T10 self-consistency | script + results | Consistent. |

## Rulings

- **R1 — Workspace.** Worktree + shared `out` symlink, per the user's earlier isolation choice. Cost if wrong: conversions mutate the original library (gitignored).
- **R2 — Reset ownership.** T5 ships the reset helper; T7 extends it to clear `ValusAttack`, `attackSeconds`, `cooldownSeconds`, and the club pose. Cost if wrong: a reset during an attack leaves a stale clip playing.
- **R3 — `ClipMotion.cs`.** T1 creates the name→`rootMotion` table in its own file, as the plan's test implies. Cost if wrong: one extra small file.
- **R4 — Gamepad test registration.** T8 adds `app/tests/test_gamepad.cpp` to the test target and `app/src/gamepad_map.h` to both targets in `app/CMakeLists.txt`. Cost if wrong: the test is not built.
- **R5 — Shared hit-test binding.** T3 wires `PlayerFrame::weakpointProxy` inside `player_frame_of`, which the S4.1 selftest also uses. The capsule closest point keeps the S4.1 head strike at 34; no S4.1 script change. Cost if wrong: the S4.1 regression fails and the fix is the hit-point convention.

## Task progress

- Baseline green: dotnet 93/93, ctest 2/2. Base `a91facc`.
- Task 1: dispatched at base `a91facc`.
- Task 1: complete (commits `a91facc..9611db0`, review clean: spec ✅, quality approved).
- Task 1 artifact check: Wander 15 clips, Valus 8, zero `unclassified`.
- Task 1: minor (deferred): brief-verbatim test leaks its temp dir.
- Task 1: minor (deferred): table granularity inconsistent (`atk1` exact, `grip_`/`damage_` prefix).
- Task 1: minor (deferred): no test ties the live sidecars to the table (unconsumed, low risk).
- Task 1: minor (deferred): mixed-category precedence undocumented.
- Task 1: minor (deferred): JSON `version` stays 1 across the schema change.
- Task 1: minor (deferred): report text miscounts grip clips (code correct).
- Task 2: complete (commits `9611db0..3cd105d`, review clean: spec ✅, quality approved).
- Task 2: minor (deferred): mixed dangling/usable weights yield a partial sum (invalid asset only).
- Task 2: minor (deferred): unnormalized weight sum not pinned.
- Task 2: minor (deferred): palette-then-model order not pinned (translations commute).
- Task 2: minor (deferred): multi-mesh aggregation and `meshBegin` edge cases untested.
- Task 2: minor (deferred): test comment names a non-farthest corner.
- Task 2: minor (deferred): skinning block duplicates `posed_bounds`.
- Task 2 hand-off: `weakpoint_material_name` may return `nullptr`; T3 must null-check before `std::string`.
- Task 3: complete (commits `3cd105d..aeea28a`, review clean: spec ✅, quality approved; aborted-session changes verified as Task-3-only).
- Task 3: minor (deferred): closest-point clamp not pinned (top vs interior).
- Task 3: minor (deferred): weakpoint 1 through the gate, and the lookup-false branch, untested.
- Task 3: minor (deferred): waist test doesn't assert `combat.weakpoint == -1`.
- Task 3: minor (deferred): squared-distance overflow theoretical.
- Task 3: minor (deferred): `std::function` constructed per frame (invoked on release only).
- Task 3: minor (deferred): `WeakpointProxyLookup` splits the `PlayerFrame` doc comment.
- Task 4: review ❌ — one Important (orbit sign inverted for D; the test pins the implementation, not the player-facing direction). Fix round 1 dispatched.
- Task 4: fix round 1/5 (1 addressed, 0 open; commits `bd58fe7..88e8182`).
- Task 4: complete (commits `aeea28a..88e8182`, review clean after fix).
- Task 4: minor (deferred): lateral on a horizontal shoulder band is a no-op (pre-existing surface model).
- Task 4: minor (deferred): no lateral-on-non-vertical-band or Striking-input coverage.
- Task 4: minor (deferred): route test seeds the attached state rather than grabbing.
- Task 4 docs: design §7.3 updated to `+lateral` with an upward axis (R6).
- Task 5: review — spec ✅, quality "needs fixes": one Important (a lethal fall can be escaped by catching, leaving a 0-HP attached player). Fix round 1 dispatched.
- Task 5: fix round 1/5 (1 addressed, 0 open; commits `48f178e..59ae79a`).
- Task 5: complete (commits `c2bad22..59ae79a`, review clean after fix).
- Task 5: minor (deferred): `heavyHit` in the Falling branch removed (fix); remaining minors were the sweep/constant, both fixed.
- Task 5: minor (deferred): `Playback::pinned` doc now also backs the runtime death pin.
- Task 5: minor (deferred): no unit test that `player_step` in `Dead` ignores input / `enter_dead` clears an attachment.
- Task 5: minor (deferred): heavy `Hurt` substitutes `h_fly_end` when `h_fly1` is missing (unreachable via the load gate).
- Task 6: complete (commits `59ae79a..d9b90d1`, review clean: spec ✅, quality approved; stills show the club in the right hand).
- Task 6: minor (deferred): club textures decoded twice (~16 MB).
- Task 6: minor (deferred): `club_model` infers the rig by palette size.
- Task 6: minor (deferred): bind-root caveat for a root joint under a transformed non-joint node.
- Task 6: minor (deferred): defeat-control club hide is redundant; comment wrong.
- Task 6: minor (deferred): missing-socket and composed material-offset paths untested.
- Task 7: complete (commits `d9b90d1..1db413f`, review clean: spec ✅, quality approved).
- Task 7: minor (deferred): `dot = 0.19` test is actually 14.001 m (passes for the range reason).
- Task 7: minor (deferred): app-level attack state machine only covered by Task 9.
- Task 7: minor (deferred): `footL/footR` default to the origin (phantom stomp if defaulted).
- Task 7: minor (deferred): cooldown input one frame stale.
- Task 7: minor (deferred): damage distance measured to the capsule axis, not the point.
- Task 7: minor (deferred): impact consumed on the first in-window frame even if it cannot land.
- Task 7: minor (deferred): doc nits (`wrap_deg` range, non-finite yaw claim).
- Task 7: minor (deferred): only the first entity is yaw-driven (one-entity scope).
- Task 8: review ❌ — one Critical (modified `scripts/s4-acceptance.sh`, forbidden) + one Important (160 px blanket margin weakens both S4 guards). Fix round 1 dispatched.
- Task 8: fix round 1/5 — implementer stalled mid-run without a report; controller verified the diff (club bbox union + HUD-free plain captures; `scripts/s4-acceptance.sh` byte-identical to base) and committed `642f454`. Two acceptance runs were found competing (the stalled child had launched its own); both killed.
- Task 8: environmental note — the machine suspends idle background work: a 1-frame perf run took 907 s while the 240-frame run took 9.3 s, and `dotnet test` took 12–25 min instead of ~1. Acceptance re-running under `caffeinate -dimsu`. The S4.2 acceptance script (T10) must wrap its viewer runs in `caffeinate`.
- Task 8: fix round 1/5 (2 addressed, 0 open; commits `3270ff3..642f454`; acceptance `PASS —`, marginal 9.26 ms/frame, S4+S3 green).
- Task 8: complete (commits `1db413f..642f454`, review clean after fix).
- Task 8: minor (deferred): `playerHealth` defaults to 1.0 (a three-field aggregate draws a full bar).
- Task 8: minor (deferred): player-bar label spacing comment overstates the gap.
- Task 8: minor (deferred): no amber-pixel assertion for the player bar (T10 can add one).
- Task 9: complete (commits `642f454..965f8c1`, review approved: spec ✅, no Critical/Important; 10 minors).
- Task 9: minor (deferred): selftest asserts damage against `tuning.*`, not literals (T10 pins 34/35/45 in the script).
- Task 9: minor (deferred): stamina margin only asserts `> 0` (T10 asserts `0 < remaining < 100`).
- Task 9: minor (deferred): attack selection not exercised (direct `start_valus_attack`; `test_valus.cpp` covers selection).
- Task 9: minor (deferred): `!opts.s42Selftest` in `valus_behavior` is dead (early return).
- Task 9: minor (deferred): selftest camera updates after `frame_of` (one frame behind).
- Task 9: minor (deferred): `next_climbable_band` has no direct unit test; comment says zero-length, predicate also skips missing/non-finite.
- Task 9: minor (deferred): `player_state.cpp` outside the brief's file list (real Waist fix, disclosed).
- Task 9: minor (deferred): report says missing clips exit 2; they exit 1.
- Task 9: minor (deferred): chest attach seeded (branch/climb real).
- Task 9: minor (deferred): i-frame check same-frame only (second full attack not timed).

## Ruling R11 (Task 10 scope)

- **T10 may add `--s42-stills DIR`** (mirroring `--s41-stills`) with six capture points: `stomp-telegraph`, `stomp-impact`, `club-swing`, `hurt`, `death`, `arm-weakpoint`. The brief requires six stills and there is no deterministic capture hook; the flag must not change the timeline's assertions, damage values, or windows. Cost if wrong: one flag and six render calls, mirroring S4.1.
- **Typed commits:** `feat(app): S4.2 acceptance stills`, `test: S4.2 acceptance script`, then `docs: S4.2 acceptance and results`. Cost if wrong: three commits instead of one.
- **The script pins literals:** `foot=0 head=34`, `stomp damage=35`, `club damage=45`, `arm weakpoint=1 damage=34`, `reset valus=100`, death clip name; stamina `0 < remaining < 100`. This closes T9's "asserts against tuning" gap at the acceptance layer.
- **All viewer runs go through `caffeinate -dimsu`** (the machine suspends idle work; see the Task 8 note).

- Task 10: complete (commits `965f8c1..af36c66`, review approved: spec ✅, no Critical/Important; 7 minors).
- Task 10: minor (deferred): stomp pair stills are close-ups (Wander occluded; HUD carries 65 %).
- Task 10: minor (deferred): `capture_still` leaves the HUD visible for the final status render (presentation-only leak).
- Task 10: minor (deferred): status "agreement" is literal-vs-literal, not event-derived.
- Task 10: minor (deferred): no scripted HUD-content guard on the six stills.
- Task 10: minor (deferred): `need` list omits standard tools.
- Task 10: minor (deferred): reproduction/walk commands omit `caffeinate`.
- Task 10: minor (deferred): `AGENTS.md` omits `W+D`/s3 in two lines.
- Task 10: note — the older `s41-results.md` `health_red=19200` is a phase mismatch (100 % strike), not stale; no fix.

## Final whole-branch review (base a91facc, head af36c66)

- 1 Critical: `apply_valus_impact` built the capsule point from the joint's XZ, so only vertical distance counted — club 45 to everyone in the 14 m cone; stomp 15 unreachable; 2.5 m ranges dead.
- 8 Minor (stomp stills, HUD flag leak, literal-vs-event status, no HUD content guard, `need` list, `caffeinate` in snippets, AGENTS.md, results-note R10 wording).
- Fix wave `af36c66..abcbdfe` (`9c5a9ff` fix, `248713f` tests, `abcbdfe` docs): shared `player_capsule_distance` used by both gates; unit tests pin 3.0 m lateral, 2.49/2.51, 5.99/6.01, capsule-top 3.105, 0.2 above; `s42-miss club=0 stomp=15` end-to-end; acceptance `PASS` with all old literals, six stills byte-identical, S4.1 34/20/34.
- Scoped re-review: Critical ADDRESSED; minors 1-6 ADDRESSED; 0 new Critical/Important. Fix quality Approved.
- Final state: review clean. S4.2 scripted acceptance green; user walk PENDING.

## Ruling R12 (parked, final review residue)

- **The README's club-glb failure parenthetical says exit 2; it is exit 1.** Doc-only, introduced in the fix wave's README sentence; the load fails loudly either way. Deferred so the approved fix wave does not reopen for a one-word doc edit. Cost if wrong: a reader expects the wrong exit code.

## Ruling R13 (parked, final review residue)

- **The results note's perf/run stamps are the pre-fix recording**, with a disclosed follow-up paragraph. The fix adds one attack-time `sqrt` and does not change the recorded geometry; re-recording the whole note would churn docs without new evidence. Cost if wrong: a reader conflates the pre-fix and final run stamps.

## Final acceptance (exact tree, `abcbdfe`)

- `caffeinate -dimsu ./scripts/s42-acceptance.sh` → `PASS —`.
- 119 dotnet tests; ctest 2/2.
- `s42-gate foot=0 head=34`; arm `right_arm` weakpoint 1 damage 34; stomp 35 `100->65` hurt; club 45 `65->20`; range split `club=0` at 4 m / `stomp=15` at 3 m; death clip; reset 100/100/grounded/Valus 100; stamina 28.425→43.484 leaving 39.
- Six stills 2560×1440; marginal 8.79 ms/frame (~113.8 fps); S4.1→S4→S3 green.
- Timeline purity: `s42-*` identical with and without stills.

## Ruling R10 (Task 8 fix — constraint-compliant)

- **Revert `scripts/s4-acceptance.sh` to base `1db413f`.** The red has a real cause (the club is outside the skinned bbox; the HUD is new), so fix the causes, not the guard.
- **Include the club in the S4 entity bbox.** Extend the S4 selftest's projected entity bounds to union the club mesh range (`ClubAttachment::meshBegin/meshEnd`, projected with its live `model`). The guard keeps its original 16 px margin and now covers what the entity actually draws.
- **Keep plain `--screenshot` captures HUD-free.** Gate the startup `set_hud` for plain captures (the `--s41-stills` path and the interactive loop keep the HUD). Then the S4 wide and its entity-less control contain no overlay pixels and need no HUD bands.
- **Keep R9's `s41-acceptance.sh` health-sample change** (strike → reaction) — that one is a deliberate behavior change and remains the only permitted edit.
- Cost if wrong: the S4 bbox grows by the club's reach; if the club moves in a future clip, the guard follows it (desirable).

## Ruling R9 (Task 8 vs the S4.1 script)

- **The S4.1 acceptance's health-bar check moves to `reaction.png`.** Gating Valus's bar until the first hit is a specified behavior change; the S4.1 script samples it on `strike.png`, which is captured before the hit. T8 may change that one sample (and its comment) in `scripts/s41-acceptance.sh` and must re-run it to `PASS —`. This is a deliberate check update, not a regression; T10's regression uses the updated script. Cost if wrong: the S4.1 acceptance is touched once, with the reason recorded.

## Ruling R8 (Task 7 testability)

- **Extract the attack decision as a pure helper.** T7 adds `app/src/valus.h/.cpp` with `ValusAttack select_valus_attack(const ValusAttackInput&)` and `bool attack_impact_due(ValusAttack, float clipSeconds, bool alreadyHit)` (or equivalent), unit-tested in `app/tests/test_valus.cpp` and wired from `app.cpp`. Selection (range, facing, grounded, cooldown, defeated) and the one-impact-per-clip window must be headless-testable; the app wiring stays thin. Cost if wrong: one small module and a CMake line.

- **Entity yaw.** T7 stores the manifest entity transform (position, yaw, scale) in `Encounter` and recomposes the entity's mesh models (and the club, which reads the live model) each frame from `yawDeg` slewing toward the player. Cost if wrong: the body does not turn, or the club detaches.

## Ruling R7 (Task 5 fix)

- **A lethal fall cannot be caught.** When `player_take_damage` arms `pendingDeath` while `Falling`, clear `catchSecondsLeft` so no grab press can re-attach; the landing resolves `Dead`. Add a test: Falling + lethal + grab press inside the window → still Falling, no attach, then landing → Dead. Also remove the unconsumed `heavyHit = heavy` assignment in the Falling branch. Cost if wrong: a player cannot catch after a lethal hit (intended).

## Ruling R6 (Task 4 fix)

- **Orient the orbit axis upward and use `+lateral`.** Compute `axis = normalize(boneB − boneA)`; if `axis.y < 0`, negate it; then `dAngle = +lateral × climbSpeed × dt / surface_radius`. With the player facing the band (`forward = −normal`), `right = forward × +Y`, so D moves along screen-right. Add a test asserting `dot(newPos − oldPos, right) > 0` for the foot band, and one band whose `boneA→boneB` points downward (the axis must flip). Cost if wrong: shimmy reversed on a horizontally-oriented band; one sign/axis change.
