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
