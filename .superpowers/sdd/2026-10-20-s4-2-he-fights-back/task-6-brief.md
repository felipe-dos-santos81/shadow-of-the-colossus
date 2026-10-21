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
