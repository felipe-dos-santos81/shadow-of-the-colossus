# R1 — animation-playback recon: from the decoded `.anb` to playable animation (2026-10-19)

Recon pass for the **next cycle**: the decoded `.anb` data (`RE-1`/`RE-2`, gate **MET** —
`docs/notes/re-anb.md` §7) becomes playable animation in the native app. This memo records what
the decode provides today, what the converter and the app provide today, the gaps between them,
candidate approaches, a proposed task decomposition, and the questions the user decides.
**Research only — nothing here is implemented; this file is the only artifact.** Every claim
about existing code cites its path; MEASURED (re-derivable from bytes/code/notes) is marked, and
INFERRED where it is an estimate or a reading.

| | |
|---|---|
| Date | 2026-10-19 |
| Repo / branch | `sotc-native`, `anb-recon` (base `036160b` = main) |
| Read (decode) | `docs/notes/re2-keys.md`, `re2-flagset.md`, `re2-sampler.md`, `re-anb.md` §6–§7, `re2-recon.md`; `tools/re/anb/parse2.py`, `records.py`, `probe.py` |
| Read (pending merge, read-only) | `re2-xval` worktree @ `644952f`: `docs/notes/re2-xval.md`, `tools/re/anb/pose_dump.py` |
| Read (converter) | `src/BpConvert.Core/` (`Model/`, `Gltf/`, `Textures/`, `Pipeline/`, `Aqua/`), `src/BpConvert.Cli/Program.cs`, `docs/notes/aqua-core-api.md` §11 |
| Read (app) | `app/src/` (asset_io, anim, app, player, scene), `app/tests/`, `app/README.md` |
| Read (design/plans) | `docs/superpowers/specs/` (2026-10-15 rebuild, 2026-10-16 SDL, 2026-10-18 first-playable, s3-arena), `docs/superpowers/plans/2026-10-18-s2-skinning-animation.md`, `2026-10-18-s3-arena-app.md`; workspace `AGENTS.md` |
| Read (context) | `out/re/stage/valus.manifest` (main checkout), `docs/notes/re-capture.md` §3, `docs/notes/s3-results.md` |
| Re-run (read-only) | `python3 tools/re/anb/pose_dump.py verify` → **17/17 PASS** (the one executable check re-run here) |
| Grounding fetched | Context7 `/jkuhlmann/cgltf` (load buffers, accessor reads) and `/donmccurdy/gltf-transform` (animation channels/samplers: targetNode/targetPath, input times in seconds, LINEAR); the glTF 2.0 spec (Khronos registry, websearch) for the TRS-vs-`matrix` animation rule. **Godot docs were not fetched: the design's Godot path is not in play** (§5-C) |

**Status note on the two design generations.** The 2026-10-15 rebuild design (the task's design
doc) chose Godot (D2) and M1 = "Godot review scene". The 2026-10-16 SDL design **supersedes** it
"in exactly two places — the runtime foundation (§6 D2: Godot dropped; the application is
self-built on SDL) and the milestone ladder (§9)"
(`docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md:5-7`, D-S1 at `:66`). The repo follows
the SDL design: `app/` is a C++20/SDL3 viewer and S1–S3 have landed (`docs/notes/s1-results.md`,
`docs/notes/s3-results.md`). The workspace `AGENTS.md` still says "M1 (Godot 4.x review scene)
comes next — no Godot project exists yet" — **stale relative to the repo**; this memo follows the
repo (SDL) and records the divergence as an open question (§7 Q1).

---

## 1 · What the decode provides today (MEASURED)

### 1.1 The readers

| tool | variant | what it yields | status |
|---|---|---|---|
| `tools/re/anb/parse2.py` | **flag-clear** (2,994 files) | per-track **quaternion key arrays, frame-indexed** (`decode_track`, `:310`); the **root position series** (`decode_root`, `:389`); `verify` 30/30 on the full 4,455-file corpus | on main (RE-2 T1) |
| `tools/re/anb/records.py` | **flag-set** (1,461 files) | the 12-byte `{type, p1, p2}` records; `p1` = a NUL-terminated name string, `p2` = the key blob; the sampler's full per-type `DISPATCH` table (`:97`); `decode_type13_track` (`:548`) → per-frame `{a, b, c}` — **b = the quaternion channel**; `verify` 20/20 | on main (RE-2 T2) |
| `tools/re/anb/probe.py` | both | the `.skb` skeleton-bank reader (`parse_skb`, `:326`: 0x40-byte bone records with name, parent, two quats); sibling finders (`find_sibling_skb`, `:1214` — `<bank>_<state>.anb` → `../skel/<bank>.skb`); `check-curves` decodes via parse2/records | on main; **gate MET** (`re-anb.md` §7: 16/16) |
| `tools/re/anb/pose_dump.py` | flag-clear only | the **pose layer**: clip decode + track→bone mapping + the bridge + composition on the converted glb + capture comparison; `verify` 17/17 | **PENDING MERGE** (`re2-xval` @ `644952f`) |

### 1.2 Exact shapes

- **Per-track keys.** `parse2.decode_track` returns `(keys, meta)`: `keys` = a list of `(x,y,z,w)`
  quaternion tuples, one per **integer frame** (`frames` keys; exceptions measured: `0xD` may
  carry one extra zero sentinel key, `0xC` blocks carry 0/2 pad bytes — `re2-keys.md` §1). Type 0
  is a **static** unit quaternion (`meta["static_quaternion"]`), no keys. `0xD` also reports its
  decoded scalars. `records.decode_type13_track` returns per-frame `{frame, a, b, c}` rows (a/c
  vectors, b quaternion) for the `caseD_9` family.
- **The root series** (`parse2.decode_root`) = a per-frame 3-component **position** series
  (`[frames × 6 B int16 triplets][6 float32 descriptor]`), descriptor-applied; consumer-open at
  the engine level (`re2-keys.md` §3; `re2-recon.md` §2.3).
- **The seconds→frames chain** (`re2-sampler.md` §3.1–3.2, MEASURED): **`fps = rate × 30.0`**,
  `frame = seconds × rate × 30`, `duration_s = (frames−1)/fps`; the wrap is modulo `frames−1`;
  between keys the engine blends **linearly** (`re-anb.md` §6.4). `rate` = the clip's Q12 field
  (`(u16@motion+6 >> 4)/4096`, default 1.0) — measured: player **0.5**, horse **0.5**, cerberus
  0.4, tree_down 0.258, leo/camera/ath 1.0. So `player_a_stand` (76 frames) loops at **15 fps /
  5.0 s**, not 30 fps.
- **The frame bridge** (`re2-xval.md` §2, MEASURED): `q_glb_local = conj_i(q_anb) = (x, −y, −z, w)`
  — one uniform map, validated cross-file through the `.skb`'s second quat (14.6° worst, 26/29
  within 1°) and semantically (arms at the sides, staggered stance). The decoded quats are in the
  `.skb`/game convention, **not** the glb's.
- **The track→bone mapping** (`re2-xval.md` §1; `pose_dump.py:277-288`): **`.anb` track i ↔
  `.skb` record i (file order — the same part bank)**, and the record's **name** resolves into the
  converted glb's joint list by name (re-verified read-only: all **29/29** `player_a.skb` record
  names — e.g. `Bone_KOSI`, `Bone_MOMO_L` — are present in `wander.glb`'s 348-joint list).
- **The pose composition** (`pose_dump.py:341-380`): bind locals from the glb's inverse bind
  matrices; the bridged animated quaternion **replaces** the bind local rotation; the bind local
  translation is kept; the **root series replaces the root joint's (`Bone_root`) local
  translation**. The bind reconstruction through the same machinery matches the glb world matrices
  to 1.1e-15; the composed frame-0 idle matches the capture semantically (17/17 assertions,
  re-run here).
- **Which clip families are decoded:** flag-clear end-to-end (quaternion + root): `player_*`
  **967/967**, `horse_*` **270/270**, `guard_*` 252/252, `yamori_*` 89/89, `minotaur_a_*` 82/82
  (all measured flag-clear by a read-only `records.py scan`). Flag-set: `objanim_*` 1,008
  (sampled 200: all flag-set; the `caseD_9` family decodes — 198 type-13 tracks corpus-wide), and
  `camera_*` 453 — types 36/37, **not decoded** (`re2-flagset.md` §6; `records.py` type-13 path
  only). The pose layer (`pose_dump.py`) is **flag-clear only** (`parse2.parse_anb` raises on
  flag-set, `parse2.py:225-226`).

## 2 · What the converter provides today

- The CLI has exactly three verbs — `inspect`, `convert`, `report`
  (`src/BpConvert.Cli/Program.cs:11-20`); sources are `.cmsh` only
  (`src/BpConvert.Core/Aqua/AquaModelAdapter.cs:78-81`: `NotSupportedException` for anything
  else — `.cmdl` included).
- The DTO has **no animation type**: `ModelData` = meshes + materials + skeleton
  (`src/BpConvert.Core/Model/ModelData.cs:16-41`).
- The writer emits geometry/skinning/materials and **joint nodes carrying a bind-pose `matrix`**,
  no `animations` (`src/BpConvert.Core/Gltf/GlbWriter.cs:296-316`; the only animation mention is
  the comment at `:301-302`: "TRS replaces it when animation lands (M3)"). Wander's output:
  `out/library/models/shadow/characters/wander/_cmn/wander.glb`, 5.0 MB, 348 joints (measured on
  disk).
- The adapter contract already anticipates the substitution: "when animation lands (M3) the
  joints should carry TRS (and animation channels) instead, which is a mechanical substitution
  (`matrix` ↔ translation/rotation/scale)" (`docs/notes/aqua-core-api.md` §11.2 addendum).
- **Nothing in `src/` reads `.anb` or `.skb`** (grep: zero mentions outside that one comment).

## 3 · What the app provides today

- Load path: cgltf (`app/src/asset_io.cpp`). `load_skin_into` (`:140-188`) builds `SkinCpu`
  joints — parent, rest TRS (decomposed from each node's `matrix`), inverse bind matrices.
  `load_animations_into` (`:194-286`) parses **glTF animation tracks**: channels whose target is a
  skin joint become `AnimationChannelCpu` (translation/rotation/scale; LINEAR→0, STEP→1);
  non-joint targets are skipped and counted; **CUBICSPLINE refuses the whole load** (`:217-221`).
- Playback: `animation_sample` (clamped ends; LINEAR = lerp, rotations via `quat_slerp`; STEP =
  hold) and `animation_pose` (CPU joint palette `world × inverseBind`, uploaded to the GPU)
  — `app/src/anim.cpp:48-174`. The app's loop wraps by the clip's duration
  (`animation_loop_time`, `:37-46`).
- **The stand-in**: `make_procedural_wave` (`app/src/anim.cpp:176-198`) — a 2-second clip that
  tilts the first ≤12 joints 15° about X; `Playback` puts it **first** in the clip list whenever
  the model has a skin, so it is the default (`app/src/app.cpp:133-152`); `--anim` /
  `--anim-time` select/pin clips; the `s2-anim clip=… joints=… procedural=1 t=…` status line
  reports it (`:175-184`). README: "A model whose source has a skeleton starts playing
  `procedural-wave` … so the skinning path is demonstrable **before the converter emits real
  clips**" (`app/README.md:69-73`).
- Pins: `app/tests/test_asset_io.cpp:389-393` asserts wander's `scene.animations.empty()` —
  i.e. **today's glbs carry no clips by design**, and the test must change when they do.
- The S3 arena (delivered, `docs/notes/s3-results.md`; done-line awaits the user's walk): the
  player is moved as a **rigid model matrix** (`apply_player_transform`, `app/src/app.cpp:224-230`)
  over a ground-follow controller (`app/src/player.h`); the S3 plan pinned the animation scope as
  "S2's delivered clip + labeled stand-in; **no locomotion clip selection**"
  (`docs/superpowers/plans/2026-10-18-s3-arena-app.md` Global Constraints).
- The milestone that is open: **S2 "It moves"** — done-line "Wander (and ≥1 other rig) plays a
  converted `.anb` clip correctly vs a dev-only reference capture"
  (`docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md:130`; first-playable §4 pins the
  demo: "Wander's idle (`player_a_stand`) plus the horse"; §5 pins the capture workflow). There is
  no `s2-results.md` yet.

## 4 · The gaps (numbered, each with evidence)

**G1 — No `.anb`/`.skb` reader in the converter.** `src/` has no animation code (grep, §2); the
readers are Python RE tools (`tools/re/anb/`) outside the product path. The converter is .NET and
must gain the decode (or a documented exception, which the offline-first contract allows only
explicitly — `2026-10-15` design §8).

**G2 — Track-index → joint mapping is not implemented outside the pending pose tool.**
`pose_dump.py:277-288` does it (track i ↔ `.skb` record i ↔ bone name → glb joint); the converter
has no `.skb` reader and no notion of a clip's track order. The `.skb` record parser exists only
as `probe.py:326` (RE tool).

**G3 — The bridge `conj_i` is not in any converter/app code.** Pinned by `re2-xval.md` §2 and
applied only by `pose_dump.py:96` (the `BRIDGE` lambda). Any future consumer must apply it when
animating converted glbs from `.anb` data (`re2-xval.md` §5, verbatim).

**G4 — Frame-indexed keys → glTF time.** glTF samplers carry **times in seconds** (Context7
gltf-transform: `getInput`/`setInput` — "Times for each keyframe, in seconds"); the decode
provides integer frames plus the `fps = rate × 30` chain (`re2-sampler.md` §3). Nothing converts
`key i → i/fps` today, and nothing reads the rate outside the RE notes.

**G5 — Static tracks (type 0) are load-bearing and easy to drop.** They are the rig's
relaxed-hand defaults: 8 finger statics sit **39.5–99.2°** off the straight-fingered T-pose bind
(`re2-xval.md` §2; pinned in `pose_dump.py` verify). A converter that only emits keyed tracks
would show straight-fingered T-pose hands.

**G6 — The root series' consumption is open at the engine level.** Decoded as the root
**position** series, but "the engine's sample base for the root is open" (`re2-keys.md` §3;
`re2-recon.md` §6 item 4). The pose layer applies it as the root joint's local translation and
the pose validates (`pose_dump.py:366-377`), but that is the RE-side consumer, not the engine's.

**G7 — Clip selection/state.** The game's state machine is decoded only at the mechanism level
(`FUN_00636650` writes the frame fields — `re2-sampler.md` §3.4); no state→clip mapping exists.
The app has manual clip switching only, and S3 explicitly excluded locomotion selection. The
converter has no policy for *which* clips belong to *which* model.

**G8 — Clip association and scope.** 4,455 `.anb` live in `cp3shadow/anim/`; the naming rule
(`<bank>_<state>` → sibling `../skel/<bank>.skb`) is the association (`probe.py:1214`). The
converter has no clip-scope concept; "bake all 967 player clips into wander.glb" is a size
decision, not a given (INFERRED estimate: `player_a_stand` alone is ≈30 KB of animation data —
19 keyed × 76 keys × 16 B + times + statics + root; ~29 MB if all 967 clips averaged that size —
arithmetic from the measured key counts, not a run).

**G9 — The playback layer's defaults.** The app plays glTF clips already, but its default is the
stand-in (`app.cpp:142-152`), its `animations.empty()` pin assumes no clips
(`test_asset_io.cpp:393`), and its status line marks `procedural=1`. Once real clips land these
flip; the stand-in's fate (fallback vs removed) is a choice.

**G10 — Flag-set/object and camera clips are not playable end-to-end.** Only the `caseD_9`
family's B (quaternion) channel is decoded by `records.py`; the A/C channels are decoded per
frame but not integrated anywhere; camera types 36/37 are not decoded at all
(`re2-flagset.md` §6). For the S2 demo (player/horse) none of this is needed.

**G11 — glTF conformance for animated joint nodes.** The writer emits joint nodes as `matrix`
(`GlbWriter.cs:303-306`); the glTF 2.0 spec: "When a node is targeted for animation … only TRS
properties MAY be present; `matrix` MUST NOT be present" (Khronos registry, Nodes chapter —
fetched). Animated joints must switch to TRS (the mechanical substitution the adapter notes
already describe, `aqua-core-api.md` §11.2). The app tolerates `matrix` today
(`node_local_trs`), but conformant loaders would not accept animated `matrix` nodes.

## 5 · Candidate approaches

### A — Bake to glTF animation tracks in the model's glb (recommended)

Converter-side: decode each `.anb` (flag-clear first), apply the bridge, map tracks→joints via the
sibling `.skb`, emit one glTF animation per clip — rotation channels for keyed **and static**
tracks, a translation channel for the root series, times `i/fps` at `fps = rate × 30`, LINEAR
interpolation — and switch animated joint nodes from `matrix` to TRS (G11). The app's existing
loader, sampler, palette, and clip switching play such clips as-is; only the default
selection/pins change (G9 → T5).

- **Pros:** matches the pinned decision D-S7 ("Animations ship as glTF animation tracks inside
  the existing `.glb` pipeline", `2026-10-16-sdl-rebuild-design.md:72`); one asset per model; the
  app's existing tests/loader cover it; the converter's own SharpGLTF reload tests extend
  naturally; glTF is inspectable and validated by conformant tooling.
- **Cons:** glb regeneration per clip-set change; size grows with clip count (G8) — needs a
  scope policy; the clip→model association needs a manifest/CLI input; `matrix`→TRS is a writer
  change touching the M0-pinned joint output (`tests/BpConvert.Tests/GltfWriterTests.cs:116` pins
  the emitted `matrix` values; `SkinningTests.cs` pins the joint transforms).
- **Grounding fetched:** Context7 gltf-transform — `AnimationChannel.targetNode/targetPath`
  (`translation`/`rotation`/`scale`), `AnimationSampler` input "times for each keyframe, in
  seconds", LINEAR; Context7 cgltf — `cgltf_load_buffers` + `cgltf_accessor_read_float` (the
  app's exact read path). Plus the glTF 2.0 spec's TRS rule (G11).

### B — A sidecar runtime clip format (converter emits, app loads)

Converter emits a compact binary per clip (or per bank): joint indices + quats + times + rate +
name; the app gains a small loader and merges the clips into its `Playback` list.

- **Pros:** smallest payloads; no glb regeneration; easy to extend (clip sets as data); avoids
  the `matrix`→TRS churn for unanimated glbs.
- **Cons:** **contradicts D-S7** (pinned) and adds a second animation path in the app (new
  loader + tests + a format to document/version); the offline-first contract still holds (it is
  offline-converted), but it is a bigger app-side surface than A for the same milestone.
- **Verdict:** viable fallback if the glb-size or regeneration cost of A becomes a problem;
  not the first choice.

### C — Engine-native (Godot) — **recorded out of play**

The 2026-10-15 design's D2 chose Godot, but the 2026-10-16 SDL design supersedes it: "Godot
dropped; the application is self-built on SDL" (D-S1, `2026-10-16-sdl-rebuild-design.md:66`).
There is no Godot project in the repo. **Godot animation/glTF-import docs were therefore not
fetched** (the task's Context7-Godot condition is not met); if the user were to revive that path,
Godot imports glTF animations natively and candidate A's converter output would feed it — but
that is a user decision, not this memo's proposal (§7 Q1).

## 6 · Proposed task decomposition (next cycle)

Sized for the S2 done-line (real clips play in the app, compared against the capture). Each task
names a verifiable output; the exact split is the user's call.

| # | task | inputs | output | verification idea |
|---|---|---|---|---|
| T1 | **Converter `.anb` reader (flag-clear)** — port `parse2`'s per-type decode into `BpConvert.Core` (new `Anim/` module, adapter-style boundary); a CLI verb (e.g. `bp-convert anim inspect`) | `player_a_stand`, `horse_a_add_horse_kick`, the S2 demo set | decoded per-track key arrays + root series (DTO + CLI) | reproduce parse2's numbers on `player_a_stand` (76 frames, 29 tracks, 19 keyed, 1,454 quats, min dot 0.98, loops) and the horse; SOTC_GAME_ROOT-gated tests fail fast without the tree |
| T2 | **`.skb` track→bone mapping** — read the sibling `.skb` (bank naming rule), map track i → name → glb joint; count/report unresolvable names | T1, `probe.py:326` as reference | a mapping report per clip | 29/29 on `player_a`; horse; mismatches counted, never silent |
| T3 | **glTF animation emission** — writer: `animations` (LINEAR; keyed+static rotation channels; root translation; times `i/(rate×30)`), bridge applied, animated joints switch `matrix`→TRS | T1/T2, `GlbWriter.cs` | wander.glb + horse glb carrying the demo clips | SharpGLTF reload tests (channel counts, durations, unit quats, first==last on loops); app-load smoke; update the `animations.empty()` pin |
| T4 | **Clip scope + pipeline wiring** — decide/bake the demo set (player idle + horse kick), clip naming, invocation (CLI list/manifest), catalog integration | T3 | converted demo glbs + report | `bp-convert` run + catalog entries; re-run reports 0 pending |
| T5 | **App playback integration** — real clips default (stand-in becomes fallback); status line `procedural=0`; clip switching; tests updated | T4 | app run playing the decoded idle | ctest; deterministic screenshots at two `--anim-time`s differ and match the bind-pose sanity checks |
| T6 | **S2 acceptance vs the capture** — side-by-side stills at pinned times (idle trio; horse), user sign-off | T5, C2 captures (`re-capture.md` §3) | `docs/notes/s2-results.md` + screenshots | the milestone's own done-line ("plays correctly", user-approved; phase-free like `re2-xval.md` §3) |
| T7 | **(stretch) flag-set/object scope decision** — extend to the `caseD_9` A/B/C channels and/or camera decode, or record the deferral | T3 | a scope note / converter extension | the `records.py` decode path as reference; not on the S2 critical path |

## 7 · Open questions for the user

1. **Playback target.** Confirm SDL stays the runtime (D-S1) and the Godot path stays dropped —
   the workspace `AGENTS.md` still describes a Godot M1, which contradicts the repo. (If the
   answer is Godot, candidates and tasks change.)
2. **Clip scope.** S2 demo only (`player_a_stand` + the horse kick), or a broader set now (e.g.
   player walk/run + horse locomotion)? All-clips baking is an INFERRED tens-of-MB-per-model
   decision (G8).
3. **Where clips live.** In the model glb (A, D-S7), a companion animation glb, or the sidecar
   format (B)?
4. **Clip selection/state.** Is locomotion state selection (walk/run → clip) in this cycle, or
   after S3's sign-off? S3 explicitly excluded it; the state machine is only mechanism-decoded.
5. **Root motion.** Apply the root series as the root joint's translation (the xval-validated
   reading, G6) — accepting the engine-consumer-open risk — and should the app then treat clips
   as in-place (controller drives position) for now?
6. **Flag-set/object/camera clips.** Defer (recommended for S2) or include T7?
7. **The stand-in.** Keep `procedural-wave` as the fallback for models without clips?

## 8 · Non-claims (what this recon did not verify)

- **No code, tools, or game runs.** The only executable check re-run was
  `pose_dump.py verify` (read-only, 17/17). The app was **not** built or run; converter behavior
  is read from code and tests, not exercised.
- **The pose validation is one clip.** The semantic ground truth (`re2-xval.md`) covers
  `player_a_stand` only; the horse and every other family are decode-validated (norms/dots) but
  not pose-validated against captures. The capture's phase and clip identity remain open
  (`re2-xval.md` §5).
- **The `re2-xval` files are pending merge** (`644952f`, branch `re2-xval`); this memo treats
  them as the intended tip and cites them as such.
- **The engine-side root-series consumer and the flag-set A/C semantics are open** (G6/G10);
  nothing here claims they are settled.
- **The glb-size figures are arithmetic estimates** from measured key counts, not a conversion
  run.
- **Godot was not researched** (not in play); Context7 was used for the in-play glTF path only
  (cited above).
- The design-doc generations were reconciled by reading; the `AGENTS.md` staleness is an
  observation, not a repo-state claim beyond the cited files.

## 9 · Reproduction

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export PYTHONDONTWRITEBYTECODE=1
# the one re-run check (from the read-only re2-xval worktree):
python3 <re2-xval>/tools/re/anb/pose_dump.py verify          # 17/17 PASS
# the family scans quoted in §1 (read-only):
python3 tools/re/anb/records.py scan 'cp3shadow/anim/player_*.anb'
python3 tools/re/anb/records.py scan 'cp3shadow/anim/horse_*.anb'
python3 tools/re/anb/records.py scan 'cp3shadow/anim/camera_*.anb'
python3 tools/re/anb/probe.py skb cp3shadow/skel/player_a.skb
```

*Personal-use fan project. No game assets are distributed from this repository.*
