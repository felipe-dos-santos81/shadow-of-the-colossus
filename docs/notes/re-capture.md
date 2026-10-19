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
  01.00 — the patch modes (`fps60`/`af16`, `SOTC_CVARS`) were *predicted* 1.01-only (the plan's
  Global Constraint), but measurement superseded that: the port's gate is **signature-based**
  (`scripts/sotc_modes.py`'s `is_supported` — both check sites `0x009367b9`/`0x01390090`
  byte-match our `eboot.bin`), so the modes are not gated out and `SOTC_CVARS` **does apply** on
  01.00 (all six `nointro` run logs carry `+nointro`; the boot log carries the applied default
  cvars — §3.2). Normal boot is not version-gated.
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
- No 1.01 update tree exists (§1.2): 01.00 boots as-is; the patch modes are **not** gated out —
  the signature gate passes on our eboot and `SOTC_CVARS` applies (§1.2/§3.2). No `libc.prx`
  `_init_env` problem (`init_env_is_ret: true`), no missing-module failure (silent only).

What this means for C2: the capture workflow can proceed against this rig — the port boots our dump,
the pad-file and frame-dump hooks work, and screenshots land as PNGs in `<user>/screenshots/` (the
hook *consumes* the trigger file, so the driver re-creates it per shot). The driver must budget for
the first-run shader compiles and must not depend on the machine being awake continuously. If the
gate is ever re-run for the record, do it on an unlocked, lid-open session with a warm cache.

## 3 · The captures (C2)

### 3.1 The driver — `tools/re/capture/port_capture.py`

```bash
python3 tools/re/capture/port_capture.py <schedule-file> --out out/re/capture \
    [--tag NAME] [--from-marker TEXT] [--env NAME=VALUE ...]
```

One event per line, `<time-seconds> <action> [hold_ms]` (`#` starts a comment); `action` is pad
tokens joined by `,` (the `BB_PAD_FILE` names of `src/runtime_pad.c`: cross, circle, square,
triangle, l1..r3, options, touchpad, touchpad_left/right, up/down/left/right, `lx=`/`ly=`/`rx=`/
`ry=` 0..255), `shot`, or `log:<text>`. A pad event holds the tokens for `hold_ms` (default
250 ms) and then releases them; an empty file releases everything (the port re-reads on mtime
change). Times count from the port log's `Starting main loop` line; `--from-start` moves the zero
to process start, and `--from-marker TEXT` to a later log line — the anchored runs below use the
game's own `st_gameflow() starting game`, because the world load between the main loop and
gameplay varies between runs (measured G = 14.2–33.5 s after the main loop).

The driver launches `python3 sotc.py` with `SOTC_USER_DIR=<out>/user` (never the port's `user/`),
`BB_PAD_FILE`, `BB_FRAME_DUMP_TRIGGER`, `SOTC_FPS=1`, `BB_HDR=0`, and the macOS env
(`DYLD_LIBRARY_PATH`, `VK_DRIVER_FILES`, `MVK_CONFIG_LOG_LEVEL` — `sotc.py` sets these for the
probe it launches as well). Per `shot` it touches the trigger, waits for the port to consume it
(`std::remove`), waits for the PNG to be *complete* (magic + IEND; a killed run caught 0-byte and
partial copies while the file was still being written), copies it to `<out>/<tag>-<port name>.png`
and appends `{time, action, file, sha256}` to `<out>/manifest.json` (one list across all runs;
`time` is seconds from that run's anchor). After the last event plus `--grace` it SIGTERMs the
process group, so `sotc.py`'s `bb-probe` child dies with it. `--dry-run` prints the parsed plan.

Saves: `<out>/save-template/CUSA08809` is restored into `<out>/user/savedata/1/CUSA08809` before
every run — the port's own `sotc_play.py` discipline, so no run depends on the saves its
predecessor wrote. This checkout has no `out/save-template`, so the template was seeded once,
read-only, from the port's `user/savedata/1/CUSA08809`; the port's own user dir is never written
(§3.4). `--keep-save`/`--fresh` opt out.

The run user dir seeds its pipeline cache once from the port's `user/cache/CUSA08809`: a cold
pipeline cache is C1 §1.3's multi-minute grind. It is not removed entirely — the pipeline warm-up
still costs ~6.6 min of wall time whenever the Metal compile cache has gone cold: the five
main-loop-anchored runs measured 398–418 s to the main loop, and the marker-anchored runs measured
42.5 s and 45.6 s to `st_gameflow` when the previous run had warmed the cache for the same route,
445.5 s when it had gone cold again.

### 3.2 The route (as measured)

Two dead ends, recorded so they are not retried blindly:

1. **No input** — the port boots (C1), then the game plays its logos (SIE ~6 s, JAPAN Studio
   ~12 s, Bluepoint ~16–20 s), the “automatically saves your progress” notice (~25 s), and from
   ~30 s the intro/attract movie: moon+bird 34 s, valley 40 s, waterfall 46 s, dark scenes with
   the horse 68 s, a colossus silhouette 130 s (R1b stills). Crosses during the logos or the movie
   do nothing (R1: cross at 5/10 s; R1b: cross at 50 s) — the movie ran past 130 s.
2. The port’s own scripted runs (`out/session-*.log`, Oct 14–15) pressed cross at ~5 s and ~10 s
   and reached the save at ~35–40 s. That did not reproduce on our 01.00 dump: the same crosses
   left the game on its logos and then the movie (R1).

**The working route** — `SOTC_CVARS="nointro doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0"`
(so `sotc.py` → `sotc_modes.py` writes `+nointro` into `out/cmdline.bin`): `nointro` is the port’s
own bench cvar (`tools/sotc_bench.sh`: “+nointro starts outside the Shrine of Worship”), the
01.00 eboot passes the version check, and the boot log carries it (`Engine command line:
+VSyncWindow=10% +nointro …`). With it there are no logos and no movie: the game loads straight
into gameplay — Wander idle on the grass outside the Shrine of Worship, Agro grazing a few metres
ahead and then walking over to Wander on his own, or to the whistle (`r1`). The opening is a
scripted sequence with subtitles (“Now, be on thy way.” → “Press ✕ to jump.” → “In a sunlit place,
hold up the sword using R2 …”) and the mount hint “Press △ or ✕ when near Agro to mount.” The
game’s own markers: `st_gameflow() starting game` at game clock 14.8–16.8 s in five of the six
nointro runs, 34.0 s in the first one (main loop at ~0.57 s).

The idle trio is therefore anchored at `st_gameflow() starting game` and shot at gameplay +5/+10/
+15 s — the world is a fixed opening state (no input before the shots), so the only variance is
the game’s own opening subtitles, which are part of the state at those times.

### 3.3 The captures

**Idle trio** (deliverable; run tag `idle-anchored`, anchor `st_gameflow()`, 2026-10-18 23:42;
`manifest.json` `time` 5.0/10.0/15.0; all 1920×1080 RGBA PNGs):

| file (`out/re/capture/`) | gameplay | bytes | max channel span | sha256 |
|---|---|---:|---:|---|
| `idle-anchored-CUSA08809_20261018_234236_562_game_000000.png` | +5 s | 921,791 | 255 | `c70285e8…f61efa` |
| `idle-anchored-CUSA08809_20261018_234241_605_game_000001.png` | +10 s | 1,092,017 | 255 | `2a88022e…b451b0` |
| `idle-anchored-CUSA08809_20261018_234246_560_game_000002.png` | +15 s | 951,011 | 255 | `ab04f0e3…9af91f` |

On screen: Wander idle in front of the shrine, seen from behind, Agro approaching / beside him —
+5 s with the subtitle “Now, be on thy way.”, +10 s with Agro galloping up to him (dust) and
“Press ✕ to jump.”, +15 s with “In a sunlit place, hold up the sword using R2 …” (full sha256s in
`manifest.json`; the two context shots at +20/+25 s are in the same run). This is a **reference**
state for C4’s comparison, never product data.

**Horse** — recorded honestly: **the horse state is reachable, the mounted ride was not.** The
whistle (`r1`) reliably brings Agro to Wander (frames: `horse3-…_000035_372` — Agro head to head
with Wander; `horse-…_233513_972` — Agro beside him with “Press △ or ✕ when near Agro to mount.”
up), and in the no-input runs Agro walks to Wander on his own (`idle-…_232404_222`). Three
scripted mount attempts — `△` at 76 s in run `horse`; whistle→`△`→`✕` at gameplay+14…28 s in
`horse-anchored`; whistle at gameplay+38 s then `✕` every 3 s through the arrival window in
`horse3` — all left Wander on foot (the frames after each press show him walking/jumping on
foot). A mounted gallop therefore stands open; the horse reference stills are the trio’s +10 s
frame and the whistle frames above.

**The set** (all gitignored, `out/re/capture/`): 81 manifest entries from 7 runs —
`route-r1b` (15) + `route-r2` (14) map the two dead ends and the nointro boot; `idle` (16) is the
unanchored idle set around gameplay (its 37–85 s frames are G+21…G+70 in that run — the reason the
anchored rerun exists); `idle-anchored` (5) is the trio; `horse` (10) + `horse-anchored` (10) +
`horse3` (11) are the mount attempts; `route-r1` (6, from the killed first run) was discarded. The
per-run driver logs (pad writes, shot collection, anchor) and the port’s stdout are kept in
`out/re/capture/logs/`.

### 3.4 Verification

- **Manifest complete** — every entry carries exactly `{time, action, file, sha256}`
  (`action: "shot"`), the `sha256` matches the bytes on disk, and the file exists (checked for
  all 81; re-run: `python3 out/re/capture/verify_stills.py`).
- **Non-uniform stills** — the s1-style check (largest channel span ≥ 30) passes for the trio
  (255 each) and for every frame except one: `route-r1b-…_225410_528_game_000000.png` (t=28 s) is
  a black transition frame of the route, kept as an honest record (span 0).
- **Saves untouched** — the port’s `user/savedata` sha256 baseline taken before the captures
  (`out/re/capture/saves-baseline-port-user.txt`, 8 files) is byte-identical after them, and
  *no* file under the port’s `user/` was written during the whole capture session (checked with
  `find … -newermt`). The restore ran before every run — each run’s driver log has its
  `saves: restored … -> <out>/user/savedata/1/CUSA08809` line. The port’s own `out/` gets only
  the rewrites any `sotc.py` launch makes (`sotc_modes.py`’s `cmdline.bin`/`patches.bin`); it was
  left back on the default cvars (`cmdline.bin` without `+nointro`) after the runs.
- **Machine state** — as in C1: clamshell, display asleep, `caffeinate -i` holding the CPU
  awake; the runs above are from that state, and the wall times in the logs reflect it.

## 4 · The gate decision and the comparison (C4)

### 4.1 The gate decision — verbatim

> **Superseded by S2** (`docs/notes/s2-results.md`): real decoded clips and locomotion now play.

**NOT MET** — the decode is **not confirmed**: RE-1's gate stands, and C3's sampler read-out
did not crack it. The decision, in the plan's words:

> **NOT MET** (the decode stays open; the reference stills stand as the §5 artifact; S2's
> sign-off stays open; the stand-in rule applies).

The evidence walk, one line per input:

- **C1 §1–§2 — BOOT.** The port boots our own APP_VER 01.00 dump: main loop reached, frames
  presented, the frame-dump hook writes PNGs. The capture rig works; the capture-derived path
  is armed.
- **C2 §3 — the reference stills exist.** 81 manifest entries across 7 runs; the idle trio at
  gameplay +5/+10/+15 s (anchored at the game's `st_gameflow() starting game`); the horse
  state reachable (whistle frames), the mounted ride not taken — recorded honestly. Saves
  untouched (baseline byte-identical).
- **RE-1 §5 / §3.6 — the decode gate is unchanged.** `check-curves` exits 1: 13/16 gating
  assertions pass, 3 curve assertions fail (`quaternion norms within 1e-3`, `adjacent-key dot
  > 0.9`, `keys with |v| > 1`). The container is decoded to the byte; the key encoding is open
  (§3.5–§3.7), and the B2 candidate data stays `UNVERIFIED`. RE-1's decision verbatim:
  "**NOT MET** — the decode did not reach verified extraction → the capture-derived path
  activates (its plan is written against the port rig); decoding continues as background work."
- **C3 §6 — the sampler read out, the gap moved.** The keyframe sampler is located and read out
  at the runtime level: `FUN_005fd900` and the per-type samplers, the 12-byte record and
  key-blob layouts, the full key-format vocabulary with exact scales (`FUN_0060bba0`, cases
  0…0xe), and the frame-based time model (integer key index, linear blend, wrap by `count−1`).
  But the **file-level decode is still open**: §6.6–§6.7 settle that the re-based structure is
  runtime-built, so the gap is now the **loader's parse chain** (the file→structure parse),
  not the sampler. §6.7 verbatim: "the `.anb` file-level decode is still open — the gap is now
  the loader's parse, not the sampler; §5's gate decision (NOT MET) does not change". The
  stride-6 int16 series (§6.5.3) are the lead to test against that parse.
- **§5 cross-reference (re-anb.md).** The sampler changed no part of the decision, so §5's text
  is not updated; §6.7 records the standing explicitly (this is the cross-reference's home).

> **Superseded by S2** (`docs/notes/s2-results.md`): real decoded clips and locomotion now play.

So: the decode stays open; the reference stills (§3) stand as the S2 reference-capture artifact
(first-playable design §5); S2's sign-off ("plays a converted `.anb` clip correctly vs a
dev-only reference capture") stays open; the labeled stand-in rule applies (s3-arena design §4).
A parse-chain continuation is in flight as background work; it is **not consumed** by this plan
— a cracked decode hands off to the converter plan (Plan C), which is written when this gate
says MET.

### 4.2 The comparison artifact — `out/re/capture/compare/` (gitignored)

A labeled side-by-side set: **the real game** (C2's captures, byte-verified copies) beside
**our app's render** of the converted library with S2's labeled `procedural-wave` stand-in.
It is the visual-validation artifact of the capture path — with the gate NOT MET there is no
candidate pose to compare, so it serves **no sign-off** and claims **no pose correspondence**.

| file | what it is |
|---|---|
| `ref-idle-anchored-…_234236_562_game_000000.png` | reference, idle trio +5 s (the real game) |
| `ref-idle-anchored-…_234241_605_game_000001.png` | reference, idle trio +10 s (Agro arriving) |
| `ref-idle-anchored-…_234246_560_game_000002.png` | reference, idle trio +15 s |
| `ref-horse-…_233513_972_game_000001.png` | reference, whistle frame (mount prompt up) |
| `ref-horse3-…_000035_372_game_000001.png` | reference, text-free frame (gameplay +42 s) |
| `app-t0.png` | app: converted **Wander** + stand-in, t = 0.000 s |
| `app-wave-t050.png` | app: converted **Wander** + stand-in, t = 0.500 s |
| `app-agro-t0.png` | app: converted **Agro** + stand-in, t = 0.000 s |
| `app-agro-wave-t050.png` | app: converted **Agro** + stand-in, t = 0.500 s |
| `README.md` | the pairing legend + the claims (its text is the authority) |

The app renders (all 1920×1080 RGBA PNGs — the same size as the reference; `--width 960
--height 540` points on the Retina display):

```bash
SOTC_LIB=…/out/library app/build/sotc-viewer --library …/out/library \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --anim-time 0 --width 960 --height 540 \
  --screenshot out/re/capture/compare/app-t0.png --frames 3
# printed: s2-anim clip=procedural-wave joints=348 procedural=1 t=0.000
#          s1-screenshot path=…/app-t0.png width=1920 height=1080
```

(the other three: the same command with `--anim procedural-wave --anim-time 0.5` for
`app-wave-t050.png`, and `--model models/shadow/characters/agro/_cmn/agro.glb` for
`app-agro-t0.png` / `app-agro-wave-t050.png`; Agro prints `joints=132`.)

> **Superseded by S2** (`docs/notes/s2-results.md`): real decoded clips and locomotion now play.

**`compare/README.md` states exactly what is and is not claimed** (its text is the authority;
summarized here):

- **Reference = the real game** — C2's captures of *Shadow of the Colossus* (2018, APP_VER
  01.00) on the dev-only port rig; the copies are byte-identical to `out/re/capture/` and their
  sha256s equal `manifest.json`'s entries.
- **Our side = the converted `wander.glb` / `agro.glb`** (the M0 converter's output) rendered by
  the built viewer, playing S2's built-in **labeled stand-in** `procedural-wave` at pinned
  times. **Never product data**; never a decoded clip — no real clip exists while the gate is
  NOT MET.
- **Not claimed:** no pose/frame correspondence (the stand-in is not Wander's or Agro's idle);
  no sign-off (S2's done-line stays open); no product path (gitignored `out/`, personal use);
  no context match (the game's world vs the bare model — cameras, framing, and lighting differ
  by construction).

Verification (2026-10-19): every `ref-*.png` sha256 equals its
`manifest.json` entry; each copy is `cmp`-identical to its original in `../`; every file in
`compare/` is a 1920×1080 PNG; the app runs printed their `s2-anim` / `s1-screenshot` lines
above.
