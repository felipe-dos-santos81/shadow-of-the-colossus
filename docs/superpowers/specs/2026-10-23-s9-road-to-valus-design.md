# S9 — "The road to Valus": the loop closes on the map

**Status:** Proposed — builder call (the user skipped the question pass and asked the builder to pick
and plan the milestone, 2026-10-23); revised the same day after a builder check and a code fact-check.
**S9 starts from `main` after S8 is merged** (S8's Tasks 8–10 and its final review are prerequisites). The binding milestone after S8 "Agro". S8/S7/S6/S5/S4.2/S4.1/S4/S3
stay green and unchanged.

## 1 · Intent

After S8 the player can ride Agro across the plain in front of the shrine, but the plain ends at an
invisible wall where Valus's arena begins, and nothing tells you where the colossus is. In the game you
raise your sword in the sunlight, its beam points the way, you ride to the arena's entrance, climb the
last stretch on foot, and fight. This milestone adds that: the arena's entrance stairs and corridor
become part of the plain, the corridor's dead end is a door into Valus's arena, and the sword's light
beam points to him. With it, the whole loop of the first colossus is playable on the map: shrine →
door → plain → whistle → ride → raise the sword → entrance → arena → fight → death → shrine.

**Why this milestone (builder's pick, from three recons on 2026-10-23).** The plain and the arena share
the world frame and the plain's ground meets the entrance stairs seamlessly; the binary yields the
beam's targeting, focus curves, shade test and effects with addresses; the beam's atlases, Wander's 14
raise clips and the three sword cues are all decodable. The second colossus still needs the app's
Valus-specific encounter generalised first (S10).

## 2 · Goals

- **G1 — The entrance.** The plain gains the arena's entrance set (stairs, corridor; the game's own
  geometry) as ground, so the player — on foot or on Agro — rides up the stairs into the corridor; the
  arena itself is drawn behind it as a backdrop.
- **G2 — The arena door.** The corridor's dead end (the foot of the cliff the game makes you climb) is
  a door into the arena stage at its own spawn, and the arena has a door back down to the corridor;
  Agro waits on the plain where you left her.
- **G3 — The sword's light.** Hold a key to raise the sword (the game's own raise clips, on foot and
  mounted); in sunlight a fan of eight beams forms from the sword's tip and closes into one as the
  camera lines up with Valus, then a ray finds the ground and sparks; in shade it fades. The game's
  rules (search mode) and assets, labelled where authored.
- **G4 — The sound.** The sword's shimmer (`sword_test`, `sword_boss_test`) as audition-picked cues in a
  new `s9_map.json` (= the S8 map + the S9 rows).
- **G5 — The proof.** `--s9-selftest` (the beam's fan, lock and shade; the entrance; both arena doors) and
  `--s9-e2e` (the whole loop from the shrine to Valus's death and back to the shrine, scripted through
  the interactive loop), run by `scripts/s9-acceptance.sh` after the frozen S8 acceptance.

## 3 · Non-goals

- Persistent world state: every arena entry is the opening state (Valus alive, fresh encounter); after
  his death the loop returns to the shrine and starts over — no save, no progress, the beam always
  points to Valus.
- Moving while the sword is raised on foot (raising stops the player; turning stays), and the mounted
  raise at a walk/trot/gallop (the game layers additive clips the app can't; the mounted raise is
  Stand-only, labelled).
- Climbing the cliff into the bowl (the door stands in for it), riding into the bowl, Valus seen alive
  from the plain or an engagement radius (the arena stays its own stage), the sky beam over the
  colossus (`sky_bossbeam_*`), the beam's weak-point mode on Valus's body (`s_line_boss`), the sword's
  glow material, the second colossus, foliage, music on the plain.

## 4 · Anti-goals

- **A1 — A fake beam.** Generic light shafts would pass a still and miss the point: the beam uses the
  game's atlases, its eight-line fan, its focus/width curves and its search-mode target; what is
  authored (the button, the sun direction and the shade test, the fade time) is labelled.
- **A2 — Breaking what exists.** Every frozen acceptance (S3…S8) and their numbers stay identical, the
  S8 plain stage as S8's selftests load it included; S9 adds `s9-*` lines only. Interactive play
  changes on purpose: the arena gains its door down to the corridor.
- **A3 — A stuck loop.** Every door can be walked through both ways; `R` still restores the arena's
  opening state from anywhere; the beam never locks input.
- **A4 — Cursor capture or a fullscreen window.**

## 5 · Constraints

- Apple Silicon, C++20, SDL3/SDL_GPU/Metal, arm64; no new runtime dependency; offline-first; game tree
  read-only; assets/derived files gitignored; RE notes carry addresses/counts, no game content.
- Frozen and invoked unchanged: `scripts/s8-acceptance.sh` … `s3-acceptance.sh`, `plain.manifest` and
  its probe pins (S8's acceptance checks them), `s5/s6/s8_map.json`, every existing selftest's output.
- `out/library` is shared: Wander's existing clips keep their indices; new clips are appended; `main`'s
  suites stay green.
- Bash 3.2; `caffeinate -dimsu`; never two acceptances at once; ≤ 4 concurrent agents.

## 6 · What the data and the binary say (the 2026-10-23 recons)

**The entrance.** The arena's 30 instances use identity transforms in the world frame. The plain's
ground meets `arena_01_entrance_stairs` (x 297–323, z 249.8–279) seamlessly (y −1.48 at z 250 on both);
the stairs rise −1.47 → 2.43 by z 270 (largest step 0.48 m per 1 m node), the entrance floor
(`arena_01_entrance_ground`, z 272–299) rises to ≈ 5.6; the game ships a 13-triangle horse ramp over the
stairs (`arena_01_entrance_collision_horse_collision.glb`). Under S8's ground rule the plain + the 16
entrance glbs reach 509,577 nodes up to z 296.7. The bowl (y 28–30) is a ~23 m cliff above the
corridor's end and is unreachable without a climb; `arena_01_entrance_demoblocker` is an invisible wall
at z 269.6–276.6 (exclude it). Of the encounter manifest's entrance glbs, seven are 72-byte 0-mesh
stubs and two are LOD duplicates (`_stairs_lod1`, `_entrance_low1`) — the S7/S8 `exclude_stub`/
`exclude_lod` rules leave ~9 real entrance glbs. Merging the bowl into the plain's ground would raise 139 of the fight's
walkable nodes (≤ 41.9 m) — the arena stays its own stage. The arena's own spawn is
`playerstartpositions.txt`'s (353, 27.951, 345), yaw 49.24°. On the app's **unlayered** maps (the
arena stage and the road both build unlayered ground) the corridor floor reachable on the road is
x 300–308, z 280–296, y 2.7–5.6; (315.5, 291) reads 31 m there (the overhang), so the reverse door
and both arrival spawns are placed on the unlayered maps. Valus stands at (382, 28.369, 370); his chest `Bone_MUNE` is 10.93 m above his feet
(`docs/notes/s4-results.md`). From the open plain the line to him climbs ~3.6° and meets no ground
within 150 m; near the stairs (e.g. from (310, −1.5, 240)) it meets the entrance at ~21.5 m. `arena_01.glb` as a backdrop
is 21 meshes / 151k vertices.

**The beam (`FUN_0077c480`; addresses are VAs).** Search mode (`s_line`, used while the colossus is not
yet active) targets the current colossus record's position (`FUN_007d1890`). The aim `dot` =
dir(sword tip → target) · dir(sword tip → the camera point `DAT_02dcdd10`). Off-angle
`k = clamp01(90·(1 − dot) / 180)`; eight lines 45° apart around the sword axis lean outward by
`k · 75°`; `sword_line_width` → `clamp01((dot − 0.6) / 0.4) · 1.35`, smoothed 0.1 per step. Locked on
(`k < 0.01`): a ray of length `raise · 150` along the aim; on a hit, `s_reaction` +
`s_reaction_subtractive` at the hit point and a `touch` one-shot with a 0.5 s cooldown. Raise blends:
`clamp01((v − 0.735)/0.42)` (lines `/0.525`; mounted 0.63–0.84; `v`'s meaning unknown). Sunlight: the
sword position sampled in a 2500-unit orthographic sun texture × an area scalar; visibility =
1 − in-light, moving at `cp10_swordInLightInterpSpeed` 0.3 per step (step unit unverified). Effects:
`particles/shadow/weapon/sword/_ps4/sword_line.cprt` (emitters `sword_beam_base`, `sword_beam_overlay`,
`flare_center`; atlases `vertical_solid_beams_01_pfx`, `vertical_beams_wave_01_pfx`,
`soft_glow_01_falloff_pfx` — the `8c0a` CTXR family the converter decodes) plus `sword_reaction`,
`sword_flare_tip`. Clips (30 fps, decodable): `hoist_sword_start/loop/end` (30/50/29 f),
`uma_hoist_sword_start/loop/end` (35/60/25 f), turns and additive variants. Sound: `com_obj.sxd`
`sword_test` (looped base), `sword_boss_test` (search); `com_amb.sxd` `Sword_reflection`; `touch` not
found. The raise button: the binary's input mapping is unresolved, but the game's own prompt captured in
`docs/notes/re-capture.md` says R2 — which the app already uses for the grip. Wander's skeleton has
`OptBone_sword` but no tip bone; the sword is a separate unrigged `wander_sword.glb` (blade from local
z −0.58 to +0.12). The clip names are `player_a_hoist_sword_{start,loop,end}` and
`player_a_uma_hoist_sword_{start,loop,end}`; Wander carries 43 clips after S8.

**The app (after S8).** Stages Arena / Shrine / Plain through the stage loop; the S8 door machine
(`door.h`) and trigger boxes; the S6 effect pass (`effect.h`) draws one fixed tendril layout (two
402-vertex ribbons + billboards) from the `death_tentacle` atlases; the renderer's lighting has one
fixed world-space sun direction, the shader literal `normalize(float3(0.4, 0.8, 0.6))` (~48° up) in
`mesh.metal`; `EffectRenderer` is built once per device with a fixed array of four tendril textures,
a tendril-sized vertex buffer and per-root index buffers, and is created only for encounter stages;
the mixer's `play` returns no handle and cannot stop a looping one-shot (`audio.h`); `StageCarry` holds
no horse state; `--stage shrine` leaves the S7 return machine at its terminal `Free`, which only `R`
resets; `heightmap_sample` clamps to the map edge and fills holes from the nearest node; keys in use: WASD, Shift, E, J, R, M, F, T, Q, Space, `[`/`]`,
Escape; gamepad: sticks, RT, west, north (whistle), south (mount), Start.

## 7 · Decisions

| # | Decision | By | Why / cost if wrong |
|---|---|---|---|
| D1 | S9 = the road to Valus (entrance + arena doors + the sword's light) | Builder | §1. Cost: the second colossus waits |
| D2 | A new probe stage `road`, written as its own composer (the probe's shrine/plain helpers read one set and apply one backdrop offset): S8's plain lines + the real entrance glbs as ground (the S7/S8 stub/LOD exclusions, minus the demoblocker and occluder) + `arena_01.glb` as a `backdrop` at identity (per-backdrop TRS) composed to `out/re/stage/road.manifest`; the S8 `plain` stage and manifest stay byte-identical (S8's acceptance pins them); the app's Plain stage loads `road.manifest` in interactive play and S9 runs, and `plain.manifest` under `--s8-selftest`/`--s8-e2e` | Builder | S8 is frozen. Cost: two plain manifests |
| D3 | The arena door: a D14 trigger box at the corridor's dead end, placed on the road's **unlayered** map where it reaches (≈ x 300–308, z 286–296) → the Arena stage at (353, 27.951, 345, yaw 49.24°); the reverse door in the arena placed where the arena's own unlayered map reaches from its spawn → the road at an arrival spawn below the cliff; the S8 door machine, unchanged. Loading the arena through the door resets the S7 return machine (`carry.ret = reset_return()`) and starts a fresh encounter, so Valus's death returns to the shrine as from the arena start | Builder | The bowl is a climb the app can't do; the fade sits where the game makes you dismount. Cost: a fade instead of a climb |
| D4 | Agro is not carried into the arena; `StageCarry` gains an optional horse pose (position, yaw), set when the player leaves the road through the arena door (a mounted rider is dismounted first — the door only fires on foot), used instead of the manifest's `horse` line on the next road load, and cleared by `R` | Builder | The game leaves the horse below. Cost: one carried pose |
| D5 | The beam is a labelled reconstruction of search mode: target = Valus's world position (the encounter manifest's entity placement, transcribed — he isn't loaded on the road) at his chest (`Bone_MUNE`, +10.93 m); the aim is the camera's look direction (the builder's reading of the binary's camera point `DAT_02dcdd10`, labelled); the fan, width, lock and ray follow §6's curves verbatim; the sword is `wander_sword.glb` attached to `OptBone_sword` (the `club_model` pattern) and the beam starts at its tip (≈ (0, 0, −0.58) in the bone's frame; else the hand bone + an offset, labelled); the ray marches **raw nodes** (NaN or out of bounds = no hit — never `heightmap_sample`, which clamps and fills) through the road's ground and a second unlayered **ray grid** rasterized from the backdrop meshes (the shrine, the arena), so a locked ray sparks on the visible cliff; raise blend over the raise clip's start duration (the `v` curve is unknown) | Binary + builder | The game's rules where known. Cost: the authored aim reading, tip and raise blend; a second height grid |
| D6 | Sunlight is authored: the sun direction is `mesh.metal`'s literal mirrored as a C++ constant with a comment (one source of truth stated in both), a raw-node shadow march from the sword tip toward it through the ground and the backdrop ray grid (blocked within 300 m → shade, so the shrine shades the plain); the in-light value moves at 0.3 per 1/30 s step (9.0/s) — the step unit is unverified, labelled | Builder (veto) | The app has no sun texture. Cost: shade only where the 1 m grids see it |
| D7 | A separate **sword pass** (`sword_fx.h/.cpp`) reusing `tendril.metal` and `EffectVertex`: its own small dynamic buffer (8 line quads, a flare sprite, a hit sprite), its own four textures (the three sword atlases + `sword_reaction`), created on any stage where the sword can be raised; `effect.h/.cpp` is not touched | Builder | The tendril renderer is fixed-size and encounter-only; a separate pass removes the S6 risk. Cost: a second small effect pass |
| D8 | Input: hold `C` / gamepad left shoulder raises the sword (the game's R2 — remapped because RT is the app's grip); release lowers; on foot, raising stops the player (turning stays — there is no walk-raised clip); mounted, the raise is only accepted at Stand (`uma_hoist_sword_*`; the game's moving variants are additive layers the app lacks) | Builder (veto) | Labelled remap. Cost: no raised walking/riding |
| D9 | The raise clips are baked into Wander (appended after S8's 43, indices kept): `player_a_hoist_sword_{start,loop,end}`, `player_a_uma_hoist_sword_{start,loop,end}`; `wander_sword.glb` is drawn attached to `OptBone_sword`; the additive and turn variants are out | Builder | The S8 bake pattern. Cost: the shared library changes again |
| D10 | Sound: `tools/re/audio/s9_map.json` = the S8 rows + `sword.shimmer` (`com_obj`/`sword_test`) and `sword.search` (`sword_boss_test`); the mixer gains a stop path (a voice handle from `play`, or stop-by-event) so the shimmer loops while raised and stops on lowering; `Sword_reflection` and `touch` deferred; audition rows, the S5 gate with a `"s9"` milestone entry; `make audio` moves to the S9 map (S8's acceptance decodes its own map explicitly) | Builder | The S5 contract. Cost: a small mixer extension; low-confidence picks |
| D11 | `--s9-e2e` scripts the whole loop through the interactive loop: `--stage shrine` → door → road → whistle → mount → raise the sword to lock → gallop to the entrance → dismount → corridor door → arena → the production lethal hit → death → return → shrine; each join asserted | Builder | The loop's joins are the risk. Cost: a long run (~minutes) |

## 8 · The design

### 8.1 Data (W2/W3/W4)
- Gate (W2): the corridor door box and its arrival spawn in the arena; the arena's reverse door box on
  the corridor floor and its arrival spawn on the road; the road's reachability with the entrance set
  (S8's reach harness); Valus's world position and the chest height; the sun direction the renderer
  uses; the sword tip bone (`OptBone_sword*` or the hand bone + an offset); selftest probe points (a
  sunlit point facing Valus, a shaded point, the entrance top). Output `tools/re/s9/placements.json`.
- Probe (W3): `STAGES["road"]` and `check-manifest --stage road`; `plain` untouched.
- Bakes (W4): the three sword atlases + the flare/reaction to library PNGs (`bp-convert texture`);
  Wander + the six raise clips; `s9_map.json` decoded; `make bake`/`make audio` extended.

### 8.2 The app (W1)
- `sword.h/.cpp` — a pure module: raise state (Lowered/Raising/Raised/Lowering with the clips), the
  in-light value (D6), and the beam per frame from (sword tip, aim, target, heightmap): `k`, the fan's
  eight line directions, width, `locked`, the ray hit; tests for every curve against §6's numbers.
- `effect` — the sword layout (D7).
- The wiring: the road manifest selection (D2), the corridor/arena doors (D3, through S8's door
  machine), Agro's carried pose (D4), the raise key/button (D8), the beam's effect frame and cues.

### 8.3 The proof (W4)
- `--s9-selftest`: on the road at a sunlit probe within ~40 m of the entrance stairs facing away from
  Valus: raise → `k ≈ 1` (fan open); turn the camera to Valus → `k < 0.01`, locked, the ray hits within
  150 m (the entrance or the backdrop cliff); at the shaded probe → the
  beam fades to 0; walk up the entrance into the corridor door → the arena spawn; the reverse door →
  the road below the cliff; stills `fan.png`, `locked.png`, `entrance.png`, `arena_door.png`.
- `--s9-e2e`: D11.
- `scripts/s9-acceptance.sh`: the frozen S8 acceptance first, the S9 decode, the road manifest gate,
  both runs, the asserts, `PASS —`.

## 9 · The builder's calls (with defaults)

The door boxes and arrival spawns; Valus's target height; the sun direction (the renderer's) and the
shade ray length; the raise blend; the slow walk while raised; the beam length when unlocked; the stills
and pixel thresholds; the perf gate (≤ 16.7 ms).

## 10 · Risks

| Risk | Severity | Mitigation |
|---|---|---|
| The road's draw cost (plain backdrop + entrance + arena backdrop) | medium | S8's R6 cut list (the roof garden first); the perf gate |
| The beam reads wrong (the fan's lean or the lock) | medium | §6's curves verbatim + unit tests; stills |
| The sword pass perturbs the S6 tendrils | low | A separate pass; `effect.*` untouched; the S6 chain (inside the frozen S8 acceptance) proves it |
| The shared re-bake breaks `main` | high | Wander's indices kept; `main`'s pins are already ≥ |
| The long e2e is flaky | medium | Fixed 1/60 s steps and scripted input, as S7/S8 |

## 11 · The walk (the user's, PENDING after implementation)

- On the plain, raise the sword in the sun: the fan closes as you face Valus, the ray sparks on the
  ground; step into shade — the beam fades; on Agro too.
- Ride to the entrance and up the stairs; dismount; the corridor's end takes you into the arena; walk
  down the bowl's slope back through the door — Agro is where you left her.
- The whole loop: from the shrine to Valus's death and back.
- Deferred/known: the climb, the sky beam, the weak-point beam, the sword glow, `touch`, music.

## 12 · Acceptance

Controls/windowed/cursor unchanged; the S8 → … → S3 scripts green and unchanged;
`scripts/s9-acceptance.sh` green; `docs/notes/s9-recon.md` and `docs/notes/s9-results.md`; assets not
committed; the S9 walk added.
