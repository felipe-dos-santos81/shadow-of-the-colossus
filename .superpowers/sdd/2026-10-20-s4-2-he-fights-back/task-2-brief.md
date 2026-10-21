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
