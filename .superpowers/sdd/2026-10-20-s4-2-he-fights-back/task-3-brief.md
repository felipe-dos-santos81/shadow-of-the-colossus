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
