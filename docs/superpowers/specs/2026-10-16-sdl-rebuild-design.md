# Shadow of the Colossus — SDL-native application + reverse-engineering design

**Design document · 2026-10-16**

Supersedes the 2026-10-15 rebuild design in exactly two places — the runtime foundation
(§6 D2: Godot dropped; the application is self-built on SDL) and the milestone ladder (§9) —
and elevates reverse engineering to a first-class, planned workstream. Everything else in the
2026-10-15 design (intent, source-material facts §7, legal, the offline-first pipeline contract
§8) is inherited unchanged and remains the reference for this document.

---

## 1 · Intent

Same goal as the original design: a native Apple-Silicon rebuild of *Shadow of the Colossus*
(2018) from the extracted PS4 data — the same game, with remaster-grade presentation, running
natively on the target Mac. What changes: **the runtime is an application we build ourselves on
SDL** (user decision, 2026-10-16), and **reverse engineering is a planned workstream** that
supplies what the shipped data alone does not answer.

**Goals**

- **GA — Native SDL application.** arm64-only macOS application on SDL3, windowed, cursor never
  captured, Metal-backed rendering (SDL_GPU).
- **GB — Something real at every step.** Every milestone ends with the app running on this Mac,
  screenshot-able (inherited G4).
- **GC — Planned reverse engineering.** Named targets, tools and outputs (below): unknown formats
  become documented formats + offline converters; unclear behavior becomes implementation notes.
- **GD — The same game.** 16 colossi, the map, Agro, stamina/climbing, HUD, music — 1:1 intent;
  reference-tuned, not frame-exact (inherited D7).
- **GE — Remaster presentation, staged.** Upscaled textures, modern lighting/shadowing — later
  tracks, never blocking the early milestones (inherited G2/D8).

**Non-goals** (inherited): emulation; new gameplay concepts; Windows/Linux/console targets;
behavioral perfection in early milestones; rewriting community parsing tooling.

**Anti-goals**

- **A-1** RE research delaying the first windowed artifact — **S1 needs zero RE.**
- **A-2** Engine systems no milestone needs (physics engine, networking, editor) — build only
  what the current milestone exercises.
- **A-3** Multi-backend rendering — Metal via SDL_GPU is the one product path.
- **A-4** Distributing game assets or derivatives (inherited).
- **A-5** Frame-exact replication of the original (inherited D7).

## 2 · Constraints

**User-given:** Apple Silicon target; SDL; **windowed mode**; **the mouse cursor is never locked
or captured** (the app behaves like a normal Mac app: alt-⌘-tab, visible cursor, no pointer
grab); up to **4 parallel agents**; RE tooling Ghidra (+ Ghidra-MCP), REA, Context7 for
SDL/library documentation.

**Inherited:** game tree is read-only; offline-first — the app never parses original game
containers at runtime, only converted assets; personal use, no redistribution; the dev-only
reference rig is the user's native port (D-S9) — never in the product path.

**Machine facts (verified 2026-10-16):** SDL3 3.4.18, CMake 4.4.4, Homebrew clang 23.1.3
installed; no Ghidra instance running yet; `uroot/eboot.bin` is still an unwrapped SELF
(magic `4F153D1F`); the only other executables in `uroot` are the SDK modules
`sce_module/libc.prx` and `sce_module/libSceFios2.prx`.

## 3 · Decisions

| # | Decision | Who | Notes |
|---|----------|-----|-------|
| D-S1 | Runtime = self-built SDL application; Godot dropped | **User** | Supersedes old D2. Windowed; cursor never captured |
| D-S2 | Language C++20, CMake, clang | Builder call | SDL3 native; toolchain verified on the dev machine |
| D-S3 | Rendering via SDL_GPU (Metal backend), Metal shaders (MSL) | Builder call | Single backend, per A-3; shader tooling validated in S1 |
| D-S4 | The app consumes the existing converted asset library (`.glb` + `.png`) | Builder call | Inherits the offline-first contract (§8 of the old design) |
| D-S5 | Vendored minimal dependencies (glTF via `cgltf`, PNG via `stb_image` or SDL_image) | Builder call | No engine framework, no package manager lock-in |
| D-S6 | RE tooling: Ghidra (headless + MCP) for binary analysis; REA for artifact interrogation/inventory; Context7 for docs | **User** (tools) + builder (how) | |
| D-S7 | Animations ship as glTF animation tracks inside the existing `.glb` pipeline | Builder call | Anticipated by `docs/notes/aqua-core-api.md` §11.2 |
| D-S8 | The application lives in this repo under `app/` (CMake project, separate from `BpConvert.sln`) | Builder call | One repo, two toolchains (.NET converter, C++ app) |
| D-S9 | Dev-only reference rig = the user's `ShadowOfTheColossus-PC` port (scripted routes, pad record/replay, screenshot tooling; no captures recorded yet); shadPS4 remains the fallback; nothing from the port enters the product path | Builder call (reviewer-raised; veto → shadPS4 only) | RE-0 reuses its `scripts/prepare.py` SELF unwrap |

## 4 · Architecture

```
[game tree, read-only]                [RE workstream: Ghidra / REA]
      │ bp-convert (.NET) + new offline converters        │ findings → format specs,
      ▼                                                   ▼ implementation notes
[asset library: .glb + .png (+ animations, scene manifests, collision/foliage as targets land)]
      │
      ▼
[SDL application  app/  — C++20, SDL3, SDL_GPU/Metal]
  window/input · GPU layer · scene graph · asset IO (cgltf) · materials ·
  skeletal animation runtime · scene-composition loader · debug HUD
```

- **Runtime modules** (grown in milestone order, not up front): platform/window + input (SDL,
  cursor-managed), GPU layer (device, pipelines, render passes), scene graph (nodes, transforms,
  draw list), asset IO (glb via cgltf, png), material mapping (converted material model → MSL
  shaders), skeletal animation runtime (S2), scene-composition loader (S3), debug overlay
  (fps/stats, from S1 on).
- **Converter** (existing `bp-convert`, `.NET`): keeps its role; extensions: animation tracks
  (S2), composition manifests (S3), collision/foliage outputs as their targets land. Same
  catalog/acceptance patterns as M0.
- **RE workstream**: answers land as durable notes in `docs/notes/` (format specs, behavior
  notes) and as converter inputs — never as app-side parsing of original containers.

## 5 · Reverse-engineering plan (targets in priority order)

Each target states what it unblocks, its tool, and its expected output. The order is
deliberate: RE-0 starts immediately and runs parallel to app work; each later target is pulled
by the milestone that needs it. Data mining precedes binary analysis throughout (inherited D7
ordering — the change is that RE is now *planned work*, not an emergency fallback).

| # | Target | Unblocks | Tool | Expected output |
|---|--------|----------|------|-----------------|
| RE-0 | Tool-up: unwrap `eboot.bin` SELF→ELF; executable inventory; Ghidra project + MCP wiring; REA inventory of the payloads | whole workstream | Ghidra, REA | loadable ELF, named functions baseline, `docs/notes/re-toolchain.md` |
| RE-1 | `.anb` animation containers | S2 | data mining first; Ghidra as needed | format spec + offline converter to glTF tracks |
| RE-2 | `virtualgeometry` **foliage/vegetation** (`.cvmg` + `vs_*` scenes) — verified 2026-10-16: *not* terrain; level ground is ordinary `.cmsh`, already converted by M0 | presentation polish (GE); **not** on S3's critical path | data mining | format spec + foliage-instancing converter (scenes exist for a subset of arenas only) |
| RE-3 | Stage composition: `cp3shadow/data/fs/stage/*.fs`, `workspaces/*.cwks`, `path/*.path`, `camdata/*` | S3 | **data mining, not binary RE** | scene manifests (instances, transforms) + behavior notes |
| RE-4 | Behavior/logic: entity scripts (`*.cgpr`), movement/camera constants in eboot/PRX functions | S4 | REA/Ghidra on demand | implementation notes for controller, camera, colossus #1 |
| RE-5 | Collision / climbable surfaces (`cclm`, `cpid`, `clb`) — **conditional**: start from the already-converted `*_collision.cmsh` meshes; escalate only if tuning shows climb/grab needs original surface flags | S4 (meshes-first fallback needs no RE) | Ghidra/REA | format spec + collision converter, or a recorded "meshes-first is enough" ruling |
| RE-6 | Shaders (`.csdr`) — **deferred**; only if a fidelity item requires the original pipeline | later | Ghidra/REA | fidelity references |
| — | Reference comparisons (port captures; shadPS4 fallback) | all milestones | dev-only rig | tuning references per milestone |

Non-negotiable ordering inside the workstream: **RE never blocks S1.** S1 renders converted
assets with our own materials; the first RE unlock needed by the app is RE-1. S3's ground
geometry already ships in the converted library (level `.cmsh` from M0); S3's critical-path RE
is RE-3 (composition).

## 6 · Milestones (revised ladder)

| # | Milestone | Done = | RE dependency |
|---|-----------|--------|---------------|
| R-0 | RE foundation | eboot unwrapped + loads in Ghidra; inventory + notes committed | itself |
| S1 | **See it (SDL)** | `app/` builds an arm64 binary; window opens **windowed**; cursor visible and free (click does not capture; alt-⌘-tab works); Wander + a prop render textured at interactive fps; fps HUD; screenshot + notes recorded | none |
| S2 | **It moves** | Wander (and ≥1 other rig) plays a converted `.anb` clip correctly vs a dev-only reference capture; conversion reproducible via the catalog | RE-1 |
| S3 | **The arena** | Valus arena explorable: already-converted level ground + props + composition from data (RE-3); ≥60 fps class | RE-3 |
| S4 | **The slice** | First colossus defeatable: controller, climb, stamina, grab + encounter (RE-4 as needed); reference-tuned | RE-4 |

Every milestone ends with a runnable artifact on this Mac and a notes file under `docs/notes/`
(screenshot + measured numbers) — the M0 acceptance pattern continues.

## 7 · Workstreams & agents (≤ 4 concurrent)

- **W1 — App (C++/SDL)**: S1 first, then the app-side of S2–S4. Never blocks on RE.
- **W2 — RE / binary (Ghidra, REA)**: RE-0 immediately; then RE-1/RE-4 pulled by milestones
  (RE-5 conditional).
- **W3 — RE / data & formats**: RE-3 composition mining + RE-2 foliage format; feeds W4.
- **W4 — Pipeline (.NET converter + glue)**: converter extensions (animations, composition,
  collision/foliage as unlocked), library versioning, acceptance scripts naming.
- Independence rule: W1 and W2 never share files; findings cross as specs/notes, not code.

## 8 · Risks

| Risk | Level | Mitigation |
|------|-------|------------|
| `.anb` resists decoding | medium-high | data-mining first; community precedent; RE-1 timeboxed; fallback: capture-derived animation, clearly labeled |
| `virtualgeometry` foliage format unknown | low-medium | presentation track (never blocks a milestone); S3 ground is already-converted `.cmsh` |
| MSL/SDL_GPU shader pipeline rough edges | medium | validate the simplest lit-textured shader as part of S1 |
| RE rabbit holes | medium | A-1 + timeboxes per target; S1 shielded; RE workstream always answers a named milestone need |
| Custom engine grows beyond need | medium | A-2/A-3; module list is milestone-driven, not aspirational |
| (legal) personal-use scope | note | no asset distribution (inherited) |

## 9 · What's left to the builder

- The implementation plan and task sequencing (next artifact: `docs/superpowers/plans/`).
- App internals: render passes, module boundaries, shader authoring/compile flow, vendored
  library pins, app-side test/acceptance scripting.
- Converter output specs for animations, scene composition, and (as unlocked) collision and
  foliage; catalog extensions.
- RE procedures: SELF-unwrap flow (validate the port's `scripts/prepare.py`), Ghidra project
  conventions, `docs/notes/re-*.md` format, REA usage patterns.
- Easing the one-toolchain-to-two transition (CMake project layout under `app/`,
  acceptance script naming).

## 10 · References

- `docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md` — the base design this
  document amends.
- `docs/notes/aqua-core-api.md` — converter/adapter contract (animation note §11.2).
- `docs/notes/m0-results.md` — converter acceptance evidence (pattern to continue).
- Dev-only reference rig: `~/code/mine/ShadowOfTheColossus-PC` (v0.57) — scripted routes, pad
  record/replay, screenshot tooling; `scripts/prepare.py` unwraps the SELF for RE-0.
- `SOTC-extract/README.md`, `SOTC-extract/logs/` — extraction facts + hashes.

*Personal-use fan project. No game assets are distributed from this repository.*
