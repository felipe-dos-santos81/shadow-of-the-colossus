# S8 — "Agro": the plain and the horse

**Status:** Proposed — builder call (the user skipped the question pass and asked the builder to pick
and plan the milestone, 2026-10-22); revised the same day after a builder check and a code
fact-check (the explicit plain stage, the horse rig, the door machine, per-variant audio events, the
local-pose split, the backdrop record, the Agro pin), and again on 2026-10-23 after a second builder
check and a plan fact-check (Wander's mount/dismount travel, the landing, upshifts through the start
clips, a blend on every clip switch, the audio gate's `variant_of`, 34 audio entries, the plain-side
door reach, the backdrop set by bounds). The binding milestone after S7 "Return to the Shrine".
S7/S6/S5/S4.2/S4.1/S4/S3 stay green and unchanged.

## 1 · Intent

S7 ends inside the Shrine of Worship: the player wakes, walks the hall, and has nowhere to go. In the
game, the next thing you do is walk out of the shrine's front door onto the plain, whistle for Agro,
climb on and ride. This milestone adds that: the plain in front of the shrine as a stage of its own,
Agro standing on it, and riding — mount, walk, trot, gallop, rein in, steer, dismount — plus the
whistle that brings her to you, with the game's own hooves, neigh, whistle and "hyah".

**Why this milestone (builder's pick, from four parallel recons on 2026-10-22).** The two other
candidates were the second colossus (S4.1-sized: the app is Valus-specific in ~167 places) and the
sword's light (only meaningful with a world to search). Agro is data-rich: every gait, transition,
mount and dismount clip decodes for both Agro and Wander, 64 of 65 horse/rider pairs share frame
counts and fps, the clips chain with 0° pose error except two upshifts, the rider's anchor bone is
identified, the speeds come from the clips' own root travel, the binary yields the kick and mount
rules, and the hoof/whistle/neigh cues sit in a bank S5 already decodes. The plain itself
(`levels/home`) shares the world frame with Valus's arena — the ground runs 460 m from the shrine's
steps to the arena's entrance stairs — so S8 also sets up S9's ride to the second colossus.

## 2 · Goals

- **G1 — The plain.** `models/shadow/levels/home` composed from the shipped stage data as a third
  stage (`plain`), entered by walking out of the shrine's front door and left by walking back in;
  `--stage plain` starts there.
- **G2 — Agro.** Agro stands on the plain (an authored position near the start), a second animated rig
  that is not an encounter: she idles, and answers the whistle.
- **G3 — Riding.** Mount from either side, ride at walk / trot / gallop with the game's own clips and
  their measured speeds, steer, rein in through the game's stop clips, back up, dismount from either
  side; Wander rides on the saddle bone, playing the paired rider clip.
- **G4 — The game's controls.** Each kick (`W` tap / stick forward press) raises the gait one step,
  following the binary's kick rules; reining (`S`) lowers it; `A`/`D` steer; `E` mounts/dismounts near
  her; `Q` (gamepad north) whistles.
- **G5 — The sound.** Footfall-synced hoof one-shots per gait on grass, Agro's neigh, Wander's whistle,
  the mount sound, the kick "hyah" and the "whoa", from `com_b_h`/`com_v`, as a new curated map
  (`tools/re/audio/s8_map.json` = the S6 cues + the S8 rows).
- **G6 — The proof.** `--s8-selftest --s8-stills out/s8/stills` and an `--s8-e2e` interactive run
  (shrine door → plain → whistle → mount → gallop → stop → dismount), and `scripts/s8-acceptance.sh`
  running them after the frozen S7 acceptance (which runs S6 → S5 → S4.2 → S4.1 → S4 → S3).

## 3 · Non-goals

- Riding into Valus's arena (the plain's ground stops at the arena's entrance stairs; that edge is a
  wall in S8), Agro in the shrine or the arena, the second colossus, the overworld beyond `home`.
- Jumps, mounted combat (sword/bow from the saddle), patting, standing on the saddle, mounting a
  moving horse (`run_ride_*` are byte copies of the stand mount), falling off at speed (the dismount
  needs a walk or stand and lands through `stand_getoff_fall`; the rider reins in first), in-place turn clips (`uma_turn_*` bake yaw into
  the spine — they would spin the body under the rider), the `add_horse_turn_*` overlays.
- Foliage/grass (`virtualgeometry/*.cvgs` is undecoded — the plain is bare textured terrain), the
  bridge, terrain surfaces other than grass, music while riding (the non-fight streams have no
  trigger found), the call voice (`bvo_call_horse_*`: no resident wave).
- Agro's bridle/reins (`agro_bridle.glb`, its own 41-joint skin) — omitted in S8, labelled.
- A navmesh: Agro's whistle approach is a straight-line walk/trot/gallop over the heightmap with the
  same walkability rules as the player.

## 4 · Anti-goals

- **A1 — A fake horse.** Generic animation, invented speeds, or a rider glued at a fixed offset would
  pass a still and miss the point. The clips, speeds, rider anchor and kick rules come from the data
  and the binary; what is authored (camera, whistle behaviour, turn rates, Agro's start, the door
  triggers) is labelled.
- **A2 — Breaking what exists.** The arena, the shrine, every frozen acceptance (S3…S7) and their
  numbers stay identical; S8 adds `s8-*` lines and stills only.
- **A3 — A stuck rider.** Every riding state ends: dismount always works at walk/stand, `R` resets to
  the arena from anywhere (mounted or not), `M`/`Escape` stay live.
- **A4 — Overclaiming the sound.** The S8 cue rows are audition picks (low/medium confidence); they are
  labelled so, and the user's ear is the check.
- **A5 — Cursor capture or a fullscreen window.**

## 5 · Constraints

- Apple Silicon, C++20, SDL3/SDL_GPU/Metal, arm64; no new runtime dependency; offline-first; the
  game tree is read-only; assets and derived files are gitignored, never committed.
- `out/library` is shared by every branch and worktree: a bake must keep `main`'s suites green (the
  S7 pattern: Wander's existing 17 clips keep their indices; new clips are appended).
- Frozen and invoked unchanged: `scripts/s7-acceptance.sh` … `s3-acceptance.sh`, `s5_map.json`,
  `s6_map.json`, the arena/encounter/shrine manifests, every existing selftest's output.
- Bash 3.2; `caffeinate -dimsu` around every viewer/acceptance run; never two acceptances at once.
- ≤ 4 concurrent agents; W1 (app) and W2 (RE) never share files.

## 6 · What the data and the binary say (the 2026-10-22 recons)

**The plain.** `levels/home`: 23 glbs → 8 kept (`home`, `home_ground`, `home_ground_high`,
`home_cliff_displaced`, `home_skirts`, `home_plainsgrey_skirt`, `home_pond`, `home_wall_pillars_01`)
after the S7 exclusion rules plus `home_occluder`, `home_shadowblocker`, `home_cliff_shadow_lod`;
`home_ground_high` overlays `home_ground` by 0.03–0.21 m (drop it — z-fighting). 52 meshes, 150k
vertices, 79 PNG references / 55 unique (33 shared with the shrine). Bounds x[−216, 735],
y[−182, 118], z[−583, 455]; ground coverage 455k m², 92 % of the ground within 300 m of the start
under 10°. An unlayered 1 m heightmap (≈ 1M nodes, 4 MB) suffices; the plateau ends in cliffs (no
ground → NaN nodes). Water: only `home_pond` (7×7 m, ≤ 0.3 m deep). The plain is in the **world
frame**; the shrine's glbs are local: world = shrine + (300, 0.071, −300) (`home.cgpr`
`level_setup`). The start: `cathedral_ext.sgpr`/`home.sgpr` (301, −6.25, −210) → floor −7.14; no yaw
(authored 0, facing +Z, away from the shrine). No horse start record exists. `home_ground` ends at
z ≈ 251 where `arena_01_entrance_stairs` begins. `shadowzones.cxml`'s `home` zone streams Home +
Cathedral_Exterior + Bridge.

**Agro and the rider.** `agro.glb` (132 joints, 30 animated; the saddle is in the glb; the bridle a
separate 41-joint glb). The rider anchor is `OptBone_boy`, a rigid child of `Bone_root` at
(0, 0.13, 0) on the saddle seat; Wander's ride clips are authored with it as origin (pelvis +0.178)
and the rider follows it per frame — except that Wander's mount/dismount clips move his root
sideways in the horse's frame (`stand_ride_l/r` x +0.53/−0.57 m, z ≈ −0.2 m; `stand_getoff_l/r_start`
x ∓0.67 m; `stand_getoff_fall` y −1.79 m to the ground, x −0.26 m), travel the converter zeroes. The
S8 clip set — Agro (`horse_a_uma_*`, 19): `stand1`,
`walk_start`/`walk`/`walk_stop`, `run_n_start`/`run_n`/`run_n_stop`, `run_n_from_walk`,
`run_start`/`run`, `run_from_run_n`, `run_stop1/2/3`, `back_start`/`back`/`back_stop`,
`neigh`/`neigh_end`; Wander (26): the 19 `player_a_uma_*` pairs (stand pairs with
`stand1_player`), `stand_ride_l/r`, `stand_getoff_l_start`/`_r_start`/`_fall`, `player_a_whistle`,
`player_a_whistle_near`. Gait speeds from root travel: walk 1.31, trot 5.29, gallop 14.7, back
≈ 1.1 m/s; each transition clip carries its own root z (e.g. `run_start` 16.5 m in 1.6 s), read from
the `.anb` (`tools/re/anb/parse2.decode_root`), not the baked glb. Joins (mean joint error): the
loops/stops/downshifts chain at 0° (walk_start→walk→walk_stop→stand; run_n_from_walk→walk;
run_from_run_n→run_n; run_stop1→2→3→stand; neigh→neigh_end→stand); stand1→walk_start 8.6° (best
7.0°), walk→run_n_start 5.9°, run_n→run_start 11.3°, `run_n`→`run_n_stop` 17° from loop frame 5 /
31.9° from its end, `run`→`run_stop1` 3.2° from frame 7, `stand_getoff_r_start`→`stand_getoff_fall`
35° (`_l_start` 5.2°); a direct cut walk→trot would be 13.5° (hoof RMS 0.40 m) and trot→gallop 30°. `stand_ride_l` starts on the `ABUMI_R` side (x −0.53): the clip is chosen by the approach side,
not its name. Agro's `agro.glb` carries only `horse_a_add_horse_kick` today.

**The binary (`horseuniqparam.sidb`, `FUN_006ea960`).** Kicks: the last three kick times are kept,
each expiring after 1.0 s; three kicks inside the window start a 3.0 s cooldown; a kick only counts
below 0.9 × max speed, otherwise it is refused (`hvo_neigh_low` plays). Steering: stick X deadzone
0.3, rescaled by 1/0.7, blended 0.5 toward its target, clamped. Mount thresholds (console defaults,
the player's position in the horse's frame): side mount needs |z| ≤ 0.3–0.6 m along the horse; back
mount z ∈ [−1.1, −0.18] within 160°. Speeds/acceleration/turn rates are animation-driven (not in the
sidb). The whistle code found is a stuck-recovery warp (> 10 s stuck); the approach itself is not
found. No riding camera values found.

**The sound (`com_b_h`, `com_v`; audition picks, none chain-proven).** `agro.hoof_walk`
(`horse_walk_grass`, waves 666–674/676), `agro.hoof_trot` (`horse_average_leg_grass`, wave 90),
`agro.hoof_gallop` (`horse_run_grass`, 160/161/797/949/1131), `agro.neigh` (`hvo_neigh_low`, 22),
`player.whistle` (`whistle1`, 19/20/21), `agro.mount` (`b_get_on_stand`, 297), `player.kick`
(`bvo_kick_horse1`, 62), `player.stop_horse` (`bvo_stop_horse1`, 22/23). Hooves are one-shots per
footfall (numbered per-event registry instances, 0.2–0.9 s samples with no loop points); touchdown
fractions measured from the baked hoof bones: walk FR 0.25, BL 0.375, FL 0.75, BR 0.875; trot
(`run_n`) BR/FL 0.0, FR 0.33, BL 0.89; gallop (`run`) BR 0.11, FL 0.44, FR 0.67, BL 0.95.

**The app today.** Wander is rig 0; any manifest `entity` is treated as Valus (S7 §6); the ground's
`heightmap_walkable` is always true on an unlayered map and a NaN node falls back to the nearest valid
height (the player could run off a cliff); there is no pose blending (`animation_pose` samples one
clip and returns finished skinning matrices, `anim.cpp:114-187`); the converter pre-mixes a cue's
`layers` into one WAV and the mixer plays one clip per cue (`AudioConverter.cs:181-209`,
`audio.h:75`), the C# manifest reader rejects unknown keys (`AudioManifest.cs:48,139`) and
`s5_audition.py` needs a `CORE_EVENTS_BY_MILESTONE` entry per milestone; keys in use: WASD (held),
Shift, E, J, R, M, F, T, Space, `[`/`]`, Escape; gamepad: sticks, RT, west, Start (Q and gamepad north
are free).

The S7 code the plain must not inherit: rig 1 is always the encounter entity (`entityRig = playerRig +
1`, `app.cpp:2297`; `entity_capsules` loops every rig after the player's, `app.cpp:1040`); the stage
loop knows only `shrine` vs not (`app.cpp:2089`, ~25 branches) — a third stage would run as the
arena, and `press_reset` loads the arena only from the shrine (`app.cpp:4657`); `--stage` accepts
arena|shrine (`main.cpp:219`); `step_return` reaches FadeOut only from a death `Hold` and `Free` is
terminal (`transition.h`); `audio_wanted` needs an entity (`app.cpp:2459`) and `new_stage()` turns
audio off per stage; the `--s7-e2e` script is inline and treats `!shrine` as the arena; there is no
bone-attach for the player (`apply_player_transform` takes position + yaw; the `club_model` pattern —
mesh × palette × bindWorld — is the closest); the follow camera's distance is a parameter but its
height is the constant `kFollowHeadHeight`; the converter zeroes root x/z (`GlbWriter.cs:240`), so
root travel is not readable at runtime; `test_asset_io.cpp:536` pins Agro's clips exactly (1 clip,
`horse_a_add_horse_kick`) and `test_anim.cpp:713` uses it; Wander's pin is already ≥ 17.

## 7 · Decisions

| # | Decision | By | Why / cost if wrong |
|---|---|---|---|
| D1 | S8 = Agro on the plain (not the second colossus / the sword light) | Builder | §1. Cost: the next fight waits one milestone |
| D2 | The plain is a third stage `plain` composed by the probe (`STAGES["plain"]`: the 8 kept glbs minus `home_ground_high`), unlayered ground, **plus the cathedral's exterior glbs as `backdrop` lines** (a new manifest record: an `instance` that is drawn but never rasterized into the ground) at the shrine→world offset (300, 0.071, −300), so the shrine stands behind you; the arena's geometry is **not** added (the entrance stairs edge is a wall). The app gets an explicit stage switch (`StageId::Plain`, no `shrine`-vs-rest booleans), `--stage plain` and `--plain-manifest` | Builder | The shrine/arena stages stay as they are; the ride into the arena is S9. Cost: the exterior's draw cost (measured, gated); a plain ending at an invisible wall by the arena stairs |
| D3 | A `Heightmap::refuseHoles` flag (set only when the plain's map is built): `heightmap_walkable` then refuses a move whose target has **no ground, or** whose floor drops > 1.5 m **or** rises > 1.0 m from the source (per 1 m cell); unflagged maps (arena, shrine) behave exactly as today. Agro uses the same rule; her stuck-warp target must have ground (never sample a hole — `heightmap_sample` scans the whole ~1M-node grid there) | Builder | The plateau ends in cliffs (the cliff mesh gives heights down the face, so a hole test alone would not stop the edge); no fall/death in S8. Cost: the cliff edge reads as an invisible wall |
| D4 | Agro is a new manifest record `horse <glb> <px py pz> <qx qy qz qw> <sx sy sz>` (the TRS shape of every other record) → `compose_scene` adds her rig; the app gets an explicit `horseRig` and sets `entityRig` invalid when the stage has no `entity`, so no encounter code (capsules, chest framing, grab joints) ever sees Agro | Builder | An `entity` is Valus to the app and rig 1 is assumed to be him. Cost: one record, a compose branch and a rig-index audit |
| D5 | Riding is a pure state module (`app/src/ride.h/.cpp`): `OnFoot`, `Mounting{side}`, `Riding{gait}`, `Dismounting{side}`, with gaits `Stand, Walk, Trot, Gallop, Back` and the clip chain of §6 (start/loop/stop/transition clips), mirroring `death.h`/`transition.h` | Builder | Headless-testable; the app wires it. Cost: one module |
| D6 | Agro's travel follows the playing clip's measured root travel: loops by their mean speed, and the start/stop/transition clips by a per-frame cumulative-z table; Wander's four mount/dismount clips (`stand_ride_l/r`, `stand_getoff_*_start`, `stand_getoff_fall`) by per-frame root x/z tables applied in Agro's frame over `OptBone_boy` — all read from the `.anb` (`parse2.decode_root`) by the RE step and transcribed into the app per the S7 R1 pattern, labelled "measured"; the converter and the shared library stay as they are | Builder | The game's own travel; the converter zeroes root x/z, so the app cannot read it back. Cost: tables to keep in sync with the bake |
| D7 | `animation_pose` splits into a per-joint **local-pose** sampler and a palette builder; `blend_poses(a, b, w)` slerps/lerps local poses, and **every clip switch on Agro and on the mounted rider blends over 0.25 s** from the outgoing pose (a no-op at the 0° joins; it covers the 3–35° joins of §6, including stops entered at any frame); hooked where `update_pose` builds a rig's palette | Builder | One rule instead of a join list; finished palettes cannot be blended. Cost: the first blend in the app and an anim refactor (the frozen outputs prove it) |
| D8 | The rider is Wander's rig placed at `OptBone_boy`'s world transform each frame (a new bone-world helper taking a rig index, after the `club_model` pattern — never matched by palette size), playing the paired `player_a_uma_*` clip at the same clip time; while mounted the on-foot controller (`player_step`) and the mode-based clip choice are skipped, as the S7 wake does | Builder | Measured anchor and pairing. Cost: the rider's feet may drift 2–8 cm from the stirrups (measured spread) |
| D9 | Kicks follow the binary's rules (1.0 s window, three kicks → 3.0 s cooldown, refused at ≥ 0.9 × max speed with `agro.neigh`); each counted kick (a `W` **press** edge / stick-forward edge) raises the gait one step **through its start clip** (Stand→Walk `walk_start`, Walk→Trot `run_n_start`, Trot→Gallop `run_start`; the loop follows); a tap of `S` lowers it one step through the downshift clips (`run_from_run_n`, `run_n_from_walk`, `walk_stop`); **holding `S` 0.5 s** stops from any gait through the stop clips (gallop `run_stop1→2→3`, trot `run_n_stop`, walk `walk_stop`); `S` at Stand plays `back_start`/`back`/`back_stop` while held | Binary + builder | The game's feel. Cost: the step-per-kick mapping is the builder's reading of the kick buffer |
| D10 | Steering: the binary's stick shaping (deadzone 0.3, 1/0.7 rescale, 0.5 blend) feeding an authored yaw rate per gait — Stand 90°/s, Walk 60°/s, Trot 60°/s, Gallop 45°/s | Binary + builder (veto) | No turn rates in the data; the turn clips can't play under a rider. Cost: authored feel |
| D11 | Mount: `E` within 1.6 m of Agro's side (authored reach) and |z| ≤ 0.6 m along her (the top of the binary's 0.3–0.6 m side range), the side picking `stand_ride_l/r` by the player's x sign in her frame (`stand_ride_l` starts on the `ABUMI_R` side — choose by side, not by name); dismount: `E` at Stand/Walk only (Agro stops first), to the side with walkable ground: `stand_getoff_<side>_start` then `stand_getoff_fall` (both sides; the right side's 35° join blends per D7), Wander on foot at the clip's end position in Agro's frame | Binary + builder | The game's side mount. Cost: no back mount, no moving mount |
| D12 | The whistle (`Q` / gamepad north): Wander plays `player_a_whistle` (`_near` within 15 m); Agro, if within 150 m and not ridden, turns to the player and approaches at trot (gallop beyond 40 m), stops 3 m from him on his side, plays `neigh`; blocked by the same walkability as the player — if she has no progress for 10 s she is placed 6 m behind the player (the binary's stuck warp) | Builder (veto) + binary (warp) | No approach code found. Cost: authored behaviour |
| D13 | The riding camera: the follow camera with distance per gait (Stand/Walk 6 m, Trot 7 m, Gallop 9 m) and a new head-height parameter (mounted +1.2 m over `kFollowHeadHeight`); FOV unchanged; eased | Builder | None in the data. Cost: authored feel |
| D14 | The shrine door and the plain's shrine steps are **trigger boxes** that fire on **entry only** (outside → inside; spawning inside never fires): the shrine door is placed where the frozen S7 shrine's own walk map already reaches toward its +Z front (verified by walking it, ≥ 5 m from the shrine spawn — the shrine's ground is not changed), the plain's on the shrine steps. A small pure **door machine** (`door.h`: `Idle → FadeOut 1.0 s → Load → FadeIn 1.5 s → Idle`, input locked) runs the crossing — the S7 return machine stays as it is (`carry.ret` untouched); the HUD fade is the larger of the two machines' fades. Each door has its own arrival spawn (in `placements.json`), not the stage's start | Builder | The two stages are separate scenes (a merged scene would make the shrine's 64-layer ground cover the plain: ~316 MB). Cost: a fade at the door instead of a seamless walk |
| D15 | The S8 audio is a new map `tools/re/audio/s8_map.json` (= the S6 ten + the S8 rows, `method: audition`); a cue with alternatives becomes **one event per variant** (`agro.hoof_walk.0…N`, `agro.hoof_gallop.0…4`, `player.whistle.0…2`, `player.stop_horse.0…1`), each variant row carrying an additive `variant_of: "<base event>"` field; the converter and the manifest stay as they are, and `s5_audition.py` changes additively: it accepts rows sharing a `name` only when they share a `variant_of` (its `duplicate cue name` check, `s5_audition.py:480-484`, is unchanged for the S5/S6 maps, which carry no `variant_of`) and gains a `"s8"` `CORE_EVENTS_BY_MILESTONE` entry; the map's `milestone` is `"s8"`; the manifest holds **34** entries (the S6 ten + 24 S8 event-variants); the app picks a variant per trigger with a seeded RNG (deterministic under the selftests) from its **own S8 cue table** (the `Sfx` list stays as pinned); audio is wanted on the plain (no entity needed); hooves are one-shots fired at the measured touchdown fractions of the playing gait clip, at Agro's position | Builder | The S5 contract, unchanged; S5/S6 maps frozen. Cost: low-confidence hoof picks the ear may reject |
| D16 | Wander and Agro are re-baked in the shared library: Wander's 17 clips keep their indices and the 26 ride clips are appended; Agro keeps `horse_a_add_horse_kick` at index 0 and gets the 19 riding clips after it (20); `test_asset_io.cpp:536`'s exact Agro pin is relaxed on `main` first (≥ 1, clip 0 by name) — **landing that test-only commit on `main` needs the user's OK at execution time** (the S7 authorization covered S7's commit); the Makefile `bake` target bakes Agro and appends the 26 ride clips to `WANDER_CLIPS` in order, and `make audio` uses `s8_map.json` (every `--s8-*` run after an S7 acceptance first re-runs `make audio`: `s6-acceptance.sh` leaves the S6 ten) | Builder | The S7 bake pattern. Cost: the shared library changes under every branch |
| D17 | `s8Selftest` joins the frozen-selftest exclusions (`frozen_selftest` in `main.cpp`, `interactive_run`, `death_wired`); `s8E2e` is an **interactive** run like `s7E2e` (not in `frozen_selftest`, `interactive_run` stays true) with its own script state and a ride-input path (whistle/kick/rein/mount presses), allowed with `--stage shrine`; the doors and the riding are stepped only in interactive play (not `s7E2e`) or under an S8 run — never under `--s7-selftest`/`--s7-e2e` or a frozen selftest; `entityRig` (a `std::size_t`) is `SIZE_MAX` without an entity, and `entity_capsules` skips `horseRig` | Builder | The S7 e2e is inline and arena/shrine-only; S7's runs must not meet the doors. Cost: one more scripted mode |

## 8 · The design

### 8.1 Data (W3/W4)
- `probe.py`: a real `plain` branch (the code treats every non-arena stage as the shrine today —
  `probe.py:1428,1565`, `shrine_sets`/`shrine_placements` hard-code the shrine): `STAGES["plain"]`
  (`cwks_match = "levels/home/"`, the S7 exclusions + the three home proxies + `home_ground_high`;
  its own `sets`/`count`/`cwks_sha256` pins — `shrine_sets`/`shrine_check_spec`/`shrine_excluded`
  hard-code the shrine today), the `backdrop` lines — the shrine-set glbs whose bounds face the plain
  (chosen by bounds in the gate, e.g. `cathedral_new_firstlevel`, `cathedral_entrance`, `rootsfront1`
  and the `cathedral_ext*` pieces) at the offset —, the `horse` line, the spawn (world
  (301, −7.14, −210), yaw 0), pinned count/meshes/sha256; `parse_manifest_text` accepts `horse` and
  `backdrop` only when the caller asks (like `posed`), so the arena manifest stays strict; the shrine
  compose stays byte-identical; `check-manifest --stage plain`.
- `tools/re/s8/placements.json` (W2 gate output): Agro's start (authored ~7 m ahead of the spawn, yaw
  facing the player), the shrine door trigger + its arrival spawn on the plain, the plain's
  shrine-steps trigger + its arrival spawn in the shrine (shrine frame), the selftest/e2e probe points,
  the backdrop glb list (by bounds), and the per-frame root tables of D6 (Agro z, Wander x/z), each
  with `source`.
- Bakes (W4): Agro 20 clips (kick + 19), Wander + 26 (appended), `s8_map.json` decoded (34 entries).

### 8.2 The app (W1)
- `Heightmap::refuseHoles` (D3).
- `manifest`/`compose_scene`: the `horse` and `backdrop` records; the stage loop's explicit `Plain`
  iteration places Agro as `horseRig` with her own `Playback`, `entityRig` invalid, never an
  encounter; `R` from any non-arena stage loads the arena.
- `anim`: the local-pose sampler / palette builder split and `blend_poses` (D7).
- `ride.h/.cpp` (D5, D9–D11): pure; inputs (kick/rein/steer/mount pressed, the player's position in
  Agro's frame, ground-free sides) → state, the current Agro clip + rider clip + clip time, speed,
  yaw rate; tests for every transition and the kick buffer.
- `whistle` (D12) inside `ride.h` or its own small module; Agro's movement on the heightmap.
- The camera (D13), the door triggers (D14) through the stage loop and the transition machine.
- Audio: `s8_map` events, `variants`, the footfall scheduler (D15).
- `--stage plain`, `--s8-selftest`, `--s8-stills`, `--s8-e2e`.

### 8.3 The proof (W4)
- `--s8-selftest`: on the plain — whistle from ~60 m (Agro arrives ≤ 3.5 m, `neigh`), mount left,
  kick to walk/trot/gallop (each gait's loop speed ±5 % over 2 s after its start clip ends), steer 90° at the gallop
  (yaw rate per D10), rein down to stop through the stop clips, back up, dismount right, a walk off
  the cliff edge refused; lines `s8-*`; stills `mount.png`, `gallop.png`, `whistle.png`, `plain.png`.
- `--s8-e2e`: the interactive loop with scripted input from the shrine (S7 `--stage shrine`) through
  the door trigger → the plain → whistle → mount → gallop 5 s → stop → dismount → `R` → arena.
- `scripts/s8-acceptance.sh`: the frozen S7 acceptance first, the S8 decode (34 entries), the plain
  manifest gate, both runs, the line/still/pixel/perf asserts, `PASS —`.

### 8.4 The RE/data gate (W2, before the dependent W1 tasks)
- **R1 — the shrine door:** the cathedral's front exit (an sgpr/cgpr door or zone record, or the
  geometry's opening facing **+Z** toward the plain; `cathedral_ext_front` spans shrine-local
  z [−1, 46]) → a trigger box placed where the frozen S7 shrine's walk map reaches (walk it with the
  S7 reach harness; ≥ 5 m from the shrine spawn (0, 8.998, 1)); the plain's matching box on the
  shrine steps (world z ≈ −259…−250) placed where the **plain's** map (backdrops excluded,
  `refuseHoles` on — the ground has a hole under the shrine's footprint) reaches from `spawn_plain`;
  both arrival spawns.
- **R2 — footfall/event frames:** if the `.anb` event tracks decode (they are undecoded today),
  replace the measured touchdown fractions; else keep them (labelled measured).
- **R3 — the whistle approach / riding camera:** a time-boxed second look in the horse controller
  (`FUN_006ea960` neighbours, `ACTIVE_HORSE_WHISTLE` at `FUN_007191f0`); else D12/D13 stand.

## 9 · The builder's calls (with defaults)

Agro's start; the trigger boxes; the yaw rates, camera distances, whistle ranges; the blend length;
the rein hold time; the dismount side rule; the selftest scripts' distances; the stills' framing and
pixel thresholds; the perf gate (the S6/S7 pattern, ≤ 16.7 ms).

## 10 · Risks

| Risk | Severity | Mitigation |
|---|---|---|
| The rider floats/sinks or slides on the saddle | medium | Per-frame `OptBone_boy` anchor; a selftest check of the pelvis–saddle offset; stills |
| Gait switches pop | medium | D7 blends at the three measured gaps; the others are 0° |
| The low-confidence hoof picks sound wrong | medium | Labelled; the ear decides; one map row to swap |
| Agro walks through the cliff edge or into the pond's banks | medium | The same walkability as the player (D3); the e2e |
| The shared re-bake breaks `main` | high | D16: pins relaxed on `main` first; the S7 chain on both branches |
| Frozen numbers move (the stage loop, the ground option) | high | Options are stage-scoped; the frozen S7 chain runs first |

## 11 · The walk (the user's, PENDING after implementation)

- From the shrine, walk out the front door onto the plain; whistle — Agro comes; mount (both sides);
  walk/trot/gallop with kicks, steer, rein in, back up, dismount; ride to the plateau's edge (it
  stops you) and toward the arena (an invisible wall at the stairs, S9).
- The hooves match the gait and the ground, the neigh, the whistle, the "hyah"; nothing pops between
  gaits; Wander sits on the saddle; the window stays and the cursor is free; the fps holds.
- Deferred/known: foliage, jumps, mounted combat, the moving mount, the call voice, music, the arena.

## 12 · Acceptance

Controls/windowed/cursor unchanged; the S7 → … → S3 scripts green and unchanged;
`scripts/s8-acceptance.sh` green; `docs/notes/s8-recon.md` (the gates) and `docs/notes/s8-results.md`
(measured values); assets not committed; the S8 walk added to the user's pending walks.
