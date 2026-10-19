# S2 acceptance results (2026-10-19) — real clips + locomotion

Spec milestone S2 (`2026-10-19-s2-motion-design.md` §1–§2): **"Wander (and ≥1 other rig)
plays a converted `.anb` clip correctly vs a dev-only reference capture"**, extended with
controller-driven locomotion (still → stand, move → walk, +Shift → run).

**Status: the converter/app checks are ACCEPTED; the user's walk (§6) is PENDING.** The
first acceptance pass found two blocking product-path defects — the converted glbs played
a **folded pose** and the single-model camera framed the **bind pose** — both are fixed and
re-measured here: the glbs now play the decoded pose (the animation bridge emits the
conjugate), and the single-model captures frame the animated subject with the whole body in
view. Both sides were independently re-verified (separate converter/pose and app/capture
passes): the glbs match the decode oracle to float32 precision, the six captures reproduce
byte-identically, and no subject pixel touches an image edge. The walk table in §6 is still
the user's to run.

| | |
|---|---|
| Date | 2026-10-19 (local −03) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `s2-motion` (`44610f0`, `a69d139`, `30de098` + this note) |
| Library (gitignored) | `SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` |
| Manifest (gitignored) | `out/re/stage/valus.manifest` (S3's 30-instance composition) |
| Captures (gitignored) | `out/re/capture/s2/` (+ `diag/`, `thumbs/`) |
| Game root | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |

## 1 · Verification

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet test tests/BpConvert.Tests -v q
# Approved! - failed 0, passed 82, skipped 0, total 82 - 1 m 37 s

export SOTC_LIB=$PWD/out/library
ctest --test-dir app/build --output-on-failure
# 1/2 unit_tests ......... Passed 23.81 s
# 2/2 windowed_selftest .. Passed  0.16 s
# 100% tests passed out of 2
```

`cmake --build app/build -j` rebuilt the viewer cleanly. The new pose pins:
`Stand_world_pose_puts_the_head_and_hands_at_the_decoded_heights` composes the **written**
glb with column-vector node semantics and asserts the decoded frame-0 heights
(`Bone_KOSI` 0.7429, `Bone_MUNE` 0.8343, `Bone_KUBI` 1.0462, `Bone_KAO` 1.1165, ±0.02 m;
`Bone_KAO` above the pelvis by > 0.30 m) and cross-checks SharpGLTF's evaluator;
`Stand_rotation_keys_stay_inside_the_xval_bands_of_their_bind_locals` catches the old
bridge (it read 156.0° on `HARA`, 176.7° on `HAND_A_L`); a component-order test pins
`AnimBridge.ToGlb` itself. The app side pins `posed_bounds()` (a one-joint fixture lifted
by a y = 0.5 root channel, with the no-skin bind fallback).

The demo models were re-converted with the current writer (`convert --force --clips
player_a_stand,player_a_walk,player_a_run` / `--clips horse_a_add_horse_kick`) and are
**byte-identical** to the shipped library:

- `wander.glb` `54fe97d7215c0d1053875d5085d2265d2a425bbd1a55a4c2e0504de8eaaefd64`
- `agro.glb` `f17165435d20fbcac1629e6fcaed30467e42d863835572b6acd105a73590bc8f`

The glbs carry `player_a_stand` (76 keys, 15 fps, 5.0 s), `player_a_walk` (21),
`player_a_run` (18) — Wander, 348 joints — and `horse_a_add_horse_kick` (16) — Agro,
132 joints. Every capture printed `procedural=0` and the expected clip name.

## 2 · The stills (app ↔ reference)

Single-model captures use `--anim NAME --anim-time SEC --width 960 --height 540
--frames 3 --screenshot …` (1920×1080 px, the reference size); the arena uses
`--manifest … --model wander.glb` with no `--anim` pin, so per-frame selection is live.

| app still (`out/re/capture/s2/`) | clip / time | paired reference (`out/re/capture/compare/`) |
|---|---|---|
| `s2-wander-stand-t000.png` | `player_a_stand` 0.000 | `ref-idle-anchored-…234236_562` (+5 s) |
| `s2-wander-stand-t250.png` | `player_a_stand` 2.500 | `ref-idle-anchored-…234241_605` / `…234246_560` (phase-free) |
| `s2-wander-walk-t050.png` | `player_a_walk` 0.500 | none — the C2 set has no walk frame |
| `s2-wander-run-t050.png` | `player_a_run` 0.500 | none — no run frame in the C2 set |
| `s2-agro-kick-t000.png` | `horse_a_add_horse_kick` 0.000 | `ref-horse3-…000035_372` (standing) |
| `s2-agro-kick-t025.png` | `horse_a_add_horse_kick` 0.250 | `ref-horse-…233513_972` (whistle; no kick reference) |
| `s2-arena-stand.png` | live selection → stand | none (our composition) |
| `s2-arena-run-selftest.png` | live selection, stand → run | none |

Supplemental evidence (not in the design's single-model set):
`s2-arena-stand-pinned.png` (stand pinned after the 240-frame arena walk),
`s2-agro-arena-rigcheck-t000.png` (agro in the arena), `diag/blender-stand-t0.png`
(Blender 5.2.2's import from the **pre-fix** glb — the fold's independent evidence, now
historical).

## 3 · Pose observations (phase-free)

Framing is measured on the 1920×1080 PNGs as the bounding box of pixels differing from the
flat stage background (26, 26, 26) by more than 10 levels. **No subject pixel touches any
image edge in any single-model still** (the pre-fix set all had subject pixels on row 0).
(The fix report's preliminary frame check used a >8 threshold; the boxes below are the >10
re-measure. Both report zero edge contact; only a few boundary pixels differ.)

**Stand — `s2-wander-stand-t000` vs the +5 s reference.** The subject is upright and fully
in frame, bbox `(810, 67, 1109, 968)`: head above the shoulders, arms hanging at the sides,
boots planted, the relaxed staggered stance visible (left foot back). The +5 s reference is
an idle seen from **behind**; our capture faces the camera, so the stance's left/right sign
is not directly comparable, and the reference's phase is unknown (`re2-xval.md` §3). The
uprightness, arm placement and relaxed stance match in kind. **PASS (phase-free).**

**Stand — `s2-wander-stand-t250`.** The pose advances (bbox `(807, 59, 1113, 946)`); the
body stays upright, hands at the hips, feet planted. Same phase-free caveat.

**Walk — `s2-wander-walk-t050`.** No reference exists. The leg cycle is legible and
upright (bbox `(786, 135, 1170, 971)`): the right leg is lifted and swung forward with a
bent knee, the left leg planted, forearms swinging out from the front (hands ≈0.24–0.28 m
lateral of the pelvis, vs ≈0.21 m in the stand). Not obviously mirrored at the visible
joints. **PASS in kind.**

**Run — `s2-wander-run-t050`.** No reference. Larger lift/stride than the walk (bbox
`(807, 191, 1066, 1005)`): the support leg extended, the trailing leg in heel-up recovery
and occluded by the torso in this front view (the high-knee phase is earlier, ≈0.13 s —
t=0.5 is frame 15 of a 30 fps clip), arms swinging. The body stays upright and the whole
figure is in frame. **PASS in kind.**

**Agro — `s2-agro-kick-t000` / `t025` vs the standing references.** The whole horse is in
frame in both stills (bbox `(853, 264, 1051, 912)` and `(856, 261, 1061, 894)`): head and
ears at the top, the four legs and all hooves above the bottom edge, bridle visible (the
saddle is only partly visible from the front). The references show a 3/4 standing view;
ours is front-on, so no frame-to-frame
correspondence is claimed (the kick has no reference, per the design). The hoof/hock
positions change between 0 and 0.25 s, so the clip advances. **The frame-0 rig check is now
performable** (whole horse, upright); the kick itself remains the user's live check.

**Arena — `s2-arena-run-selftest`.** The status lines show the per-frame selection
(`s2-anim clip=player_a_stand` then `clip=player_a_run`, both `procedural=0`) and
`s3-status … pos=309.312,29.802,305.405 ground=ok frames=240`; the still shows the player
upright on the bowl floor with the support foot at the surface. The stand-pinned arena
still shows the same upright pose. Grounding is **pass-in-kind**.

## 4 · The two defects (fixed)

### 4.1 The glb played a folded pose — fixed by conjugating the animation rotation

The decode was upright; the exported glb was not.

`python3 tools/re/anb/pose_dump.py pose cp3shadow/anim/player_a_stand.anb --frame 0`:

```text
Bone_KOSI  y +0.7429   (pelvis)
Bone_MUNE  y +0.8343
Bone_KUBI  y +1.0462
Bone_KAO   y +1.1165   (head, +0.374 above the pelvis)
```

Root cause, confirmed by re-composition: `tools/re/anb/pose_dump.py` builds **row-vector**
locals (`local` rows; translation in row 3) and substitutes `matrix_of_quat(bridged)` into
the rotation rows. `matrix_of_quat` returns the standard **column-vector** rotation
R(q), so the tool's effective column-vector local is R(q)ᵀ = R(q⁻¹). glTF node rotations
are column-vector, and the writer copied `bridged` verbatim: the file's animated local was
the transpose of the validated pose. The bind path was already consistent (the T-pose and
the IBM-derived node TRS match), which is why only animation folded.

Fix: `AnimBridge.ToGlb` now emits **(−x, y, z, w)** — the conjugate/inverse of the tool's
(x, −y, −z, w) — uniformly for keyed and static tracks. Times, statics, the root channel,
the 0xD key cap and the TRS scope are unchanged. The written `player_a_stand` at t=0,
evaluated with standard column-vector glTF semantics (node TRS; animation replaces the bind
rotation; LINEAR):

```text
Bone_KOSI  y 0.7429
Bone_MUNE  y 0.8343
Bone_KUBI  y 1.0462
Bone_KAO   y 1.1165   (head, +0.374 above the pelvis)
```

Exactly the decoded heights. As a control, applying the conjugate a second time to the
fixed file (arithmetically the old emission) folds it again: `Bone_KAO` 0.3758, below the
pelvis — the fold's signature. The pre-fix measurement and Blender's independent import are
recorded in the Task 7 report; `diag/blender-stand-t0.png` is that historical file.

### 4.2 The single-model camera framed the bind pose — fixed by posed bounds

`camera_frame` ran on the bind bounds before playback; the clip's root translation then
lifts the subject onto its feet (the design-approved semantics), but the camera was not
reframed. Wander's stand root y = 0.7429 put the whole body above the target
(y = −0.1281); Agro's kick root y ≈ 1.37–1.40 put the camera under the belly. Every
single-model still was cut at y = 0.

Fix (`app/src/anim.cpp`, `app/src/app.cpp`): `posed_bounds()` skins the scene's vertices
under the current palette (`worldJoint × inverseBind`, then the mesh model transform; bind
positions when there is no skin). Single-model mode frames at the resolved start time
(`--anim`/`--anim-time`), and `F` reframes on the current pose. The posed box is padded by
1/cos(fov/2) because `camera_frame`'s r/tan(fit) distance leaves the sphere tangent only
past the edge, so a tall narrow figure would otherwise still touch the top. Manifest mode
and its follow camera are unchanged, as is the S3 ground lift.

Post-fix framing (whole subject; margins are edge distances, `W−1−x1` / `H−1−y1`):

| still | bbox (x0, y0, x1, y1) | margin left/top/right/bottom |
|---|---|---|
| `s2-wander-stand-t000` | 810, 67, 1109, 968 | 810 / 67 / 810 / 111 |
| `s2-wander-stand-t250` | 807, 59, 1113, 946 | 807 / 59 / 806 / 133 |
| `s2-wander-walk-t050` | 786, 135, 1170, 971 | 786 / 135 / 749 / 108 |
| `s2-wander-run-t050` | 807, 191, 1066, 1005 | 807 / 191 / 853 / 74 |
| `s2-agro-kick-t000` | 853, 264, 1051, 912 | 853 / 264 / 868 / 167 |
| `s2-agro-kick-t025` | 856, 261, 1061, 894 | 856 / 261 / 858 / 185 |

## 5 · Known limits (accepted scope)

- **No blending:** hard cuts between stand/walk/run (design §4); the switch restarts the
  clip at t = 0.
- **Foot sliding is possible:** the controller drives 2.2/5.0 m/s while the clips play
  in place (root x/z zeroed); speed-matched playback is out of scope (design §3).
- **Phase-free comparison:** the capture's animation phase is unknown and no
  frame-to-frame correspondence is claimed (`re2-xval.md` §3).
- **Source-data finger-bone loop gaps:** the played walk loop is open on
  `Bone_YUBI_A_R` (**22.87°**) and `Bone_YUBI_B_R` (**22.86°**); run on
  `Bone_YUBI_A_L` (**6.66°**), `Bone_YUBI_B_L` (**6.68°**) and `Bone_OYAYUBI_B_R`
  (**21.20°**). These keys are authored in the `.anb` and emitted unchanged (the writer
  drops only the surplus `0xD` key beyond the engine duration) — a finger snap at the
  loop point in the source data, not a converter regression. The body/root loops close.
- The horse's clip is standalone; in-arena multi-rig animation is deferred (design §3).
- Frames: stand 76 keys / 15 fps / 5.0 s; walk 21 keys / 15 fps / 1.333 s; run 18 keys /
  **30 fps** / 0.567 s; horse kick 16 keys / 15 fps / 1.0 s. (The run's source rate field
  is 1.0 → 30 fps; the others 0.5 → 15 fps.)

## 6 · The user's walk — PENDING

Run the arena and walk the bowl floor:

```bash
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/valus.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | still → stand | start; press no movement | status `clip=player_a_stand`; the stand loop | ☐ |
| 2 | move → walk | W/A/S/D | status `clip=player_a_walk`; feet cycle | ☐ |
| 3 | +Shift → run | W+Shift | status `clip=player_a_run` | ☐ |
| 4 | feet grounded | walk onto the bowl floor | feet track the heightmap; no float/sink | ☐ |
| 5 | hard cuts | switch still/walk/run | immediate switch; no blend (accepted) | ☐ |
| 6 | controls | `F`; Escape | reframe; clean quit | ☐ |

**The user's verdict: PENDING — the walk has not been performed.** The two blockers are
fixed, so the walk can now be run against a build that shows the decoded, upright pose.

## 7 · Evidence files (all gitignored)

Under `out/re/capture/s2/`: `s2-wander-stand-t000.png`, `s2-wander-stand-t250.png`,
`s2-wander-walk-t050.png`, `s2-wander-run-t050.png`, `s2-agro-kick-t000.png`,
`s2-agro-kick-t025.png`, `s2-arena-stand.png`, `s2-arena-run-selftest.png`,
`s2-arena-stand-pinned.png`, `s2-agro-arena-rigcheck-t000.png`, the matching `*.log`
files, `diag/blender-stand-t0.png` (pre-fix), `thumbs/`.

## 8 · Cross-references

- Design: `docs/superpowers/specs/2026-10-19-s2-motion-design.md` (§2 goals, §4
  anti-goals, §6.3 grounding, §6.4 acceptance).
- Plan: `docs/superpowers/plans/2026-10-19-s2-motion.md` (Task 7).
- Decode/cross-validation: `docs/notes/re2-xval.md` §2–§3 (the bridge and the
  phase-free pose comparison), `docs/notes/re-anb.md` §7.
- Reference captures: `docs/notes/re-capture.md` §3; pairing legend
  `out/re/capture/compare/README.md`.
- S3 arena / grounding: `docs/notes/s3-results.md` §2, §5.
