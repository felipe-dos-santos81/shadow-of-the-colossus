# RE-1b capture path — port bring-up spike (C1) (2026-10-18)

W3's capture workflow ("reference stills from the port rig") needs the user's port to boot *our*
01.00 dump on this Mac. This file is the measured run-sheet for that (commands that worked,
deviations, versions) and the gate verdict. §3 (the captures) and §4 (the gate decision) land with
C2/C4. The port repo is a read-only input; every port-side output stays in its own `out/` (or its
user dir); nothing from it enters this repo's product path.

| | |
|---|---|
| Date | 2026-10-18 |
| Machine | Apple M3 Max, macOS 27.0.1 (26A434); the port runs as **x86-64 under Rosetta 2**; GPU driver **KosmicKrisp 26.2.99** (Vulkan 1.4.363, Metal 4) |
| Machine state during the spike | **clamshell** (lid closed), session locked at `loginwindow`, built-in display asleep; the machine enters clamshell-sleep cycles, so processes freeze intermittently (`time.monotonic` excludes sleep: measured waits are shorter than wall time); `caffeinate` keeps the CPU awake but not the display |
| Port repo (read-only) | `~/code/mine/ShadowOfTheColossus-PC`, commit `cdce18b` (2026-10-15); `out/bb-probe` already up to date (`ninja: no work to do`) |
| Game dump (read-only) | `~/Downloads/sotc/SOTC-extract/extracted/uroot` — APP_VER **01.00**, `EP9000-CUSA08809_00-SOTC0000000000EU`, eboot sha256 `21982727…a4e0`; the fpkg is also at `~/Downloads/sotc/…-A0100-V0100.pkg` and on `/Volumes/Felipe/…` |
| Staged game dir | `<port>/out/CUSA08809` → symlink → `<port>/out/pkg-extract/uroot` |
| This repo / branch | `sotc-native`, `capture-decode` (base `931bc5b`) |
| Evidence kept in the port's tree | `out/c1-verify-30s.log` · `out/c1-verify-180s-pass.log` · `out/c1-boot-freshuser.log` · `out/c1-gate-frame-20261018-205751.png` (97 KB, sha256 `ec261d1f…ca81`) |

## 1 · The run-sheet, as measured

### 1.1 Build

All build tools were already present (`cmake`, `ninja`, `pkgconf`, `glslang`, `nasm`, `python3`,
`git`, `lipo`), so `make deps` was not run. Two of the run-sheet's optional inputs resolved
through the port's own fast paths:

```bash
cd ~/code/mine/ShadowOfTheColossus-PC
make driver          # <1 s: reused the sibling checkout's cached x86-64 driver
bash build.sh        # exit 0, ~1 s: incremental (out/gpu-build.log: "ninja: no work to do")
bash build.sh --test # exit 0, ~2 s: all 11 C/C++ targets pass
```

- `make driver` did **not** need `BB_KOSMICKRISP_URL`/`BB_KOSMICKRISP_TARBALL`: `tools/build_kosmickrisp.sh`
  found a complete driver in the sibling bbport checkout (`~/code/theirs/bloodborne_pc/out/`) and
  `tools/install_kosmickrisp.sh` installed it (the three files + an x86-64 slice check) in under a
  second. `out/libvulkan_kosmickrisp.dylib` is x86_64-only, as the game process requires.
- `bash build.sh` also found its inputs without configuration: `libMoltenVK.dylib` from the same
  sibling checkout (universal x86_64+arm64 — the port's `lipo` slice check requires the x86_64 half)
  and vcpkg from `~/code/theirs/bloodborne_pc/out/vcpkg` with the ready `out/vcpkg_installed/x64-osx`
  (`tools/vcpkg_ready.sh` passes → manifest install off).
- The binary was already current: the GPU build log said `ninja: no work to do`, so `out/bb-probe`
  is the user's last build (Oct 15 12:45) — the same binary as the port's own Oct 14–15 session
  logs. `bash build.sh --test` runs the C/C++ targets only; the Makefile's `test` target additionally
  runs `python3 -m unittest discover -s tests`.

### 1.2 Staging

The staging the port's own `sotc.py` creates was already in place and is byte-identical to our
extraction of the same fpkg, so it was verified rather than rebuilt:

| File | Check | Result |
|---|---|---|
| `eboot.bin` | sha256 vs `SOTC-extract/extracted/uroot/eboot.bin` | `21982727…a4e0` — **identical** |
| `sce_sys/param.sfo` | sha256 vs `SOTC-extract/extracted/pkg-meta/param.sfo` | `317b9716…73b3` — **identical** |
| `sce_module/` | contents | `libc.prx` + `libSceFios2.prx` present |

- **No 1.01 update tree exists** (checked `<staged>-UPDATE`/`<staged>-patch` next to the staged dir,
  `mdfind -name CUSA08809`, and the external volume): the only package on this machine is the base
  `…-A0100-V0100.pkg`. `sotc.py`'s update merge therefore has nothing to merge and the boot is pure
  01.00 — the patch modes (`fps60`/`af16`, `SOTC_CVARS`) are 1.01-only and are simply not applied;
  normal boot is not version-gated.
- `SOTC_SYS_MODULES` could not be set to anything useful: `libSceUlt.sprx` / `libSceNgs2.sprx` are
  **not present anywhere on this machine** (checked with `mdfind` and targeted `find`s). The run is
  silent by design; the log's first line is the port's own warning (`SOTC_SYS_MODULES not set: no
  sound`) and later `Runtime: Ngs2 system created (silent audio)`.
- There is no `dvdroot_ps4/` in this dump: the game's data lives in the six `*.psarc` paks at the
  uroot root, which is what the port mounts at `/app0`. `scripts/prepare.py`'s `dvdroot_ps4` scan
  therefore finds nothing (`out/analysis.json` reports `"resources": {}`).
- `out/analysis.json` (the port's own prepared-image report) confirms the pre-boot gates that the
  brief flagged: **`libc_evidence.init_env_is_ret: true`** (our 01.00 `libc.prx`'s `_init_env` is
  exactly `RET`, so the gate passes), `image_bytes: 60123812` (below 2 GiB), `bundled_modules:
  [libSceFios2.prx, libc.prx]`, `import_count: 588`.

### 1.3 The gate

**`make verify` (30 s) — FAILED in the current machine state**, with a diagnosed cause:

```
missing: presenter ready
missing: heap guard
```

The captured log stops right after the swapchain's present-mode query
(`vk_swapchain.cpp:228 FindPresentMode: … falling back to Fifo.`) — `bbgpu_init` never returns. A
`sample` of the live process shows exactly where it is (stack, main thread):

```
probe_main (probe.c:1223) → bbgpu_init (bbgpu.cpp:362)
  → Libraries::GnmDriver::RegisterLib (gnmdriver.cpp:2923)
    → Vulkan::Presenter::Presenter (vk_presenter.cpp:473)
      → Vulkan::Rasterizer::Rasterizer (vk_rasterizer.cpp:56)
        → Vulkan::PipelineCache::PipelineCache (vk_pipeline_cache.cpp:354)
          → Vulkan::PipelineCache::WarmUp() (vk_pipeline_serialization.cpp:367)
            → Storage::DataBase::ForEachBlob → LoadGraphicsPipeline (vk_pipeline_serialization.cpp:248)
              → GraphicsPipeline::GraphicsPipeline (vk_graphics_pipeline.cpp:430)
                → vk_common_CreateGraphicsPipelines (libvulkan_kosmickrisp)
                  → kk_compile_graphics_pipeline → mtl_new_library
                    → -[AGXG15XFamilyDevice newLibraryWithSource:…]
```

The presenter's pipeline warm-up re-creates the stored pipeline cache in the port's user dir
(**8,676 files** under `user/cache/CUSA08809`), and KosmicKrisp compiles each Metal library **from
source** (`mtl_new_library`). That compile cache (`/var/folders/…/C/com.apple.metal`) is **cold on
this boot** — the machine rebooted Oct 16 17:27 and the port's last run was Oct 15 — so the first
run after the reboot pays the whole compile cost. It does not fit in 30 s: an instrumented run left
grinding for **~80 minutes of wall time** (sleep-frozen most of it) had still not finished. The
user's own `out/session-hunt1.log` (Oct 15 18:30) ends at this same line — they hit this stall too.

A second `make verify` attempt with a longer deadline — `make verify seconds=180`, run **after** the
earlier runs had warmed the Metal cache with the shaders the game actually uses — **passed**:
`macos_check: PASS` (all five markers, no `missing:`/`forbidden:` lines). So the canonical gate does
work on this machine; the first run after a reboot (or any Metal-cache invalidation) is the
expensive one, and it is a one-time cost.

**The real boot — BOOT, with a warm-up workaround.** Running `python3 sotc.py` with a **fresh user
dir** (`SOTC_USER_DIR=<tmp>/fresh-user`, so the stored pipeline cache is empty and `WarmUp()` has
nothing to load) gets the port all the way through:

```bash
SOTC_USER_DIR=<tmp>/fresh-user BB_HDR=0 BB_GPU_LOG=info SOTC_FPS=1 \
  BB_FRAME_DUMP_TRIGGER=<tmp>/dump.trigger BB_DUMP_DIR=<tmp>/dump \
  python3 sotc.py
```

| Marker / evidence | Value |
|---|---|
| Presenter | `GPU: window and Vulkan presenter ready; SDK 0x04508101, 327 HLE symbols` |
| Game version | `[MSG-Init] ParseBuildVersion: +BuildVersion=2017-11-09.106777 +RetailBuild=1 +titleskuid=SCEE` |
| Game data | `Resource dependency file loaded: $/workspaces/_ps4/shadow.cwks`, `+StartupScript=$/scripts/shadow/shadowmain.sgpr`, VideoOut 1920×1080 `A2R10G10B10Srgb`, render/save/trophy/audio threads created |
| Heap guard | `Runtime: BPE heap guard on heap 0x1f53c700 (2000 of 2000 frees)` |
| **Main loop** | `[MSG-Init] Starting main loop: 00:10:55.75` |
| **Frames presented** | `SOTC FPS 49.0 at 5s` · `57.8 at 10s` · `60.0 at 15s` (…`30.0 at 326s`, when the probe killed it) |
| **Dumped frame** | `SavePendingScreenshot: Saved screenshot: <user>/screenshots/CUSA08809_20261018_205751_163_game_000000.png` — 1920×1080 RGBA PNG, sha256 `ec261d1f…ca81`; the frame shows the game's Japan Studio intro logo |

The 30 s gate's two missing markers both appear in this run, so the gate's **criteria** (main loop
reached, a frame presented) are met; what failed was the 30 s deadline against a one-time, cold-cache
cost that the workaround bypasses.

**The capture hooks, as measured** (C2's inputs):

- `BB_FRAME_DUMP_TRIGGER=<file>`: the port **removes** the file and saves the next presented game
  frame as a PNG into `<BB_GPU_USER_DIR>/screenshots/`, named
  `CUSA08809_<yyyymmdd>_<hhmmss>_<ms>_game_NNNNNN.png`. So each shot needs the trigger re-created,
  and the PNG (not a raw dump) is what to collect — `tools/raw2png.py` and the Windows-shaped
  `tools/sotc_play.py`'s `output_*.raw` expectation do not apply to this port's KosmicKrisp path
  (its own fallback copies from `user/screenshots`). `SOTC_SHOT_OVERLAY=1` includes the overlay.
- `BB_PAD_FILE=<file>`: whitespace-separated pad tokens held while the file says so (`cross`,
  `options`, `lx=0..255`, …); `BB_PAD_RECORD`/`BB_PAD_REPLAY` (F9) can record/replay a route.
- The game's boot reached rendering unattended (no pad input was given); the FPS numbers above
  measure whatever the game was drawing then (the intro logos at least).

### 1.4 Deviations from the plan's run-sheet

1. `make deps` not run (all tools present).
2. `make driver` took the sibling-checkout reuse path instead of `BB_KOSMICKRISP_URL`/`TARBALL`
   (the driver was already installed and valid).
3. Staging was verified, not rebuilt (the port's own `sotc.py` extraction of the same fpkg was
   already in place and byte-identical to ours).
4. `SOTC_SYS_MODULES` could not be set (the two Sony modules are absent from this machine) — silent
   run, documented above.
5. **HDR forced off** (`BB_HDR=0`) for the boot run: the display currently reports HDR headroom, and
   the run-sheet's baseline is the SDR path. HDR was *not* a failure cause — the 30 s gate failed
   with HDR allowed and the instrumented SDR run stalled at the same place.
6. **Fresh user dir** for the boot run: it bypasses the cold-cache pipeline warm-up (and, as a
   side effect, starts from a fresh save state — the run never touches `user/savedata`).
7. The machine was asleep/locked throughout (clamshell, `loginwindow` frontmost, display asleep).
   The port ran and presented frames anyway; the main loop took ~11 minutes by the game's own clock
   (`00:10:55.75`) — the lazy shader compiles — and the wall time was longer still because the
   machine sleeps in cycles. A normal (unlocked, lid-open) session would be faster, and after this
   run the Metal cache holds the intro shaders.

## 2 · The gate verdict

**BOOT** — main loop reached and frames presented, on our own APP_VER **01.00** dump, by the port
build at `cdce18b`, with the evidence above (log markers in `out/c1-boot-freshuser.log`, the FPS
lines, and the dumped 1920×1080 frame `out/c1-gate-frame-20261018-205751.png`).

Caveats, stated plainly so they are not lost:

- The **canonical `make verify` (30 s) did not pass in the machine state of this run**: the first
  run after the Oct 16 reboot pays the cold Metal compile for the whole stored pipeline cache
  (stack in §1.3). The cost is one-time — a `make verify seconds=180` run **passed** after the
  earlier runs warmed the cache (§1.3), so `make verify` is expected to pass on a warm cache; only
  the first run after a reboot/Metal-cache invalidation needs the longer deadline (or a fresh user
  dir).
- The boot above ran with `BB_HDR=0` and a fresh user dir (§1.4). Neither is a product decision —
  they are the two knobs that made this machine's state workable.
- No 1.01 update tree exists (§1.2): 01.00 boots as-is; the patch modes remain inapplicable. No
  `libc.prx` `_init_env` problem (`init_env_is_ret: true`), no missing-module failure (silent only).

What this means for C2: the capture workflow can proceed against this rig — the port boots our dump,
the pad-file and frame-dump hooks work, and screenshots land as PNGs in `<user>/screenshots/` (the
hook *consumes* the trigger file, so the driver re-creates it per shot). The driver must budget for
the first-run shader compiles and must not depend on the machine being awake continuously. If the
gate is ever re-run for the record, do it on an unlocked, lid-open session with a warm cache.
