# Scene manifest format — the S3 W1↔W2 interface (2026-10-18) + the S4 `entity` extension

The S3 arena's app workstream (W1) assembles its scene from a **scene manifest**: a
line-oriented text file emitted by the RE-3 composition workstream (W2) — data-mined
offline, never parsed from original containers at runtime. This file is the committed
schema; the parser is `app/src/manifest.h` / `app/src/manifest.cpp` (`parse_manifest`), and
W2 emits the arena to

```text
out/re/stage/valus.manifest        (gitignored — derived from the game tree)
```

## Format

One directive per line; tokens are separated by whitespace. Blank lines (including
whitespace-only lines) are skipped, and a `#` starts a comment that runs to the end of the
line. The directives, verbatim:

```text
instance <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
entity <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName>
posed <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName> <seconds>
backdrop <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
floor <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
horse <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
spawn <px> <py> <pz> <yawDeg>
encounter <id>
```

- `instance` — one placed model. `<glbRelPath>` is a single token, relative to the
  converted library (`out/library`); `(px, py, pz)` is the world position in meters,
  `(qx, qy, qz, qw)` the rotation as a glTF-order quaternion (x, y, z, w), and
  `(sx, sy, sz)` the scale. The composer bakes each instance's
  `mat4_from_trs(position, rotation, scale)` into the meshes it contributes. An instance
  is static: its meshes carry no rig, and a skeleton/clips it may have are skipped.
- `entity` — one placed, **animated** model (the S4 addition). The ten numbers are the
  same placement as an `instance`'s; `<clipName>` names the exact clip the entity's own
  rig loops. An entity keeps its skeleton and clips as its own rig (its meshes get rig
  1, 2, ... in manifest order, the player's are rig 0), and a clip the model does not
  carry (or a model with no clips) is a load error naming the glb path and the clip —
  never a silent rest pose.
- `posed` — one placed **static** prop frozen in a pose (the S7 addition; Mono lying down).
  Placement as an `instance`'s, then the exact clip name and a time in seconds
  (finite, `>= 0`). At compose time the glb's clip is sampled once at `seconds`
  (clamped into the clip, like a pinned clock), every vertex's position and normal is
  CPU-skinned with the shader's weighted palette sum, and the result is appended as static
  meshes (`rig = -1`, no rig record, no playback, never an encounter entity). The TRS
  stays in each mesh's `model`; `localMin/localMax` and the scene bounds are of the
  posed vertices. A missing glb or a clip the glb lacks is a load error naming the path
  (and the clip). Posed props are appended after the entities, so rig and mesh ranges
  of the player and entities are unchanged.
- `backdrop` — the S8 addition: an `instance` that is drawn but never ground. Static
  meshes (`rig = -1`) with `MeshCpu::ground = false`; `build_heightmap` skips them, and they
  stay out of the scene bounds (the heightmap grid is sized by those), so a distant
  backdrop cannot stretch the walkable map. Loaded after the entities.
- `floor` — the S13 addition (arena_04's ruins): an `instance` that is ground **floors-only**.
  Static meshes (`rig = -1`, `ground = true`, in the scene bounds) with
  `MeshCpu::floorsOnly = true`: `build_heightmap` keeps only their up-facing triangles
  (`normal.y >= kWallSteepness · |normal|`, the normal `(p1 − p0) × (p2 − p0)` of the glTF CCW
  winding, flipped under a mirroring `model`) — so a ceiling or a down-facing floor adds no
  layer — and they never flag a wall. Loaded right after the instances (`SceneManifest::floors`).
  Always accepted by `parse_manifest`; the probe parses it only with `parse_manifest_text(...,
  floor=[])`. A manifest without `floor` lines builds exactly the map it did before.
- `horse` — the S8 addition: Agro. Placement as an `instance`'s, no clip. Like an `entity` it
  keeps its skeleton and clips as its own rig (index after the player and any entities,
  `Scene::horseRig`; `-1` when the manifest has none), but it is never an encounter entity —
  the app drives it. A model that cannot be loaded is a load error naming the path.
- `spawn` — where the player starts: position and facing yaw in degrees. Optional; without
  it `hasSpawn` is false and the app falls back to the scene bounds' centre.
- `encounter` — the S11 addition: which colossus the manifest's entity is (`valus`,
  `quadratus`). Read only when the caller opts in (`parse_manifest(path, true)`; the probe's
  `parse_manifest_text(..., encounter=[])`); otherwise it is an unknown directive, as before
  S11. At most one line; absent means `valus` (`SceneManifest::encounter`).

Token counts are exact (12 for `instance`, `backdrop`, `floor` and `horse`, 13 for `entity`, 14 for `posed`, 5 for `spawn`, 2 for `encounter`), and unknown
directives fail loudly. A malformed line throws `std::runtime_error` formatted
`<file>:<line>: <reason>`.

**Strict floats.** Every number must be a full-string finite float: the whole token has to
parse (`1.5x` is refused) and the value must be finite (`nan`, `inf`, `1e999` are refused).
There is no silent coercion and no default for a bad number.

## Example (3 lines)

```text
# valus arena — emitted by the W2 probe (provenance header)
instance models/shadow/levels/arena_01/_cmn/arena_01.glb 0 0 0 0 0 0 1 1 1 1
spawn 300 28.5 305 90
```

The arena glbs are already world-positioned, so W2's instances carry identity transforms;
the manifest is still where a transform would go — it is not special-cased.

## Example (the shrine, S7)

`out/re/stage/shrine.manifest` (gitignored; `probe.py compose --stage shrine`) carries the
34 cathedral `instance` lines in workspace pool-list order (identity transforms), 17 statue
`instance` lines (slot 1 the broken glb + its skirt, slots 2-16 intact; yaw about +Y written as
the glTF-order quaternion), one `posed` Mono and the `spawn`; the values come from
`tools/re/s7/shrine_placements.json`, the header holds no absolute path, and its sha256 is
pinned in `STAGES["shrine"]`:

```text
instance models/shadow/levels/cathedral/_cmn/cathedral_interior.glb 0 0 0 0 0 0 1 1 1 1
instance models/shadow/objects/actors/statues/_cmn/mammoth_statue.glb -14.159 9.5917 -3.0661 0 0.70715 0 0.707064 1 1 1
posed models/shadow/characters/mono/_cmn/mono.glb 0 11.4023 23 0 0 0 1 1 1 1 girl_a_death_loop 0
spawn 0 8.998 1 0
```

## Example (the plain, S8)

`out/re/stage/plain.manifest` (gitignored; `probe.py compose --stage plain`) carries the 7
`levels/home` `instance` lines in workspace pool-list order (the world frame, identity
transforms; `home_ground_high` dropped with the proxies), 27 `backdrop` lines (shrine-set glbs
at the shrine-to-world offset `300 0.071 -300`, identity rotation, scale 1), one `horse` line
(Agro: position and yaw from `tools/re/s8/placements.json`, the yaw as the glTF-order
quaternion) and the `spawn`. Pins in `STAGES["plain"]`: 7 instances, 27 backdrops, 3541 meshes
and 1,916,318 vertices over the 7 + 27 + 1 glbs, sha256
`3a2a9b3bdd7481ad71e5165957ac669b4ee47cfe26299a5100e1cf008048b06d`. The probe parses `backdrop`
and `horse` only for the plain stage (the arena and shrine manifests stay strict);
`probe.py check-manifest --stage plain` asserts it.

```text
instance models/shadow/levels/home/_cmn/home_ground.glb 0 0 0 0 0 0 1 1 1 1
backdrop models/shadow/levels/cathedral/_cmn/cathedral_ext_front.glb 300 0.071 -300 0 0 0 1 1 1 1
horse models/shadow/characters/agro/_cmn/agro.glb 301 -7.361 -203 0 1 0 0 1 1 1
spawn 301 -7.14 -210 0
```

## Example (the road, S9)

`out/re/stage/road.manifest` (gitignored; `probe.py compose --stage road`) is the plain manifest
byte-for-byte (header, 7 instances, 27 backdrops, the `horse`, the `spawn`), then a comment block,
the six `levels/arena_01_entrance` glbs as identity `instance` lines (`arena_01_entrance`,
`_furwall`, `_ground`, `_skirt`, `_skirt_02`, `_stairs`) and one identity `backdrop` naming
`levels/arena_01/_cmn/arena_01.glb`. The six are the encounter's 18 entrance instances minus the 8
hash-suffixed stubs, `_stairs_lod1`, `arena_01_entrance_low1` (excluded by name: it survives both the
`_lod[0-9]` and the `_low` suffix rule), the demoblocker and `arena01e_occluder`; the horse-ramp
collision glb is not in the set. Pins in `STAGES["road"]`: 13 instances (7 + 6), 28 backdrops (27 + 1),
3620 meshes and 2,256,460 vertices over the 13 + 28 + 1 glbs, spawn `301 -7.14 -210 0`, sha256
`3af63a18f88b12de32d07c4d10a33e0d91ed26306bc8193f0dfdcb97b70a5df4`. `compose --stage road` and
`check-manifest --stage road` use their own `compose_road`/`road_checks` (the plain helpers are
untouched); the check asserts the plain prefix is byte-equal to a fresh plain compose.

```text
instance models/shadow/levels/arena_01_entrance/_cmn/arena_01_entrance_stairs.glb 0 0 0 0 0 0 1 1 1 1
backdrop models/shadow/levels/arena_01/_cmn/arena_01.glb 0 0 0 0 0 0 1 1 1 1
```

## Example (arena_02, S11 — Quadratus)

`out/re/stage/arena_02.manifest` (gitignored; `probe.py compose --stage arena_02`, checked by
`probe.py check-manifest --stage arena_02`) is the stage *and* its encounter in one file — the
arena_01 `compose-encounter` stays hard-coded to Valus. The set is `models/shadow/levels/arena_02/`:

| glb files | dropped | why |
|---|---|---|
| 67 | 28 hash-suffix stubs | 72-byte reference containers (the shrine rule) |
| | 4 `*_collision*`, 3 `_lod*`, `arena_02_ruin_path_walls_low` | collision geometry; lower details of a kept glb |
| | `arena02_groundbttm_occluder`, `arena02_groundtop_occluder`, `arena02_occluder`, `arena_02_bridge_occluder` | invisible occlusion proxies (`occluder*` materials) |
| | `arena02_shadowblocker` | a shadow caster (`shadow_caster_2sided`) |
| | `arena_02_demoblocker`, `arena_02_demoblocker_02` | cutscene ("demo") blockers, `defaultSG`; one spans the whole arena up to y 55.9 |
| | `arena_02_minotaur_door` | the intact boss door is the pre-boot `Boss_mammoth_door_Whole` state; S11 starts after the boot with Quadratus outside (the `Broke` state, whose pieces are the animated `c02_boss_door_break` actor, not a level glb). The doorframe stays. |

That leaves 23: 19 world-frame glbs (identity lines, workspace pool-list order) and the 4
local-frame bridge glbs (`arena_02_bridge_support`, `arena_02_bridge_sunk`, `bridge_support`,
`bridge_support1`, all |x| ≤ 7). These are **placed from data**: `scripts/shadow/world/arena_02/_cmn/arena_02_shared.csml`
carries 13 instances of them (indices 11–23; the instance is a 48-byte transform — three basis
rows, then the translation — followed by a `0xbb46d388` subobject holding the `.cmdl` ref, whose
8-hex suffix is dropped as for the shrine idols). Every one is a pure +Y yaw of 0° or 180° at unit
scale along the bridge line x 299.9968, z −1002.9…−589.3 (`STAGES["arena_02"]["placed"]`). Then
`encounter quadratus`, the entity (`colossus_02_bull_a.glb`, clip `boss_a_stand`, the recon's
builder pick (240, −925), yaw 21.682292° facing the spawn) and the spawn (x/z from
`misc/playerstartpositions.txt:3`, yaw −158.317708° facing the entity). Both y values are sampled
on the composed instance lines (the highest surface over the column, each glb in its own frame):
−96.741 and −98.308, both on `arena_02_groundbttm`. The header holds no absolute path.

Pins (`STAGES["arena_02"]`): 67 files, 19 world-frame + 13 placed instance lines, 187 meshes and
489,757 vertices over the 32 instance lines' glbs, grounds (−96.741, −98.308), sha256
`e665de4716bfb1cbfddfd8be1d66bcca30424526d7d2c16bc572cfa83e566520`.

```text
instance models/shadow/levels/arena_02/_cmn/arena_02_groundbttm.glb 0 0 0 0 0 0 1 1 1 1
instance models/shadow/levels/arena_02/_cmn/arena_02_bridge_support.glb 299.9968 -102.8196 -837.3278 0 1 0 0 1 1 1
encounter quadratus
entity models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.glb 240 -96.741 -925 0 0.188087 0 0.982152 1 1 1 boss_a_stand
spawn 273 -98.308 -842 -158.317708
```

**arena_02 is layered** (measured 2026-10-23 with the app's own `build_heightmap` at 1 m, a
scratch harness): walking the layered map from the spawn with the player's rules reaches
278,599 nodes, and on 18,090 of them (6.5 %) the unlayered max-Y sits more than 1 m above the
feet (18,084 more than 3 m; worst 101.5 m). The roofs are the plateau's overhanging lips
(`arena_02_groundtop`, `arena_02`), the rock land-bridge (`arena_02_landbridge` + skirt, x ≈ 157–182,
z −782…−529), the stone bridge deck (the placed `arena_02_bridge_support`, ≈ 95 m overhead) and the
cave ramp (`arena_02_ground_ramp`, `arena_02_ruin_path_walls`, x 213–250, z −942…−912 — inside
Quadratus's yawed footprint). The spawn and entity columns and the straight line between them are
clean (unlayered − layered = 0.000 m there). So arena_02 needs `layered = true`; the deepest node
holds 51 floor layers and none reaches the cap, so `kHeightmapLayers` = 64 needs no change. The
layered map is large: 1111 × 1151 nodes, 409 MB of layers + wall flags — the scene bounds reach
z 0.1 because the player glb's rest pose at the origin is merged into them (the level alone spans
z −1150…−463).

## Example (arena_04, S13 — Phaedra)

`probe.py compose --stage arena_04` (`make arena04-manifest`; gate `probe.py check-manifest --stage
arena_04`, which needs `app/build/stage_flood`). The set is `models/shadow/levels/arena_04/` (26
glb files; the 9 hash stubs and the 7 invisible occluder / shadow-blocker meshes dropped = 10 kept,
cwks order per group): `arena_04_ground` and `arena_04_structures_platform` as `instance`, the three
ruins glbs (`ruins_entrance`, `ruins_skirts`, `ruins_corridors`) as `floor`, and `arena_04`, the moss
fur, the skirts and the two water planes as `backdrop` (the corridor water at y −0.095, its
`arena_04.csml` placement). Then `encounter kirin`, Phaedra (`c04_kirin.glb`, `kirin_a_stand`) at
(1122, 53.76, 147) on the platform facing the ruins' centre (990, 140), and the player start
`spawn 984 52.5 142` verbatim. The north road (`arena_04_ground` prims 2 and 5, terrain class 5) is
kept (spec D9). Pins in `STAGES["arena_04"]`: 49 meshes, 148,792 vertices, sha256
`1062c045766621e12d57765db0dae69fc79f7e1b4244828adb9ee6021a442940`.

The gate floods the app's own layered map (`app/tools/stage_flood.cpp`: `compose_scene` +
`build_heightmap` 1 m, layered, static bounds + node-to-node `heightmap_walkable`) from the start and
from the four tunnel mouths (measured 2026-10-24): 422 × 312 nodes, 40.68 MiB; 0 out-of-bounds edges
(the map's edge holds at the road's end too); the road's end (z < −15) reached at x 986–994,
z −19…−15, y 60.34–61.69 (Task 13's backstop box); 201 of the 207 ring-corridor layers
(y 43.5–44.6) reached from each mouth, lowest reached floor 43.73; 0 layers at the dirt prim's
phantom 42.49. With the ruins as `instance` lines instead, the ring is reached 0 times and the
phantom floor holds 375 layers.

## Output path

W2's `compose` step writes the arena manifest to **`out/re/stage/valus.manifest`**
(gitignored, regenerable); W1.5's acceptance consumes exactly that path and fails loudly
when it is absent.

The other stages default to their own file (`probe.py` `STAGE_OUT`; `compose --stage X` without
`--out`, and `check-manifest --stage X` without a file): `shrine` → `shrine.manifest`, `plain` →
`plain.manifest`, `road` → `road.manifest`, `arena_02` → `arena_02.manifest`, `arena_03` →
`arena_03.manifest`, `arena_04` → `arena_04.manifest`, all under
`out/re/stage/`. Only `arena_01` (the default stage) writes `valus.manifest`.
