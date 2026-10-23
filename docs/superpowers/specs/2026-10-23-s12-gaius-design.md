# S12 — The third colossus, from the game's own tables

**Status:** Proposed — builder call (the user asked to plan the next milestone and skipped the questions;
2026-10-23). Starts from `main` after S11 (c33d714). Every frozen acceptance (S3…S11) and every blessed golden stays
green and unchanged. Windowed, the cursor never captured.

## 1 · Intent

S11 proved a colossus can be a data table, but Quadratus still needed ~1,000 lines of colossus-specific fight code,
and every number in it was either read by hand from the eboot or authored. The S11/S12 recon then found that the
original game ships **its own behaviour engine as data**: each colossus is a mode graph (`scr_mode_status` /
`scr_mode_orient` / `scr_condition_def` tables in `eboot.elf`), evaluated by one walker over ~40 shared predicates
for the colossi studied so far; its attacks, footsteps and damage are per-clip event tracks in `objanim_*.anb` files
the converter does not read yet.

S12 builds the two pieces that let later colossi be **exported, not written** — a graph interpreter in the app and
an objanim reader in the converter — and proves them on the third colossus, **Gaius** (`colossus_03_knight`, the
`KNIGHT` tables, clip stem `knight_a`), on his lake arena (`arena_03`), with his signature mechanic: climbing his
sword while it is stuck in the ground. Success: Gaius's fight runs from his exported graph and his exported events
with only a short, labelled list of authored values, and the scripted proof and the user's walk show the fight.

## 2 · Goals

- **G1 · The graph, exported.** A tool reads `out/re/eboot.elf` and writes `sotc-scr-graph/0` JSON per boss
  (`<library>/behaviour/<BOSS>.graph.json`; derived, never committed). Gaius's (`KNIGHT`) is the one the app runs.
- **G2 · The graph, run.** An app-side interpreter that follows the game's walker contract exactly (recon
  `graph/findings.md` §1: forced death, entry/exit motion gates, ordered first-match transitions with the timer-2
  minimum, enter/update/exit actions and controls, the two timers' reset rules) and evaluates conditions exactly
  (§2: first-record combiner, NOT via `expect`, null = true). It implements every predicate and action kind Gaius's
  graph uses (24 predicates; his action kinds) and refuses to load a graph that uses one it does not know.
- **G3 · Events, exported and used.** A converter `ObjAnimReader` and a `<glb stem>.events.json` sidecar (schema
  from the recon: damage types 3/10/13, effect events 29, judge volumes 44/46; type 11 debris dropped in S12), plus a
  small curated `tools/re/objanim/knight_map.json` for the effect-name → spawned-damage link that lives in the
  eboot. The app takes Gaius's damage volumes, his footstep-spawned damage and his judge volumes from it.
- **G4 · Gaius playable.** His bake (62 clips, 35 joints), his arena (`arena_03`), his encounter table, his climb
  (stuck sword → forearm → upper arm → shoulder → neck → head), his two weak points (head, belly), his shake-off,
  his death; the fight starts at the arena's player start.
- **G5 · Proof.** `--s12-selftest` and `--s12-e2e`, a runner adapter + blessed goldens, `scripts/s12-acceptance.sh`
  (the frozen S11 chain first), `docs/notes/s12-results.md` with a walk checklist.

## 3 · Non-goals

- **Valus does not change** (the user: "Fix later" — a separate data pass with a deliberate re-bless).
- **Quadratus does not move onto the interpreter in S12.** His S11 code and goldens stay. A unit test may run his
  exported graph through the interpreter as a smoke check (load + step without unknown predicates is not required —
  he uses predicates Gaius does not); his migration is recorded as the next debt.
- The armour break (flags 266/267: set by a *failed* slam on collision class 0xc00, whose arena surfaces are not
  decoded), Hard mode (the left-arm sigil, region 0x110), the eye graph, the arrow hit kinds.
- Translucent water (the lake draws opaque as a `backdrop`; the blended pass is ~100 lines and is deferred),
  skirts' second UV set, music, Dormin, cutscenes, the world between arenas.

## 4 · Anti-goals

- A second hand-written fight file like `quadratus_fight.cpp` for Gaius. If a behaviour cannot come from his graph
  or events, it is an authored value in his encounter table with a label, not code keyed on his identity.
- Committing anything derived from game data beyond small generated numeric tables with the existing precedent
  (`ride_points.h`, `quadratus_root.h`): no exported graphs, no event sidecars, no mesh vertices in git.
- A changed golden for S3…S11.

## 5 · Constraints

C++20/SDL3/SDL_GPU (Metal), windowed, cursor free; bash 3.2 scripts; stdlib Python tools; `caffeinate -dimsu`
around every viewer/acceptance run; GPU runs serial; frozen `scripts/s3…s11-acceptance.sh` never modified; branches
bake into a library view (`make libview`), promoted to the base at merge (S11 ruling A16 pattern); personal use
(Oodle, ImageSharp never redistributed); the converter's parsing stays behind its adapter (`AnimBridge`/new
`ObjAnimReader` read our decoded records, not `AquaModelLibrary` types).

## 6 · What the recon found (2026-10-23; `.superpowers/sdd/2026-10-23-s12-recon/`)

- **graph/** — walker `FUN_0082c080`, evaluator `FUN_0082d9f0`, action runner `FUN_0082bfb0` decoded; 129
  predicates across the 16 colossi, 39 for the three studied (Valus 29, Quadratus 27, Gaius 24); app today: 15 yes,
  7 partial, 16 new state (perception `sensed` latch from `scr_relposparam`, anger per hit kind, a path follower,
  the whistle event, flags), 1 stub. Gaius's graph: 13 modes, 53 conditions, 40 transitions, 171 motion edges. An
  exporter (`py/graphwalk.py export`) already writes the format end to end down to clip names.
- **knight/** — Gaius: HP 170 (255 Hard; guess on meaning); head (region 0x10) and belly ("MOUTH", 0x50) live from
  spawn, ~86 points each; no sprain/down — hits play additive hurt clips; shake-off by region (head/neck →
  `refuse_head`, chest/belly/back/hips → `refuse_body`, right arm/shoulder/sword (0xa0) → `refuse_arm_r`, left arm →
  `refuse_arm_l`); swings only from stand/walk, 10 s cooldown (5 s on the head), forced stop after 20 s walking with
  the player attached; slam when three rays 35 m ahead (centre, ±20 m) are clear, else stab; slam trigger 30 m ahead
  r 5; no anger. The stuck sword is not a timed mode: the slam chain `atk1_start → atk1 → atk1_2 → atk1_3 → atk1_end`
  advances on clip ends.
- **sword/** — the blade is still 3.07 s (`atk1_2` f6 → `atk1_3` f86); 2.0 m wide, 30.8–32.9°, 11.9 m from where
  its top meets flat ground; the "fling" volume ends `atk1_3` f8 → the climb window is `atk1_3` f9–84; on the tilted
  disc (≈5.5°) its low end can float 1.6–2.9 m above the ground; the arm moves 4–6 m during the hold, upper-arm slope
  49.5–64.1°; measured band chain (blade, forearm, upper arm, shoulder, neck, head; radii, slopes, walk/hang); walk
  6.667 m/s, turns +90.01°/−89.98°/−179.97° (yaw in the bones), x negated as for Quadratus; 14 clips without a root
  series (incl. `boot`) must never play raw; the slam tip reaches 350 m/s.
- **objanim/** — judge volumes (44/46) are AI triggers on the player's position point; damage is 3/10/13, in-clip
  or spawned by type-29 events; Knight has no type 46; every Knight footstep spawns `knight_walk_dmg` (walking
  stomps); type-29 events land on measured touchdowns (8/8, `MAM_ATK` = 1.834 s); volume tests decoded (box size =
  half extents; sectors about local +z); sidecar sizes Knight ≈38 KB without type 11. Open: whether the default
  (`_skelton`) sectors open forward.
- **arena03/** — 24 glbs → 10 kept (838,527 vertices, 82 PNGs); only the tower glbs as ground → a 4.8 MB layered
  heightmap (all-instance would be 139.6 MB); the layered rules already seal the disc (rim r 47.5–53.0 m, no
  leaks); water = one plane at y 17.0, opaque as a `backdrop`; all 62 clips map into the 35 joints; the player start
  (−301, −1375) is on the disc at y 43.33.
- **render/** — the renderer has one opaque scene pipeline; a blended pass is ~100 lines (deferred, §3).

## 7 · Decisions (builder calls unless marked)

- **D1 · Gaius on the interpreter; nothing else moves.** Valus and Quadratus keep their S4/S11 paths and goldens.
- **D2 · The graph file is derived data in the library.** `tools/re/scr/graph_export.py` (the recon exporter,
  productised, stdlib) reads `out/re/eboot.elf` and writes `<SOTC_LIB>/behaviour/KNIGHT.graph.json`; `make
  behaviour` runs it (and is part of `make bake`). The exporter, not the app, resolves table handles; the app reads
  only the JSON. A golden test pins Gaius's export (mode/condition/transition counts 13/53/40, named spot rows).
- **D3 · The interpreter is generic and strict.** `app/src/scr_graph.{h,cpp}` (pure: no SDL, no GPU): load,
  validate (every predicate/action kind known, every clip present in the glb, every mode reachable or flagged),
  step(dt, world view) → requested controls + events. World inputs come through one `ScrWorld` view struct the app
  fills each frame (player position/attachment/region/motion, hit record, boss motion current/previous/switched,
  distances/bearings, ground and ray queries, flags) — the predicates read only that struct, so they are unit-tested
  from data rows without a scene.
- **D4 · Controls become clips through the exported motion graph** (control-mode → control-assign → motion-orient
  edge → clip), as in the game; blends use the edges' frame counts (`p30`/`p34` as decoded; label the reading).
- **D5 · New state, minimal and from data.** Perception: the `sensed` latch from the mode's `scr_relposparam` (10
  floats). Path following: straight-line goals (player ground position, last-seen) with ground follow on the layered
  heightmap and the arena bound (D9); `arrived_goal` / `lost_target` from distance thresholds in the data where
  present, else authored and labelled. Flags: a small `std::bitset` keyed by the data's flag ids. The whistle event:
  the player's whistle input (S8) sets it.
- **D6 · Events from the sidecar.** Damage rows are posed each frame from their parent joint (or the entity) and
  tested against the player capsule inside their frame windows; a fast row (the slam) is swept between the previous
  and the current pose (sub-steps, labelled). Each row hits at most once per clip instance; when rows overlap the
  highest amount wins. Spawned damage (footsteps) runs its own timeline at the spawn transform. Judge volumes answer
  the graph's "player inside volume NAME" predicates. Default (`_skelton`) sectors open **forward** (the recon's
  data-consistent reading — a guess; a walk row checks it).
- **D7 · The stuck-sword climb.** The blade is a band (`OptBone_IK_atk → OptBone_sword`, radii 0.33 → 0.55 m,
  `horizontal`, walkable) that exists only while his playing clip is `atk1_3` and its frame is in 9…84 (both from
  the data/measurement; the enable rule is a field on the band: clip name + frame window). When it disables under the
  player, the player falls. **Entry:** a grab (E) at the blade's low end, not a walk-on — the tilted disc leaves the
  low end 1.6–2.9 m up, beyond the 1.0 m step (authored deviation from the game's walk-on; labelled). The chain then
  follows the measured bands: forearm (walk), upper arm (hang), shoulder (walk), neck (hang), head (walk).
- **D8 · A slope gate on walkable bands.** A `horizontal` band is walked only while its top normal's y ≥ cos 60°;
  steeper, the player hangs (climbs) instead. Applies to every encounter — Valus has no horizontal bands and
  Quadratus's back stays under 60° in his scripted route (the goldens prove it); if a frozen golden moves, the gate
  becomes per-band opt-in instead.
- **D9 · The arena.** `arena_03` STAGES entry in `probe.py`: the four tower glbs as ground, the other kept glbs and
  the water plane as `backdrop` (composer + check-manifest gain the road stage's `backdrop` support); the encounter
  `encounter knight`; Gaius spawns at the authored (−300, −1420) ground 47.657 facing the player start; his root is
  clamped within 36 m of (−300, −1400) (authored; his graph never tests walkability); a player below y 34 is reset to
  the start (fall backstop, authored).
- **D10 · Silent fight** as in S11 (no curated Gaius audio map yet); the sidecar's footstep events are where cues
  will attach later.
- **D11 · Bake and promotion.** `make bake-knight` (62 clips, byte-sorted, by name) into the branch's view; promoted
  to the base library at merge with `bake-knight` and `behaviour` joining `make bake` (A16 pattern). `knight_root.h`
  generated by a twin of `tools/re/s11/quadratus_root.py` (root x/z per frame with x negated, turn headings per
  frame; numbers only).
- **D12 · Death and end.** At HP 0 the graph's root death mode plays `knight_death` (hold its last frame); the
  sidecar's `KNIGHT_PLAYER_OUT` (MUNE r 30, f109–180) throws the player off; `returnWired` off in arena_03 as in
  arena_02 — the corpse stays, `R` resets.

## 8 · The design (sketch; the plan carries the tasks)

Lanes that can run in parallel after a short shared-interface step (the `ScrWorld` struct, the sidecar schema, the
band enable field):
1. **Exporter** — `tools/re/scr/graph_export.py` + its tests + `make behaviour`.
2. **Converter** — `ObjAnimReader` (C#) + `<glb>.events.json` writer + `knight_map.json` + xUnit tests (the recon's
   pinned rows: Quadratus `attack_r` t46 f0 = (18.0875, 2.2019, 37.2787) size (20, 15, 30); `knight_a_walk`
   `KNIGHT_FOOT_R` bone 8 event [40, 42]).
3. **Interpreter** — `scr_graph.*` + Gaius's 24 predicates and his action kinds + unit tests driven by his exported
   graph (fixtures generated at test time from the library, never committed).
4. **Arena + bake** — `arena_03` stage, `make bake-knight`, `knight_root.h`, his encounter table
   (`encounters/knight.h`: joints, bands with the D7/D8 fields, weak points, capsules, props, death, target camera).
5. **Climb** — band enable windows, the slope gate, the blade grab entry, the fall on disable (pure + unit tests;
   frozen goldens prove no S11 change).
Then **wiring** (the interpreter + events + climb in the frame step for the `knight` encounter) and **proof**
(`--s12-selftest`: approach → dodge a slam → grab the blade's low end in the window → walk the blade → forearm →
upper arm → shoulder → neck → head → strike the head sigil → a shake-off survived → the belly sigil (route the plan
picks) → death → the corpse stays; `--s12-e2e` through the interactive loop; four stills), then **acceptance**.

## 9 · Left to the builder

The `ScrWorld` field list; the predicate implementation details within the decoded semantics; sub-step counts for
the swept test; the camera framing for the blade climb; the belly route (from the measured bands); the selftest's
exact script and timings; whether the exporter also writes MAMMOTH/MINOTOUR_A graphs (allowed, unused).

## 10 · Risks

- **Undecoded predicate corners** (path follower fields, `player_surface` attribute meanings): mitigated by
  implementing exactly Gaius's 24 and failing loudly on anything else.
- **The forward-sector guess (D6)** decides where he aims: a walk row and a selftest line record which judge volume
  fired.
- **The blade window is short** (75 frames = 2.5 s at 30 fps) and the walk up it takes 2.4–5.4 s: the player may not
  reach the fist before it lifts. The game's answer is that he lifts you with the sword; S12's: the fall (D7). If the
  selftest cannot climb in one window it may take two slams (record the count).
- **The slope gate (D8)** touching Quadratus's route: the goldens catch it; fallback is per-band opt-in.
- **Interpreter fidelity vs. feel**: the game's numbers may make the fight feel different from S11's authored
  Quadratus; that is the point, and the walk judges it.

## 11 · The walk (the user's, PENDING after S12)

Gaius approaches and swings (slam when the ground ahead is clear, stab otherwise); a slam's blade can be grabbed at
its low end and walked; the climb chain; the head sigil; a shake-off; the belly sigil; death; the corpse; `R`; the
water reads as a lake; the rim (no fall-through; the backstop); windowed/cursor; fps; whether he aims at you (D6).

## 12 · Acceptance

`scripts/s12-acceptance.sh` (bash 3.2): the frozen `scripts/s11-acceptance.sh` chain first, then the S12 checks
(export counts, sidecar present and valid for knight_a, `--s12-selftest` gates and stills, `--s12-e2e`, windowed=1
relative_mouse=0, the perf line); `PASS —`. `scripts/verify` gains `tools/verify/s12.sh` + blessed goldens; s3…s11
goldens byte-identical.

## 13 · Builder-check rulings (2026-10-23; binding, override the text above)

The builder check (`.superpowers/sdd/2026-10-23-s12-recon/builder-check.md`) raised eight questions; the user
skipped questions, so each is a builder call. S12 therefore opens with **RE gates (R1–R5)**, each time-boxed, each
with a named fallback that the plan implements if the gate does not close.

1. **Attacks, walking and turns (Q1).** They are issued by the attackdata rows and locomotion kinds (0, 1, 10) via
   `FUN_0085a960`, not by the graph's actions. **R1:** decode `FUN_0085a960` and the three locomotion kinds.
   **Fallback (authored, labelled):** an attackdata runner — while the mode's attackdata is armed, the row fires its
   issue control when the player point is inside its ATK volume and outside its FIX volume (the volumes from the
   sidecar's default judge set), with ATTAIN as the approach goal; locomotion kind 0 walks toward the goal (turn
   controls when the bearing leaves ±30°, the data's turn trigger), kind 1 turns in place, kind 10 holds position
   and lets the attackdata runner act. D6's sentence "judge volumes answer the graph's predicates" is withdrawn:
   judge volumes feed this runner.
2. **Control-assign conditions (Q2).** **R2:** decode the control-def condition opcodes (time-boxed). **Fallback:**
   a row fires when its `ctrl` bit is requested; condition-only rows are mapped by name — the hurt rows to a landed
   hit on the matching region, landing to his ground contact, mortality to HP ≤ 0 (which also satisfies the forced
   death gate); any other condition-only row is listed and ignored. The strict loader still refuses unknown
   predicates and action kinds; control-defs are not on that list.
3. **Collision attributes (Q3), authored stand-ins:** the arena's ground (the tower glbs) is owner kind 0x400000 and
   0x800000 (the battle area); EVENT1 is not needed (ruling 7); `swing_space_clear`: each of the three 35 m rays,
   sampled every 1 m, is clear unless the layered ground rises more than 1.0 m above his root's ground (walls block,
   drop-offs do not); `ray_ctrl`'s failure probe never reports class 0xc00 (the armour break is a non-goal), so
   slams never fail.
4. **The blade window and pace (Q4).** **R3:** settle the window from the data — the end is the earliest of the
   measured stillness end (f84–86) and the `KNIGHT_PULLSWD` event (`atk1_3` f75); the start is after the fling
   volume (f9). The blade is walked at the run speed (5 m/s, not the 2.2 m/s surface walk). When the blade disables
   with the player on its upper 40 %, the player is carried onto the forearm band (the game's "he lifts you with the
   sword"; authored threshold); lower, the player falls. The selftest climbs in one slam.
5. **Damage on the climbing player.** `KNIGHT_ATK_SHOCK2` is live in `atk1_3` f0–94 over the blade. Ruling: a
   colossus's damage rows never hit a player who is attached to, or standing on, that colossus (authored; recorded as
   the likely reaction filter, unverified).
6. **Volume space (Q5).** **R4:** resolve the damage-volume pose space against measured references (the slam reach
   28–30.6 m, the measured foot touchdowns, Quadratus's validated t46 frame). **Fallback:** the parent joint with
   the recon's best convention, each row's residual error recorded in the results; the walk judges the hits.
7. **Additive clips (Q6).** No additive layer in S12: rows that request additive clips (hurt, arm raises,
   discovery) are dropped and listed as a gap — the same no-flinch state as Quadratus.
8. **Start mode (Q7).** The fight starts in the graph's **retry start mode** (3, route_patrol); the standby → movie →
   `boot` path is skipped (the cutscene is a non-goal; `boot` has no root series).
9. **Bearing sign (Q8).** **R5:** pin `player_bearing_in`'s sign from the `FUN_00833a70` decompile and the S11
   Valus/Quadratus turn data; a unit test names it, and a walk row checks which foot he stomps with.
10. **What "from his graph" claims.** The results note reports, per behaviour, whether it came from the exported
    graph, from the sidecar, from an R-gate decode, or from an authored fallback — the S12 success line is "a short,
    labelled list of authored values", measured by that table.
