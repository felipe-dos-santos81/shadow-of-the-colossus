"""Golden normalisation for the regression runner (design §13.2).

Paths are normalised, not masked: the frozen scripts assert `path=` and `mixdown=` exactly, so the
runner rewrites the out-dir / library / repo / game-tree prefixes to placeholders and keeps the rest.
Timing fields are masked. Every field a frozen script compares to a literal stays exact
(test_masks.py parses the scripts and proves it).
"""
import re

# (line prefix, field): the value of `field=` on a line starting with `prefix` becomes <masked>.
# Timing only. Blessing (two fresh runs diffed, §13.3) appends any other unstable field here, each
# with a comment naming the evidence.
MASKS = (
    ("s6-perf ", "effect_ms"),
    ("s6-perf ", "base_ms"),
    ("s6-perf ", "marginal_ms"),
    ("s6-perf ", "frame_ms"),
    ("s7-perf ", "frame_ms"),
    ("s8-perf ", "frame_ms"),
    ("s9-perf ", "frame_ms"),
    ("s7-load ", "ms"),       # the arena/shrine/plain load wall time (also inside the s8 e2e)
    ("s7e2e-load ", "ms"),
    ("s8-load ", "ms"),
    ("s9-load ", "ms"),       # the road load wall time (the s9 e2e's loads are s7-load lines)
    ("s11-perf ", "frame_ms"),  # S11 (Task 10's two runs: 8.345 / 8.289)
    ("s11-load ", "ms"),        # the arena_02 load wall time (1073.7 / 891.6)
    ("s11e2e-load ", "ms"),     # the same load inside the e2e (952.0 / 869.7)
    ("s12-perf ", "frame_ms"),  # S12 (Task 9's two runs)
    ("s12-load ", "ms"),        # the arena_03 load wall time
    ("s12e2e-load ", "ms"),     # the same load inside the e2e
    ("s13-perf ", "frame_ms"),  # S13 (Task 16's two runs)
    ("s13-load ", "ms"),        # the arena_04 load wall time
    ("s13e2e-load ", "ms"),     # the same load inside the e2e
)
MASKED = "<masked>"

# compose-encounter writes the composing worktree's absolute path into its `# source:` comment.
_SOURCE = re.compile(r"^(# source: )\S*/(out/re/stage/)", re.M)


def mask_line(line):
    for prefix, field in MASKS:
        if line.startswith(prefix):
            line = re.sub(r"(?<= )%s=\S*" % re.escape(field), "%s=%s" % (field, MASKED), line)
    return line


def normalise(text, paths):
    """paths: {placeholder: [path, ...]}. The longest path is replaced first, so the runner's out
    dir (under the repo) wins over the repo root."""
    pairs = sorted(((p, ph) for ph, ps in paths.items() for p in ps if p), key=lambda t: -len(t[0]))
    for path, placeholder in pairs:
        text = text.replace(path, placeholder)
    text = _SOURCE.sub(r"\1<REPO>/\2", text)
    return "\n".join(mask_line(line) for line in text.split("\n"))
