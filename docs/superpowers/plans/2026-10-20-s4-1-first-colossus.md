# S4.1 "The first colossus" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the S4.0 encounter playable end to end — grab Valus's fur, climb him under stamina, reach a data-derived weakpoint, strike it, survive the shake-off, and bring him down.

**Architecture:** A new `PlayerCombat` state machine (`Grounded → Grabbing → Climbing → Striking → Falling → Staggered`) drives bone-anchored climb bands, with the attach point recomputed from the entity's posed palette every frame. The converter bakes Wander's real grip/attack/damage clips and Valus's damage/death reactions and emits a clip record; the app adds a climb camera, a small SDL_ttf HUD, and a deterministic `--s41-selftest`. The S3/S4 scripted paths are not modified.

**Tech Stack:** C++20 · SDL3 + SDL_GPU/Metal · SDL3_ttf (Homebrew 3.2.2, CMake config present) · .NET 10 converter · doctest · bash 3.2

**Spec:** `docs/superpowers/specs/2026-10-20-s4-1-first-colossus-design.md`

## Global Constraints

- **SDL3 + SDL_GPU/Metal**, C++20, Apple Silicon; **windowed**; **the cursor is never captured**. `SDL_HINT_MOUSE_AUTO_CAPTURE=0`, no relative mode, no grab; `windowed_selftest` stays green.
- Offline-first: the app consumes converted glbs, manifests, and the converter's clip record; no original containers at runtime.
- **Static-only RE**. Personal use, no redistribution; assets and derived glbs stay uncommitted.
- Tests fail fast without `SOTC_GAME_ROOT` (converter) or `SOTC_LIB` (library-gated app tests); bash 3.2 scripts; typed commits.
- S3's and S4.0's manifests, scripts, and user walks stay valid and green.
- Spec values: grab range **0.9 m**; climb speed **1.2 m/s**; stamina pool **100**, drains **2.5 / 4.0 / 8.0 per s**, regen **25/s** after **1 s**; strike charge **0.9 s**, hit window **0.25–0.45** of the swing, catch window **0.6 s** / **2.5 m**; Valus HP **100**, damage **20 / 34**; climb camera **5–8 m**, pitch **±45°**.
- Keys: `E` grab (held), `J` strike (held to charge). Existing keys stay.
- Gamepad: left stick move, right stick camera, RT grip, X strike, Start pause; hot-plug.

## Review Focus

1. **Frame-time spike while attached** — a 0.25 s hitch must not teleport the climber past a band or into the mesh. `PlayerFrame`-driven attach is absolute; `dt` is clamped to 0.05 s in the state machine.
2. **Grab at a band seam or band end** — `nearest_climb_band` must return a band or `-1`, never an invalid index; crossing a band end must enter the adjacent band or hold, never freeze.
3. **Stamina exactly zero / regen boundary** — the mode must not flicker between `Grabbing` and `Falling` frame to frame.
4. **Old library without the new clips** — a missing named clip must fail the load loudly, never silently play the first clip.
5. **Gamepad hot-unplug mid-grip** — the grip input must not latch on; the player must fall/release cleanly.

---
### Task 1: Converter — bake the encounter clips and emit a clip record

**Files:**
- Create: `src/BpConvert.Core/Pipeline/ClipManifest.cs`
- Modify: `src/BpConvert.Core/Pipeline/ModelConverter.cs` (call the writer after the glb is written)
- Test: `tests/BpConvert.Tests/ClipManifestTests.cs`
- Artifact (gitignored): re-converted `out/library/.../wander.glb`, `.../c01_minotaur_a.glb`, and their `.clips.json`

**Interfaces:**
- Consumes: `IReadOnlyList<AnimClip>` from `ModelConverter.AttachClips`; `AnimClip.Name`, `.Frames`, `.Fps`, `.Tracks`, `.Root`.
- Produces: `public static class ClipManifest { public static string Write(string glbPath, string sourceRelPath, IReadOnlyList<AnimClip> clips); }` — writes `<stem>.clips.json` beside the glb and returns its path.

- [ ] **Step 1: Write the failing test**

```csharp
[Fact]
public void Write_emits_one_entry_per_clip()
{
    var dir = Directory.CreateTempSubdirectory("bp-clips-");
    var glb = Path.Combine(dir.FullName, "wander.glb");
    var clips = new[]
    {
        new AnimClip { Name = "player_a_stand", Frames = 76, Rate = 0.5, Tracks = [], Root = [] },
        new AnimClip { Name = "player_a_atk1",  Frames = 12, Rate = 1.0, Tracks = [], Root = [] },
    };

    var path = ClipManifest.Write(glb, "models/shadow/characters/wander/_cmn/wander.cmsh", clips);

    Assert.Equal(Path.Combine(dir.FullName, "wander.clips.json"), path);
    using var doc = JsonDocument.Parse(File.ReadAllText(path));
    var root = doc.RootElement;
    Assert.Equal(1, root.GetProperty("version").GetInt32());
    Assert.Equal("models/shadow/characters/wander/_cmn/wander.cmsh", root.GetProperty("source").GetString());
    var list = root.GetProperty("clips");
    Assert.Equal(2, list.GetArrayLength());
    Assert.Equal("player_a_stand", list[0].GetProperty("name").GetString());
    Assert.Equal(76, list[0].GetProperty("frames").GetInt32());
    Assert.Equal(15.0, list[0].GetProperty("fps").GetDouble(), 3);
    Assert.Equal("player_a_atk1", list[1].GetProperty("name").GetString());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"`
Expected: FAIL — `ClipManifest` does not exist.

- [ ] **Step 3: Implement `ClipManifest.Write` in `src/BpConvert.Core/Pipeline/ClipManifest.cs`**

Use `System.Text.Json` with `WriteIndented = true`; `clips` entries carry `name`, `frames`, `fps` (`clip.Fps`), `tracks` (`clip.Tracks.Count`), `root` (`clip.Root.Count > 0`). The path is `Path.ChangeExtension(glbPath, ".clips.json")`. Then call it from `ModelConverter.Convert` immediately after the glb is written, passing the same `sourceRelative` spelling.

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"`
Expected: PASS.

- [ ] **Step 5: Re-convert both models with the full clip set**

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/wander/_cmn/wander.cmsh \
  --clips player_a_stand,player_a_walk,player_a_run,player_a_grip_stand_crouch,player_a_grip_loop,player_a_grip_move_f1,player_a_grip_move_f2,player_a_grip_float_u_hi_attack,player_a_atk1,player_a_damage_m_fly2,player_a_fall_damage_to_death --force
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh \
  --clips minotaur_a_stand,minotaur_a_add_head_damage,minotaur_a_add_ude_r_damage,minotaur_a_add_mune_r_damage,minotaur_a_death_front --force
```

Expected: two `ok` lines; the glbs grow; both `.clips.json` exist. Record the two sha256 sums for the results note.

- [ ] **Step 6: Update the clip-count pin**

`app/tests/test_asset_io.cpp:399-402` currently asserts exactly 3 clips on the real Wander glb. Change the expectation to 11 and add assertions that `clip_index_named` finds `player_a_grip_loop` and `player_a_atk1`.

- [ ] **Step 7: Run the converter and app suites**

Run: `dotnet test tests/BpConvert.Tests -v q` and `cmake --build app/build && ctest --test-dir app/build --output-on-failure`
Expected: both green (library-gated tests included).

- [ ] **Step 8: Bounded eboot stat pass (timebox 0.5 agent-day)**

Decompile the stat registration block at `out/re/eboot.elf` vaddr `0x551bb1–0x551c56` and the
EasyMode block at `0x7edf3d–0x7ee237`, using the Ghidra project or `objdump`. A confirmed base
HP/stamina/grip value replaces the spec's builder-tuned numbers and is recorded as `decoded`;
otherwise record the negative in the results note and keep the builder-tuned values. Do not treat
the 100/570/700 debug-menu bounds as gameplay values.

- [ ] **Step 9: Commit**

```bash
git add src/BpConvert.Core/Pipeline/ClipManifest.cs src/BpConvert.Core/Pipeline/ModelConverter.cs tests/BpConvert.Tests/ClipManifestTests.cs app/tests/test_asset_io.cpp
git commit -m "feat: bake S4.1 climb/fight clips and emit a clip record"
```

---
### Task 2: Grab solver and capsule identity

**Files:**
- Create: `app/src/grab.h`, `app/src/grab.cpp`
- Modify: `app/src/collision.h` (add identity fields), `app/src/collision.cpp` (fill them)
- Test: `app/tests/test_grab.cpp`
- Modify: `app/CMakeLists.txt` (both targets)

**Interfaces:**
- Consumes: `Vec3`, `Mat4`, `kPlayerCapsuleRadius`.
- Produces:
```cpp
enum class ClimbBandId { LeftFoot, LeftShin, LeftThigh, Waist, Chest, Neck, Head, RightShoulder, RightArm, Count };
struct ClimbBand {
    const char* boneA;
    const char* boneB;
    float radius;
    ClimbBandId up;    // -1-equivalent: Count
    ClimbBandId down;
    int weakpoint;     // -1, 0 = forehead, 1 = right arm
};
const ClimbBand& climb_band(ClimbBandId);
using JointLookup = std::function<bool(std::string_view, Vec3&)>;
bool climb_band_segment(const ClimbBand&, const JointLookup&, Vec3& a, Vec3& b);
bool climb_surface_point(const ClimbBand&, const JointLookup&, float t, Vec3 hint,
                         Vec3& point, Vec3& normal);
ClimbBandId nearest_climb_band(const JointLookup&, Vec3 playerPos, float maxSurfaceDistance);
```
- `Capsule` gains `int rig = -1; int segment = -1;` as trailing fields so existing aggregate initialisers still compile; `entity_collision_capsules` fills them (rigIndex, index in the 8-entry table).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("climb bands form a connected chain") {
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const ClimbBand& b = climb_band(static_cast<ClimbBandId>(i));
        if (b.up != ClimbBandId::Count) REQUIRE(climb_band(b.up).down == static_cast<ClimbBandId>(i));
    }
}

TEST_CASE("climb surface point sits outside the band radius") {
    JointLookup lookup = [](std::string_view name, Vec3& out) {
        if (name == "A") { out = Vec3{0, 0, 0}; return true; }
        if (name == "B") { out = Vec3{0, 4, 0}; return true; }
        return false;
    };
    const ClimbBand band{"A", "B", 1.5f, ClimbBandId::Count, ClimbBandId::Count, -1};
    Vec3 point, normal;
    REQUIRE(climb_surface_point(band, lookup, 0.5f, Vec3{3, 2, 0}, point, normal));
    REQUIRE(point.y == doctest::Approx(2.0f).epsilon(1e-4));
    REQUIRE((point.x * point.x + point.z * point.z) == doctest::Approx((1.5f + kPlayerCapsuleRadius) * (1.5f + kPlayerCapsuleRadius)).epsilon(1e-3));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL — `grab.h` not found.

- [ ] **Step 3: Implement the band table and solver**

Table (radii are start values from the S4 capsule measurement; the test above pins the surface rule, not the numbers):

| Band | boneA → boneB | radius | up | down | weakpoint |
|---|---|---|---|---|---|
| LeftFoot | `Bone_TUMASAKI_L` → `Bone_TOE_L` | 1.7 | LeftShin | Count | -1 |
| LeftShin | `Bone_TOE_L` → `Bone_MOMO_L` | 1.6 | LeftThigh | LeftFoot | -1 |
| LeftThigh | `Bone_MOMO_L` → `Bone_KOSI` | 1.7 | Waist | LeftShin | -1 |
| Waist | `Bone_KOSI` → `Bone_HARA` | 2.0 | Chest | LeftThigh | -1 |
| Chest | `Bone_HARA` → `Bone_MUNE` | 2.0 | Neck | Waist | -1 |
| Neck | `Bone_MUNE` → `Bone_KUBI` | 1.4 | Head | Chest | -1 |
| Head | `Bone_KUBI` → `Bone_KAO` | 1.2 | Count | Neck | 0 |
| RightShoulder | `Bone_MUNE` → `Bone_KATA_R` | 1.5 | RightArm | Chest | -1 |
| RightArm | `Bone_KATA_R` → `Bone_UDE_R` | 1.2 | Count | RightShoulder | 1 |

`climb_surface_point` = closest point on the segment at `t`, radial direction = the horizontal component of `hint − axis` (fallback `+X` when degenerate), point = axis + radial × (radius + `kPlayerCapsuleRadius`). Non-finite inputs return `false` and write nothing.

- [ ] **Step 4: Add capsule identity and fill it**

In `entity_collision_capsules`, when a capsule is built, set `rig = static_cast<int>(rigIndex)` and `segment` = its index in `kValusCapsules` (0–7). Existing tests that aggregate-initialise `Capsule{a, b, r}` keep compiling.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add app/src/grab.h app/src/grab.cpp app/src/collision.h app/src/collision.cpp app/tests/test_grab.cpp app/CMakeLists.txt
git commit -m "feat(app): climb bands and capsule identity"
```

---
### Task 3: Player combat state machine

**Files:**
- Create: `app/src/player_state.h`, `app/src/player_state.cpp`
- Test: `app/tests/test_player_state.cpp`
- Modify: `app/src/player.h` (`PlayerInput` gains `grab`, `strike`)
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `player_update`, `resolve_player_collision`, `climb_surface_point`, `nearest_climb_band`.
- Produces:
```cpp
enum class PlayerMode { Grounded, Grabbing, Climbing, Striking, Falling, Staggered };
struct PlayerCombat {
    PlayerMode mode = PlayerMode::Grounded;
    float modeSeconds = 0.0f;
    float stamina = 100.0f;
    float regenDelay = 0.0f;
    ClimbBandId band = ClimbBandId::Count;
    float bandT = 0.5f;
    int weakpoint = -1;
    bool strikeHit = false;
    bool grabWasDown = false;
    bool strikeWasDown = false;
};
struct PlayerFrame {
    const Scene* scene = nullptr;
    const Heightmap* heightmap = nullptr;
    JointLookup jointWorld;
    std::size_t entityRig = 0;
    float cameraYawDeg = 0.0f;
    std::vector<Capsule> entityCapsules;
};
struct CombatTuning {
    float grabRange = 0.9f;
    float climbSpeed = 1.2f;
    float staminaHang = 2.5f;
    float staminaClimb = 4.0f;
    float staminaStrike = 8.0f;
    float staminaRegen = 25.0f;
    float staminaRegenDelay = 1.0f;
    float strikeSeconds = 1.1f;
    float strikeHitFrom = 0.275f;   // 0.25 * 1.1
    float strikeHitTo = 0.495f;     // 0.45 * 1.1
    float catchSeconds = 0.6f;
    float catchRange = 2.5f;
    float staggerSeconds = 1.2f;
    float damageWeak = 20.0f;
    float damageFull = 34.0f;
};
struct StepEvents {
    int damageDealt = 0;
    bool gripBroke = false;
    bool landed = false;
    bool grabbed = false;
};
StepEvents player_step(PlayerState&, PlayerCombat&, const PlayerInput&,
                       const PlayerFrame&, const CombatTuning&, float dtSeconds);
```

- [ ] **Step 1: Write the failing transition tests**

```cpp
static PlayerFrame make_foot_frame() {
    PlayerFrame f;
    f.jointWorld = [](std::string_view name, Vec3& out) {
        if (name == "Bone_TUMASAKI_L") { out = {0, 0, 0}; return true; }
        if (name == "Bone_TOE_L")      { out = {0, 2, 0}; return true; }
        return false;
    };
    return f;
}

TEST_CASE("holding grab attaches to the nearest band") {
    PlayerState player{}; player.position = {0, 0, 0};
    PlayerCombat combat{};
    PlayerInput input{}; input.grab = true;
    PlayerFrame frame = make_foot_frame();   // test-local: a JointLookup whose band 0 spans y 0->2 at x=0
    CombatTuning tuning{};

    StepEvents e = player_step(player, combat, input, frame, tuning, 1.0f / 60.0f);

    REQUIRE(combat.mode == PlayerMode::Grabbing);
    REQUIRE(e.grabbed);
    REQUIRE(combat.band == ClimbBandId::LeftFoot);
}

TEST_CASE("stamina zero forces a fall once, not every frame") {
    // Grabbing with stamina 0 -> Falling on the first step, then Staggered then Grounded.
}

TEST_CASE("a strike deals damage once and returns to the climb") {
    // Attached to Head, hold strike for the hit window: damageDealt == 34 exactly once.
}

TEST_CASE("a frame-time spike cannot skip a band") {
    // dt = 0.25 is clamped to 0.05; bandT advances <= climbSpeed * 0.05 / length.
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL — `player_state.h` not found.

- [ ] **Step 3: Implement `player_step`**

Rules, in order: clamp `dt` to `[0, 0.05]`; advance `modeSeconds`; on `Grounded` run `player_update` then `resolve_player_collision` (same order as today); grab on a rising edge when `nearest_climb_band(...) >= 0`; climbing advances `bandT` by `climbSpeed * dt / segmentLength`, entering `up`/`down` at the ends; stamina drains by mode and regenerates on `Grounded` after `regenDelay`; at zero → `Falling` (once), then `Staggered` on landing, then `Grounded`; `Striking` computes charge fraction and emits `damageWeak`/`damageFull` once inside the hit window; a hit sets `gripBroke` and enters `Falling` with a `catchSeconds` catch window in which a **fresh** grab press re-attaches. Attached modes write the surface point into `player.position` and never call the heightmap or the capsule resolver.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/src/player_state.h app/src/player_state.cpp app/src/player.h app/tests/test_player_state.cpp app/CMakeLists.txt
git commit -m "feat(app): player combat state machine"
```

---
### Task 4: Climb camera

**Files:**
- Modify: `app/src/camera.h`, `app/src/camera.cpp`
- Test: `app/tests/test_camera.cpp`

**Interfaces:**
- Produces:
```cpp
struct CameraClimb {
    bool active = false;
    float distance = 6.0f;
};
void camera_climb_enter(Camera&, CameraClimb&, Vec3 attachPoint, Vec3 surfaceNormal);
void camera_climb_update(Camera&, CameraClimb&, Vec3 attachPoint, float dragDx,
                         float dragDy, float wheelDelta, float dtSeconds);
void camera_climb_exit(Camera&, CameraClimb&);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("climb camera seeds outward and clamps distance") {
    Camera cam{}; CameraClimb climb{};
    camera_climb_enter(cam, climb, Vec3{0, 10, 0}, Vec3{1, 0, 0});
    REQUIRE(climb.active);
    REQUIRE(cam.distance == doctest::Approx(6.0f));
    REQUIRE(cam.target.x == doctest::Approx(0.0f));
    REQUIRE(cam.target.y == doctest::Approx(10.0f));
    for (int i = 0; i < 100; ++i) camera_climb_update(cam, climb, cam.target, 0, 0, 10, 1.0f / 60.0f);
    REQUIRE(cam.distance <= 8.0f + 1e-4f);
    REQUIRE(cam.distance >= 5.0f - 1e-4f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement the camera**

`enter` sets `target = attachPoint`, `yaw = atan2(normal.x, normal.z)` in degrees, `pitch = 10`, `distance = 6`. `update` sets the target, applies drag at 0.4°/px with pitch clamped ±45°, wheel multiplicative clamped `[5, 8]`. `exit` sets `active = false` and leaves the orbit where it is.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/src/camera.h app/src/camera.cpp app/tests/test_camera.cpp
git commit -m "feat(app): climb camera"
```

---
### Task 5: HUD (SDL_ttf → SDL_GPU)

**Files:**
- Create: `app/src/hud.h`, `app/src/hud.cpp`
- Modify: `app/src/renderer.h`, `app/src/renderer.cpp` (one HUD quad pass), `app/CMakeLists.txt`
- Test: `app/tests/test_hud.cpp`

**Interfaces:**
- Produces:
```cpp
struct HudRect { float x, y, w, h; };
struct HudState { bool visible = false; float stamina = 1.0f; float health = 1.0f; };
HudRect hud_stamina_rect(float fraction);   // bottom-left, 240×12 px at 1280×720
HudRect hud_health_rect(float fraction);    // top edge, 600×8 px
void hud_format_stamina(char (&out)[32], float fraction);  // "STAMINA 62"
```
Renderer gains `void set_hud(const HudState&);` and draws the overlay at the end of `render_frame`, after the meshes, using the existing shader with an identity view-projection and NDC-space quads. The stamina percentage is one SDL_ttf texture, uploaded only when the integer changes.

- [ ] **Step 1: Write the failing layout test**

```cpp
TEST_CASE("hud bars clamp and scale") {
    HudRect s = hud_stamina_rect(0.5f);
    REQUIRE(s.w == doctest::Approx(120.0f));
    REQUIRE(hud_stamina_rect(-1.0f).w == 0.0f);
    REQUIRE(hud_stamina_rect(2.0f).w == doctest::Approx(240.0f));
    HudRect h = hud_health_rect(0.0f);
    REQUIRE(h.w == 0.0f);
    char label[32];
    hud_format_stamina(label, 0.62f);
    REQUIRE(std::string(label) == "STAMINA 62");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement layout, text, and the quad pass**

`hud_stamina_rect` clamps the fraction to `[0,1]`; bar x/y are pinned by the test constants. `set_hud` stores the state; `render_frame` draws the two bar quads with alpha blending and the stamina label via `TTF_RenderText_Blended` (font: `/System/Library/Fonts/Supplemental/Arial.ttf`, 16 pt), re-uploading only when the rounded percentage changes. Add `find_package(SDL3_ttf CONFIG REQUIRED)` and link `SDL3_ttf::SDL3_ttf` in both targets.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build app/build && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/src/hud.h app/src/hud.cpp app/src/renderer.h app/src/renderer.cpp app/CMakeLists.txt app/tests/test_hud.cpp
git commit -m "feat(app): stamina/health HUD"
```

---
### Task 6: App wiring, input, and `--s41-selftest`

**Files:**
- Modify: `app/src/app.cpp`, `app/src/app.h`, `app/src/main.cpp`
- Modify: `app/src/anim.h`, `app/src/anim.cpp` (mode→clip selector, sibling to `select_clip_index`)
- Modify: `app/README.md`
- Test: `app/tests/test_player_state.cpp` (the app-level timeline is exercised by the selftest)

**Interfaces:**
- Consumes: everything above.
- Produces: `AppOptions::s41Selftest` (`--s41-selftest`); status line
  `s41-status grab=<band> climb=<m> strikes=<n> entity_health=<n> mode=<mode> defeated=<0|1>`; event lines
  `s41-grab`, `s41-climb`, `s41-strike`, `s41-fall`, `s41-death`.

- [ ] **Step 1: Wire input**

Add `bool grab` / `bool strike` to `PlayerInput`; in the interactive loop read `E`/`J` from `SDL_GetKeyboardState`, and poll the gamepad (`SDL_OpenGamepad` on `SDL_EVENT_GAMEPAD_ADDED`, axes → move/camera, RT → grab, X → strike, Start → pause). A removed gamepad clears its handle and every input it owned.

- [ ] **Step 2: Replace the interactive player block**

In `app.cpp` around 1373–1400, build `PlayerFrame` (entity palette, `entity_collision_capsules`, camera look yaw), call `player_step`, apply `StepEvents` to the encounter state (health, reaction clip, flash), then apply the player transform and update the player pose. `Grounded` keeps today's `player_update` → `resolve_player_collision` order inside `player_step`.

- [ ] **Step 3: Add clip selection and camera/HUD branches**

`select_mode_clip_index(playback, combat, input)` maps: Grabbing → `player_a_grip_loop`, Climbing → `player_a_grip_move_f1`, Striking → `player_a_grip_float_u_hi_attack`, Falling/Staggered → `player_a_damage_m_fly2`, Grounded → the existing locomotion selector. A missing name is a load error, not a fallback. Attached modes drive `camera_climb_*`; the app calls `renderer.set_hud(...)` each frame and the overlay is drawn at the end of `render_frame`.

- [ ] **Step 4: Write the `--s41-selftest` timeline**

Reuse S4's spawn/approach, then: grab (assert band + finite position), climb 240 frames (assert mode, a ≥3.0 m rise, monotonic, no NaN), strike twice (assert one damage event per swing and the loop closes), a deliberately missed catch (assert ground contact), force stamina to zero (assert fall + land), force health to zero (assert death clip + `defeated=1`). Print the event lines and the final `s41-status`; exit 1 with a specific message on any failed invariant.

- [ ] **Step 5: Build and run the selftest**

Run:
```bash
cmake --build app/build
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb --s41-selftest
```
Expected: `s41-status ... defeated=1`, exit 0.

- [ ] **Step 6: Update the README controls**

Add `E` grab, `J` strike, gamepad map, and the climb camera line; keep the "cursor never captured" paragraph unchanged.

- [ ] **Step 7: Commit**

```bash
git add app/src/app.cpp app/src/app.h app/src/main.cpp app/src/anim.h app/src/anim.cpp app/README.md
git commit -m "feat(app): wire the climb and fight loop"
```

---
### Task 7: Acceptance script, stills, and results note

**Files:**
- Create: `scripts/s41-acceptance.sh`
- Create: `docs/notes/s41-results.md`
- Modify: `AGENTS.md` (S4.1 state)
- Artifact (gitignored): `out/s41/*.png`, `out/s41-acceptance-run.log`

**Interfaces:**
- Consumes: `--s41-selftest`, `scripts/s4-acceptance.sh` (regression), `scripts/s3-acceptance.sh` (regression).
- Produces: a `PASS —` summary line and the results note with the user's walk checklist.

- [ ] **Step 1: Write the script**

Follow `scripts/s4-acceptance.sh` exactly: `set -euo pipefail`, `stamp`/`fail`, tee to `out/s41-acceptance-run.log`, check dependencies loudly, build both projects, run both suites, run `--s41-selftest`, assert the event/status lines field by field, delete stale stills, capture grab/mid-climb/strike/reaction/defeat stills with PNG magic + size + non-uniformity guards, run the pixel guard, measure load and marginal frame cost (`--frames 1` vs `--frames 240`), run S3 and S4 unchanged, print the summary with sha256s.

- [ ] **Step 2: Run it**

Run: `./scripts/s41-acceptance.sh`
Expected: `PASS —` with the measured numbers. Fix the script or the app, never the expectations, until it is green.

- [ ] **Step 3: Write the results note**

Sections as in `docs/notes/s4-results.md`: environment, commands, clip record + sha256s, provenance (`derived-mesh` weakpoints, builder-tuned constants), the status line verbatim, stills table, pixel numbers, perf table, suites/regressions, known limitations, and the user's walk checklist **PENDING**.

- [ ] **Step 4: Update `AGENTS.md`**

Change the S4.0 line to record S4.1 as implemented with the user's walk pending, and keep the S3/S4.0 walks' status explicit.

- [ ] **Step 5: Commit**

```bash
git add scripts/s41-acceptance.sh docs/notes/s41-results.md AGENTS.md
git commit -m "docs: S4.1 acceptance and results"
```

---
## Self-Review Notes

- **Spec coverage:** goals 1–9 map to T2/T3 (grab/climb/stamina/strike), T1 (clips), T4 (camera), T5 (HUD), T6 (gamepad/wiring), T7 (acceptance). Non-goals (AI, player health, cloth, audio) have no task by design.
- **Pinned values:** grab 0.9 m, climb 1.2 m/s, stamina 100/2.5/4.0/8.0/25/1 s, strike 0.9 s charge / 0.25–0.45 window, catch 0.6 s / 2.5 m, HP 100 / damage 20/34, camera 5–8 m / ±45°. A mismatch in a test is a finding, not a test edit.
- **Type consistency:** `ClimbBandId`, `PlayerMode`, `StepEvents`, `PlayerFrame`, `CombatTuning`, `CameraClimb`, `HudRect` are defined once and used by name throughout.
- **Review Focus mapping:** (1) T3 dt-clamp test; (2) T2 chain/seam test + T3 band-end test; (3) T3 stamina-zero test; (4) T6 load error path + T1 clip pin; (5) T6 gamepad removal clears inputs.
- **Deviation recorded:** the converter's clip record is a provenance artifact; the app resolves clips by glTF animation name (authoritative), and the acceptance cross-checks the two. This is the builder's call inside the spec's "sidecar format" allowance.
- **Recon addendum (2026-10-20):** the `.cgpr` weakpoint-binding pull is **closed negative** (two identical anchors, no body-part binding) and `.cchr` holds no stats. T1's derived-mesh weakpoints and clip record are unaffected; the bounded eboot stat pass is Step 8.

## Completion (<date>)

Pending — filled when the acceptance run is green and the user's walk is done.
