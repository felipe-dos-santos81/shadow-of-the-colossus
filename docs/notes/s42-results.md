# S4.2 acceptance results (2026-10-21) — he fights back

Spec milestone S4.2 (`docs/superpowers/specs/2026-10-20-s4-2-he-fights-back-design.md` §2):
**"the shell-derived weakpoints are the only damage, the body is climbable around and into the arm
branch, Valus turns, stomps and swings his club, Wander has health and dies, and `R` resets the
encounter."** The loop is now a fight: damage is gated on the two shipped shells, the right-arm
route exists, Valus attacks on pinned windows, and Wander can be hurt and killed.

**Status: the app/converter acceptance is PASS (script) — the user's walk is PENDING.**
`scripts/s42-acceptance.sh` ran green end to end: both builds, both suites, the deterministic
`s42-*` timeline parsed field by field (and re-run without stills to prove the captures do not
touch it), six HUD-bearing stills with PNG/size/span guards, the perf measurement, and the
unchanged S4.1 acceptance (which runs S4, which runs S3). The milestone's done-line is **not
claimed here**: telegraph readability, feel, scale, HUD legibility, cursor freedom, gamepad
hardware and fps are the user's walk (§9).

| | |
|---|---|
| Date | 2026-10-21 (local −03; the recorded run is stamped `2026-10-21T14:55:48Z`–`15:02:48Z`) |
| Machine | Apple M3 Max, macOS 27.0.1 (26A434), arm64; 1280×720 points → 2560×1440 px captures |
| Repo / branch | `sotc-native`, worktree `s4-2-he-fights-back`; base `965f8c1`, this note after it |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<worktree>/out/library` (symlinked 12 GB library) |
| Encounter manifest (gitignored) | `out/re/stage/encounter.manifest` — **4,391 B**, sha256 `49fba07a48b2b75e6a0ccae608e9111de0c9753b17aea478f4945116d982065f`, 30 instances + 1 entity + 1 spawn |
| Wander glb (gitignored) | **5,583,356 B**, sha256 `e945e0c1d0ee5f84094ebb7d15ae85e70803f5186790c99de286d54fc6596d7c`, **15 clips** |
| Valus glb (gitignored) | **38,482,972 B**, sha256 `7db8fbaaac1b96762c239fd00af311992a6951e8ff690a3f7097f0e792f0678a`, **8 clips** |
| Club glb (gitignored) | **613,576 B**, sha256 `93b97e2371f25249506a9c00b462abaa4a0bab4a08cca661c479b1080d4b469d` |
| Acceptance script | **`scripts/s42-acceptance.sh`** (bash 3.2; the run used `/bin/bash` 3.2.57) |
| Run log (gitignored) | `out/s42-acceptance-run.log` — ends `s42-acceptance: PASS —` |
| S4.1 regression log (gitignored) | `out/s42-s41-regression.log` — ends `s41-acceptance: PASS —` |
| Captures (gitignored) | `out/s42/` (six stills + perf logs) |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<worktree>/out/library
caffeinate -dimsu ./scripts/s42-acceptance.sh    # exit 0; everything below is from
                                                 # out/s42-acceptance-run.log
```

The script builds `BpConvert.sln` and `app/` (Release), runs both suites, deletes and recreates
`out/s42/` (so no stale PNG can mask a failed write), runs `--s42-selftest --s42-stills out/s42`,
parses every `s42-*` line to literals, re-runs `--s42-selftest` without stills and diffs the event
lines, checks the six stills (PNG magic, > 5000 B, 2560×1440, the viewer's own non-uniformity
span, exactly one `s42-still` line each), measures `--frames 1` vs `--frames 240`, and finally
runs `scripts/s41-acceptance.sh` unchanged. Every viewer invocation is wrapped in
`caffeinate -dimsu` (the machine suspends idle background work; an unwrapped 1-frame run has been
observed at 907 s).

The individual steps, for reproduction (the script is the source of truth):

```bash
dotnet build BpConvert.sln -v q
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j
dotnet test tests/BpConvert.Tests -v q
ctest --test-dir app/build --output-on-failure
app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb \
    --s42-selftest --s42-stills out/s42
./scripts/s41-acceptance.sh             # the S4.1 + S4 + S3 regressions, unchanged
```

## 2 · What the timeline proves

`--s42-selftest` composes the encounter manifest, pins the entity to one stand frame, and runs the
whole S4.2 loop through fixed 1/60 s steps with no window input. The verbatim event lines:

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

- **Both shells resolve.** `posed_weakpoint_proxy` (the same function the strike binding calls)
  resolves weakpoint 0 (`weakpoint_03`, forehead) at radius **1.955 m** and weakpoint 1
  (`weakpoint_04`, right upper arm) at **2.165 m**, both finite and inside the acceptance's
  (0.5, 5.0) bounds.
- **The gate is earned damage.** A full-charge strike on the foot band (weakpoint `-1`, outside
  every shell) deals **0** — a real clang that still breaks the grip — while the same charged
  strike on the forehead deals **34**, 100 → 66.
- **The arm route is reachable.** Chest top → `RightShoulder` (the `W+D` branch) → `RightArm`, to
  `bandT = 1.0`, where the right-arm shell is in range: **weakpoint 1, damage 34**, 66 → 32. The
  route is the production `player_step` path, not a teleport; only the *entry* attach is the S4.1
  scaffolding.
- **The stomp lands once.** The player is placed 1.0 m from the posed `Bone_TUMASAKI_L` at the
  window pose, `StompL` starts, and the pinned `[0.55, 0.70]` window deals **35** (100 → 65,
  `Hurt`). The same impact re-applied on the same frame is blocked by the i-frame window.
- **The club lands.** The player is placed under the posed `OptBone_hit`; the `[0.45, 0.65]`
  window deals **45**, 65 → 20, `Hurt`.
- **Wander dies on the death clip.** One lethal club (45 HP set-up) takes him to 0, and
  `player_a_fall_damage_to_death` is selected pinned, played once to its own duration, and held on
  its last frame (the selftest asserts mode `Dead`, pinned, time at duration).
- **`R` resets the whole encounter.** Health 100, stamina 100, `Grounded`, Valus 100,
  `defeated=0`; the camera snaps behind the spawn and the entity returns to `minotaur_a_stand`.
- **The full climb has margin.** After the reset the run walks from the spawn into the nearest leg
  capsule, walks the foot-band leg, grabs `left_foot`, and climbs `E+W` to the top of `Head` with
  no teleport. The real rise is **28.425 → 43.484 m (15.059 m)** and **39** of the 100-point pool
  remain. That 39 is the measured margin at the sigil this milestone's acceptance asks for; it is
  not asserted against tuning (the script pins `0 < remaining < 100` and records the value).

The status line is one field per phase and agrees with every event line; the acceptance checks
both the literals and the agreement, so a status schema drift cannot pass silently.

**Whole-branch review follow-up.** `apply_valus_impact` used to build the closest point from the
damage joint's own XZ, so the club and the stomp measured only vertical separation and ignored
horizontal distance: every grounded player in the club's 14 m cone took 45, and the 2.5–6 m
shockwave band was unreachable. The distance now comes from `player_capsule_distance`
(`app/src/collision.cpp`), the same function the weakpoint gate uses, and the timeline's club/stomp
phase adds two production-path probes: a club tip 4 m from the target (**0** damage) and a stomp
foot 3 m away (**15**, the shockwave). The new literal is `s42-miss club=0 stomp=15`, printed
between `s42-stomp` and `s42-club`; every line above is unchanged by the fix. The post-fix
acceptance re-run is `PASS`; the details are in
`.superpowers/sdd/2026-10-20-s4-2-he-fights-back/final-fix-report.md`.

## 3 · Provenance

**Weakpoints — `derived-mesh`.** The strike spheres are computed from the converted glb's own
shells, selected by material name, never from a band flag:

| Weakpoint | Shell | Verts | Dominant weight | Runtime sphere |
|---|---|---|---|---|
| forehead | `weakpoint_03` (Mesh_8) | 730 | `Bone_KAO` 98.4 % | radius **1.955 m** |
| right upper arm | `weakpoint_04` (Mesh_4) | 204 | `Bone_JOWA_R` 87 % | radius **2.165 m** |

`posed_weakpoint_proxy` poses every shell vertex with the weighted palette sum plus the mesh model
transform, takes the mean as the centre, and the farthest distance from it to the eight corners of
the posed bounding box as the radius (`app/src/anim.cpp`). The two radii above are the values the
acceptance reads from the runtime; the sphere is labelled `derived-mesh` for the vertices and
`builder-tuned` only for the choice that the bbox radius is the hurt volume.

**Club — measured, not guessed.** `c01_minotaur_a_weapon.glb` is 613,576 B, one node, one unskinned
mesh (9,701 verts, 18,668 tris, material `colossus_01_legs`). It is attached identity to
`OptBone_sword` (`clubModel = entityMeshModel × palette[OptBone_sword] ×
bindWorld[OptBone_sword]`). The socket was chosen by measurement: the identity attachment puts the
club tip within **0.015 m** of `OptBone_hit`; `HAND_A_R` identity is 8.56 m off and
`OptBone_atk_weapon` 14.44 m off (design §6). The club-swing still shows it in the swing.

**Tuning — `builder-tuned`.** The design's pinned numbers, unchanged by this task:

| Field | Value | Unit |
|---|---:|---|
| Player HP | 100 | HP |
| Stomp damage | 35 direct / 15 shockwave | HP |
| Club damage | 45 | HP |
| Invulnerability | 1.0 | s |
| Hurt clips | 0.40 medium; 0.60 + 0.63 heavy | s |
| Stomp window | [0.55, 0.70] | clip s |
| Club window | [0.45, 0.65] | clip s |
| Stomp range / club range | 4.5 / 14.0 (direct 2.5) | m |
| Cooldowns | 4 stomp / 5 club | s |
| Yaw slew | 30 | °/s |
| Weakpoint damage | 34 full / 20 weak | HP |

## 4 · The six stills

All captures are 2560×1440, written by `--s42-stills` with the HUD visible. Each is guarded by PNG
magic, > 5000 B, the viewer's own non-uniformity span (largest channel span ≥ 30 from
`s1-pixels`), and exactly one `s42-still` line. Stale PNGs are deleted with `out/s42/` before the
run.

| file (`out/s42/`) | bytes | span | what it shows |
|---|---:|---:|---|
| `stomp-telegraph.png` | 8,986,294 | 254 | the `StompL` telegraph frame (5 × 1/60 s before the 0.55 s window); amber player bar full |
| `stomp-impact.png` | 9,021,505 | 254 | immediately after the impact: Wander `Hurt`, amber bar at 65 % |
| `club-swing.png` | 8,948,574 | 255 | the club mid-swing (5 frames before the 0.45 s window), in Valus's hand; amber 100 % |
| `hurt.png` | 8,981,222 | 255 | after the club impact: Wander `Hurt`, amber bar at 20 % |
| `death.png` | 10,036,130 | 255 | the pinned last frame of `player_a_fall_damage_to_death`; the amber bar is absent at 0 HP |
| `arm-weakpoint.png` | 9,599,465 | 255 | the right-arm shell rune live just after the strike (the reaction has just begun); amber 100 %, stamina 43 |

An out-of-band pixel count of the amber bar (design rect 240×8 at x=24, y=652, sampled at 2×)
confirms the HUD numbers: **480** px wide (100 %) in the stomp telegraph, **312** (65 %) in the
stomp impact, **96** (20 %) in the hurt still, and **0** in the death still (a zero-width bar at
0 HP). The captures are observations: the run was repeated without `--s42-stills` and the
`s42-*` event/status lines were byte-identical. (The still capture selects the current mode clip
one step earlier than the timeline would, so the log gains a few `s2-anim` lines; every playback
clock is restored and no `s42-*` value changes.)

## 5 · Performance

`--frames 1` vs `--frames 240`, same encounter manifest, player and capture path, both under
`caffeinate -dimsu`:

| run | wall time |
|---|---:|
| `--frames 1` (load-dominated) | **8.866 s** |
| `--frames 240` | **11.127 s** |
| marginal (239 frames) | **9.46 ms/frame ≈ 105.7 fps** |

The gate is **≤ 16.7 ms** (60 fps); the 240-frame run also printed
`s1-pixels min=0,0,0 max=248,248,248 mean=102.2,96.7,88.8` and wrote a 2560×1440 capture, so the
marginal number is a real 240-frame run, not a load-only success. The S4.1 regression measured
**6.39 ms/frame** in the same session. The loop is vsync-paced; the ~9 s load is the fixed 30-glb
arena + 165 textures + the 38 MB Valus glb, a per-run startup cost.

## 6 · Suites

- `dotnet build BpConvert.sln -v q`: clean.
- `dotnet test tests/BpConvert.Tests -v q`: **119 passed**, 0 failed (49 s).
- `ctest --test-dir app/build --output-on-failure`: **2/2 passed** (unit 9.94 s, windowed 0.19 s;
  total 10.14 s).

## 7 · The S4.1 regression (which runs S4, which runs S3)

`scripts/s41-acceptance.sh` is unmodified by this task and ran green under `caffeinate -dimsu`
(exit 0; log `out/s42-s41-regression.log`). Its final line:

```text
s41-acceptance: PASS — build, both suites, s41 timeline (grab left_foot, climb 4.77 m, strikes 34+20+34 damage health 100->66->46 and 34->0, catch head, player px 1147,661,273,93, fall 102 frames landed, stamina fall once, death minotaur_a_death_front, settled grounded in 177 frames), five stills (2560x1440, span >= 30, > 5000 B), defeat presence 42.229 % inside bbox 326,188,2559,1254 (outside 0), HUD bars present (stamina 11520 green px, health 12672 red px; 0-HP bar absent), marginal 6.39 ms/frame, S4+S3 regressions green
```

The S4.1 event/status line is unchanged:

```text
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

The S4 and S3 statuses from the nested regressions:

```text
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=993,237,1485,751 pos=378.187,28.430,369.028 frames=423
s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
```

The S4 bbox now starts at x=993 because R10 includes the club in the projected entity bounds; the
S4/S3 scripts themselves are byte-unchanged. The S4.1 HUD numbers grew by the new 240×8 amber
player bar (7,680 px): `s41-defeat-hud changed=21685` (was 14,005) and `s41-reaction-hud
changed=32413` (was 24,733), with `health_changed=12672` — exactly the 66 % Valus bar at the
reaction moment. The S4.1 guards stayed green (they require ≥ 500 red/green pixels).

## 8 · Rulings that shaped this behaviour

- **R6 — orbit sign.** The climb orbit axis is oriented upward (`boneB − boneA`, negated when it
  points down) and uses `+lateral`, so `D` moves along the player's right. The arm route and the
  lateral shimmy the acceptance exercises ship in that form; a horizontal-band sign flip would be
  a one-line regression, and the foot-band test pins the direction.
- **R7 — a lethal fall cannot be caught.** `player_take_damage` clears the catch window when a
  fall becomes lethal, so no grab press can re-attach a 0-HP player; the landing resolves `Dead`.
  The S4.2 death still is a grounded death (club), but the same state machine owns both paths.
- **R10 — club bbox, HUD-free plain captures.** The S4 entity bbox unions the club's mesh range
  (the S4 status above) and plain `--screenshot` captures stay HUD-free; the `--s42-stills` path
  (like `--s41-stills`) feeds `hud_state(true)` itself. S4.1's R10 also retuned the head/neck
  bands; the S4.1 regression still passes.
- **R11 — the stills hook.** `--s42-stills DIR` mirrors `--s41-stills`: six named captures, a
  precondition on `--s42-selftest`, and no change to the timeline's assertions, damage numbers or
  windows. Each capture snapshots and restores the playback clocks.

## 9 · Known limitations

- **The selftest drives the attacks.** The stomp/club/death phases call `start_valus_attack` and
  then the same `advance_valus_attack` / `attack_impact_due` / `apply_valus_impact` the interactive
  loop uses; the *selection* (range/facing/cooldown) is unit-tested in `test_valus.cpp`, not
  exercised here. The target placement is scaffolding: the player is placed at the precomputed
  impact point and the entity capsules are cleared for those steps. The stamina phase is the only
  teleport-free route, as required.
- **The attack stills are close-ups.** At the ruled 1.0 m standoff the raised foot (stomp) and the
  club (club swing) fill the frame; Wander is small or occluded behind them. The HUD proves the
  damage state (65 %/20 %/absent), but telegraph readability is the user's judgement. The
  `arm-weakpoint` still is the clearest weakpoint evidence (the shell rune is in frame).
- **The death still shows a grounded corpse, not a ragdoll.** Valus's death clip is a one-shot
  freeze; Wander's death clip likewise plays once and holds its last frame. No physics.
- **The stills log gains a few `s2-anim` lines.** Selecting the mode clip for the observation is
  the S4.1 still pattern; the clocks are restored, and all `s42-*` values are identical with and
  without stills (the script diffs them). This is a log difference, not a timeline difference.
- **Gamepad hardware unverified.** The mapping (left stick move, right stick camera, RT grip,
  west/X/square strike, Start pause, hot-plug) is unit-tested headlessly; no physical pad was
  available. If the user has one, the walk records it; otherwise it stays "hardware unverified".
- **No blending, no reference capture, inherited S2/S4 gaps.** Hard clip cuts, no ragdoll, no cloth
  or eye glow, and no game-reference silhouette match (design §3 non-goals).

## 10 · The user's walk — PENDING

The scripted pass cannot judge feel, telegraph readability, dodgeability, scale, cursor freedom,
HUD legibility, the gamepad, or the title-bar fps. Run the interactive loop from the repo root and
fill the result cells:

```bash
export SOTC_LIB=<worktree>/out/library
app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

Controls: `W`/`A`/`S`/`D` move (Shift runs), `E` grabs (hold), `J` charges a strike (hold; release
to plunge), `W`/`S` climb, `A`/`D` shimmy, `W+D` branches to the right shoulder at the chest top,
left-drag turns the view, wheel zooms, `T` toggles the entity framing, `F` recenters, `Space`
pauses, `R` soft-resets the encounter, `Escape` quits. Gamepad: left stick move, right stick turn,
RT grip, west/X/square strike, Start pause. The cursor is never captured.

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | windowed, cursor free | open the viewer; move the mouse over the window | a 1280×720 window; the cursor stays visible and free (no capture, no warp) | ☐ |
| 2 | approach and facing | walk (then run) to Valus; circle him on the ground | the feet sit on the bowl floor; he turns toward you (30°/s slew) without a turn clip; ~17.7 m tall, not a toy | ☐ |
| 3 | stomp telegraph and hit | stand within 4.5 m of a foot and wait | the stomp clip is readable as a telegraph; the foot lands in the pinned window; a direct hit takes 35 and the amber bar drops to 65 | ☐ |
| 4 | club telegraph and hit | stand within 14 m in front and wait | the club is in his right hand and follows the swing; the hit takes 45 and the amber bar drops; the 1.0 s i-frames read | ☐ |
| 5 | grab, climb and shimmy | hold `E` at the body; `W` up, `A`/`D` around; at the chest top use `W+D` | the grip holds the surface, the climb camera stays outside, `A`/`D` orbit the band, the chest branches to the right shoulder | ☐ |
| 6 | weakpoints | strike the foot, the forehead, the right arm | the foot is a clang (0 damage); the forehead and arm take 34 and show the shell rune; a whiff still shakes you off | ☐ |
| 7 | death | stand in the club until the amber bar empties | `player_a_fall_damage_to_death` plays once, freezes on its last frame; the HUD shows no amber bar; the fight stops reacting | ☐ |
| 8 | `R` reset | press `R` at any point in the fight | player HP/stamina 100, spawn/camera behind you, Valus 100 and back to stand, his bar hidden until the next hit | ☐ |
| 9 | gamepad (if a pad is available) | connect a standard SDL3 gamepad; use both sticks, RT and west/X/square | the mapping matches the controls above; hot-plug opens/closes it; removal clears its inputs | ☐ |
| 10 | fps, HUD, quit | walk while reading the title and the bars; press `Escape` | the title's fps is in the ≥60 class; the amber health and green stamina bars are legible; Escape quits cleanly | ☐ |

**Verdict: PENDING — the user's.** S4.2's done-line closes only when this table is filled and
passes; nothing in §1–§9 claims it. Item 9 stays "hardware unverified" if no pad is available.

## 11 · Evidence files (all gitignored)

- `out/s42-acceptance-run.log` — the recorded run's full output (ends with the PASS line).
- `out/s42/events-with-stills.txt`, `events-without-stills.txt`, `events.diff` — the
  timeline-purity check (empty diff).
- `out/s42/` — `stomp-telegraph.png`, `stomp-impact.png`, `club-swing.png`, `hurt.png`,
  `death.png`, `arm-weakpoint.png`; `perf-1.log`, `perf-240.log` (the perf capture is deleted).
- `out/s42-s41-regression.log` — the unchanged S4.1 acceptance's full output, ending `PASS —`.
- `out/re/stage/encounter.manifest` — 4,391 B, sha256 `49fba07a…982065f`.
- `out/library/.../wander.glb` — sha256 `e945e0c1…6596d7c`; `c01_minotaur_a.glb` —
  sha256 `7db8fbaa…f0678a`; `c01_minotaur_a_weapon.glb` — sha256 `93b97e23…4b469d`.

## 12 · Cross-references

- Design: `docs/superpowers/specs/2026-10-20-s4-2-he-fights-back-design.md` (§2 goals, §6 recon,
  §7.1–7.8 behaviour, §9 builder inputs, §10 decisions, §11 resolutions).
- Plan: `docs/superpowers/plans/2026-10-20-s4-2-he-fights-back.md`; ledger rulings R6/R7/R10/R11
  in `.superpowers/sdd/2026-10-20-s4-2-he-fights-back/progress.md`.
- Task records: `.superpowers/sdd/2026-10-20-s4-2-he-fights-back/task-9-report.md` (the timeline
  and its defect fix), `task-10-report.md` (this acceptance, stills and docs).
- S4.1 results: `docs/notes/s41-results.md`; S4.0: `s4-results.md`; S3: `s3-results.md`; S2:
  `s2-results.md`.
- Script: `scripts/s42-acceptance.sh` (this pass); the frozen regressions are
  `scripts/s41-acceptance.sh`, `scripts/s4-acceptance.sh`, `scripts/s3-acceptance.sh`.
