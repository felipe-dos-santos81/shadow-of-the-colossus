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
