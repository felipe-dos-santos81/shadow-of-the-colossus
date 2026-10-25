# S13 acceptance results (2026-10-24) — the shared systems, proven on Phaedra

Spec milestone S13 (`docs/superpowers/specs/2026-10-24-s13-phaedra-systems-design.md`, goals G1–G9, decisions
D1–D13, rulings §13): the shared colossus systems, built once from the game's own data in six parallel lanes — the
exporter for all 16 graphs (G1), the generic control-def evaluator (G2), the shared predicate library (G3), the
additive motion layer (G4), `.path` routes and their follower (G5), `.clb` terrain classes (G6) and the per-boss
coverage report (G8) — proven on the fourth colossus, **Phaedra** (`colossus_04_kirin`, the `KIRIN` tables, clip
stem `kirin_a`), on `arena_04` (G7). Dodge her stomp; hide in a stair tunnel and whistle; she routes to the tunnel's
GP and peeps; leave by another tunnel and grab her tail while she crouches; climb; strike the neck trigger twice
(her neck lowers); climb to the head and strike the sigil; she dies and her body stays; `R` resets. Her fight is
silent (D10).

**Status: implemented; the scripted acceptance is PASS. The user's walk is PENDING (§10).**
`scripts/s13-acceptance.sh` runs green end to end (exit 0). It covers:
- the inputs, checked before anything runs: the game tree, the library with Phaedra's bake (75 clips) and events
  sidecar, Wander, `out/re/stage/arena_04.manifest` (composed when absent; it must say `encounter kirin`), **the 16
  colossus graphs** (`sotc-scr-graph/0`, one per boss), KIRIN's counts (15 modes, 84 conditions, 97 transitions,
  314 motion edges), KNIGHT's unchanged 13/53/40/171 **with `control_defs`**, `kirin.route.json` (`sotc-route/0`,
  5 routes, 11 GPs) and `arena_04.terrain.json` (`sotc-terrain/0`);
- the **frozen S12 acceptance first** (`scripts/s12-acceptance.sh`, unchanged). It runs the S11 chain (the S9
  chain — both builds, both suites, S8 → S7 → S6 → S5 → S4.2 → S4.1 → S4 → S3 — then S11) and then S12;
- `make coverage`'s two commands: one row per colossus (16), KIRIN with 0 missing items;
- five flag refusals (exit 2);
- the deterministic `s13-*` proof (`--s13-selftest --s13-stills`), parsed line by line;
- four 2560×1440 stills with pixel-span checks;
- the end-to-end loop (`--s13-e2e`): one arena load, no shrine load, its lines equal to the selftest's.

The script does not judge whether the fight feels like the game's. That is the user's walk (§10).

| | |
|---|---|
| Date | 2026-10-24 local (−03). The recorded run, on `cf39733` (the script, the `PIN`): stamped `2026-10-24T23:23:15Z`–`23:42:40Z` (wall 1,165 s = 19 min 25 s). The S12 chain inside it took 1,066 s (its S11 chain 997 s, whose S9 chain 938 s); the S13 part 99 s. |
| Machine | Apple M3 Max, macOS 27.0.1, arm64. 1280×720 points → 2560×1440 px captures. Run from a desktop (Aqua) session: no `audio: no output device` line in any log. |
| Repo / branch | `sotc-native`, branch `s13` (worktree `.claude/worktrees/s13`) off main `4f2bf25` (the spec). Script `cf39733` (the `PIN`); goldens `588cf7a`. |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<repo>/out/libviews/s13`, the branch's view. It holds Phaedra's bake (`c04_kirin.glb`, 75 `kirin_a_*` clips, `c04_kirin.events.json` 54,000 B), the 16 `behaviour/<BOSS>.graph.json`, `behaviour/{kirin,knight,minotaur}.route.json` and `stage/arena_04.terrain.json`. **Not yet in the base `out/library`**: the merge promotes them (`make bake` now includes `bake-kirin`, `behaviour` for all 16, `routes` and `terrain`). Gaius now needs `control_defs` in his graph, so until the promotion the base's S12 `KNIGHT.graph.json` refuses him (his load, `test_knight`, s12 verify). |
| arena_04 manifest (gitignored) | `out/re/stage/arena_04.manifest` (`probe.py compose --stage arena_04`, `make arena04-manifest`): `ground` and `structures_platform` as instances, the three ruins glbs as **floors-only** `floor` lines, five backdrops (both waters), `encounter kirin`, her entity at (1122, 53.76, 147); the viewer's scene is 72 meshes. `make probe-check` gates it (20/20). |
| Acceptance script | **`scripts/s13-acceptance.sh`** (bash 3.2); `make accept` / `make accept-s13` |
| Run log (gitignored) | `out/s13-acceptance-run.log`, which ends `s13-acceptance: PASS —` |
| S12 regression log (gitignored) | `out/s13-s12-regression.log`, which ends `s12-acceptance: PASS —` |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/libviews/s13     # <repo>/out/library after the merge promotes the exports
caffeinate -dimsu ./scripts/s13-acceptance.sh     # exit 0
```

- **The S12 chain** ran unchanged and printed its own `s12-acceptance: PASS —` (1,066 s): the S11 chain (997 s:
  the S9 chain 938 s, then S11), then S12's refusals, selftest, stills and e2e (arena_03 load 920.2 ms, frame
  8.344 ms, the e2e load 889.5 ms) — on the S13 library code and the S13 view. Its outputs are not re-asserted here.
- **The S13 part** (99 s): the preflight (the 16 graphs, KIRIN 15/84/97/314, KNIGHT 13/53/40/171 with
  `control_defs`, the routes, the terrain, the sidecar), the coverage report, the five refusals, the selftest with
  stills (11 s), the e2e (88 s), the summary. The script ended (exit 0):

```
s13-acceptance: PASS — the frozen S12 acceptance green (1066 s: the S11 chain — builds, both suites, S9 → … → S3, S11 — then S12 inside it); the 16 colossus graphs exported, KIRIN 15/84/97/314, KNIGHT 13/53/40/171 with control_defs, her routes and terrain valid; coverage: 16 rows, KIRIN 0 missing (| KIRIN | 29/2/0 | 19/4/0 | 29/0/0 | 8/0/0 | 0 |); five flag refusals exit 2; arena_04 load 561.9 ms (72 meshes, layered ground 422x312, 5965 terrain tris, no return); the stomp dodged clear (off_line 14.60); hidden in the class-7 stair, she routed to tunnel 4 GP 0 and peeped deep (arrived 76.7 s); the tail grabbed (kirin_a_watch_loop f47); both neck stages; the climb tail → head; a hung-neck shake (drain 20.0) and a head shake survived (drain 22.9, low 99.6); the head sigil took 160; the death in Giraffe_death (9.86 → 2.44 m) and her held death_loop, no shrine; R exact; frame 8.345 ms (drawn 63, culled 9); 9598 frames / 160.0 s; the e2e loop (frames=9598 seconds=160.0; load 551.5 ms) equal to the selftest, one arena load; four stills 2560x1440; windowed, cursor free; 1165 s in all
```

- **The flag refusals.** Each exits 2 before any load: `--s13-stills` without `--s13-selftest`;
  `--s13-selftest` with `--s13-e2e`; `--s13-e2e` with `--stage shrine`; `--s13-selftest` without `--manifest`
  (each with an `--s13-*` message); `--stage plain` with `--s13-selftest` (main.cpp's general rule, "--stage plain
  is an interactive start", meets it first).
- **The runner.** `tools/verify/s13.sh` is the second copy of the script's gates, like s3…s12
  (`test_coverage.py`: s13 84/84 present, 4 excluded — the nested S12 run, its log, its PASS line, the viewer
  build; 7 viewer runs). The s13 goldens (7 files) are blessed (§9).

## 2 · The deterministic proof (`--s13-selftest`)

Fixed 1/60 s steps with a scripted player (`app/src/s13_script.h`, every value labelled `authored (the script)`).
She starts in her retry mode and patrols; the player walks out until she notices and stomps, runs off her line,
runs round the ruins' south side into the west stair tunnel (terrain class 7) and whistles once a second; she routes
to tunnel 4's inner GP and peeps deep from the ruins' boss ground; the player leaves down the stair, along the ring
corridor and out of the north-west tunnel (class 8), goes round behind her, waits 7 m back until `watch_loop` has run
0.5 s (her hind footsteps' 4.35 m shock spheres cover the tail tip), grabs the tail, climbs to the withers, strikes the
neck trigger twice (resting on the withers between), climbs to the head, survives a shake, strikes the head sigil to
her death, waits for `Done` and presses `R`. The lines (the blessed golden; timings masked; every run since has equalled
it; the 23 `s13-mode`, 17 `s13-climb` and 5 `s13-strike` lines in between are summarised):

```
s13-load stage=arena encounter=kirin meshes=72 ground=422x312 layers=64 entity=1122.00,53.76,147.00 yaw=-93.04 spawn=984.00,52.50,142.00 return=0 graph=KIRIN modes=15 edges=314 add_edges=74 routes=5 gps=11 nodes=52 terrain=5965 boss_ground=706 start=Kirin_withdrawal_return_mode clip=kirin_a_stand ms=<masked>
  (mode: withdrawal_return → route_patrol at 0.0; route_patrol → Normal_attack_mode at 18.7)
s13-stomp clip=kirin_a_atack_leg_l_a mode=Kirin_Normal_attack_mode dist=9.4 at=33.3
s13-dodge clip=kirin_a_atack_leg_l_a rows=0 off_line=14.60 nearest=9.47 player_hp=100 end_clip=kirin_a_stand at=38.7
s13-route event=pick route=4 gp=-1 kind=8 mode=Kirin_underpass_GP_mode at=67.7
s13-route event=pick route=4 gp=0 kind=8 mode=Kirin_underpass_GP_mode at=67.7
s13-hide class=7 at=971.3,52.2,120.4 mode=Kirin_underpass_GP_mode her_dist=35.4 t=67.9
s13-route event=arrive route=4 gp=0 kind=8 mode=Kirin_underpass_GP_mode arrived=76.7 at=76.7
s13-peep mode=Kirin_underpass_included_peeping_(depth)_mode route=4 gp=0 ground=0x400000 arrived=76.7 clip=kirin_a_watch_start her=974.4,53.3,126.3 yaw=-67.6 at=83.2
s13-entry kind=tail band=tail clip=kirin_a_watch_loop frame=47 window=0..-1 mode=Kirin_underpass_included_peeping_(depth)_mode retries=0 player_hp=100 at=118.6
s13-shake_hung clip=kirin_a_refuse_tail band=neck_low drain=20.0 start=99.3 end=69.7 loss=29.5 seconds=1.15 ended=grip at=126.7
s13-neck stage=1 strikes=1 mode=Kirin_Normal_attack_mode layers=kirin_a_add_neck_down_1_start,kirin_a_seach,kirin_a_add_dmg_head_min,kirin_a_add_insight_dummy flags=1,0 at=131.3
s13-neck stage=2 strikes=2 mode=Kirin_Normal_attack_mode layers=kirin_a_add_neck_down_2_start,kirin_a_add_dmg_head_min,kirin_a_add_insight_dummy flags=1,1 at=136.0
s13-route_climb bands=tail,back,withers,neck_low,withers,neck_low,withers,neck_low,withers,neck_low,neck_high,head stamina=100.0 layers=kirin_a_add_neck_down_2_loop at=139.2
s13-ground_handoff from=0x400000 to=0x0 her=958.7,51.2,132.8 yaw=-67.6 mode=Kirin_parent_mode clip=kirin_a_walk_angry_loop rider=head at=143.1
s13-shake survived=1 clip=kirin_a_add_neck_down_2_refuse band=head drain=22.9 start=100.0 low=99.6 end=100.0 loss=0.0 at=144.3
  (strikes on weakpoint_01_int: 34, 34, 34, 34, 24 → health 126, 92, 58, 24, 0 at 145.4 … 152.0)
s13-sigil target=head strikes=5 spent=160 health=0 at=152.0
s13-death phase=Motion total=0.000 mode=Giraffe_death graph_clip=kirin_a_death clip=kirin_a_death lift=9.86
s13-death phase=Collapse total=7.333 mode=Giraffe_death graph_clip=kirin_a_death clip=kirin_a_death lift=2.44
s13-death phase=Done total=8.000 mode=Giraffe_death graph_clip=kirin_a_death clip=kirin_a_death lift=2.44
s13-corpse live_body=1 clip=kirin_a_death_loop held=1 lift=2.44 moved=0.00 player=grounded return=Idle
s13-route event=pick route=-1 gp=-1 kind=-1 mode=none at=160.0
s13-reset health=160 defeated=0 mode=Kirin_withdrawal_return_mode clip=kirin_a_stand at_base=1 layers=none player_hp=100 pmode=grounded death=Idle
s13-coverage boss=KIRIN preds=29/2/0 acts=19/4/0 ops=29/0/0 kinds=8/0/0 total=85/6/0
s13-perf frames=240 frame_ms=<masked> drawn=63 culled=9
s13-status phases_done=12 frames=9598 seconds=160.0 health=160
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
```

While the climber is on her the graph flips `Kirin_parent_mode` ↔ `Kirin_Normal_attack_mode` at each strike (the
hit row), and once through `Kirin_lose_sight_mode` at the hung-neck shake (126.8 s).

| Line | Gate (the script and `s13.sh`) | Measured |
|---|---|---|
| order | load, stomp, dodge, hide, peep, entry, shake_hung, neck ×2, route_climb, shake, sigil, death ×3, corpse, reset, coverage, perf, status — exactly | as gated |
| `s13-load` | `encounter=kirin`, `layers=64`, `return=0`, `graph=KIRIN modes=15 edges=314 add_edges=74 routes=5 gps=11 nodes=52`, terrain and boss-ground counts, `start=Kirin_withdrawal_return_mode clip=kirin_a_stand` | 72 meshes; ground 422×312 nodes; 5,965 stage + 706 boss-ground triangles; **561.9 ms** this run (§5) |
| `s13-mode` | route_patrol → Normal_attack; → underpass_GP; GP → peeping (depth); peeping → parent; → Giraffe_death | at 18.7, 67.7, 76.8, 118.7 (the grab), 152.0 s |
| `s13-stomp` | `kirin_a_atack_leg_*` or `threat`, from her attack mode | `atack_leg_l_a` at 9.4 m, 33.3 s |
| `s13-dodge` | `rows=0`, `player_hp=100` (every posed, skelton and spawned stomp volume clear of the player) | 14.60 m off her line; nearest volume 9.47 m |
| `s13-hide` | `class=7` | the west stair at (971.3, 52.2, 120.4), she 35.4 m away |
| `s13-route` | pick route 4 GP 0 kind 8 in `underpass_GP_mode`; one arrive | picked at 67.7 s (whistle lure), arrived 76.7 s |
| `s13-peep` | depth mode (12), route 4 GP 0, `ground=0x400000`, a `watch_*` clip | `watch_start` at 83.2 s, yaw −67.6° |
| `s13-entry` | `kind=tail band=tail`, a `watch_*` clip, `player_hp=100` | `watch_loop` frame 47 (30 fps frames, R-AD), 0 retries |
| `s13-neck` | stages `1:1,0 2:1,1`; `add_neck_down_1` at stage 1, `add_neck_down_2` at stage 2 | 131.3 / 136.0 s; flags 587/588 |
| `s13-route_climb` | `tail,back,withers,neck_low,…,neck_high,head` | the head at 139.2 s, stamina 100.0, `add_neck_down_2_loop` playing |
| `s13-ground_handoff` | from `0x400000` to any | to 0x0 at 143.1 s in `parent_mode` (an unsteered walk, R-AL) |
| `s13-shake_hung` | on `neck_low`, drain ≥ 1, loss > 0 | `refuse_tail` 20.0/s: 99.3 → 69.7 (loss 29.5 in 1.15 s), ended by a grip break |
| `s13-shake` | `survived=1` on the head, a `refuse` clip, with drain/start/low/end/loss | `add_neck_down_2_refuse` 22.9/s, low 99.6 (regen 25/s on a walked band) |
| `s13-sigil` | `target=head … spent=160 health=0`; the last strike kills | 5 strikes (34 × 4 + 24) |
| `s13-death` | `Motion/Collapse/Done` in `Giraffe_death`, lift `9.86 → 2.44 → 2.44` | 0.000 / 7.333 / 8.000 s |
| `s13-corpse` | `clip=kirin_a_death_loop held=1 lift=2.44 … return=Idle` | moved 0.00 m; no shrine |
| `s13-reset` | exact | — |
| `s13-coverage` | `boss=KIRIN … total=N/M/0` | 85 implemented / 6 gap / 0 missing (equals `make coverage`); since the code review 84 / 8 / 0 (`kinds=7/1/0 feats=0/1/0`: kind 1 and the foot-IK aim counted as gaps, §6) |
| `s13-perf` | 240 frames, `0 < frame_ms <= 16.7` | **8.345 ms** this run (§5) (drawn 63, culled 9) |
| `s13-status` | `phases_done=12` | 9,598 frames, 160.0 s simulated |
| `s1-status` | `windowed=1 relative_mouse=0 mouse_auto_capture=0` | as gated |

## 3 · The end-to-end loop (`--s13-e2e`)

The same script through the interactive loop (its whistle goes in as the S8 `Q` key, its `R` through the key's
handler). Gated: exit 0; one `s13e2e-done`; an `s1-status`; **one `s7-load` line, `stage=arena`** (no shrine after
her death: `returnWired` is off outside Valus's arena); its `s13e2e-load` line gated like the selftest's; the
`s13e2e-*` lines equal the selftest's `s13-*` lines with `load`, `perf`, `status` and `done` aside. Measured:
`s13e2e-done frames=9598 seconds=160.0`, load 551.5 ms, 88 s wall.

## 4 · The stills

Each is one `s1-screenshot` line, PNG magic, > 5,000 bytes, and a channel span ≥ 30 (the viewer's `s1-pixels`
line).

| Still | Bytes | Span | Shows |
|---|---|---|---|
| `peeping.png` | 10,648,069 | 253 | her crouched at the tunnel, from behind, just before the tail run |
| `climb.png` | 9,677,550 | 253 | Wander on the withers |
| `neck_down_2.png` | 11,925,343 | 255 | her neck lowered (stage 2, additive) with the rider on it — from her side (an authored camera for this still only: the climb camera sits inside her neck there) |
| `death.png` | 11,268,516 | 255 | the death clip |

## 5 · Load and frame time

| Run | arena_04 load (ms) | frame (ms, 240 frames) |
|---|---|---|
| this acceptance: selftest / e2e | 561.9 / 551.5 | 8.345 |
| Task 16 (selftest) | 570.0 | 8.351 |

The budget is S9's: `0 < frame_ms <= 16.7` (60 fps). The layered ground is 422 × 312 nodes (40.68 MiB, the four
ground glbs and the floors-only ruins). Her per-frame think (`kirin_think`, with the route follower's clear rays and
string-pulling and the sight ray) costs 0.008–0.012 ms mean, 0.04–0.10 ms max (Task 15, CPU, 1,378 frames).

## 6 · Coverage — the 16 colossi (`make coverage`, `out/re/coverage.md`)

`make coverage` builds `scr_registry_dump` (the app's registries: knight and kirin, one line per fn VA with
`implemented`/`gap`, the kinds — `kind 1 gap`, R-AL —, the 37 opcodes, `feature aim gap`) and runs
`tools/re/scr/coverage.py` over the 16 exports in `<SOTC_LIB>/behaviour/`. Cells are implemented / gap / missing,
counted over the items each boss's exported graph uses; `features` holds one item, the foot-IK aim, for a boss whose
attackdata names an aim bone (+d0). The table is the code review's `make coverage` (2026-10-24, the base library);
the acceptance's own run (step 3, before the review) counted kind 1 as implemented and had no features column (KIRIN
`8/0/0` kinds, total 85/6/0):

| boss | predicates | actions | opcodes | kinds | features | missing |
|---|---|---|---|---|---|---|
| MINOTOUR_A (Valus) | 25/2/3 | 12/0/3 | 26/0/0 | 3/1/0 | 0/0/0 | 6 |
| MAMMOTH (Quadratus) | 26/1/1 | 12/0/1 | 21/0/0 | 4/1/0 | 0/1/0 | 2 |
| KNIGHT (Gaius) | 23/1/0 | 17/1/0 | 28/0/0 | 3/1/0 | 0/0/0 | 0 |
| **KIRIN (Phaedra)** | 29/2/0 | 19/4/0 | 29/0/0 | 7/1/0 | 0/1/0 | **0** |
| BIRD | 17/2/4 | 10/0/12 | 12/0/0 | 2/1/0 | 0/0/0 | 16 |
| MINOTOUR_B | 24/3/5 | 14/0/5 | 25/0/0 | 3/1/0 | 0/0/0 | 10 |
| EEL | 13/1/14 | 7/0/16 | 12/0/0 | 2/1/0 | 0/0/0 | 30 |
| YAMORI_B | 20/1/13 | 12/0/25 | 24/2/0 | 4/1/0 | 0/0/0 | 38 |
| KAME | 25/2/13 | 15/0/18 | 27/0/0 | 4/1/0 | 0/0/0 | 31 |
| NARGA | 16/2/14 | 8/0/19 | 9/0/0 | 3/1/0 | 0/0/0 | 33 |
| LEO | 24/2/10 | 14/0/5 | 27/2/0 | 8/1/0 | 0/0/0 | 15 |
| POSEIDON | 24/3/13 | 14/0/21 | 25/0/0 | 4/1/0 | 0/0/0 | 34 |
| SNAKE | 9/0/5 | 6/0/8 | 11/0/0 | 1/1/0 | 0/0/0 | 13 |
| CERBERUS_R | 21/1/13 | 14/0/8 | 23/2/0 | 9/1/0 | 0/0/0 | 21 |
| MINOTOUR_C | 26/2/12 | 14/0/9 | 28/0/0 | 4/1/0 | 0/0/0 | 21 |
| BUDDHA | 15/2/4 | 8/0/6 | 20/0/0 | 1/0/0 | 0/0/0 | 10 |
| total (boss rows summed) | 337/27/124 | 196/5/156 | 347/6/0 | 62/15/0 | 0/2/0 | 280 |

- **Every opcode and every locomotion kind any of the 16 uses is known** (missing 0 in both columns): the generic
  evaluator covers the 37 opcodes the exports use (gaps 0x02, 0x4a, 0x4b, 0x4d: false in play), and kinds −1, 0, 10
  and 2–9 (through the route hook) run; **kind 1 runs as no-steer and is counted as a gap** (R-AL: S12 R1's reading,
  no waypoint and no request; the binary's kind-1 walk — the follower's `+378 = 1`, the nav-loop patrol — is
  undecoded). Every boss but BUDDHA uses kind 1.
- **The foot-IK aim is counted as a gap** (§13 ruling 4): KIRIN and MAMMOTH name aim bones (`OptBone_IKFootFL/FR`);
  the app plays the stomps as authored clips and never reads `+d0/+e0`.
- **Gaius's 2 fn gaps** are S12's own labelled no-ops, now registered as gaps under D4: `signal_arrow_present`
  (`0x8350c0`, always false) and `ray_ctrl` (`0x82ef20`, never reports class 0xc00); with kind 1 (R-AL) he has 3
  gaps and 0 missing items.
- **Phaedra's gaps**: `cloak_equipped` (`0x835290`, false) and `demo_camera_active` (`0x834fb0`, false) among the
  predicates; the rockfall spawners (`0x6fccf0`, `0x6fd5a0`) and the proximity latch (`0x6fc8a0`, `0x6fc8c0`) among
  the actions (§3 non-goals); kind 1 (R-AL) and the foot-IK aim (§13 ruling 4).
- **Deviation from D5: `demo_camera_active` is registered as a gap.** D5 lists "demo_camera false" in the
  implemented batch; the library returns the same constant false but registers it under `gaps` (D4's labelled
  no-op), since the app has no demo camera state to read. The coverage counts it as a gap, deliberately.
- **Valus and Quadratus** still run on their S4/S11 code; the report lists what their graphs need: Valus 6 items
  (`route_point_event`, `path_to_player`, `waypoint_bearing_le`, `goal_wall`, `profile_reset`,
  `profile_by_surface`), Quadratus 2 (`dprelate_changed`, `stage_anim`) — the census estimate was ~6 each.
- **Caveat (ruling R-U).** BUDDHA, EEL, NARGA, SNAKE, BIRD, LEO and CERBERUS_R use non-`scr_slowstatus` status
  types (`scr_fixstatus`, `scr_wormstatus`, `scr_birdstatus`, `scr_faststatus`) in some modes; the exporter walks
  the per-mode fields only for slowstatus types (attackdata is walked for all), so their counts may be **under**.
  The total row sums the boss rows, so an item two bosses share counts twice. `sub` (`0x832610`) is the
  evaluator's own predicate, always implemented.

## 7 · What came from where

Labels: **graph** — the exported `KIRIN.graph.json` (eboot tables; with S13's `control_defs`, `dprelate`,
`attackdata`, the slowstatus fields and the edge `ref`/`from_ref`/`speed`/`blend_out`); **route** — the exported
`kirin.route.json` (`cp3shadow/path/pathdata/kirin.path`); **terrain** — the exported `arena_04.terrain.json`
(`kirins_hill` `.clb`: the stage classes/owners and the boss ground layer); **sidecar** — the objanim events sidecar
`c04_kirin.events.json` (+ the curated `tools/re/objanim/kirin_map.json`); **decode** — a function of the binary
read for S13 (Ghidra, cited); **data** — another shipped table or file; **measured** — computed from the bake, the
arena glbs or the exports; **authored** — the builder's pick, labelled in `app/src/encounters/kirin.h`,
`kirin_fight.cpp` or the selftest script; **guess** — a reading the evidence does not settle, labelled `guess` in
source.

| Behaviour | Source |
|---|---|
| Modes, transitions, conditions, enter/update/exit actions; the start = her retry mode (`Kirin_withdrawal_return_mode`, then `Kirin_route_patrol`); the death = root death mode 5 (`Giraffe_death`) | graph |
| Control-def conditions of every control-assign row (66 control-defs; 37 opcodes over the 16) | graph + decode (`FUN_0062bfd0`, S12 R2 + the census) |
| Pad, stick, hold and d-pad opcodes false in play; the AI's stick X 0 | **authored** reading (S12's) |
| Op 0x27 reads her ground word as the contact word | **guess** |
| Ops 0x3a / 0x45 true (probe valid; un-ridden boss); 0x28/0x3b ignore slope and range | **guess** / S12 R2 ruling |
| Control → clip (base rows), `p30`/`p34` | graph |
| Additive rows: level-triggered every frame, table order, capped at the pool | graph + decode (`FUN_00631cd0` → `FUN_006329b0` → `FUN_00634f80`) |
| The additive layer: request walk (from-rows, Continue/Shift, Always, Always-Cancel), step, cut on stop, compose `q = slerp(I, cur·ref⁻¹, w)·base`, `t = base + w·(cur_t − ref_t)` | decode (`FUN_00639520`, `FUN_00622470`) |
| The layer pool of 4 | **guess** |
| Layer rate 1 (flag-`0x2` rows' speed ×1); reference = the edge's `ref` at frame 0, else the clip's frame 0 | **guess** |
| Additive edge reference clip (`+0x98`) and the from-row's reference (`from_ref`, R-AG) | graph (R-S: the elf's `kirin_a_add_neck_refuse_start`) |
| Op 0x56 / `additive_playing` match by clip, not by motion-def | **authored** deviation (labelled) |
| The stomp rows, cooldowns, re-selection (attack runner) | graph `attackdata` + the sidecar's judge volumes |
| ATTAIN cylinders: the sidecar's static skelton judge default turned 180° | sidecar + S12 R1 reading (a guess) |
| MOVE_BACK sector: `KIRIN_BACK` (r 20 m, 80° → half 40°) bound from the sidecar under the same 180° reading | sidecar + **guess** (Gaius keeps his authored 75 m / 90° when unbound) |
| Anger (set 30 per hit, decay 1/s, clamp [0, 100]) | graph `root.anger` + decode (`FUN_0082aef0`) |
| The shared library (D5 batch, perception, gait, kind 0/10, attack runner) | decode (S12 + census16) |
| Route tree (5 routes, 11 GPs, 17 points), nav graph (52 nodes, 62 links), link tags | route |
| Route positions in the glb frame: `(600·col − 2700 − x, y, 600·row − 2700 + z)` from the stage cell id | **guess** (fitted on 3 stages; arena_04 residual 0.149 m median on the ruins) |
| Route node heights snapped to her ground (§13.2) | **authored** |
| The follower, kinds 2–9 (entry / per-frame steps, GP hysteresis 0.5 m, kind 8's AP→GP sweep 0.1 m short) | decode (`FUN_00859ad0`, `FUN_0085a420`) |
| The nav walk: Dijkstra over open links from the nearest usable node with a clear ray | **guess** (the game's search is undecoded) |
| Walker class 0 (class mask 1) | **guess** |
| String-pulling to the farthest visible waypoint | **authored** |
| The pick LFSR (`FUN_005866c0`, verbatim); its seed 1 | decode / **guess** |
| `route_by_dprelate`: terrain class → route id; an absent route id (route 1) → no route (R-AB) | graph `dprelate` + decode (`FUN_00823b00` returns −1) |
| Locomotion kind 1 = no steer (no waypoint, no request) — her nav-loop patrol | S12 R1 reading (R-AL); the patrol is **undecoded** (gap) |
| The player's and her ground words (`class << 48 \| owner`) | terrain |
| The stage-word rule `class = (w >> 47) & 0x7f`, `owner = (w << 1) & 0x3c00000` (one bit low; the shift's location not found) | terrain + **guess** |
| Her ground: the boss ground layer where it answers (ruins 0x400000 → deep peeping), else the layered heightmap within 2.0 m (§13.2); an empty node is no ground | terrain + **authored** (the 2.0 m step is S12's; the empty-node rule) |
| One ray serves her sight and the route clear test (R-AI) | **authored** |
| Root motion, walk 5.00 m/s, turns ±40° / 2 s | measured (`kirin_root.h`) |
| The death root drop 9.86 → 2.44 m; its linear timing over `kirin_a_death` | measured / **authored** |
| Damage on the player: rows, windows, posed volumes, the footstep/stomp spawns (`kirin_walk_dmg`, `kirin_atk_dmg`) | sidecar + `kirin_map.json` (curated from `KIRIN_EFFECT`/`STAGE19`; damage_def rows data) |
| The `_wait` re-fire drop (`dropEvents`) | **authored** curation (one hit per stomp chain) |
| The rockfall timer actions (`6fdf70` running, `6fdf20` tick, `6fdf90`/`6fdfb0` arm/disarm) | decode |
| The rockfall feed: +0.5 s per stomp effect (`*_ATK*`) while armed, cap 5 s (R-AJ) | **guess** (her sidecar has no `GROUND_DEST` row) |
| Grip regions per band (tail/rump 0x1e0, back 0x1a0, withers 0x60, neck 0x20, head 0x10); legs carry none | data (`kirin_a_coli.clb`, `boss_regions.py`) |
| Band radii | measured (kirin recon §3) |
| The tail band runs Bone_TAIL → Bone_MATA_L (R-AE); back 0x1a0 and withers 0x60 of their pairs; walk/hang per band; back has no `down` | **authored** |
| The tail windows `watch_start` [46, end), `watch_loop` [0, end), `watch_end` [0, 36) (30 fps frames, R-AD) | measured (tail ≤ 3.73 m above her ground) |
| The entry: a grab (`E`) on the tail band while she peeps | **authored** (D10; the game's entry is not decoded) |
| The neck trigger: a strike on the `trigger_01` mesh is a hit of region 0x20, kind 0, no HP | data (material, region) + **authored** (kind 0) |
| HP 160, the head sigil pool 160 (region 0x10) | data (`kirin_first` 0x16c08a0) |
| Shake-off triggers, the brake window (first 5 s of each 30 s, `KIRIN:C110`) | graph |
| Shake strength (stamina drain, window) | **authored** rule (S11/S12: the region bone's p90 angular speed / 10), measured on her bake (`tools/re/s13/kirin_shake.py`, R-AH) |
| Any control-def handle naming another boss's motion row admitted as a rootless, "not playing" clip (today KNIGHT's 8 SWORD_ONLY defs, `knight_a_atk1_start`) | data + **authored** reading (identity compare never holds for her) |
| Game flags widened to 1024 (her neck flags 587/588) | **guess** (size) |
| Her spawn (1122, 147) on the platform, ground 53.76 | data-supported (`kirin_standby_col`) + measured |
| The player start (984, 52.5, 142) | data (the arena start) |
| The ruins fed floors-only (`floor` lines); the north road kept | **authored** (D9) |
| The road's-end backstop box x [985.5, 994.5], y [58, 64], z [−19.5, −15] | **authored** (Task 12's measured extent, padded) |
| The stomp foot-IK aim (`+d0/+e0`) | **gap** (§13.4: the hoof lands at the clip's offsets) |
| Camera keys, the tendril window, death end | **authored** (Gaius's values) |
| The selftest's script (spots, timings, the side-on `neck_down_2` still) | **authored** (the script only) |

The authored and guessed list is the one the spec's success line asks for, every value labelled in source: the
layer pool, rate and reference; the cell frame; walker class and the Dijkstra stand-in; string-pulling and the
node snap; the stage-word shift; op 0x27; the MOVE_BACK and ATTAIN 180° reading; the rockfall feed; the tail band,
its entry and the band picks; the neck trigger's kind; the shake rule; the flag size; the floors-only ruins and the
backstop; the camera.

## 8 · Gaps and deviations

**Labelled gaps (registered as `gap`, counted by the coverage report, or wired as no-ops):**
- **Rockfall spawners** (`0x6fccf0`, `0x6fd5a0`; the `rockfall_cave/hill` layouts): no-ops. The timer actions run and
  the feed (R-AJ, a guess) fills it, but no rock falls.
- **The proximity latch** (`prox_latch_*`, `0x6fc8a0`, `0x6fc8c0`): no-ops (no fight effect found).
- **The stomp foot-IK aim** (`+d0/+e0`, §13.4): exported, unused; the hoof lands at the clip's offsets.
- **The hashigo ladders** (separate skeletons, attach point unknown), **Hard mode** (shoulder sigils 0x70/0x90),
  the cloak (false), the demo camera (false), the bow, the boot cutscene (`kirin_a_boot` is unusable: its root series
  is absolute).
- **Silent** (D10): no curated Phaedra audio map; the sidecar's footstep and stomp events are where cues attach.
- **Her nav-loop patrol** (ruling R-AL): her patrol and attached modes (3, 14) are locomotion kind 1, which stays
  "no steer" (S12 R1: no waypoint, no request). The follower's `+378 = 1` nav-loop mode is undecoded, so she does not
  patrol; she stands, or walks on her heading through the else-edge walk loop. `s13-ground_handoff` measures exactly
  that: her root's ground word leaves the ruins' boss ground (0x400000 → 0x0, 143.1 s) during an unsteered walk with
  the rider aboard. The earlier claim "she can leave the ruins after peeping" is withdrawn.
- **The neck's additive swing (`add_neck_refuse_*`, 15.4/s) is not met on the scripted route**: the climber leaves
  `neck_low` within ~3 s of each visit. The hung-neck shake gated instead is the base `refuse_tail` (20.0/s, loss
  29.5). In an earlier failing iteration the swing drained a rider on the neck from 67 to 0.
- **The head shake is survived trivially**: `add_neck_down_2_refuse` drains 22.9/s but standing on a walked band
  regenerates 25/s (low 99.6). The walk may find the neck harsh and the head soft.
- **Gaius's 2 gaps** (`signal_arrow_present` 0x8350c0, `ray_ctrl` 0x82ef20) are S12's labelled no-ops, now
  registered under D4; his behaviour is unchanged.

**Deviations and behaviour the walk will see:**
- **The entry is the tail** (D10, authored): the §13.2 roof-neck fallback was not needed. Her tail tip hangs between
  her hind feet, inside every hind footstep's 4.35 m `GROUNDDEST_SHOCK2` sphere, so the script waits 7 m back until
  `watch_loop` has run 0.5 s and gives up 0.9 s into `watch_end`.
- **She keeps one heading** (−67.6°) from the peep to her death while she walks with the rider. Investigated in the
  code review (a temporary per-frame probe in `kirin_fight_step` over `--s13-selftest`, 83–152 s, not committed):
  the peep (mode 12) is kind −1 and the ridden mode 14 is kind 1, neither of which steers (steer 0, no turn bit
  48/49/55/56 on any ridden frame of kind 1's own); the rider's bearing stays within ±10° (on the neck/head) or near
  ±180° (on the tail). Kind 10 (mode 6) runs only in 1–2-frame flickers when a strike breaks the grip (131.3, 136.0,
  145.5, 149.1, 150.1, 151.0 s); those frames do request a turn or the backward step (steer −156°, 88°, −73°: the
  ATTAIN target, not the rider's bearing), but the next frame is mode 14 again, no turn clip is entered and the yaw
  stays −67.63° to the hundredth. So it is not "kind 10 sees the rider at bearing ≈ 0": it is **R-AL** — mode 14's
  kind 1 runs as no-steer; the game's kind-1 walk is undecoded (a counted gap, §6).
- **The climb ping-pongs** (`tail,back,withers,neck_low,withers,…`): each neck strike's grip break is caught on the
  withers and the climber rests there to 95 before the next run up.
- **`refuse_tail` keeps draining a rider already on the back or neck**: the S11/S12 shake rule is not band-aware.
- **The neck trigger is a strike on the `trigger_01` mesh** (region 0x20, kind 0, no HP); the game's own neck
  trigger input is not decoded.
- **KIRIN's graph names `knight_a_atk1_start`** through the shared `COMMON_BOSS_CONTROL` SWORD_ONLY control-defs
  (the 8: [10], [14], [18], [28], [35], [86], [102], [109]); the exporter is faithful (an identity compare with
  KNIGHT's motion row never holds for her). The loader admits any control-def handle naming another boss's motion
  row (a row that is not `kirin[…]`; today only these 8) as a rootless, "not playing" clip; any other unknown clip
  is refused.
- **Op 0x56 / `additive_playing` match by clip, not motion-def** (labelled): `GIRAFFE_NECK_DOWN_QUIT_CONDITIONS`
  reads "no `refuse_start` layer" instead of "not the neck dummy".
- **MOVE_BACK**: Phaedra's `KIRIN_BACK` sector (20 m, 80°) is bound from her sidecar; Gaius keeps the authored
  75 m / 90° (his glue does not bind).
- **Valus and Quadratus** still run on their S4/S11 code (the "Fix later" backlog); the coverage report lists what
  their graphs need (§6).
- **The north road stays** (terrain class 5: standing there makes her give up); a box at its end returns the player
  to the start.

## 9 · The runner (`scripts/verify`)

1. **Blessed**: `scripts/verify --bless --only s13` with `SOTC_LIB=out/libviews/s13`, on `cf39733` (the commit that
   adds `scripts/s13-acceptance.sh`; it becomes the `PIN`, so the script is frozen too). Two blesses from a clean
   tree (each runs the section twice: four fresh runs, 93–94 s each) are byte-identical (`diff -r` of the two
   `golden/s13/` trees and `cmp PIN`: empty). No new mask: Task 16's three (`s13-load`/`s13e2e-load` `ms`,
   `s13-perf` `frame_ms`) cover every moving field. `git diff --stat 286a67c -- tools/verify/golden/` lists only the
   7 new `s13/gpu/` files, `PIN` and the README paragraph; **s3…s12 goldens byte-identical** (no §13.3 ruling). No
   golden holds an `audio:` line.
2. **Agreement**: a one-value mutation of the blessed golden (`s13-sigil target=head strikes=6 …`) makes
   `scripts/verify --only s13` FAIL naming that line (`differs: -… strikes=6 … | +… strikes=5 …`); reverted.
3. **The adapter** gained the acceptance's new preflight gates (the 16 graphs, KNIGHT with `control_defs`, the
   coverage report with 16 rows and KIRIN 0 missing; the report goes to `out/verify/…/s13/gpu/coverage.md`) and is no
   longer report-only.
4. **Full run**: `make verify ARGS=--force` on the blessed tree (`588cf7a` + these docs): rc 0, 521 s wall —
   dotnet 63 s, ctest 23 s (664 app cases), audio 1 s, s3 8 s, s4 19 s, s41 21 s, s42 10 s, s5 8 s, s6 19 s, s7 30 s,
   s8 52 s, s9 102 s, s11 59 s, s12 68 s, s13 94 s: **all twelve milestones PASS** against their goldens.

## 10 · The user's walk — PENDING

```bash
export SOTC_LIB=<repo>/out/library   # S13 was promoted at its merge
make phaedra      # app/build/sotc-viewer … --manifest out/re/stage/arena_04.manifest
```

| # | Check (spec §11) | Do this | Expected | Result |
|---|---|---|---|---|
| 1 | she patrols | watch her from the start, out of her sight | **known gap** (R-AL): kind 1 is no-steer, so she stands or walks on her heading; the game's nav-loop patrol is not decoded | ☐ |
| 2 | she notices you and stomps | walk toward her on the platform | she turns to you and stomps (`atack_leg_*`); stepping off her line clears it; a stomp hurts; no hit while you ride her | ☐ |
| 3 | you hide in a tunnel | go down a stair tunnel (west stair = class 7) | the stairs and the ring corridor are walkable; no fall through the ruins | ☐ |
| 4 | she walks to it and peeps | press `Q` (whistle) inside the tunnel | she routes to the tunnel's GP and crouches to peep: light (owner 0x3c00000) or deep (on the ruins, 0x400000) | ☐ |
| 5 | you leave by another tunnel and reach her tail | run out through another tunnel, round behind her while she crouches; hold `E` at the tail | the grab works in `watch_*` (the tail ≤ 3.73 m above her ground, measured); her hind footsteps hurt (stay ~7 m back until the loop) | ☐ |
| 6 | the climb | climb | tail (hang) → back (walk) → withers (walk) → neck (hang) → neck high → head (walk); her shakes drain the grip | ☐ |
| 7 | the neck strikes lower her neck | strike (`J`) the nape (the neck trigger) twice | the first raises the head (neck down 1), the second lowers it (neck down 2); no HP lost; the additive layer reads as a neck motion | ☐ |
| 8 | the head sigil | from the lowered neck, onto the head; strike | the sigil spends her 160 HP; the last blow kills | ☐ |
| 9 | a shake-off | stay on her | `refuse_tail`/`_body`/`_mune` and the neck/head swings; holding on survives; the hung neck is harsh, the head soft (§8) | ☐ |
| 10 | death | watch | `kirin_a_death` plays, the root drops 9.86 → 2.44 m, `death_loop` holds; **no** return to the shrine | ☐ |
| 11 | `R` | press `R` mid-fight, on her, and after her death | she is back at her spawn, health 160, no layers; you on the ground at the start; the hill links reopen | ☐ |
| 12 | the tunnels and the rim | walk every tunnel, the ring corridor, the ruin roof; walk off the north road | the tunnels hold; the road's end returns you to the start | ☐ |
| 13 | window, cursor, fps | move the mouse; watch the title | a 1280×720 window, the cursor free, the fps in the ≥ 60 class | ☐ |

Known rough edges to expect (§8): no patrol; she keeps one heading through the climb (kind 1's no-steer, R-AL); the climb
ping-pongs between withers and neck; the head shake is easy, the hung neck hard; silence; no rocks fall.

**Verdict: PENDING.** S13's done-line closes only when the user has filled this table.

## 11 · The rulings that shaped S13 (`.superpowers/sdd/2026-10-24-s13-phaedra/progress.md`)

No §13.3 ruling was needed: Gaius's S12 goldens are byte-identical on the new library code (D3), so
`tools/verify/golden/s12/` did not move.

| Ruling | What it decided | Cost (as recorded) | Changes what the user sees |
|---|---|---|---|
| R-A | lanes L1–L6 run as parallel implementers in their own worktrees, merged into `s13` after a clean review | merge conflicts in Makefile/CMake, resolved at merge | no |
| R-B | Task 8 waits for Task 2 (its `route_by_dprelate` reads the exported `dprelate`) | L4 idles briefly | no |
| R-C | Task 14 also needs Tasks 11 and 13 (plan omission) | none | no |
| R-D | lanes edit only their own Makefile recipes; the controller resolves hunk conflicts | a manual merge | no |
| R-E | Task 1 may edit `scr_predicates.cpp` and `knight_fight.*` (`playerOnArenaGround` → `playerGroundAttr`) | none; s12 identical | no |
| R-F | each D5 predicate's test row comes from any of the 16 exports | none | no |
| R-G | implementers never run a GPU step; the controller runs `make verify` (the GPU token) | controller time | no |
| R-H | `sotc-route/0` uses the key `"format"` like the other two schemas | none | no |
| R-I | attackdata exports no terminator row; a run ends before the first row with no motion and no FIX | none | no |
| R-J | `sub` (832610) listed as implemented in the dump is correct | none | no |
| R-K | terrain JSON `class`/`owner` are integers; only 64-bit control-def masks are `"0x…"` strings | none | no |
| R-L | Python/C#/docs-only tasks get `make verify` at the merge, app tasks in their lane | a merge-time failure is attributed later | no |
| R-M | route 2's PP spacing test uses the measured 20.79–24.45 m | none | no |
| R-N | the frame-residual test samples the ruins' classified triangles (where the 0.149 m was measured) | a frame error confined to the hill would go unseen | no |
| R-O | stage owners exported as decoded (0x3c00000 / 0x400000); consumers mask `(attr & mask) == value` | none | no |
| R-P | `terrain_boss_ground` at her spawn is found (owner 0, y ≈ 53.9); her spawn height comes from the boss layer | none | no |
| R-Q | implementers run `ctest -E windowed_selftest` (it opens a window) | none | no |
| R-R | the converter's `dropEvents` (12 lines) and `kUnusable` for `boot` accepted | none | no |
| R-S | the edge `ref` follows the elf (`kirin_a_add_neck_refuse_start`), not the brief | if the +0x98 decode is wrong, the neck-down-2 hold pose is wrong (the still shows it) | **yes** — the lowered-neck pose |
| R-T | MAMMOTH 55 → 57 and MINOTOUR_A 65 → 66 conditions (attackdata conditions walked) | none (s11 never loads a graph) | no |
| R-U | non-slowstatus bosses get the per-mode field walk only for slowstatus types; the coverage may under-count | a later boss milestone extends the walk | no (the report) |
| R-V | a `+0x98` that is not a motion-def exports `ref: null` | none | no |
| R-W | lane-level `make verify` only for frozen-golden risks (Tasks 4, 5, 12); the rest by one verify per merge batch | a batch failure is bisected by lane | no |
| R-X | Task 2's S12 byte-identity test strips the added keys from both sides (it survives the promotion) | one fix round | no |
| R-Y | the manifest's `entity` line for Phaedra is accepted | none | no |
| R-Z | floor lines load in `compose_scene`; the no-SDL `stage_flood` serves the gate; arena_04 joins `make probe-check` (done in Task 16) | none | no |
| R-AA | S12's table had 30 keys; the 37 opcodes are those the exports use (0x03 never occurs); the unknown-opcode check sits in the loader | a trivial merge conflict | no |
| R-AB | an absent `dprelate` route id (route 1) resolves to "no route" and leaves the follower unchanged | none | **yes** — standing on terrain class 2 does not move her |
| R-AC | live link flags live in `RouteState` (reset with the runtime); `ScrWorld::routes` stays const | signatures change in L4's files | no |
| R-AD | band enable windows are in the runtime's 30 fps frames; her 15 fps windows converted | one fix in Task 13 | no |
| R-AE | the tail band runs Bone_TAIL → Bone_MATA_L; the head sigil material is `weakpoint_01_int` | the tail root sits 0.57–0.92 m off the band axis | **yes** — where the tail grab hangs |
| R-AF | Task 5's additive `scr_graph.h` fields and the renumbered `attackTable`/`attackRow` accepted | none (s12 verify identical) | no |
| R-AG | the loader reads 64-bit `"0x…"` args (terrain masks were 0), game flags widen to 1024, the exporter writes `from_ref`; add rows are level-triggered | Gaius's path touched → a full verify (PASS) | **yes** — her terrain lures, the neck flags, the shake reaching its loop |
| R-AH | her shake drain uses the S11/S12 rule measured from her bake (Task 16) | a small measurement | **yes** — how hard each shake is |
| R-AI | one `ScrWorld::ray` serves sight and the route clear test; Gaius's core is shared, not copied | ruin rims also block her sight | **yes** — where she can see the player |
| R-AJ | the rockfall feed counts only the stomp effects (`*_ATK*`), labelled `guess` | if the walk steps also feed in the game, rocks fall less often here (the spawners are a gap anyway) | no (no rocks fall in S13) |
| R-AK | the proof tasks (16, 17) hold the GPU token for their whole run | none | no |
| R-AL | locomotion kind 1 stays "no steer"; her nav-loop patrol is a labelled gap; `s13-leave_ruins` is reworded `s13-ground_handoff` (what it measures) | she does not patrol in S13; the walk judges it | **yes** — she stands where a patrol would walk |

## 12 · What's next

- **The merge**: promote the S13 exports and bake into the base `out/library` (`make bake-kirin behaviour routes
  terrain` with the base `SOTC_LIB`, A16); until then Gaius's graph in the base lacks `control_defs` and main's
  `test_knight`, s12 verify and the s13 runs fail. After the promotion the base must be byte-identical to the view
  (the exports are deterministic: Task 2's two-run check) or s12/s13 need a re-bless.
- **Her patrol**: decode the follower's nav-loop mode (`+378 = 1`) and kind 1's steering (R-AL).
- **The next colossi** from the coverage report (§6): Quadratus onto the interpreter (2 missing), Valus from his own
  tables (6 missing; the "Fix later" backlog; both need a deliberate re-bless), then the cheapest of the rest
  (BUDDHA 10, MINOTOUR_B 10, SNAKE 13 — under-counted, R-U).
- **The exporter's per-mode walk for the non-slowstatus status types** (R-U), the rockfall spawners, the foot-IK aim.

**Backlog (code review, 2026-10-24)** — recorded, not fixed in S13:
- The shared core still carries Gaius's names: `KnightCore`/`KnightFight`/`knight_*` (and `scr_knight_step`) run
  Phaedra too; rename to `Boss*`/`boss_*` when a third boss joins.
- The core's root and turn tables are null-function-pointer seams (`KnightCore::rootAt`/`turnAt`) that default to
  Gaius's `knight_root.h` tables when unset; a boss that forgets them silently gets his.
- The per-colossus `stage_ctx`/`interactive` boilerplate: each fight adds its own `*_wired` branches.
- The verify adapter `tools/verify/s13.sh` duplicates the parsing and gates of `scripts/s13-acceptance.sh`.
- The selftest helpers are copied per milestone (`selftest_s12.cpp` → `selftest_s13.cpp`).

## 13 · Evidence files (all gitignored)

- `out/s13-acceptance-run.log`, `out/s13-s12-regression.log`, `out/s12-s11-regression.log`,
  `out/s11-s9-regression.log` (the shared `out/`, via the worktree's `out` symlink).
- `out/s13/s13-selftest.txt`, `out/s13/s13-e2e.txt`, `out/s13/stills/{peeping,climb,neck_down_2,death}.png`,
  `out/re/coverage.md`.
- The runner's: `out/verify/s13-<hash>/` (logs, candidates).
- Task reports and reviews: `.superpowers/sdd/2026-10-24-s13-phaedra/task-{1..17}-*.md`, the ledger `progress.md`.
