# tools/verify/golden — the runner's blessed outputs

`scripts/verify` diffs each adapter section's golden candidates (every viewer run's full output,
headed `# run: <args>`, plus the pixel/probe/decode outputs the adapter `keep`s) against
`<sN>/<section>/<name>.txt` here, after normalising paths (`<OUT>`, `<LIB>`, `<REPO>`, `<GAME>`, the
`# source:` comment) and masking the timing fields in `../masks.py`. `PIN` is the commit the goldens
were blessed on; the runner refuses to run while any frozen `scripts/*-acceptance.sh` that exists at
`PIN` differs from it.

**Not blessed yet.** Blessing waits until S9 merges (design §13.3: goldens come from fresh runs on the
pinned commit, never from files under the shared `out/`).

## How to bless (once, on main after S9 merges; nothing else running on the machine)

1. Check out the pinned commit clean (`git status` empty; `scripts/` must equal `HEAD`).
2. `make shrine-manifest plain-manifest`, then `SOTC_LIB=<the library or a libview> make verify ARGS=--bless`.
   This runs every section twice (VERIFY_RUN is cleared between the passes, so nothing is replayed),
   diffs the two normalised outputs, and writes `<sN>/<section>/*.txt` and `PIN` only if they agree.
3. On `UNSTABLE — <file>: -… | +…`: add the moving field to `MASKS` in `../masks.py` with a comment
   naming the evidence (`python3 -I tools/verify/test_masks.py` must stay green: a field a frozen
   script compares to a literal can never be masked), then bless again.
4. `scripts/verify` → every milestone PASS on the unchanged tree; time it cold (no
   `out/verify/<worktree>-<hash>/stamps/`) and warm; `scripts/verify --long` on the same tree must agree.
5. Agreement check: a one-line change to a selftest print on a scratch branch must make
   `scripts/verify` FAIL naming the line; revert.
6. Commit `tools/verify/golden/` (`test: bless the runner's goldens at <PIN>`).

Re-bless only for a deliberate output change (a new milestone's adapter, a changed selftest line that
the milestone owns), never to make a failure go away.
