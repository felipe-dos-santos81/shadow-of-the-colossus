# Scene manifest format — the S3 W1↔W2 interface (2026-10-18)

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
spawn <px> <py> <pz> <yawDeg>
```

- `instance` — one placed model. `<glbRelPath>` is a single token, relative to the
  converted library (`out/library`); `(px, py, pz)` is the world position in meters,
  `(qx, qy, qz, qw)` the rotation as a glTF-order quaternion (x, y, z, w), and
  `(sx, sy, sz)` the scale. The composer bakes each instance's
  `mat4_from_trs(position, rotation, scale)` into the meshes it contributes.
- `spawn` — where the player starts: position and facing yaw in degrees. Optional; without
  it `hasSpawn` is false and the app falls back to the scene bounds' centre.

Token counts are exact (12 for `instance`, 5 for `spawn`), and unknown directives fail
loudly. A malformed line throws `std::runtime_error` formatted `<file>:<line>: <reason>`.

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

## Output path

W2's `compose` step writes the arena manifest to **`out/re/stage/valus.manifest`**
(gitignored, regenerable); W1.5's acceptance consumes exactly that path and fails loudly
when it is absent.
