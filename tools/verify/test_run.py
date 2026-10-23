"""The runner's orchestration against fake sections (no build, no viewer, no decode): bless writes
normalised goldens only when two runs agree, a run passes on unchanged output and stores stamps, an
unchanged tree re-runs nothing, a changed selftest line fails naming it, --quick skips the e2e
goldens and its pass does not satisfy a full run; a frozen script without an adapter refuses the run;
the audio sections never use a bp-convert published from other sources; ctest starts after the audio
decode and the headless suites finish before any perf-gated section starts.

Run: python3 -I tools/verify/test_run.py
"""
import argparse
import contextlib
import io
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run  # noqa: E402


class Fake(run.Runner):
    line = "s7-status stage=shrine phase=Free lock=0"
    calls = []
    counter = 0
    events = []
    publishes = 0

    def sh(self, cmd, log, env=None, mode="a"):
        if cmd[0] == "ctest":
            Fake.events.append("ctest-start")
            time.sleep(0.2)
            Fake.events.append("ctest-end")
        elif cmd[:2] == ["dotnet", "exec"]:
            Fake.events.append("decode")
        return 0

    def publish(self, log):
        Fake.publishes += 1
        os.makedirs(self.bin, exist_ok=True)
        open(os.path.join(self.bin, "bp-convert.dll"), "w").close()
        self.mark_published()
        return True

    def app_build(self):
        return True

    def pin_check(self):
        return "pin", []

    def run_section(self, m, section):
        Fake.calls.append("%s.%s" % (m, section))
        Fake.events.append("%s.%s" % (m, section))
        Fake.counter += 1
        cand = self.p("candidates", m, section)
        os.makedirs(cand, exist_ok=True)
        with open(os.path.join(cand, "selftest.txt"), "w") as f:   # a timing that moves every run
            f.write("# run: --s7-selftest --s7-stills %s/stills\n%s\ns7-load stage=arena ms=%d.0\n"
                    "s1-screenshot path=%s/stills/fade.png width=2560 height=1440\n"
                    % (self.p(m, section), Fake.line if (m, section) == ("s7", "gpu") else "s3-status ok",
                       Fake.counter, self.p(m, section)))
        if section == "gpu" and m in ("s7", "s8", "s9") and not self.args.quick:
            with open(os.path.join(cand, "e2e.txt"), "w") as f:
                f.write("# run: --%s-e2e\ns7e2e-status stage=shrine\n" % m)
        return True, 0.0, ""


def go(out, *argv):
    args = argparse.Namespace(only=[], quick=False, jobs=1, force=False, dry_run=False, bless=False, long=False)
    for a in argv:
        setattr(args, a, True)
    r = Fake(args)
    r.out, r.bin = out, os.path.join(out, "bin")
    Fake.calls, Fake.events = [], []
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        rc = r.run()
    return rc, buf.getvalue(), sorted(Fake.calls)


def main():
    all_sections = sorted("%s.%s" % (m, s) for m in run.adapters() for s in run.sections_of(m))
    with tempfile.TemporaryDirectory() as tmp:
        os.environ["SOTC_LIB"] = os.path.join(tmp, "lib")
        os.makedirs(os.environ["SOTC_LIB"])
        run.GOLDEN, run.PIN = os.path.join(tmp, "golden"), os.path.join(tmp, "golden", "PIN")
        out = os.path.join(tmp, "out")

        rc, log, calls = go(out)
        assert rc == 1 and "no goldens" in log, log

        rc, log, calls = go(out, "bless")
        assert rc == 0, log
        assert calls == sorted(all_sections * 2), "bless runs every section twice"
        golden = open(os.path.join(run.GOLDEN, "s7", "gpu", "selftest.txt")).read()
        assert "path=<OUT>/s7/gpu/stills/fade.png" in golden and "ms=<masked>" in golden, golden
        assert os.path.exists(run.PIN)

        rc, log, calls = go(out, "force")
        assert rc == 0 and calls == all_sections, log
        assert "verify: s7 PASS" in log and "verify: dotnet PASS" in log, log
        ev = Fake.events
        assert ev.index("decode") < ev.index("ctest-start"), "ctest reads the library: after the decode %s" % ev
        perf = [ev.index(k) for k in ("s4.gpu", "s41.gpu", "s42.gpu", "s6.gpu", "s7.gpu", "s8.gpu", "s9.gpu")]
        assert ev.index("ctest-end") < min(perf), "the perf-gated sections run alone: %s" % ev

        rc, log, calls = go(out)
        assert rc == 0 and calls == [], "an unchanged tree re-runs nothing: %s" % calls
        assert "verify: s3 PASS (0 s, stamps unchanged)" in log, log

        Fake.line = "s7-status stage=shrine phase=Free lock=1"
        rc, log, calls = go(out, "force")
        assert rc == 1 and "verify: s7 FAIL" in log and "+s7-status stage=shrine phase=Free lock=1" in log, log
        assert "verify: s3 PASS" in log, log
        Fake.line = "s7-status stage=shrine phase=Free lock=0"

        rc, log, calls = go(out, "quick", "force")
        assert rc == 0, "quick skips the e2e goldens: %s" % log
        rc, log, calls = go(out)
        assert calls == ["s7.gpu", "s8.gpu", "s9.gpu"], "only the sections with e2e runs owe a full run: %s" % calls

        # the CLI: republished when the dotnet stamp moved, refused when the dotnet build failed
        r = Fake(argparse.Namespace(only=[], quick=False, jobs=1, force=False, dry_run=False, bless=False, long=False))
        r.out, r.bin = out, os.path.join(out, "bin")
        r.published.set()
        r.dotnet_value = "A"
        Fake.publishes = 0
        assert r.ensure_cli() and Fake.publishes == 1, "published from stamp A"
        assert r.ensure_cli() and Fake.publishes == 1, "A again: reused"
        r.dotnet_value = "B"
        assert r.ensure_cli() and Fake.publishes == 2, "src moved (--only sN): republished"
        r.dotnet_failed = True
        assert not r.ensure_cli(), "a failed dotnet build never feeds the audio sections"

        # a frozen script without an adapter refuses the run (as S9's did before s9.sh)
        real = run.adapters
        run.adapters = lambda: [a for a in real() if a != "s9"]
        rc, log, calls = go(out)
        assert rc == 1 and calls == [] and "s9-acceptance.sh has no adapter" in log, log
        run.adapters = real

        Fake.line = "s7-status stage=shrine phase=Free lock=%d"
        orig = Fake.run_section

        def unstable(self, m, section):
            Fake.line = "s7-status stage=shrine phase=Free lock=%d" % Fake.counter
            return orig(self, m, section)
        Fake.run_section = unstable
        rc, log, calls = go(out, "bless")
        assert rc == 1 and "UNSTABLE" in log, log
    print("test_run: OK (bless, pass, stamps, a changed line, --quick, an unstable field)")


if __name__ == "__main__":
    main()
