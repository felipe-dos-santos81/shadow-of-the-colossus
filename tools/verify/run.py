#!/usr/bin/env python3
"""The regression runner behind scripts/verify (design D2–D4, rulings §13.1–5 and §13.7).

    scripts/verify [--only NAME]... [--quick] [--jobs N] [--force] [--dry-run] [--bless] [--long]

Per milestone, the adapter tools/verify/sN.sh reproduces every viewer run and every gate of its
frozen scripts/sN-acceptance.sh (test_coverage.py proves the gates are all there). The adapter's
sections (probes | audio | gpu) run only when their content-hash stamp changed (stamps.py); their
golden candidates are path-normalised, timing-masked (masks.py) and diffed against
tools/verify/golden/<sN>/<section>/. dotnet build/test/publish and ctest run once, concurrently
with the GPU runs. The runner refuses to run when a frozen script differs from the blessed pin.

  --only NAME   a milestone (s3 … s8) or a step (dotnet, ctest, audio); repeatable
  --quick       skip the e2e runs (the inner loop); a quick pass never satisfies a full run's stamp
  --jobs N      GPU width for the sections without a perf gate (default 1: D4's measurement first)
  --force       ignore the stamps
  --dry-run     list every step, run and check with its stamp decision, and the gate coverage
  --bless       run every selected section twice, refuse on any unstable line, write the goldens
                and the pin (only on the pinned commit, after S9 merges — §13.3)
  --long        run the highest scripts/sN-acceptance.sh (the milestone gate) instead

Output: out/verify/<worktree>/ (logs/, stamps/, candidates/, <sN>/<section>/, bin/, run/).
"""
import argparse
import concurrent.futures
import difflib
import glob
import os
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import masks  # noqa: E402
import stamps  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
GOLDEN = os.path.join(HERE, "golden")
PIN = os.path.join(GOLDEN, "PIN")
SECTION_ORDER = ("probes", "audio", "gpu")
STEPS = ("dotnet", "ctest", "audio")


def milestone_key(name):
    """s3 -> 3, s41 -> 4.1, s10 -> 10."""
    n = name[1:]
    # ponytail: S4.1/S4.2 are the only sub-milestones; a later sNx would need a separator
    return {"41": 4.1, "42": 4.2}.get(n, float(n) if n.isdigit() else 999.0)


def adapters():
    names = [os.path.basename(p)[:-3] for p in glob.glob(os.path.join(HERE, "s*.sh"))]
    return sorted((n for n in names if re.fullmatch(r"s\d+", n)), key=milestone_key)


def read(path):
    with open(path) as f:
        return f.read()


def squash(text):
    return re.sub(r"\s+", " ", text.replace("\\\n", " ")).strip()


# --- the static gate parser (coverage cross-check and dry-run) --------------------------------

GATE_CALL = re.compile(r"""['"]\^s\d|\bone_line |\be2e_one |\bline_count """)
FAIL_CALL = re.compile(r"""\bfail ["']""")
CONT = ("||", "&&", "|", "\\")
# Frozen gates the adapters deliberately do not copy, with the reason (§13.1).
EXCLUDED = (
    (re.compile(r"regression_log|\$s\d+_log\b|scripts/s\d+-acceptance\.sh"),
     "nested regression: the runner runs every milestone's adapter instead (--long runs the chain)"),
    (re.compile(r"\bs\d+_(marginal|perf_line|pass_line|status_line|status)\b"),
     "parsed from a nested regression's log"),
    (re.compile(r'-x "\$viewer"|-f "\$cli"'),
     "build/publish: run.py's app build and dotnet publish"),
    (re.compile(r"<<'PY'"),
     "heredoc invocation: the body is extracted at run time (heredoc_py)"),
)


def _continues(line):
    s = line.rstrip()
    return s.endswith(CONT) or (s.count("'") % 2 == 1 and not s.lstrip().startswith("#"))


def statements(text):
    """(start, end, lines) of the gate statements before the summary step; heredoc bodies are
    folded into their opener line."""
    lines = text.split("\n")
    out = []
    heredoc_open = {}        # terminator line index -> opener index
    skip = set()
    opener = None
    for i, line in enumerate(lines):
        if line.startswith('stamp "summary"'):
            lines = lines[:i]
            break
        if opener is not None:
            skip.add(i)
            if line == "PY":
                heredoc_open[i] = opener
                opener = None
            continue
        if "<<'PY'" in line:
            opener = i
    for i, line in enumerate(lines):
        if i in skip or line.lstrip().startswith("#"):
            continue
        is_fail = bool(FAIL_CALL.search(line)) and not re.match(r"\s*fail\(\)", line)
        if not is_fail and not GATE_CALL.search(line):
            continue
        j = i
        if is_fail and line.lstrip().startswith("fail ") and i > 0 and not _continues(lines[i - 1]):
            j = i - 1          # inside an `if …; then` / `|| {` block: the header is the condition
            while j > 0 and not re.search(r"(then|\{)\s*$", lines[j]):
                j -= 1
            end = j
        else:
            end = i
        # a heredoc inside a $( … ) (s3's `python3 - <<'PY'`) folds into its statement; a
        # standalone one (s4's `cat > file <<'PY'`) is a statement of its own
        while j > 0 and (_continues(lines[j - 1])
                         or ((j - 1) in heredoc_open and lines[j].lstrip().startswith(")"))):
            j = heredoc_open.get(j - 1, j - 1)
        body = [lines[k] for k in range(j, end + 1) if k not in skip]
        if end == i and is_fail:
            body[-1] = body[-1][:FAIL_CALL.search(body[-1]).start()]
        cond = squash("\n".join(body))
        cond = re.sub(r"\s*\|\|\s*$", "", cond)
        if cond:
            out.append((i + 1, cond))
    return out


def frozen_gates(milestone):
    """[(line, condition, excluded_reason or None)] of scripts/<milestone>-acceptance.sh."""
    text = read(os.path.join(ROOT, "scripts", "%s-acceptance.sh" % milestone))
    seen, gates = set(), []
    for line, cond in statements(text):
        if cond in seen:
            continue
        seen.add(cond)
        reason = next((why for rx, why in EXCLUDED if rx.search(cond)), None)
        gates.append((line, cond, reason))
    return gates


def heredoc_count(milestone):
    text = read(os.path.join(ROOT, "scripts", "%s-acceptance.sh" % milestone))
    return sum(1 for line in text.split("\n") if "<<'PY'" in line)


def coverage(milestone):
    """(gates, missing, excluded, heredocs_missing) — every non-excluded frozen gate condition
    must appear verbatim (whitespace-normalised) in the adapter or common.sh."""
    text = read(os.path.join(HERE, milestone + ".sh")) + "\n" + read(os.path.join(HERE, "common.sh"))
    adapter = squash("\n".join(l for l in text.split("\n") if not l.lstrip().startswith("#")))
    gates = frozen_gates(milestone)
    missing = [(n, c) for n, c, why in gates if why is None and c not in adapter]
    excluded = [(n, c, why) for n, c, why in gates if why is not None]
    heredocs = [k for k in range(1, heredoc_count(milestone) + 1)
                if "heredoc_py %s %d" % (milestone, k) not in adapter]
    return gates, missing, excluded, heredocs


def sections_of(milestone):
    """{section: body text} of an adapter, in run order."""
    text = read(os.path.join(HERE, milestone + ".sh"))
    marks = [(m.group(1), m.start()) for m in re.finditer(r"^section_(\w+)\(\) \{$", text, re.M)]
    end = text.find("\nrun_section ")
    bodies = {}
    for k, (name, start) in enumerate(marks):
        stop = marks[k + 1][1] if k + 1 < len(marks) else end
        bodies[name] = text[start:stop]
    return {s: bodies[s] for s in SECTION_ORDER if s in bodies}


CHECK = re.compile(r'checked \S+|heredoc_py \w+ \d|python3 "\$gate_script"|python3 "\$audition"|'
                   r'python3 -I "\$points_gen"|dotnet exec "\$cli" audio|ffprobe -v|ffmpeg -hide_banner|cmp -s|diff -u')


def plan(body):
    """The viewer runs (direct invocations + wrapper call sites) and the checks of a section."""
    lines = body.split("\n")
    wrappers, runs, inside = set(), [], None
    for i, line in enumerate(lines):
        m = re.match(r"^(\w+)\(\) \{$", line)
        if m and not line.startswith("section_"):
            inside = m.group(1)
            continue
        if inside and line == "}":
            inside = None
            continue
        if '"$viewer"' in line:
            if inside:
                wrappers.add(inside)
            else:
                j = i
                while j > 0 and _continues(lines[j - 1]):
                    j -= 1
                k = i
                while _continues(lines[k]) and k + 1 < len(lines):
                    k += 1
                runs.append(squash("\n".join(lines[j:k + 1])))
    for line in lines:
        m = re.match(r"^\s*(\w[\w-]*) ", line)
        if m and m.group(1) in wrappers:
            runs.append(squash(line))
    checks = sorted({m.group(0) for m in CHECK.finditer(body)})
    gates = len(FAIL_CALL.findall(body))
    perf = bool(re.search(r"\bperf_(ms|line)\b", body))
    return runs, checks, gates, perf


# --- the run ---------------------------------------------------------------------------------

class Runner:
    def __init__(self, args):
        self.args = args
        self.lib = os.environ.get("SOTC_LIB") or os.path.join(ROOT, "out", "library")
        self.game = os.environ.get("SOTC_GAME_ROOT", "")
        self.out = os.path.join(ROOT, "out", "verify", os.path.basename(ROOT))
        self.bin = os.path.join(self.out, "bin")
        self.results = []          # (name, ok, seconds, reason)
        self.published = threading.Event()
        self.threads = []

    # paths ----------------------------------------------------------------------------------
    def p(self, *parts):
        return os.path.join(self.out, *parts)

    def norm_paths(self):
        both = lambda p: [p, os.path.realpath(p)] if p else []  # noqa: E731
        return {"<OUT>": both(self.out), "<LIB>": both(self.lib), "<REPO>": both(ROOT), "<GAME>": both(self.game)}

    def env(self, **extra):
        env = dict(os.environ, SOTC_LIB=self.lib, SOTC_GAME_ROOT=self.game)
        env.update(extra)
        return env

    def sh(self, cmd, log, env=None, mode="a"):
        os.makedirs(os.path.dirname(log), exist_ok=True)
        with open(log, mode) as f:
            f.write("$ %s\n" % " ".join(cmd))
            f.flush()
            return subprocess.call(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, env=env or self.env())

    # stamps ---------------------------------------------------------------------------------
    def stamp_file(self, key):
        return self.p("stamps", key + ".stamp")

    def fresh(self, key, value):
        """True when the stored stamp satisfies this run (a quick pass never satisfies a full run)."""
        if self.args.force or self.args.bless:
            return False
        try:
            stored = read(self.stamp_file(key)).strip()
        except OSError:
            return False
        return stored == value or (self.args.quick and stored == value + ":quick")

    def store(self, key, value, quick=False):
        os.makedirs(self.p("stamps"), exist_ok=True)
        with open(self.stamp_file(key), "w") as f:
            f.write(value + (":quick" if quick else "") + "\n")

    def record(self, name, ok, seconds, reason):
        self.results.append((name, ok, seconds, reason))
        print("verify: %s %s (%.0f s, %s)" % (name, "PASS" if ok else "FAIL", seconds, reason), flush=True)

    # the pin --------------------------------------------------------------------------------
    def pin_check(self):
        """(pin, problems): every frozen scripts/*-acceptance.sh at the pin must be unchanged."""
        if not os.path.exists(PIN):
            return None, ["no pin: tools/verify/golden/PIN is absent (the goldens are not blessed)"]
        pin = read(PIN).strip()
        names = subprocess.run(["git", "-C", ROOT, "ls-tree", "--name-only", pin, "scripts/"],
                               capture_output=True, text=True).stdout.split()
        frozen = [n for n in names if n.endswith("-acceptance.sh")]
        if not frozen:
            return pin, ["the pin %s lists no frozen scripts" % pin]
        bad = [n for n in frozen if subprocess.call(["git", "-C", ROOT, "diff", "--quiet", pin, "--", n]) != 0]
        return pin, ["%s differs from the pin %s" % (n, pin[:12]) for n in bad]

    # selection ------------------------------------------------------------------------------
    def selected(self):
        names = adapters()
        only = self.args.only
        unknown = [o for o in only if o not in names and o not in STEPS]
        if unknown:
            sys.exit("verify: unknown --only %s (milestones %s, steps %s)" % (unknown, names, list(STEPS)))
        milestones = [m for m in names if not only or m in only]
        steps = [s for s in STEPS if not only or s in only or (s == "audio" and milestones)]
        # (the runner's audio decode is every milestone's prerequisite: the GPU runs read its library)
        return milestones, steps

    def current_map(self):
        maps = glob.glob(os.path.join(ROOT, "tools", "re", "audio", "s*_map.json"))
        return max(maps, key=lambda p: milestone_key(os.path.basename(p).split("_")[0]))

    # dry run --------------------------------------------------------------------------------
    def dry_run(self):
        milestones, steps = self.selected()
        pin, problems = self.pin_check()
        print("verify: dry run — root %s, out %s, library %s" % (ROOT, self.out, os.path.realpath(self.lib)))
        print("pin: %s%s" % (pin or "none", "".join("\n  REFUSE: " + p for p in problems)))
        st = stamps.Stamper(ROOT, self.lib)
        for step in steps:
            value = st.step(step)
            what = {"dotnet": "dotnet build BpConvert.sln; dotnet publish src/BpConvert.Cli -> <OUT>/bin; "
                              "dotnet test tests/BpConvert.Tests",
                    "ctest": "cmake build app/build (always, incremental); ctest --test-dir app/build",
                    "audio": "bp-convert audio --map %s --force into the library (the current map)"
                             % os.path.relpath(self.current_map(), ROOT)}[step]
            print("step %-7s %s — %s" % (step, "skip (stamp unchanged)" if self.fresh(step, value) else "RUN", what))
        total_runs = total_gates = 0
        for m in milestones:
            gates, missing, excluded, heredocs = coverage(m)
            print("\n%s — frozen scripts/%s-acceptance.sh: %d gate conditions, %d present in the adapter, "
                  "%d excluded, %d missing; %d heredoc(s) extracted at run time%s"
                  % (m, m, len(gates), len(gates) - len(missing) - len(excluded), len(excluded),
                     len(missing), heredoc_count(m), " (MISSING %s)" % heredocs if heredocs else ""))
            for n, c in missing:
                print("  MISSING gate (line %d): %s" % (n, c[:160]))
            for section, body in sections_of(m).items():
                runs, checks, ngates, perf = plan(body)
                key = "%s.%s" % (m, section)
                decision = "skip (stamp unchanged)" if self.fresh(key, st.section(m, section)) else "RUN"
                print("  %s.%s: %s — %d viewer run(s), %d gate statement(s)%s"
                      % (m, section, decision, len(runs), ngates, ", perf-bearing (runs alone)" if perf else ""))
                for r in runs:
                    tag = " [e2e: skipped by --quick]" if "-e2e" in r else ""
                    print("    run: %s%s" % (r, tag))
                for c in checks:
                    print("    check: %s" % c)
                total_runs += len(runs)
                total_gates += ngates
        print("\nverify: dry run — %d milestone(s), %d viewer runs, %d gate statements" % (len(milestones), total_runs, total_gates))
        return 0

    # steps ----------------------------------------------------------------------------------
    def app_build(self):
        log = self.p("logs", "app-build.log")
        t = time.time()
        ok = (self.sh(["cmake", "-S", "app", "-B", "app/build", "-DCMAKE_BUILD_TYPE=Release"], log, mode="w") == 0
              and self.sh(["cmake", "--build", "app/build", "-j"], log) == 0)
        if not ok:
            self.record("app-build", False, time.time() - t, "see %s" % log)
        return ok

    def publish(self, log):
        return self.sh(["dotnet", "publish", "src/BpConvert.Cli", "-c", "Release", "-o", self.bin], log) == 0

    def dotnet_job(self, value):
        log = self.p("logs", "dotnet.log")
        t = time.time()
        ok = self.sh(["dotnet", "build", "BpConvert.sln", "-v", "q"], log, mode="w") == 0 and self.publish(log)
        self.published.set()
        ok = ok and self.sh(["dotnet", "test", "tests/BpConvert.Tests", "-v", "q"], log) == 0
        if ok:
            self.store("dotnet", value)
        self.record("dotnet", ok, time.time() - t, "build, publish, test" if ok else "see %s" % log)

    def ctest_job(self, value):
        log = self.p("logs", "ctest.log")
        t = time.time()
        ok = self.sh(["ctest", "--test-dir", "app/build", "--output-on-failure"], log, mode="w") == 0
        if ok:
            self.store("ctest", value)
        self.record("ctest", ok, time.time() - t, "ctest" if ok else "see %s" % log)

    def ensure_cli(self):
        self.published.wait()
        if not os.path.exists(os.path.join(self.bin, "bp-convert.dll")):
            self.publish(self.p("logs", "publish.log"))
        return os.path.exists(os.path.join(self.bin, "bp-convert.dll"))

    def audio_step(self, value):
        """The current (highest) map decoded once per audio stamp into the library the GPU runs read
        (§13.5: the library holds whatever the last frozen script left — 8 or 10 — and S8+ needs 34)."""
        log = self.p("logs", "audio.log")
        t = time.time()
        ok = self.ensure_cli() and self.sh(
            ["dotnet", "exec", os.path.join(self.bin, "bp-convert.dll"), "audio", "--root", self.game,
             "--out", self.lib, "--map", self.current_map(), "--force"], log, mode="w") == 0
        if ok:
            self.store("audio", value)
        self.record("audio", ok, time.time() - t, os.path.basename(self.current_map()) if ok else "see %s" % log)
        return ok

    # sections -------------------------------------------------------------------------------
    def run_section(self, m, section):
        """Runs one adapter section; returns (ok, seconds, reason)."""
        work, cand = self.p(m, section), self.p("candidates", m, section)
        for d in (work, cand):
            shutil.rmtree(d, ignore_errors=True)
        log = self.p("logs", "%s-%s.log" % (m, section))
        env = self.env(VERIFY_MS=m, VERIFY_OUT=work, VERIFY_CAND=cand, VERIFY_RUN=self.p("run"),
                       VERIFY_BIN=self.bin, VERIFY_QUICK="1" if self.args.quick else "")
        t = time.time()
        rc = self.sh(["bash", os.path.join(HERE, m + ".sh"), section], log, env=env, mode="w")
        seconds = time.time() - t
        if rc != 0:
            lines = [l for l in read(log).split("\n") if l.strip()]
            why = next((l for l in reversed(lines) if l.startswith("verify %s:" % m)), lines[-1] if lines else "")
            return False, seconds, "%s: %s (log %s)" % (section, why, os.path.relpath(log, ROOT))
        return True, seconds, ""

    def candidates(self, m, section):
        cand = self.p("candidates", m, section)
        paths = self.norm_paths()
        return {os.path.basename(f): masks.normalise(read(f), paths) for f in sorted(glob.glob(os.path.join(cand, "*.txt")))}

    def golden_diff(self, m, section):
        """The first mismatch between the normalised candidates and the goldens, or ''."""
        gdir = os.path.join(GOLDEN, m, section)
        got = self.candidates(m, section)
        want = {os.path.basename(f): read(f) for f in sorted(glob.glob(os.path.join(gdir, "*.txt")))}
        if not want:
            return "no goldens in %s (bless after S9 merges)" % os.path.relpath(gdir, ROOT)
        for name, text in want.items():
            if self.args.quick and "-e2e" in text.split("\n", 1)[0]:
                continue
            if name not in got:
                return "golden %s/%s/%s: no candidate (the run did not happen)" % (m, section, name)
            if got[name] != text:
                diff = [l for l in difflib.unified_diff(text.split("\n"), got[name].split("\n"), lineterm="", n=0)
                        if l[:1] in "+-" and not l.startswith(("+++", "---"))]
                return "golden %s/%s/%s differs: %s" % (m, section, name, " | ".join(diff[:4]))
        extra = sorted(set(got) - set(want))
        if extra:
            return "candidates without a golden in %s/%s: %s" % (m, section, ", ".join(extra))
        return ""

    def sections(self, milestones, keyfn):
        """[(milestone, section, stamp)] whose stamp is not fresh (never passed, or an input changed)."""
        todo = []
        for m in milestones:
            for section in sections_of(m):
                value = keyfn(m, section)
                if not self.fresh("%s.%s" % (m, section), value):
                    todo.append((m, section, value))
        return todo

    def execute(self, todo, bless=False):
        """Runs the sections; perf-bearing GPU sections alone, the rest up to --jobs wide.
        Returns {(m, section): (ok, seconds, reason)}."""
        outcome = {}

        def one(item):
            m, section, value = item
            ok, seconds, reason = self.run_section(m, section)
            if ok and not bless:
                reason = self.golden_diff(m, section)
                ok = not reason
                if ok:   # a quick pass of a section with e2e runs never satisfies a full run
                    e2e = any("-e2e" in r for r in plan(sections_of(m)[section])[0])
                    self.store("%s.%s" % (m, section), value, quick=self.args.quick and e2e)
            outcome[(m, section)] = (ok, seconds, reason)
            print("verify:   %s.%s %s (%.0f s)%s" % (m, section, "ok" if ok else "FAILED", seconds,
                                                     "" if ok else " — " + reason), flush=True)

        perf = [i for i in todo if plan(sections_of(i[0])[i[1]])[3]]
        rest = [i for i in todo if i not in perf]
        with concurrent.futures.ThreadPoolExecutor(max(1, self.args.jobs)) as pool:
            list(pool.map(one, rest))
        for item in perf:
            one(item)
        return outcome

    def summarise(self, milestones, outcome, todo=(), why=""):
        """One line per milestone; a section that was due but never ran (why) is a failure."""
        for m, s, _ in todo:
            outcome.setdefault((m, s), (False, 0.0, "%s: not run (%s)" % (s, why)))
        for m in milestones:
            mine = [(s, outcome[(m, s)]) for s in sections_of(m) if (m, s) in outcome]
            seconds = sum(o[1] for _, o in mine)
            failed = [o[2] for _, o in mine if not o[0]]
            if failed:
                self.record(m, False, seconds, failed[0])
            elif not mine:
                self.record(m, True, 0, "stamps unchanged")
            else:
                ran = ",".join(s for s, _ in mine)
                self.record(m, True, seconds, "ran %s%s" % (ran, "; quick: e2e skipped" if self.args.quick else ""))

    # modes ----------------------------------------------------------------------------------
    def long(self):
        scripts = glob.glob(os.path.join(ROOT, "scripts", "s*-acceptance.sh"))
        name = max((os.path.basename(p).split("-")[0] for p in scripts), key=milestone_key)
        log = self.p("logs", "long-%s.log" % name)
        t = time.time()
        rc = self.sh(["bash", os.path.join(ROOT, "scripts", "%s-acceptance.sh" % name)], log, mode="w")
        ok = rc == 0 and re.search(r"^%s-acceptance: PASS" % name, read(log), re.M) is not None
        self.record("long " + name, ok, time.time() - t, "scripts/%s-acceptance.sh (log %s)" % (name, os.path.relpath(log, ROOT)))
        return 0 if ok else 1

    def run(self):
        milestones, steps = self.selected()
        pin, problems = self.pin_check()
        if self.args.bless:   # goldens come from the committed frozen scripts (§13.3)
            dirty = subprocess.call(["git", "-C", ROOT, "diff", "--quiet", "HEAD", "--", "scripts/"]) != 0
            problems = ["scripts/ differs from HEAD: bless on the clean pinned commit"] if dirty else []
        if problems:
            for p in problems:
                print("verify: REFUSE — " + p, file=sys.stderr)
            return 1
        shutil.rmtree(self.p("run"), ignore_errors=True)
        os.makedirs(self.p("run"), exist_ok=True)
        st = stamps.Stamper(ROOT, self.lib)
        need_gpu = any("gpu" in sections_of(m) for m in milestones)
        if ("ctest" in steps or need_gpu) and not self.app_build():
            return 1
        self.published.set()            # cleared while a dotnet job will publish the CLI
        for step, job in (("dotnet", self.dotnet_job), ("ctest", self.ctest_job)):
            if step not in steps:
                continue
            if self.fresh(step, st.step(step)):
                self.record(step, True, 0, "stamp unchanged")
                continue
            if step == "dotnet":
                self.published.clear()
            th = threading.Thread(target=job, args=(st.step(step),))
            th.start()
            self.threads.append(th)

        early = [i for i in self.sections(milestones, st.section) if i[1] != "gpu"]
        if any(i[1] == "audio" for i in early) and not self.ensure_cli():
            self.record("publish", False, 0, "no bp-convert.dll in %s" % self.bin)
        outcome = self.execute(early, bless=self.args.bless)
        audio_ok = True
        if "audio" in steps:
            if self.fresh("audio", st.step("audio")):
                self.record("audio", True, 0, "stamp unchanged")
            else:
                audio_ok = self.audio_step(st.step("audio"))
        st = stamps.Stamper(ROOT, self.lib)   # the decode may have rewritten audio.manifest
        gpu = [i for i in self.sections(milestones, st.section) if i[1] == "gpu"]
        if audio_ok:
            outcome.update(self.execute(gpu, bless=self.args.bless))
        for th in self.threads:
            th.join()
        if self.args.bless and audio_ok:
            return self.bless(milestones, outcome, early + gpu)
        self.summarise(milestones, outcome, gpu, "the audio decode failed")
        return 0 if all(r[1] for r in self.results) else 1

    def bless(self, milestones, first, todo):
        """Second run of every section, diff the two, write the goldens and the pin (§13.3)."""
        if not all(o[0] for o in first.values()):
            self.summarise(milestones, first)
            return 1
        snapshot = {(m, s): self.candidates(m, s) for m, s, _ in todo}
        shutil.rmtree(self.p("run"), ignore_errors=True)
        os.makedirs(self.p("run"), exist_ok=True)
        second = self.execute(todo, bless=True)
        unstable = []
        for m, s, _ in todo:
            again = self.candidates(m, s)
            for name in sorted(set(snapshot[(m, s)]) | set(again)):
                a, b = snapshot[(m, s)].get(name, ""), again.get(name, "")
                if a != b:
                    lines = [l for l in difflib.unified_diff(a.split("\n"), b.split("\n"), lineterm="", n=0)
                             if l[:1] in "+-" and not l.startswith(("+++", "---"))]
                    unstable.append("%s/%s/%s: %s" % (m, s, name, " | ".join(lines[:4])))
        if unstable or not all(o[0] for o in second.values()):
            for u in unstable:
                print("verify: UNSTABLE — %s (add the field to masks.MASKS with a comment)" % u, file=sys.stderr)
            self.summarise(milestones, second)
            return 1
        for m, s, _ in todo:
            gdir = os.path.join(GOLDEN, m, s)
            shutil.rmtree(gdir, ignore_errors=True)
            os.makedirs(gdir)
            for name, text in snapshot[(m, s)].items():
                with open(os.path.join(gdir, name), "w") as f:
                    f.write(text)
        head = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
        with open(PIN, "w") as f:
            f.write(head + "\n")
        print("verify: blessed %d section(s) at %s; commit tools/verify/golden/ (then run scripts/verify)" % (len(todo), head[:12]))
        return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="scripts/verify", description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", action="append", default=[], metavar="NAME")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--bless", action="store_true")
    ap.add_argument("--long", action="store_true")
    args = ap.parse_args(argv)
    if args.bless and args.quick:
        sys.exit("verify: --bless needs the full run (no --quick)")
    runner = Runner(args)
    if args.dry_run:
        return runner.dry_run()
    if args.long:
        return runner.long()
    return runner.run()


if __name__ == "__main__":
    sys.exit(main())
