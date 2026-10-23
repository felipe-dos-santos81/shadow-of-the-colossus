# S8 acceptance results (2026-10-23) — Agro: the plain and the horse

Spec milestone S8 (`docs/superpowers/specs/2026-10-22-s8-agro-design.md`): walk out of the shrine's
front door onto the plain, whistle for Agro, mount from either side, ride at walk / trot / gallop with
the game's own clips and measured speeds, steer, rein in, back up, dismount; walk back into the
shrine; `R` returns to the arena from anywhere.

**Status: the app/converter acceptance is PASS (script). The user's walk is PENDING.**
`scripts/s8-acceptance.sh` runs green end to end (exit 0). It covers:
- both builds and both suites;
- the **frozen S7 acceptance first**, which runs S6 → S5 → S4.2 → S4.1 → S4 → S3;
- the S8 audio decode, which the script runs itself (34 entries), plus the audition gate;
- the arena compose against its pinned hash, and the shrine and plain composes byte-identical to their manifests;
- the five probe checks;
- the deterministic `s8-*` ride (16 lines), parsed field by field;
- the end-to-end run (`--s8-e2e`: the interactive loop from the shrine door to the ride and back, then `R`);
- the JSON cross-check;
- four 2560×1440 stills.

The script does not judge whether the ride feels like the game's, whether the hooves sound right or
whether nothing pops between gaits. Those are for the user's eye, ear and hands (§10).

| | |
|---|---|
| Date | 2026-10-23 local (−03). The recorded run is stamped `2026-10-23T05:26:06Z`–`05:36:19Z` (10 min 13 s). The S7 chain inside it took 8 min 17 s. |
| Machine | Apple M3 Max, macOS 27.0.1, arm64. 1280×720 points → 2560×1440 px captures. |
| Repo / branch | `sotc-native`, branch `s8-agro`. The code head measured is `47824a4` (the seatRest fix over Task 9's `4d0a13f`). |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<repo>/out/library`, shared. Wander has 43 clips (17 + 26 ride clips appended). Agro has 20 (the S4.2 kick at index 0 + 19 riding clips). |
| Plain manifest (gitignored) | `out/re/stage/plain.manifest`, sha256 `3a2a9b3bdd7481ad71e5165957ac669b4ee47cfe26299a5100e1cf008048b06d`. It holds 7 plain-set instances, 27 cathedral `backdrop` lines at (300, 0.071, −300), 1 `horse` (agro.glb) and the spawn (301, −7.14, −210, yaw 0). Totals: 3,541 meshes and 1,916,318 vertices. The scratch re-compose is byte-identical. |
| Audio manifest (gitignored) | `out/library/audio/audio.manifest`, 34 entries (the S6 ten + 24 S8 event-variants), sha256 `775096a3…0edd` |
| Acceptance script | **`scripts/s8-acceptance.sh`** (bash 3.2; also checked under `/bin/bash` 3.2.57) |
| Run log (gitignored) | `out/s8-acceptance-run.log`, which ends `s8-acceptance: PASS —` |
| S7 regression log (gitignored) | `out/s8-s7-regression.log`, which ends `s7-acceptance: PASS —` |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/library
caffeinate -dimsu ./scripts/s8-acceptance.sh   # exit 0
```

- **Builds.** `dotnet build BpConvert.sln`, `dotnet publish src/BpConvert.Cli`, and the app's `cmake` Release build.
- **Suites.**
  - `dotnet test`: **146 passed, 0 failed** (49 s).
  - `ctest`: **2/2** (`unit_tests` 8.17 s with 388 cases, `windowed_selftest` 0.17 s).
- **The frozen S7 acceptance** ran unchanged and printed its own `PASS —`. It includes:
  - shrine loads of 1,665.0 and 1,652.0 ms;
  - a shrine frame of 8.314 ms (drawn 170, culled 3,393);
  - S4.2's marginal of 8.44 ms (~118.5 fps).

  Its outputs are not re-asserted.
- **The S8 audio.**
  - The S7 chain leaves the S6 ten in the shared manifest. The script therefore decodes `tools/re/audio/s8_map.json` itself, with `dotnet exec out/bin/bp-convert.dll audio … --map tools/re/audio/s8_map.json --force`. It does not use `make audio`, because S9 repoints that target.
  - The decode printed `audio: 34 written, 0 reused, 34 manifest entries`.
  - The manifest's 34 names equal the map's `cues` events, and every file is present.
  - The audition gate (`s5_audition.py --check`, written to `out/s8/audition-gate/`) printed `AUDITION PASSED: 34 cue(s), 1 rate(s) (48000 Hz), resident layers non-silent`.
- **Manifests.**
  - The arena compose hashes to the pinned `088e6218…c1d0`.
  - The shrine compose (`6bacc017…`) and the plain compose (`3a2a9b3b…`) are byte-identical to their manifests.
  - `check-cwks` 13/13, `check-manifest` (arena) 11/11, `check-encounter` 22/22, `check-manifest --stage shrine` 12/12 and `check-manifest --stage plain` 12/12, with `gate: MET` on each.
- **The ride.** `--s8-selftest --s8-stills out/s8/stills` exited 0 with exactly **16** `s8-*` lines (§3).
- **The end-to-end run.** `--stage shrine --s8-e2e` exited 0 with 4 `s8e2e-*` lines (§4).
- **The JSON cross-check.**
  - `tools/re/s8/ride_points.py` regenerates `app/src/ride_points.h` byte-identical from `tools/re/s8/placements.json`.
  - The printed expected speeds equal the JSON's `speeds`, and the back-up is within 5 % of `speeds.back`.
  - The whistle distance equals the probe's distance to Agro's start: 59.99 m.
  - Both edge stops lie on the `probes.edge` segment, short of its end.
- **The stills.** Four 2560×1440 PNGs (§6).

## 2 · The RE gates (`docs/notes/s8-recon.md`)

| Gate | Outcome |
|---|---|
| R1 — the doors | **PASS** (geometry + walk). `door_shrine` is placed where the frozen S7 shrine map reaches (shrine frame, z 44–46). `door_plain` is placed on the shrine steps where the plain's `refuseHoles` map reaches. Both arrival spawns are in `placements.json`. |
| Root tables (D6) | **PASS** (data). `root_z` covers Agro's 14 non-loop clips and `rider_root_xz` covers Wander's 5 mount/dismount clips. |
| R2 — `.anb` event tracks | **FALLBACK**. The touchdown fractions stay measured (the design §6 values; the re-measure differs by one frame on two hooves). |
| R3 — whistle approach / riding camera | **FALLBACK**. D12/D13 stand as authored. |

## 3 · The ride (`--s8-selftest`)

The selftest starts on the plain and runs fixed 1/60 s steps with no window input. These lines are verbatim from the recorded run:

```
s8-load stage=plain instances=7 meshes=3553 ms=894.3
s8-whistle dist=59.99 arrived=2.96 neigh=1
s8-mount side=L clip=player_a_uma_stand_ride_l
s8-gait gait=Walk speed=1.310 expected=1.310
s8-gait gait=Trot speed=5.290 expected=5.290
s8-gait gait=Gallop speed=14.700 expected=14.700
s8-steer yaw_rate=-45.00 expected=-45.00
s8-stop from=Gallop clips=run_stop1,run_stop2,run_stop3
s8-back speed=-1.100
s8-edge-ride stopped=89.39,-289.89 clips=walk_start,walk,run_n_start,run_n,run_n_stop,stand1 blocked=1
s8-dismount side=R
s8-edge stopped=89.43,-290.79 blocked=1
s8-rider offset=0.058
s8-audio events=141 hooves=131
s8-perf frames=240 frame_ms=8.366 drawn=1720 culled=1833
s8-reset stage=arena ride=OnFoot door=Idle valus=100 player=100
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
```

The gates the script applies:
- the plain load is ≤ 15,000 ms;
- arrival is ≤ 3.5 m, with `neigh=1`;
- the mount is exactly the left mount on `stand_ride_l`;
- each gait is within ±5 % of `expected`, in the order Walk, Trot, Gallop;
- the steer's `expected` is exactly −45.00 and the measured rate is within ±5 % of it;
- the stop clips are exact;
- the ridden edge has `blocked=1` and the exact clip chain ending `run_n_stop,stand1`;
- the dismount is `side=R`;
- the on-foot edge has `blocked=1`;
- the rider is ≤ 0.10 m;
- hooves > 0 and events ≥ hooves;
- perf runs 240 frames at ≤ 16.7 ms with culled > 0;
- the reset line is exact;
- the window line is exact.

### 3.1 What these lines prove, and what they do not

- **`s8-gait` and `s8-steer` prove the wiring, not the data.** Their `expected` values are the
  machine's own constants:
  - the gait speeds are `ride_points.h`'s `kSpeed*`, which are `placements.json`'s measured `speeds` (1.31 / 5.29 / 14.7 m/s);
  - the yaw rate is `kYawRateGallop`, 45 °/s, which is **authored** (D10).

  An exact match means that Agro moves by the constant the state machine selected. It does not show that the constant is the game's. The speeds' provenance is the recon (§2). The yaw rate has no data source.
- **`s8-rider` (ruling R13)** is the pelvis's (`Bone_KOSI`) largest departure from its **seated rest**, measured in Agro's frame during the 2 s gallop: **0.058 m** (gate ≤ 0.10).
  - It is not the literal pelvis→`OptBone_boy` distance. At rest the pelvis sits at **(0, 0.178, 0)** relative to `OptBone_boy` in Agro's frame: horizontally exactly over the bone, and 0.178 m above it. That offset comes from the ride clips themselves, which are authored with `OptBone_boy` as the origin. At the gallop the height ranges over 0.136–0.240 m as the clip bounces.
  - The literal metric would read about 0.24 m on correct data.
  - What the gate does not catch: a rider-offset error that stays constant over the gallop. The stills cover that case.
  - Since this task, the rest is sampled after `hold(kClipBlendSeconds)` (commit `47824a4`). Before, it was taken about 2 frames into the Mount → stand1 blend. The value is unchanged at 0.058, because the gallop bounce sets the maximum.
- **`s8-edge-ride` (ruling R14)** is an extra line beyond the plan. It proves ruling R11: when the ground refuses a ridden move, `step_ride` sees a held rein, so the stop chain runs and Agro does not gallop in place. Agro is placed mounted at `probes.edge.from` heading for `edge.to`, then kicked to Walk and then Trot. She stops at (89.39, −289.89) through `run_n_stop` → `stand1`.
- **The right dismount happens because her left side has no ground** (R14). The machine prefers the side she was mounted from (left). The selftest therefore turns her in place at the cliff edge so that her left faces over the drop, and the machine picks the right side. This exercises D11's "the side with walkable ground" in the real app.
- **`s8-edge` (on foot)** stopped at (89.43, −290.79), 13.0 m short of `edge.to` (76.9, −294.6). The cliff edge is a wall (D3).
  - The step-9 loop that drives this walk calls `player_step` directly and **skips the normal frame order**: no clip choice, no pose update, no `plain_step`.
  - It proves the heightmap's refusal for the on-foot controller, not a full interactive frame.
- **`s8-reset ride=OnFoot` is always true on the arena's fresh iteration**, because the ride state belongs to the plain's iteration. The checks that carry weight are door Idle, return Idle, Valus and the player at 100, and that `R` was pressed while mounted at the gallop. `s8e2e-reset` also checks death Idle and Valus not defeated.
- **Perf: 8.366 ms/frame** (240 timed frames plus render, facing the shrine backdrop, 1,720 drawn and 1,833 culled). This is probably vsync at 120 Hz, as S7's 8.3 ms is. R6's roof-garden cut was not needed.

## 4 · The end-to-end run (`--stage shrine --s8-e2e`)

This is the interactive loop at a fixed 1/60 s with scripted keys. The route is: the shrine → the
front door → the plain → whistle → mount → kicks to the gallop → 5 s at the gallop along the
gallop line → hold `S` → dismount → walk into `door_plain` → the shrine → `R` → the arena.

```
s7-load stage=shrine instances=51 meshes=3563 ms=1684.0
s7-load stage=plain instances=7 meshes=3553 ms=877.7
s8e2e-door from=shrine to=plain
s8e2e-ride gallop_m=66.6
s7-load stage=shrine instances=51 meshes=3563 ms=1671.4
s8e2e-door from=plain to=shrine
s7-load stage=arena instances=30 meshes=142 ms=554.7
s8e2e-reset stage=arena ride=OnFoot door=Idle valus=100 player=100
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
```

The script checks the following:
- exactly 4 `s8e2e-*` lines;
- the doors in the order shrine→plain, then plain→shrine;
- the loads in the order shrine, plain, shrine, arena, each ≤ 15,000 ms;
- the gallop at ≥ 60 m. The expected value is 66.5 m: `run_start`'s 16.5 m plus 3.4 s at 14.7 m/s;
- the reset line is exact;
- the window line.

**The e2e sets the player's heading directly.** For each walk leg, the script writes the walk heading itself: the player's yaw plus the camera's look. It then holds `W`.
- This is **heading only**. The e2e never moves the player or Agro directly.
- Every transition comes from the production machines: the door, the whistle, the ride and the reset.
- The shrine route is 3 authored waypoints around Mono's altar.

## 5 · Audio

The manifest holds 34 entries: the S6 ten plus 24 S8 event-variants.
- `agro.hoof_walk.0…9`, `agro.hoof_trot`, `agro.hoof_gallop.0…4`, `agro.neigh`, `player.whistle.0…2`, `agro.mount`, `player.kick`, `player.stop_horse.0…1`.
- Every S8 row is `method: audition`, with low or medium confidence, and none is chain-proven (A4). The user's ear is the check.
- The selftest's `s8-audio events=141 hooves=131` counts the ride cues fired over the timeline. Each hoof is a one-shot at the measured touchdown fractions of the playing gait clip.

## 6 · The stills

The four stills are in `out/s8/stills/`. Each is 2560×1440 with the PNG magic, larger than 5,000 B, and has a non-uniformity span of at least 30.

| Still | Bytes | Span | Shows |
|---|---|---|---|
| `whistle.png` | 7,705,574 | 255 | Wander from behind; Agro approaching at half the whistle distance; the shrine's facade on the left |
| `mount.png` | 6,055,481 | 254 | Agro from behind at about 6 m; Wander mid-swing onto the saddle (0.8 s into `stand_ride_l`) |
| `gallop.png` | 6,168,339 | 254 | Agro from behind at about 9 m on open sand, rider upright on the saddle, cliffs and pillars ahead (looked at, downscaled to 1024) |
| `plain.png` | 9,739,139 | 254 | a wide shot: the shrine's facade and stairs, Wander at the spawn, Agro in the foreground |

## 7 · Authored values and rulings

**Authored (labelled `source: authored` in the data, or `// authored` in the code):**
- Agro's start: (301, −7.361, −203), yaw 180, 7 m ahead of the spawn and facing it. The spawn yaw 0 is authored; the spawn position is data.
- The door boxes and their arrival spawns:
  - `door_shrine`: shrine frame, x −2.5…2.5, y −0.5…3.0, z 44…46. Arrival on the plain at (301, −2.333, −245), yaw 0.
  - `door_plain`: world frame, x 296…306, y −2.0…2.5, z −258.5…−252.5. Arrival in the shrine at (0, 9.79, 34), yaw 180.
- The door fades: FadeOut 1.0 s and FadeIn 1.5 s.
- The yaw rates: 90 / 60 / 60 / 45 °/s at stand / walk / trot / gallop (D10). The steer blend's frame is 1/30 s.
- Mount and dismount:
  - the rein hold is 0.5 s;
  - the mount reach is 1.6 m lateral (the binary's ≤ 0.6 m along her length is data);
  - the clip blend is 0.25 s.
- The whistle:
  - the range is 150 m;
  - she gallops beyond 40 m, with a 38/42 m hysteresis (R9);
  - she stops at 3 m;
  - the warp behind the player is 6 m (the binary's 10 s stuck warp is data);
  - `_near` plays within 15 m.
- The riding camera: 6 / 7 / 9 m at stand-walk / trot / gallop, head +1.2 m (D13).
- The probes:
  - `whistle_from` (252.5, −167.7);
  - the gallop line from (301, −203), heading −54°, 300 m;
  - the edge from (114.3, −280.5) to (76.9, −294.6).
- The selftest and e2e scaffolding (`kS8*` / `kS8E2e*` in `app.cpp`):
  - the mount placement is 1.0 m beside her, and the front-left waypoint is 1.8 m;
  - pursuit uses a 40 m lookahead, full steer at 20° of error, and a kick once within 10°;
  - kicks are spaced 1.1 s apart;
  - the `plain.png` orbit;
  - the budgets.

**Data / measured, not authored:**
- the gait speeds;
- the per-frame root travel of the 14 non-loop clips and the 5 rider clips;
- the clip pairing and the `OptBone_boy` anchor;
- the kick rules: a 1.0 s window, three kicks then a 3.0 s cooldown, and refusal at ≥ 0.9 × the gallop speed;
- the steer shaping: deadzone 0.3, the 1/0.7 rescale and the 0.5 blend;
- the touchdown fractions;
- the plain's composition and spawn position.

**Rulings that shaped the result** (from `.superpowers/sdd/2026-10-22-s8-agro/progress.md`):
- **R5:** a right dismount mirrors `stand_getoff_fall`'s x values.
- **R6:** a cut that was not needed. The plain draws at 8.37 ms.
- **R8:** kicks and dismount presses are ignored while a stop or downshift clip plays.
- **R9:** the approach uses 38/42 m hysteresis.
- **R11:** a refused ridden move reads as a held rein, so the stop chain runs and Agro never gallops in place.
- **R12:** the mount's rider offset lerps from the on-foot spot over the 0.25 s blend, with no snap.
- **R13:** the `s8-rider` metric (§3.1).
- **R14:** the `s8-edge-ride` line, and the right dismount happens when her left side has no ground (§3.1).
- **R15:** a **wall brush** (a refusal shorter than 0.5 s while Riding) downshifts Agro **one gait** through R11's rein. A tap of the rein is the downshift input. This was accepted as milder than R11's cost. The visible effect is that brushing a wall slows her by one gait.

## 8 · Known limitations

- **Not in S8:** foliage and grass (the `.cvgs` format is undecoded, so the plain is bare textured terrain), Agro's bridle and reins, jumps, mounted combat, the moving mount, Wander's call voice (`bvo_call_horse_*` has no resident wave), music while riding, and other terrain surfaces.
- **The arena's entrance stairs are a wall.** The ride into the arena is S9.
- **Low-confidence hoof picks.** The S8 cue rows are audition picks; the ear may reject them.
- **The deferred minors from Task 8:**
  - the whistle pose shows **one frame late**;
  - Agro's ground refusal tests **her centre only**, so her front half can hang over an edge, and there is **no slide** along a wall;
  - there is **no anti-tunnelling at 14.7 m/s** on long frames, the same as for the player.
- **Wall brushes cost a gait** (R15). A sustained refusal stops her (R11).
- **The cliff edge reads as an invisible wall** (D3). There is no fall in S8.
- The dismount needs a stand or walk; the rider reins in first. There is no back mount.
- `s8-gait` and `s8-steer` prove the wiring, not the data (§3.1). The step-9 on-foot edge loop skips the frame order (§3.1).
- The e2e drives the loop with scripted keys at a fixed step. The physical keys, the gamepad and wall-clock frame times are still the user's walk to check.
- Other deferred minors from Tasks 2 and 6:
  - a staircase of 0.9 m node steps passes the per-node cliff rule;
  - `box_entered` tests endpoints only (no tunnelling protection), which is fine at walking speed.

## 9 · The suites

- `dotnet test tests/BpConvert.Tests`: **146 passed, 0 failed, 0 skipped** (49 s).
- `ctest --test-dir app/build`: **2/2** (`unit_tests` 8.17 s, `windowed_selftest` 0.17 s).

The unit suite (388 cases) carries the S8 cases from Tasks 2–9:
- the `refuseHoles` heightmap;
- the local-pose split and `blend_poses`;
- the `horse` and `backdrop` manifest records;
- `ride` with its kick buffer, dismount sides, R11 and R12;
- the `door` machine;
- the 34-entry audio manifest.

## 10 · The user's walk — PENDING

The scripted pass cannot judge the ride's feel, the sound or the look. Run from the repo root and fill
the cells:

```bash
export SOTC_LIB=<repo>/out/library
make audio      # needed after an S7 acceptance (it leaves the S6 ten); the S8 script leaves the 34
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb --stage shrine
# or: make shrine / make plain
```

| # | Check | Do this | Expected | Result |
|---|---|---|---|---|
| 1 | the front door | walk out of the shrine's front door | a fade to the plain, arriving on the shrine steps facing the plain | ☐ |
| 2 | the whistle | press `Q` (far from her, then near) | Wander whistles (`_near` within 15 m); Agro trots, or gallops when far, to about 3 m beside him and neighs | ☐ |
| 3 | the mount, both sides | press `E` beside her left, and later beside her right | the matching side mount plays; Wander ends seated with no snap or float | ☐ |
| 4 | the gaits | tap `W` once, twice, three times | walk → trot → gallop through the start clips; the speed feels right for each gait | ☐ |
| 5 | the kick buffer | spam `W`; kick at the gallop | three kicks in 1 s start a 3 s cooldown; a kick at top speed is refused with a neigh | ☐ |
| 6 | steering | hold `A`/`D` at each gait | she turns smoothly; the turn rate feels right (authored) | ☐ |
| 7 | rein in, back up | tap `S`; hold `S`; hold `S` at a stand | one gait down; a stop through the stop clips; backing up | ☐ |
| 8 | the dismount | press `E` at a stand or walk, at the gallop, and beside a cliff | it dismounts at a stand or walk; at the gallop it is refused; beside a cliff it uses the other side | ☐ |
| 9 | the plateau edge | ride and walk to the plateau's cliff | it stops you (an invisible wall); Agro stops through the stop clips; a brief wall brush slows her one gait (R15) | ☐ |
| 10 | toward the arena | ride to the arena's entrance stairs | an invisible wall (S9 opens it) | ☐ |
| 11 | the sound | listen while riding | the hooves match the gait and the ground; the neigh, the whistle, the mount, the "hyah" and the "whoa" sound right | ☐ |
| 12 | no pops | watch the gait changes, the mount and the dismount | nothing pops between clips (0.25 s blends); Wander sits on the saddle with no float, sink or slide | ☐ |
| 13 | back into the shrine | walk into the shrine steps | a fade back into the shrine | ☐ |
| 14 | `R` from anywhere | press `R` mounted, mid-mount, mid-door-fade and on foot on the plain | the arena in its opening state | ☐ |
| 15 | window, cursor, fps | move the mouse; watch the title | a 1280×720 window, the cursor free, the fps in the ≥ 60 class on the plain | ☐ |

**Deferred — do not mark these as failures:** foliage, the bridle, jumps, mounted combat, the moving
mount, the call voice, music while riding, the arena wall at the stairs, and the low-confidence hoof picks.

**Verdict: PENDING.** S8's done-line closes only when the user has filled this table and every check passes.
Nothing in §1–§9 claims it.

## 11 · Evidence files (all gitignored)

- `out/s8-acceptance-run.log`: the recorded run's full output, ending with the PASS line.
- `out/s8-s7-regression.log`: the frozen S7 acceptance's full output, ending `PASS —`.
- `out/s8/s8-selftest.txt` and `out/s8/s8-e2e.txt`: the captured runs.
- `out/s8/arena-compose.manifest`, `shrine-compose.manifest`, `plain-compose.manifest`: the scratch re-composes.
- `out/s8/ride_points.h`: the regenerated header.
- `out/s8/manifest-names.txt` and `map-names.txt`: the audio name check.
- `out/s8/audition-gate/`: the audition gate's decode.
- `out/s8/stills/`: `whistle.png`, `mount.png`, `gallop.png`, `plain.png`.

## 12 · Cross-references

- Design: `docs/superpowers/specs/2026-10-22-s8-agro-design.md` (§7 decisions D1–D17, §11 the walk).
- Plan: `docs/superpowers/plans/2026-10-22-s8-agro.md`. Rulings R1–R15 are in `.superpowers/sdd/2026-10-22-s8-agro/progress.md`.
- RE gates: `docs/notes/s8-recon.md`. Data: `tools/re/s8/placements.json`, transcribed into `app/src/ride_points.h`.
