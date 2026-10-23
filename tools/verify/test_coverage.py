"""The static cross-check (§13.1, anti-goal A1): every gate condition of every frozen
scripts/sN-acceptance.sh appears verbatim in its adapter tools/verify/sN.sh (or common.sh), unless it
is one of run.EXCLUDED's classes (nested regressions, builds, heredoc invocations); every frozen
heredoc is extracted by its adapter; every frozen script has an adapter. No viewer is run: the
adapters' plumbing (common.sh) is exercised against a stub viewer.

Run: python3 -I tools/verify/test_coverage.py
"""
import glob
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run  # noqa: E402

# Frozen scripts before the runner's scope (S1's model viewer, M0's converter, RE-0) are not adapted.
NOT_ADAPTED = {"m0", "re0", "s1"}
# The viewer runs each frozen script makes (§13.1: ≈27 across s3–s7, plus s8's; s9's 8 = six flag
# refusals + the selftest + the e2e).
RUNS = {"s3": 4, "s4": 8, "s41": 3, "s42": 4, "s5": 2, "s6": 1, "s7": 2, "s8": 2, "s9": 8}


def plumbing():
    """common.sh against a stub viewer: candidates named after the arguments, identical runs replayed
    from the cache (but never a 240-frame perf run), exit status passed through, heredocs extracted."""
    with tempfile.TemporaryDirectory() as tmp:
        stub = os.path.join(tmp, "stub")
        with open(stub, "w") as f:
            f.write('#!/bin/bash\necho call >> "%s/calls"\necho "s1-out $*"\n'
                    '[[ " $* " = *" --fail "* ]] && exit 3\nexit 0\n' % tmp)
        os.chmod(stub, 0o755)
        env = dict(os.environ, VERIFY_MS="s3", VERIFY_OUT=os.path.join(tmp, "s3", "gpu"),
                   VERIFY_CAND=os.path.join(tmp, "cand"), VERIFY_RUN=os.path.join(tmp, "run"),
                   VERIFY_BIN=os.path.join(tmp, "bin"))
        script = r"""
. "%s/common.sh"
viewer_bin="%s"
a="$("$viewer" --library L --manifest /x/enc.manifest --model M --s42-selftest)"
b="$(caffeinate -dimsu "$viewer" --library L --manifest /x/enc.manifest --model M --s42-selftest)"
[[ "$a" = "$b" && "$a" = "s1-out --library L --manifest /x/enc.manifest --model M --s42-selftest" ]]
"$viewer" --library L --manifest m --frames 240 > /dev/null
"$viewer" --library L --manifest m --frames 240 > /dev/null
status=0; "$viewer" --library L --fail > /dev/null || status=$?
[[ "$status" = 3 ]]
for m in s3 s4 s41 s6 s7 s8 s9; do python3 -m py_compile "$(heredoc_py "$m" 1)"; done
""" % (run.HERE, stub)
        subprocess.run(["bash", "-c", script], env=env, check=True)
        calls = open(os.path.join(tmp, "calls")).read().count("call")
        assert calls == 4, "%d viewer calls (want 4: one replayed run, two perf runs, one failing)" % calls
        names = sorted(os.listdir(os.path.join(tmp, "cand")))
        assert names == sorted(["fail.txt", "manifest_m_frames_240.txt", "manifest_m_frames_240-2.txt",
                                "manifest_enc_s42-selftest.txt", "manifest_enc_s42-selftest-2.txt"]), names
        head = open(os.path.join(tmp, "cand", "manifest_enc_s42-selftest.txt")).readline()
        assert head == "# run: --library L --manifest /x/enc.manifest --model M --s42-selftest\n", head


def main():
    plumbing()
    frozen = sorted((os.path.basename(p).split("-")[0]
                     for p in glob.glob(os.path.join(run.ROOT, "scripts", "*-acceptance.sh"))),
                    key=run.milestone_key)
    frozen = [f for f in frozen if f not in NOT_ADAPTED]
    assert frozen == run.adapters(), "frozen %s vs adapters %s" % (frozen, run.adapters())
    problems, rows = [], []
    for m in frozen:
        gates, missing, excluded, heredocs = run.coverage(m)
        assert gates, m
        for n, cond in missing:
            problems.append("%s: frozen line %d not in the adapter: %s" % (m, n, cond))
        for k in heredocs:
            problems.append("%s: heredoc %d is not extracted (heredoc_py %s %d)" % (m, k, m, k))
        runs = sum(len(run.plan(body)[0]) for body in run.sections_of(m).values())
        if runs != RUNS[m]:
            problems.append("%s: %d viewer runs in the adapter, the frozen script makes %d" % (m, runs, RUNS[m]))
        rows.append("%s %d/%d (%d excluded)" % (m, len(gates) - len(missing) - len(excluded),
                                                len(gates) - len(excluded), len(excluded)))
    # The parser must see the gates it is meant to see (a parser that finds nothing proves nothing).
    s42 = [c for _, c, _ in run.frozen_gates("s42")]
    assert any("^s42-gate foot=0 head=34$" in c for c in s42), "one_line literal gates are parsed"
    assert any('[[ "$stamina_remaining" -gt 0 && "$stamina_remaining" -lt 100 ]]' == c for c in s42)
    s5 = [c for _, c, _ in run.frozen_gates("s5")]
    assert any("exit !(d <= 0.05)" in c for c in s5), "multi-line awk gates are parsed"
    assert any(c.startswith("if ! s42_off_out=") for c in s5), "if-block headers are parsed"
    assert not any("s5-acceptance: PASS" in c for c in s5), "the summary is not a gate"
    if problems:
        print("\n".join(problems))
        raise SystemExit("test_coverage: %d problem(s)" % len(problems))
    print("test_coverage: OK — present/required per milestone: " + ", ".join(rows))


if __name__ == "__main__":
    main()
