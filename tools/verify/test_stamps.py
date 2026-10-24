"""Changing one input of each class changes exactly the stamps of the steps that read that class.

Run: python3 -I tools/verify/test_stamps.py
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stamps  # noqa: E402

MILESTONES = {"s3": ["gpu"], "s4": ["probes", "gpu"], "s41": ["gpu"], "s42": ["gpu"], "s5": ["audio", "gpu"],
              "s6": ["probes", "audio", "gpu"], "s7": ["probes", "gpu"], "s8": ["probes", "audio", "gpu"],
              "s9": ["probes", "audio", "gpu"], "s11": ["gpu"], "s12": ["gpu"]}
STEPS = ["dotnet", "ctest", "audio"]
ALL = STEPS + ["%s.%s" % (m, s) for m, ss in MILESTONES.items() for s in ss]


def by(*classes):
    """The steps that read any of the classes."""
    return {k for k in ALL if k.split(".")[-1] in classes}


# the audio sections (not the runner's decode step) run s9's preflight: the clips, the sword PNGs,
# the placements and the manifests
AUDIO_SECTIONS = {k for k in ALL if k.endswith(".audio")}


# the sections that read each stage manifest (what the adapters pass the viewer / check / preflight)
def of(*milestones, **only):
    return {k for k in ALL if k.split(".")[0] in milestones} | set(only.get("plus", ()))


MANIFEST_READERS = {
    "valus": {"s3.gpu", "s4.probes"},
    "encounter": of(*[m for m in MILESTONES if m not in ("s3", "s11", "s12")]) - {"s7.audio"},
    "shrine": of("s7", "s8", "s9"),
    "plain": of("s8", "s9"),
    "road": of("s9"),
    "arena_02": of("s11"),
    "arena_03": of("s12"),
}


# file -> the steps whose stamp must change when only that file changes
CASES = {
    "src/BpConvert.Core/Audio/AudioConverter.cs": by("dotnet", "audio"),
    "tests/BpConvert.Tests/T.cs": by("dotnet"),
    "BpConvert.sln": by("dotnet"),
    "Directory.Build.props": by("dotnet"),
    "tools/native/liboo2core_8_win64_.dylib": by("dotnet"),
    "tools/re/audio/s8_map.json": by("dotnet", "audio", "ctest") | {"s8.gpu"},
    "tools/re/audio/s5_map.json": by("dotnet", "audio", "ctest") | {"s5.gpu"},
    "tools/re/audio/s9_map.json": by("dotnet", "audio", "ctest") | {"s9.gpu"},
    "tools/re/audio/s5_audition.py": by("audio"),
    "app/src/app.cpp": by("ctest", "gpu"),
    "app/shaders/mesh.metal": by("ctest", "gpu"),
    "app/CMakeLists.txt": by("ctest", "gpu"),
    "app/src/shrine_points.h": by("ctest", "gpu"),
    "app/src/ride_points.h": by("ctest", "gpu"),
    "app/src/road_points.h": by("ctest", "gpu"),
    **{"out/re/stage/%s.manifest" % n: s for n, s in MANIFEST_READERS.items()},
    "tools/re/stage/probe.py": by("probes", "gpu"),   # s9.gpu re-composes the road
    "tools/re/anb/probe.py": by("probes"),
    "tools/re/s6/s6_asset_gate.py": by("probes"),
    "tools/re/s7/shrine_placements.json": by("probes", "gpu"),
    "tools/re/s8/placements.json": by("probes", "gpu"),
    "tools/re/s8/ride_points.py": by("gpu"),
    "tools/re/s9/placements.json": by("probes", "gpu") | AUDIO_SECTIONS,
    "tools/re/s9/road_points.py": by("probes", "gpu"),
    "out/re/stage/arena_paths.txt": by("probes"),
    "lib/models/a/a.glb": by("ctest", "probes", "gpu") | AUDIO_SECTIONS,
    "lib/models/a/a.clips.json": by("ctest", "probes", "gpu") | AUDIO_SECTIONS,
    "lib/models/a/a.events.json": by("ctest", "probes", "gpu") | AUDIO_SECTIONS,
    "lib/behaviour/KNIGHT.graph.json": by("ctest", "probes", "gpu") | AUDIO_SECTIONS,
    # the probes read no audio: the runner's decode never makes them re-run
    "lib/audio/audio.manifest": by("ctest", "gpu", "audio"),
    "lib/audio/s5/valus_hurt.wav": by("ctest", "gpu"),
    "lib/textures/vfx/common/beam/_ps4/vertical_solid_beams_01_pfx.png": by("ctest", "probes", "gpu") | AUDIO_SECTIONS,
    "tools/verify/common.sh": {k for k in ALL if "." in k},
    "tools/verify/s3.sh": {"s3.gpu"},
    "scripts/s4-acceptance.sh": {"s4.probes", "s4.gpu"},
    "tools/verify/golden/s5/gpu/run.txt": {"s5.gpu"},
    "tools/verify/golden/s8/probes/compose.txt": {"s8.probes"},
}
UNCHANGED = ["out/re/stage/zz_test.manifest",   # no section reads it
             "app/build/CMakeCache.txt", "src/BpConvert.Cli/bin/x.dll", "lib/textures/t.png", "lib/textures/models/m.png",
             "lib/audio/clip.txt", "docs/notes/x.md"]


def write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def all_stamps(root, lib):
    s = stamps.Stamper(root, lib)
    out = {k: s.step(k) for k in STEPS}
    out.update({"%s.%s" % (m, sec): s.section(m, sec) for m, secs in MILESTONES.items() for sec in secs})
    return out


def adapter_manifests():
    """{milestone.section: names} read off the adapters: the out/re/stage/<name>.manifest variables
    a section's body uses (s9's sections call preflight(), which requires four of them)."""
    import re
    import run
    out = {}
    for m in MILESTONES:
        text = run.read(os.path.join(run.HERE, m + ".sh"))
        names = {v: n for v, n in re.findall(r'^(\w+)="\$repo_root/out/re/stage/([\w-]+)\.manifest"', text, re.M)}
        pre = re.search(r"^preflight\(\) \{\n.*?(?=^section_)", text, re.M | re.S)
        for section, body in run.sections_of(m).items():
            if re.search(r"^preflight$", body, re.M):
                body += pre.group(0)
            out["%s.%s" % (m, section)] = {n for v, n in names.items() if re.search(r"\$\{?%s\b" % v, body)}
    return out


def main():
    declared = {"%s.%s" % (m, s): set(n.split()) for m, ss in stamps.MANIFESTS.items() for s, n in ss.items()}
    assert declared == adapter_manifests(), "stamps.MANIFESTS drifted from the adapters: %s vs %s" % (
        sorted(declared.items()), sorted(adapter_manifests().items()))
    with tempfile.TemporaryDirectory() as root:
        lib = os.path.join(root, "lib")
        for rel in list(CASES) + UNCHANGED:
            write(root, rel, "v1 " + rel)
        git = ["git", "-C", root]
        subprocess.run(git + ["init", "-q"], check=True)
        subprocess.run(git + ["update-index", "--add", "--cacheinfo",
                              "160000,%s,third_party/PSO2-Aqua-Library" % ("1" * 40)], check=True)
        base = all_stamps(root, lib)
        assert len(set(base.values())) == len(base), "two steps share a stamp"

        def changed():
            now = all_stamps(root, lib)
            return {k for k in now if now[k] != base[k]}

        for rel, want in CASES.items():
            write(root, rel, "v2 " + rel)
            got = changed()
            assert got == want, "%s: changed %s, expected %s" % (rel, sorted(got), sorted(want))
            write(root, rel, "v1 " + rel)
            if not rel.endswith((".glb", ".png")):   # size+mtime fingerprints: v1 again has a new mtime
                assert changed() == set(), rel
            base = all_stamps(root, lib)

        for rel in UNCHANGED:
            write(root, rel, "v2 changed " + rel)
            assert changed() == set(), "%s must not be an input" % rel

        # a re-decode rewrites the WAVs with the same bytes (new mtimes): content, not mtime
        wav = os.path.join(lib, "audio/s5/valus_hurt.wav")
        st = os.stat(wav)
        os.utime(wav, ns=(st.st_atime_ns, st.st_mtime_ns + 10**9))
        write(root, "lib/audio/s5/valus_hurt.wav", "v1 lib/audio/s5/valus_hurt.wav")
        assert changed() == set(), "a WAV rewritten with the same bytes is the same WAV"

        # compose-encounter writes the composing worktree's path into `# source:`; only the path
        # prefix is normalised: the source's sha (and the rest of the line) still counts
        src = "# source: %s/out/re/stage/valus.manifest (sha256 %s, 30 instances)\ninstance a\n"
        write(root, "out/re/stage/encounter.manifest", src % ("/wt-a", "1" * 64))
        base = all_stamps(root, lib)
        write(root, "out/re/stage/encounter.manifest", src % ("/Users/x/wt-b", "1" * 64))
        assert changed() == set(), "the `# source:` worktree prefix is not an input"
        write(root, "out/re/stage/encounter.manifest", src % ("/Users/x/wt-b", "2" * 64))
        assert changed() == MANIFEST_READERS["encounter"], "the source's sha is an input"
        base = all_stamps(root, lib)

        # a manifest a section reads that goes missing changes its stamp (the adapter then fails):
        # never a stale PASS
        for name, want in MANIFEST_READERS.items():
            rel = "out/re/stage/%s.manifest" % name
            os.rename(os.path.join(root, rel), os.path.join(root, rel + ".gone"))
            assert changed() == want, "deleting %s: changed %s" % (rel, sorted(changed()))
            os.rename(os.path.join(root, rel + ".gone"), os.path.join(root, rel))
            assert changed() == set(), rel

        subprocess.run(git + ["update-index", "--cacheinfo",
                              "160000,%s,third_party/PSO2-Aqua-Library" % ("2" * 40)], check=True)
        assert changed() == by("dotnet"), "the submodule SHA is a dotnet input"
        subprocess.run(git + ["update-index", "--cacheinfo",
                              "160000,%s,third_party/PSO2-Aqua-Library" % ("1" * 40)], check=True)

        os.symlink(lib, os.path.join(root, "lib-view"))
        moved = {k for k, v in all_stamps(root, os.path.join(root, "lib-view")).items() if v != base[k]}
        assert moved == set(), "a symlink to the same library is the same library (realpath)"
        os.rename(lib, os.path.join(root, "lib2"))
        moved = {k for k, v in all_stamps(root, os.path.join(root, "lib2")).items() if v != base[k]}
        assert moved == by("ctest", "audio", "probes", "gpu"), "the library's realpath: %s" % sorted(moved)
    print("test_stamps: OK (%d input files, %d steps)" % (len(CASES), len(ALL)))


if __name__ == "__main__":
    main()
