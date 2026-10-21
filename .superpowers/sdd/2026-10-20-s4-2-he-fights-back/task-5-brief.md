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
