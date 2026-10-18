# S3 "The arena" — design amendment

**Design amendment · 2026-10-18**

Amends the 2026-10-16 SDL design (§5 RE-3, §6 S3) by pinning S3's decomposition for
execution, and activates the 2026-10-18 first-playable amendment's §5 capture workflow
(RE-1's decode gate: **NOT MET**, 2026-10-18). Records the port-rig recon that shaped it.
Everything else — intent, constraints, decisions D-S1…D-S9, the RE plan's other targets,
the offline-first contract — is inherited unchanged.

---

## 1 · The milestone

**S3 "The arena":** the Valus arena composed from real game data, explorable — *you*
steer Wander: keyboard + mouse, game-style third-person camera (auto-follow + drag-to-look
nudges; the cursor is never captured), hand-tuned feel. The original's exact
movement/camera constants stay S4's RE-4 pull (no binary RE on S3's critical path).

Done = a runnable artifact on this Mac: arena ground + props + composition from data
(RE-3 manifests); you walk/run in it with the camera; ≥60 fps class; notes + screenshots
(the M0 acceptance pattern).

**Rider (completes S2's open half):** the 10-18 amendment's §5 capture workflow runs this
cycle — reference captures of Wander's idle + the horse. The S2 sign-off ("plays a
converted clip correctly") lands when a real clip exists; §4 pins what happens meanwhile.

**Builder calls pinned here (2026-10-18, flagged for veto):**

- **Extent** — the arena is the bounded walkable complex: the `arena_01` +
  `arena_01_entrance` level sets. The surrounding region is out of scope (wider map
  scales are a later milestone; the user's "first walkable place" framing).
- **Movement model** — ground-follow + arena-edge clamp; no wall/prop collision
  (A-2: no physics engine; collision is S4's RE-5 pull). Clipping is accepted for S3.
- **Animation scope** — S3 requires S2's delivered clip (the idle demo) and uses the
  labeled stand-in for locomotion. If the decode cracks during W3, an idle/walk swap is
  an allowed stretch; locomotion clip selection and the converter's `player_*`
  locomotion scope are the follow-on milestone, not S3.

## 2 · Workstreams (≤4 agents)

| # | Workstream | Delivers | RE dependency |
|---|-----------|----------|---------------|
| W1 | **App / S3** | character controller + movement + third-person camera; scene-composition loader (consumes W2's manifests); the arena assembled from already-converted ground/props | none — W2's manifests arrive as data |
| W2 | **RE-3 composition** | format spec + scene manifests for the Valus arena (`cp3shadow/data/fs/stage/*.fs`, `workspaces/*.cwks`, `path/*.path`, `camdata/*`) — **data mining, not binary RE** | itself |
| W3 | **Capture + decode continuation (RE-1b)** | the §5 capture rig + reference stills; the decode's unblocking step — the runtime's keyframe sampler in Ghidra (RE-1's B2/B3 findings); clips + sign-off if it cracks | itself |

Independence: W1 never blocks on W2/W3; findings cross as manifests/notes, never code.

## 3 · Port-rig recon (2026-10-18) — facts

- The rig (`~/code/mine/ShadowOfTheColossus-PC`, the user's fork) is a **loader-style
  native port**: the game's own x86-64 PS4 code runs on CPU; the PS4 GPU is translated to
  Vulkan by a shadPS4 fork. Not a decompilation; contains no game code or assets.
- **macOS path exists but is non-trivial**: x86-64 build under Rosetta 2 + Mesa
  **KosmicKrisp** (MoltenVK insufficient; prebuilt or 20–40 min build). A macOS acceptance
  gate exists (`make verify`: runs 30 s, checks the log).
- **Boot uncertainty**: our dump is APP_VER **01.00**; the port's patch modes target EU
  **1.01** (modes certainly won't apply). Whether 01.00 boots is unverified. Staging is
  required (the port wants `dvdroot_ps4/`-style layout + `param.sfo`; our RE-0 staging
  pattern already exists).
- **Hooks that exist**: frame dumps (`BB_FRAME_DUMP_TRIGGER` → PNG, optional overlay);
  scripted pad input (`BB_PAD_FILE`, `BB_PAD_RECORD`/`BB_PAD_REPLAY` F9; Python driver
  `tools/sotc_play.py` with scheduled events + "shot"); process diagnostics. RenderDoc
  capture is Windows-only as shipped.
- **Hooks that do NOT exist**: no pose/bone/skeleton dump, no memory-inspection hook, no
  scriptable camera, no animation viewer, no game-state API. (Unverified avenue: engine
  cvars pass through — debug/camera cvars, if the engine has them.)
- `scripts/prepare.py` builds the loader image from the plaintext SELF (`eboot.bin`) —
  it is our RE-0 unwrap step; it does not unwrap PKGs.

## 4 · The capture/fallback path, pinned (builder calls — flagged for veto)

- **Capture workflow = scripted pad route + frame dumps → pinned-time stills →
  side-by-side comparison** against the app's render (the 10-18 §5 artifact, now concrete).
  The thin macOS script drives the port when the Mac is free; it writes only into the
  port's own `out/`; saves stay untouched; dev-only, never in the product path.
- **Port bring-up is a spike with a gate**: build (Rosetta + KosmicKrisp) → `make verify`
  → boot *our* 01.00 dump → scripted idle capture. Gate fail → options: shadPS4 (D-S9's
  standing fallback), or park captures and continue decode-only. No W1/W2 dependency on it.
- **No pose hooks exist** → the decode's semantic ground truth narrows to **the Ghidra
  sampler** (B3's lead: the consumer of `FUN_0066fe60`'s outputs in `eboot`), with the
  captures serving as **visual validation** (render a candidate pose in our app, compare to
  the reference still at a pinned time).
- **Recorded stretch avenue (not promised):** the port is the user's own fork — runtime /
  loader instrumentation for decoded-structure dumps is possible, but unscoped; spike only
  if the Ghidra sampler stalls.
- **Labeled stand-in rule:** if no real clip exists when S3 ships, the demo keeps the
  procedural stand-in and S2's sign-off stays open — no hand-authored clip without a new
  user decision.

## 5 · RE-3's expected outputs

- `docs/notes/re-stage.md`: the format spec for the composition family (measured, verbatim
  values) + behavior notes.
- **Scene manifests** for the Valus arena: instances, transforms, placement — emitted by a
  new mining tool or a `bp-convert` extension, following the M0 catalog/acceptance pattern.
- The app's composition loader consumes **manifests only** (offline-first: no original
  containers at runtime).
- **If reference resolution stalls (builder call, 2026-10-18):** a bounded Ghidra pass on
  the `.fs`/workspace loader is the allowed escalation (same pattern as RE-1's "data
  mining first; Ghidra as needed"). A curated composition may appear only as a **labeled
  demo aid** — the done-line's "composition from data" must hold; if neither data nor
  Ghidra resolves it, stop and re-plan with the user.

## 6 · Constraints (inherited, unchanged)

Windowed; cursor never captured; game tree read-only; offline-first; personal use, no
redistribution; ≤4 concurrent agents; typed commits; docs are the spec; acceptance =
scripts + measured numbers; RE answers land as `docs/notes/re-*.md` + converter inputs.

## 7 · Builder-check resolutions (2026-10-18)

A cold-read planner raised four questions; all four resolved as builder calls (flagged for
veto) — no question round was needed:

1. **Arena extent** → bounded complex (`arena_01` + `arena_01_entrance`); region out of
   scope (§1).
2. **Movement/collision model** → ground-follow + edge clamp; no prop collision; clipping
   accepted for S3 (§1).
3. **Animation scope** → S2's delivered clip + labeled stand-in; conditional idle/walk
   swap allowed; locomotion selection is the follow-on (§1).
4. **Mining-stall fallback** → bounded Ghidra escalation; curated composition only as a
   labeled aid; full stall → stop + re-plan (§5).

## 8 · What's left to the builder

The three implementation plans; the manifest format's fields; movement/camera feel
constants; the capture script's exact interface; the composition loader's boundaries; the
port spike's bring-up steps and its gate's fallback choice.

*Personal-use fan project. No game assets are distributed from this repository.*
