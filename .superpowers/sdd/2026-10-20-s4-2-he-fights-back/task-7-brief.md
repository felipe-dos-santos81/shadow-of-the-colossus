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
