# Task 6 report — club attachment

Status: **DONE**. Commit `d9b90d1 feat(app): attach Valus's club to OptBone_sword`.

## What I implemented

**Bind world (`app/src/scene.h`, `app/src/asset_io.cpp`).**

- `JointCpu` gains `Mat4 bindWorld = mat4_identity()`.
- `compose_bind_world(SkinCpu&)` runs once per loaded skin, after the inverse
  bind matrices are read: `bindWorld[j] = bindWorld[parent] *
  TRS(restT, restR, restS)`, parent first. The rest pose is the bind pose, so
  this is the inverse of `inverseBind` without a matrix inverse. The pass uses
  the same order-independent chain walk as `animation_pose` (a parent listed
  after its child still resolves; a malformed cycle stops at the chain-length
  guard).
- Verified against the shipped data: for `OptBone_sword` (skin joint 139) the
  composed bind world differs from `inverse(inverseBind)` by 3.7e-6, and
  `bindWorld * inverseBind` is the identity within float noise.

**Club model (`app/src/anim.h`, `app/src/anim.cpp`).**

- `club_model(scene, palette, swordJoint, entityMeshModel)` returns
  `entityMeshModel * palette[swordJoint] * bindWorld[swordJoint]`.
- The prescribed signature carries no rig index, so the binding rig is the rig
  whose skin has exactly `entityPalette.size()` joints, preferring a non-player
  rig. No match, an empty palette, or an out-of-range joint returns the identity
  — never NaN.

**Static load (`app/src/asset_io.h`, `app/src/asset_io.cpp`).**

- New `append_static_model(scene, library, glbRelPath, model)`: appends a glb's
  meshes to an existing scene with `rig = -1` (no rig record), material indices
  offset by the scene's material count, textures appended to the shared list,
  and the scene's world bounds grown by its vertices. Returns the first mesh
  index. It is the club's path: an unskinned prop, not a skinned model.

**App (`app/src/app.cpp`).**

- Constants `kClubGlb` (`.../c01_minotaur_a_weapon.glb`) and
  `kClubSocketJoint` (`"OptBone_sword"`).
- `ClubAttachment` holds the loaded flag, entity rig, resolved socket slot, and
  the club's half-open mesh range.
- `load_club` runs in manifest mode with an entity: it resolves `OptBone_sword`
  **by name** in the entity rig, then appends the club at the entity's mesh
  placement. A rig without the joint leaves the club undrawn with one log line
  naming the glb and the missing joint.
- The club is appended **after** `build_heightmap` (it is held in the air, not
  terrain) and **before** `renderer.upload` (the renderer uploads its mesh list
  exactly once).
- `update_all_poses` now takes `Scene&` and the club; after every rig's palette
  is computed it calls `update_club_model`, which reads the entity's live first
  mesh model (manifest TRS plus the once-per-run grounding lift) and sets every
  club mesh to `club_model(...)`. All draw paths (interactive, `--screenshot`,
  and every selftest path) go through this one function.
- The S4.1 `defeat-noentity` control now translates the club along with the
  entity body, so the control frame has no floating club.

## Bind composition

```
bindWorld[root] = TRS(restT, restR, restS)
bindWorld[j]    = bindWorld[parent(j)] * TRS(restT, restR, restS)

palette[j]      = worldJoint(j) * inverseBind(j)
clubModel       = entityMeshModel * palette[OptBone_sword] * bindWorld[OptBone_sword]
                = entityMeshModel * worldJoint(OptBone_sword)
```

The club's vertices are authored in `OptBone_sword`'s bind frame (identity
attachment); its grip is the local origin. The recon chain
`Bone_root → Bone_HAND_A_R → OptBone_sword → OptBone_hit` was reproduced from
the loaded rest chain; `OptBone_hit`'s local (0, 0, −7) carried through the club
model lands within 2 cm of the posed `OptBone_hit` (the recon measured 1.5 cm
for the authored tip). No matrix inverse was added.

## RED evidence

Tests written first; the build failed because the interface did not exist:

```
app/tests/test_asset_io.cpp:486:44: error: no member named 'bindWorld' in 'JointCpu'
app/tests/test_asset_io.cpp:1034:31: error: use of undeclared identifier 'append_static_model'
app/tests/test_asset_io.cpp:1105:28: error: use of undeclared identifier 'club_model'
app/tests/test_anim.cpp:1498:34: error: no member named 'bindWorld' in 'JointCpu'
app/tests/test_anim.cpp:1556:25: error: use of undeclared identifier 'club_model'
gmake[2]: *** [CMakeFiles/sotc-app-tests.dir/build.make:107: .../test_asset_io.cpp.o] Error 1
```

## GREEN evidence

Focused (the five new cases):

```
./app/build/sotc-app-tests --test-case="palette times bindWorld maps the bind origin to the posed joint position,club_model composes entityModel * palette * bindWorld,club_model resolves the rig that produced the palette,the club glb loads as one static mesh with 9701 vertices,OptBone_sword resolves in the Valus rig and carries the club"
[doctest] test cases:  5 |  5 passed | 0 failed | 192 skipped
[doctest] assertions: 83 | 83 passed | 0 failed
```

Existing loader fixtures extended and green:

```
a two-joint fixture parses skin and animation             (bindWorld 1/2, bindWorld*inverseBind == I)
a joint node carrying a matrix decomposes into its rest TRS (bindWorld = T(0,3,0) with the 2x basis)
```

Full unit suite:

```
./app/build/sotc-app-tests
[doctest] test cases:   197 |   197 passed | 0 failed | 0 skipped
[doctest] assertions: 14650 | 14650 passed | 0 failed
```

Required command:

```
export SOTC_LIB="$(pwd)/out/library"
cmake --build app/build -j 8
ctest --test-dir app/build -R unit_tests --output-on-failure
1/1 Test #1: unit_tests ... Passed 9.80 sec
100% tests passed, 0 tests failed out of 1
```

Full ctest:

```
1/2 Test #1: unit_tests ......... Passed  9.83 sec
2/2 Test #2: windowed_selftest .. Passed  0.19 sec
100% tests passed, 0 tests failed out of 2
```

Regression selftests against `out/re/stage/encounter.manifest`:

```
s4-status ... feet=ok ...                (exit 0; heightmap unaffected by the club)
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1  (exit 0)
```

Missed-socket path exercised with an Agro entity (no `OptBone_sword`):

```
sotc-viewer: models/shadow/characters/agro/_cmn/agro.glb has no OptBone_sword joint; the club is not drawn
s1-scene meshes=19 ...                   (plank + Wander + Agro; no club appended)
```

## Still check

Command (manifest mode, stand clip pinned at t = 0, entity target framing):

```
./app/build/sotc-viewer --library out/library \
  --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --entity-time 0 --camera-target entity \
  --screenshot out/s42/t6-club-stand.png --frames 12 --width 1280 --height 720
```

Result: **`out/s42/t6-club-stand.png`** (2560×1440). Valus stands at
`minotaur_a_stand` t = 0; the club is visibly in his right hand (image-left),
head hanging beside the right knee, entirely above the ground plane. No club
geometry appears detached, at the origin, or at another socket.

A second still at t = 0.8 (`out/s42/t6-club-stand-t08.png`) shows the same
attachment with the hand/club shifted by the idle animation — evidence the club
follows the posed joint, not the bind pose.

Numerically (library-gated test): the club pose is finite and its lowest posed
vertex is more than 1 m above the entity's manifest ground (28.369 m) at the
stand pose. The S4.1 stills were regenerated under `out/s42/t6-s41-stills/`; the
`defeat-noentity` control contains neither the body nor the club.

## Files changed

- `app/src/scene.h` — `JointCpu::bindWorld` with its composition contract.
- `app/src/asset_io.cpp` — `compose_bind_world` called by `load_skin_into`;
  `append_static_model`.
- `app/src/asset_io.h` — `append_static_model` declaration/contract.
- `app/src/anim.h` / `app/src/anim.cpp` — `club_model`.
- `app/src/app.cpp` — club constants, `ClubAttachment`, `joint_index_named`,
  `load_club`, `update_club_model`, per-frame update in `update_all_poses`,
  ground/upload ordering, club in the defeat control.
- `app/tests/test_anim.cpp` — bind-origin mapping (rest and turned), club model
  product, rig resolution, invalid-input identity.
- `app/tests/test_asset_io.cpp` — bindWorld assertions on the two loader
  fixtures; club static load (9701 verts, rig −1, no rig, material, textures);
  `OptBone_sword` resolution, grip/tip placement, above-ground pose.

## Self-review findings

1. **Ground ordering.** The club is static geometry (`rig = -1`); appending it
   before `build_heightmap` would have let its triangles raise the ground under
   Valus. It is appended after the ground sample; `s4-status feet=ok` confirms
   no change.
2. **One upload.** The renderer accepts exactly one `upload(scene)`; the club is
   appended before it. No renderer change.
3. **No stale club.** The pose update is centralized in `update_all_poses`, the
   single function every rendered frame calls (including all selftest and
   screenshot paths), so the club cannot lag its palette.
4. **The club follows the lift.** `update_club_model` reads the entity's live
   first-mesh model instead of copying it, so a later grounding lift is
   inherited automatically.
5. **Missing socket is inert.** `load_club` logs once and never appends; no
   palette math, no NaN, no wrong-hand prop (verified with Agro above).
6. **Controls stay honest.** The defeat presence control hides the club with
   the entity, so its diff still means "the entity is drawn".
7. **No inverse, no math change.** `compose_bind_world` only multiplies TRS
   matrices; `animation_pose`, the shader and the renderer are untouched.
8. **The recon is pinned.** The loader fixtures now assert the composed bind
   world, and the Valus test asserts `bindWorld * inverseBind == I` and the tip
   point's 2 cm agreement with posed `OptBone_hit`.

## Concerns

- **Texture duplication (minor).** `append_static_model` uses the normal glb
  load path, so the club's two 1024² PNGs (already decoded for Valus) are
  decoded into the scene a second time (~16 MB RGBA). No conversion was added;
  deduplicating by `sourceRelPath` would change the loader counts pinned by
  existing compose tests and is out of this task's scope.
- **Rig identity is inferred from palette size.** The prescribed `club_model`
  signature has no rig index. In this app the entity rig is unambiguous
  (238 joints vs the player's 348), and the test pins "prefer a non-player rig
  with matching size". Two different skins with the same joint count in one
  scene would need an explicit rig argument later.
- **One entity.** The app loads a club for `manifest.entities[0]` only, matching
  S4's one-entity encounter.
- **Rigid attachment.** In the death pose the club can end up under the torso
  (visible only as the body; rigid attachment is this task's scope — dropping
  the weapon is not modeled).
- **Out-of-order parents** are handled by the shared chain walk but the loader
  fixtures list parents first; only the Valus chain (also parent-first) is
  exercised on real data.
