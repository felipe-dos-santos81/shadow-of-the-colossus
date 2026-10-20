# sotc-viewer

The native macOS viewer for the converted *Shadow of the Colossus* asset library:
C++20, CMake, SDL3, SDL_GPU on Metal. It consumes only converted assets
(`out/library`: `.glb` + `.png`) — never the game tree or original containers.

**Status (milestone S4.0): the encounter — Valus stands in the arena (user walk pending).**
The app opens a resizable window, loads a converted model with its PNGs, draws
it (offscreen target → swapchain → present) at vsync, and frames the scene
bounds on load. A model with a skeleton is skinned on the GPU: the joint palette
is evaluated on the CPU per frame (`worldJoint × inverseBind`) and uploaded as a
vertex storage buffer, blended per vertex by four joint weights. A model whose
glb carries baked animation clips plays its first clip by default; the built-in
`procedural-wave` is only the fallback for a skinned model with no clips.
`--anim` selects a clip and `--anim-time` pins a time for deterministic
captures. In a composed arena (`--manifest`) the player's state drives the loop
instead — still stands, movement walks, Shift runs — with hard clip switches;
`--anim` / `--anim-time` pin a clip manually. A composed scene keeps one palette
and one playback per rig: the player's locomotion and each `entity`'s baked clip
run on independent clocks, and every mesh draws with its own rig's palette.
`--entity-time` pins an entity's clip for captures (clamped into the clip). The
arena's ground lift is skipped
while the active clip carries the root translation series (the series already
holds the ground height), so the feet stay on the heightmap instead of floating
by the model's origin-to-feet offset. A minimal capsule proxy built from the
entity's posed legs and torso stops the player walking through it; `T` toggles a
camera-only framing around the entity's chest (`--camera-target entity` selects
it for captures). Drag to orbit, scroll to zoom, press `F`
to reframe; `Space` plays/pauses, `[`/`]` switch clips in single-model mode (in
arena mode the controller owns the clip), `R` restarts. The window
title shows the fps readout. Escape or the window's close button quits.
`--screenshot` writes the same frame to a PNG.

## Build

```bash
cmake -S app -B app/build      # from the repo root
cmake --build app/build -j
```

Requires CMake ≥ 3.25 and SDL3 ≥ 3.4 via Homebrew (`brew install sdl3`). If CMake
does not find SDL3 on its own, configure with `-DCMAKE_PREFIX_PATH="$(brew --prefix)"`.

## Run

```bash
app/build/sotc-viewer --library out/library
app/build/sotc-viewer --library out/library --model models/objects/props/_cmn/obj_plank_01.glb
app/build/sotc-viewer --library out/library --model models/objects/props/_cmn/obj_plank_01.glb \
  --screenshot out/s1/smoke.png --frames 2
# Wander's baked clips play by default; pin a time for captures:
app/build/sotc-viewer --library out/library \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --anim player_a_walk --anim-time 0.5 --screenshot out/s2-walk-t05.png
```

The converted library comes from `--library DIR`, else `$SOTC_LIB`; one of the two
is required. Escape or the close button quits.

## Options

| Option | Meaning |
| --- | --- |
| `--library DIR` | converted asset library (default: `$SOTC_LIB`) |
| `--model REL` | model to load, relative to the library; with `--manifest`, the player's glb |
| `--manifest PATH` | compose the scene from a scene manifest; the player is `--model`, the static instances and animated entities come from the file (see [Manifests](#manifests)) |
| `--anim NAME\|IDX` | clip to play, by name or 0-based index (default: the model's first clip; `procedural-wave` only when a skinned model carries no clips); in arena mode it is the manual override of stand/walk/run selection; an unknown name or index exits 2 |
| `--anim-time SEC` | start paused at `SEC` seconds into the clip (wrapped into the clip's duration) — the deterministic-capture flag; it also pins the clip, so arena selection leaves it alone |
| `--entity-time SEC` | pin every manifest `entity`'s clip at `SEC` seconds (clamped into the clip's duration) — the deterministic entity capture; the entity starts paused; needs `--manifest` with an entity |
| `--camera-target player\|entity` | initial framing for captures: `player` (default) is the normal follow camera; `entity` starts in the chest-framing target mode and needs a `--manifest` whose scene has an entity |
| `--screenshot PNG` | render `--frames` frames into the offscreen target, write the PNG, exit |
| `--frames N` | frames the `--screenshot` path renders first (default 3) |
| `--width N` / `--height N` | window size in points (default 1280 × 720) |
| `--selftest` | render one frame, print `s1-status …`, exit |
| `--s4-selftest` | compose the encounter `--manifest`, pin the entity at t=0, assert the spawn is clear and the entity's feet are on the ground, walk the player into the nearest leg capsule until the collision stops it, print the `s4-status`/`s4-spawn`/`s4-entity`/`s4-walk` lines, exit; saves the final frame with `--screenshot`; needs `--manifest` (with an entity) and `--model` |

## Manifests

`--manifest` composes a scene from a line-oriented text file: the static arena
instances, the player (`--model`), and — in S4.0 — one placed, animated entity
(the encounter's Valus). The format accepts more, but framing targets the first
entity and the collision capsule table is Valus's bone names and radii for
every rig, so multiple entities are not a shipping configuration.
The committed schema is `docs/notes/scene-manifest.md`; the directives are:

```text
instance <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
entity   <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName>
spawn    <px> <py> <pz> <yawDeg>
```

`entity` is S4's addition: the ten numbers are the same placement as an
`instance`'s, and `<clipName>` names the exact clip the entity's own rig loops —
a model that does not carry it (or carries no clips) is a load error naming the
glb and the clip, never a silent rest pose. The player is rig 0; entities are
rigs 1, 2, …; each keeps its own skeleton, palette, and playback clock. The
ground heightmap is built from the static instances only, never from the player
or an entity. The encounter scene (gitignored, composed by
`tools/re/stage/probe.py compose-encounter`) is
`out/re/stage/encounter.manifest`:

```bash
app/build/sotc-viewer --library out/library --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb
```

Controls (no mouse capture: the cursor stays visible and free):

| Gesture | Action |
| --- | --- |
| Left-drag | Orbit: yaw freely, pitch stops at ±89°; in entity target mode, orbit the entity's chest (pitch ±80°) |
| Scroll | Zoom: distance clamped to [0.5, 500]; in entity target mode, [22, 30] m |
| `F` | Reframe the scene bounds (single model) / recenter behind the player (manifest mode) |
| `T` | Toggle entity target framing: orbit the entity's chest; `T` again restores the follow view without a snap. Inert when the manifest has no entity (the S3 arena) |
| `Space` | Play/pause the current clip |
| `[` / `]` | Previous / next clip (restarts the clip at t = 0); single-model mode — in arena mode the player's state owns the clip (use `--anim` to pin one) |
| `R` | Restart the current clip |
| Escape / close button | Quit |

A model whose glb carries baked clips starts playing the model's first clip
(`player_a_stand` on the converted Wander). The `procedural-wave` stand-in (a
2-second deterministic wobble of the first joints) is used only when a skinned
model carries no clips at all — so the skinning path stays demonstrable for
un-animated rigs. Without `--anim-time` the captured time depends on the
frames' wall-clock spacing. The run reports the animation state at startup and
on every clip change:

```text
s2-anim clip=player_a_stand joints=348 procedural=0 t=0.500
```

(`joints` is the skin's joint count — the palette size; `procedural=0` marks a
real baked clip, `procedural=1` the built-in fallback. A model without a
skeleton and without clips prints no such line. A composed scene prints one
line per rig: the player's as above, then one per entity — e.g.
`s2-anim rig=1 clip=minotaur_a_stand joints=238 procedural=0 t=0.000`.)

A screenshot run also prints its size and a per-channel pixel summary, e.g.

```text
s1-pixels min=15,18,0 max=177,180,188 mean=28.1,28.3,27.1
s1-screenshot path=out/s1-smoke.png width=2560 height=1440
```

`--s4-selftest` reports the encounter in the same style. It prints the `s4-spawn`, `s4-entity`,
and `s4-walk` diagnostics first and the `s4-status` line last:

```text
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=1123,237,1485,751 pos=378.187,28.430,369.028 frames=423
```

The acceptance script (`scripts/s4-acceptance.sh`) parses these lines;
`docs/notes/s4-results.md` records the measured values and the walk checklist.

## Tests

```bash
export SOTC_LIB=…/out/library   # repo convention: app tests fail fast without it
ctest --test-dir app/build --output-on-failure
```

- `unit_tests` — doctest cases (asset IO, camera math, quaternion/pose math,
  animation sampling).
- `windowed_selftest` — opens the window, compiles the shader, renders one
  frame, and asserts the `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0`
  line. `mesh.metal` must sit next to the binary (the build copies it; a bad
  shader fails this test loudly). Run it with `--model` for a skinned model to
  exercise the playback path as well:
  `app/build/sotc-viewer --library "$SOTC_LIB" --model models/shadow/characters/wander/_cmn/wander.glb --selftest`
  — it prints the `s2-anim` line and renders one skinned frame.

The `wander`/`agro` animation pins load those glbs from `$SOTC_LIB` and require a library
rebuilt with the S2 clips (a pre-S2 library fails them). Rebuild it once with:

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/wander/_cmn/wander.cmsh \
  --clips player_a_stand,player_a_walk,player_a_run --force
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/agro/_cmn/agro.cmsh \
  --clips horse_a_add_horse_kick --force
```

`--clips` bakes every named clip into every source in scope, so it is a single-model
invocation (`--under <file>`): every track must map into that model's skeleton.

## Guarantees

- **Windowed, always.** Never fullscreen; `SDL_WINDOW_RESIZABLE` from the start.
- **Cursor never captured.** `SDL_HINT_MOUSE_AUTO_CAPTURE=0` is set before
  `SDL_Init`; relative mouse mode and mouse grab are explicitly disabled (and
  never enabled anywhere). `--selftest` reports the state *queried back from SDL*,
  not flags the app assumes.
- **No Xcode Metal toolchain needed to build.** The GPU device requests MSL
  shader source (`SDL_GPU_SHADERFORMAT_MSL`), which SDL compiles at runtime:
  CMake copies `shaders/mesh.metal` next to the binary and the renderer loads it
  from `SDL_GetBasePath()`. A shader that cannot be read or compiled aborts the
  run with a non-zero exit — never a silent fallback.
- **Unskinned geometry draws in bind pose.** Every vertex is skinned through the
  same path (a primitive without JOINTS_0/WEIGHTS_0 defaults to joint 0, weight
  1), and a model without a skeleton keeps a single identity palette matrix —
  so unskinned models and sub-meshes render unchanged, never collapsed.

## Vendored dependencies

`third_party/` holds `cgltf.h`, `stb_image.h`, `stb_image_write.h`, and
`doctest.h` at pinned upstream revisions — see `third_party/VERSIONS.md`.
