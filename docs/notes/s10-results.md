# S10 results

## Task 4 — concurrent viewers (GPU width)

Protocol (design §13.7): the frozen S4.2 perf pair (`--frames 1` then `--frames 240`, `--anim-time 0
--entity-time 0`, encounter manifest, Wander; marginal = (t240 - t1) * 1000 / 239 ms) run alone vs three
pairs at once (the 1-frame runs together, then the 240-frame runs together), three trials each, every run
under `caffeinate -dimsu`, outputs in a scratch dir. Background: `mediaanalysisd`/`photolibraryd` were
at ~100 % CPU (macOS, nice 0) during the solo trials and trial 1 of the concurrent set; a nice-19
`sotc-app-tests` from another worktree overlapped concurrent trial 3, so that trial was discarded and
re-measured clean (trial 4).

| trial | solo marginal (ms) | concurrent marginals, pairs 1/2/3 (ms) |
|---|---|---|
| 1 | 7.74 | 10.27 / 8.21 / 8.41 |
| 2 | 8.44 | 8.54 / 8.63 / 8.73 |
| 3 (4 for conc.) | 8.47 | 8.23 / 8.28 / 8.23 |

Solo baseline (mean of 3) 8.22 ms (a warm-up run and a fourth solo trial read 8.26 and 8.50). Load time:
solo 1.27–1.44 s, concurrent 1.64–2.11 s.
Discarded confounded trial 3: 8.48 / 8.59 / 8.36 (a nice-19 `sotc-app-tests` was running).

Decision: **width 1.** Concurrent marginals reach +2.05 ms (10.27) and +0.51 ms (8.73) over the solo
mean, and the solo trials themselves span 0.73 ms, so the ±0.2 ms rule cannot be met; the load phase is
also ~55 % slower under contention. The runner keeps `--jobs` default 1 (perf-gated runs always alone).

`scripts/verify` after the change: all nine milestones PASS, 261 s wall (cold stamps; width 1 is the
unchanged default, so no golden moved and the time is the same as before).

## Task 6 — the app.cpp split

Behaviour-preserving, steps a–d (15 `refactor(app):` commits on `s10-split`); every step's
`make verify` re-ran all nine milestones' GPU sections and matched the goldens (PASS), and step e's
`make verify-long` is recorded below.

Before: `app/src/app.cpp` **9,275** lines (`run_app` alone ~6,940). After (24 files, 10,654 lines; the
growth is declarations, includes and the per-function reference preambles that keep the moved bodies
textually unchanged):

| file | lines | holds |
|---|---|---|
| `app.cpp` | 151 | `run_app`: option checks, the SDL shell, the stage loop |
| `stage_ctx.h` / `.cpp` | 331 / 579 | `Shell`, `RunState` (per run), `PlainRide`, `FrameState`, `StageCtx` (per stage) and its methods (`hud_state`, `soft_reset_encounter`, `press_reset`, the wake lambdas, `plain_step`, `sword_frame`, `door_frame`, …), shared constants |
| `stage_setup.cpp` | 661 | `stage_setup`: the stage's composition (`std::optional<int>` exit code) |
| `selftests.h` | 108 | `SelftestResult`, the selftest declarations, `run_selftests` (the original order) |
| `selftest_s1.cpp` | 103 | the plain `--selftest`, the `--screenshot` capture |
| `selftest_s3.cpp` / `_s4` / `_s6` / `_s7` | 102 / 379 / 503 / 414 | one selftest each (s4: `s4_pin_entity` + `run_s4`) |
| `selftest_s41.cpp` / `_s42` | 908 / 1,235 | s41; s42 with s5 woven in |
| `selftest_s8.cpp` / `_s9` | 552 / 562 | the S8/S9 timelines |
| `interactive.h` / `.cpp` | 51 / 1,594 | `FrameInput`, `FrameResult`, `frame_step`, `run_interactive` (the loop + the s7/s8/s9 e2e scripts) |
| `encounter_rt.h` / `.cpp` | 592 / 1,085 | the stateless runtime helpers (+ `camera_sword_shoulder`) |
| `capture.h` / `.cpp` | 109 / 192 | view-proj, s1-status, screenshots/stills, screen boxes |
| `audio_seam.h` / `.cpp` | 96 / 240 | `AudioSeam` |
| `gamepad_input.h` / `.cpp` | 40 / 67 | `GamepadInput` |

Step e: `make verify-long` → `verify: long s9 PASS (784 s, scripts/s9-acceptance.sh)` (the whole frozen
chain s9 → … → s3 on the split tree; 13 m 05 s wall). Shared frames: `frame_step` is the interactive
frame only — the S8/S9 selftests' `frame` lambdas and S4.2's `step_frame` differ from it (no death/return
machines or player-sfx drain, the S9 look before the player step, a basis-yaw override, no sword facing
turn), so they stay their own; the S9 shoulder shift is one function (`camera_sword_shoulder`) both the
loop and `--s9-selftest` call just before `sword_frame`.
