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
