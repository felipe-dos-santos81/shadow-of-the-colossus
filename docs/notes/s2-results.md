# S2 acceptance results (2026-10-19) — real clips + locomotion

Spec milestone S2 (`2026-10-19-s2-motion-design.md` §1–§2): **"Wander (and ≥1 other rig)
plays a converted `.anb` clip correctly vs a dev-only reference capture"**, extended with
controller-driven locomotion (still → stand, move → walk, +Shift → run).

**Status: NOT ACCEPTED — blocked by two product-path defects found by this acceptance**
(not fixed in this task): the converted glbs play a **folded pose**, and the single-model
capture camera frames the **bind pose** so the subject leaves the frame. The clips are
baked, load, are named correctly, and are selected by the controller; the pose is not the
decoded pose. The user's walk (§6) is **pending** and blocked until the fixes.

| | |
|---|---|
| Date | 2026-10-19 (local −03) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `s2-motion` (`8194991` + this note) |
| Library (gitignored) | `SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` |
| Manifest (gitignored) | `out/re/stage/valus.manifest` (S3's 30-instance composition) |
| Captures (gitignored) | `out/re/capture/s2/` (+ `diag/`, `thumbs/`) |
| Game root | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |

## 1 · Verification

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet test tests/BpConvert.Tests -v q
# Approved! - failed 0, passed 79, skipped 0, total 79 - 47 s

export SOTC_LIB=$PWD/out/library
ctest --test-dir app/build --output-on-failure
# 1/2 unit_tests ......... Passed 23.24 s
# 2/2 windowed_selftest .. Passed  0.14 s
# 100% tests passed out of 2
```

`cmake --build app/build -j` rebuilt `app.cpp` and relinked the viewer cleanly.

The demo models were re-converted with the current writer (`convert --force --clips
player_a_stand,player_a_walk,player_a_run` / `--clips horse_a_add_horse_kick`) and the
outputs are **byte-identical** to the shipped library:

- `wander.glb` `87d6fdfa2a474a1570e08381be0c7fdee3f933b995ca030469f9793059da899e`
- `agro.glb` `f3cfc2eead8fa6c19a2b5cb0e22306cd3667569dab4a3df5a1dc790935952e43`

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
`s2-agro-arena-rigcheck-t000.png` (attempted full-horse view), `diag/blender-stand-t0.png`
(independent Blender 5.2.2 import/render — pose-defect evidence).

## 3 · Pose observations (phase-free)

**Stand — `s2-wander-stand-t000` vs the +5 s reference.** The reference is an upright
idle seen from behind: head above the shoulders, arms hanging at the sides, a relaxed
staggered stance (left foot back), no lean. Our still shows only the lower body: both
boots planted with a relaxed staggered stance *in kind*, but the **head sits at hip
height** (the dark hair blob beside the thighs) and the torso is folded — the arms and
uprightness are **not matched**, and the stance's left/right sign cannot be read from
this cropped front view. The subject is also cut at the top by the camera (§4.2).
**FAIL.**

**Stand — `s2-wander-stand-t250`.** The pose advances (pixel bbox 702,0,1066,452 →
606,0,1060,397); the stance stays staggered and the visible hands sit at hip height.
The reference's phase is unknown (the phase-free caveat, `re2-xval.md` §3), so no
frame correspondence is claimed; the same fold and crop remain.

**Walk — `s2-wander-walk-t050`.** No reference exists. The leg cycle is legible (one
knee bent, foot swung forward, the other leg planted) and is not obviously mirrored at
the visible joints, but the head is again at hip level and the upper body is out of
frame. **Not acceptable.**

**Run — `s2-wander-run-t050`.** No reference. A larger lift/stride than the walk, the
same folded/cropped pose. **Not acceptable.**

**Agro — `s2-agro-kick-t000` / `t025` vs the standing references.** The references show
a standing horse in 3/4 view: head up, four legs planted, saddle and bridle on. Our
stills look at the **underside of the belly**; the hooves are visible at the frame
edges and the head/neck are out of the top. The hooves move between 0 and 0.25 s (the
clip advances), but **no rig/pose comparison is possible**.

**Arena — `s2-arena-run-selftest`.** The status lines show the per-frame selection
(`s2-anim clip=player_a_stand` then `clip=player_a_run`, both `procedural=0`) and
`s3-status … pos=309.312,29.802,305.405 ground=ok frames=240`; the crop shows the feet
meeting the bowl floor. Grounding is **pass-in-kind**; the pose fold dominates. The
`--anim player_a_stand --anim-time 0` arena still shows the fold clearly.

## 4 · The blocking defects

### 4.1 The glb plays a folded pose (anti-goal: wrong pose)

The decode is upright; the exported glb is not.

`python3 tools/re/anb/pose_dump.py pose cp3shadow/anim/player_a_stand.anb --frame 0`:

```text
Bone_KOSI  y +0.7429   (pelvis)
Bone_MUNE  y +0.8343
Bone_KUBI  y +1.0462
Bone_KAO   y +1.1165   (head, +0.374 above the pelvis)
```

The converted `wander.glb`, `player_a_stand` at t=0, evaluated with standard glTF
semantics (node TRS; the animation rotation replaces the bind rotation; LINEAR):

```text
Bone_KOSI  (0,      0.7429, 0)
Bone_MUNE  (0.0224, 0.6543, -0.0221)
Bone_KUBI  (0.0472, 0.5266,  0.0152)
Bone_KAO   (0.0582, 0.4515,  0.0398)   (head, 0.291 BELOW the pelvis)
```

The chest/neck/head chain is the vertical mirror of the decode (+0.091/+0.303/+0.374 →
−0.089/−0.216/−0.291). Walk and run show the same: glb KAO 0.4139 / 0.4935 vs decode
1.0405 / 1.0611. Blender 5.2.2's independent glTF import places `Bone_KAO` below
`Bone_KOSI` too (`diag/blender-stand-t0.png` shows a character folded double, head by
the knees), so this is the file, not the viewer.

Diagnosis (to verify and fix in the follow-up): the decoded/bridged locals are composed
by `pose_dump.py` in a **row-vector** matrix convention, while the writer copies the
same numbers into glTF, where a conformant loader applies them as **column-vector**
local rotations. The converter's bind path is consistent with glTF (the T-pose is
correct; the converted IBM locals equal the node TRS), so the animation path is the
one transpose off. The fix is the transpose/conjugate of the animated locals (or the
tool's composition), validated by a world-pose test against the decoder's upright
pose; `AnimWriterTests.cs:62` currently pins the raw bridged quaternion — the bug's
test.

### 4.2 The single-model camera frames the bind pose (capture defect)

`camera_frame` runs on the bind bounds before playback. The clip's root translation
then lifts the model onto its feet (the design-approved semantics), but the camera is
not reframed:

- Wander bind bounds y ∈ [−0.7596, 0.5033] → target y = −0.1281; stand root y = 0.7429
  → feet y = −0.0167, head y ≈ 1.117 (correct pose): everything is above the target.
- Agro bind bounds y ∈ [−1.3558, 0.5381] → target y = −0.4089; kick root y =
  1.3697–1.4017: the camera looks up at the belly.

All six single-model PNGs have subject pixels touching **y = 0** (cut at the top):
`s2-wander-stand-t000` bbox `(702, 0, 1066, 452)`, `s2-wander-stand-t250`
`(606, 0, 1060, 397)`, `s2-wander-walk-t050` `(878, 0, 1236, 447)`,
`s2-wander-run-t050` `(850, 0, 1350, 347)`, `s2-agro-kick-t000` `(828, 0, 1146, 413)`,
`s2-agro-kick-t025` `(816, 0, 1156, 380)`. The arena path is unaffected for grounding
(`ground=ok`, follow camera, lift skipped for root-carrying clips).

### 4.3 The horse frame-0 rig check — not performed

The design's frame-0 check (§6.4) needs the whole horse in view and the standing pose.
The single-model still looks at the belly/underside; the arena attempt
(`s2-agro-arena-rigcheck-t000.png`, kick pinned at 0) shows the horse from below the
belly with the head/neck out of frame. **No bridge/mapping/statics check is possible**
until §4.1/§4.2 are fixed. The design's "frame 0 is its standing pose" is also still
unverified — the capture cannot separate it from the fold.

## 5 · Known limits (accepted scope)

- **No blending:** hard cuts between stand/walk/run (design §4); the switch restarts the
  clip at t = 0.
- **Foot sliding is possible:** the controller drives 2.2/5.0 m/s while the clips play
  in place (root x/z zeroed); speed-matched playback is out of scope (design §3).
- **Phase-free comparison:** the capture's animation phase is unknown and no
  frame-to-frame correspondence is claimed (`re2-xval.md` §3).
- **Source-data finger-bone gaps:** walk **track 26 `Bone_YUBI_B_R`** differs from key 0
  by **11.4°** at the last key; run **track 18 `Bone_YUBI_B_L`** by **6.7°** — a finger
  snap at the loop point in the authored data, not a converter regression.
- The horse's clip is standalone; in-arena multi-rig animation is deferred (design §3).
- Frames: stand 76 keys / 15 fps / 5.0 s; walk 21; run 18; horse kick 16.

## 6 · The user's walk — PENDING (blocked)

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

**The user's verdict: PENDING — the walk has not been performed.** It is blocked on
§4.1/§4.2: the current build (and the walk) shows the folded pose, so a run now cannot
pass.

## 7 · Evidence files (all gitignored)

Under `out/re/capture/s2/`: `s2-wander-stand-t000.png`, `s2-wander-stand-t250.png`,
`s2-wander-walk-t050.png`, `s2-wander-run-t050.png`, `s2-agro-kick-t000.png`,
`s2-agro-kick-t025.png`, `s2-arena-stand.png`, `s2-arena-run-selftest.png`,
`s2-arena-stand-pinned.png`, `s2-agro-arena-rigcheck-t000.png`, the matching `*.log`
files, `diag/blender-stand-t0.png`, `thumbs/`.

## 8 · Cross-references

- Design: `docs/superpowers/specs/2026-10-19-s2-motion-design.md` (§2 goals, §4
  anti-goals, §6.3 grounding, §6.4 acceptance).
- Plan: `docs/superpowers/plans/2026-10-19-s2-motion.md` (Task 7).
- Decode/cross-validation: `docs/notes/re2-xval.md` §2–§3 (the bridge and the
  phase-free pose comparison), `docs/notes/re-anb.md` §7.
- Reference captures: `docs/notes/re-capture.md` §3; pairing legend
  `out/re/capture/compare/README.md`.
- S3 arena / grounding: `docs/notes/s3-results.md` §2, §5.
