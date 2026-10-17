# S1 acceptance results (2026-10-17)

Spec milestone S1 (rebuild design; decisions D-S2/D-S3/D-S4/D-S5/D-S8): **"`app/` builds an arm64
binary; window opens windowed; cursor visible and free; Wander + a prop textured; fps HUD;
screenshot + notes."** — **PASS** (evidence below; the items a script cannot reach are marked "≈"
and listed with what *is* verified for each).

| | |
|---|---|
| Date | 2026-10-17 (local −03; acceptance run stamped 2026-10-17T23:49Z) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 (`uname -m` = `arm64`) |
| Repo / branch | `sotc-native`, `s1-sdl-re0`; results recorded at `9f412d2` |
| Library (gitignored) | `SOTC_LIB` = `/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` (main checkout's converted library; this worktree has no `out/library`) |
| Acceptance script | **`scripts/s1-acceptance.sh`** (re-runnable: build → ctest → selftest status → 2 screenshots → summary) |
| Run log (gitignored) | `out/s1-acceptance-run.log` |

All numbers below come from a run of the script (or its exact commands); nothing is estimated
except where explicitly marked "≈".

## 1 · The acceptance run

```bash
SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library ./scripts/s1-acceptance.sh
#   exit 0
```

The script configures/builds `app/` (viewer + tests + `mesh.metal` copied next to the binary — MSL
source compiled by SDL at runtime; no metallib, plan ruling R2), runs the suite, requires the exact
status line, and takes two screenshots (each checked for the `\x89PNG` magic and > 5000 bytes;
the target file is deleted first so a stale PNG cannot mask a failed write). Unset `SOTC_LIB`
falls back to `<repo>/out/library` and fails fast with a clear message when that directory is
absent (verified: exit 1, `asset library not found: … (set SOTC_LIB)`).

**Suite** (`ctest --test-dir app/build --output-on-failure`):

```text
1/2 Test #1: unit_tests .......................   Passed    3.05 sec
2/2 Test #2: windowed_selftest ................   Passed    0.16 sec
100% tests passed out of 2
```

(`windowed_selftest` briefly opens a real window and pins the same status regex the script greps.)

**The verbatim status line** (`--selftest`, queried from SDL — window flags, relative mouse mode,
the auto-capture hint; never assumed):

```text
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
```

**Screenshots** (`--screenshot out/s1/<name>.png --frames 3`, 1280×720 points → 2560×1440 px @2x):

| file | model | bytes | PNG magic | scene |
|---|---|---:|---|---|
| `out/s1/wander.png` | `models/shadow/characters/wander/_cmn/wander.glb` | 835,942 | ✓ | 12 meshes, 62,466 verts, 12 materials, 21 textures |
| `out/s1/plank.png` | `models/objects/props/_cmn/obj_plank_01.glb` | 1,224,993 | ✓ | 1 mesh, 980 verts, 1 material, 2 textures |

Pixel stats (non-uniform ⇒ geometry actually drawn): wander `min=0,0,0 max=219,194,193
mean=28.4,27.9,27.5`; plank `min=3,3,0 max=184,187,198 mean=30.6,31.0,28.5`.

## 2 · Measured fps (title readout, 1280×720, wander, vsync)

The app presents with `SDL_GPU_PRESENTMODE_VSYNC` and writes a rolling 1 s average into the window
title. Read this session via `osascript` (System Events, non-interactive) and a `CGWindowList`
window-title read, with the screen unlocked:

```text
sotc-viewer | 120.0 fps     (AX title read)
sotc-viewer | 121.0 fps     (AX title read)
sotc-viewer | 121.0 fps     (window list)
sotc-viewer | 114.0 fps     (window capture)
sotc-viewer | 118.0 fps     (window list)
sotc-viewer | 117.0 fps     (window list)
sotc-viewer | 113.0 fps     (window list, after the live resize)
```

**≈ 113–121 fps** at 1280×720 with wander, vsync (matches Task 4's locked-screen reading of
117–121 fps). Task 4 carry-over "measure fps unlocked" is therefore **done**.

## 3 · Manual observations

| observation | result | evidence |
|---|---|---|
| **window opens windowed** | **verified** | `CGWindowListCopyWindowInfo`: `onscreen=true`, bounds `291,212 1280×752` (720 content + 32 px title bar — not fullscreen); the window capture shows the macOS title bar and traffic lights. Selftest also queries `windowed=1`. |
| **click does not capture the cursor** | **verified (synthetic click)** | `cliclick c:931,588` into the window (1280×720 content): cursor position stayed exactly `931,588` (no warp/hide), then `cliclick m:1200,300` moved it freely to `1200,300` — the cursor is neither frozen (relative mode) nor re-centered. Window kept rendering throughout. Selftest evidence: `relative_mouse=0 mouse_auto_capture=0`; the app never calls `SDL_SetWindowRelativeMouseMode(true)`/`SDL_CaptureMouse`. A *human* click is still the final on-glasses check (Task 4 carry-over, below). |
| **⌘-Tab works** | **≈** | Not directly verified: an agent cannot press ⌘-Tab (a synthetic app switch would disturb the user's session). Supporting facts: the app registers no global hotkey and no event tap, never captures input, and is a plain resizable windowed SDL window — nothing can swallow a system shortcut. The human check remains open. |

**Live window resize while running (Review Focus #4)** — also verified this session: with the
viewer running, AX set the window to `900×700`, then `500×900`; the window list shows the new
bounds, the title keeps advancing (113.0 → 121.0 fps), the process stays alive, and the capture of
the 500×900 window (`out/s1/window-wander-portrait.png`) shows Wander at correct aspect (not
stretched) — consistent with Task 4's numeric aspect checks. Known, documented behaviour: in a
narrow window the arms clip at the sides (the camera fit uses the vertical FOV only).

## 4 · Task 4 carry-overs

| carry-over | status |
|---|---|
| human-verify the first physical drag | **≈ pending human** — Task 4's synthetic-event probe (yaw/pitch/zoom/F all correct) plus the SDL-source review of the `xrel/yrel` drag path stand as the automated evidence. |
| observe on-screen presentation | **observed this session** — window captures show Wander textured and centred with the live fps title (`out/s1/window-wander.png`, `out/s1/window-wander-portrait.png`); human on-glasses confirmation still open. |
| measure fps unlocked | **done** — §2 (screen unlocked this session; 113–121 fps). |

## 5 · Evidence files (all gitignored under `out/`)

- `out/s1/wander.png`, `out/s1/plank.png` — the acceptance screenshots.
- `out/s1/window-wander.png`, `out/s1/window-wander-portrait.png` — live window captures
  (windowed presentation; portrait after the live resize).
- `out/s1-acceptance-run.log` — the full script output quoted above.
- `out/s1-interactive.log` — the interactive run's stdout (`s1-gpu`/`s1-scene` lines only, no errors).
