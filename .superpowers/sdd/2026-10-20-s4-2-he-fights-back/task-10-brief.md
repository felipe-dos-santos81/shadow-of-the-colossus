### Task 10: Acceptance, stills, results

**Files:**
- Create: `scripts/s42-acceptance.sh`, `docs/notes/s42-results.md`
- Modify: `app/README.md`, `AGENTS.md` (workspace root, uncommitted by design)
- Artifact: `out/s42/*.png`

- [ ] **Step 1: Write the script** in the S4.1 house pattern: build/suites/selftest, six stills (stomp telegraph, stomp impact, club swing, hurt, death, arm weakpoint) with guards, perf, `scripts/s41-acceptance.sh` regression (which runs S4→S3).
- [ ] **Step 2: Run to `PASS —`**, write the results note with verbatim numbers, the measured stamina margin, provenance (shell-derived weakpoints; club socket measured), and the user's walk checklist PENDING.
- [ ] **Step 3: Update `app/README.md`** (sdl3_ttf build, `E`/`J`/`A/D`, gamepad west, club) and `AGENTS.md`.
- [ ] **Step 4: Commit** `docs: S4.2 acceptance and results`.

---
## Self-Review Notes

- **Spec coverage:** goals 1–9 map to T2/T3 (weakpoints/gating), T4 (route), T5 (health), T6/T7 (club/attacks), T8 (HUD/gamepad), T1 (records), T9/T10 (acceptance).
- **Pinned values:** HP 100; 35/15/45; 1.0 s; 0.40/1.23/1.165; 4.5/14 m; 30°/s; 4/5 s; capsule hit point; `Grounded` exact.
- **Type consistency:** `ClimbBandId`, `PlayerMode`, `PlayerCombat`, `CombatTuning`, `PlayerFrame`, `HudState`, `ValusAttack`, `Encounter` are defined once and used by name.
- **Review Focus mapping:** (1) T6 bind test + still; (2) T3 capsule test + S4.1 regression; (3) T7 one-impact test; (4) T5 reset + death tests; (5) T3/T9 head-strike 34.
- **Deviation recorded:** `rootMotion` is record-only; the app's lift rule is unchanged (design §11.4).

## Completion (<date>)

Pending — filled when the acceptance run is green and the user's walk is done.
