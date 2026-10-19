# RE-2 T5 — capture cross-validation: the decoded pose vs the port's reference stills (2026-10-19)

Semantic ground truth for the decode: `player_a_stand.anb` is decoded to per-joint
transforms, composed on the converted skeleton, and compared — feature by feature —
with C2's captured reference stills (`out/re/capture/`, the idle trio +5/+10/+15 s).
The comparison is **data-level + visual** (a projected stick figure beside the
reference), not an engine render: the app cannot play decoded clips yet.  This file
is **new**; `re-anb.md`, `re2-recon.md`, `re2-keys.md` and `re2-flagset.md` are
untouched.

| | |
|---|---|
| Date | 2026-10-19 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `re2-xval` (base `69bbe1f` = the RE-2 T2 tip) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Inputs | `cp3shadow/anim/player_a_stand.anb` (8,424 B; 76 frames, 29 tracks) + sibling `cp3shadow/skel/player_a.skb` (29 records) + the converted skeleton `out/library/models/shadow/characters/wander/_cmn/wander.glb` (348 joints) |
| Reference stills (read-only) | C2's `idle-anchored` trio (+5/+10/+15 s) and `horse3` (+42 s) under `out/re/capture/compare/`; provenance in `docs/notes/re-capture.md` §3, `compare/README.md` |
| Tools | **`tools/re/anb/pose_dump.py`** (new: decode / pose / render / measure / verify) — imports `parse2.py` (T1's flag-clear decode) and `probe.py` (`.skb` reader, sibling/glb finders); neither is modified |
| Artifacts (gitignored) | `out/re/xval/`: `compare-idle-f0.png` and `compare-idle15-f0.png` (reference + stick figure), `pose-strip-back.png`, `pose-strip-side.png`, `decode-f0.json`, `pose-f0.json`, `measure.json`, `verify.json` |

## 0 · What is compared, and what is not

**Compared.** The pose of `player_a_stand` (the stand/idle loop) at a *known frame*
(frame 0 of 76), plus the loop's invariants across all 76 frames, against the
features readable in the reference stills: stance width, foot stagger and its sign,
the hands' height and lateral position, uprightness, and the planted-foot behavior.

**Assumptions (stated, not hidden).**

1. **The capture's animation phase is unknown.** The trio shows Wander in the
   input-free idle, but which of the 76 frames the game was showing at +5/+10/+15 s
   is not recoverable from the stills.  The comparison is therefore **phase-free**:
   the loop's invariants are compared with the stills' features, and frame 0 is
   shown as a representative for the visual pairing.  This is the honest core
   limit of the whole exercise.
2. **The clip identity is inferred.** `player_a_stand` is taken to be the live
   clip (the game's own opening state, no input); the `player_a_stand_*` fidgets
   exist and cannot be excluded from the stills alone.
3. **The cameras differ.** The reference is a third-person perspective view from
   behind and slightly above; ours is an orthographic oblique projection
   (`v = y + 0.25 z` in the back view, mimicking the capture's shallow downward
   tilt so the stagger reads).  Image measures are projected quantities — the
   numeric ratios carry a tolerance; the semantic features (limb directions, the
   stagger's sign, uprightness) are the robust part.
4. **Joint positions vs silhouette.** The model side measures *joints*; the image
   side measures the *silhouette* (the mesh is wider than the joints).  Only
   features where the two are comparable are quantified (feet, hands, height).

## 1 · The decode (what T1 pinned, applied to this clip)

The clip's 29 tracks map to the sibling `.skb`'s 29 records **by index** (both are
the same part bank in file order; the L/R pairs are mirror-symmetric in the decoded
data — measured, §2).  Per-track local quats come from T1's per-type formats;
the root's per-frame position comes from the pre-region series.  Measured on this
clip: every keyed track carries exactly `frames` keys (76), every extent equals
`keys x stride`, **19/19 keyed tracks loop** (first == last key bytes), and the
minimum adjacent-key dot over all 1,425 pairs is **0.9800**.

The world pose composes the animated local rotations over the converted skeleton's
bind locals (from the glb's inverse bind matrices; the bind local translation is
kept).  The composition is exact: rebuilding the bind pose through the same
machinery reproduces the glb's world matrices to **1.1e-15**.

## 2 · The bridge — the convention finding (new)

The clip's quats are **not** in the glb's local frames.  Measured, the fix is one
uniform map: **`q_glb_local = conj_i(q_anb) = (x, -y, -z, w)`** (conjugation by the
180° rotation about x).  Three independent pieces of evidence:

1. **The `.skb`'s second quat `q2` is the game's own bind local in the other
   convention.**  For every one of the 29 records, `conj_i(q2)` equals the glb's
   bind local within **14.6°** (26/29 within 1°; median 0°; the outliers are
   finger records, ≤ 14.6°).  Where the
   conjugation is ~identity (legs, spine, head — 180° rotations about yz-plane
   axes) `q2` matches the glb directly; where it is not (KATA/JOWA/HAND_A/
   OYAYUBI_A: `ang(q2, glbL)` = 176–180°; TOE: 40.5°) the conjugation is exactly
   what closes the gap.
2. **The decoded clip quats match `q2`'s convention, not the glb's** (e.g. the
   frame-0 KATA_L static is 23.8° from `q2` but 170.8° from the glb bind local).
3. **The bridged pose is semantically right and symmetric**: the arms hang at the
   sides instead of crossing the body (hands at ±0.20·height, hip height), the
   feet keep the bind's stagger, and the L/R pairs mirror (e.g. the hands differ
   by 0.025 after mirroring).

With the bridge, the frame-0 residuals vs the glb bind local are idle-plausible
everywhere: the core chain (legs/spine/head) ≤ **34.99°** (MOMO_R), the arm chain
≤ **57.39°** (JOWA_R, the upper arm on its way down from the T-pose bind).  The
eight **finger statics** sit **39.5–99.2°** off the straight-fingered T-pose bind
along the bones' own bend axes — the rig's **relaxed-hand default**, not a
convention error; they are applied as-is (pinned by `verify`).

Without the bridge the same composition produces crossed arms and a torn-up
stance — the bridge is what makes the decode a *pose*.

## 3 · The comparison (quantified)

**Model (frame 0; height = head joint − lowest foot = 1.0915 units):**

| feature | value |
|---|---|
| stance — toe joints (x) | 0.1583 → **0.145 · height**; over the loop [0.1439, 0.1458] |
| stance — toe tips (x) | 0.2271 → **0.208 · height** |
| foot stagger (toe-tip z) | **0.2113** (the left foot back, the right forward) |
| planted foot (left) z range over the loop | **0.0007** (planted); the free right foot 0.1246 |
| feet at ground level (y) | 0.025 / 0.029 |
| hands (x, y, z) | (+0.222, 0.627, −0.017) / (−0.197, 0.624, −0.017) → height **0.551 · height**, lateral **0.203 · height** |
| uprightness | head (0.014, 1.117, 0.050) vs hip (0.016, 0.743, 0.003): dx −0.0017 |
| root series y | 0.7427 … 0.7473 (the pelvis above the ground) |

**Reference stills** (the silhouette method pinned in the tool: brightness > 60,
saturation < 25, largest 4-connected component, feet = the bottom 12 %; window
`1000,420,1420,960`):

| still | height (px) | feet sep / height | outer foot span / height | lower foot |
|---|---|---|---|---|
| idle +5 s | 386 | 52.6 px → **0.136** | 90 px → **0.233** | **image-left** (8 px) |
| idle +15 s | 385 | 42.4 px → **0.110** | 77 px → **0.200** | tie |
| horse3 +42 s | 407 | 46.5 px → **0.114** | 95 px → **0.233** | tie |

(The +10 s still is polluted by Agro entering the window — height reads 486 px —
and is excluded.  The back-view convention is pinned in the tool: a viewer behind
Wander sees his left on the image left, so the lower/nearer foot at +5 s being the
image-left one is *his left foot back*.)

**The verdict, feature by feature:**

- **Stagger sign — MATCH.** The decoded loop keeps the left foot back / right
  foot forward (the toe-tip z differ by 0.21); the +5 s still's lower (nearer)
  foot is the image-left one = his left.  (Caveat: the depth cue is weak — 8 px
  at +5 s, a tie in the other two stills — so the sign rests on one frame.)
- **Stance width — MATCH within the measurement's resolution.** The model's
  outer foot span (toe tips) is 0.208 · height, inside the reference's
  0.200–0.233; the mid-foot (toe-joint) stance 0.145 sits just above the
  reference's silhouette-center readings 0.110–0.136 — the silhouette band biases
  inward (it includes the lower shins) and the reference's own spread across the
  trio is ±12 %.
- **Hands — MATCH.** Height 0.551 · height in the model; in the +5 s still the
  right fist (a clear dark blob at the sleeve's end; the image-left side is
  confounded by the poncho's drape) reads at ≈ 0.55 · height above the ground,
  at ≈ 0.23 · height laterally (the model: ±0.203).  The arms hang down at the
  sides in both, elbows slightly bent.
- **Uprightness / height — MATCH.** Head above the hips, near the hip axis, in
  both; no crouch, no lean.
- **Plantedness — MATCH in kind.** The idle keeps one foot planted (left, z range
  0.0007 over the whole loop) while the body sways (root z range 0.067, head
  0.124) — the free right foot swings with the sway, which is why the stills'
  stagger cue varies between frames (the phase-free caveat, made visible).

**So: the decoded pose matches the capture semantically** — same upright idle,
arms hanging at the sides at hip height, the same staggered stance (left foot
back), the same stance scale within the image measurement's resolution.  What the
comparison *cannot* do is pin the phase (no frame-to-frame correspondence) or
validate the fine hand/finger shape at this camera distance.

## 4 · Verification (the tool's pinned assertions)

`python3 tools/re/anb/pose_dump.py verify` — exit non-zero on failure; **17/17
pass** on this clip.  The gate: the T1 decode invariants (76 keys, extents, 19/19
looped, dots ≥ 0.95 — measured 0.9800); the composition's bind reconstruction
(≤ 1e-4 — measured 1.1e-15); the **bridge cross-file** check (`conj_i(q2)` vs the
glb bind local ≤ 20° — measured 14.6°); the bridge residuals (core ≤ 40° —
34.99°; arm ≤ 60° — 57.39°; the finger statics in the relaxed-hand band [30, 110]
— 39.5–99.2°); the pose semantics across the loop (the planted foot ≤ 0.005 —
0.0007; the free foot ≤ 0.15 — 0.1246; both feet at ground level; the stagger
≥ 0.15 — 0.2113; the hands at the sides; upright; the root series y in
[0.73, 0.76]); and re-decode determinism (byte-identical).

The reference-side measures are **not** gated (the stills are a machine-local,
gitignored tree the gate must not depend on); they are reproduced by
`measure --ref ...`, whose method is pinned in the docstring.

## 5 · What this changes / open items

- **The decode now has semantic ground truth for one clip.**  The T1 key formats,
  the track→bone mapping, the root series, and the frame composition produce a
  pose that matches the port's capture of the same idle.  RE-1 §5's gate language
  ("decode not confirmed") is about the *file-level parse*; this pass confirms the
  *pose semantics* for `player_a_stand` — a step toward the gate's intent, not a
  re-run of the gate itself (T4's job).
- **New: the `.anb`↔`.cskl` frame bridge** (`conj_i`) and its cross-file
  validation through the `.skb`'s `q2`.  Any future consumer (converter, app) must
  apply it when animating converted glbs from `.anb` data.
- **Open:** the capture's phase (needs a game-side frame counter or a
  clip-matching run — not available statically); the clip identity (fidgets);
  the fine hand pose at this camera distance; the arm chain's JOWA residual
  (57°) is idle-plausible but the largest in the chain; the free foot's 0.125
  sway is taken as animation (it is the *free* foot) — a horse-set or
  second-clip comparison would broaden the evidence.

## 6 · Reproduction

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/anb/pose_dump.py verify                       # 17/17, exit 0
python3 tools/re/anb/pose_dump.py decode cp3shadow/anim/player_a_stand.anb --frame 0
python3 tools/re/anb/pose_dump.py pose   cp3shadow/anim/player_a_stand.anb --frame 0 --json out/re/xval/pose-f0.json
python3 tools/re/anb/pose_dump.py render cp3shadow/anim/player_a_stand.anb --frames 0 --view back \
  --out out/re/xval/compare-idle-f0.png \
  --ref <capture>/ref-idle-anchored-CUSA08809_20261018_234236_562_game_000000.png \
  --ref-window 1000,420,1420,960
python3 tools/re/anb/pose_dump.py measure cp3shadow/anim/player_a_stand.anb \
  --ref <capture>/ref-idle-anchored-..._000000.png --ref-window 1000,420,1420,960 \
  --json out/re/xval/measure.json
```

(`<capture>` = the C2 tree, `out/re/capture/compare/` of the main checkout; the
stills are read-only inputs, never modified.)  `render`/`measure` need Pillow
(the repo's dev-only capture tooling already uses it); `decode`/`pose`/`verify`
are stdlib-only.

*Personal-use fan project. No game assets are distributed from this repository.*
