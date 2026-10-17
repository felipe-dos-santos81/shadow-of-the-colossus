# S1 — "See it (SDL)" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A windowed Apple-Silicon SDL3 application that loads the converted asset library and renders Wander (plus a prop) textured, with a free camera, an fps readout, and the mouse cursor never captured.

**Architecture:** A new C++20/CMake project at `app/` in this repo. It consumes only converted assets (`.glb` + `.png` from `out/library`) — it never parses original game containers. Rendering goes through SDL_GPU on Metal: one graphics pipeline, MSL shaders compiled to a metallib at build time, an offscreen color target copied to the swapchain (so the same path yields screenshots), per-draw uniforms pushed via `SDL_PushGPU*UniformData`.

**Tech Stack:** C++20 · CMake ≥3.25 · SDL3 (Homebrew, 3.4.x) · SDL_GPU/Metal · vendored `cgltf`, `stb_image`, `stb_image_write`, `doctest`.

**Spec:** `docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md` (milestone S1; decisions D-S2, D-S3, D-S4, D-S5, D-S8).

## Global Constraints

- Target macOS/Apple Silicon only; C++20; generator **Unix Makefiles** (`cmake -S app -B app/build`).
- SDL3 ≥ 3.4 via `find_package(SDL3 CONFIG REQUIRED)`; Metal shaders must be **metallib** (`SDL_GPU_SHADERFORMAT_METALLIB`); build with `xcrun -sdk macosx metal` + `metallib`.
- **Windowed always; the cursor is never captured**: set `SDL_HINT_MOUSE_AUTO_CAPTURE="0"` before `SDL_Init`, never enable `SDL_SetWindowRelativeMouseMode`, never `SDL_CaptureMouse(true)`, never `SDL_SetWindowMouseGrab`.
- The app reads converted assets only — never the game tree or original containers.
- Personal use: game assets and derived outputs (`out/`) are never committed; `out` and `app/build` are gitignored.
- App tests need `SOTC_LIB` (the converted library). Fail fast with a clear message when unset — never skip silently (repo convention).
- Tests use `doctest` (vendored); run everything with `ctest --test-dir app/build --output-on-failure`.
- Commits are small and typed (`feat:`, `fix:`, `test:`, `chore:`, `docs:`).

## Review Focus

Failure modes the spec implies but the S1 acceptance does not exercise; each gets a test in the task noted.

1. A texture the converter refused (BC6H/HDR) is simply absent — the loader must degrade to factor-only material, not crash. → Task 2.
2. Primitives lacking NORMAL or TEXCOORD_0 (stub-ish content) — the loader must tolerate and default them. → Task 2.
3. Texture URIs are library-relative with `/` separators — every texture referenced by `wander.glb` must resolve on disk. → Task 2.
4. Window resize while running: the swapchain is acquired at the current size each frame; no stale-size crash, correct aspect. → Task 4 (note) + Task 5 (observed).
5. GPU/window creation failure (no Metal device): exit code 1 with the `SDL_GetError()` message — no crash, no silent black window. → Task 1.

---

### Task 1: App skeleton — windowed shell, cursor-free guarantees, selftest

**Files:**
- Create: `app/CMakeLists.txt`, `app/README.md`, `app/third_party/VERSIONS.md`
- Create: `app/src/main.cpp`, `app/src/app.h`, `app/src/app.cpp`
- Download: `app/third_party/{cgltf.h, stb_image.h, stb_image_write.h, doctest.h}`
- Modify: `.gitignore` (add `app/build/`)

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `sotc-viewer` executable; `struct AppOptions { std::string libraryDir, modelRelPath, screenshotPath; int frames = 3; bool selftest = false; int width = 1280, height = 720; }`; `int run_app(const AppOptions&)` (0 ok, 1 runtime failure, 2 usage); status line `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=<n>` printed by `--selftest`.

- [ ] **Step 1: Download and pin the vendored headers**

```bash
curl -L -o app/third_party/cgltf.h https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/cgltf.h
curl -L -o app/third_party/stb_image.h https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
curl -L -o app/third_party/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image_write.h
curl -L -o app/third_party/doctest.h https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
shasum -a 256 app/third_party/*.h | tee app/third_party/VERSIONS.md
```

Record the source URLs (the four above) in `VERSIONS.md` above the hash list.

- [ ] **Step 2: Write `app/CMakeLists.txt`**

Key content (fill in what CMake requires; nothing else):

```cmake
cmake_minimum_required(VERSION 3.25)
project(sotc_viewer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 CONFIG REQUIRED)

add_executable(sotc-viewer
  src/main.cpp src/app.cpp)
target_link_libraries(sotc-viewer PRIVATE SDL3::SDL3)
target_include_directories(sotc-viewer PRIVATE third_party)

enable_testing()
add_executable(sotc-app-tests tests/test_main.cpp tests/test_camera.cpp tests/test_asset_io.cpp)
target_link_libraries(sotc-app-tests PRIVATE SDL3::SDL3)
target_include_directories(sotc-app-tests PRIVATE third_party src)
add_test(NAME unit_tests COMMAND sotc-app-tests)
add_test(NAME windowed_selftest COMMAND sotc-viewer --selftest)
set_tests_properties(windowed_selftest PROPERTIES
  PASS_REGULAR_EXPRESSION "s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0")
```

(A metallib custom command and the renderer sources are added by Tasks 3–4; create the test files as empty-compiling stubs in this task so configure succeeds.)

- [ ] **Step 3: Implement the shell in `app/src/app.cpp`**

- Before `SDL_Init`: `SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0")`.
- `SDL_Init(SDL_INIT_VIDEO)`; create the window with `SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY`; explicitly `SDL_SetWindowRelativeMouseMode(win, false)` and `SDL_SetWindowMouseGrab(win, false)`.
- `SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB, /*debug*/ false, NULL)`; on NULL print `SDL_GetError()` and return 1; `SDL_ClaimWindowForGPUDevice`; `SDL_SetGPUSwapchainParameters(..., SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC)`.
- Event loop: `SDL_PollEvent`; `SDL_EVENT_QUIT` and `SDL_EVENT_KEY_DOWN` with `SDLK_ESCAPE` quit. Acquire the swapchain every frame (this also keeps resize correct).
- `--selftest`: run 1 frame, print the status line, exit 0. The line reports **queried** state: window flags have no FULLSCREEN flag → `windowed=1`; `SDL_GetWindowRelativeMouseMode(win)` false → `relative_mouse=0`; `SDL_GetHint(SDL_HINT_MOUSE_AUTO_CAPTURE)` equals `"0"` → `mouse_auto_capture=0`.
- `main.cpp`: parse `--library`, `--model`, `--screenshot`, `--frames`, `--selftest`, `--width`, `--height`; unknown argument → usage on stderr, return 2. Library defaults to `$SOTC_LIB`; if neither is present, error.

- [ ] **Step 4: Verify the build and the selftest**

Run: `cmake -S app -B app/build && cmake --build app/build -j`
Expected: builds clean; then `ctest --test-dir app/build --output-on-failure` → `windowed_selftest` PASSes (a window appears briefly), `unit_tests` passes (empty stubs).

- [ ] **Step 5: Commit**

```bash
git add app/CMakeLists.txt app/README.md app/third_party app/src .gitignore
git commit -m "feat(app): SDL3 windowed shell with cursor-free guarantees + vendored pins"
```

---

### Task 2: Asset IO — load converted glbs and PNGs

**Files:**
- Create: `app/src/math3d.h`, `app/src/scene.h`, `app/src/asset_io.h`, `app/src/asset_io.cpp`
- Create: `app/tests/test_main.cpp`, `app/tests/test_asset_io.cpp`

**Interfaces:**
- Consumes: vendored `cgltf.h`, `stb_image.h` (Task 1).
- Produces:

```cpp
// math3d.h — column-major (m[col*4+row]), right-handed, Y-up
struct Vec3 { float x, y, z; };
struct Mat4 { float m[16]; };
float vec3_dot(Vec3, Vec3); Vec3 vec3_cross(Vec3, Vec3); Vec3 vec3_normalize(Vec3);

// scene.h
struct Vertex { float px, py, pz, nx, ny, nz, u, v; };            // 32 bytes
struct MeshCpu { std::vector<Vertex> vertices; std::vector<uint32_t> indices; int materialIndex; };
struct TextureCpu { int width, height; std::vector<uint8_t> rgba8; std::string sourceRelPath; };
struct MaterialCpu { std::string name; float baseColorFactor[4]; float emissiveFactor[3];
                     int baseColorTexture; int normalTexture; };   // -1 = none
struct Scene { std::vector<MeshCpu> meshes; std::vector<MaterialCpu> materials;
               std::vector<TextureCpu> textures; Vec3 boundsMin, boundsMax; };

// asset_io.h
Scene load_scene(const std::filesystem::path& libraryDir, const std::string& glbRelPath); // throws std::runtime_error
```

- [ ] **Step 1: Write the failing tests (`app/tests/test_asset_io.cpp`)**

Use `doctest` (`test_main.cpp` holds `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`). `SOTC_LIB` comes from the environment; a helper `test_library()` throws with "Set SOTC_LIB to the converted asset library." when unset.

```cpp
TEST_CASE("wander glb matches the converter catalog") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    CHECK(scene.meshes.size() == 12);
    long verts = 0; for (auto& m : scene.meshes) verts += (long)m.vertices.size();
    CHECK(verts == 62466);
    CHECK(scene.materials.size() == 12);
    float extentY = scene.boundsMax.y - scene.boundsMin.y;
    CHECK(extentY > 1.0f); CHECK(extentY < 1.5f);   // adapter notes: wander ≈ 1.26 tall in Y
}
TEST_CASE("every texture referenced by wander resolves on disk") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    for (auto& t : scene.textures)
        CHECK(std::filesystem::exists(test_library() / t.sourceRelPath));
}
TEST_CASE("plank converts to the pinned counts") {
    auto scene = load_scene(test_library(), "models/objects/props/_cmn/obj_plank_01.glb");
    CHECK(scene.meshes.size() == 1);
    CHECK(scene.meshes[0].vertices.size() == 980);
}
TEST_CASE("a missing base-color PNG degrades to factor-only") {
    // fixture: a minimal .gltf in a temp dir whose only image URI points at a missing file
    auto scene = load_scene(tmp_fixture_dir_with_missing_png(), "fixture.gltf");
    CHECK(scene.materials.size() == 1);
    CHECK(scene.materials[0].baseColorTexture == -1);  // no throw
}
```

Write the fixture `.gltf` inline in the test (JSON string → temp file): one triangle, one material with `baseColorTexture` pointing at `missing.png`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build -j && SOTC_LIB=out/library ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL — `load_scene` is not defined.

- [ ] **Step 3: Implement `load_scene` in `app/src/asset_io.cpp`**

- `cgltf_parse_file` + `cgltf_load_buffers` (data URIs are used by this library's glbs? check — the converter wrote external `.bin`? It wrote GLB single-file; buffers are embedded).
- Per mesh primitive: read POSITION (required), NORMAL (default `(0,1,0)` when absent per Vertex — wait, default to `(0,0,1)`? Use `(0,1,0)`, Y-up like the adapter's fallback), TEXCOORD_0 (default `(0,0)`), indices (uint32; when the primitive is non-indexed, generate 0..n-1).
- `materialIndex = primitive.material` index (keep as-is; materials parsed once).
- Materials: `pbrMetallicRoughness.baseColorFactor` → `baseColorFactor`; `emissiveFactor` → `emissiveFactor`; base-color texture index resolved to a loaded texture (or -1 when missing/undecodable — decode via `stb_image`, 4 channels RGBA8).
- Texture URIs: relative to the `.glb`'s directory with `/` separators; resolve against `libraryDir`, open the file → `TextureCpu.sourceRelPath` is the library-relative path; skip + record message on missing file (no throw).
- Bounds: min/max over all positions; throw `std::runtime_error` with the cgltf result string on parse failure.

- [ ] **Step 4: Run tests to verify they pass**

Run: `SOTC_LIB=out/library ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS (5 test cases).

- [ ] **Step 5: Commit**

```bash
git add app/src app/tests
git commit -m "feat(app): glb/png asset loading with catalog-pinned tests"
```

---

### Task 3: Renderer — pipeline, metallib shader, offscreen target, screenshot path

**Files:**
- Create: `app/shaders/mesh.metal`, `app/src/renderer.h`, `app/src/renderer.cpp`
- Modify: `app/CMakeLists.txt` (metallib custom command + renderer sources)
- Modify: `app/src/app.cpp` (render loop calls the renderer; `--screenshot` renders `--frames` frames then writes the PNG and exits)

**Interfaces:**
- Consumes: `Scene` (Task 2).
- Produces:

```cpp
class Renderer {
public:
    Renderer(SDL_GPUDevice* device, SDL_Window* window);   // throws std::runtime_error on setup failure
    void upload(const Scene& scene);                        // buffers + textures on the GPU
    void render_frame(const Scene& scene, const Mat4& viewProj, bool acquire_swapchain);
                                                            // renders into the offscreen target; when
                                                            // acquire_swapchain, also copies it to the
                                                            // swapchain and presents in the same submit
    std::vector<uint8_t> read_pixels();                     // RGBA8 rows, top-down (offscreen target)
};
```

- [ ] **Step 1: Write the shader `app/shaders/mesh.metal`**

SDL_GPU MSL conventions (from the SDL3 wiki, verified): vertex input via `[[stage_in]]`; uniform slots → `[[buffer(n)]]` starting at 0 in push order; sampled textures → `[[texture(n)]]`; samplers → `[[sampler(n)]]`. Layout:

```metal
#include <metal_stdlib>
using namespace metal;
struct VertexIn { float3 pos [[attribute(0)]]; float3 nrm [[attribute(1)]]; float2 uv [[attribute(2)]]; };
struct Uniforms { float4x4 viewProj; };          // vertex slot 0
struct PerDraw  { float4x4 model; };             // vertex slot 1
struct Material { float4 baseColor; float4 emissive; };  // fragment slot 0 (std140: vec4-aligned)
// mesh_vertex(VertexIn [[stage_in]], constant Uniforms& u [[buffer(0)]], constant PerDraw& d [[buffer(1)]])
// mesh_fragment(VSOut [[stage_in]], constant Material& m [[buffer(0)]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]])
```

Fragment: sample albedo → `pow(rgb, 2.2)`; simple key light (`dot(n, L)` with L = normalize(float3(0.4, 0.8, 0.6))), + 0.1 ambient; output `pow(color * baseColor.rgb, 1.0/2.2)` with alpha 1. **Note:** if the swapchain format turns out to be an `_SRGB` variant, drop the shader gamma (single-line change; caught by the screenshot check).

- [ ] **Step 2: Add the metallib build to `app/CMakeLists.txt`**

```cmake
set(METAL_OUT ${CMAKE_BINARY_DIR}/shaders/mesh.metallib)
add_custom_command(OUTPUT ${METAL_OUT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
  COMMAND xcrun -sdk macosx metal -c ${CMAKE_SOURCE_DIR}/shaders/mesh.metal -o ${CMAKE_BINARY_DIR}/shaders/mesh.air
  COMMAND xcrun -sdk macosx metallib ${CMAKE_BINARY_DIR}/shaders/mesh.air -o ${METAL_OUT}
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/mesh.metal)
add_custom_target(shaders DEPENDS ${METAL_OUT})
add_dependencies(sotc-viewer shaders)
add_custom_command(TARGET sotc-viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${METAL_OUT} $<TARGET_FILE_DIR:sotc-viewer>/mesh.metallib)
```

Runtime locates the metallib via `SDL_GetBasePath()` + `mesh.metallib`.

- [ ] **Step 3: Implement `Renderer`**

- Pipeline: vertex buffer slot 0 (pitch 32) with three `FLOAT3/FLOAT3/FLOAT2` attributes at offsets 0/12/24; 32-bit index buffer; depth target `D32_FLOAT` (LESS_EQUAL, write); color target format = `SDL_GetGPUSwapchainTextureFormat(device, window)` — the offscreen texture uses the same format (`COLOR_TARGET | SAMPLER` usage), so the swapchain copy is format-exact.
- Per frame: command buffer → offscreen render pass (clear + draws; per mesh: push `PerDraw` at vertex slot 1, `Material` at fragment slot 0; slot 0 holds `viewProj`, pushed once) → copy pass copies offscreen → swapchain (`SDL_CopyGPUTextureToTexture`) when `acquire_swapchain`.
- Uploads (one-time, in `upload`): vertex/index buffers; textures (`SDL_CreateGPUTexture` RGBA8 + `SDL_UploadToGPUTexture` from transfer buffers); one linear sampler (`SDL_CreateGPUSampler`); wait with `SDL_SubmitGPUCommandBufferAndAcquireFence` + `SDL_WaitForGPUFences`.
- `read_pixels`: `SDL_DownloadFromGPUTexture` (transfer buffer usage DOWNLOAD) → fence → map/copy/unmap; if the swapchain format is `B8G8R8A8_UNORM`, swap R/B.
- `--screenshot`: render `frames` frames, `read_pixels`, write PNG via `stb_image_write`, exit 0.

- [ ] **Step 4: Verify — screenshot smoke**

Run (needs the library; `SOTC_LIB` or `--library`):

```bash
app/build/sotc-viewer --library out/library --model models/objects/props/_cmn/obj_plank_01.glb \
  --screenshot out/s1-smoke.png --frames 2
```

Expected: exit 0; `out/s1-smoke.png` exists (> 5000 bytes); opening it shows the textured plank — not a black frame.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(app): SDL_GPU renderer, MSL shader, offscreen target + screenshot"
```

---

### Task 4: Camera + input + fps readout

**Files:**
- Create: `app/src/camera.h`, `app/src/camera.cpp`, `app/tests/test_camera.cpp`
- Modify: `app/src/app.cpp` (input handling, per-frame view matrix, window-title fps)

**Interfaces:**
- Consumes: `Vec3`/`Mat4` (Task 2).
- Produces:

```cpp
struct Camera { Vec3 target; float distance, yaw, pitch; float fovYDeg = 60.0f; };
Mat4 camera_view(const Camera&);
Mat4 camera_proj(const Camera&, float aspect);
void camera_orbit(Camera&, float dxPixels, float dyPixels);
void camera_zoom(Camera&, float wheelDelta);
void camera_frame(Camera&, Vec3 boundsMin, Vec3 boundsMax);   // fit bounds, pitch ~20°
```

- [ ] **Step 1: Write the failing camera tests (`app/tests/test_camera.cpp`)**

```cpp
TEST_CASE("look-at with eye at +Z maps the origin to -Z") { /* mat4_look_at((0,0,5), origin, +Y): m[14] == -5 */ }
TEST_CASE("camera_frame fits a unit cube") {
    Camera c{}; camera_frame(c, {-1,-1,-1}, {1,1,1});
    CHECK(c.target.x == 0.0f); CHECK(c.target.y == 0.0f); CHECK(c.target.z == 0.0f);
    CHECK(c.distance > 2.5f); CHECK(c.distance < 3.5f);          // sqrt(3)/tan(30°) ≈ 3.0
}
TEST_CASE("zoom clamps") { /* camera_zoom below floor / above ceiling stays in [0.5, 500] */ }
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build -j && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL — `camera_view`/`camera_frame` not defined.

- [ ] **Step 3: Implement camera math and wire the input**

- Orbit: left-drag → `camera_orbit` (yaw/pitch, pitch clamped to ±89°); wheel → `camera_zoom`; `camera_frame` on load and on `F`.
- Input must not require capture: use normal `SDL_EVENT_MOUSE_MOTION` with `SDL_BUTTON_LMASK` held; **no** relative mode, **no** `SDL_CaptureMouse`.
- Fps: rolling average over the last second; `SDL_SetWindowTitle(win, ...)` updated twice a second. (A drawable on-screen text HUD arrives with SDL_ttf when needed — the title readout is the S1 HUD.)

- [ ] **Step 4: Run tests + interactive check**

Run: `ctest --test-dir app/build -R unit_tests --output-on-failure` → PASS.
Interactive: run the app with wander; drag to orbit, wheel to zoom, resize the window (Review Focus #4: no crash, aspect stays correct).

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(app): orbit camera, uncaptured input, fps title"
```

---

### Task 5: Acceptance script + run + milestone notes

**Files:**
- Create: `scripts/s1-acceptance.sh`
- Create: `docs/notes/s1-results.md`

**Interfaces:**
- Consumes: everything above.
- Produces: re-runnable S1 acceptance; `out/s1/wander.png`, `out/s1/plank.png`; recorded results.

- [ ] **Step 1: Write `scripts/s1-acceptance.sh`**

Follow the `m0-acceptance.sh` conventions (bash 3.2-safe, `set -euo pipefail`, `SOTC_LIB` required-with-default `<repo>/out/library`, outputs under `out/`). Steps:

1. `cmake -S app -B app/build && cmake --build app/build -j`
2. `export SOTC_LIB`; `ctest --test-dir app/build --output-on-failure`
3. Selftest: capture output, `grep -q 's1-status windowed=1 relative_mouse=0 mouse_auto_capture=0'` or fail.
4. Screenshots: for `wander.glb` and `obj_plank_01.glb` → `app/build/sotc-viewer --library "$SOTC_LIB" --model <rel> --screenshot "out/s1/<name>.png" --frames 3`; assert each PNG has the `\x89PNG` magic and size > 5000 B.
5. Print a summary block (paths, byte sizes, the status line).

- [ ] **Step 2: Run it and record the results**

Run: `./scripts/s1-acceptance.sh`
Expected: exit 0. Then write `docs/notes/s1-results.md` with: date/commit, machine, the verbatim status line, screenshot paths + byte sizes (from `out/s1/`), measured fps (title readout at 1280×720 with wander, vsync), and the manual observations: **click does not capture the cursor; ⌘-Tab works; window opens windowed**. Mark anything not directly observed as "≈".

- [ ] **Step 3: Commit**

```bash
git add scripts/s1-acceptance.sh docs/notes/s1-results.md
git commit -m "docs: S1 acceptance script + results (windowed SDL viewer)"
```

---

## Self-review notes (author)

- Spec coverage: S1 milestone line ("app/ builds an arm64 binary; window opens windowed; cursor visible and free; Wander + a prop textured; fps HUD; screenshot + notes") maps to Tasks 1/3/4/5. D-S2/D-S3/D-S5/D-S8 pinned in Tasks 1/3; D-S4 (converted-assets-only) enforced throughout.
- Type consistency: `Scene`/`MeshCpu`/`MaterialCpu`/`Camera`/`Renderer` signatures are used identically across Tasks 2–5.
- Review Focus lines each carry their test (Task 2 ×3, Task 5 ×2 incl. the Task 4 interactive note).
