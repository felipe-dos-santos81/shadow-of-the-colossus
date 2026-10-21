### Task 9: `--s42-selftest`

**Files:**
- Modify: `app/src/app.h` (`s42Selftest`), `app/src/main.cpp`, `app/src/app.cpp`
- Test: exercised by the selftest itself

**Interfaces:** event lines `s42-proxy`, `s42-gate`, `s42-arm`, `s42-stomp`, `s42-club`, `s42-death`, `s42-reset`, `s42-stamina`, and one `s42-status`.

- [ ] **Step 1: Implement the timeline** — proxy resolves 0/1 with the measured radii; foot strike 0 and head strike 34; chest→shoulder→arm reaches weakpoint 1; stomp damages and staggers; i-frames block; club damages; lethal hit plays the death clip and pins; reset restores; the full foot→head climb prints the stamina margin.
- [ ] **Step 2: Run it green** and commit `feat(app): S4.2 deterministic selftest`.

---
