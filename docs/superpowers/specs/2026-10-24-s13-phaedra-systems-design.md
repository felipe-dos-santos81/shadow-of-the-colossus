# S13 — The shared systems, proven on Phaedra

**Status:** Proposed — builder calls (the user asked to plan the next milestone and skipped the questions;
2026-10-24). Starts from `main` after S12's merge (e569350). Every frozen acceptance (S3…S12) and every blessed golden
stays green and unchanged. Windowed, the cursor never captured.

## 1 · Intent

S12 proved a colossus can run from his own exported mode graph. The S13 recon then showed that each next colossus needs
**systems** the last one lacked, and that the same few systems recur across all 16: the additive motion layer (every
boss's hurt reactions, all 16), the route graph and its follower kinds 2–9 (10 bosses), the terrain/contact classes of
the arena collision (most bosses), a general control-def condition evaluator (all 16), and attack tables exported from
data (12 bosses). The user chose (2026-10-24) to make S13 a **systems milestone built in parallel lanes**, with Phaedra
as the proof, to speed up the colossi that follow.

S13 builds those systems once, **from the game's own data**, and proves them on the fourth colossus, **Phaedra**
(`colossus_04_kirin`, the `KIRIN` tables, clip stem `kirin_a`, stage `kirins_hill` = `arena_04`), whose fight needs all
of them: she patrols on routes, is lured by where the player stands (tunnel or hill — terrain classes), peeps into the
tunnels, lowers her neck when it is struck (additive), and shakes the player off by region. Success: Phaedra's fight
runs from her exported graph, routes, terrain and events with a short labelled list of authored values; Gaius's S12
outputs are unchanged on the new library code; and a per-boss coverage report says, for each of the 16, what is left.

## 2 · Goals

- **G1 · The exporter, for all 16.** `tools/re/scr/graph_export.py` gains the recon's two fixes (motion-orient tables
  by the boss's motion-def object; typed raw argument keys), exports every boss, and adds the attackdata rows, the
  slowstatus fields it drops today (`+04/+08` route reference/tracking, `+78` judge, `+60…+74`, `+98`, `+9c`, root
  `+00`), the **control-def records** of every control-def a control-assign row names (today S12 hand-built a
  32-key table in the app; R2's 0x68-byte records: opcode, min/max, handle, mask, value, expect), the `scr_dprelate` tables
  (terrain mask/value, object, condition → route id) that `route_by_dprelate` reads, and per
  motion-graph edge the additive reference clip (motion-def `+0x98`), the speed flag (`+0x60` bit 1)
  and the blend-out (`+0x218`). KNIGHT's export stays byte-identical except for the added keys (a golden pins it).
- **G2 · The control-def evaluator.** A general evaluator of all 37 control-def opcodes (`FUN_0062bfd0`, decoded in
  R2 + the census) over the exported records replaces S12's 32-key control-def table. Pad opcodes are false in play;
  its inputs are the requested control bits, mortality (HP ≤ 0), his ground contact, the failure bit, his
  control-mode, this frame's hit (flag, kind, region), the rider's region, his own ground attribute and which clips
  his layers play; the AI's stick X is 0 (S12's reading: the Turn rows fire on their bits).
- **G3 · The shared library.** The interpreter's predicates and actions split into a **shared library** (`0x82…/0x83…`
  functions, any boss) and **per-boss locals** (`0x6c…–0x79…`). S13 implements Phaedra's 29 items and the census's
  cheap shared batch (§7 D5), each unit-tested from exported data rows. Gaius moves onto the shared library and his
  attackdata comes from the export (`kAttack` is deleted).
- **G4 · The additive layer.** The game's layer pool (`FUN_00639520`, compose `FUN_00622470`): requests from the add
  control-assign rows, Continue/Shift/Always/Always-Cancel, the p30/p34 gates, blend-in ramps, cut on stop, the
  reference pose, `boss_motion_in_layers` (the base motion or any layer; with no layers, the base only — Gaius
  unchanged). Opt-in per encounter; Phaedra uses it.
- **G5 · Routes.** `.path` (`qath`) export to `sotc-route/0` and an app follower: the route tree (routes → GPs → AP/PP
  points), kinds 2/5/6/8/9 (and 3/4/7, cheap twins), the nav graph walk over open links, link flags, and the route
  predicates and actions (`route_by_dprelate`, `move_done`, `move_done_ap`, `player_gp_changed`, link set/clear).
- **G6 · Terrain.** `.clb` export of the stage's terrain classes and owner kinds (and the boss-only ground layer) to
  `sotc-terrain/0`, and an app lookup under the player's and the boss's feet that feeds `player_surface`,
  `boss_attr_bits` and control-def ops 0x04/0x4c/0x27-family.
- **G7 · Phaedra playable** on `arena_04`: her bake, sidecar, routes, terrain, encounter table and climb, from her
  graph. **G8 · Coverage.** `make coverage` writes, for each of the 16 bosses, which graph items the app implements and
  which are missing (the strict loader's list, in report mode). **G9 · Proof**: `--s13-selftest`, `--s13-e2e`, a
  runner adapter + goldens, `scripts/s13-acceptance.sh` (the frozen S12 chain first), `docs/notes/s13-results.md`.

## 3 · Non-goals

- Valus and Quadratus stay on their S4/S11 code (the "Fix later" backlog); the coverage report records what each still
  needs (~6 items each after S13, census estimate). Gaius keeps his no-flinch state (the layer is not switched on for
  him: his goldens would move).
- The other 12 bosses' local functions and new root locomotions (flight, the serpent body follower).
- Phaedra's rockfall (`rockfall_cave/hill` spawn layouts and rocks; the timer actions run, the spawners are labelled
  no-ops), the hashigo ladders (separate skeletons, attach point unknown), the proximity latch (`prox_latch_*`, no
  fight effect found), Hard mode (shoulder sigils 0x70/0x90), the cloak (constant false), the bow, the boot cutscene.
- Replacing the climb band capsules with the coli surface (bone frames unsettled); band **regions** do come from data.
- Translucent water, music, Dormin, the world between arenas, audio for Phaedra (silent, D10).

## 4 · Anti-goals

- A `kirin_fight.cpp` of Phaedra-specific behaviour. The glue file may wire world inputs and the climb; anything
  behavioural is the graph, the routes, the terrain or a labelled authored value in `encounters/kirin.h`.
- Committing derived data beyond the existing precedent (small generated numeric headers): no graphs, routes, terrain
  JSON, sidecars or meshes in git.
- A changed golden for S3…S11; an S12 golden changed without a recorded ruling (§13 ruling 3). A one-off per-boss
  copy of a shared predicate.

## 5 · Constraints

As S12 (§5 there): C++20/SDL3/SDL_GPU Metal, windowed, cursor free; bash 3.2; stdlib Python `-I`; `caffeinate -dimsu`;
GPU runs serial; frozen `scripts/s3…s12-acceptance.sh` never modified; a branch bakes into a library view, promoted at
merge (A16); personal use (Oodle, ImageSharp); the converter's parsing stays behind its adapter. **Parallel lanes**:
each lane works in its own git worktree with its own `app/build` and its own `out/verify/<worktree>/`; only one lane
runs the GPU at a time (the controller hands out the GPU; a lane waits for it before any viewer run or `make verify`
that runs GPU steps). All lanes export and bake into the one view `out/libviews/s13`, each into its own files.

## 6 · What the recon found (`.superpowers/sdd/2026-10-23-s13-recon/`)

- **graph/** — the S12 exporter's two bugs (hard-coded motion-orient tables; undecoded args dropped); KIRIN 15 modes, 84
  conditions, 97 transitions, 314 edges; HP head 160 (Normal, `kirin_first`), neck 0x20 is the trigger; the mode-by-
  mode fight (patrol 3, lose-sight 4, attack 6, hill 7→8→9, threat 13, tunnel 10→11/12 peeping, attached 14, death 5);
  anger set 30 per hit, decay 1/s; stomp cooldowns 8/6 s; whistle range 70 m.
- **kirin/** — the bake (75 body clips, 15 s), walk 5.00 m/s, turns ±40°/2 s, sigils (head 15.7 m standing, 5.3 m in
  `watch_loop`, 9.05 m in `add_neck_down_2_loop`), the stomp chains, sidecar 50 KB (+ spawned `kirin_walk_dmg`,
  `kirin_atk_dmg` the exporter's glob misses; the `_wait` clips re-fire impacts — dedupe), `death` needs an authored
  root drop 9.86 → 2.44 m; 25 rootless clips.
- **arena04/** — 10 glbs kept, ground = `ground`, `structures_platform`, `ruins_entrance`, `ruins_skirts`; heightmap
  40.7 MB; a dirt prim leak and a north road ramp (guard `floor y ≤ 56.5`); the tunnels are only reachable if the ruins
  are fed **floors-only** (≈30-line per-line flag); start (984, 52.5, 142) → ground 51.41.
- **additive/** — the layer record, the request/edge rules, the compose formula `q = slerp(I, cur·ref⁻¹, w)·base`,
  `t = base + w·(cur_t − ref_t)`; add clips are stored absolute; ≈350 lines, no converter change; opt-in keeps goldens.
- **census16/** — the union over all 16: 129 predicates, 162 actions, 37 opcodes, kinds −1…10; KIRIN needs 29 items;
  both exporter fixes are byte-identical on KNIGHT/MAMMOTH/MINOTOUR_A; attackdata exports exactly (`kAttack`
  reproduced); every boss has additive rows; whole union ≈45–60 dev-days (guess).
- **path/** — `qath` decoded (144/144 files); nav graph + route tree; frame glb = (600·col − 2700 − x, y, 600·row −
  2700 + z) from the stage cell id (kirin 56 → (900 − x, y, 300 + z); fitted on 3 stages); kinds 2–9 decoded; link bit
  0x4 = closed; KIRIN route 2 = hill (ping-pong PP pacing), routes 3–6 = the four tunnels (outer and inner GPs).
- **clb/** — `.clb` decoded; arena_04's classes: 3 hill, 4 ring corridor, 5 north road, 6–9 the four stair tunnels;
  stage words one bit low (`(w>>47)&0x7f`, owner `(w<<1)&0x3c00000`; the shift's location not found); the boss ground
  layer marks the ruins 0x400000 (deep peeping); same frame as the path; **Phaedra's legs carry no grip faces** — her
  fur is tail, hips, back, shoulders, neck, head; `kirin_standby_col` supports the spawn (1122, 147).
- **bow/** — not needed (no arrow-only row in any graph).

## 7 · Decisions (builder calls)

- **D1 · Lanes.** After a one-task **contracts** step (the JSON shapes — the `sotc-scr-graph/0` additions, `sotc-route/0`,
  `sotc-terrain/0`; the `ScrWorld` additions; the additive and route header APIs; the registry split), six lanes run in
  parallel worktrees, each merging into the `s13` integration branch when green: **L1 exporter + coverage**, **L2
  control-def evaluator + shared library + Gaius migration**, **L3 additive layer**, **L4 routes**, **L5 terrain**,
  **L6 Phaedra content** (bake, sidecar map, root header, arena_04 stage, encounter table, bands). Then **wiring** and
  **proof** run serially. Python/C#/pure-C++ lanes need no GPU; only L2's Gaius check and the proof use it.
- **D2 · Schema.** The graph file keeps its `sotc-scr-graph/0` label and gains **additive** keys: `attackdata`, the
  slowstatus fields, edge `ref`/`speed`/`blendOut`, and root `kind` (ruling 1, §13). `make behaviour` exports all 16
  (the app loads the two it runs). Routes: `<SOTC_LIB>/behaviour/<stem>.route.json`; terrain:
  `<SOTC_LIB>/stage/<arena>.terrain.json` (stage-wide classes/owners + the boss ground layer as its own list, with
  heights — §13.2). All
  derived, never committed; exporters are stdlib under `tools/re/{scr,path,clb}/` with tests and `make` targets
  (`behaviour`, `routes`, `terrain`; all in `make bake`).
- **D3 · Gaius is the regression.** The generic evaluator, the shared library and the exported attackdata must leave
  `tools/verify/golden/s12/` byte-identical. A difference is a bug in the new code until a decode shows S12's table was
  wrong; in that case the plan stops for a ruling recorded in the results note (no silent re-bless; S3…S11 never move).
- **D4 · Strictness stays.** Load still fails on an unknown predicate, action, opcode or kind. The labelled no-ops
  (rockfall spawners, prox latch, cloak false, pad opcodes false) are registered explicitly with a `gap` label that the
  coverage report counts separately from "implemented".
- **D5 · Library scope.** Implemented and data-row tested: KIRIN's 12 predicates, 11 actions, kinds 2/6/8/9 (+3/4/5/7),
  ops 0x04/0x4c; the generic evaluator's 37 opcodes; the census's cheap shared batch (hit_kind, hit_region, hp_pct_ge/le,
  time_since_transition_ge, time_since_attack_ge, anger ge/le/set/add, demo_camera false, mode_is,
  boss_control_mode_is, perceived_now, perceived_bearing_le, player_dist3d_le, turn_to_player, goal_lastseen,
  player_riding). Everything else is listed by the coverage report.
- **D6 · The additive layer** as the recon specifies (formula, gates, cut on stop since every boss ADD has
  `+0x218 = 0`); pool of 4 slots (guess, labelled); reference = the edge's `ref` clip at frame 0, else the clip's own
  frame 0; speed ×1 for flag-`0x2` rows (guess). An empty pool skips the compose entirely (bit-identical). Wired into
  `update_pose`/`blended_palette` only for encounters that opt in (`kirin`).
- **D7 · Routes.** Positions are exported in the glb frame (cell formula; per-stage residual recorded). The follower is
  the decoded kinds over the route tree; goals are reached by a nav-graph walk: Dijkstra over open, class-allowed links
  (walker class 0, guess) from the nearest node with a clear ray to the boss, then straight to the goal; segments are
  walked by S12's slowleg; node heights are snapped to the ground. The library's locomotion step handles kinds −1/0/1/10
  and hands kinds 2–9 to a registry hook the route module installs (an unhandled kind is a load error). Kind 8's AP→GP collision sweep runs against the
  layered heightmap's walls (fallback: stop at the GP, labelled). Link tags toggle bit 0x4 as decoded.
- **D8 · Terrain.** Exported triangles in the glb frame; the app answers "class and owner under point p" by a
  2D-bucketed point-in-triangle test picking the highest classified triangle whose surface lies within [p.y − 1.0, p.y + 0.5] m, else class 0 (unclassified ground is not exported, so the corridor under the ruin roof must not answer for a player on the roof). `player_surface` reads the
  player's class/owner; the boss's owner comes from the boss ground layer under his root (ruins → 0x400000 → mode 12).
  The one-bit rule is encoded in the exporter with the recon's evidence cited; the app sees game-mask values: the
  player's and the boss's ground attribute are each one 64-bit word `class << 48 | owner` in the `ScrWorld`, which
  `player_surface` (when not attached), `boss_attr_bits` and op 0x04 mask as the game does. Gaius's glue keeps his
  S12 stand-in (owner 0x800000 on the arena ground) through the same word.
- **D9 · The arena.** `STAGES["arena_04"]` in `probe.py` (`make arena04-manifest`): the four ground glbs; the ruins
  glbs (`ruins_entrance`, `ruins_skirts`, `ruins_corridors`) fed **floors-only** through a new per-line flag (opt-in, so
  frozen heightmaps cannot move), which makes the stair tunnels and ring corridor walkable and removes the dirt-prim
  phantom floor; the north-road guard; the other kept glbs and water as `backdrop`; the 7 occluder/shadow meshes
  excluded. **The north road stays** (it is terrain class 5, the graph's "tracking abandoned" ground: standing there
  makes her give up); the arena04 recon's `floor y ≤ 56.5` guard is not used. If the layered map's north edge does not
  hold the player at the road's end, an authored backstop box there (as S12's y 34) returns them to the start. Phaedra spawns at (1122, 147) on the platform (`kirin_standby_col`, data-supported) facing the ruins; the
  player at the arena start. No authored root clamp: her walks are the nav graph's.
- **D10 · The climb.** Data: grip regions from `kirin_a_coli.clb` (legs carry none). Bands (measured in the kirin recon
  §3, regions from the coli): tail (hang, 0x1e0) → hips/back (walk, 0x1a0–0x1d0) → withers (walk, shoulders 0x60/0x80)
  → neck low (hang, 0x20) → neck high (0x20) → head (walk, 0x10). **Entry (authored, labelled):** a grab (`E`) on the
  tail band, which is in reach only while she crouches to peep (`watch_*`: tail tip ≈ 1.7 m above ground; an S12-style
  band enable window on her playing clip). The game's own entry is not decoded; the selftest records which is used.
  Striking the neck (region 0x20) feeds the graph's neck-down chain (stage 1 raises the head, stage 2 lowers it: head
  sigil at 9.05 m facing forward), which is how the head is reached; the head sigil (0x10, 160) ends the fight. The
  neck **trigger** is a strike target on the `trigger_01` mesh (the nape, KUBI/KUBI_B, data `+14` = 10): a strike there
  is a hit of region 0x20, kind 0 (sword) fed to the graph, with no HP damage. Bands are posed from her final pose,
  the additive layers included, so the neck band moves with the neck-down.
- **D11 · Hazards and shake-offs from data.** Footstep and stomp damage from the sidecar plus a curated
  `tools/re/objanim/kirin_map.json` for the spawned `kirin_walk_dmg`/`kirin_atk_dmg` (the `_wait` re-fires deduped,
  one hit per stomp chain); the rear-kick boxes; the threat cylinder; S12's "never hit a rider" filter. Shake-offs are
  the graph's control rows: `refuse_tail`/`refuse_body`/`refuse_mune`, `Kirin_body_swing`/`Sudden_braking` in mode 14,
  and the additive `add_neck_refuse_*`/`add_head_refuse` while on the neck/head.
- **D12 · Death.** Forced mode 5 → `kirin_a_death` with an authored root drop (9.86 → 2.44 m, measured) → hold
  `death_loop`; no corpse swap (as S11/S12); no return to the shrine; `R` resets.
- **D13 · Coverage.** `tools/re/scr/coverage.py` (stdlib) reads the 16 exported graphs and a registry manifest the app
  build writes (`scr_registry_dump`, one line per fn VA with `implemented`/`gap`), and prints per boss: items total,
  implemented, gap, missing; `make coverage` writes `out/re/coverage.md`. It is the "next colossus" planning input.

## 8 · The design (sketch; the plan carries the tasks)

1. **Contracts** (one task, serial): schemas (the `/0` additions, `sotc-route/0`, `sotc-terrain/0`), headers with stubs that compile, the registry split, `ScrWorld` fields
   (the player's and the boss's ground attribute words, the route graph, anger, max HP, the layer-playing query), the
   exported control-def records and attackdata parsed by the loader, unit-test fixtures
   generated from the library at test time.
2. **Lanes** (parallel worktrees): L1 exporter (fixes, the `/0` additions, all 16, KNIGHT/KIRIN goldens, `coverage.py`); L2
   evaluator + shared library + Gaius on it (s12 goldens identical); L3 additive (pure + tests: a flinch returns to base
   at its last frame; a 1-frame loop holds its start's delta; the Shift/Cancel gates; empty pool bit-identical); L4
   routes (`tools/re/path/qath.py` + exporter + tests; `app/src/route.{h,cpp}` + tests on kirin.json: hill ping-pong,
   tunnel GP pick by reference, link close/open); L5 terrain (`tools/re/clb/` + exporter + tests; `app/src/terrain.{h,cpp}`
   + tests: the start is class 0, each tunnel mouth's class, the ruins' boss owner); L6 content (`make bake-kirin` 75
   clips byte-sorted + sidecar; `kirin_map.json`; `kirin_root.h`; `arena_04` stage with the floors-only flag;
   `encounters/kirin.h`).
3. **Wiring** (serial): `kirin_fight.{h,cpp}` glue (world inputs, additive opt-in, the climb) on the shared step.
4. **Proof** (serial, GPU): `--s13-selftest` (spawn → patrol → the player enters a tunnel → she routes to its GP and
   peeps → the player leaves by another tunnel → grabs the tail in the window → climbs → neck strike ×2 → neck down 1
   then 2 → head → a shake-off survived → head sigil → death → `R`), `--s13-e2e` through the interactive loop, four
   stills, `tools/verify/s13.sh` + goldens, `scripts/s13-acceptance.sh`, results note, README/AGENTS.

## 9 · Left to the builder

Task boundaries inside the lanes; the `ScrWorld` field names; the coverage report's format; the nav walk's
visibility ray details; the band radii and slopes from the measured table; the selftest's exact script and timings and
which tunnel it uses; the camera framing while she peeps; whether the hill lure (modes 7–9, 13) is scripted in the
selftest or only reachable in play (it must at least run in a unit test from data).

## 10 · Risks

- **The tail entry** is authored; if the tail band cannot be reached in the `watch_*` window, fall back to a grab of the
  neck band from the ruin roof at the tunnel mouth (≈ 54.4 m roof vs her lowered neck), labelled; the selftest records it.
- **The cell-formula frame** is fitted on 3 stages; one shared function serves the route and terrain exporters, and the
  terrain test fails when the median distance from arena_04's stage-collision vertices to its ground glb vertices
  exceeds 0.5 m (measured 0.149 m) — the routes' frame check too.
- **Gaius on new code** (D3) is the largest regression risk; L2 runs `make verify --only s12` before merging.
- **Floors-only ruins** may open other holes; the layered checks (out-of-bounds edges, drops) are re-run and recorded.
- **Lane merges**: conflicts concentrate in `scr_predicates.*`, `Makefile`, `app/CMakeLists.txt`; the contracts step
  creates every new file and target up front so lanes mostly edit their own files.
- **The generic evaluator vs S12's table**: S12's table carries readings (stick X false, pad false, the failure bit);
  L2 first proves the evaluator equals the table on KNIGHT's base control-defs over a grid of world states, then
  deletes the table.
- **Additive fidelity**: the reference frame and the weighted compose are partly guesses; a still mid-neck-down is the
  visual check.

## 11 · The walk (the user's, PENDING after S13)

Phaedra patrols; she notices you and stomps (dodge); you hide in a tunnel and she walks to it and peeps in (light or
deep); you leave by another tunnel and reach her tail while she crouches; the climb; the neck strikes lower her neck;
the head sigil; a shake-off; death; `R`; the ruins' tunnels are walkable and the rim holds; windowed/cursor; fps.

## 12 · Acceptance

`scripts/s13-acceptance.sh` (bash 3.2): the frozen `scripts/s12-acceptance.sh` chain first, then the S13 checks (all 16
graphs export; KNIGHT/KIRIN counts; routes and terrain exported and valid; `make coverage` runs; `--s13-selftest`
gates and stills; `--s13-e2e`; windowed=1 relative_mouse=0; the perf line); `PASS —`. `scripts/verify` gains
`tools/verify/s13.sh` + blessed goldens; S3…S12 goldens byte-identical.

## 13 · Builder-check rulings (2026-10-24; binding, override the text above)

The builder check raised four questions; the user skipped questions, so each is a builder call.

1. **The graph format label stays `/0`.** The frozen `scripts/s12-acceptance.sh` (line 124) and `tools/verify/s12.sh`
   require `"format": "sotc-scr-graph/0"` for KNIGHT, and the frozen script may not change. The new keys are added
   under `/0` (they change no count the frozen checks read); the app's loader reads them, and Gaius's attackdata comes
   from the export (`kAttack` is deleted, G3 holds). A later incompatible change takes a new label and a new file name.
2. **Phaedra stands on the boss ground layer.** L5 exports `HILL_BOSS_COL`'s triangles with their heights (not only
   the owner bits). Her root height = the boss layer under her root where it exists (the ruins block, y 50.7–55.2,
   which lets her step onto the ruin roof where the inner GPs sit), else the layered heightmap within S12's 2.0 m step.
   Route node heights snap to that same ground. Depth peeping (mode 12, deep `watch_*`) therefore happens when she
   stands on the ruins, which is where the tail entry (D10) is reachable: the selftest picks an approach that ends on
   an inner GP and records which GP and peeping mode it got; if no approach does, the §10 fallback (the neck from the
   ruin roof) becomes the route, labelled.
3. **The builder rules on an S12 golden difference.** If D3's check shows a difference and a decode proves S12's
   table was wrong, the builder re-blesses **only** `tools/verify/golden/s12/`, with each changed line explained in
   `docs/notes/s13-results.md`, and the frozen `scripts/s12-acceptance.sh` must still pass unchanged. Without such a
   proof the new code is fixed instead. S3…S11 goldens never move.
4. **The stomp foot-IK aim (`+d0/+e0`) is a labelled gap.** The exporter writes the fields; the app plays the stomps
   as authored clips (the hoof lands at the clip's fixed offsets) and the coverage report counts the aim as a gap. The
   selftest's dodge is measured against the sidecar's posed volumes, as S12's was.
