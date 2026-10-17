# sotc-viewer

The native macOS viewer for the converted *Shadow of the Colossus* asset library:
C++20, CMake, SDL3, SDL_GPU on Metal. It consumes only converted assets
(`out/library`: `.glb` + `.png`) — never the game tree or original containers.

**Status (milestone S1, task 4): the windowed viewer with an orbit camera.**
The app opens a resizable window, loads a converted model with its PNGs, draws
it (offscreen target → swapchain → present) at vsync, and frames the scene
bounds on load. Drag to orbit, scroll to zoom, press `F` to reframe; the window
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
```

The converted library comes from `--library DIR`, else `$SOTC_LIB`; one of the two
is required. Escape or the close button quits.

| Option | Meaning |
| --- | --- |
| `--library DIR` | converted asset library (default: `$SOTC_LIB`) |
| `--model REL` | model to load, relative to the library |
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
| Escape / close button | Quit |

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

- `unit_tests` — doctest cases (asset IO and camera math).
- `windowed_selftest` — opens the window, compiles the shader, renders one
  frame, and asserts the `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0`
  line. `mesh.metal` must sit next to the binary (the build copies it; a bad
  shader fails this test loudly).

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

## Vendored dependencies

`third_party/` holds `cgltf.h`, `stb_image.h`, `stb_image_write.h`, and
`doctest.h` at pinned upstream revisions — see `third_party/VERSIONS.md`.
