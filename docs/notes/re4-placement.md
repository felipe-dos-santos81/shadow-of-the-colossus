# RE-4 placement pull — the MinA spawn in the Valus arena (2026-10-19)

S4.0 "The encounter" §7.3: a **bounded, static-only** pull for a MinA (Valus) spawn/placement
record in the arena's stage data. The milestone does not block on it; a labeled builder pick is
the designed fallback. This note records the method, **every candidate with its raw bytes/offsets
and cross-check numbers (including the rejected ones)**, and the verdict.

| | |
|---|---|
| Date | 2026-10-19 |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; `python3` 3.12.12 (stdlib + `ctypes`) |
| Repo / branch | `sotc-native`, `s4-encounter` |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Probe | **`tools/re/stage/spawn_probe.py`** — `cgpr`, `csml`, `cwks`, `path`, `ground`, `scan`, `arena` |
| Evidence dumps (gitignored) | `out/re/stage/s4-placement-arena.txt`, `out/re/stage/s4-arena01.cgpr.json`, `out/re/stage/s4-placement-cwks.txt` |
| Verdict | **negative** — no data record satisfies the bar; builder-pick fallback §8 |

`tools/re/stage/probe.py` is untouched; its `check-fs` re-run today is green (10/10, §5).

## 1 · The acceptance bar and the method

Design §7.3, quoted as the bar applied here: a data record counts only if

- **(a)** it is named/typed as the MinA spawn/placement — not a sensor, relay, camera, or prop;
- **(b)** its world point is inside the arena bowl (`x ≈ 237–431`, `z ≈ 297–390`), near the floor
  (`y ≈ 28–31` if world), and within ~100 m of the player start `(353, 345)`;
- **(c)** it carries x/z + yaw **without requiring an unknown frame origin**.

A bare float triple that merely looks plausible is a **negative**. The bowl/floor numbers used
below are the RE-3 measurements (`arena_01.glb` bounds `x[237.37, 431.39] z[297.56, 390.04]`,
floor at the player column `27.951 m`; `docs/notes/re-stage.md` §3.2–3.3), not assumptions.

Method: decompress the compiled script containers through the vendored Oodle dylib
(`tools/native/liboo2core_8_win64_.dylib`, personal use only), then walk the containers
structurally — the `.cgpr` entity records and their typed subobjects, the `.csml` static instance
lists, the `.cwks` dependency pool, and the `.path` nodes. Every transform printed below is decoded
from bytes; every cross-check number is computed from the decoded point.

### 1.1 The decode that makes the rest possible — measured

The `.cgpr`/`.csml`/`.csli`/`.cpid` files are **Oodle `8c0a` containers** (first two bytes `8c 0a`;
the last u32 holds the decompressed length plus the container base (`0x60000000` for `8c0a`/`8cc`,
`0x80000000` for `68c`/`6cc`); the container's last 12 bytes are
appended after the payload, reproducing the vendored `CompressionHandler`). `arena_01.cgpr`:
35,370 B → **201,277 B** body (+12 B footer). The body is not an opaque "compiled program": it is
the CGPR object graph, and it decodes.

A SOTC `.cgpr` **entity record**, measured on all **21,524** named records in the shipped family
(342 `.cgpr` files, all under `scripts/`):

```text
u32 0x10 | guid[16] | u8 nameLen | name | u32 0 | u32 count | 39-byte property block (GUID+u32, GUID+u24) | body
```

The body starts at **nameEnd + 47** and is a sequence of typed subobjects `u32 magic | u16 len (LE) | payload`.
Two magics are decoded here:

| magic | len | payload | meaning (measured) |
|---|---|---|---|
| `0x2D870A80` | 12 | 3 floats | a vec3 (bounds/scale) — optional, precedes the transform |
| `0xF543030E` | 48 | 12 floats = 4 rows of 3 | basis rows 0–2 + translation row 3 |

**Validation:** all **21,524/21,524** named records decode a transform at `nameEnd + 47`;
over the whole family `det = 1.000000` (min = max), `max |row norm − 1| = 1.9e‑7`,
`max |pairwise row dot| = 1.9e‑7`. A wrong layout or offset would not produce a proper rotation
on 21,524 independent records; the decode is therefore trusted, and the translations below are
read, not guessed.

Raw example — `arena_01.cgpr`, `North_MinoA_Sensor` record at **0x529** (the first Mino record):

```text
0x529  10 00 00 00                          guid length 16
0x52D  44 ee 9a 65 85 8d 35 4c 9c 1f 6c e3 4f 05 78 5e   GUID
0x53D  12                                   name length 18
0x53E  4e 6f 72 74 68 5f 4d 69 6e 6f 41 5f 53 65 6e 73 6f 72   "North_MinoA_Sensor"
0x550  00 00 00 00                          u32 0
0x554  02 00 00 00                          count 2
0x558  1e 45 29 c0 73 00 77 e7 6d e0 63 00 01 44 4d 09   property 1 GUID
0x568  c2 55 8f f3                          property 1 value
0x56C  4d 85 bf b8 73 93 51 fa 53 01 00 5f 7a 4a fb   property 2 GUID
0x57C  00 02 00                             property 2 value (3 B)
0x57F  80 0a 87 2d                          vec3 magic 0x2D870A80
0x583  0c 00                                len 12
0x585  10 3a 1a 42 42 9e 98 42 10 3a 1a 42   (38.556702, 76.309097, 38.556702)
0x591  0e 03 43 f5                          transform magic 0xF543030E
0x595  30 00                                len 48
0x597  96 f9 3a 3f  00 00 00 00  6b dc 2e 3f   row 0  ( 0.730371,  0.000000,  0.683051)
0x5A3  00 00 00 00  00 00 80 3f  00 00 00 00   row 1  ( 0.000000,  1.000000,  0.000000)
0x5AF  6b dc 2e bf  00 00 00 00  96 f9 3a 3f   row 2  (-0.683051,  0.000000,  0.730371)
0x5BB  06 69 cc 43  04 27 35 42  d4 33 ce 43   row 3  (408.820496, 45.288101, 412.404907)
0x5C7  (next subobject)
```

(`yaw` in the tables below is `atan2(row0.z, row0.x)`; the cgpr rotation's handedness is not
independently anchored — irrelevant here, because every candidate is rejected on (a)/(b) before
yaw matters. Translations are convention-free.)

The `count` column of the table is the u32 at nameEnd+4 (**2** for sensors, **3** for relays, **9**
for the cutscene relay); its role is not decoded, and it does not shift the body — the body is at
`nameEnd + 47` on all 21,524 records. The 24-bit field that ends the property block is likewise
measured, not explained.

## 2 · Candidate 1 — the arena `.cgpr` property graph

Files and counts (all decompressed and walked):

| file | raw B | body B | records | decoded transforms |
|---|---|---|---|---|
| `scripts/shadow/world/arena_01/_cmn/arena_01.cgpr` | 35,370 | 201,277 | **366** | 366 |
| `scripts/shadow/world/arena_01/_cmn/arena_01_shared.cgpr` | 10,775 | 49,676 | 63 | 63 |
| `scripts/shadow/world/arena_01_entrance/_cmn/arena_01_entrance.cgpr` | 15,303 | 58,521 | 146 | 146 |
| `.../arena_01_entrance_shared.cgpr` | 661 | 649 | 2 | 2 |
| `.../arena_01_entrance_low.cgpr` | 653 | 641 | 2 | 2 |
| `scripts/shadow/entities/colossi/_cmn/colossus_01_minotaur_a.cgpr` | 6,889 | 21,128 | 31 | 31 |

**Names.** `arena_01.cgpr` holds **27 Mino-named records: 13 sensors + 14 relays, and zero
records named/typed spawn, placement, body, or spawn-point.** The full set, with the decoded
translation, distance to the player start, and the bar:

| record | offset | count | translation (x, y, z) | yaw° | d(m) | y − 27.951 | verdict |
|---|---|---|---|---|---|---|---|
| `North_MinoA_Sensor` | 0x000529 | 2 | (408.820, 45.288, 412.405) | 43.08 | 87.52 | +17.34 | sensor; outside bowl |
| `North_MinoA_Relay` | 0x0006a4 | 3 | (374.785, 49.126, 380.973) | 0.00 | 42.05 | +21.18 | relay; y not near floor |
| `Central_MinoA_Sensor1` | 0x008fda | 2 | (332.852, 47.202, 306.821) | 30.94 | 43.17 | +19.25 | sensor |
| `Central_MinoA_Relay1` | 0x009158 | 3 | (338.963, 47.589, 342.036) | 0.00 | 14.35 | +19.64 | relay; y not near floor |
| `Sourh_MinoA_Sensor2` | 0x00d415 | 2 | (270.354, 46.264, 297.527) | −17.64 | 95.31 | +18.31 | sensor; outside bowl (0.03 m) |
| `South_MinoA_Relay` | 0x00d591 | 3 | (269.005, 52.793, 327.295) | 0.00 | 85.84 | +24.84 | relay; y not near floor |
| `Cutscene_MinotaurARelay` | 0x014ecd | 9 | (300.941, 51.608, 317.180) | 0.00 | 59.03 | +23.66 | relay; y not near floor |
| `North_West_MinoA_Sensor1` | 0x01609e | 2 | (446.161, 45.288, 380.403) | 62.64 | 99.66 | +17.34 | sensor; outside bowl |
| `North_West_MinoA_Relay1` | 0x01621f | 3 | (374.785, 49.126, 380.973) | 0.00 | 42.05 | +21.18 | relay |
| `North_East_MinoA_Sensor1` | 0x01989d | 2 | (370.264, 45.288, 432.525) | 14.94 | 89.21 | +17.34 | sensor; outside bowl |
| `North_East_MinoA_Relay1` | 0x019a1e | 3 | (374.785, 49.126, 380.973) | 0.00 | 42.05 | +21.18 | relay |
| `North_West_MinoA_Sensor2` | 0x01cae5 | 2 | (392.809, 45.288, 347.219) | 43.08 | 39.87 | +17.34 | sensor |
| `North_West_MinoA_Relay2` | 0x01cc66 | 3 | (374.785, 49.126, 380.973) | 0.00 | 42.05 | +21.18 | relay |
| `North_East_MinoA_Sensor2` | 0x020ab3 | 2 | (330.923, 45.288, 397.051) | 58.52 | 56.54 | +17.34 | sensor; outside bowl |
| `North_East_MinoA_Relay2` | 0x020c34 | 3 | (374.785, 49.126, 380.973) | 0.00 | 42.05 | +21.18 | relay |
| `Central_West_MinoA_Sensor1` | 0x023d41 | 2 | (361.980, 47.202, 321.991) | 36.07 | 24.70 | +19.25 | sensor |
| `Central_West_MinoA_Relay1` | 0x023ec4 | 3 | (338.963, 47.589, 342.036) | 0.00 | 14.35 | +19.64 | relay |
| `Central_East_MinoA_Sensor2` | 0x0255f5 | 2 | (316.596, 47.202, 361.501) | 55.17 | 39.97 | +19.25 | sensor |
| `Central_East_MinoA_Relay2` | 0x025778 | 3 | (338.963, 47.589, 342.036) | 0.00 | 14.35 | +19.64 | relay |
| `Central_East_MinoA_Sensor3` | 0x026cbd | 2 | (299.974, 47.202, 352.744) | 4.17 | 53.59 | +19.25 | sensor |
| `Central_East_MinoA_Relay3` | 0x026e40 | 3 | (338.963, 47.589, 342.036) | 0.00 | 14.35 | +19.64 | relay |
| `Sourh_MinoA_Sensor3` | 0x02863b | 2 | (273.752, 46.264, 351.437) | −9.85 | 79.51 | +18.31 | sensor |
| `South_MinoA_Relay1` | 0x0287b7 | 3 | (269.005, 52.793, 327.295) | 0.00 | 85.84 | +24.84 | relay |
| `Sourh_MinoA_Sensor4` | 0x02a9d2 | 2 | (244.346, 46.264, 355.863) | 9.88 | 109.20 | +18.31 | sensor; >100 m |
| `South_MinoA_Relay2` | 0x02ab4e | 3 | (269.005, 52.793, 327.295) | 0.00 | 85.84 | +24.84 | relay |
| `Sourh_MinoA_Sensor5` | 0x02d8a1 | 2 | (235.252, 46.264, 316.586) | −36.48 | 121.13 | +18.31 | sensor; outside bowl |
| `South_MinoA_Relay3` | 0x02da1d | 3 | (269.005, 52.793, 327.295) | 0.00 | 85.84 | +24.84 | relay |

Every one of the 27 fails **(a)**: the names are sensors/relays, the exact categories the bar
excludes. Independently they also fail **(b)**: their y is **45.288–52.793**, i.e. **+17.3 to
+24.8 m above the player-start floor** — wall-mounted trigger volumes, not floor placements. Six
sensors are outside the `arena_01.glb` footprint (one by 0.03 m; the rest sit on the surrounding
wall, whose union extends to z ≈ 502), and `Sourh_MinoA_Sensor4` is 109.2 m from the start. The relays repeat one
transform per group (all five north relays at `(374.785, 49.126, 380.973)`; all four central at
`(338.963, 47.589, 342.036)`), which is graph structure, not a placement.

**The name sweep is tree-wide, not just the arena.** All **342** `.cgpr` under `scripts/` were
walked: **21,524 named records**; the pattern `spawn|placement` matches **7** names across the
whole family:

```text
scripts/shadow/cutscene/_cmn/op1.cgpr              0x000bdb  MainMenuPlacement       t=(-3000.00, 91.25, -786.00)
scripts/shadow/entities/wander/_cmn/wander.cgpr    0x003c65  FoliageDisplacement     t=(0, 0, 0)
.../wander_lv1/lv4/lv8/lv12/lv16.cgpr              ~0x3c7x   FoliageDisplacement     t=(0, 0, 0)  (6 files)
```

No Mino/colossus spawn/placement record exists in the shipped `.cgpr` family.

**The entity's own script is at the origin.** `colossus_01_minotaur_a.cgpr`'s root record is
`MinotaurA` at **0x4** (name at 0x19, count 23 at 0x26, body transform at 0x51):

```text
0x51  0e 03 43 f5 30 00
0x57  00 00 80 3f 00 00 00 00 00 00 00 00 00 00 00 00   row 0 (1, 0, 0)
0x63  00 00 80 3f ...                                    row 1 (0, 1, 0)
0x6F  00 00 80 3f ...                                    row 2 (0, 0, 1)
0x7B  00 00 00 00 00 00 00 00 00 00 00 00                row 3 t=(0, 0, 0)
```

i.e. the entity definition is **identity at the origin** — the script says how Valus behaves, not
where he stands in the arena. The 30 other records (`weak1_Spot` at `(0.423, 0, 14.337)`, `Bone_*` at
bind-pose offsets such as `(11.606, 5.997, 6.023)`, `ScriptRelay_*` at `(0.250, 0, 11.165)`) are
small entity-local offsets, not arena world coordinates.

**`arena_01_shared.cgpr`** (63 records) is arena-wide settings/triggers (`A01 RenderSetting`,
`A01 surfacefog`, `Collision`, `StateMachine`, `StaticInstanceLoader`, `Corpse`, touch volumes…);
its only MinA strings are damage-zone names (`Mino_walking_damage_A/`, `Mino_club_attack_damage_A/`,
`Mino_foot_attack_damage_A/`) and the dead-body script
`$/scripts/shadow/entities/dead_colossi/****/minotaur_a_dead_body.cgpr` (a corpse entity, not a
spawn). **Delta against the recon:** the measured string is `minotaur_a_dead_body.cgpr` under
`dead_colossi/`, not `minotaur_a_body.cgpr`; there is no live-body reference in the arena scripts
and no transform for one.

## 3 · Candidate 2 — the `.csml` scene/instance lists

`.csml` is a **static instance list**: first u32 = count, then per instance a **48-byte transform
(4 rows of 3; row 3 = world translation)** followed by a subobject `magic 0xBB46D388 | u16 len |
payload`, the payload opening with a small count (8 in instance 0) and the `$/models/...`
reference strings. Walked and measured (the arena walk consumes all 1,792 records and ends at
0x5AD94 of the 0x60150-byte body; a trailing section follows):

| file | instances (u32 @0) | model refs | colossus/minotaur refs |
|---|---|---|---|
| `arena_01.csml` | **1,792** | 1,792 | **0** |
| `arena_01_shared.csml` | 71 | 71 | **0** |
| `arena_01_entrance.csml` | 2,009 | 2,009 | **0** |
| `arena_01_entrance_shared.csml` | 24 | 24 | **0** |

Raw head, `arena_01.csml` body:

```text
0x00  00 07 00 00                          count = 1792
0x04  00 00 80 3f 00 00 00 00 00 00 00 00  transform (instance 0, identity)
...   row 0 (1, 0, 0) row 1 (0, 1, 0) row 2 (0, 0, 1) row 3 (0, 0, 0)
0x34  88 d3 46 bb                          subobject magic 0xBB46D388
0x38  98 00                                len 152
0x3A  08 00                                count 8
0x3C  dd 5d 54 16                          string magic
0x40  42 00 41                             lengthLength 66, length 65
0x43  24 2f 6d 6f 64 65 6c 73 ...          "$/models/shadow/levels/arena_01/****/arena_01_skirt_1d2ff767.cmdl"
```

The instances are genuine world placements — e.g. instance 2 at `(362.222, 30.167, 327.241)`,
instance 4 at `(410.210, 29.775, 374.631)`, instance 6 at `(411.180, 29.395, 367.811)`. The game's
static props (skirt, rocks, debris) are placed this way. **The colossus is not one of them.**

Tree-wide check (all **189** `.csml` in the tree, **264,018 instances**): **17** model refs match
`coloss|minotaur|mino` — **16 `dead_colossi` dead-body meshes** (e.g.
`.../colossus_01_minotaur_a_deadbody/****/c01_minotaur_a_dead_body_5e2e6107.cmdl`, which live in
their own entity `.csml`, not the arena scene) and **one non-colossus prop**:
`arena_02_minotaur_doorframe_5917d87b.cmdl` (`arena_02.csml:0x0b63c6`, a doorframe named after the
arena). **None is a live colossus.** There is no live-colossus instance and no `$/scripts/` entity
ref in any arena `.csml` (0 script refs in all four arena files).

Two sibling families were checked and excluded:

- **`.csli`** (`arena_01_shared_f6296eb7.csli`, 271,208 B body): `TILS` footer, an index table at
  the head (`0x42368, 0, 0x18, 0x84, 0x7c4, 0x1a30, 0xc48c, 0x41868, …`), and exactly **one**
  path string — `$/scripts/shadow/world/arena_01/arena_01_probes.spid`. It is the light-probe
  instance list, not entities.
- **`.cpid`** (`arena_01.cpid`, 104,284 B body): count `1185` at 0x00, footer `<_DIP`; **0** words
  in the body form a plausible world triple (x 200–450, y 20–80, z 250–450) and there are no
  readable names. It is not a world-transform table for named entities.

## 4 · Candidate 3 — the `.cwks` workspace pool

`workspaces/_ps4/shadow.cwks`: 29,398,788 B, `PSKW` v6, **67,045** index records, **407,965**
string-pool entries (measured; `spawn_probe.py cwks`, the same parse as RE-3 §2).

- **378** entries contain `minotaur` (**287 distinct**): model/texture/anim/particle paths plus the
  entity scripts `$/scripts/shadow/entities/colossi/_cmn/colossus_01_minotaur_a.cgpr` (and `_hiza`,
  `_weapon`, B/C). These are dependencies, not placements.
- **3,838** entries contain `spawn` — **all** are particle-emitter assets
  (`.../emitter_N_spawn.csdr`, `.cprt`); **0** non-particle spawn strings.
- The four record fields are undecoded and the pointers land mid-string as often as on entry
  starts (`docs/notes/re-stage.md` §2.4) — a naive name-pointer reading is wrong, and no spawn
  record can be read from this file.

## 5 · Candidate 4 — the `.fs` hash tables

`cp3shadow/data/fs/stage/*.fs`: **1,896** files, `u32 n; n × {u32 v, u32 hash32}`, `size = 4 + 8n`
exactly, **83,433 entries**, **17,686 distinct `hash32`** values, **no strings**. `check-fs`
re-run today: **10/10 gating assertions pass (gate MET)**, including the 17,686 distinct-hash pin.

The `hash32` namespace is **unresolved**: RE-3 scanned 1,265,721 candidate names × 8 conventions
(FNV-1a/1, CRC32, Murmur3, xxh32, djb2, sdbm, java31) = 10,125,768 computations, and no convention
covered more than **8** of the 17,686 targets, where the random-function coincidence expectation
was **41.7** — the hits are birthday collisions (`docs/notes/re-stage.md` §1.3). Separately, the
**arena's stage id is itself unmapped** (`docs/notes/re-stage.md` §4 open item 2: the workspace
names 10 stage `.fs` lists, but no stage-id ↔ level-set mapping is decoded). The `.fs` family
therefore cannot yield a named/typed MinA record — it cannot yield a name at all.

## 6 · Rejected near-candidates (the "looks plausible" set)

These are recorded because each could tempt a bare-float-triple reading; all are negatives:

1. **`cp3shadow/path/pathdata/minotaur.path`** — `qath`, 800 B, header 0x30, **6 nodes** at a
   measured **0x30 stride**, floats at 0x40/0x70/0xA0/0xD0/0x100/0x130:

   | node | offset | (x, y, z) |
   |---|---|---|
   | 0 | 0x0040 | (−44.448, 35.000, 35.306) |
   | 1 | 0x0070 | (−81.625, 33.204, 87.386) |
   | 2 | 0x00a0 | (31.327, 32.936, 23.690) |
   | 3 | 0x00d0 | (0.235, 33.000, 22.175) |
   | 4 | 0x0100 | (−24.291, 33.932, 27.797) |
   | 5 | 0x0130 | (51.498, 33.000, 31.396) |

   bbox `x[−81.625, 51.498] y[32.936, 35.000] z[22.175, 87.386]`. The points *look* like a patrol
   route in a colossus-local frame (the y band is plausible), but the file carries **no transform**:
   its header is small u32 offsets only (`0x320, 0x30, 0x180, 0,0,0, 0x37, 0, 0x280, 0,0, 6, 0x150, 0,0`),
   and **no data-given origin** exists. Fails **(c)** by construction; the design's rule ("not used
   without an anchor for its frame") stands, and the file is not used for the pick.
2. **The 27 Mino sensor/relay transforms** — MinA-typed, world-space, but wrong type (a) and
   ~17–25 m above the floor (b). The nearest, `Central_MinoA_Relay1` at 14.35 m from the start,
   is still a relay at y = 47.589.
3. **The `MinotaurA` entity record** — identity at the origin; a definition, not a placement.
4. **`st17a_mino_start_camera` camdata** (`stage55.csv`): focal distances 37.1 m (frames 14/72/187)
   and 53.7 m (frame 259), but the CSV has no position column, and the camera clip
   `cp3shadow/anim/camera_st17_mino_start_camera.anb` has **1 track of type 36, which is
   undecoded** (`tools/re/anb/probe.py anb --curves`: `tracks_total 1`, `type 36`,
   `undecoded true`). No look-at target can be derived from measured data; not used.
5. **`.cpid` float scan** — 0 plausible world triples (see §3), so it is not a hidden transform
   table either.

## 7 · Verdict — **negative**

**No record in the shipped arena data satisfies (a) + (b) + (c).** The MinA-named records are 27
sensor/relay trigger volumes (y ≈ 45–53); the only truly placed entities in the arena scripts are
static props in `.csml` and effect emitters in `.cgpr`, none of them a colossus; the entity's own
script is at the origin; the `.cwks`/`.fs` families carry dependencies and hashes, not placements;
and `minotaur.path` has no frame origin. This is consistent with the recon's reading
(`docs/notes/s4-recon.md` §3): the colossus is created at runtime by the event system
(`MinA_Spawn`/`MinotaurARelay` are eboot event/class names, not transforms), so the shipped stage
scripts do not need to carry its spawn point.

**Inference (marked):** the spawn transform is produced by the event/flow layer at runtime, and
recovering it would need the bounded Ghidra pass on the `MinA_Spawn` path — out of this task's
timebox, and not needed (design §7.3: the pull never blocks the milestone).

## 8 · The builder-pick fallback (for Task 6)

The design's fallback: a labeled **builder pick** on the bowl floor, feet on the ground, facing the
player's data-derived spawn. The pick is on open floor, inside the bowl, on the player's forward
(+Z) side, 13.1 m from the Mino relays' column `(374.785, 380.973)` (corroboration only — the
relays themselves are rejected data). It is placed far enough south that the **yawed** bind
footprint fits inside the bowl (see the footprint check below).

**Entity (Valus) — provenance `builder-pick`:**

| field | value | source |
|---|---|---|
| provenance | **`builder-pick`** | this note; manifest header carries the value |
| x | **382.000** | chosen |
| z | **370.000** | chosen |
| y (manifest; ground sample) | **28.369** | ray-cast on `out/library/models/shadow/levels/arena_01/_cmn/arena_01_ground.glb` (8,258 tris), column (382, 370); the composer re-samples. This is the **manifest y**, not the model origin: the origin is this + the origin-to-feet lift (8.739 m, or the clip's root series). |
| yaw (Valus) | **−130.76°** (equivalently 229.24°) | Valus faces the player start; `atan2(353−382, 345−370)` |
| quaternion (x,y,z,w) | (0, **−0.909104**, 0, **0.416570**) | the same yaw about +Y |

**Player spawn — provenance `data-position` + `builder-yaw`:**

| field | value | source |
|---|---|---|
| provenance | **`data-position` + `builder-yaw`** | position from `misc/playerstartpositions.txt:1`; yaw chosen to face Valus |
| x / z | **353.000 / 345.000** | data (`353 29 345`) |
| y (manifest; ground sample) | **27.951** | ray-cast on the same glb at (353, 345); the file's own y is 29, not the feet height |
| yaw (spawn) | **+49.24°** | faces Valus; `atan2(382−353, 370−345)` |
| quaternion (x,y,z,w) | (0, **0.416570**, 0, **0.909104**) | the same yaw about +Y |

Checks (measured at the final pick):

- pick point: x = 382 ∈ [237.37, 431.39] ✔; z = 370 ∈ [297.56, 390.04] ✔; ground 28.369 ∈ [28, 31] ✔;
  distance to `(353, 345)` = **38.29 m** < 100 ✔; 0 `arena_01.glb`/`shared-source` triangles above
  the column (open floor) ✔.
- **yawed** bind footprint (local x[−17.481, 17.481], z[−4.439, 5.367] rotated by −130.76°):
  **x[366.521, 396.776] z[353.255, 386.139]** — fully inside the bowl; margins west 129.15 m,
  east 34.61 m, south 55.70 m, **north 3.90 m**. (The unrotated footprint is not the placed one:
  at the previous pick (370, 380) the yawed z reached ≈391.6, past the bowl's z max 390.04.)
- floor under the **rotated** footprint: 5×5 sample, **25/25 hits, 28.139–29.441 m**; the
  feet-on-floor rule uses the pick's 28.369 m + the origin-to-feet lift.

If Task 6 prefers a spot straight ahead of the S3 spawn yaw (0, +Z), `(353, 385)` at yaw 180° is
geometrically valid (yawed AABB x[335.519, 370.481] z[379.633, 389.439], floor 28.731, 40.0 m), but
its z margin is only 0.60 m; the pick above is preferred.

## 9 · Reproduce

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/stage/spawn_probe.py arena \
    > out/re/stage/s4-placement-arena.txt                    # the acceptance table
python3 tools/re/stage/spawn_probe.py cgpr \
    scripts/shadow/world/arena_01/_cmn/arena_01.cgpr \
    --json out/re/stage/s4-arena01.cgpr.json                 # records + transforms
python3 tools/re/stage/spawn_probe.py scan cgpr --pattern "spawn|placement"   # 342 files / 21,524 records
python3 tools/re/stage/spawn_probe.py scan csml --pattern "coloss|minotaur|mino"  # 189 files / 264,018 instances
python3 tools/re/stage/spawn_probe.py path cp3shadow/path/pathdata/minotaur.path
python3 tools/re/stage/spawn_probe.py ground \
    out/library/models/shadow/levels/arena_01/_cmn/arena_01_ground.glb 382,370 353,385 353,345
python3 tools/re/stage/spawn_probe.py cwks --match minotaur  # 407,965 entries
python3 tools/re/stage/probe.py check-fs                    # 10/10, gate MET
```

## 10 · What this task did not do

- No game or port run; no Ghidra dispatch; no converter/app/vendored-library change
  (`spawn_probe.py` is the only new file; `probe.py` is untouched and its `check-fs` is green).
- No follow-up RE: the event-layer `MinA_Spawn` path (Ghidra on the eboot) remains the escalation
  if a data transform is ever required. Under design §7.3 it is not required for S4.0.
