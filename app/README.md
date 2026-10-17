# sotc-viewer

The native macOS viewer for the converted *Shadow of the Colossus* asset library:
C++20, CMake, SDL3, SDL_GPU on Metal. It consumes only converted assets
(`out/library`: `.glb` + `.png`) — never the game tree or original containers.

**Status (milestone S1, task 1): the windowed shell.** The app opens a resizable
window, presents a frame at vsync, and exits on Escape or the window's close
button. Asset loading, rendering, and the camera arrive in later tasks.

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
```

The converted library comes from `--library DIR`, else `$SOTC_LIB`; one of the two
is required. Escape or the close button quits.

| Option | Meaning |
| --- | --- |
| `--library DIR` | converted asset library (default: `$SOTC_LIB`) |
| `--model REL` | model to load, relative to the library *(later task)* |
| `--screenshot PNG` | write a PNG after `--frames` frames, then exit *(later task)* |
| `--frames N` | frames to render before exiting (default 3) |
| `--width N` / `--height N` | window size in points (default 1280 × 720) |
| `--selftest` | render one frame, print `s1-status …`, exit |

## Tests

```bash
export SOTC_LIB=…/out/library   # repo convention: app tests fail fast without it
ctest --test-dir app/build --output-on-failure
```

- `unit_tests` — doctest cases (stubs today; asset IO lands in task 2, camera in task 4).
- `windowed_selftest` — opens the window, renders one frame, and asserts the
  `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0` line.

## Guarantees

- **Windowed, always.** Never fullscreen; `SDL_WINDOW_RESIZABLE` from the start.
- **Cursor never captured.** `SDL_HINT_MOUSE_AUTO_CAPTURE=0` is set before
  `SDL_Init`; relative mouse mode and mouse grab are explicitly disabled (and
  never enabled anywhere). `--selftest` reports the state *queried back from SDL*,
  not flags the app assumes.
- **No Xcode Metal toolchain needed to build.** The GPU device requests MSL
  shader source (`SDL_GPU_SHADERFORMAT_MSL`), which SDL compiles at runtime.

## Vendored dependencies

`third_party/` holds `cgltf.h`, `stb_image.h`, `stb_image_write.h`, and
`doctest.h` at pinned upstream revisions — see `third_party/VERSIONS.md`.
