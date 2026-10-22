### Task 6: Acceptance, results, docs

**Files:**
- Create: `scripts/s5-acceptance.sh`, `docs/notes/s5-results.md`
- Modify: `app/README.md`, `AGENTS.md` (workspace root; tracked since `275d249`)

**Interfaces:**
- Consumes: the `audio` verb (Task 3), `--s5-selftest`/`--audio-mixdown` (Task 5), the S4.2 acceptance.
- Produces: a re-runnable `PASS —` and the measured numbers.

- [ ] **Step 1: Write the script**

House pattern (S4.2 as the model, `caffeinate -dimsu` on every viewer/acceptance call):
1. Build (`BpConvert.sln`, app Release), `dotnet test`, `ctest`.
2. `dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" --out out/library --banks ... --events ...`; clean `out/s5` first; assert the manifest count and every file.
3. `--s5-selftest --audio-mixdown out/s5/mix.wav` under `caffeinate`; capture `s5-*`.
4. Assert the sequence: music start on the first hit; `valus.telegraph` before the club/stomp
   impact; `player.swing`/`hit`; `player.hurt`; `player.death`; `valus.defeat`; music stop; reset
   restores. Hard criteria: music + the goal-3 core cues; ambience only if mapped.
5. WAV checks: 48 kHz, expected duration, RMS in the non-silent band, peak ≤ 0.99, no NaN; a
   per-window RMS check proves music and SFX both present.
6. Perf: the 240-frame run's marginal ≤ 16.7 ms.
7. Regression: `caffeinate -dimsu ./scripts/s42-acceptance.sh`; assert its `PASS —`.
8. Summary with the numbers and `PASS —`.

- [ ] **Step 2: Run to `PASS —`**

Run: `caffeinate -dimsu ./scripts/s5-acceptance.sh`
Expected: `s5-acceptance: PASS — ...`; record the exact numbers.

- [ ] **Step 3: Write the results note and docs**

`docs/notes/s5-results.md`: the decode gate results (per-bank layout, config, channels), the
attribution table with method/confidence, the curated event list, the manifest/WAV counts, the
event sequence, the mixdown RMS/peak, the perf number, the S4.2 regression, and the **user
listening checklist PENDING** (design §12). `app/README.md`: `--s5-selftest`/`--audio-mixdown`,
`M` mute, the curated audio install step. `AGENTS.md`: S5 status and the audio command.

- [ ] **Step 4: Commit**

```bash
git add scripts/s5-acceptance.sh docs/notes/s5-results.md app/README.md AGENTS.md
git commit -m "docs: S5 acceptance and results"
```

---

## Self-Review

**Spec coverage:**
- Goals 1 (build-time decode) → Tasks 0, 2, 3. Goal 2 (AT9 proof) → Task 0. Goal 3 (voice, attribution) → Task 1, 5. Goal 4 (spatial) → Task 4. Goal 5 (deterministic mixer) → Tasks 4, 5. Goal 6 (acceptance) → Task 6. Goal 7 (user walk) → Task 6.
- Non-goals/anti-goals: curated scope (D8), no runtime SXDF (Tasks 3/4 manifest), assets uncommitted (all), no placeholders (gate stop), no frozen-script edits (Task 6 invokes S4.2 only).
- Constraints: windowed/cursor unchanged; SDL3; caffeinate; ≤4 agents (task order allows parser/mixer parallel only after the gates).

**Step scan:** each step names one action and one check; the only open-ended steps are the two RE gates, which carry explicit candidate lists and stop rules. No step says "handle edge cases" or "add tests."

**Type consistency:** `AudioEntry` (Task 3) ↔ `AudioEngine::load_manifest` (Task 4); `Sfx`/`SfxEvent` (Task 5) ↔ `append_*`; `StepEvents` fields (Task 5) ↔ tests; the `audio.manifest` key names match the app parser.

**Review Focus:** each of the six failure modes has an owning task test (see the Review Focus section).

**Proportion:** the plan is longer than the spec's task list but shorter than the code; the two RE gates carry the uncertainty rather than hiding it.

## Execution Handoff

Execution method: the user asked to plan; the plan is complete. Per the writing-plans skill, the user reviews the plan and chooses subagent-driven or native execution before implementation. The recommended method is **subagent-driven**: the two gates are independent, the converter/app tasks have clean interfaces, and a shipped wrong cue is expensive (the ear check is the only identity gate).
