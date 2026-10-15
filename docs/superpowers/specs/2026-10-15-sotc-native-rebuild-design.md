# Shadow of the Colossus — Native Apple-Silicon Rebuild (SOTC-Native)

**Design document · 2026-10-15 · v1.1 (reviewer-checked — awaiting your approval)**
Repo: `~/Downloads/sotc-native` · Game data: `~/Downloads/SOTC-extract/` (read-only)

---

## 1 · Intent

Rebuild **Shadow of the Colossus (2018, Bluepoint Games)** — from the extracted content of the
PS4 release `EP9000-CUSA08809_00-SOTC0000000000EU` — as a **native Apple-Silicon application**:
the same game, with remaster-grade presentation, running natively on MacBook Pro (M3 and newer).

The game's full asset tree is already extracted and verified (see `SOTC-extract/README.md`):
130,155 files / 14.39 GB in the original game layout (`characters/`, `cp3shadow/`, `models/`,
`textures/`, `sound/`, `virtualgeometry/`, …), plus the original package kept for provenance.

**Why native, not emulated:** the emulator route runs the original PS4 binary through translation —
it can play the game, but it cannot remaster it (no control over rendering, no asset pipeline
control, no longevity guarantees). A native rebuild owns the presentation layer, which is exactly
what "remaster" means here. *(User decision.)*

## 2 · Goals

- **G1 — Native runtime.** arm64-only, Apple Silicon, no Rosetta / x86 components in the product
  path. Engine: **Godot 4.x** (Metal). Approved.
- **G2 — Remaster presentation.** ≥60 fps, high resolution, upscaled textures (staged: hero
  content first — D8), modern lighting/shadows/AA — visibly beyond the PS4 original.
- **G3 — The same game.** 16 colossi, the map, Agro, stamina/climbing, HUD, music — 1:1 intent,
  no new gameplay concepts.
- **G4 — Something real at every step.** Each milestone ends with a runnable, visible artifact on
  the target Mac (screen-able, screenshot-able).

## 3 · Non-goals (for now)

Multiplayer; new content/DLC; Windows/Linux/console targets (Apple Silicon first); behavioral
perfection in early milestones; rewriting existing community parsing tooling.

## 4 · Anti-goals (failures to avoid)

- **A1** Endless converter polish before anything renders — M1 must land early.
- **A2** Building "the whole game" before the vertical slice proves itself.
- **A3** Long-lived x64/Rosetta or Windows-only dependencies anywhere in the product path.
  (Dev-time, one-off use of such tools as verification oracles is allowed — see §5. The product
  must build, run and update without them.)
- **A4** Distributing game assets — personal use only; this repo carries code + docs, never game
  data.
- **A5** Silent scope creep (engine rewrites, new features) — Godot is the tool, not the project.

## 5 · Environment & constraints

- Dev machine: **MacBook Pro M3 Max, 36 GB, macOS 27**; toolchains present: .NET 10, clang/cmake,
  Rust, Python 3. Godot 4.x to be installed (first M1 task).
- Data source: `SOTC-extract/extracted/game/` (130,155 files), plus `uroot/`, `pkg-meta/`;
  integrity manifests in `SOTC-extract/logs/*.sha256`. **Read-only.**
- Tooling allowed: community libraries (parse-only), Ghidra (+ MCP), REA, Context7; up to
  **4 parallel subagents** for independent workstreams.
- Dev-only tooling: x64/Windows tools are allowed **one-off** for development, verification and
  comparisons (e.g., community-converted reference assets, Windows-only utilities in a VM). They
  are never required by the product path (A3).
- Reference rig: shadPS4 runs on the target Mac as a **dev-only reference player** for look/feel
  tuning and capture comparisons; user-provided captures add ground truth. Never part of the
  product path.
- Legal: personal use; no redistribution of assets or derivatives; code licensing stays
  GPL-compatible where we link GPL code (PSO2-Aqua-Library is GPL-3.0).

## 6 · Decisions made

| # | Decision | Who | Rationale |
|---|----------|-----|-----------|
| D1 | Native rebuild is the goal; emulator is not in the build | User | emulator can't remaster; optional stopgap only |
| D2 | Engine = **Godot 4.x** | User (builder proposal) | arm64-native, Metal renderer, glTF-native import, fast iteration |
| D3 | Target hardware = **Apple Silicon, M3+** | User | |
| D4 | Conversion emits **glTF 2.0 (.glb) + PNG** — not FBX | Builder | avoids Autodesk FBX SDK native dependency; Godot/Blender/Unity import glTF natively |
| D5 | Model parsing = **AquaModelLibrary.Core** `BluePoint` module (community-proven for `cmdl/cmsh`); our tools wrap it and write glTF ourselves | Builder | don't rewrite parsers; bypass their FBX/FBX-SDK export path |
| D6 | Unknown formats (`.anb`, terrain/`virtualgeometry`, resource maps) = reverse-engineer with Ghidra/REA as needed | Builder | user-supplied toolboxes |
| D7 | Gameplay fidelity = reference-tuned re-implementation; the game's own data/scripts mined first; binary-level RE only where data runs out; M3 bar = "recognizably SOTC", not frame-exact | User | keeps M3 achievable |
| D8 | Texture remaster = staged AI upscale (hero content first, world later) | User | visible remaster wins early; GPU cost staged |
| D9 | Original audio required (own recon workstream; labeled capture-grade fallback if formats resist) | User | |
| D10 | Dev reference rig = shadPS4, dev-only, + user captures if provided | User (default) | tuning/verification ground truth; never in product path |

## 7 · Source material facts (for the builder)

- Original folder layout is preserved (critical: `cmdl/cmsh` resolve materials and skeletons from
  the same tree by path — the converter must run against the tree root).
- Key content: `models/` + `characters/` (incl. `models/shadow/characters/wander/_cmn/wander.cmsh`);
  `cp3shadow/` (game data: `anim/*.anb` ×4,455; `data/fs/stage/*.fs`; …); `textures/` + `.ctxr`
  textures (7,334), `.cmat` materials (10,365), `.cskl` skeletons (86), `.csdr` shaders (34,392),
  `sound/`; `virtualgeometry/`.
- Container magics observed (consistent across the tree): `.anb/.clb` `78 66 66 00`; `.npe`
  `3E BE 7B xx`; `.cmsh` `0002/0005` variants; `.cmdl` `FB AD 98 97 …`; `.ctxr` `CC 0A …` /
  `8C 0A …` (8C 0A = Oodle "Mermaid" compressed block).
- `eboot.bin` is a (fake-signed) **SELF** — unwrap to ELF before RE tooling (REA does not accept
  SELF; standard PS4 SELF tooling does).
- Resource mapping worth parsing early: `workspaces/*` (`.cwks` dependency manifests, referenced
  from the game's command line) + `cp3shadow/data/fs/stage/*.fs` — the road to "what composes
  arena #1".
- Data-driven hints worth mining: colossus entity scripts
  (`scripts/…/colossi/colossus_01_minotaur_a*.cgpr`), path data (`cp3shadow/path/*.path`),
  camera data (`camdata/`), character data (`.cchr`) — much of the game's behavior may be
  data-convertible rather than binary-only.

## 8 · Architecture

```
[extracted game tree]
      │  (M0) bp-convert — .NET CLI, arm64
      ▼
[asset library: .glb + .png + catalog.json]   (resume-able batch over 130k files)
      │  (M1+) Godot 4.x project — Metal, 60 fps
      ▼
[native app on Apple Silicon]
```

- **bp-convert**: wraps `AquaModelLibrary.Core` (pinned commit) for parsing; resolves
  `cmat/cskl/ctxr` references by path within the game root; writes `.glb` (geometry, skinning,
  materials, multi-UV) + `.png` textures; per-file status in `catalog.json`; verification output
  (counts, sizes, screenshot sheets).
- **Pipeline contract (offline-first):** everything the runtime needs is converted offline to
  engine-native assets; the app never parses original game containers at runtime. New data
  categories (animations, collision, arena composition, game tables) each get an offline
  converter; a runtime loader for an original format requires an explicit documented exception.
  *(Builder call after reviewer check.)*
- **Oodle**: payloads may embed Oodle *Mermaid* blocks. Must decode on macOS without Windows
  DLLs: validate early on a Wander file; integrate a portable decoder (ooz-class C/C++) if the
  library can't handle it.
- **Godot project**: import `.glb`; materials mapped to Godot shading; review scenes with
  free-fly camera + perf HUD; later streaming/LOD/impostors.
- **Recon workstream (parallel)**: R1 arena composition (`fs`/`cwks`); R2 `.anb` animation
  format; R3 terrain/`virtualgeometry` mesh extraction.

## 9 · Milestones

**M0 — Converter toolchain** *(weeks)*
- bp-convert v0.1 for characters (`cmdl/cmsh/cskl/cmat/ctxr` → glb+png), Oodle spike resolved,
  batch mode + catalog over the full tree; verification samples.
- Done = Wander and ≥9 other characters convert reproducibly; catalog covers `characters/` +
  `models/`.

**M1 — See it** *(days→weeks)*
- Godot review scene: Wander (rigged, textured; animated if R2 is ready) + colossus #1 + a
  terrain slice; free-fly camera; ≥60 fps; screenshots.
- Done = you fly around Wander & Valus on this Mac and it is unmistakably them. *This milestone
  also validates the engine choice.*

**M2 — Native arena** *(weeks)*
- First arena assembled: terrain (R3), props, sky, lighting; performance pass.
- Done = full Valus arena explorable at ≥60 fps; side-by-side stills beat the PS4 original.

**M3 — Playable slice** *(months)*
- Character controller (move/run/climb/stamina/grab) + the full colossus #1 encounter (climb,
  weak point, death) + camera; minimal HUD.
- Done = you can defeat the first colossus natively — an experience recognizably SOTC,
  reference-tuned (per D7), not frame-exact.

**M4+ — The long road**: remaining colossi, Agro, world traversal/streaming, audio (own recon
workstream — formats unknown, see §10), menus/save, intro/outro. Content target = the story
campaign (start→credits) of APP_VER **01.00** as extracted; extra modes (photo mode, time attack,
gallery, stats) are an optional post-campaign stretch. *(Builder call.)*

## 10 · Risks

| Risk | Level | Mitigation |
|------|-------|------------|
| Oodle Mermaid decode on macOS | **high** | validate on Wander `ctxr/cmsh` first; portable decoder integration (ooz-class); RE fallback |
| AquaModelLibrary.Core builds/runs on arm64 | medium | parse-only subset; build spike first; upstream fixes |
| `.anb` animations lack a public parser | **high** | R2: Ghidra/REA on `eboot.bin` + libraries; community tooling hunt; R2 is time-boxed and scheduled before M3 content work; if unsolved at M3, ship best-available animation fidelity clearly labeled |
| Terrain/`virtualgeometry` format unknown | **high** | R3 recon; fallback: heightfield re-mesh for the M1 slice |
| Godot performance on dense arena | medium | LODs/impostors; M2 as the gate |
| Audio formats (`sound/`, ~1.9 GB: `sxd`, `sxd1/2`, `crmp`, `calt`) have no decoder path yet | medium | original audio required (D9); recon workstream at M4 entry; labeled capture-grade fallback if formats resist |
| (legal) personal-use scope | note | no asset distribution |

## 11 · Workstreams & agents

- **W1 Converter** (1–2 agents) — M0.
- **W2 Recon** (1 agent, Ghidra/REA) — R1/R2/R3 in parallel with W1.
- **W3 Engine/scene** (1 agent) — Godot project + M1 scene once assets exist.
- **W4 Verification** — shared; artifact-based evidence (files, screenshots, frame times — not
  claims).

Independent workstreams run concurrently (≤4 agents).

## 12 · What's left to the builder

- Detailed plan & task sequencing (next artifact).
- bp-convert internals (glTF writer details, material mapping, CLI shape).
- Godot import/scene conventions; material shading mapping.
- Recon specifics (first RE targets; SELF unwrap tooling).
- Catalog schema; repo conventions; local scripts (no CI).

## 13 · References

- Extraction report + hashes: `SOTC-extract/README.md`, `SOTC-extract/logs/`
- Aqua-Toolset / PSO2-Aqua-Library: `github.com/Shadowth117/{Aqua-Toolset,PSO2-Aqua-Library}`
- Community precedent (SOTC models): ReShax topic 589; ctxr tooling: ReShax topic 21
- PSARC/pak internals (already solved): `SOTC-extract/tools/`

*Personal-use fan project. No game assets are distributed from this repository.*
