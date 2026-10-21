# Task 2 report — Shell-derived weakpoint proxy

**Status:** COMPLETE
**Commit:** `3cd105d feat(app): shell-derived weakpoint proxy`
**Branch:** `s4-2-he-fights-back`

## What I implemented

Two interfaces from the design §7.1:

- `app/src/anim.h/.cpp` — `posed_weakpoint_proxy(const Scene&, std::size_t rigIndex,
  const std::vector<Mat4>& palette, const std::string& materialName, Vec3& outCenter,
  float& outRadius)`. It scans only the rig's own `[meshBegin, min(meshEnd, meshes.size()))`
  range, selects meshes whose `materialIndex` is in range and whose material name matches
  exactly, poses every vertex with the shader's weighted palette sum (zero-weight
  passthrough), applies `mesh.model`, and reduces the posed points to a sphere.
- `app/src/grab.h/.cpp` — `weakpoint_material_name(int)`: 0 → `"weakpoint_03"`,
  1 → `"weakpoint_04"`, anything else → `nullptr`.

`posed_bounds`, the band solver, and all existing behavior are untouched. No CMake changes:
both functions live in already-compiled translation units (`anim.cpp`, `grab.cpp`).

## RED evidence

Tests were written first, then the build was run before any implementation:

```
app/tests/test_anim.cpp:1323:17: error: use of undeclared identifier 'posed_weakpoint_proxy'
app/tests/test_anim.cpp:1338:13: error: use of undeclared identifier 'posed_weakpoint_proxy'
app/tests/test_anim.cpp:1381:13: error: use of undeclared identifier 'posed_weakpoint_proxy'
app/tests/test_anim.cpp:1388:13: error: use of undeclared identifier 'posed_weakpoint_proxy'
app/tests/test_grab.cpp:276:23: error: use of undeclared identifier 'weakpoint_material_name'
...
gmake: *** [Makefile:101: all] Error 2
```

An intermediate green run exposed one test bug of mine: extending `meshEnd` to 1000 in the
scoping fixture legitimately pulled in the decoy at index 2, so the test — not the
implementation — was wrong. The clamp check now runs on the one-mesh fixture. The
implementation was not changed to satisfy it.

## GREEN evidence

Focused (`--test-case="*weakpoint*"`):

```
[doctest] test cases:  6 |  6 passed | 0 failed | 167 skipped
[doctest] assertions: 73 | 73 passed | 0 failed |
[doctest] Status: SUCCESS!
```

Full `ctest --test-dir app/build --output-on-failure`:

```
1/2 Test #1: unit_tests .......................   Passed    9.24 sec
2/2 Test #2: windowed_selftest ................   Passed    0.22 sec

100% tests passed, 0 tests failed out of 2
```

## Files changed

- `app/src/anim.h` — declaration + semantics comment, immediately after `posed_bounds`.
- `app/src/anim.cpp` — `is_finite(Vec3)` helper in the file's anonymous namespace and the
  proxy implementation (after `posed_joint_world_position`, where `transform_point` exists).
- `app/src/grab.h` — `weakpoint_material_name` declaration after `climb_band_name`.
- `app/src/grab.cpp` — the id → material-name switch.
- `app/tests/test_anim.cpp` — 6 new test cases (centroid/radius, weighted blend, rig-range
  scoping + range clamp, failure matrix with untouched-output assertions).
- `app/tests/test_grab.cpp` — the id → name mapping test.

## Exact pose/radius math shipped

For each vertex `p = (px, py, pz)` of a selected mesh:

```
posed = Σ_i w_i · (palette[j_i] · p)        over i = 0..3 with w_i ≠ 0 and j_i < palette.size()
mesh_space = posed                if Σ w_i > 0
           = p                    otherwise (zero-weight passthrough)
world = mesh.model · mesh_space   (affine xyz transform)
```

`center = (Σ world) / vertex_count` (arithmetic mean over every selected posed vertex).

The posed bounding box is tracked over the same `world` points; with `min`/`max`:

```
corner_c = ( (c&1) ? max.x : min.x,
             (c&2) ? max.y : min.y,
             (c&4) ? max.z : min.z )   for c = 0..7
radius = sqrt( max_c |corner_c - center|² )
```

Refusal (returns `false`, writes nothing): empty skin; `palette.size() != joints.size()`;
no selected mesh/vertex; a non-finite weight; a non-finite posed world point; a non-finite
centre or radius. Outputs are assigned only after every check passes. A `meshEnd` past
`Scene::meshes` is clamped; a `meshBegin` past it yields no iterations.

## Self-review findings

- **Trap avoided:** the scan starts at `rig.meshBegin`, never at 0 and never via
  `player_rig`; the empty rig 0 in the fixture and the decoy-shell test both pin this.
- `materialIndex < 0` and `>= materials.size()` are skipped (tested for `-1` and `3`).
- The weighted sum mirrors `posed_bounds`/the shader exactly, including the unnormalized
  sum and zero-weight passthrough; a dedicated test blends 0.5/0.5 across two joints.
- No per-frame cost path touched; nothing calls the proxy yet (Task 3 does, on release).
- `posed_bounds` and the climb band table are byte-for-byte unchanged.
- One new file-scope helper (`is_finite(Vec3)`), no name collision in the TU; build is
  warning-clean.

## Concerns

- The proxy deliberately treats a vertex whose weights all reference joints outside the
  palette as an unskinned (passthrough) vertex, matching `posed_bounds`. Malformed assets
  therefore degrade to bind positions rather than failing; a future strictness ruling could
  change this.
- Profile-time cost is not measured here (design estimates 204–730 vertex transforms per
  strike release); Task 3 owns the call site and the acceptance numbers.
