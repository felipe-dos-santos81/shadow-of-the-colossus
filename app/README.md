# sotc-viewer

The native macOS viewer for the converted *Shadow of the Colossus* asset library:
C++20, CMake, SDL3, SDL_GPU on Metal. It consumes only converted assets
(`out/library`: `.glb` + `.png`) — never the game tree or original containers.

**Status (milestone S2, task 5): real clips by default.**
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
`--anim` / `--anim-time` pin a clip manually. The arena's ground lift is skipped
while the active clip carries the root translation series (the series already
holds the ground height), so the feet stay on the heightmap instead of floating
by the model's origin-to-feet offset. Drag to orbit, scroll to zoom, press `F`
to reframe; `Space` plays/pauses, `[`/`]` switch clips, `R` restarts. The window
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

| Option | Meaning |
| --- | --- |
| `--library DIR` | converted asset library (default: `$SOTC_LIB`) |
| `--model REL` | model to load, relative to the library |
| `--anim NAME\|IDX` | clip to play, by name or 0-based index (default: the model's first clip; `procedural-wave` only when a skinned model carries no clips); in arena mode it is the manual override of stand/walk/run selection; an unknown name or index exits 2 |
| `--anim-time SEC` | start paused at `SEC` seconds into the clip (wrapped into the clip's duration) — the deterministic-capture flag; it also pins the clip, so arena selection leaves it alone |
| `--screenshot PNG` | render `--frames` frames into the offscreen target, write the PNG, exit |
| `--frames N` | frames the `--screenshot` path renders first (default 3) |
| `--width N` / `--height N` | window size in points (default 1280 × 720) |
| `--selftest` | render one frame, print `s1-status …`, exit |

Controls (no mouse capture: the cursor stays visible and free):

| Gesture | Action |
| --- | --- |
| Left-drag | Orbit: yaw freely, pitch stops at ±89° |
| Scroll | Zoom: distance clamped to [0.5, 500] |
| `F` | Reframe the scene bounds (keeps the viewing direction) |
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
skeleton and without clips prints no such line.)

A screenshot run also prints its size and a per-channel pixel summary, e.g.

```text
s1-pixels min=15,18,0 max=177,180,188 mean=28.1,28.3,27.1
s1-screenshot path=out/s1-smoke.png width=2560 height=1440
```

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
