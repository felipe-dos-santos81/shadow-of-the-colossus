"""The mask table hides timings and nothing a frozen script pins (§13.2).

For every frozen scripts/sN-acceptance.sh: every field it compares with `=` against a literal — a
`[[ "$(field …)" = "lit" ]]`, a `[[ "$var" = "lit" ]]` on a field, an exact expected line or a
literal one_line/grep pattern — is NOT masked. The timing fields (ms=, frame_ms=, effect_ms,
base_ms, marginal_ms) are masked; path= and mixdown= are normalised to <OUT>, not masked.

Run: python3 -I tools/verify/test_masks.py
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import masks  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MASKED_FIELDS = {field for _, field in masks.MASKS}

FIELD_EQ = re.compile(r'\[\[ "\$\((?:field "\$\w+"|event_field \w+|event_value_at "\$\w+") (\w+)\)" = "[^"$]+" \]\]')
FIELD_VAR = re.compile(r'(\w+)="\$\((?:field "\$\w+"|event_field \w+) (\w+)\)"')
VAR_EQ = re.compile(r'\[\[ "\$(\w+)" = "([^"$][^"]*)" \]\]')
LITERAL_LINE = re.compile(r"""["']\^?(s\d+[a-z0-9]*-[a-z-]+) ([^"']*)["']""")
TOKEN = re.compile(r"(\w+)=([\w.,\\>-]+?)(?=\$|\s|$)")


def pinned(text):
    """({key compared to a literal, prefix unknown}, {(prefix, key, value) from literal lines})."""
    keys = set(FIELD_EQ.findall(text))
    var_key = dict(FIELD_VAR.findall(text))
    keys |= {var_key[v] for v, _ in VAR_EQ.findall(text) if v in var_key}
    lines = set()
    for prefix, rest in LITERAL_LINE.findall(text):
        for key, value in TOKEN.findall(rest):
            if "[" not in value and "(" not in value:
                lines.add((prefix, key, value.replace("\\", "")))
    return keys, lines


def main():
    scripts = sorted(glob.glob(os.path.join(ROOT, "scripts", "s*-acceptance.sh")))
    assert scripts
    total_keys, total_lines = set(), set()
    for path in scripts:
        keys, lines = pinned(open(path).read())
        name = os.path.basename(path)
        clash = keys & MASKED_FIELDS
        assert not clash, "%s compares %s to a literal, but the mask table hides it" % (name, sorted(clash))
        for prefix, key, value in sorted(lines):
            line = "%s %s=%s" % (prefix, key, value)
            assert masks.mask_line(line) == line, "%s pins %r, which the mask table hides" % (name, line)
        total_keys |= keys
        total_lines |= lines
    # the parser must find what it claims to check (an empty parse would pass vacuously)
    for key in ("damage", "health", "gain", "clip", "lock", "frames", "corpse", "fade"):
        assert key in total_keys, "the literal-compared field %s was not found" % key
    assert ("s42-gate", "head", "34") in total_lines and ("s7-idols", "broken_slot", "1") in total_lines

    # the timings are masked, the rest of the line is kept
    cases = {
        "s6-perf frames=240 effect_ms=0.512 base_ms=8.101 marginal_ms=0.411 frame_ms=8.512":
            "s6-perf frames=240 effect_ms=<masked> base_ms=<masked> marginal_ms=<masked> frame_ms=<masked>",
        "s7-perf frames=240 frame_ms=8.339 drawn=170 culled=3393":
            "s7-perf frames=240 frame_ms=<masked> drawn=170 culled=3393",
        "s8-perf frames=240 frame_ms=7.900 drawn=99 culled=12":
            "s8-perf frames=240 frame_ms=<masked> drawn=99 culled=12",
        "s7-load stage=shrine instances=51 meshes=3563 ms=1727.2":
            "s7-load stage=shrine instances=51 meshes=3563 ms=<masked>",
        "s7e2e-load stage=arena meshes=142 ms=564.3": "s7e2e-load stage=arena meshes=142 ms=<masked>",
        "s8-load stage=plain instances=9 meshes=88 ms=901.0": "s8-load stage=plain instances=9 meshes=88 ms=<masked>",
        "s9-perf frames=240 frame_ms=8.365 drawn=120 culled=40":
            "s9-perf frames=240 frame_ms=<masked> drawn=120 culled=40",
        "s9-load stage=plain manifest=road instances=12 backdrops=3 meshes=900 raygrid=64x64 ms=1249.0":
            "s9-load stage=plain manifest=road instances=12 backdrops=3 meshes=900 raygrid=64x64 ms=<masked>",
        "s9-lock k=0.0012 locked=1 ray_m=150.0 hit=1 hit_m=38.0":
            "s9-lock k=0.0012 locked=1 ray_m=150.0 hit=1 hit_m=38.0",
        "s5-event t=2.050 event=music_start clip=music.boss gain=0.45":
            "s5-event t=2.050 event=music_start clip=music.boss gain=0.45",
    }
    for line, want in cases.items():
        assert masks.mask_line(line) == want, (line, masks.mask_line(line))

    # paths are normalised, not masked: the frozen scripts assert path= and mixdown= exactly
    paths = {"<OUT>": ["/w/out/verify/wt"], "<LIB>": ["/lib"], "<REPO>": ["/w"]}
    text = ("s1-screenshot path=/w/out/verify/wt/s7/gpu/stills/fade.png width=2560 height=1440\n"
            "s5-status mixdown=/w/out/verify/wt/s5/gpu/mix.wav events=20 music=1 defeat_stop=1\n"
            "audio: root=/g out=/lib map=/w/tools/re/audio/s8_map.json\n"
            "# source: /other/wt-s9/out/re/stage/valus.manifest (sha256 0, 30 instances)")
    want = ("s1-screenshot path=<OUT>/s7/gpu/stills/fade.png width=2560 height=1440\n"
            "s5-status mixdown=<OUT>/s5/gpu/mix.wav events=20 music=1 defeat_stop=1\n"
            "audio: root=/g out=<LIB> map=<REPO>/tools/re/audio/s8_map.json\n"
            "# source: <REPO>/out/re/stage/valus.manifest (sha256 0, 30 instances)")
    assert masks.normalise(text, paths) == want, masks.normalise(text, paths)
    print("test_masks: OK (%d literal-compared fields and %d literal line fields across %d scripts stay exact)"
          % (len(total_keys), len(total_lines), len(scripts)))


if __name__ == "__main__":
    main()
