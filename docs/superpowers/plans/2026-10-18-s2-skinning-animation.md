# S2 app — skeletal skinning + animation playback — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The viewer loads a converted model's skeleton and glTF animations, skins the mesh on the GPU, and plays a clip — with tests pinning the parsing, the pose math, and a deterministic screenshot smoke.

**Architecture:** Extend `Scene` with skin joints and animation channels parsed from the glb (cgltf); a new CPU pose evaluator (`anim.cpp`) samples channels and composes the joint palette (worldJoint × inverseBind); the renderer uploads the palette as a vertex-stage storage buffer and the MSL shader skins position/normal by 4 joint weights; playback state lives in the app behind a grep-able status line. A built-in procedural clip keeps the app fully demoable before the converter emits real clips (Plan C, RE-1-gated).

**Tech Stack:** C++20 · CMake (Unix Makefiles) · SDL3 + SDL_GPU/Metal (runtime MSL) · cgltf · doctest · the existing `app/` project.

**Spec:** `docs/superpowers/specs/2026-10-18-first-playable-design.md` (§1–2 first playable; §4 demo clips) + the inherited `docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md` (D-S7: animations as glTF tracks; §6 S2).

## Global Constraints

- macOS/Apple Silicon only; C++20; configure with `cmake -S app -B app/build` (Unix Makefiles).
- SDL3 ≥ 3.4 via `find_package(SDL3 CONFIG REQUIRED)`; shaders ship as **runtime MSL source** (`app/shaders/mesh.metal`, copied next to the binary; entry points `mesh_vertex`/`mesh_fragment`; no metallib — the build machine has no Xcode Metal toolchain).
- **Windowed always; the cursor is never captured** — new input must not use relative mouse mode, grab, or capture.
- The app reads converted assets only — never the game tree.
- Tests need `SOTC_LIB` (the converted library) and fail fast when unset — never skip silently.
- Run everything with `ctest --test-dir app/build --output-on-failure`; doctest is vendored.
- Assets and derived outputs are never committed; `out/` and `app/build/` are gitignored.
- Small typed commits (`feat:`, `fix:`, `test:`, `chore:`, `docs:`).

## Review Focus

Failure modes this milestone will meet that the happy path does not exercise; each gets its test in the task noted.

1. Vertices with no/zero weights (the converter emits unskinned sub-meshes) must render in bind pose — never collapse to the origin. → A1 (defaults) + A3 (smoke).
2. A primitive without JOINTS_0/WEIGHTS_0 inside a skinned model — the loader defaults it; no throw. → A1.
3. An animation channel targeting a non-joint node (camera/helper bones) — skipped and counted; never a crash or a wrong joint. → A1.
4. CUBICSPLINE samplers (3 values per key) read as LINEAR silently mis-animate — the loader must refuse loudly. → A1.
5. Large joint counts (wander: 348; colossi larger) — the palette is sized from the skin and uploaded per frame without per-joint chatter. → A2/A3.

---

### Task A1: Skin + animation parsing, quaternion math

**Files:**
- Modify: `app/src/math3d.h` (Quat + ops), `app/src/scene.h` (Vertex 56 B; skin/animation types), `app/src/asset_io.cpp` (parse + skip rules)
- Create: `app/tests/test_anim.cpp` (quat math for now; grows in A2)
- Test: `app/tests/test_asset_io.cpp` (extend)
- Modify: `app/CMakeLists.txt` (add `test_anim.cpp` to `sotc-app-tests`)

**Interfaces:**
- Consumes: `load_scene` (S1), cgltf.
- Produces (exact):

```cpp
// math3d.h
struct Quat { float x, y, z, w; };
Quat quat_normalize(Quat q);
Quat quat_mul(Quat a, Quat b);            // Hamilton product; rotation a applied after b
Quat quat_slerp(Quat a, Quat b, float t); // shortest path; t in [0,1]
Mat4 quat_to_mat4(Quat q);                // column-major rotation
Mat4 mat4_from_trs(Vec3 t, Quat r, Vec3 s);

// scene.h — Vertex gains (56 bytes total):
//   uint16_t j[4]; float w[4];   // attributes 3 (USHORT4) and 4 (FLOAT4)
struct JointCpu {
    std::string name;
    int parent;          // index into SkinCpu::joints, -1 = root
    Mat4 inverseBind;
    Vec3 restT; Quat restR; Vec3 restS;   // rest local TRS, decomposed from the node matrix
};
struct AnimationChannelCpu {
    int joint;           // index into SkinCpu::joints (channels to non-joint nodes are skipped)
    int path;            // 0 = translation, 1 = rotation, 2 = scale
    int interpolation;   // 0 = LINEAR, 1 = STEP
    std::vector<float> times;   // seconds, strictly increasing
    std::vector<float> values;  // 3 (T/S) or 4 (R) floats per key
};
struct AnimationCpu { std::string name; float duration; std::vector<AnimationChannelCpu> channels; };
struct SkinCpu { std::vector<JointCpu> joints; };
// Scene gains: SkinCpu skin; std::vector<AnimationCpu> animations;
```

- [ ] **Step 1: Write the failing tests**

In `app/tests/test_anim.cpp` (with `#include "math3d.h"`):

```cpp
TEST_CASE("quat_to_mat4 rotates a point 90 degrees about Z") {
    const Quat q = {0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};
    const Mat4 m = quat_to_mat4(q);
    // column-major: transform (0,1,0) by hand -> (-1,0,0)
    const float x = m.m[4] * 1.0f;   // m[col=1][row=0]
    const float y = m.m[5] * 1.0f;
    CHECK(x == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(y == doctest::Approx(0.0f).epsilon(1e-4));
}
TEST_CASE("quat_slerp halfway between identity and 90 degrees Z is 45 degrees") {
    const Quat a = {0, 0, 0, 1};
    const Quat b = {0, 0, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};
    const Quat h = quat_slerp(a, b, 0.5f);
    CHECK(h.z == doctest::Approx(std::sin(kPi / 8.0f)).epsilon(1e-4));
    CHECK(h.w == doctest::Approx(std::cos(kPi / 8.0f)).epsilon(1e-4));
}
TEST_CASE("mat4_from_trs round-trips through decomposition values") { /* T(1,2,3) R=90Z S=(2,2,2): check m[12..14]==1,2,3 and m[0]==0, m[1]==2, m[4]==-2, m[5]==0 */ }
```

In `app/tests/test_asset_io.cpp` (extend; `test_library()` exists from S1):

```cpp
TEST_CASE("wander carries its 348-joint skeleton") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    CHECK(scene.skin.joints.size() == 348);
    CHECK(scene.skin.joints[0].name == "Bone_root");
    CHECK(scene.animations.empty());
    const auto& v = scene.meshes[0].vertices[0];
    CHECK(v.j[0] == 102); CHECK(v.j[1] == 73); CHECK(v.j[2] == 0); CHECK(v.j[3] == 0);
    CHECK(v.w[0] == doctest::Approx(0.9094f).epsilon(1e-3));
    CHECK(v.w[1] == doctest::Approx(0.0906f).epsilon(1e-3));
}
TEST_CASE("a two-joint fixture parses skin and animation") { /* see fixture below */ }
TEST_CASE("a primitive without JOINTS_0 defaults to joint 0, weight 1") { /* fixture variant */ }
TEST_CASE("a channel targeting a non-joint node is skipped") { /* fixture variant */ }
TEST_CASE("a CUBICSPLINE sampler is refused loudly") {
    // fixture variant with interpolation CUBICSPLINE
    CHECK_THROWS_WITH_AS(load_scene(dir, "cubic.gltf"), doctest::Contains("CUBICSPLINE"), std::runtime_error);
}
```

The fixture: an inline `.gltf` string written to a temp dir (S1's pattern) with a base64 data-URI buffer. Structure: nodes `Root`(children `[1]`) → `JointA`(translation `[0,1,0]`, children `[2]`) → `JointB`(translation `[0,1,0]`); `skin.joints = [1, 2]`; `inverseBindMatrices` = inverse bind world transforms (`JointA`: translate(0,-1,0); `JointB`: translate(0,-2,0)); one mesh primitive (3 vertices, POSITION + JOINTS_0 `(1,0,0,0)` + WEIGHTS_0 `(1,0,0,0)`); one animation `"turn"`: channel target node 1, path `rotation`, LINEAR, input `[0, 1]`, output quats `[(0,0,0,1), (0,0,sin45°,cos45°)]` (90° about Z). Generate the base64 buffer with a one-liner and paste it as the fixture literal (document the command in a test comment).

Fixture assertions: `skin.joints.size() == 2`; `animations.size() == 1`; `animations[0].duration == doctest::Approx(1.0f)`; `channels.size() == 1`; `channels[0].joint == 0`; `times == {0,1}`; `values.size() == 8`; `interpolation == 0`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build -j && SOTC_LIB=out/library ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL — `quat_to_mat4` not defined; `scene.skin` not a member.

- [ ] **Step 3: Implement**

- `math3d.h`: Quat ops as declared; `quat_to_mat4` column-major; `mat4_from_trs` = T × R × S.
- `scene.h`: the new types + `Vertex` gains `uint16_t j[4]; float w[4];` (update the `static_assert` to 56 and the layout comment: offsets 0/12/24/32/40).
- `asset_io.cpp`:
  - Skin: parse the first `cgltf_skin`; `joints[i]` ↔ node index; `parent` from the node children map; `inverseBind` from the accessor; rest local TRS decomposed from the node's `matrix` (nodes carry matrices — decompose: `T` = column 3, `S` = column lengths, `R` = normalized columns → quat; handle zero scale by `S = 1`).
  - Vertex skin data: JOINTS_0 (u16 or u8 — normalize to u16) and WEIGHTS_0 (float); **absent attributes default to `j = {0,0,0,0}`, `w = {1,0,0,0}`** (Review Focus 1/2).
  - Animations: for each `cgltf_animation`, channels whose target node is a skin joint become `AnimationChannelCpu` (path T/R/S; interpolation LINEAR→0, STEP→1); **non-joint targets are skipped** (Review Focus 3); **CUBICSPLINE throws `std::runtime_error` naming the animation and channel** (Review Focus 4); `duration` = max input time (0 when empty).
- `CMakeLists.txt`: `test_anim.cpp` joins `sotc-app-tests`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build app/build -j && SOTC_LIB=out/library ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS (all cases).

- [ ] **Step 5: Commit**

```bash
git add app/src/math3d.h app/src/scene.h app/src/asset_io.cpp app/tests app/CMakeLists.txt
git commit -m "feat(app): parse glTF skins and animation channels; quaternion math"
```

---

### Task A2: Pose evaluation + procedural clip

**Files:**
- Create: `app/src/anim.h`, `app/src/anim.cpp`
- Test: `app/tests/test_anim.cpp` (extend)
- Modify: `app/CMakeLists.txt` (add `anim.cpp` to `sotc-viewer` and `sotc-app-tests`)

**Interfaces:**
- Consumes: `Scene`, `SkinCpu`, `AnimationCpu` (A1); `Mat4`/`Quat` (A1).
- Produces:

```cpp
// anim.h
float animation_loop_time(float time, float duration);  // wraps into [0, duration); duration <= 0 -> 0
void animation_sample(const AnimationChannelCpu& ch, float t, float* out);  // clamped ends; LINEAR = lerp/nlerp, STEP = hold
std::vector<Mat4> animation_pose(const Scene& scene, const AnimationCpu& clip, float time);
    // one matrix per skin joint: worldJoint * inverseBind (the skin palette);
    // channels override T/R/S per joint; missing components use restT/restR/restS
AnimationCpu make_procedural_wave(const Scene& scene);
    // 2 s loop; channels for the first min(12, joints) joints; path rotation;
    // keys at t = 0/1/2 s: restR, restR * 15deg-about-X, restR; LINEAR. Deterministic.
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("sampling LINEAR at the midpoint interpolates rotations") { /* fixture "turn" channel at t=0.5 -> 45 degrees about Z (check via quat components) */ }
TEST_CASE("STEP holds until the next key") { /* same channel as STEP: t=0.99 == key 0 value */ }
TEST_CASE("sampling clamps outside the key range") { /* t=-1 == key 0; t=99 == last key */ }
TEST_CASE("loop time wraps and handles zero duration") {
    CHECK(animation_loop_time(1.5f, 1.0f) == doctest::Approx(0.5f));
    CHECK(animation_loop_time(3.0f, 2.0f) == doctest::Approx(1.0f));
    CHECK(animation_loop_time(3.0f, 0.0f) == doctest::Approx(0.0f));
}
TEST_CASE("the fixture pose at t=0 is the bind pose") { /* palette[0], palette[1] both approx identity (1e-4) */ }
TEST_CASE("the fixture pose at t=1 rotates the bound vertex around JointA") {
    // palette[0] maps (0,2,0) to (-1,1,0) within 1e-4 (JointA is at (0,1,0), rotated 90 deg about Z)
}
TEST_CASE("wander's rest palette is identity for all 348 joints") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    AnimationCpu empty;  // no channels
    for (const Mat4& m : animation_pose(scene, empty, 0.0f))
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-3));
}
TEST_CASE("the procedural wave starts at rest and moves later") {
    // pose(wave, 0.0) approx identity for all joints; pose(wave, 0.5) differs (> 1e-3) for at least one joint
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build -j && SOTC_LIB=out/library ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL — `animation_pose` not defined.

- [ ] **Step 3: Implement `anim.cpp`**

- Sampling: binary search (or linear scan) for the key pair; LINEAR lerps (rotations via `quat_slerp`, vectors via lerp), STEP holds the left key; clamp outside.
- Pose: local TRS per joint (rest, overridden by each channel's sampled value) → `mat4_from_trs` → world = parent world × local (joints are topologically ordered: parents precede children in the joint list; if not, resolve with a visited/parent-first pass) → palette = world × inverseBind.
- `make_procedural_wave`: as declared; 15° about X composed onto each joint's rest rotation, sinusoidal shape approximated by the 3 LINEAR keys.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build app/build -j && SOTC_LIB=out/library ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/src/anim.h app/src/anim.cpp app/tests/test_anim.cpp app/CMakeLists.txt
git commit -m "feat(app): pose evaluation and procedural clip"
```

---

### Task A3: GPU skinning + playback wiring

**Files:**
- Modify: `app/shaders/mesh.metal`, `app/src/renderer.h`, `app/src/renderer.cpp`, `app/src/app.cpp`, `app/src/main.cpp`, `app/README.md`

**Interfaces:**
- Consumes: `animation_pose` (A2), `Scene::skin` (A1).
- Produces:
  - `void Renderer::upload_pose(const std::vector<Mat4>& palette);` — sized from `scene.skin.joints` at `upload()`; per-frame copy-pass upload (`SDL_UploadToGPUBuffer`), bound with `SDL_BindGPUVertexStorageBuffers`; no-op when the scene has no skin.
  - App flags: `--anim <name|index>` (default: `procedural-wave` when the model has a skin), `--anim-time <seconds>` (pauses at that time — deterministic captures).
  - Status line (stdout, at startup and on clip change): `s2-anim clip=<name> joints=<N> procedural=<0|1> t=<seconds>`.
  - Keys: `Space` play/pause, `[`/`]` previous/next clip, `R` restart.

- [ ] **Step 1: Shader** — `mesh.metal`: `VertexIn` gains `ushort4 joints [[attribute(3)]]` and `float4 weights [[attribute(4)]]`; `mesh_vertex` gains `device const float4x4* palette [[buffer(2)]]` (uniform buffers occupy 0/1; storage buffers follow — SDL_GPU convention) and skins:

```metal
float4 p = in.weights.x * (palette[in.joints.x] * float4(in.pos, 1.0))
         + in.weights.y * (palette[in.joints.y] * float4(in.pos, 1.0))
         + in.weights.z * (palette[in.joints.z] * float4(in.pos, 1.0))
         + in.weights.w * (palette[in.joints.w] * float4(in.pos, 1.0));
// same blend with float4(in.nrm, 0.0) for the normal
```

- [ ] **Step 2: Renderer** — vertex layout pitch 56 with five elements (FLOAT3@0, FLOAT3@12, FLOAT2@24, USHORT4@32, FLOAT4@40); palette buffer created in `upload()` with `SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ` (size = joints × 64, minimum 64) and bound per frame in the vertex stage; `upload_pose` writes it via a copy pass (the S1 texture-upload pattern) and the render pass binds it before the draws.

- [ ] **Step 3: App wiring** — build the clip list (`procedural-wave` when a skin exists, then `scene.animations`); playback state (index, time, playing); per frame: `time += dt` when playing → `animation_pose` → `upload_pose`; keys as declared; status line as declared; `--anim-time` sets a fixed paused time.

- [ ] **Step 4: Verify** — build + suite + deterministic smoke:

```bash
cmake --build app/build -j
SOTC_LIB=out/library ctest --test-dir app/build --output-on-failure
app/build/sotc-viewer --library out/library --model models/shadow/characters/wander/_cmn/wander.glb \
  --anim procedural-wave --anim-time 0 --screenshot out/s2-wave-t0.png
app/build/sotc-viewer --library out/library --model models/shadow/characters/wander/_cmn/wander.glb \
  --anim procedural-wave --anim-time 0.5 --screenshot out/s2-wave-t05.png
```

Expected: exit 0 everywhere; both PNGs exist and are non-uniform (the s1 checker's span rule); `cmp -s out/s2-wave-t0.png out/s2-wave-t05.png` reports **different** (the wave moved); `out/s2-wave-t0.png` looks like S1's bind-pose Wander (no collapse); the run prints `s2-anim clip=procedural-wave joints=348 procedural=1`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(app): GPU skinning, joint palette, animation playback"
```
