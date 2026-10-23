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
spawn <px> <py> <pz> <yawDeg>
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
- `spawn` — where the player starts: position and facing yaw in degrees. Optional; without
  it `hasSpawn` is false and the app falls back to the scene bounds' centre.

Token counts are exact (12 for `instance`, 13 for `entity`, 14 for `posed`, 5 for `spawn`), and unknown
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

## Output path

W2's `compose` step writes the arena manifest to **`out/re/stage/valus.manifest`**
(gitignored, regenerable); W1.5's acceptance consumes exactly that path and fails loudly
when it is absent.
