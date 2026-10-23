# S8 — the RE/data gate: the doors, Agro's start, the probes, the root tables (2026-10-23, Task 0)

| gate | verdict | what S8 adopts |
|---|---|---|
| **R1** the shrine door + the plain's door | **PASS** (geometry + walk; no door record) | `door_shrine` at the S7 walk's +Z end (the front stairs' foot), `door_plain` on the shrine steps, both reached by walking; arrival spawns verified both ways; 27 backdrop glbs by bounds |
| Step 2 Agro + probes | **authored, verified** | Agro 7 m ahead of the spawn facing it (slope 3.28°); whistle 60 m, gallop 300 m (< 10°), edge into the west cliff (refused by a 4.89 m drop) |
| Step 3 root tables | **PASS** (data) | `root_z` (14 non-loop Agro clips) and `rider_root_xz` (5 Wander clips) from the `.anb` root series |
| Step 3 footfalls | **measured** (design values kept; re-measure within one frame) | `footfalls` = the design's values; `footfalls_remeasured` alongside |
| **R2** `.anb` event tracks | **FALLBACK** | no event track in the flag-clear structure; the measured fractions stand |
| **R3** whistle approach / riding camera | **FALLBACK** | D12 (whistle) and D13 (camera) defaults stand |

The output is `tools/re/s8/placements.json`, composed and checked by `tools/re/s8/placements.py`
(every check prints `ok`/`FAIL`, exit 1 on a failure). Static work only; the game tree, the shared
library and `out/re/eboot.elf` were read, never written; the one bake went to a scratch `--out`.

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
SCR=<scratch>; H=models/shadow/levels/home/_cmn
GROUND="$H/home.glb $H/home_ground.glb $H/home_cliff_displaced.glb $H/home_skirts.glb $H/home_plainsgrey_skirt.glb $H/home_pond.glb $H/home_wall_pillars_01.glb"
# the walks: tools/re/s8/reach.cpp, built against the read-only app sources (its header has the line)
c++ -std=c++20 -O2 -Iapp/src -Iapp/third_party tools/re/s8/reach.cpp app/src/asset_io.cpp \
  app/src/manifest.cpp app/src/heightmap.cpp app/src/anim.cpp app/src/player.cpp app/src/collision.cpp -o $SCR/reach
mkdir -p $SCR/walks
$SCR/reach shrine "$SOTC_LIB" out/re/stage/shrine.manifest $SCR/walks/shrine.bin
$SCR/reach shrine "$SOTC_LIB" out/re/stage/shrine.manifest $SCR/walks/shrine_arrive.bin 0 9.79 34
$SCR/reach plain "$SOTC_LIB" 301 -7.14 -210 $SCR/walks/plain.bin $GROUND
$SCR/reach plain "$SOTC_LIB" 301 -2.28 -245 $SCR/walks/plain_arrive.bin $GROUND
# the footfall bake (scratch --out only)
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out $SCR/bake \
  --under models/shadow/characters/agro/_cmn/agro.cmsh --clips horse_a_uma_walk,horse_a_uma_run_n,horse_a_uma_run --force
python3 -I tools/re/s8/root_z.py tables                         # the root tables
python3 -I tools/re/s8/root_z.py footfalls $SCR/bake/models/shadow/characters/agro/_cmn/agro.glb
python3 -I tools/re/s8/placements.py $SCR/walks $SCR/bake/models/shadow/characters/agro/_cmn/agro.glb
```

Validate the JSON (both doors, both arrival spawns, Agro, probes, speeds, `root_z` for every
non-loop clip, `rider_root_xz` for the five clips, backdrops, footfalls; exit 0 + `OK`):

```bash
python3 -I -c "import json;d=json.load(open('tools/re/s8/placements.json'));S,P=d['door_shrine'],d['door_plain'];assert S['frame']=='shrine' and P['frame']=='world' and all(len(b[k])==3 for b in (S,P) for k in ('min','max')) and all(b['min'][i]<b['max'][i] for b in (S,P) for i in range(3));assert len(S['arrive_plain']['p'])==3 and len(P['arrive_shrine']['p'])==3 and all(isinstance(a['yaw_deg'],(int,float)) for a in (S['arrive_plain'],P['arrive_shrine']));assert len(d['agro']['p'])==3 and d['agro']['source']=='authored' and d['spawn_plain']['p']==[301,-7.14,-210];Q=d['probes'];assert len(Q['whistle_from'])==2 and Q['gallop_line']['len_m']>=300 and len(Q['gallop_line']['from'])==2 and len(Q['edge']['from'])==2==len(Q['edge']['to']);assert d['speeds']=={'walk':1.31,'trot':5.29,'gallop':14.7,'back':-1.1,'source':'measured'};nl=['horse_a_uma_'+c for c in 'walk_start walk_stop run_n_start run_n_stop run_n_from_walk run_start run_from_run_n run_stop1 run_stop2 run_stop3 back_start back_stop neigh neigh_end'.split()];assert sorted(d['root_z'])==sorted(nl) and all(v[0]==0 and len(v)>1 for v in d['root_z'].values());rc=['player_a_uma_stand_ride_l','player_a_uma_stand_ride_r','player_a_uma_stand_getoff_l_start','player_a_uma_stand_getoff_r_start','player_a_uma_stand_getoff_fall'];assert sorted(d['rider_root_xz'])==sorted(rc) and all(len(p)==2 for v in d['rider_root_xz'].values() for p in v);B=d['backdrops'];assert len(B)>=4 and all(b.startswith('models/shadow/levels/cathedral/') for b in B) and {'cathedral_new_firstlevel','cathedral_entrance','rootsfront1','cathedral_ext_front'}<={b.split('/')[-1][:-4] for b in B};F=d['footfalls'];assert all(len(F[g])==4 for g in ('walk','trot','gallop')) and F['source']=='measured';print('OK doors=2 arrivals=2 agro probes speeds root_z=%d rider=%d backdrops=%d footfalls'%(len(d['root_z']),len(d['rider_root_xz']),len(B)))"
# -> OK doors=2 arrivals=2 agro probes speeds root_z=14 rider=5 backdrops=27 footfalls
```

**Conventions of the JSON.** `shrine` frame = the cathedral glbs' frame (the S7 shrine manifest);
`world` = the plain's (`levels/home`) frame; world = shrine + (300, 0.071, −300) (`home.cgpr`
`level_setup` @0xda54, S7 §3.2). `yaw_deg` is the app's +Y yaw (0 faces +Z, 90 faces +X). Door
boxes are axis-aligned `min`/`max` in their frame; an arrival `p` carries the floor height under
it (shrine: the walk's `heightmap_floor` feet; plain: the bilinear node height, as
`heightmap_sample`). Keys beyond the brief's schema are additive provenance: `agro.slope_deg/note`,
`spawn_plain.p_source/yaw_source`, `door_*.reach`, `probes.source`, `root_fps` (the `.anb` rate
per table clip), `footfalls_remeasured`, `frames`.

---

## 1 · R1 — the doors: PASS (geometry + walk)

### 1.1 The records: no door, an open front stair

Dumped with `tools/re/stage/spawn_probe.py cgpr` (Oodle-decoded; record @offset in the decoded
body): `cathedral_ext.cgpr` 2 records, `cathedral_ext_shared.cgpr` 208, `cathedral_int.cgpr`
1,611, `home.cgpr` 86, `home_shared.cgpr` 2. No record names a front door, exit, portal or zone
trigger toward +Z. The door-named records are elsewhere: `SecretRoomDoor*` (x ≈ −77.7, the west
secret room), `back_entrance` / `BackDoor_EntrySpot` / `home_gate.nmo` (z −127…−142, y ≈ 75, the
back) and `CutsceneProxy door dust*` (z −130.7, the back). The front is an open stair, marked by
volume records only:

| record (file @offset) | t (shrine frame) | volume |
|---|---|---|
| `Outside_Cathedral_stairs` (`cathedral_int` @0x037c42) | (−14.822, 6.794, 33.014) | 19.02 × 14.92 × 14.92 |
| `Outside_Cathedral_stairs_02` (`cathedral_int` @0x037ac2) | (16.423, 6.794, 33.014) | 19.02 × 14.92 × 14.92 |
| `save_volume` (`cathedral_ext_shared` @0x001ae2) | (0.004, 4.255, 34.897) | 1.957 × 3.018 × 1.180 |
| `PrayerStoneFacingSensor` (`cathedral_ext_shared` @0x001ca2) | (−0.004, 4.183, 34.986) | 1.580 × 2.750 × 2.157 |
| `DustSensor` × 7 (`cathedral_ext_shared` @0x005287…@0x006faf) | x −6.6…6.7, y 10.2–10.4, z 37.2–37.8 | 2 × 1 × 1 each |

`cathedral_ext_front.glb` spans shrine z [−1.2, 45.9] (world [−301.2, −254.1]) down to y −0.6.

### 1.2 The shrine walk (the frozen S7 map) → `door_shrine`

`reach shrine` builds the S7 layered 1 m map from `out/re/stage/shrine.manifest` (158 × 186 nodes)
and walks a 0.25 m lattice (628 × 741) from the spawn (0, 8.998, 1) with per-axis
`heightmap_walkable` moves and `heightmap_floor` feet: **39,488 points** reached, x −19.43…19.32,
z −86.5…**45.25**, feet −0.34…27.78. Along x ≈ 0: the hall/dais to z 21 (10.56), the altar
blocks z 21.5–24, the floor beyond at 10.56 to z 30, the front terrace at 9.79 (z 32–37), then the
front stair is entered from the sides (the lateral stairs, x ±11…19, feet 1.6 → 0.0 at z 36–40) and
the centre run x [−2.43, 2.32] descends 1.49 (z 41.5) → 0.08 (z 45.25), the farthest +Z reached.

- **`door_shrine`** (frame shrine, `source: authored`): min (−2.5, −0.5, 44.0), max (2.5, 3.0, 46.0)
  — **120** reached points inside (feet 0.07…0.43); it spans the walk's z max 45.25; 43.00 m from
  the spawn (≥ 5, not overlapping). World: x 297.5…302.5, z −256…−254, the top of the shrine steps.
- **`door_plain.arrive_shrine`** = (0, **9.79**, 34.0), yaw 180 (facing −Z, into the hall): on the
  front terrace, 10.00 m from `door_shrine` (≥ 3). A walk from it (`shrine_arrive.bin`) reaches
  the same 39,488 points — `door_shrine` (120 points) and the shrine spawn — so the way back works.
  A stair point closer to the door (0, 2.07, 40) was rejected: its own walk reaches 1,520 points and
  not the spawn (going back up, the terrace edge between z 37.5 and 37 is a 7.15 m rise).

### 1.3 The plain walk (unlayered, `refuseHoles`) → `door_plain`

`reach plain` rasterizes the seven ground glbs (`home`, `home_ground`, `home_cliff_displaced`,
`home_skirts`, `home_plainsgrey_skirt`, `home_pond`, `home_wall_pillars_01` — the kept set minus
`home_ground_high`; no backdrops) into the app's unlayered 1 m map (**952 × 1040** nodes, bounds
x −216.1…734.8, z −583.3…454.7) and walks node to node from the node nearest `spawn_plain`
(301, −7.14, −210; node height −7.073) with D3 emulated (refused: target node NaN, rise > 1.0 m,
drop > 1.5 m): **508,743 nodes** reached. The ground has a hole under the shrine's footprint
(x ≈ 220…370, z ≈ −410…−259); at x 297…303 the steps climb to 0.9–1.0 at z −257, and z −259 has
no ground.

- **`door_plain`** (frame world, `source: authored`): min (296, −2.0, −258.5), max (306, 2.5,
  −252.5) — **58** reached nodes inside (ground −0.83…1.37), 42.5 m from `spawn_plain`.
- **`door_shrine.arrive_plain`** = (301, **−2.333**, −245), yaw 0 (facing +Z, away from the shrine):
  the steps' foot, 7.50 m from `door_plain`; its own walk (`plain_arrive.bin`) reaches the same
  508,743 nodes, `door_plain` and `spawn_plain`.

### 1.4 The backdrops by bounds

Rule: a shrine-manifest level glb is a backdrop when its world-offset bounds are **not contained**
in the hall's (`cathedral_interior.glb`, world x 278.3…321.6, y 8.0…160.1, z −427.9…−271.9) —
the plain surrounds the shrine, so geometry outside the hall faces it — minus the `cathedral_int*`
pieces (the interior's own; `cathedral_int_shell` leaves the box only below the hall floor).
**27 of the 34** are kept (≈ 1.61 M triangles in all; the roof garden family — `cathedral_garden`,
`_garden_ground`, `_garden_fruitbranches`, `cathedral_sg_*` kept — is 0.49 M of it, a perf lever
for W1). World bounds (m) and triangle counts:

| glb | x | y | z | tris |
|---|---|---|---|---|
| `cathedral_ext_fur` | 218.1…221.1 | −1.5…21.3 | −342.2…−327.6 | 2,708 |
| `cathedral_ext_fur_02` | 258.6…268.0 | −0.1…20.9 | −415.4…−411.6 | 651 |
| `cathedral_ext_fur_03_a` | 333.3…339.4 | 0.1…65.7 | −406.6…−397.4 | 5,109 |
| `cathedral_ext_fur_03_b` | 327.8…330.0 | 50.9…61.6 | −418.5…−413.7 | 117 |
| `cathedral_ext_fur_03_c` | 315.6…319.6 | 46.7…66.7 | −428.4…−422.6 | 267 |
| `cathedral_ext_fur_03_d` | 312.8…316.0 | 47.6…54.4 | −429.0…−426.6 | 95 |
| `cathedral_ext_fur_03_e` | 307.5…311.9 | 55.5…74.1 | −432.7…−427.3 | 358 |
| `cathedral_ext_door` | 298.1…301.9 | −0.1…5.6 | −300.9…−300.0 | 2,192 |
| `cathedral_new_firstlevel` | 216.1…372.6 | −1.7…185.1 | −434.9…−262.5 | 503,666 |
| `cathedral_ext_front` | 274.5…325.5 | −0.6…38.6 | −301.2…−254.1 | 334,227 |
| `cathedral_ext1` | 255.0…355.6 | 124.6…286.1 | −420.0…−347.0 | 185,780 |
| `cathedral_sg_innerwalls` | 264.9…331.6 | 83.0…207.1 | −415.1…−305.5 | 1,483 |
| `sandpile1` | 291.3…308.8 | 2.7…9.9 | −268.4…−262.7 | 132 |
| `cathedral_entrance` | 289.9…309.6 | −0.1…106.6 | −439.0…−420.3 | 20,703 |
| `cathedral_mountain_structure` | 332.1…344.0 | 92.8…99.8 | −344.7…−333.4 | 1,816 |
| `rootsback1` | 281.8…307.3 | 61.2…124.5 | −437.4…−425.6 | 28,566 |
| `rootsfront1` | 321.0…358.4 | 6.2…43.2 | −289.8…−265.5 | 17,352 |
| `rootsside1` | 327.9…335.8 | 10.5…67.9 | −417.8…−404.0 | 15,006 |
| `cathedral_garden` | 258.3…332.7 | 60.5…206.7 | −417.0…−287.6 | 456,892 |
| `cathedral_garden_ground` | 270.1…331.5 | 83.6…99.6 | −407.5…−284.7 | 2,077 |
| `cathedral_sg_waterpoolleft` | 274.6…280.4 | 95.1 | −320.1…−314.4 | 8 |
| `cathedral_sg_waterpoolright` | 323.0…326.9 | 95.0 | −320.8…−317.1 | 8 |
| `cathedral_sg_waterfallleft` | 270.2…278.1 | 95.1…106.4 | −317.8…−309.9 | 1,058 |
| `cathedral_sg_waterfallmain` | 258.3…266.0 | 99.4…112.0 | −346.2…−338.6 | 1,058 |
| `cathedral_sg_waterfallright` | 324.4…329.3 | 95.0…102.5 | −319.3…−313.9 | 1,058 |
| `cathedral_garden_fruitbranches` | 270.9…329.9 | 98.0…113.7 | −352.1…−310.4 | 23,558 |
| `water_cathedral2` | 287.6…312.4 | 9.8 | −270.4…−262.3 | 202 |

Not kept (inside the hall's box, or `cathedral_int*`): `cathedral_int_shell`,
`cathedral_sg_waterpool`, `cathedral_sg_waterfallsmallleft/right`, `water_cathedral1`,
`cathedral_interior`, `cathedral_interior_skirts`.

## 2 · Agro and the probes (authored; checked on the plain walk)

| value | where | check (D3 rule over the node heights, 1 m steps) |
|---|---|---|
| `agro` | (301, −7.361, −203), yaw 180 (faces the spawn) | 7.0 m ahead (+Z) of `spawn_plain`; reached; slope 3.28° (max of the 3 × 3 central differences) |
| `probes.whistle_from` | (252.5, −167.7) = Agro + 60 m at heading −54° | straight line walkable both ways, worst slope 6.62° |
| `probes.gallop_line` | from Agro, heading −54°, 300 m (to (58.3, −26.7)) | walkable, worst slope 7.11° (< 10); the ±4° corridor −58…−50° stays ≤ 7.98° |
| `probes.edge` | (114.3, −280.5) → (76.9, −294.6), toward the plateau's west cliff | 26 m walkable of 40.0, then refused: **drop 4.89 m** (the nearest outer edge to the spawn is ≈ 225 m away, at (91, −290)) |

Headings from Agro, for the record (300 m lines): 0° walks but crosses a 10.13° spot, 2–4° stay
under 8°, 6° crosses a 60° face; 8–60° and −4…−36° end at rises within 32–255 m; −46° reaches
10.06°; the flat corridor is the −48…−60° fan (≤ 7.98°).

## 3 · The root tables and the footfalls

### 3.1 `root_z` / `rider_root_xz` — PASS (data)

`root_z.py tables` reads each clip's root position series with `parse2.decode_root` (every clip:
root keys = frames). `root_z` = z − z(frame 0), metres per frame, the 14 non-loop clips of Agro's 19
(the loops `stand1`, `walk`, `run_n`, `run`, `back` move by the measured `speeds`):

| clip | frames | fps (.anb) | z end (m) |
|---|---|---|---|
| `walk_start` | 9 | 15.9961 | 0.4417 |
| `walk_stop` | 14 | 15.5933 | 0.5603 |
| `run_n_start` | 22 | 15.0 | 3.9845 |
| `run_n_stop` | 20 | 15.0 | 2.0850 |
| `run_n_from_walk` | 23 | 15.3442 | 3.6213 |
| `run_start` | 25 | 15.0 | 16.5392 |
| `run_from_run_n` | 19 | 15.4248 | 9.7988 |
| `run_stop1` / `2` / `3` | 12 / 9 / 9 | 15.7104 / 15.0 / 15.0 | 3.8790 / 1.7212 / 0.2912 |
| `back_start` / `back_stop` | 14 / 14 | 15.5933 | −0.3968 / −0.4348 |
| `neigh` / `neigh_end` | 15 / 19 | 15.0 / 15.4248 | −0.6038 / 0.6038 |

`rider_root_xz` = the raw root (x, z) per frame (the ride clips are authored with `OptBone_boy` as
origin; y is the clip's own):

| clip | frames | fps | x start → end | z start → end |
|---|---|---|---|---|
| `stand_ride_l` | 26 | 15.0 | −0.5289 → 0.0 | 0.1091 → −0.0769 |
| `stand_ride_r` | 26 | 15.0 | 0.5692 → 0.0 | 0.1269 → −0.1055 |
| `stand_getoff_l_start` | 11 | 15.0 | 0.0 → −0.6675 | −0.0769 → 0.1199 |
| `stand_getoff_r_start` | 11 | 15.0 | 0.0 → 0.6675 | −0.0769 → 0.1199 |
| `stand_getoff_fall` | 8 | 16.1499 | −0.6676 → −0.9273 | 0.1199 → 0.1327 |

**For W1:** `stand_getoff_fall` starts at x −0.6676 — the end of `getoff_l_start` only. After
`getoff_r_start` (ending at x +0.6675) the fall's table would jump 1.335 m in x unless it is applied
mirrored (x → −x) on the right side (the clip itself is one-sided; design §6 measures that join at
35°).

### 3.2 Footfalls — measured; the design's values kept

Scratch bake (`--clips horse_a_uma_walk,horse_a_uma_run_n,horse_a_uma_run`, 30 animated tracks
each), pure-Python FK of `Bone_{F,B}_TUME_{L,R}` (model space, root x/z zeroed by the bake). Rule:
touchdown = the first frame of the hoof's longest cyclic run with height < 0.05 m above its loop
minimum; fraction = frame / (frames − 1). Per hoof (design → re-measured):

| gait | FR | BL | FL | BR | 1 frame |
|---|---|---|---|---|---|
| walk (17 f) | 0.25 → 0.25 | 0.375 → 0.375 | 0.75 → 0.75 | 0.875 → 0.875 | 0.0625 |
| trot `run_n` (10 f) | 0.33 → **0.222** | 0.89 → 0.889 | 0.0 → 0.0 | 0.0 → 0.0 | 0.111 |
| gallop `run` (10 f) | 0.67 → 0.667 | 0.95 → **0.0** | 0.44 → 0.444 | 0.11 → 0.111 | 0.111 |

10 of 12 agree; trot FR differs by one frame (the hoof is 0.04 m above its minimum at frame 2 and
0.00 at 3 — a threshold call) and gallop BL by half a frame (it lands between frames 8 and 9).
`footfalls` keeps the design's values (`source: measured`); `footfalls_remeasured` carries these.

### 3.3 R2 — the `.anb` event tracks: FALLBACK

Time-boxed. In the flag-clear structure of `horse_a_uma_walk`/`run`/`run_n`/`stand1` the bytes
between the last track blob and the structure are the per-track blob-pointer array (30 × u32), the
type-byte array, and a record pointed to by structure +0x1C with count 1 at +0x18 (0 on
`player_a_walk`/`player_a_uma_walk`) whose floats are one 0.13 (`OptBone_boy`'s seat offset) and
four 1.0 — a static transform, with an aux key series on `stand1` (31 entries = its frames; the
`re2-aux.md` family). No time-keyed event list and no strings: nothing to decode as footfall
events. The measured fractions stand.

## 4 · R3 — the whistle approach / riding camera: FALLBACK

Time-boxed, on `out/re/eboot.elf` (read-only; the recon's decompiles reused, no new Ghidra run):

- `FUN_007191f0` is the **`cp10::EHeadTrackState` enum-to-string table** (a 15-entry static table,
  strings at VA 0x127cc22…0x127cf16: `NONE`, `ACTIVE_SWORDHOIST`, `ACTIVE_BOSS`,
  `ACTIVE_BOSS_WHISTLE`, `ACTIVE_HORSE_WHISTLE`, `ACTIVE_LOOKAROUND`, `PASSIVE_TORCH`/`BOSS`/
  `HORSE`/`LIZARD`/`FRUIT`/`BIRD`/`LOOKAT`/`LOOKAT_HORSEVIEW`): head tracking, not the approach.
- The horse-call names (75 strings: `P_horse_called_conditions_(near/medium/far/boss/
  Force_finger_whistle)`, `*_(when_running)`, `*_(when_called_run)`, `Player_status_by_horse_call_*`,
  `Player_speed_by_horse_call`, `HORSE_CAMERA` ×1 at file 0x10549f3) are name-table entries; the
  registered-parameter scanner (`cvals.py`: RIP-relative LEA → the registration's range/default
  stores) finds **0** code references over the 75, so no distance/speed/camera value comes out
  within the box.

D12 (whistle: 150 m reach, trot ≤ 40 m / gallop beyond, stop 3 m, 10 s stuck warp 6 m behind) and
D13 (camera distances 6/7/9 m, +1.2 m mounted head height) stand as `source: authored`.

## 5 · Files

| file | what |
|---|---|
| `tools/re/s8/reach.cpp` | the walk harness: the app's heightmap code (read-only sources), shrine layered BFS and plain `refuseHoles` BFS, binary dumps |
| `tools/re/s8/root_z.py` | `tables` (the `.anb` root series) and `footfalls` (hoof FK on a scratch bake); stdlib |
| `tools/re/s8/placements.py` | composes and checks `placements.json` (the walks, Agro/probes, backdrops by bounds, the tables); stdlib |
| `tools/re/s8/placements.json` | the gate output (paths, numbers, record names — no game content) |
