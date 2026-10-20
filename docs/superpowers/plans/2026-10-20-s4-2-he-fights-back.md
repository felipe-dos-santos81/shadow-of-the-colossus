# S4.2 "He fights back" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Valus encounter a real fight — shell-derived weakpoints with gated damage, a two-dimensional climb that reaches both weakpoints, Valus turning/stomping/swinging his club, and a player who can be hurt and killed.

**Architecture:** The entity's weakpoint shells are located by material name and reduced to a posed sphere each strike; damage is gated on that sphere. The climb gains lateral orbit and one chest→shoulder branch. A small reactive attack state drives two clipped attacks with pinned impact windows; the club is a static mesh drawn from `OptBone_sword`'s bind frame. Player health/death lives in `PlayerCombat`; the HUD and `R` reset complete the loop.

**Tech Stack:** C++20 · SDL3 + SDL_GPU/Metal · SDL3_ttf · .NET 10 converter · doctest · bash 3.2

**Spec:** `docs/superpowers/specs/2026-10-20-s4-2-he-fights-back-design.md`

## Global Constraints

- **SDL3 + SDL_GPU/Metal**, C++20, Apple Silicon; **windowed**; **the cursor is never captured**; `windowed_selftest` green.
- Offline-first: converted glbs, manifests, clip records only. Assets and derived glbs uncommitted.
- Static-only RE. Personal use, no redistribution.
- Tests fail fast without `SOTC_GAME_ROOT` / `SOTC_LIB`; bash 3.2; typed commits; **≤4 concurrent agents**.
- S3/S4/S4.1 manifests and scripts are **not modified**; their acceptance stays green.
- Spec values: player HP **100**; stomp **35** direct (2.5 m) / **15** shockwave (6 m); club **45** (2.5 m from `OptBone_hit`); i-frames **1.0 s**; hurt 0.40 s medium / 1.23 s heavy; death 1.165 s pinned; attack range 4.5 m stomp / 14 m club; yaw **30°/s**; cooldowns 4 s / 5 s.
- The hit test is the **closest point on the player capsule segment** to the shell centre.
- "Grounded" for attack selection is `PlayerMode::Grounded` exactly.
- The S4.1 head strike must keep dealing **34** damage (capsule top inside the shell sphere).

## Review Focus

1. **Club attach frame** — a wrong socket or bind composition puts the club in the ground. A known-clip still must show it in the hand; a unit test maps a known bind-space point to a known world point.
2. **Hit test point** — using the feet turns the S4.1 head strike into 0 and fails the regression; the capsule segment is the ruled point.
3. **One impact per attack** — a stomp/club window that fires twice per swing, or after death, is a failure.
4. **Death/reset state** — a dead player who can still be stomped, or an `R` that leaves 0 stamina or a dead Valus, breaks the loop.
5. **S4.1 regression** — the head weakpoint must stay hittable from the scripted front pose.

---
### Task 1: Converter — bake the fight clips and extend the clip record

**Files:**
- Modify: `src/BpConvert.Core/Pipeline/ClipManifest.cs` (add `rootMotion`, add `Delete`)
- Modify: `src/BpConvert.Core/Pipeline/ModelConverter.cs` (delete a stale sidecar when no clips)
- Test: `tests/BpConvert.Tests/ClipManifestTests.cs`
- Artifact (gitignored): re-converted `out/library/.../wander.glb` (15 clips), `.../c01_minotaur_a.glb` (8 clips)

**Interfaces:**
- `ClipManifest.Write(string glbPath, string sourceRelPath, IReadOnlyList<AnimClip> clips)` keeps its shape; each entry gains `"rootMotion": "ground-height" | "body-offset" | "unclassified"` from a name-keyed table in `ClipMotion.cs`.
- `ClipManifest.Delete(string glbPath)` removes `<stem>.clips.json`; `File.Delete` is a no-op when absent.

- [ ] **Step 1: Write the failing tests**

```csharp
[Fact]
public void Write_records_root_motion_and_deletes_a_stale_record()
{
    var dir = Directory.CreateTempSubdirectory("bp-clips-");
    var glb = Path.Combine(dir.FullName, "wander.glb");
    var clips = new[] { new AnimClip { Name = "player_a_damage_h_fly1", Frames = 9, Rate = 0.5, Tracks = [], Root = [] } };
    var path = ClipManifest.Write(glb, "models/.../wander.cmsh", clips);
    using var doc = JsonDocument.Parse(File.ReadAllText(path));
    Assert.Equal("body-offset", doc.RootElement.GetProperty("clips")[0].GetProperty("rootMotion").GetString());

    ClipManifest.Delete(glb);
    Assert.False(File.Exists(path));
}
```

- [ ] **Step 2: Run it red** — `dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"`; expect failure on `rootMotion`.

- [ ] **Step 3: Implement `ClipMotion` + `Delete` + the `else` branch in `ModelConverter`**

`ClipMotion.For(name)` returns `ground-height` for the locomotion and grip/fall clips, `body-offset` for the damage/attack clips, `unclassified` otherwise. `ModelConverter` calls `ClipManifest.Delete` when `animations.Count == 0`.

- [ ] **Step 4: Re-convert both models**

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet publish src/BpConvert.Cli -c Release -o out/bin
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/wander/_cmn/wander.cmsh \
  --clips player_a_stand,player_a_walk,player_a_run,player_a_grip_stand_crouch,player_a_grip_loop,player_a_grip_move_f1,player_a_grip_move_f2,player_a_grip_float_u_hi_attack,player_a_atk1,player_a_damage_m_fly2,player_a_fall_damage_to_death,player_a_damage_h_fly1,player_a_damage_h_fly_end,player_a_fall_damage_start,player_a_fall_damage_end --force
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh \
  --clips minotaur_a_stand,minotaur_a_add_head_damage,minotaur_a_add_ude_r_damage,minotaur_a_add_mune_r_damage,minotaur_a_death_front,minotaur_a_atk_leg_l,minotaur_a_atk_leg_r,minotaur_a_atk_weapon2_a --force
```

Expected: two `ok` lines; sidecars show 15 and 8 clips. Record the sha256s.

- [ ] **Step 5: Update the clip-count pins** — `app/tests/test_asset_io.cpp` 11→15 and 5→8; add `clip_index_named` checks for `player_a_damage_h_fly1` and `minotaur_a_atk_leg_l`.

- [ ] **Step 6: Run both suites and commit**

`dotnet test tests/BpConvert.Tests -v q`; `SOTC_LIB=$(pwd)/out/library ctest --test-dir app/build --output-on-failure`.
Commit: `feat: bake S4.2 fight clips and extend the clip record`.

---
### Task 2: Shell-derived weakpoint proxy

**Files:**
- Modify: `app/src/anim.h`, `app/src/anim.cpp`
- Modify: `app/src/grab.h`, `app/src/grab.cpp`
- Test: `app/tests/test_anim.cpp`, `app/tests/test_grab.cpp`

**Interfaces:**
- `bool posed_weakpoint_proxy(const Scene&, std::size_t rigIndex, const std::vector<Mat4>& palette, const std::string& materialName, Vec3& outCenter, float& outRadius);`
- `const char* weakpoint_material_name(int weakpoint);` → `"weakpoint_03"` (0), `"weakpoint_04"` (1), `nullptr` otherwise.

- [ ] **Step 1: Write the failing tests**
  - A hand-built `Scene` with rig 1, one mesh `rig=1`, material `"weakpoint_03"`, non-identity `model`; assert the world centroid and the corner radius.
  - `false` and untouched outputs for: wrong material, short palette, no skin, out-of-range material index.
  - `weakpoint_material_name` mapping.

- [ ] **Step 2: Run red**, implement exactly as the design §7.1 specifies (rig's own mesh range; weighted palette; `mesh.model`; mean + max corner distance; no writes on failure).

- [ ] **Step 3: Run green** and commit `feat(app): shell-derived weakpoint proxy`.

---
### Task 3: Gate damage on the weakpoint sphere

**Files:**
- Modify: `app/src/player_state.h` (`PlayerFrame::weakpointProxy`), `app/src/player_state.cpp`
- Modify: `app/src/app.cpp` (bind the lookup in `player_frame_of`)
- Test: `app/tests/test_player_state.cpp`

**Interfaces:**
```cpp
using WeakpointProxyLookup = std::function<bool(int weakpoint, Vec3& center, float& radius)>;
// PlayerFrame gains: WeakpointProxyLookup weakpointProxy;
```

- [ ] **Step 1: Write the failing tests**
  - Head band with a valid proxy → 34; head band with an out-of-range proxy → 0; no lookup → 0; waist/chest strike → 0 (replaces the current 20); grip break still true on every swing.
  - Use the **capsule closest point**, not the feet: a proxy centred at head height with a small radius must miss when only the feet are near.

- [ ] **Step 2: Run red**, implement the gate from design §7.2, bind the lookup in `player_frame_of` to `posed_weakpoint_proxy`.

- [ ] **Step 3: Run green**, confirm `--s41-selftest` still deals 34/20/34 (the capsule top is inside the head sphere), commit `feat(app): gate strike damage on the weakpoint shell`.

---
### Task 4: Lateral climb and the chest→shoulder branch

**Files:**
- Modify: `app/src/grab.h/.cpp` (`ClimbBand::branch`, chest row)
- Modify: `app/src/player_state.cpp` (lateral movement, branch at the band top)
- Test: `app/tests/test_grab.cpp`, `app/tests/test_player_state.cpp`

**Interfaces:**
```cpp
struct ClimbBand { /* existing */ int weakpoint; ClimbBandId branch = ClimbBandId::Count; };
```

- [ ] **Step 1: Write the failing tests**
  - Table: chest branch == `RightShoulder`, all others `Count`; branch connectivity `climb_band(branch).down == chest`.
  - Route: attach at chest `t=0.99`, step with `forward+right` → `RightShoulder` at `t=0`; then `forward` → `RightArm`, `weakpoint == 1`.
  - Lateral: hold `right` for N steps → the radial angle advances by `climbSpeed*dt/(r+0.35)`, `y` unchanged; `left` reverses.

- [ ] **Step 2: Run red**, implement the branch + Rodrigues orbit (design §7.3), run green, commit `feat(app): lateral climb and the arm branch`.

---
### Task 5: Player health, hurt, death, reset

**Files:**
- Modify: `app/src/player_state.h/.cpp` (`Hurt`, `Dead`, health, i-frames, `player_take_damage`)
- Modify: `app/src/anim.h/.cpp` (mode→clip)
- Modify: `app/src/app.cpp` (reset path, death clip pin)
- Test: `app/tests/test_player_state.cpp`, `app/tests/test_anim.cpp`

**Interfaces:**
```cpp
bool player_take_damage(PlayerCombat&, float damage, bool heavy, const CombatTuning&);
```
`PlayerMode` gains `Hurt`, `Dead`; `PlayerCombat` gains `health=100`, `invulnSeconds`, `heavyHit`, `pendingDeath`.

- [ ] **Step 1: Write the failing tests** — damage subtracts; i-frames block a second hit; `Falling` + lethal sets `pendingDeath` and dies on landing; `Staggered` + damage → `Hurt`; zero on the ground → `Dead` with the death clip pinned; `R`-reset helper clears HP/stamina/i-frames/mode/Valus state.

- [ ] **Step 2: Run red**, implement design §7.5, map `Hurt`/`Dead` clips in `select_mode_clip_index`, run green, commit `feat(app): player health, hurt and death`.

---
### Task 6: Club attachment

**Files:**
- Modify: `app/src/scene.h` (`JointCpu::bindWorld`), `app/src/asset_io.cpp` (compose the rest chain at load)
- Modify: `app/src/app.cpp` (load the club glb, per-frame model)
- Test: `app/tests/test_anim.cpp` (bind-frame mapping), `app/tests/test_asset_io.cpp` (club load)

**Interfaces:**
```cpp
Mat4 club_model(const Scene&, const std::vector<Mat4>& entityPalette,
                std::size_t swordJoint, const Mat4& entityMeshModel);
```

- [ ] **Step 1: Write the failing tests** — a synthetic joint with a known rest chain: `palette[j] * bindWorld[j]` maps the bind origin to the posed joint position; the club model equals `entityModel × palette × bindWorld`.
- [ ] **Step 2: Run red**, compose `bindWorld` parent-first at load, load `c01_minotaur_a_weapon.glb` as a static mesh, update its model each frame from `OptBone_sword`.
- [ ] **Step 3: Still check** at the stand pose (club in the right hand, above ground); commit `feat(app): attach Valus's club to OptBone_sword`.

---
### Task 7: Valus attacks — stomp and club

**Files:**
- Modify: `app/src/app.h` (`Encounter` attack fields), `app/src/app.cpp` (attack state, windows, damage)
- Test: `app/tests/test_player_state.cpp` (damage application), plus an app-level selftest phase in Task 9

**Interfaces:**
```cpp
enum class ValusAttack { None, StompL, StompR, Club };
struct Encounter { /* existing */ ValusAttack attack = ValusAttack::None; float attackSeconds = 0; float cooldownSeconds = 0; };
```

- [ ] **Step 1: Write the failing tests** — attack selection (stomp if within 4.5 m of a foot; club if within 14 m and `dot > 0.2`; never when the player is attached or not `Grounded`); one impact per clip inside the window; a landed weakpoint hit cancels the attack; no attack after `defeated`.
- [ ] **Step 2: Run red**, implement the selection, yaw slew (30°/s, grounded and not attached), impact windows (stomp `[0.55,0.70]`, club `[0.45,0.65]`), damage points (`Bone_TUMASAKI_*`, `OptBone_hit`), cooldowns.
- [ ] **Step 3: Run green**, still-check the telegraph/impact frames, commit `feat(app): Valus turns, stomps and swings`.

---
### Task 8: HUD player bar, encounter gating, gamepad west

**Files:**
- Modify: `app/src/hud.h/.cpp` (`playerHealth`, `hud_player_health_rect`)
- Modify: `app/src/renderer.cpp` (third bar)
- Modify: `app/src/app.cpp` (gate `set_hud`, feed player health)
- Create: `app/src/gamepad_map.h`; modify `app/src/app.cpp` (WEST)
- Test: `app/tests/test_hud.cpp`, `app/tests/test_gamepad.cpp`, `app/CMakeLists.txt`

**Interfaces:**
```cpp
struct HudState { bool visible=false; float stamina=1; float health=1; float playerHealth=1; };
HudRect hud_player_health_rect(float fraction);   // 240×8 above stamina
inline constexpr SDL_GamepadButton kStrikeGamepadButton = SDL_GAMEPAD_BUTTON_WEST;
```

- [ ] **Step 1: Write the failing tests** — player bar clamps/scales; west-not-south.
- [ ] **Step 2: Implement**, gate the HUD on `!manifest.entities.empty()`, show Valus's bar only when `encounter.health < 100`, run green, commit `feat(app): player health HUD and west strike`.

---
### Task 9: `--s42-selftest`

**Files:**
- Modify: `app/src/app.h` (`s42Selftest`), `app/src/main.cpp`, `app/src/app.cpp`
- Test: exercised by the selftest itself

**Interfaces:** event lines `s42-proxy`, `s42-gate`, `s42-arm`, `s42-stomp`, `s42-club`, `s42-death`, `s42-reset`, `s42-stamina`, and one `s42-status`.

- [ ] **Step 1: Implement the timeline** — proxy resolves 0/1 with the measured radii; foot strike 0 and head strike 34; chest→shoulder→arm reaches weakpoint 1; stomp damages and staggers; i-frames block; club damages; lethal hit plays the death clip and pins; reset restores; the full foot→head climb prints the stamina margin.
- [ ] **Step 2: Run it green** and commit `feat(app): S4.2 deterministic selftest`.

---
### Task 10: Acceptance, stills, results

**Files:**
- Create: `scripts/s42-acceptance.sh`, `docs/notes/s42-results.md`
- Modify: `app/README.md`, `AGENTS.md` (workspace root, uncommitted by design)
- Artifact: `out/s42/*.png`

- [ ] **Step 1: Write the script** in the S4.1 house pattern: build/suites/selftest, six stills (stomp telegraph, stomp impact, club swing, hurt, death, arm weakpoint) with guards, perf, `scripts/s41-acceptance.sh` regression (which runs S4→S3).
- [ ] **Step 2: Run to `PASS —`**, write the results note with verbatim numbers, the measured stamina margin, provenance (shell-derived weakpoints; club socket measured), and the user's walk checklist PENDING.
- [ ] **Step 3: Update `app/README.md`** (sdl3_ttf build, `E`/`J`/`A/D`, gamepad west, club) and `AGENTS.md`.
- [ ] **Step 4: Commit** `docs: S4.2 acceptance and results`.

---
## Self-Review Notes

- **Spec coverage:** goals 1–9 map to T2/T3 (weakpoints/gating), T4 (route), T5 (health), T6/T7 (club/attacks), T8 (HUD/gamepad), T1 (records), T9/T10 (acceptance).
- **Pinned values:** HP 100; 35/15/45; 1.0 s; 0.40/1.23/1.165; 4.5/14 m; 30°/s; 4/5 s; capsule hit point; `Grounded` exact.
- **Type consistency:** `ClimbBandId`, `PlayerMode`, `PlayerCombat`, `CombatTuning`, `PlayerFrame`, `HudState`, `ValusAttack`, `Encounter` are defined once and used by name.
- **Review Focus mapping:** (1) T6 bind test + still; (2) T3 capsule test + S4.1 regression; (3) T7 one-impact test; (4) T5 reset + death tests; (5) T3/T9 head-strike 34.
- **Deviation recorded:** `rootMotion` is record-only; the app's lift rule is unchanged (design §11.4).

## Completion (<date>)

Pending — filled when the acceptance run is green and the user's walk is done.
