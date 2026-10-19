# S2 — "It moves": real animation playback + locomotion (design)

**Date:** 2026-10-19 · **Status:** design (pre-plan) · **Recon:** `docs/notes/anb-playback-recon.md` (R1)

## 1 · Intent

The app stops faking motion. The animation decoded from the game's own `.anb` files — the RE-2
decode, gate **MET** (`docs/notes/re-anb.md` §7) — becomes playable in the SDL viewer, and Wander's
clip follows what he is doing: standing, walking, running. This is the milestone where the decode
stops being a research artifact and becomes something to watch and move with.

It closes the SDL design's pinned **S2 done-line** — "Wander (and ≥1 other rig) plays a converted
`.anb` clip correctly vs a dev-only reference capture"
(`docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md:130`) — and, by user decision
(2026-10-19), adds **locomotion selection** on top: moving drives walk/run clips. The S3 plan's
"no locomotion clip selection" constraint was scoped to the arena milestone and is lifted here.

## 2 · Goals (the done-line)

1. **The glbs carry real clips.** `wander.glb` carries `player_a_stand`, `player_a_walk`,
   `player_a_run`; the horse's glb carries its demo clip (`horse_a_add_horse_kick`) — as glTF
   animation tracks baked by the converter (D-S7).
2. **The app plays the right clip for the state.** Still → stand loop; moving → walk loop; moving +
   Shift → run loop (the S3 controller: walk 2.2 m/s, run 5.0 m/s, `app/src/player.h`). The horse
   plays its clip standalone (the single-model viewer; §3). Clips play in place — the decoded
   root's horizontal travel is stripped (§6.2); the controller keeps driving world position.
3. **Acceptance.** The pinned idle trio + horse stills compared against the S2 capture set,
   phase-free (the `re2-xval.md` §3 framing); plus the user's live walk — moving switches the clip,
   Shift runs it. Evidence lands in `docs/notes/s2-results.md`.

## 3 · Non-goals (this pass)

- Blending/cross-fade: hard switches between loops.
- The game's transition clips: `player_a_walk_start/end*`, `player_a_run_start1/2`,
  `player_a_run_end1/2` — not baked, not played.
- Slope/turn/fall/combat variants; horse locomotion and riding (the horse is a static rig with its
  clip).
- Flag-set object clips (`caseD_9` beyond the demo needs) and camera types 36/37 — deferred
  (`docs/notes/re2-aux.md`).
- Engine-exact root-motion semantics (recon G6): the decoded root series applies as the root
  joint's local translation (the xval-validated reading); the controller drives world position.
- Speed-matched playback / foot-planting (foot sliding at 2.2/5.0 m/s is accepted for this
  milestone).
- Clip streaming, gameplay state machines beyond speed, and clip→model association beyond the demo
  set.
- Multi-rig animation inside the composed arena: the horse's clip is checked standalone (the
  single-model viewer); animating Agro in the scene (multi-rig clip support + scene work) is a later
  milestone (user, 2026-10-19).
- Root-motion-driven locomotion: the clips' horizontal root travel is not played — the controller
  owns position; a root-motion mode is a later feature.

## 4 · Anti-goals (a failure even if it runs)

- A clip that plays but is the wrong pose: dropped static tracks (straight-fingered T-pose hands —
  the statics matter, recon G5), the wrong bridge (mirrored/inverted limbs), or the wrong
  rate/fps (wrong motion speed). The capture comparison exists to catch exactly this.
- A glb that breaks conformant loaders: animated joint nodes must not carry `matrix` (glTF 2.0;
  recon G11).
- Regressing what works: the app's existing tests/pins, the arena walk, the stage.
- Docs that disagree: the design, plan, and results must state the same scope.
- Wander floating or sinking when a clip is active: the root channel and the S3 ground lift must
  compose so the feet stay on the heightmap in the composed arena.

## 5 · Constraints (user-decided, fixed)

- **SDL** is the runtime; the Godot path stays dropped (user, 2026-10-19; D-S1
  `2026-10-16-sdl-rebuild-design.md:66`). Windowed; the cursor is never captured.
- **Clips live in the model's glb** (D-S7, `2026-10-16-sdl-rebuild-design.md:72`) — no sidecar
  format.
- **Locomotion is in scope** (user, 2026-10-19).
- Offline-first: the runtime never parses original game containers; all decode happens in the
  converter (rebuild design §8).
- Apple Silicon; .NET 10 converter; C++20/SDL3 app; GPL-3.0 (no game assets or derived glbs
  committed).

## 6 · The how (the decided shape)

### 6.1 Converter — the `.anb` path

- A new `BpConvert.Core/Anim/` module behind the adapter boundary (same rule as `Aqua/`): an `.anb`
  reader (flag-clear; port the verified `tools/re/anb/parse2.py` semantics for `decode_track` /
  `decode_root`), a `.skb` reader (the 0x40-byte record bank; port `probe.py`'s `parse_skb`), a
  track map (**track i ↔ `.skb` record i ↔ bone name → glb joint**, the `pose_dump.py` mapping),
  and an `AnimClip` DTO (name, rate/fps, frames, per-joint rotation keys including statics, root
  translation keys).
- Clip scope is a converter input (a CLI list or a manifest — builder's choice). The demo set:
  `player_a_stand`, `player_a_walk`, `player_a_run` for `wander.glb`; `horse_a_add_horse_kick` for
  `agro.glb` (the converted Agro the capture set shows).
- The catalog is source-keyed; adding clips changes a glb's output, so the demo models need an
  explicit re-convert (the plan pins how: `--force` or a scope-aware key).

### 6.2 Writer — glTF animation emission

- One `animation` per clip, named after the clip's file stem.
- Channels: **rotation** for keyed *and* static tracks (statics as constant 1–2-key channels);
  **translation** for the root series; LINEAR interpolation.
- Times: `i / (rate × 30)` seconds, `duration = (frames − 1) / fps`, `fps = rate × 30`
  (`re2-sampler.md` §3.1). `player_a_stand` = 76 keys at rate 0.5 → 15 fps → 5.0 s.
- The **bridge** `q_glb = (x, −y, −z, w)` applied to decoded quaternions (`re2-xval.md` §2); the
  root series is already in the glb frame.
- **Root translation, in place.** The decoded series carries horizontal travel (walk ≈1.0 m over
  21 frames; run ≈2.5 m over 18 — not loop-closed). This milestone emits the series **raw in Y**
  (the pose-validated reading: it carries the character's ground height and the bob) with **x/z
  zeroed** (the controller owns horizontal position, §2). Root-motion playback is a later feature.
- **Animated joint nodes switch `matrix` → TRS** (the mechanical substitution the adapter notes
  already describe, `docs/notes/aqua-core-api.md` §11.2); unanimated nodes keep `matrix`.
- Expected size: tens of KB per clip (INFERRED arithmetic, recon G8).

### 6.3 App — selection by controller state

- Loading is the existing cgltf path (`app/src/asset_io.cpp` — `load_animations_into` already reads
  glTF clips; LINEAR/STEP supported).
- New: per-frame clip selection from `PlayerState`/`PlayerInput` — no movement → stand; moving →
  walk; moving + Shift → run. Switch on state change (hard cut). `--anim` remains the manual
  override.
- The **stand-in** (`procedural-wave`) becomes the fallback only for models with no clips; the
  status line reports the real clip and `procedural=0`.
- Tests: flip the `scene.animations.empty()` pin (`app/tests/test_asset_io.cpp:393`) to assert the
  clips; add a selection test (state → clip name).
- **Grounding:** the S3 ground lift (`player_ground_offset`, ≈0.7596 m for Wander) applies only
  when the active model has no root-carrying clip — a clip's root series already carries the ground
  height (the pose-validated reading), so the lift on top would float the character by ≈0.76 m. The
  single-model path is unchanged; the composed-arena stills verify the feet on the heightmap.
  (Builder-check refinement, made during planning: app-side, not converter-side — a converter-side
  rebase would sink the single-model render by the same ≈0.76 m.)

### 6.4 Acceptance

- Re-convert the demo models; run the app; capture stills at pinned times (idle trio + horse) with
  the S3 capture tooling; compare phase-free against the C2 reference set
  (`docs/notes/re-capture.md` §3). The horse's **frame 0** (its standing pose) is compared against
  the captures' standing Agro stills — a rig check (bridge, mapping, statics); the **kick** is the
  user's live check in the single-model viewer (no kick reference exists — the captures are
  standing/approach frames).
- The user's walk: move, Shift-run — the clip switches live.
- `docs/notes/s2-results.md`: the evidence, the measured numbers, the walk checklist.

## 7 · Left to the builder

- Module/API names and the internal split of `Anim/`.
- CLI flag vs manifest for clip scope.
- Switch timing (immediate on state change vs at the loop boundary) — prefer immediate; pin one in
  the plan.
- The exact still times for the locomotion record; whether the horse also gets a stand clip.
- Test list and fixture choices.

## 8 · Decisions recorded (user)

- 2026-10-19 — SDL stays the runtime; Godot stays dropped.
- 2026-10-19 — locomotion selection is in this milestone: moving drives walk/run.
- 2026-10-19 — the demo scope = the pinned S2 demo (idle + horse) extended with walk/run.
- 2026-10-19 — the horse's clip is checked standalone (the single-model viewer); in-arena
  multi-rig animation is deferred.

## 9 · References

- Recon: `docs/notes/anb-playback-recon.md` (§1 shapes, §4 gaps G1–G11, §5 candidates, §6 tasks,
  §7 questions).
- Decode: `docs/notes/re-anb.md` §6–§7 (gate MET); `re2-keys.md` (key formats); `re2-flagset.md`;
  `re2-sampler.md` §3.1–3.4 (fps chain, dispatch); `re2-xval.md` §2–§3 (bridge, mapping, pose
  validation); `re2-aux.md` (family coverage).
- Designs: `2026-10-16-sdl-rebuild-design.md` (D-S1, D-S7, §9 ladder, S2 done-line);
  `2026-10-18-first-playable-design.md` (§4 S2 demo, §5 capture workflow);
  `2026-10-18-s3-arena-design.md`.
- Plan precedent: `docs/superpowers/plans/2026-10-18-s3-arena-app.md`.
