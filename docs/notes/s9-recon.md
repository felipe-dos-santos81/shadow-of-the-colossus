# S9 — the RE/data gate: the road's reach, the doors, Valus, the sun, the sword, the probes (2026-10-23, Task 0)

| gate | verdict | what S9 adopts |
|---|---|---|
| Step 1 the road's reach | **PASS** (the app's heightmap code) | 509,579 nodes reached from the plain spawn; the entrance reached to z 296.69; the corridor floor x 300–308, z 280–296, y 2.71–5.77 (125 nodes); the (315.5, 291) overhang (node y 31.15) not reached |
| Step 1 `door_corridor` | **PASS** (authored box, measured y) | x 300–308, z 286–296, y 2.2–8.3 (77 reached nodes, floor 2.78–5.77); arrival = the arena spawn (data) |
| Step 1 `door_arena` | **PASS** (authored box, walked) | x 298–312, z 303–311, y 27.5–32.0 on the bowl's rim above the corridor (108 bowl nodes); the straight walk from the arena spawn enters it after 55.0 m; 53.3 m from the spawn, 91.5 m from Valus; arrival `arrive_road` (304, 2.788, 282) yaw 180 on the reached corridor floor, 4.00 m outside `door_corridor` |
| Step 2 Valus | **PASS** (data) | feet (382, 28.369, 370) = `encounter.manifest`'s entity line; chest lift 10.93 = `Bone_MUNE` 10.9251 (`s4-results.md`) |
| Step 2 the sun | **PASS** (the renderer's literal) | `app/shaders/mesh.metal:93` `normalize(float3(0.4, 0.8, 0.6))` = (0.371391, 0.742781, 0.557086), 47.97° up; shade march 300 m (authored, D6) |
| Step 3 the sword | **PASS** (measured) | `wander_sword.glb` in the library (blade local z −0.580…+0.121); `OptBone_sword` = joint node 296 of Wander's skin (child of `Bone_HAND_A_R`); tip = local (0, 0, −0.58) |
| Step 3 textures | **PASS** (data; flare labelled) | three distinct atlases, single images (frame 1×1); the hit atlas is the flare's |
| Step 3 clips | **PASS** (data) | 30/50/29 and 35/60/25 frames at 30 fps = the `.anb` durations |
| Step 4 probes | **PASS** (authored points, measured marches) | `sunlit` (335, −4.534, 233.5) clear for 300 m, beam hits at 37.5 m; `shade` (304, 2.925, 284) blocked at 17.5 m; `stairs_foot` (304, −3.603, 238); `entrance_top` (304, 2.434, 273); `agro_stairs` (330, −5.234, 228) yaw 20.11, sunlit, beam 43.5/44.5 m; `e2e_route` 4 waypoints, 487 m, legs < 10° |

The output is `tools/re/s9/placements.json`, composed and checked by `tools/re/s9/placements.py`
(every check prints `ok`/`FAIL`, exit 1 on a failure — 44 checks; it reuses `tools/re/s8/placements.py`'s grid
reader and `tools/re/anb/parse2.py`; Step 4's marches run `tools/re/s9/probes.cpp`, built against the app's
`heightmap_node`/`ray_march`/`build_backdrop_grid`). Static work only; the game tree and the shared library were
read, never written; the one bake went to a scratch `--out`.

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
SCR=<scratch>; H=models/shadow/levels/home/_cmn; E=models/shadow/levels/arena_01_entrance/_cmn
PLAIN="$H/home.glb $H/home_ground.glb $H/home_cliff_displaced.glb $H/home_skirts.glb $H/home_plainsgrey_skirt.glb $H/home_pond.glb $H/home_wall_pillars_01.glb"
ENT="$E/arena_01_entrance.glb $E/arena_01_entrance_furwall.glb $E/arena_01_entrance_ground.glb $E/arena_01_entrance_skirt.glb $E/arena_01_entrance_skirt_02.glb $E/arena_01_entrance_stairs.glb"
c++ -std=c++20 -O2 -Iapp/src -Iapp/third_party tools/re/s8/reach.cpp app/src/asset_io.cpp \
  app/src/manifest.cpp app/src/heightmap.cpp app/src/anim.cpp app/src/player.cpp app/src/collision.cpp -o $SCR/reach
$SCR/reach plain "$SOTC_LIB" 301 -7.14 -210 $SCR/road.bin $PLAIN $ENT
#  -> plain grid 952x1040 nodes @1.0 m, bounds x -216.1..734.8 z -583.3..454.7, spawn node (517,373) h -7.073, reached 509579 nodes
$SCR/reach plain "$SOTC_LIB" 353 27.951 345 $SCR/arena.bin $(grep '^instance' out/re/stage/encounter.manifest | awk '{print $2}')
#  -> plain grid 493x504 nodes @1.0 m, bounds x -0.6..490.4 z -0.1..502.3, spawn node (354,345) h 27.971 (array 0 only is used)
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out $SCR/lib --force \
  --under models/shadow/characters/wander/_cmn/wander.cmsh --clips player_a_hoist_sword_start,player_a_hoist_sword_loop,player_a_hoist_sword_end,player_a_uma_hoist_sword_start,player_a_uma_hoist_sword_loop,player_a_uma_hoist_sword_end
c++ -std=c++20 -O2 -Iapp/src -Iapp/third_party tools/re/s9/probes.cpp app/src/asset_io.cpp \
  app/src/manifest.cpp app/src/heightmap.cpp app/src/anim.cpp app/src/player.cpp app/src/collision.cpp -o $SCR/probes
#  (reads out/re/stage/road.manifest, Task 1's composition: the plain + the six entrance glbs as ground,
#   27 shrine backdrops + arena_01.glb as backdrops) -> ground 952x1040, backdrop grid 217x831 x 216.1..431.4 z -439.0..390.0
python3 -I tools/re/s9/placements.py $SCR $SCR/lib/models/shadow/characters/wander/_cmn/wander.glb $SCR/probes
```

Validate the JSON (both doors and arrivals, Valus, the sun, the sword, four textures, six clips,
every probe filled; exit 0 + `OK`):

```bash
python3 -I -c "import json;d=json.load(open('tools/re/s9/placements.json'));C,A=d['door_corridor'],d['door_arena'];assert C['stage']=='plain' and A['stage']=='arena' and all(len(b[k])==3 for b in (C,A) for k in ('min','max')) and all(b['min'][i]<b['max'][i] for b in (C,A) for i in range(3));assert C['arrive_arena']=={'p':[353,27.951,345],'yaw_deg':49.24} and len(A['arrive_road']['p'])==3 and A['arrive_road']['yaw_deg']==180;assert all(b['source'] for b in (C,A));V=d['valus'];assert V['feet']==[382,28.369,370] and V['chest_lift']==10.93 and V['source']=='data';S=d['sun'];assert S['dir']==[0.4,0.8,0.6] and S['normalized']==[0.371391,0.742781,0.557086] and S['shade_m']==300 and S['source']=='authored';W=d['sword'];assert W['glb'].endswith('/wander_sword.glb') and W['bone']=='OptBone_sword' and W['tip']==[0,0,-0.58] and W['source'];T=d['textures'];assert all(T[k].endswith('.png') and T['sources'][k].endswith('.cptx') for k in ('lines','overlay','flare','hit')) and T['frame']==[1,1];K=d['clips'];assert K=={'player_a_hoist_sword_start':30,'player_a_hoist_sword_loop':50,'player_a_hoist_sword_end':29,'player_a_uma_hoist_sword_start':35,'player_a_uma_hoist_sword_loop':60,'player_a_uma_hoist_sword_end':25,'fps':30};P=d['probes'];pk=('sunlit','shade','stairs_foot','entrance_top','agro_stairs','e2e_route');assert all(P[k] for k in pk) and P['source']!='pending-task-3' and len(P['sunlit']['p'])==3 and isinstance(P['sunlit']['yaw_away_deg'],(int,float)) and len(P['shade']['p'])==3 and len(P['stairs_foot'])==3 and len(P['entrance_top'])==3 and len(P['agro_stairs']['p'])==3 and isinstance(P['agro_stairs']['yaw_deg'],(int,float)) and len(P['e2e_route'])>=2 and all(len(w)==2 for w in P['e2e_route']) and P['e2e_route'][-1]==[P['stairs_foot'][0],P['stairs_foot'][2]];R=d['road_reach'];assert R['nodes']>500000 and 296<=R['z_max']<297 and R['corridor']['x']==[300,308];print('OK doors=2 arrivals=2 valus sun sword textures=4 clips=6 probes=6 route=%d road_reach=%d'%(len(P['e2e_route']),R['nodes']))"
# -> OK doors=2 arrivals=2 valus sun sword textures=4 clips=6 probes=6 route=4 road_reach=509579
```

**Conventions of the JSON.** One frame: the world (the plain and the arena share it; every
instance is at identity). `yaw_deg` is the app's +Y yaw (0 faces +Z, 90 faces +X). Door boxes are
axis-aligned `min`/`max`, fired by `box_entered` on the feet. An arrival `p` carries the ground
under it (the bilinear node height, as `heightmap_sample`). Keys beyond the brief's schema are
additive provenance: `door_*.box_source/arrive_source/reach`, `valus.chest`, `sun.shader`,
`sword.hand_bone/blade_z/note`, `textures.emitters/note`, `clips_measured`,
`road_reach.ground/overhang`, `probes.*.shade_hit_m/beam_hit_m`, `probes.sources/tip_lift_m`.

---

## 1 · Step 1 — the reach and the doors: PASS

### 1.1 The road (the Plain stage's rule)

`tools/re/s8/reach.cpp plain`, unchanged: an unlayered 1 m map of the given glbs (identity
transforms), BFS over the nodes from the plain spawn (301, −7.14, −210) with the D3 rule (a move is
refused into a hole, up > 1.0 m or down > 1.5 m). Ground = `plain.manifest`'s seven instance glbs +
the six entrance glbs (`arena_01_entrance`, `_furwall`, `_ground`, `_skirt`, `_skirt_02`, `_stairs`).
Grid origin (−216.108, −583.305): nodes sit at x = n + 0.892, z = n + 0.695.

| measure | value |
|---|---:|
| reached nodes | **509,579** (the design's 509,577 was the 16-glb set) |
| the entrance (x 280–330) reached up to | z **296.69** |
| the corridor floor x 300–308, z 280–296 | **125** reached nodes, y **2.71–5.77** |
| its highest node | (307.89, 292.69) y 5.77 (the design's ≈ 5.6 is the median edge; the east wall's foot rises) |
| east of the corridor, z ≥ 280 | reached to x 315.89, y ≤ 8.46 (the ramp at the east wall's foot) |
| the overhang (315.5, 291) | node (315.89, 290.69) **y 31.15, not reached**; its west neighbour (314.89) y 16.67, not reached |

`door_corridor` = x 300–308, z 286–296 (authored), y = the reached floor inside −0.5 … +2.5 m =
**2.2 … 8.3** (77 reached nodes, floor 2.78–5.77). The walk's z max (296.69) is the dead end; the box
is 496 m from the plain spawn. Arrival: the arena spawn (353, 27.951, 345), yaw 49.24 (the
manifest's 49.236395; `playerstartpositions.txt` x/z, ground re-sampled — data).

### 1.2 The arena (the arena's rule) → `door_arena`

The arena stage builds the encounter manifest's 30 instances (identity) into the same unlayered
1 m map with **no** hole refusal (`heightmap.refuseHoles` is set for the Plain only), so every
in-bounds move is walkable and the feet follow `heightmap_sample`. The dump is `reach plain` over
the 30 glbs (array 0 = the node heights; array 1, its D3 walk, is ignored). The spawn's bilinear
ground reads **27.951** (= the manifest). The bowl floor runs southwest from the spawn at y 27.7–30.4
to a rim over the corridor's dead end at z ≈ 303–304, x ≈ 300–306, where it drops to 11–20 m
(the cliff's ledges) and then to the corridor floor (4.6–5.1 at z 296).

`door_arena` = x 298–312, z 303–311, y 27.5–32.0 (authored): **108** bowl nodes inside (y
28.60–30.07; 4 more nodes of the drop, y 18.7–19.8, sit below the box). The straight walk from the
spawn toward the box centre (0.5 m steps, in bounds throughout, feet 27.94–29.59) enters it after
**55.0 m** at (309.88, 29.431, 310.86). The box is **53.3 m** from the spawn and **91.5 m** from
Valus's feet (≥ 2 × the 14 m club range; Valus turns but never translates — `valus.cpp` moves no
position). Arrival `arrive_road` = (304, **2.788**, 282), yaw **180** (facing −Z, down the stairs):
a reached corridor-floor node, **4.00 m** outside `door_corridor` (z 286), local slope 7.37°.

Both doors walk both ways: the road reaches `door_corridor` and `arrive_road` from the same plain
spawn (one connected walk), and the arena's walk reaches `door_arena` from `arrive_arena`, which
lies outside it.

## 2 · Step 2 — Valus and the sun: PASS

- `out/re/stage/encounter.manifest`: `entity models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb 382 28.369 370 …` → feet **(382, 28.369, 370)**.
- `docs/notes/s4-results.md` frame-0 world heights: `Bone_MUNE` **10.9251** m → chest lift **10.93**, the chest at (382, 39.299, 370).
- `app/shaders/mesh.metal:93`: `const float3 key_light = normalize(float3(0.4, 0.8, 0.6));` → (0.371391, 0.742781, 0.557086), elevation 47.97°. (The plan quoted line 93; it is the line.) Shade length 300 m: authored (D6).

## 3 · Step 3 — the sword: PASS

**The glb.** `models/shadow/characters/wander/_cmn/wander_sword.glb` is in the library: one node
`Mesh_0`, 5,311 vertices, no skin; POSITION z **−0.58 … +0.1215**, x ±0.0288, y ±0.0524. No convert
is needed (else: `bp-convert convert --under models/shadow/characters/wander/_cmn/wander_sword.cmsh`).

**The bone.** `OptBone_sword` is node 296 of the library's `wander.glb`, a joint of its skin, a
matrix node under **`Bone_HAND_A_R`** (origin 4.5 cm from the hand).

**The tip.** FK over the scratch bake at `player_a_hoist_sword_loop` (26 keys): at every frame the
local z −0.58 end is **0.607 m** from the hand bone and the +0.12 end **0.102 m** (closest frame 7) →
the tip is **(0, 0, −0.58)** in the bone's frame. Observation for the sword pass: in the loop pose
the blade runs model +X (direction (0.98, 0.11…0.14, −0.11…−0.19)), the tip at (0.47, 1.42, 0.28),
the hand at (−0.12, 1.31, 0.33) — the beam leaves the tip along the aim (D5), not the blade axis.

**The textures** (`$/textures/… .cptx` strings after each `<file>:<emitter>` name in the `.cprt`):

| role | emitter | atlas (`textures/…`) | decoded |
|---|---|---|---|
| `lines` | `sword_line.cprt:sword_beam_base` | `vfx/common/beam/_ps4/vertical_solid_beams_01_pfx` (69,553 B) | 512×512, 8c0a |
| `overlay` | `sword_line.cprt:sword_beam_overlay` | `vfx/common/beam/_ps4/vertical_beams_wave_01_pfx` (154,946 B) | 512×512, 8c0a |
| `flare` | `sword_line.cprt:flare_center` | `vfx/common/glows/_ps4/soft_glow_01_falloff_pfx` (22,184 B) | 256×256, 8c0a |
| `hit` | `sword_reaction.cprt:flare_radial_ring` | the same `soft_glow_01_falloff_pfx` | — |

`sword_reaction_subtractive.cprt:flare_radial_ring` binds the same glow too; `sword_flare_tip.cprt`
binds `flare/_ps4/flare_02_half_pfx`, `flare_02_quarter_pfx`, `flare_04_glow_pfx` — **not used**:
the flare is `sword_line`'s own `flare_center` glow (labelled). So the four roles are three distinct
PNGs. **Frame [1, 1]**: decoded to scratch with `bp-convert texture`, each atlas is one image (a
vertical beam strip, a wavy beam strip, a radial glow) and none carries an `NxM_` name prefix (the
S6 atlases' grid came from theirs).

**The clips** (`tools/re/anb/parse2.py` frames + the `.anb` rate word, as `root_z.py`):

| clip | keys | fps | seconds | frames @ 30 |
|---|---:|---:|---:|---:|
| `player_a_hoist_sword_start` | 16 | 15.0000 | 1.0000 | **30** |
| `player_a_hoist_sword_loop` | 26 | 15.0000 | 1.6667 | **50** |
| `player_a_hoist_sword_end` | 16 | 15.5127 | 0.9669 | **29** |
| `player_a_uma_hoist_sword_start` | 19 | 15.4248 | 1.1670 | **35** |
| `player_a_uma_hoist_sword_loop` | 31 | 15.0000 | 2.0000 | **60** |
| `player_a_uma_hoist_sword_end` | 14 | 15.5933 | 0.8337 | **25** |

The design's "30/50/29 f, 30 fps" are these durations in 30 fps frames; the baked clips carry the
`.anb` keys (16/26/16, 19/31/14 at ~15 fps). `clips_measured` records both.

## 4 · Step 4 — the probes: PASS (authored points, measured marches)

`tools/re/s9/probes.cpp` composes `out/re/stage/road.manifest` with the app's `compose_scene`,
builds the ground (`build_heightmap`, 1 m, unlayered) and the backdrop ray grid
(`build_backdrop_grid`, 1 m), and answers marches with `ray_march` at **step 0.5 m** (cellSize/2) and
a unit direction, ground and backdrop combined with `std::fmin` (ruling B4). A march's `t` is
quantised to the step: recorded values carry **±1.0 m** (step + 0.5). No fill, no clamp: a hole or an
out-of-bounds sample is no hit. The sun direction is `normalize(0.4, 0.8, 0.6)` (`kSunDir`); the
shade march is 300 m; the beam march runs to Valus's chest (382, 39.299, 370) for 150 m. The tip
is the ground (bilinear) + 2.0 m on foot, + 3.5 m mounted (authored). Each point is checked on its
1 m ring (the point and its 8 neighbours at ±1 m), so a probe never sits on a march's edge.

| probe | point (y = ground) | slope | march (ground / backdrop → fmin) |
|---|---|---:|---|
| `sunlit` | (335, −4.534, 233.5), `yaw_away_deg` −161.0 (Valus's chest bears 19.0°) | 7.65° | shade: none within 300 m (all 9 ring nodes); beam: 37.5 / none → **37.5 m** (ring 35.5–40.5) |
| `shade` | (304, 2.925, 284) — the corridor floor, 2.00 m outside `door_corridor` | 10.05° | shade: **17.5** / none → **17.5 m** (ring 12.0–23.0), the road's ground grid (the entrance set) |
| `stairs_foot` | (304, −3.603, 238) — the approach's last < 10° ground before the stairs' ramp (z 243–250 is 10–13°) | 8.94° | — |
| `entrance_top` | (304, 2.434, 273) — the corridor floor at the stairs' top, 13.00 m short of `door_corridor` | 0.30° | — |
| `agro_stairs` | (330, −5.234, 228), `yaw_deg` 20.11 (toward the chest) | 7.37° | tip +2.0: sunlit (ring); beam 43.5 / 79.5 → **43.5 m**; tip +3.5: sunlit (ring); beam 44.5 / 79.0 → **44.5 m** |

`sunlit` is 20.2 m from the stairs' box (x 297–323, z 249.8–279). Near the
stairs' foot the ground grid shades the tip: the points checked at x 295–325, z 225–238 all block
the shade march at 60–82 m, and the sunlit points found there lie at x ≥ 330. Every beam's first hit is
the ground grid; the backdrop grid (`arena_01.glb`) answers too from `agro_stairs` (79 m), farther.

Walks (the D3 rule, `s8.walk_line`): `stairs_foot → entrance_top` 35.0 m ok (it climbs the stairs, worst
24.3°); `entrance_top → (304, 290)` (into `door_corridor`) 17.0 m ok; `agro_stairs → stairs_foot`
27.9 m ok, worst 9.5°.

`e2e_route` — from S8's plain arrival (301, −245) to `stairs_foot`: Dijkstra over the road walk's
reached nodes (8-neighbour, the D3 rise/drop, node slope < 9.5°, 493 m), then greedy straight legs
that `walk_line` passes with a worst slope < 9.5° → **4 waypoints** (301, −245) → (290.9, −231.3) →
(304.9, −49.3) → (304, 238), **487 m** in legs of 17 / 183 / 287 m, worst slopes 9.48 / 9.43 / 8.94°
(past each leg's first 1.5 m: the arrival itself, the shrine steps' foot, is 11.09°).

## 4a · Notes for the later tasks

- **For Task 8's stills check (explicit item): the raised blade lies nearly flat.** In
  `player_a_hoist_sword_loop` the blade runs model +X (direction ≈ (0.98, 0.11…0.14, −0.11…−0.19)),
  the tip at (0.47, 1.42, 0.28), the hand at (−0.12, 1.31, 0.33), only 0.07–0.09 m above the
  pommel. The tip choice (the −0.58 end, 0.607 m from the hand vs 0.102 m) is unambiguous and the beam
  follows the aim, not the blade; the stills must still show the sword in the raised hand, on the
  hand, pointing where the clip puts it — check `wander_sword.glb`'s attachment to `OptBone_sword`
  visually.
- The hit texture is the flare's: `sword_reaction.cprt:flare_radial_ring` binds the same
  `soft_glow_01_falloff_pfx` as `sword_line.cprt:flare_center`, so the four roles are three distinct PNGs.
- The corridor floor's measured y max is **5.77** (node (307.89, 292.69), the east wall's foot), not the
  design's ≈ 5.6; `door_corridor`'s y (2.2–8.3) covers it.
- The six clip numbers (30/50/29, 35/60/25 at 30 fps) are **durations**; the baked glb clips carry the
  `.anb` keys (16/26/16, 19/31/14 at ≈ 15 fps) — never read them as key counts.

## 5 · Files

- `tools/re/s9/placements.py` — the composer/checker (stdlib; reuses `tools/re/s8/placements.py`, `tools/re/anb/parse2.py`).
- `tools/re/s9/probes.cpp` — the Step 4 march harness (scratch-built against the read-only app sources).
- `tools/re/s9/placements.json` — its output.
- Scratch (not committed): `road.bin`, `arena.bin`, the Wander bake, the three decoded atlases.

## 6 · The bakes and the audio slice (Task 6) — into the library view only

S9 never writes the base `out/library`; every command runs with `SOTC_LIB` at the branch's view.

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
make libview BRANCH=s9-road                    # APFS clone, ~4 s
export SOTC_LIB=$PWD/out/libviews/s9-road
make bake  LIB="$SOTC_LIB"    # mono, wander (49 clips), agro (20), then the four sword atlases:
#   bp-convert texture --in "$SOTC_GAME_ROOT/textures/<t>.cptx" --out "$SOTC_LIB/textures/<t>.png"
#   for t in textures.sources {lines, overlay, flare, hit} (hit = flare's atlas: 3 distinct PNGs)
python3 tools/re/audio/s5_audition.py --root "$SOTC_GAME_ROOT" \
  --map tools/re/audio/s9_map.json --out out/s9/audition --check   # -> AUDITION PASSED: 36 cue(s)
make audio LIB="$SOTC_LIB"    # s9_map.json -> audio.manifest, 36 entries
make road-manifest            # composes out/re/stage/road.manifest when absent
```

* `wander.glb`: S8's 43 clips keep indices 0…42; the six raise clips are 43…48 in design D9's
  order (`player_a_hoist_sword_{start,loop,end}`, `player_a_uma_hoist_sword_{start,loop,end}`),
  baked at their `.anb` keys (16/26/16, 19/31/14).
* Atlases (`variant 8c0a`): `vertical_solid_beams_01_pfx` 512×512, `vertical_beams_wave_01_pfx`
  512×512, `soft_glow_01_falloff_pfx` 256×256 (flare and hit).
* Audio (`method: audition`, neither chain-proven; the com_obj `TONE` chunk is the program form):
  `sword.search` = request `sword_boss_test` (REQD 173 → `PLAY_TONE` 10…13) → wave **55**, the
  pool label equal to the request name (confidence medium, 0.967 s). `sword.shimmer` = request
  `sword_test` (REQD 175 → `PLAY_TONE` 66 under a `SET_PARAM` envelope; its name sits at pool
  index 310, past the 241 resident waves, so no label match) → wave **239** `shine_normal`
  (confidence **low**, 3.240 s, `loop: true`): the longest sustained normal-sword shine; the
  alternates auditioned were w048 `Shine_Normal_sword` (0.543 s glint), w179
  `DorminSwordShine_VoiceSampleLoop_v2` and w030/w060–063 (the Dormin sword's shines).
* `scripts/s8-acceptance.sh` decodes `s8_map.json` itself (34 entries), so re-run `make audio`
  after it before any `--s9-*` run.
