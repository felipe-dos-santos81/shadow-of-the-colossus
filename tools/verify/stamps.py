"""Content-hash stamps for the regression runner (design D3, §13.5).

A step's stamp is the SHA-256 of its sorted input hashes; the step re-runs only when the stamp
differs from the one stored after its last PASS. A missed input makes a stale pass — the long run
(`scripts/verify --long`) is the backstop.
"""
import glob
import hashlib
import os
import subprocess

GITLINK = "@gitlink"    # the PSO2-Aqua-Library submodule SHA recorded in the index
LIB = "@lib"            # the library: its realpath, glbs, *.clips.json and audio/audio.manifest
LIBPATH = "@libpath"    # the library's realpath only (where the audio decode writes)
LIBAUDIO = "@libaudio"  # the library's audio/audio.manifest content (8/10/34 entries: who decoded last)

# The input classes (D3 + ruling §13.5). A section of an adapter hashes its class, the adapter, its
# frozen script, its goldens and the runner's own code (VERIFY).
CLASSES = {
    "dotnet": ["src/**", "tests/**", "*.sln", "Directory.Build.*", "tools/re/audio/*.json",
               "tools/native/**", GITLINK],
    "ctest": ["app/**", "tools/re/audio/*.json", LIB],
    "audio": ["tools/re/audio/**", "src/**", LIBPATH, LIBAUDIO],
    "probes": ["tools/re/stage/**", "tools/re/anb/**", "tools/re/s6/**",
               "tools/re/s7/shrine_placements.json", "tools/re/s8/placements.json", "tools/re/s9/**",
               "out/re/stage/*.manifest", "out/re/stage/arena_paths.txt", LIB],
    "gpu": ["app/**", "out/re/stage/*.manifest", "tools/re/s7/**", "tools/re/s8/**", LIB],
}
VERIFY = ["tools/verify/common.sh", "tools/verify/masks.py", "tools/verify/run.py",
          "tools/verify/stamps.py"]
SKIP_DIRS = {"build", "bin", "obj", "__pycache__", ".git"}


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _file_sha(path):
    with open(path, "rb") as f:
        return _sha(f.read())


class Stamper:
    """Expands and hashes input patterns once; make a new one after anything rewrites inputs."""

    def __init__(self, root, lib):
        self.root = root
        self.lib = lib
        self._memo = {}

    def _expand(self, pattern):
        if pattern not in self._memo:
            self._memo[pattern] = list(self._rows(pattern))
        return self._memo[pattern]

    def _rows(self, pattern):
        root = self.root
        if pattern == GITLINK:
            try:
                out = subprocess.run(["git", "-C", root, "ls-files", "-s", "third_party/PSO2-Aqua-Library"],
                                     capture_output=True, text=True).stdout
            except OSError:
                out = ""
            yield (GITLINK, _sha(out.encode()))
        elif pattern == LIBPATH:
            yield (LIBPATH, _sha(os.path.realpath(self.lib).encode()))
        elif pattern == LIBAUDIO:
            path = os.path.join(self.lib, "audio", "audio.manifest")
            yield (LIBAUDIO, _file_sha(path) if os.path.isfile(path) else "absent")
        elif pattern == LIB:
            yield (LIBPATH, _sha(os.path.realpath(self.lib).encode()))
            yield (LIB, self._lib_fingerprint())
        elif pattern.endswith("/**"):
            base = os.path.join(root, pattern[:-3])
            for dirpath, dirs, files in os.walk(base):
                dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
                for name in sorted(files):
                    if name != ".DS_Store":
                        path = os.path.join(dirpath, name)
                        yield (os.path.relpath(path, root), _file_sha(path))
        else:
            for path in sorted(glob.glob(os.path.join(root, pattern))):
                if os.path.isfile(path):
                    yield (os.path.relpath(path, root), _file_sha(path))

    def _lib_fingerprint(self):
        real = os.path.realpath(self.lib)
        h = hashlib.sha256()
        for dirpath, dirs, files in os.walk(real):
            # ponytail: the textures (8.9 GB) are skipped and the glbs and decoded WAVs are
            # fingerprinted by size + mtime, not content (3 GB); a bake rewrites both the glb and its
            # textures, so the glb's mtime moves, and a re-decode rewrites the WAVs even when the
            # manifest is byte-identical. Hash content if a texture-only change ever needs catching.
            dirs[:] = sorted(d for d in dirs if not (dirpath == real and d == "textures"))
            for name in sorted(files):
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, real)
                if name.endswith(".glb") or (name.endswith(".wav") and rel.startswith("audio" + os.sep)):
                    st = os.stat(path)
                    h.update(("%s %d %d\n" % (rel, st.st_size, st.st_mtime_ns)).encode())
                elif name.endswith(".clips.json") or rel == os.path.join("audio", "audio.manifest"):
                    h.update(("%s %s\n" % (rel, _file_sha(path))).encode())
        return h.hexdigest()

    def stamp(self, patterns, name):
        rows = sorted({row for pattern in patterns for row in self._expand(pattern)} | {("@step", name)})
        return _sha("".join("%s %s\n" % row for row in rows).encode())

    def step(self, step):
        """dotnet | ctest | audio (the runner's own decode of the current map into the library)."""
        return self.stamp(CLASSES[step], step)

    def section(self, milestone, section):
        """An adapter section: probes | audio | gpu."""
        own = ["tools/verify/%s.sh" % milestone, "scripts/%s-acceptance.sh" % milestone,
               "tools/verify/golden/%s/%s/**" % (milestone, section)]
        if section == "gpu":   # s5/s6/s8 gpu read their own map's decode (manifest names, cues)
            own.append("tools/re/audio/%s_map.json" % milestone)
        return self.stamp(CLASSES[section] + VERIFY + own, "%s.%s" % (milestone, section))
