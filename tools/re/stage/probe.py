#!/usr/bin/env python3
"""RE probe for SOTC's stage-composition family — the `.fs` files + the inventory.

The family (measured 2026-10-18, docs/notes/re-stage.md §1):
  cp3shadow/data/fs/stage/*.fs              1,896 stage segment lists
  cp3shadow/data/fs/stagetexseg_def/*.fs      112 texture-segment defs
  cp3shadow/data/fs/rootseg.fs                  1 root segment list
  workspaces/_ps4/shadow.cwks                   1 workspace dependencies file (§2)
  cp3shadow/path/**/*.path                    144 path definitions
  cp3shadow/camdata/*.csv                      20 camera data tables

The `.fs` layout (measured byte-exact on all 1,896 stage files; the two other
`.fs` families share it):
  0x00  u32 n                      entry count
  0x04  n × {u32 v, u32 hash32}    8-byte entries at 0x04+8i
  size == 4 + 8·n exactly (no trailing bytes, n >= 2 in the stage family)
`v` is a small integer (measured range [1, 257], 202 distinct across the stage
family); the hash32 namespace is UNRESOLVED — the conventions tested (no
systematic match) are listed in docs/notes/re-stage.md §1.3, never a guessed
mapping here.

The `.cwks` layout (measured on the one file; docs/notes/re-stage.md §2):
  0x00  "PSKW" + u32 6             fourcc + version (the eboot names the file a
                                   "workspace dependencies file" — §2.1)
  0x08  u32 130234, u32 67045      header pair; the second value is the 16-B
                                   record count (verified: 0x10 + count·16 ==
                                   the string pool start)
  0x10  count × 16 B records       four u32 fields; field[0] non-decreasing
                                   over all records; fields [1] and [3] point
                                   into the string pool — semantics OPEN
                                   (§2.4), never guessed here
  pool  strings as [u8 len]$path\0 (len == the whole entry length), plus bare
        $path\0 entries; the arena path list is extracted from the pool (§2.3)

The arena manifest (W2.3, docs/notes/re-stage.md §3): the converted library's
two level sets `models/shadow/levels/arena_01/` + `arena_01_entrance/` hold
13 + 22 glbs; the non-*_collision* ones (12 + 18 = 30) are the manifest's
instances — identity transforms, the glbs are already world-positioned — and the
spawn is a labeled builder pick.  The format is W1's committed schema
(docs/notes/scene-manifest.md): line-oriented, `#` comments, exact token counts,
strict full-string finite floats.

Modes
  fs <file> [--json <out>]   structure dump of a .fs: header, entries, v histogram
  classify                   one line per family: counts, size ranges, name ranges
  check-fs                   pinned assertions for the stage .fs family (exit 0/1)
  cwks <file> [--json <out>] [--arena-out <out>] [--match SUBSTR] [--stage NAME]
                             structure dump of the workspace file: header,
                             records, string pool; extracts the pool path list
                             matching SUBSTR (default: the stage's match)
  check-cwks                 pinned assertions for the workspace file (exit 0/1)
  compose --library <dir> --out <file> [--paths <file>] [--stage arena_01|shrine]
                             emit the stage's scene manifest (the W1↔W2 format):
                             arena_01: the level sets' non-*_collision* glbs,
                             identity transforms, the labeled spawn pick;
                             shrine: the cathedral set + statues + posed Mono
                             + spawn (tools/re/s7/shrine_placements.json)
  check-manifest [<file>] --library <dir> [--stage arena_01|shrine]
                             pinned assertions for the stage's manifest (exit 0/1);
                             no --stage means arena_01
  compose-encounter --library <dir> [--s3-manifest <file>] [--out <file>]
                             derive the S4 encounter manifest from the S3 one:
                             the 30 instances verbatim, a rewritten spawn, one
                             `entity` (Valus), a new provenance header
  check-encounter [<file>] --library <dir>
                             pinned assertions for the encounter manifest (exit 0/1)

Inputs are read-only.  Files are resolved against the game tree ($SOTC_GAME_ROOT,
default below); a bare `.fs` name also resolves inside the family's directories,
so `fs 1010.fs` works from anywhere.  `cwks` with no file argument defaults to
the one measured workspace file.
"""

import argparse
import glob
import hashlib
import json
import math
import os
import re
import struct
import sys

DEFAULT_GAME_ROOT = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"

# Family globs, against the game tree (measured 2026-10-18).
FS_STAGE_GLOB = "cp3shadow/data/fs/stage/*.fs"
FS_TEXSEG_GLOB = "cp3shadow/data/fs/stagetexseg_def/*.fs"
FS_ROOTSEG = "cp3shadow/data/fs/rootseg.fs"
CWKS_GLOB = "workspaces/**/*.cwks"
PATH_GLOB = "cp3shadow/path/**/*.path"
CAMDATA_GLOB = "cp3shadow/camdata/*.csv"

# The .fs directories a bare name may resolve in (stage first).
FS_DIRS = ("cp3shadow/data/fs/stage", "cp3shadow/data/fs/stagetexseg_def", "cp3shadow/data/fs")

# Pinned samples and family facts (check-fs; all re-measured 2026-10-18).
FS_1010 = "cp3shadow/data/fs/stage/1010.fs"
FS_874 = "cp3shadow/data/fs/stage/874.fs"
FS_1010_N = 50
FS_1010_FIRST = ((0x95, 0x4FBE65EE), (0x94, 0x03BF0117), (0x93, 0x2E3D3989))
FS_874_N = 2
FS_FILE_COUNT = 1896
FS_SIZE_RANGE = (20, 6340)
FS_ENTRY_TOTAL = 83433
FS_V_RANGE = (1, 257)
FS_V_DISTINCT = 202
FS_HASH_DISTINCT = 17686

# Pinned facts for the workspace dependencies file (check-cwks; all measured
# 2026-10-18 with this probe, docs/notes/re-stage.md §2).
CWKS_FILE = "workspaces/_ps4/shadow.cwks"
CWKS_MAGIC = b"PSKW"
CWKS_VERSION = 6
CWKS_SIZE = 29398788
CWKS_HEADER_PAIR = (130234, 67045)  # u32 pair at 0x08; the second is the record count
CWKS_RECORD_START = 0x10
CWKS_RECORD_COUNT = 67045
CWKS_RECORD_SIZE = 16
CWKS_POOL_START = 0x105E60
CWKS_ENTRIES = 407965  # the pool's string entries (tagged + bare)
CWKS_TAGGED = 340920
CWKS_BARE = 67045
CWKS_TAIL_MATCH = "cp3shadow/data/fs/stage"
CWKS_TAIL_FIRST = 0x1C07319
CWKS_TAIL_PATHS = (
    "/cp3shadow/data/fs/stage/132391.fs",
    "/cp3shadow/data/fs/stage/136811.fs",
    "/cp3shadow/data/fs/stage/136867.fs",
    "/cp3shadow/data/fs/stage/144144.fs",
    "/cp3shadow/data/fs/stage/145969.fs",
    "/cp3shadow/data/fs/stage/316283.fs",
    "/cp3shadow/data/fs/stage/317197.fs",
    "/cp3shadow/data/fs/stage/346875.fs",
    "/cp3shadow/data/fs/stage/350578.fs",
    "/cp3shadow/data/fs/stage/908.fs",
    "/cp3shadow/data/fs/stagetexseg_def/657159.fs",
    "/cp3shadow/data/fs/stagetexseg_def/659959.fs",
    "/cp3shadow/data/fs/stagetexseg_def/660533.fs",
)

# The per-stage composition configs (W2.3 for arena_01, S7 for shrine; the arena
# values were re-measured 2026-10-18, docs/notes/re-stage.md §3).  `compose`,
# `check-manifest` and `cwks` take `--stage`; none means arena_01.
#   cwks_match   the pool path substring that lists the stage's workspace entries
#   count        that list's entry count; cwks_sha256 its canonical-text sha256
#   sets         the library directories holding the stage's glbs
#   instances    the manifest's instance count (None until pinned on a measured compose)
#   spawn        the manifest's spawn (x y z yaw), or None when the stage has none yet
#   bounds_evidence  (key, level glb, evidence) per level glb (arena_01 only)
# arena_01: the two level sets hold 13 + 22 glbs; the non-*_collision* ones (12 + 18 = 30)
# are the instances (identity transforms — the glbs are already world-positioned); the
# collision glbs are collision geometry, not render dependencies (the workspace pool
# references their `.cclm` definitions, and the non-collision cmsh exactly — §3).
# The spawn is a labeled builder pick (§3): the entrance `_ground` mesh's footprint centre
# (309.3118, 285.4028) at its surface height (4.3940 m, ray-cast), facing +Z — the app's
# yaw 0 is +Z, toward the arena bowl; the sets meet near z≈300.  NOT data-derived (the
# `.path`/camdata candidates are in §3).  The bounds evidence is the plan's rounded numbers
# (check-manifest re-derives them from the accessor min/max and matches at this precision —
# x/z to integers, the entrance's y to 1 dp).
# shrine: one set, in cwks order (the first appearance of each cmsh in the pool list), minus
# the exclusions below (design D3), then the 17 statue instances, the posed Mono and the
# spawn, all from tools/re/s7/shrine_placements.json (Task 0; `source` is data|binary|authored).
# Pinned on the measured compose (Task 7): `instances` is the set's glb count (the
# cathedral instance lines), `meshes` the (glTF primitives, vertices) totals over every
# instance/posed line's library glb, `sha256` the manifest file's.
STAGES = {
    "arena_01": {
        "cwks_match": "levels/arena_01/",
        "count": 142,
        "cwks_sha256": "13e3bb60daf3dc91bbf36818b3bc6dd50dc59c27e9730f341d2c4b24d77e5706",
        "sets": (
            "models/shadow/levels/arena_01",
            "models/shadow/levels/arena_01_entrance",
        ),
        "order": "sorted",            # instances sorted per set
        "set_files": (13, 22),        # glb files per set, before the collision filter
        "set_collision": (1, 4),      # *_collision* glb files per set
        "instances": 30,              # 12 + 18 non-*_collision* glbs (35 - 5)
        "spawn": (309.312, 4.394, 285.403, 0.0),
        "bounds_evidence": (
            ("arena_01", "models/shadow/levels/arena_01/_cmn/arena_01.glb",
             ("x[237,431]", "y[28,70]", "z[298,390]")),
            ("entrance", "models/shadow/levels/arena_01_entrance/_cmn/arena_01_entrance.glb",
             ("x[259,345]", "y[-1.7,81.5]", "z[249,307]")),
        ),
    },
    "shrine": {
        "cwks_match": "levels/cathedral/",
        "count": 589,
        "cwks_sha256": "c52e90a680e958916fb5e8fe6d358b52afc345c838cd7d56ddfd14010102199d",
        "sets": ("models/shadow/levels/cathedral",),
        "order": "cwks",              # instances in the pool list's order
        "instances": 34,              # 118 glb files, 84 excluded (measured 2026-10-22)
        "meshes": (3551, 2229907),    # (primitives, vertices) over the 34 + 17 + 1 glbs
        "sha256": "6bacc017ab2349786b5fcb126dd2a931ccc863ec7fea0f5b70f4e6d3d9bc97d8",
        "spawn": (0.0, 8.998, 1.0, 0.0),   # == the JSON's spawn (compose and check assert it)
        "placements": os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "s7",
                                   "shrine_placements.json"),
        "sources": ("data", "binary", "authored"),
        # Dropped from the library's glbs (matched on the file stem, `_collision` anywhere in it):
        "exclude_stub": re.compile(r"_[0-9a-f]{8}\."),   # hash-suffix reference stubs (72-byte glbs)
        "exclude_lod": re.compile(r"_lod[0-9]"),
        "exclude_names": (
            "occluder1", "occluderint1", "occluderintlow", "occluderstairs1", "cathedral_sg_occluder",
            "cathint_shadow", "cathedral_sg_shadow", "shadowcaster1", "cath_int_shadowblocker_pool",
            "emitter_arch_lightray_long",
        ),
        # plus any `<name>_low` glb whose full-detail `<name>` sibling exists (stage_sets)
    },
}
ARENA = STAGES["arena_01"]
COLLISION_MARK = "_collision"
MANIFEST_OUT = "out/re/stage/valus.manifest"
ARENA_PATHS_OUT = "out/re/stage/arena_paths.txt"

# The S4 encounter (Task 6): the S3 arena's 30 instances carried verbatim, a
# rewritten player spawn, and one placed animated entity.  The placement values
# are the labeled builder-pick fallback measured in docs/notes/re4-placement.md
# §8 (the pull's verdict was negative: no shipped record satisfies the bar).
# The composer re-samples both ground heights from the converted ground mesh, so
# the manifest carries measured metres — not hand-copied ones.
ENCOUNTER_OUT = "out/re/stage/encounter.manifest"
ENCOUNTER_ENTITY_GLB = (
    "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
)
ENCOUNTER_ENTITY_CLIP = "minotaur_a_stand"
ENCOUNTER_GROUND_GLB = "models/shadow/levels/arena_01/_cmn/arena_01_ground.glb"
ENCOUNTER_ENTITY_XZ = (382.0, 370.0)   # builder pick on the bowl floor
ENCOUNTER_SPAWN_XZ = (353.0, 345.0)    # misc/playerstartpositions.txt:1 (353 29 345)
ENCOUNTER_PLACEMENT = "builder-pick"
ENCOUNTER_SPAWN_PROVENANCE = "data-position+builder-yaw"
ENCOUNTER_ENTITY_GROUND_Y = 28.369     # the re4 note's rounded ground sample
ENCOUNTER_SPAWN_GROUND_Y = 27.951
ENCOUNTER_BOWL_XZ = ((237.37488, 431.3924), (297.56433, 390.03604))  # arena_01.glb


def die(msg, code=2):
    sys.stderr.write("probe: %s\n" % msg)
    sys.exit(code)


# --------------------------------------------------------------------------- io


def game_root():
    root = os.environ.get("SOTC_GAME_ROOT") or DEFAULT_GAME_ROOT
    if not os.path.isdir(root):
        die(
            "game tree not found: %s (set SOTC_GAME_ROOT to the extracted game dir)" % root
        )
    return root


def resolve_fs(arg):
    """A `.fs` argument: absolute paths as-is; else against the game tree; a bare
    name (no separator) also against the family's three directories."""
    if os.path.isabs(arg):
        if os.path.isfile(arg):
            return arg
        die("file not found: %s" % arg)
    root = game_root()
    candidates = [os.path.join(root, arg)]
    if "/" not in arg:
        candidates += [os.path.join(root, d, arg) for d in FS_DIRS]
    for cand in candidates:
        if os.path.isfile(cand):
            return cand
    die("file not found: %s (tried the game tree and %s)" % (arg, ", ".join(FS_DIRS)))


def resolve_cwks(arg):
    """A `.cwks` argument: absolute paths as-is; else against the game tree; a
    bare name also against workspaces/_ps4; None → the one measured file."""
    if arg is not None and os.path.isabs(arg):
        if os.path.isfile(arg):
            return arg
        die("file not found: %s" % arg)
    root = game_root()
    arg = arg if arg is not None else CWKS_FILE
    candidates = [os.path.join(root, arg)]
    if "/" not in arg:
        candidates.append(os.path.join(root, "workspaces/_ps4", arg))
    for cand in candidates:
        if os.path.isfile(cand):
            return cand
    die("file not found: %s (tried the game tree and workspaces/_ps4)" % arg)


def family_files(pattern, root):
    return sorted(glob.glob(os.path.join(root, pattern), recursive=True))


def read_bytes(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        die("cannot read %s: %s" % (path, exc.strerror or exc))


# ------------------------------------------------------------------- primitives


def u32le(data, off):
    return struct.unpack_from("<I", data, off)[0]


class FsError(Exception):
    """The file does not fit the measured `.fs` layout."""


def fs_entries_from(data):
    """Parse the `.fs` layout from bytes; raise FsError with the measured numbers."""
    size = len(data)
    if size < 4:
        raise FsError("shorter than the 4-byte header (%d bytes)" % size)
    n = u32le(data, 0)
    if size != 4 + 8 * n:
        raise FsError("size %d != 4 + 8*%d (= %d)" % (size, n, 4 + 8 * n))
    return size, n, [(u32le(data, 4 + 8 * i), u32le(data, 8 + 8 * i)) for i in range(n)]


def fs_entries(path):
    """Parse the `.fs` layout; raise FsError with the measured numbers on mismatch."""
    return fs_entries_from(read_bytes(path))


def v_histogram(entries):
    hist = {}
    for v, _ in entries:
        hist[v] = hist.get(v, 0) + 1
    return hist


def fmt_entries(entries):
    return ", ".join("(0x%02X, 0x%08X)" % (v, h) for v, h in entries)


# ------------------------------------------------------------------ fs command


def cmd_fs(args):
    path = resolve_fs(args.file)
    try:
        size, n, entries = fs_entries(path)
    except FsError as exc:
        die("%s does not fit the .fs layout: %s" % (path, exc))

    hist = v_histogram(entries)
    distinct_hashes = {h for _, h in entries}

    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(
                {
                    "file": path,
                    "size": size,
                    "n": n,
                    "entries": [
                        {
                            "index": i,
                            "offset": "0x%04X" % (4 + 8 * i),
                            "v": v,
                            "hash32": "0x%08X" % h,
                        }
                        for i, (v, h) in enumerate(entries)
                    ],
                    "v_histogram": [{"v": v, "count": hist[v]} for v in sorted(hist)],
                    "distinct_v": len(hist),
                    "distinct_hash32": len(distinct_hashes),
                },
                fh,
                indent=2,
            )
            fh.write("\n")
        sys.stderr.write("probe: wrote %s\n" % out)

    out = sys.stdout
    out.write("fs %s (%d bytes)\n" % (path, size))
    out.write("header\n")
    out.write("  0x00 u32 n              %d\n" % n)
    out.write(
        "  0x04 entries            %d × {u32 v, u32 hash32} at 0x04+8i (%d B)\n"
        % (n, 8 * n)
    )
    out.write("  0x%02X (end)             size == 4 + 8·n  (%d == 4 + 8·%d)\n" % (size, size, n))
    out.write("entries\n")
    for i, (v, h) in enumerate(entries):
        out.write("  [%3d] 0x%04X  v=0x%02X (%3d)  hash=0x%08X\n" % (i, 4 + 8 * i, v, v, h))
    out.write("v histogram (%d distinct)\n" % len(hist))
    for v in sorted(hist):
        out.write("  0x%02X (%3d): %d\n" % (v, v, hist[v]))
    out.write(
        "summary: %d entries; %d distinct v; %d distinct hash32; size == 4 + 8·n\n"
        % (n, len(hist), len(distinct_hashes))
    )
    return 0


# ------------------------------------------------------------ classify command


FAMILIES = [
    (FS_STAGE_GLOB, True),
    (FS_TEXSEG_GLOB, True),
    (FS_ROOTSEG, True),
    (CWKS_GLOB, False),
    (PATH_GLOB, False),
    (CAMDATA_GLOB, False),
]


def family_stats(pattern, root, structure):
    files = family_files(pattern, root)
    if not files:
        die("family not found: %s (under %s)" % (pattern, root))
    sizes = [os.path.getsize(p) for p in files]
    stats = {
        "pattern": pattern,
        "count": len(files),
        "size_min": min(sizes),
        "size_max": max(sizes),
        "size_total": sum(sizes),
        "structure_ok": None,
        "structure_bad": None,
        "first_name": None,
        "last_name": None,
    }
    if structure:
        ok = 0
        for path in files:
            try:
                fs_entries(path)
                ok += 1
            except FsError as exc:
                if stats["structure_bad"] is None:
                    stats["structure_bad"] = "%s (%s)" % (os.path.basename(path), exc)
        stats["structure_ok"] = ok
        stats["first_name"] = os.path.basename(files[0])
        stats["last_name"] = os.path.basename(files[-1])
    return stats


def cmd_classify(args):
    root = game_root()
    print("classify: SOTC stage-composition families under %s" % root)
    total = 0
    for pattern, structure in FAMILIES:
        stats = family_stats(pattern, root, structure)
        total += stats["count"]
        size_text = (
            "%d (%d B)" % (stats["size_min"], stats["size_total"])
            if stats["size_min"] == stats["size_max"]
            else "%d..%d (%d B)" % (stats["size_min"], stats["size_max"], stats["size_total"])
        )
        line = "  %-38s %5d %-5s size %s" % (
            pattern,
            stats["count"],
            "file" if stats["count"] == 1 else "files",
            size_text,
        )
        if structure:
            line += "  4+8n %d/%d" % (stats["structure_ok"], stats["count"])
            if stats["structure_bad"]:
                line += "  bad: %s" % stats["structure_bad"]
            if stats["count"] > 1:
                line += "  names %s..%s" % (stats["first_name"], stats["last_name"])
        print(line)
    print("  total: %d files in %d families" % (total, len(FAMILIES)))
    return 0


# ------------------------------------------------------------- check-fs command


class Check:
    def __init__(self, name, kind, expected, measured, ok):
        self.name = name
        self.kind = kind
        self.expected = expected
        self.measured = measured
        self.ok = ok


def cmd_check_fs(args):
    root = game_root()
    files = family_files(FS_STAGE_GLOB, root)
    checks = []

    def add(name, kind, expected, measured, ok):
        checks.append(Check(name, kind, expected, measured, ok))

    def sample(rel):
        """(size, n, entries, error) for a pinned file — never dies: a missing
        or unreadable file renders as a structured FAIL (exit 1), not usage."""
        path = os.path.join(root, rel)
        try:
            with open(path, "rb") as fh:
                data = fh.read()
        except OSError as exc:
            return None, None, None, "unreadable: %s" % (exc.strerror or exc)
        try:
            size, n, entries = fs_entries_from(data)
            return size, n, entries, None
        except FsError as exc:
            return None, None, None, str(exc)

    add(
        "stage .fs file count",
        "structure",
        FS_FILE_COUNT,
        len(files),
        len(files) == FS_FILE_COUNT,
    )

    bad = []  # (name, reason) for files that do not fit 4+8n
    sizes = []
    total_entries = 0
    vs = set()
    hashes = set()
    vmin = vmax = None
    for path in files:
        try:
            size, n, entries = fs_entries(path)
        except FsError as exc:
            bad.append((os.path.basename(path), str(exc)))
            continue
        sizes.append(size)
        total_entries += n
        for v, h in entries:
            vs.add(v)
            hashes.add(h)
            vmin = v if vmin is None else min(vmin, v)
            vmax = v if vmax is None else max(vmax, v)

    detail = "%d/%d" % (len(files) - len(bad), len(files))
    if bad:
        detail += "  bad: %s (%s)" % bad[0]
    add(
        "every .fs satisfies size == 4 + 8·n",
        "structure",
        "%d/%d" % (len(files), len(files)),
        detail,
        not bad,
    )

    _, n1010, e1010, err1010 = sample(FS_1010)
    add(
        "1010.fs n",
        "structure",
        FS_1010_N,
        n1010 if err1010 is None else "unparsed: %s" % err1010,
        n1010 == FS_1010_N,
    )
    first3 = tuple(e1010[:3]) if e1010 is not None else None
    add(
        "1010.fs first three entries",
        "structure",
        fmt_entries(FS_1010_FIRST),
        fmt_entries(first3) if first3 is not None else "unparsed: %s" % err1010,
        first3 == FS_1010_FIRST,
    )
    _, n874, _, err874 = sample(FS_874)
    add(
        "874.fs n",
        "structure",
        FS_874_N,
        n874 if err874 is None else "unparsed: %s" % err874,
        n874 == FS_874_N,
    )

    size_range = (min(sizes), max(sizes)) if sizes else None
    v_range = (vmin, vmax) if vs else None
    add(
        "size range",
        "measured",
        "[%d, %d]" % FS_SIZE_RANGE,
        "[%d, %d]" % size_range if size_range else "none",
        size_range == FS_SIZE_RANGE,
    )
    add("total entries", "measured", FS_ENTRY_TOTAL, total_entries,
        total_entries == FS_ENTRY_TOTAL)
    add(
        "v range",
        "measured",
        "[%d, %d]" % FS_V_RANGE,
        "[%d, %d]" % v_range if v_range else "none",
        v_range == FS_V_RANGE,
    )
    add("distinct v", "measured", FS_V_DISTINCT, len(vs), len(vs) == FS_V_DISTINCT)
    add("distinct hash32", "measured", FS_HASH_DISTINCT, len(hashes),
        len(hashes) == FS_HASH_DISTINCT)

    print("check-fs: %d stage .fs files under %s" % (len(files), FS_STAGE_GLOB))
    for check in checks:
        status = "PASS" if check.ok else "FAIL"
        print(
            "%-5s %-9s %-38s expected %s, measured %s"
            % (status, check.kind, check.name, check.expected, check.measured)
        )
    failed = [c for c in checks if not c.ok]
    print(
        "summary: %d/%d gating assertions pass"
        % (len(checks) - len(failed), len(checks))
    )
    if failed:
        print(
            "gate: NOT MET — %d assertion(s) fail; re-measure and record "
            "(docs/notes/re-stage.md §1)" % len(failed)
        )
        return 1
    print("gate: MET")
    return 0


# ------------------------------------------------------------------ cwks mode


class CwksError(Exception):
    """The file does not fit the measured PSKW layout."""


def cwks_parse(path):
    """Parse the workspace dependencies file; raise CwksError with the measured
    numbers on mismatch.  Returns a dict:
      size, version, header_pair, count, pool_start, records, entries
    where records = list of 4-u32 tuples and entries = list of
    (offset, kind, path) with kind "tagged" ([u8 len]$path, len == entry
    length) or "bare" ($path).  All offsets are validated in-file."""
    data = read_bytes(path)
    size = len(data)
    if size < CWKS_RECORD_START:
        raise CwksError("shorter than the 16-byte header (%d bytes)" % size)
    if data[:4] != CWKS_MAGIC:
        raise CwksError("magic %r != PSKW" % data[:4])
    version = u32le(data, 4)
    if version != CWKS_VERSION:
        raise CwksError("version %d != %d" % (version, CWKS_VERSION))
    header_pair = (u32le(data, 0x08), u32le(data, 0x0C))
    count = header_pair[1]
    pool_start = CWKS_RECORD_START + CWKS_RECORD_SIZE * count
    if pool_start > size:
        raise CwksError(
            "record table (%d x %d B from 0x%X) runs past the file end 0x%X"
            % (count, CWKS_RECORD_SIZE, CWKS_RECORD_START, size)
        )
    records = [
        struct.unpack_from("<IIII", data, CWKS_RECORD_START + CWKS_RECORD_SIZE * i)
        for i in range(count)
    ]
    entries = []
    off = pool_start
    while off < size:
        end = data.find(b"\x00", off)
        if end == -1:
            raise CwksError("string entry at 0x%X has no NUL to EOF" % off)
        token = data[off:end]
        if not token:
            raise CwksError("empty string entry at 0x%X (consecutive NULs)" % off)
        first = token[0]
        if len(token) >= 2 and token[1] == 0x24:  # [u8 len]$path
            if first != len(token):
                raise CwksError(
                    "tagged entry at 0x%X: length byte %d != entry length %d"
                    % (off, first, len(token))
                )
            kind = "tagged"
            text = token[2:]
        elif first == 0x24:  # $path, no length byte
            kind = "bare"
            text = token[1:]
        else:
            raise CwksError(
                "string entry at 0x%X: neither [len]$path nor $path (%r)"
                % (off, token[:12])
            )
        if not text.startswith(b"/"):
            raise CwksError("string entry at 0x%X does not start with '/' after the marker" % off)
        entries.append((off, kind, text.decode("latin1")))
        off = end + 1
    return {
        "path": path,
        "size": size,
        "magic": data[:4],
        "version": version,
        "header_pair": header_pair,
        "count": count,
        "pool_start": pool_start,
        "records": records,
        "entries": entries,
    }


def arena_list(entries, match=ARENA["cwks_match"]):
    """The pool path list matching `match`, in pool order.  The default is the
    arena path list: every entry matching `levels/arena_01/` (measured 142
    entries; the literal `models/shadow/` prefix narrows it to 32 — see
    docs/notes/re-stage.md §2.3)."""
    return [path for _, _, path in entries if match in path]


def arena_text(paths):
    """The list's canonical serialization: one path per line, trailing newline."""
    return "".join(p + "\n" for p in paths).encode()


def fmt_records(records):
    return ", ".join("(0x%08X, 0x%08X, %d, 0x%08X)" % r for r in records)


def cmd_cwks(args):
    path = resolve_cwks(args.file)
    try:
        parsed = cwks_parse(path)
    except CwksError as exc:
        die("%s does not fit the .cwks layout: %s" % (path, exc))

    entries = parsed["entries"]
    records = parsed["records"]
    if not records:
        die("%s has 0 index records (nothing to dump)" % path)
    match = args.match or STAGES[args.stage]["cwks_match"]
    arena = arena_list(entries, match)
    arena_sha = hashlib.sha256(arena_text(arena)).hexdigest()
    tail = [p for _, _, p in entries if CWKS_TAIL_MATCH in p]
    decreases = sum(1 for i in range(len(records) - 1) if records[i][0] > records[i + 1][0])
    equals = sum(1 for i in range(len(records) - 1) if records[i][0] == records[i + 1][0])
    in_pool = sum(1 for r in records if parsed["pool_start"] <= r[1] < parsed["size"])
    in_pool2 = sum(1 for r in records if parsed["pool_start"] <= r[3] < parsed["size"])

    if args.arena_out:
        out = os.path.abspath(args.arena_out)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "wb") as fh:
            fh.write(arena_text(arena))
        sys.stderr.write("probe: wrote %s\n" % out)

    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        first = min(5, len(records))
        with open(out, "w") as fh:
            json.dump(
                {
                    "file": path,
                    "size": parsed["size"],
                    "version": parsed["version"],
                    "header_pair": list(parsed["header_pair"]),
                    "record_count": parsed["count"],
                    "record_start": "0x%X" % CWKS_RECORD_START,
                    "record_size": CWKS_RECORD_SIZE,
                    "records_sample": {
                        "first": [list(r) for r in records[:first]],
                        "last": [list(r) for r in records[-first:]],
                    },
                    "field0": {
                        "non_decreasing": decreases == 0,
                        "min": records[0][0],
                        "max": records[-1][0],
                        "equal_adjacent_pairs": equals,
                    },
                    "pool_start": "0x%X" % parsed["pool_start"],
                    "entry_count": len(entries),
                    "tagged": sum(1 for _, k, _ in entries if k == "tagged"),
                    "bare": sum(1 for _, k, _ in entries if k == "bare"),
                    "entries_sample": {
                        "first": [
                            {"offset": "0x%X" % o, "kind": k, "path": p}
                            for o, k, p in entries[:first]
                        ],
                        "last": [
                            {"offset": "0x%X" % o, "kind": k, "path": p}
                            for o, k, p in entries[-first:]
                        ],
                    },
                    "arena": {
                        "match": match,
                        "count": len(arena),
                        "distinct": len(set(arena)),
                        "sha256": arena_sha,
                        "paths": arena,
                    },
                    "tail": {"match": CWKS_TAIL_MATCH, "paths": tail},
                },
                fh,
                indent=2,
            )
            fh.write("\n")
        sys.stderr.write("probe: wrote %s\n" % out)

    out = sys.stdout
    out.write("cwks %s (%d bytes)\n" % (path, parsed["size"]))
    out.write("header\n")
    out.write("  0x00 PSKW               fourcc\n")
    out.write("  0x04 u32 %-14d version\n" % parsed["version"])
    out.write("  0x08 u32 %-14d (semantics open)\n" % parsed["header_pair"][0])
    out.write("  0x0C u32 %-14d index record count\n" % parsed["header_pair"][1])
    out.write(
        "  0x%02X index             %d x %d B records, ends at 0x%X\n"
        % (CWKS_RECORD_START, parsed["count"], CWKS_RECORD_SIZE, parsed["pool_start"])
    )
    out.write("records (four u32 fields; semantics OPEN, docs/notes/re-stage.md §2.4)\n")
    shown = list(range(min(5, len(records)))) + list(range(max(0, len(records) - 5), len(records)))
    for i in shown:
        out.write("  [%5d] %s\n" % (i, fmt_records([records[i]])))
    out.write(
        "  field[0] non-decreasing: %s (%d .. %d; %d equal adjacent pairs)\n"
        % ("yes" if decreases == 0 else "NO", records[0][0], records[-1][0], equals)
    )
    out.write(
        "  field[1] in the pool: %d/%d; field[3] in the pool: %d/%d\n"
        % (in_pool, len(records), in_pool2, len(records))
    )
    out.write("string pool\n")
    out.write(
        "  0x%X..0x%X (%d B)\n"
        % (parsed["pool_start"], parsed["size"], parsed["size"] - parsed["pool_start"])
    )
    tagged = sum(1 for _, k, _ in entries if k == "tagged")
    out.write(
        "  %d entries (%d tagged [len]$path, %d bare $path)\n"
        % (len(entries), tagged, len(entries) - tagged)
    )
    for i in list(range(min(5, len(entries)))) + list(range(max(0, len(entries) - 5), len(entries))):
        o, k, p = entries[i]
        out.write("  [%6d] 0x%X %-6s %s\n" % (i, o, k, p))
    out.write("arena path list (match %r)\n" % match)
    out.write("  %d entries (%d distinct); sha256 %s\n" % (len(arena), len(set(arena)), arena_sha))
    for p in arena[:5]:
        out.write("  %s\n" % p)
    if len(arena) > 5:
        out.write("  ... %d more\n" % (len(arena) - 5))
    out.write("tail stage paths (match %r)\n" % CWKS_TAIL_MATCH)
    for p in tail:
        out.write("  %s\n" % p)
    out.write(
        "summary: %d records; %d string entries; arena list %d entries sha256 %s; "
        "tail stage paths %d\n" % (parsed["count"], len(entries), len(arena), arena_sha, len(tail))
    )
    return 0


# ------------------------------------------------------------ check-cwks mode


def cmd_check_cwks(args):
    root = game_root()
    path = os.path.join(root, CWKS_FILE)
    checks = []

    def add(name, kind, expected, measured, ok):
        checks.append(Check(name, kind, expected, measured, ok))

    try:
        if not os.path.isfile(path):
            raise CwksError("missing from the tree")
        parsed = cwks_parse(path)
        error = None
    except CwksError as exc:
        parsed = None
        error = str(exc)

    def fail_all(reason):
        for name, kind, expected in CHECK_CWKS_SPEC:
            add(name, kind, expected, "unparsed: %s" % reason, False)

    if parsed is None:
        fail_all(error)
    else:
        records = parsed["records"]
        entries = parsed["entries"]
        arena = arena_list(entries)
        arena_sha = hashlib.sha256(arena_text(arena)).hexdigest()
        tail = [p for _, _, p in entries if CWKS_TAIL_MATCH in p]
        tail_first = next(
            (o for o, _, p in entries if CWKS_TAIL_MATCH in p), None
        )
        decreases = sum(1 for i in range(len(records) - 1) if records[i][0] > records[i + 1][0])

        add("magic PSKW", "structure", repr(CWKS_MAGIC), repr(parsed["magic"]),
            parsed["magic"] == CWKS_MAGIC)
        add("u32 6 @0x04 (version)", "structure", CWKS_VERSION, parsed["version"],
            parsed["version"] == CWKS_VERSION)
        add("file size", "structure", CWKS_SIZE, parsed["size"],
            parsed["size"] == CWKS_SIZE)
        add("header pair @0x08", "structure", "%d, %d" % CWKS_HEADER_PAIR,
            "%d, %d" % parsed["header_pair"], parsed["header_pair"] == CWKS_HEADER_PAIR)
        add("index record count", "structure", CWKS_RECORD_COUNT, parsed["count"],
            parsed["count"] == CWKS_RECORD_COUNT)
        add("records end at the pool start", "structure", "0x%X" % CWKS_POOL_START,
            "0x%X" % parsed["pool_start"], parsed["pool_start"] == CWKS_POOL_START)
        add("pool start", "structure", "0x%X" % CWKS_POOL_START,
            "0x%X" % parsed["pool_start"], parsed["pool_start"] == CWKS_POOL_START)
        add("string entries", "measured", CWKS_ENTRIES, len(entries),
            len(entries) == CWKS_ENTRIES)
        tagged = sum(1 for _, k, _ in entries if k == "tagged")
        add("tagged / bare split", "measured", "%d / %d" % (CWKS_TAGGED, CWKS_BARE),
            "%d / %d" % (tagged, len(entries) - tagged),
            tagged == CWKS_TAGGED and len(entries) - tagged == CWKS_BARE)
        add("record field[0] non-decreasing", "structure", "0 decreases",
            "%d decreases" % decreases, decreases == 0)
        add("arena list (match %r)" % ARENA["cwks_match"], "structure",
            ARENA["count"], len(arena), len(arena) == ARENA["count"])
        add("arena list sha256", "structure", ARENA["cwks_sha256"], arena_sha,
            arena_sha == ARENA["cwks_sha256"])
        add("tail stage paths (13 verbatim from 0x%X)" % CWKS_TAIL_FIRST, "structure",
            "%d paths" % len(CWKS_TAIL_PATHS),
            "%d paths%s" % (len(tail), "" if tail == list(CWKS_TAIL_PATHS) else ": %s" % tail[:1]),
            tail == list(CWKS_TAIL_PATHS) and tail_first == CWKS_TAIL_FIRST)

    print("check-cwks: %s" % path)
    for check in checks:
        status = "PASS" if check.ok else "FAIL"
        print(
            "%-5s %-9s %-42s expected %s, measured %s"
            % (status, check.kind, check.name, check.expected, check.measured)
        )
    failed = [c for c in checks if not c.ok]
    print("summary: %d/%d gating assertions pass" % (len(checks) - len(failed), len(checks)))
    if failed:
        print(
            "gate: NOT MET — %d assertion(s) fail; re-measure and record "
            "(docs/notes/re-stage.md §2)" % len(failed)
        )
        return 1
    print("gate: MET")
    return 0


# The check-cwks spec: (name, kind, expected) — used verbatim when the file is
# unparsable, so every pin renders as a FAIL with the parse error.
CHECK_CWKS_SPEC = [
    ("magic PSKW", "structure", "PSKW"),
    ("u32 6 @0x04 (version)", "structure", CWKS_VERSION),
    ("file size", "structure", CWKS_SIZE),
    ("header pair @0x08", "structure", "%d, %d" % CWKS_HEADER_PAIR),
    ("index record count", "structure", CWKS_RECORD_COUNT),
    ("records end at the pool start", "structure", "0x%X" % CWKS_POOL_START),
    ("pool start", "structure", "0x%X" % CWKS_POOL_START),
    ("string entries", "measured", CWKS_ENTRIES),
    ("tagged / bare split", "measured", "%d / %d" % (CWKS_TAGGED, CWKS_BARE)),
    ("record field[0] non-decreasing", "structure", "0 decreases"),
    ("arena list (match %r)" % ARENA["cwks_match"], "structure", ARENA["count"]),
    ("arena list sha256", "structure", ARENA["cwks_sha256"]),
    ("tail stage paths (13 verbatim from 0x%X)" % CWKS_TAIL_FIRST, "structure",
     "%d paths" % len(CWKS_TAIL_PATHS)),
]


# ------------------------------------------------------- manifest (W1↔W2) format
# W1's committed schema (docs/notes/scene-manifest.md): one directive per line;
# `#` starts a comment to end of line; blank lines are skipped; `instance
# <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>` (12 tokens),
# `entity <glbRelPath> <10 numbers> <clipName>` (13) and `spawn <px> <py> <pz>
# <yawDeg>` (5); every number a full-string finite float (the app's
# parse_manifest refuses `nan`, `inf`, `1.5x`, `1e999`).

MANIFEST_FLOAT_RE = re.compile(r"^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$")

MANIFEST_INSTANCE_FIELDS = ("px", "py", "pz", "qx", "qy", "qz", "qw", "sx", "sy", "sz")
MANIFEST_SPAWN_FIELDS = ("px", "py", "pz", "yawDeg")


class ManifestError(Exception):
    """The text does not fit the W1↔W2 manifest schema."""


def manifest_float(token, path, line, field):
    if not MANIFEST_FLOAT_RE.match(token):
        raise ManifestError("%s:%d: invalid number %r for %s" % (path, line, token, field))
    value = float(token)
    if not math.isfinite(value):
        raise ManifestError("%s:%d: %r is not finite for %s" % (path, line, token, field))
    return value


def parse_manifest_text(text, path, posed=None):
    """Parse the W1↔W2 schema; return (instances, spawns, entities) where
    instances = [(glbRelPath, [10 floats])], spawns = [[4 floats]] and entities
    = [(glbRelPath, [10 floats], clipName)].  A `posed` line (S7) is accepted
    only when the caller passes a `posed` list to fill with
    (glbRelPath, [10 floats], clipName, seconds); otherwise it is an unknown
    directive, as for the arena.  Raises ManifestError shaped
    `<path>:<line>: <reason>` (the app's message shape)."""
    instances = []
    spawns = []
    entities = []
    for number, line in enumerate(text.splitlines(), start=1):
        tokens = line.split("#", 1)[0].split()
        if not tokens:
            continue
        if tokens[0] == "instance":
            if len(tokens) != 12:
                raise ManifestError(
                    "%s:%d: instance expects 12 tokens (glb path + 10 numbers), got %d"
                    % (path, number, len(tokens))
                )
            values = [
                manifest_float(token, path, number, MANIFEST_INSTANCE_FIELDS[i])
                for i, token in enumerate(tokens[2:])
            ]
            instances.append((tokens[1], values))
        elif tokens[0] == "entity":
            if len(tokens) != 13:
                raise ManifestError(
                    "%s:%d: entity expects 13 tokens (glb path + 10 numbers + clip), got %d"
                    % (path, number, len(tokens))
                )
            values = [
                manifest_float(token, path, number, MANIFEST_INSTANCE_FIELDS[i])
                for i, token in enumerate(tokens[2:12])
            ]
            entities.append((tokens[1], values, tokens[12]))
        elif tokens[0] == "posed" and posed is not None:
            if len(tokens) != 14:
                raise ManifestError(
                    "%s:%d: posed expects 14 tokens (glb path + 10 numbers + clip + seconds), got %d"
                    % (path, number, len(tokens))
                )
            values = [
                manifest_float(token, path, number, MANIFEST_INSTANCE_FIELDS[i])
                for i, token in enumerate(tokens[2:12])
            ]
            seconds = manifest_float(tokens[13], path, number, "seconds")
            if seconds < 0:
                raise ManifestError("%s:%d: posed seconds %r is negative" % (path, number, tokens[13]))
            posed.append((tokens[1], values, tokens[12], seconds))
        elif tokens[0] == "spawn":
            if len(tokens) != 5:
                raise ManifestError(
                    "%s:%d: spawn expects 5 tokens (3 position + yaw), got %d"
                    % (path, number, len(tokens))
                )
            values = [
                manifest_float(token, path, number, MANIFEST_SPAWN_FIELDS[i])
                for i, token in enumerate(tokens[1:])
            ]
            spawns.append(values)
        else:
            raise ManifestError("%s:%d: unknown directive %r" % (path, number, tokens[0]))
    return instances, spawns, entities


def fmt_manifest_float(value):
    """A plain decimal token (no exponent) for a manifest number."""
    text = "%.6f" % value
    text = text.rstrip("0").rstrip(".")
    return text if text not in ("", "-") else "0"


# ---------------------------------------------------------------- arena sets


def arena_sets(library):
    """Per level set: (setrel, instances, excluded) — library-relative glb
    paths, sorted.  `excluded` are the *_collision* glbs (collision geometry,
    not render dependencies — §3)."""
    sets = []
    for setrel in ARENA["sets"]:
        setdir = os.path.join(library, setrel)
        if not os.path.isdir(setdir):
            die("arena set not found under the library: %s" % setdir)
        instances = []
        excluded = []
        for path in sorted(glob.glob(os.path.join(setdir, "**", "*.glb"), recursive=True)):
            rel = os.path.relpath(path, library)
            (excluded if COLLISION_MARK in os.path.basename(rel) else instances).append(rel)
        sets.append((setrel, instances, excluded))
    return sets


def shrine_sets(library):
    """The shrine's one set, as `arena_sets` returns it: the glbs that survive
    the stage's exclusions (design D3), in the pool list's order; `excluded` is
    everything dropped.  Every kept glb's cmsh must be referenced by the
    workspace (the arena's cross-check, done here because the order comes from
    the same list); the list's sha256 is pinned."""
    stage = STAGES["shrine"]
    setrel = stage["sets"][0]
    setdir = os.path.join(library, setrel)
    if not os.path.isdir(setdir):
        die("shrine set not found under the library: %s" % setdir)
    rels = [
        os.path.relpath(path, library)
        for path in sorted(glob.glob(os.path.join(setdir, "**", "*.glb"), recursive=True))
    ]
    stems = {os.path.basename(rel)[: -len(".glb")] for rel in rels}
    kept = []
    excluded = []
    for rel in rels:
        name = os.path.basename(rel)
        stem = name[: -len(".glb")]
        drop = (
            stage["exclude_stub"].search(name)
            or stage["exclude_lod"].search(stem)
            or COLLISION_MARK in stem
            or stem in stage["exclude_names"]
            or (stem.endswith("_low") and stem[: -len("_low")] in stems)
        )
        (excluded if drop else kept).append(rel)

    path = resolve_cwks(None)
    try:
        listed = arena_list(cwks_parse(path)["entries"], stage["cwks_match"])
    except CwksError as exc:
        die("%s does not fit the .cwks layout: %s" % (path, exc))
    sha = hashlib.sha256(arena_text(listed)).hexdigest()
    if (len(listed), sha) != (stage["count"], stage["cwks_sha256"]):
        die(
            "shrine path list (match %r): %d entries, sha256 %s != the pin %d, %s"
            % (stage["cwks_match"], len(listed), sha, stage["count"], stage["cwks_sha256"])
        )
    order = {}
    for entry in listed:
        if entry.startswith("/" + setrel + "/") and entry.endswith(".cmsh"):
            order.setdefault(entry[1 : -len(".cmsh")] + ".glb", len(order))
    unlisted = [rel for rel in kept if rel not in order]
    if unlisted:
        die(
            "%d shrine glb(s) not referenced by the workspace list (%s)"
            % (len(unlisted), ", ".join(unlisted[:3]))
        )
    kept.sort(key=order.__getitem__)
    return [(setrel, kept, excluded)]


def stage_sets(library, name):
    return (arena_sets if STAGES[name]["order"] == "sorted" else shrine_sets)(library)


def glb_counts(path):
    """(glTF primitives, vertices) of a glb — the mesh/vertex totals the shrine
    pins (a primitive is what the app draws as one mesh; vertices are the
    POSITION accessors' counts).  A stub glb with no meshes counts (0, 0)."""
    doc, _ = glb_container(path)
    primitives = vertices = 0
    for mesh in doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitives += 1
            vertices += doc["accessors"][primitive["attributes"]["POSITION"]]["count"]
    return primitives, vertices


def manifest_counts(library, rels):
    """The (primitives, vertices) totals over `rels`, each line counted once."""
    cache = {}
    total = [0, 0]
    for rel in rels:
        if rel not in cache:
            cache[rel] = glb_counts(os.path.join(library, rel))
        total[0] += cache[rel][0]
        total[1] += cache[rel][1]
    return tuple(total)


def shrine_placements():
    """Task 0's tools/re/s7/shrine_placements.json, validated, as
    (statues, posed, spawn): statues = [(glbRelPath, [10 floats])] — slot 1 is the
    broken glb + its skirt, slots 2-16 the intact idols — posed = (glb, [10 floats],
    clip, seconds) and spawn = (x, y, z, yaw).  Yaw is degrees about +Y, written
    as `yaw_quaternion` (glTF x, y, z, w), scale 1."""
    stage = STAGES["shrine"]
    path = os.path.normpath(stage["placements"])
    try:
        with open(path) as fh:
            data = json.load(fh)
    except (OSError, ValueError) as exc:
        die("shrine placements %s: %s" % (path, exc))

    def trs(position, yaw):
        return [float(v) for v in position] + list(yaw_quaternion(yaw)) + [1.0, 1.0, 1.0]

    def source(record, what):
        if record.get("source") not in stage["sources"]:
            die("shrine placements: %s source %r is not one of %s"
                % (what, record.get("source"), "|".join(stage["sources"])))

    try:
        idols = sorted(data["idols"], key=lambda idol: idol["slot"])
        if [idol["slot"] for idol in idols] != list(range(1, 17)):
            die("shrine placements: idol slots are not 1..16")
        broken = data["broken"]
        if broken["slot"] != 1 or len(broken["glbs"]) != len(broken["ps"]):
            die("shrine placements: broken is not slot 1 with one position per glb")
        source(broken, "broken")
        statues = []
        for idol in idols:
            source(idol, "idol %d" % idol["slot"])
            if idol["slot"] == 1:
                statues += [(g, trs(q, broken["yaw_deg"])) for g, q in zip(broken["glbs"], broken["ps"])]
            else:
                statues.append((idol["glb"], trs(idol["p"], idol["yaw_deg"])))
        mono = data["mono"]
        source(mono, "mono")
        posed = (mono["glb"], trs(mono["p"], mono["yaw_deg"]), mono["clip"], float(mono["seconds"]))
        source(data["spawn"], "spawn")
        spawn = tuple(float(v) for v in data["spawn"]["p"]) + (float(data["spawn"]["yaw_deg"]),)
    except (KeyError, TypeError, ValueError) as exc:
        die("shrine placements %s: malformed (%s: %s)" % (path, type(exc).__name__, exc))
    if spawn != stage["spawn"]:
        die("shrine spawn %s in the JSON != the pin %s" % (spawn, stage["spawn"]))
    return statues, posed, spawn


def fmt_values(values):
    return " ".join(fmt_manifest_float(v) for v in values)


def fmt_counts(counts):
    return " + ".join("%d" % n for n in counts) + " = %d" % sum(counts)


class GlbError(Exception):
    """The file does not fit the glb container the bounds check needs."""


def glb_container(path):
    """The glb's JSON document and BIN chunk (b"" when absent).  Raises
    GlbError for a container that does not match its own header."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        raise GlbError("cannot read: %s" % (exc.strerror or exc))
    if len(data) < 20 or data[:4] != b"glTF":
        raise GlbError("not a glb (magic %r)" % data[:4])
    size = u32le(data, 8)
    if size != len(data):
        raise GlbError("declared length %d != file size %d" % (size, len(data)))
    chunk_len, chunk_type = struct.unpack_from("<I4s", data, 12)
    if chunk_type != b"JSON":
        raise GlbError("first chunk %r is not JSON" % chunk_type)
    try:
        doc = json.loads(data[20 : 20 + chunk_len])
    except ValueError as exc:
        raise GlbError("JSON chunk does not parse: %s" % exc)
    bin_chunk = b""
    offset = 20 + chunk_len
    if offset + 8 <= len(data):
        bin_len, bin_type = struct.unpack_from("<I4s", data, offset)
        if bin_type == b"BIN\x00":
            bin_chunk = data[offset + 8 : offset + 8 + bin_len]
    return doc, bin_chunk


def glb_bounds(path, require_identity=True):
    """The glb's bounds (lo, hi) as the union of every POSITION accessor's
    min/max.  With `require_identity` (the default) a node transform is an
    error: the converter's output has none, and a node transform would make the
    raw accessor bounds wrong — fail loudly instead of silently mis-measuring.
    A skinned model (the colossus) passes False: its vertex positions are in
    skin space, the same space the app's skinning path uses."""
    doc, _ = glb_container(path)
    if require_identity:
        for node in doc.get("nodes", []):
            if any(key in node for key in ("translation", "rotation", "scale", "matrix")):
                raise GlbError("node transform present; the accessor bounds would need it")
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    primitives = 0
    for mesh in doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            accessor = doc["accessors"][primitive["attributes"]["POSITION"]]
            if "min" not in accessor or "max" not in accessor:
                raise GlbError("POSITION accessor without min/max")
            for i in range(3):
                lo[i] = min(lo[i], accessor["min"][i])
                hi[i] = max(hi[i], accessor["max"][i])
            primitives += 1
    if primitives == 0:
        raise GlbError("no POSITION accessor (0 primitives)")
    return lo, hi


def glb_ground_height(path, x, z):
    """The highest Y of any triangle covering the column (x, z), or None.
    Positions/indices are read from the BIN chunk; node transforms are ignored
    (the level glbs are already world-positioned and identity-placed)."""
    doc, bin_chunk = glb_container(path)
    best = None
    for mesh in doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            position = primitive["attributes"].get("POSITION")
            if position is None:
                continue
            accessor = doc["accessors"][position]
            view = doc["bufferViews"][accessor["bufferView"]]
            base = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            stride = view.get("byteStride", 12)
            count = accessor["count"]
            points = [
                struct.unpack_from("<fff", bin_chunk, base + i * stride) for i in range(count)
            ]
            indices = primitive.get("indices")
            if indices is None:
                order = range(0, count - 2, 3)
                tri = lambda k: (points[k], points[k + 1], points[k + 2])  # noqa: E731
            else:
                iacc = doc["accessors"][indices]
                iview = doc["bufferViews"][iacc["bufferView"]]
                ibase = iview.get("byteOffset", 0) + iacc.get("byteOffset", 0)
                ctype = {5121: "<B", 5123: "<H", 5125: "<I"}[iacc["componentType"]]
                isize = struct.calcsize(ctype)
                idx = [
                    struct.unpack_from(ctype, bin_chunk, ibase + i * isize)[0]
                    for i in range(iacc["count"])
                ]
                order = range(0, len(idx) - 2, 3)
                tri = lambda k: (points[idx[k]], points[idx[k + 1]], points[idx[k + 2]])  # noqa: E731
            for k in order:
                a, b, c = tri(k)
                d = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2])
                if abs(d) < 1e-9:
                    continue
                u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d
                v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / d
                if u < 0 or v < 0 or u + v > 1:
                    continue
                y = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])
                if best is None or y > best:
                    best = y
    return best


def yaw_between(source_xz, target_xz):
    """The app's yaw convention (degrees): 0 faces +Z, 90 faces +X."""
    return math.degrees(math.atan2(target_xz[0] - source_xz[0], target_xz[1] - source_xz[1]))


def yaw_quaternion(yaw_deg):
    """The +Y rotation quaternion (x, y, z, w) — the app's player/entity yaw."""
    half = 0.5 * math.radians(yaw_deg)
    return (0.0, math.sin(half), 0.0, math.cos(half))


def yaw_from_quaternion(values):
    """Recovers the +Y yaw from a glTF-order (x, y, z, w) quaternion; refuses an
    axis that is not +Y (the encounter's placement contract)."""
    qx, qy, qz, qw = values[3], values[4], values[5], values[6]
    if abs(qx) > 1e-6 or abs(qz) > 1e-6:
        raise ManifestError("entity rotation is not a pure +Y yaw (x=%.6f z=%.6f)" % (qx, qz))
    norm = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    if abs(norm - 1.0) > 1e-5:
        raise ManifestError("entity rotation is not unit length (|q|=%.6f)" % norm)
    return math.degrees(2.0 * math.atan2(qy, qw))


def bounds_decimals(text):
    """The decimals each value in an evidence string uses, e.g. `y[-1.7,81.5]`."""
    return [len(part.split(".")[1]) if "." in part else 0 for part in text[2:-1].split(",")]


def fmt_bounds(lo, hi, evidence):
    """Recomputed bounds formatted at the evidence's own precision, so the
    plan's rounded numbers can be matched exactly (e.g. `x[237,431]`)."""
    parts = []
    for axis, text, low, high in zip("xyz", evidence, lo, hi):
        fmt = "%%.%df" % max(bounds_decimals(text))
        parts.append("%s[%s,%s]" % (axis, fmt % low, fmt % high))
    return " ".join(parts)


# -------------------------------------------------------------------- compose


def cross_check_paths(instances, list_path):
    """Cross-check the instance set against W2.2's pool path list (`cwks
    --arena-out`, sha256-pinned): every arena_01 instance's source cmsh is
    referenced by the workspace and every referenced arena_01 cmsh has an
    instance.  The list's pattern (`levels/arena_01/`) does not cover the
    entrance set — reported, not checked.  Returns a one-line summary."""
    if not os.path.isfile(list_path):
        die(
            "path list not found: %s (regenerate: python3 tools/re/stage/probe.py "
            "cwks --arena-out %s)" % (list_path, list_path)
        )
    raw = read_bytes(list_path)
    sha = hashlib.sha256(raw).hexdigest()
    if sha != ARENA["cwks_sha256"]:
        die(
            "path list %s: sha256 %s != the pin %s (regenerate with cwks --arena-out)"
            % (list_path, sha, ARENA["cwks_sha256"])
        )
    listed = [line for line in raw.decode("utf-8").splitlines() if line]

    parts = []
    for setrel in ARENA["sets"]:
        want = {
            "/" + rel[: -len(".glb")] + ".cmsh"
            for rel in instances
            if rel.startswith(setrel + "/")
        }
        have = {
            path
            for path in listed
            if path.startswith("/" + setrel + "/") and path.endswith(".cmsh")
        }
        if not have:
            parts.append("%s: not covered by the list" % os.path.basename(setrel))
            continue
        missing = sorted(want - have)
        extra = sorted(have - want)
        if missing or extra:
            die(
                "cross-check vs %s: %s: %d cmsh not referenced%s, %d referenced but "
                "not instanced%s"
                % (
                    list_path,
                    setrel,
                    len(missing),
                    " (%s)" % ", ".join(missing[:3]) if missing else "",
                    len(extra),
                    " (%s)" % ", ".join(extra[:3]) if extra else "",
                )
            )
        parts.append("%s: %d/%d cmsh referenced" % (os.path.basename(setrel), len(want), len(want)))
    return "; ".join(parts)


def cmd_compose(args):
    library = os.path.abspath(args.library)
    if not os.path.isdir(library):
        die("library not found: %s (the converted glb tree, e.g. out/library)" % library)
    stage = STAGES[args.stage]
    arena = args.stage == "arena_01"

    sets = stage_sets(library, args.stage)
    instances = [rel for _, set_instances, _ in sets for rel in set_instances]
    if stage["instances"] is not None and len(instances) != stage["instances"]:
        die(
            "instance set is %d glbs, expected %d (%s files minus %s excluded)"
            % (
                len(instances),
                stage["instances"],
                fmt_counts([len(i) + len(e) for _, i, e in sets]),
                fmt_counts([len(e) for _, _, e in sets]),
            )
        )

    if arena:
        summary = cross_check_paths(instances, os.path.abspath(args.paths))
        header = [
            "# valus arena — RE-3 composition (W2.3), generated by tools/re/stage/probe.py compose",
            "# instances: every non-*_collision* glb of models/shadow/levels/arena_01/ and",
            "#   models/shadow/levels/arena_01_entrance/ (%s of the %s glb files;"
            % (fmt_counts([len(i) for _, i, _ in sets]), fmt_counts([len(i) + len(e) for _, i, e in sets])),
            "#   %s collision glbs excluded — collision geometry, not render dependencies);"
            % fmt_counts([len(e) for _, _, e in sets]),
            "#   identity transforms: the glbs are already world-positioned (the level glbs span",
            "#   x[237,431] y[28,70] z[298,390] and x[259,345] y[-1.7,81.5] z[249,307]; the sets",
            "#   meet near z~300).  Cross-checked against out/re/stage/arena_paths.txt (sha256-pinned).",
            "# spawn: BUILDER PICK, not data-derived — the entrance ground mesh's footprint centre at",
            "#   its surface height, facing +Z into the arena; the candidates (.path, camdata) are",
            "#   recorded in docs/notes/re-stage.md §3.",
        ]
    else:
        summary = "%d/%d kept glbs' cmsh referenced" % (len(instances), len(instances))
        statues, posed, _ = shrine_placements()
        header = [
            "# shrine — S7 composition, generated by tools/re/stage/probe.py compose --stage shrine",
            "# instances: the %d highest-detail glbs of %s/ (of the %d glb files), in the"
            % (len(instances), stage["sets"][0], len(instances) + len(sets[0][2])),
            "#   workspace pool-list order; hash-suffix stubs, _lod*, *_collision*, the occluders,",
            "#   the shadow casters/blockers, emitter_arch_lightray_long and _low duplicates dropped",
            "#   (design D3); identity transforms: the glbs are already world-positioned.",
            "# statues: %d instances from tools/re/s7/shrine_placements.json (idols: source data;"
            % len(statues),
            "#   slot 1 is the shipped broken glb + its skirt); posed Mono and spawn from the same",
            "#   file (spawn position source data, yaw authored; Mono source authored).",
        ]
    lines = list(header)
    for rel in instances:
        lines.append("instance %s 0 0 0 0 0 0 1 1 1 1" % rel)
    all_rels = list(instances)
    if not arena:
        for rel, values in statues:
            lines.append("instance %s %s" % (rel, fmt_values(values)))
            all_rels.append(rel)
        lines.append("posed %s %s %s %s" % (posed[0], fmt_values(posed[1]), posed[2], fmt_manifest_float(posed[3])))
        all_rels.append(posed[0])
        totals = manifest_counts(library, all_rels)
        if stage["meshes"] is not None and totals != stage["meshes"]:
            die("shrine mesh/vertex totals %s != the pin %s" % (totals, stage["meshes"]))
    spawn = stage["spawn"]
    if spawn:
        lines.append("spawn %s" % " ".join(fmt_manifest_float(v) for v in spawn))
    text = "\n".join(lines) + "\n"

    # Round-trip the text through the schema before writing: a path that the
    # format cannot carry (whitespace, `#`) must fail here, not in the app.
    parsed_posed = [] if not arena else None
    parsed_instances, parsed_spawns, parsed_entities = parse_manifest_text(text, args.out, parsed_posed)
    n_extra = 0 if arena else len(statues)
    if (
        len(parsed_instances) != len(instances) + n_extra
        or len(parsed_spawns) != (1 if spawn else 0)
        or parsed_entities
        or (not arena and len(parsed_posed) != 1)
    ):
        die(
            "composed manifest does not round-trip: %d instances, %d spawns"
            % (len(parsed_instances), len(parsed_spawns))
        )

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w") as fh:
        fh.write(text)

    print("compose: library %s" % library)
    for setrel, set_instances, set_excluded in sets:
        print(
            "compose: %-44s %d glb (%d instances, %d %sexcluded)"
            % (
                setrel,
                len(set_instances) + len(set_excluded),
                len(set_instances),
                len(set_excluded),
                "*_collision* " if arena else "",
            )
        )
    if arena:
        print(
            "compose: cross-check %s (sha256 %s): %s"
            % (args.paths, ARENA["cwks_sha256"][:8] + "…", summary)
        )
    else:
        print(
            "compose: cross-check %s (sha256 %s…, %d entries): %s"
            % (stage["cwks_match"], stage["cwks_sha256"][:8], stage["count"], summary)
        )
    if spawn:
        print(
            "compose: spawn %s (%s)"
            % (" ".join(fmt_manifest_float(v) for v in spawn),
               "builder pick — docs/notes/re-stage.md §3" if arena else "shrine_placements.json")
        )
    else:
        print("compose: no spawn (the %s stage has none pinned yet)" % args.stage)
    if not arena:
        print(
            "compose: %d statue instances + 1 posed (%s %s at %s s); totals %d meshes, %d vertices; sha256 %s"
            % (len(statues), posed[0].rsplit("/", 1)[-1], posed[2], fmt_manifest_float(posed[3]),
               totals[0], totals[1], hashlib.sha256(text.encode("utf-8")).hexdigest())
        )
    print(
        "compose: wrote %s (%d instances + %d spawn + %d comment lines)"
        % (out, len(instances) + (0 if arena else len(statues) + 1), 1 if spawn else 0, len(header))
    )
    return 0


# ------------------------------------------------------------ check-manifest


def cmd_check_manifest(args):
    library = os.path.abspath(args.library)
    if not os.path.isdir(library):
        die("library not found: %s (the converted glb tree, e.g. out/library)" % library)
    path = os.path.abspath(args.file)
    stage = STAGES[args.stage]
    arena = args.stage == "arena_01"
    checks = []

    def add(name, kind, expected, measured, ok):
        checks.append(Check(name, kind, expected, measured, ok))

    def fail_all(reason):
        for name, kind, expected in check_manifest_spec(args.stage):
            add(name, kind, expected, "unparsed: %s" % reason, False)

    try:
        if not os.path.isfile(path):
            raise ManifestError("%s: cannot open manifest file" % path)
        raw = read_bytes(path)
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ManifestError("%s: not UTF-8 text (%s)" % (path, exc))
        posed = [] if args.stage != "arena_01" else None
        instances, spawns, _entities = parse_manifest_text(text, path, posed)
        error = None
    except ManifestError as exc:
        instances, spawns, error = None, None, str(exc)

    if error is not None:
        fail_all(error)
    elif not arena:
        for name, kind, expected, measured, ok in shrine_checks(library, text, instances, spawns, posed):
            add(name, kind, expected, measured, ok)
    else:
        sets = stage_sets(library, args.stage)
        want = [rel for _, set_instances, _ in sets for rel in set_instances]
        files = [len(i) + len(e) for _, i, e in sets]
        collisions = [len(e) for _, _, e in sets]
        rels = [rel for rel, _ in instances]
        count = stage["instances"]

        add(
            "manifest parses (W1↔W2 schema: comments, exact tokens, strict finite floats)",
            "structure",
            "ok",
            "ok",
            True,
        )
        if arena:
            add(
                "arena glb files (pre-filter)",
                "structure",
                fmt_counts(stage["set_files"]),
                fmt_counts(files),
                tuple(files) == stage["set_files"],
            )
            add(
                "*_collision* glbs excluded",
                "structure",
                fmt_counts(stage["set_collision"]),
                fmt_counts(collisions),
                tuple(collisions) == stage["set_collision"],
            )
            add(
                "manifest instances (non-*_collision*)",
                "structure",
                "%d (%d - %d)"
                % (count, sum(stage["set_files"]), sum(stage["set_collision"])),
                len(instances),
                len(instances) == count,
            )
        exact = sorted(set(rels)) == sorted(want) and len(rels) == len(set(rels))
        missing = sorted(set(want) - set(rels))
        extra = sorted(set(rels) - set(want))
        duplicates = sorted(rel for rel in set(rels) if rels.count(rel) > 1)
        if exact:
            set_detail = "exact (%d)" % len(want)
        else:
            parts = []
            if missing:
                parts.append("missing %d (%s)" % (len(missing), ", ".join(missing[:3])))
            if extra:
                parts.append("extra %d (%s)" % (len(extra), ", ".join(extra[:3])))
            if duplicates:
                parts.append("duplicates %d (%s)" % (len(duplicates), ", ".join(duplicates[:3])))
            set_detail = "differs: " + "; ".join(parts)
        add(
            "instance set == the library's non-*_collision* set",
            "structure",
            "exact (%d)" % len(want),
            set_detail,
            exact,
        )
        existing = [rel for rel in rels if os.path.isfile(os.path.join(library, rel))]
        missing_files = sorted(set(rels) - set(existing))
        exists_detail = "%d/%d" % (len(existing), len(rels))
        if missing_files:
            exists_detail += " missing: %s" % ", ".join(missing_files[:3])
        add(
            "every glbRelPath exists under the library",
            "structure",
            "%d/%d" % (len(rels), len(rels)),
            exists_detail,
            len(existing) == len(rels),
        )
        identity = [
            1
            for _, v in instances
            if v[:3] == [0, 0, 0] and v[3:7] == [0, 0, 0, 1] and v[7:10] == [1, 1, 1]
        ]
        add(
            "identity transforms on every instance",
            "structure",
            "%d/%d" % (len(instances), len(instances)),
            "%d/%d" % (len(identity), len(instances)),
            len(identity) == len(instances),
        )
        add("exactly one spawn", "structure", 1, len(spawns), len(spawns) == 1)

        # The level glbs' bounds: re-derived from the library, matched to the
        # plan's rounded evidence; then the spawn tested against the entrance's.
        bounds = {}
        for key, rel, evidence in stage.get("bounds_evidence", ()):
            name = "%s bounds evidence (plan %s)" % (key, " ".join(evidence))
            try:
                lo, hi = glb_bounds(os.path.join(library, rel))
            except GlbError as exc:
                add(name, "measured", " ".join(evidence), "unparsed: %s" % exc, False)
                continue
            measured = fmt_bounds(lo, hi, evidence)
            add(name, "measured", " ".join(evidence), measured, measured == " ".join(evidence))
            bounds[key] = (lo, hi)

        if arena:
            entrance = " ".join(stage["bounds_evidence"][1][2])
            spawn = spawns[0] if spawns else None
            if bounds.get("entrance") is None or spawn is None:
                add(
                    "spawn inside the entrance bounds",
                    "structure",
                    "inside " + entrance,
                    "unparsed: %s" % ("no entrance bounds" if spawn is not None else "no spawn line"),
                    False,
                )
            else:
                lo, hi = bounds["entrance"]
                inside = all(lo[i] <= spawn[i] <= hi[i] for i in range(3))
                add(
                    "spawn inside the entrance bounds",
                    "structure",
                    "inside " + entrance,
                    "inside" if inside else "outside (spawn %.3f %.3f %.3f)" % tuple(spawn[:3]),
                    inside,
                )

    print("check-manifest: %s (library %s)" % (path, library))
    for check in checks:
        status = "PASS" if check.ok else "FAIL"
        print(
            "%-5s %-9s %-58s expected %s, measured %s"
            % (status, check.kind, check.name, check.expected, check.measured)
        )
    failed = [c for c in checks if not c.ok]
    print("summary: %d/%d gating assertions pass" % (len(checks) - len(failed), len(checks)))
    if failed:
        print(
            "gate: NOT MET — %d assertion(s) fail; re-measure and record "
            "(docs/notes/re-stage.md §3)" % len(failed)
        )
        return 1
    print("gate: MET")
    return 0


# The check-manifest spec: (name, kind, expected) — used verbatim when the
# manifest is unparsable, so every pin renders as a FAIL with the parse error.
SHRINE_STATUES = 17
MONO_CLIP = "girl_a_death_loop"


def shrine_check_spec():
    """(name, kind, expected) per shrine row — used verbatim when the manifest is
    unparsable, and as the row order of `shrine_checks`."""
    stage = STAGES["shrine"]
    spawn = " ".join(fmt_manifest_float(v) for v in stage["spawn"])
    return [
        ("manifest parses (W1↔W2 schema: comments, exact tokens, strict finite floats)",
         "structure", "ok"),
        ("manifest instances (the shrine set)", "structure", stage["instances"]),
        ("shrine instance lines == the library's set, in cwks order", "structure",
         "exact, ordered (%d)" % stage["instances"]),
        ("statue instance lines == the placements JSON, in order", "structure",
         "exact (%d)" % SHRINE_STATUES),
        ("slot 1 is the broken glb + skirt (no intact mino_a_statue)", "structure", "ok"),
        ("one posed Mono line == the placements JSON (%s)" % MONO_CLIP, "structure", "exact (1)"),
        ("every glbRelPath exists under the library", "structure", "all"),
        ("identity transforms on every shrine-set instance", "structure",
         "%d/%d" % (stage["instances"], stage["instances"])),
        ("no excluded name among the instances", "structure", "0 excluded"),
        ("exactly one spawn == the pin (%s)" % spawn, "structure", 1),
        ("mesh/vertex totals over the manifest's glbs", "measured",
         "%d meshes, %d vertices" % tuple(stage["meshes"])),
        ("manifest sha256", "measured", stage["sha256"]),
    ]


def shrine_excluded(rel):
    """The reason the stage's D3 exclusions would drop this glb, or None."""
    stage = STAGES["shrine"]
    name = os.path.basename(rel)
    stem = name[: -len(".glb")]
    if stage["exclude_stub"].search(name):
        return "hash stub"
    if stage["exclude_lod"].search(stem):
        return "lod"
    if COLLISION_MARK in stem:
        return "collision"
    if stem in stage["exclude_names"]:
        return "excluded name"
    return None


def shrine_checks(library, text, instances, spawns, posed):
    """The shrine rows, measured: [(name, kind, expected, measured, ok)] in
    `shrine_check_spec` order."""
    stage = STAGES["shrine"]
    spec = shrine_check_spec()
    setdir = stage["sets"][0] + "/"
    want = [rel for _, set_instances, _ in shrine_sets(library) for rel in set_instances]
    statues, mono, _ = shrine_placements()
    rels = [rel for rel, _ in instances]
    set_lines = [(rel, v) for rel, v in instances if rel.startswith(setdir)]
    statue_lines = [(rel, v) for rel, v in instances if not rel.startswith(setdir)]
    results = [("ok", True)]

    n = len(set_lines)
    results.append((n, n == stage["instances"]))
    ordered = rels[: len(want)] == want and n == len(want)
    results.append(("ordered (%d)" % n if ordered else "differs (%d lines, %d wanted)" % (n, len(want)), ordered))

    got = [(rel, fmt_values(v)) for rel, v in statue_lines]
    exp = [(rel, fmt_values(v)) for rel, v in statues]
    results.append(("exact (%d)" % len(got) if got == exp else "differs (%d lines, %d wanted)" % (len(got), len(exp)),
                    got == exp and len(got) == SHRINE_STATUES))
    broken = [rel.rsplit("/", 1)[-1] for rel, _ in statue_lines[:2]] == ["mino_a_statue_broke.glb", "mino_a_statue_broke_skirt.glb"]
    intact = any(rel.endswith("/mino_a_statue.glb") for rel in rels)
    results.append(("ok" if broken and not intact else "broken lines %s, intact present %s" % (broken, intact),
                    broken and not intact))

    mono_ok = (
        len(posed) == 1
        and (posed[0][0], fmt_values(posed[0][1]), posed[0][2], fmt_manifest_float(posed[0][3]))
        == (mono[0], fmt_values(mono[1]), mono[2], fmt_manifest_float(mono[3]))
        and posed[0][2] == MONO_CLIP
    )
    results.append(("exact (1)" if mono_ok else "%d posed line(s), differs" % len(posed), mono_ok))

    every = rels + [p[0] for p in posed]
    missing = sorted({r for r in every if not os.path.isfile(os.path.join(library, r))})
    results.append(("%d/%d" % (len(every) - len([r for r in every if r in missing]), len(every))
                    + (" missing: %s" % ", ".join(missing[:3]) if missing else ""), not missing))

    identity = [1 for _, v in set_lines if v[:3] == [0, 0, 0] and v[3:7] == [0, 0, 0, 1] and v[7:10] == [1, 1, 1]]
    results.append(("%d/%d" % (len(identity), n), len(identity) == n))

    bad = [rel for rel in every if shrine_excluded(rel)]
    results.append(("%d excluded" % len(bad) + (": %s" % ", ".join(bad[:3]) if bad else ""), not bad))

    spawn_ok = len(spawns) == 1 and tuple(spawns[0]) == stage["spawn"]
    results.append((len(spawns), spawn_ok))

    if missing:
        results.append(("unmeasurable: missing glbs", False))
    else:
        totals = manifest_counts(library, every)
        results.append(("%d meshes, %d vertices" % totals, totals == tuple(stage["meshes"])))
    sha = hashlib.sha256(text.encode("utf-8")).hexdigest()
    results.append((sha, sha == stage["sha256"]))

    return [(name, kind, expected, measured, ok) for (name, kind, expected), (measured, ok) in zip(spec, results)]


def check_manifest_spec(name):
    stage = STAGES[name]
    count = stage["instances"]
    head = [
        ("manifest parses (W1↔W2 schema: comments, exact tokens, strict finite floats)",
         "structure", "ok"),
    ]
    if name != "arena_01":
        return shrine_check_spec()
    (key0, _, ev0), (key1, _, ev1) = stage["bounds_evidence"]
    return head + [
        ("arena glb files (pre-filter)", "structure", fmt_counts(stage["set_files"])),
        ("*_collision* glbs excluded", "structure", fmt_counts(stage["set_collision"])),
        ("manifest instances (non-*_collision*)", "structure",
         "%d (%d - %d)" % (count, sum(stage["set_files"]), sum(stage["set_collision"]))),
        ("instance set == the library's non-*_collision* set", "structure",
         "exact (%d)" % count),
        ("every glbRelPath exists under the library", "structure", "%d/%d" % (count, count)),
        ("identity transforms on every instance", "structure", "%d/%d" % (count, count)),
        ("exactly one spawn", "structure", 1),
        ("%s bounds evidence (plan %s)" % (key0, " ".join(ev0)), "measured", " ".join(ev0)),
        ("%s bounds evidence (plan %s)" % (key1, " ".join(ev1)), "measured", " ".join(ev1)),
        ("spawn inside the entrance bounds", "structure", "inside " + " ".join(ev1)),
    ]


# ------------------------------------------------------- encounter (S4, Task 6)
# The encounter manifest derives from the S3 arena manifest (one source for the
# 30 instances), rewrites the spawn to the data position with a builder yaw,
# appends one `entity` (Valus), and writes a NEW provenance header: the S3
# header is not inherited, so the placement/spawn labels travel with this file.
# docs/notes/re4-placement.md §8 is the source of the values (the pull's verdict
# was negative; the labeled builder pick is the design's fallback).


def encounter_instance_set(library):
    """The S3 instance set from the library, sorted, for the set comparison."""
    sets = arena_sets(library)
    return [rel for _, set_instances, _ in sets for rel in set_instances]


def fmt_ground(value):
    """A ground sample as manifest metres: millimetre precision (the re4 note's
    rounded values are the same numbers: 28.369 / 27.951)."""
    return fmt_manifest_float(round(value, 3))


def cmd_compose_encounter(args):
    library = os.path.abspath(args.library)
    if not os.path.isdir(library):
        die("library not found: %s (the converted glb tree, e.g. out/library)" % library)
    s3_path = os.path.abspath(args.s3_manifest)
    try:
        with open(s3_path, "rb") as fh:
            raw = fh.read()
    except OSError as exc:
        die(
            "S3 manifest not found: %s (%s) — regenerate it: python3 tools/re/stage/probe.py "
            "compose --library %s --out %s"
            % (s3_path, exc.strerror or exc, library, MANIFEST_OUT)
        )
    digest = hashlib.sha256(raw).hexdigest()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        die("%s: not UTF-8 text (%s)" % (s3_path, exc))
    try:
        instances, spawns, entities = parse_manifest_text(text, s3_path)
    except ManifestError as exc:
        die("S3 manifest does not parse: %s" % exc)
    if entities:
        die("S3 manifest already carries %d entity line(s); compose the encounter from the arena manifest" % len(entities))
    if len(instances) != ARENA["instances"]:
        die(
            "S3 manifest has %d instances, expected %d — regenerate it with `compose`"
            % (len(instances), ARENA["instances"])
        )
    if not spawns:
        die("S3 manifest has no spawn line; `compose` always writes one")
    want = encounter_instance_set(library)
    rels = [rel for rel, _ in instances]
    if sorted(rels) != sorted(want):
        die(
            "S3 manifest's instance set differs from the library's non-*_collision* set "
            "(%d vs %d): regenerate it with `compose`" % (len(rels), len(want))
        )

    ground_glb = os.path.join(library, ENCOUNTER_GROUND_GLB)
    try:
        entity_ground = glb_ground_height(ground_glb, *ENCOUNTER_ENTITY_XZ)
        spawn_ground = glb_ground_height(ground_glb, *ENCOUNTER_SPAWN_XZ)
    except GlbError as exc:
        die("ground mesh %s: %s" % (ground_glb, exc))
    if entity_ground is None or spawn_ground is None:
        die(
            "ground mesh %s has no triangle under %s"
            % (ground_glb, ENCOUNTER_ENTITY_XZ if entity_ground is None else ENCOUNTER_SPAWN_XZ)
        )

    entity_yaw = yaw_between(ENCOUNTER_ENTITY_XZ, ENCOUNTER_SPAWN_XZ)
    spawn_yaw = yaw_between(ENCOUNTER_SPAWN_XZ, ENCOUNTER_ENTITY_XZ)
    qx, qy, qz, qw = yaw_quaternion(entity_yaw)

    header = [
        "# encounter — S4.0 \"The encounter\", generated by tools/re/stage/probe.py compose-encounter",
        "# source: %s (sha256 %s, %d instances); the instance lines are carried"
        % (args.s3_manifest, digest, len(instances)),
        "#   from that arena composition — one source for the arena; the S3 header is NOT inherited.",
        "# placement=%s entity=%s clip=%s"
        % (ENCOUNTER_PLACEMENT, ENCOUNTER_ENTITY_GLB, ENCOUNTER_ENTITY_CLIP),
        "#   bowl floor (%s, %s), measured ground %s m, yaw %.6f deg (q %.6f %.6f %.6f %.6f),"
        % (
            fmt_manifest_float(ENCOUNTER_ENTITY_XZ[0]),
            fmt_manifest_float(ENCOUNTER_ENTITY_XZ[1]),
            fmt_ground(entity_ground),
            entity_yaw,
            qx,
            qy,
            qz,
            qw,
        ),
        "#   facing the spawn.  Builder pick, not data: no shipped record satisfies the",
        "#   (a)+(b)+(c) bar (docs/notes/re4-placement.md §7-8).",
        "# spawn=%s position (%s, %s), measured ground %s m, yaw %.6f deg;"
        % (
            ENCOUNTER_SPAWN_PROVENANCE,
            fmt_manifest_float(ENCOUNTER_SPAWN_XZ[0]),
            fmt_manifest_float(ENCOUNTER_SPAWN_XZ[1]),
            fmt_ground(spawn_ground),
            spawn_yaw,
        ),
        "#   x/z from misc/playerstartpositions.txt:1 (`353 29 345`, y=29 is not the feet",
        "#   height), y re-sampled on %s, yaw builder-chosen to face the entity."
        % os.path.basename(ENCOUNTER_GROUND_GLB),
        "# grounding: the manifest y is the ground sample; the idle clip carries Bone_root's",
        "#   translation series, so the app applies no extra origin-to-feet lift (s4-status feet=ok).",
    ]
    lines = list(header)
    for rel, values in instances:
        lines.append(
            "instance %s %s" % (rel, " ".join(fmt_manifest_float(v) for v in values))
        )
    lines.append(
        "entity %s %s %s %s %s %s %s %s %s %s %s %s"
        % (
            ENCOUNTER_ENTITY_GLB,
            fmt_manifest_float(ENCOUNTER_ENTITY_XZ[0]),
            fmt_ground(entity_ground),
            fmt_manifest_float(ENCOUNTER_ENTITY_XZ[1]),
            "0",
            "%.6f" % qy,
            "0",
            "%.6f" % qw,
            "1",
            "1",
            "1",
            ENCOUNTER_ENTITY_CLIP,
        )
    )
    lines.append(
        "spawn %s %s %s %s"
        % (
            fmt_manifest_float(ENCOUNTER_SPAWN_XZ[0]),
            fmt_ground(spawn_ground),
            fmt_manifest_float(ENCOUNTER_SPAWN_XZ[1]),
            "%.6f" % spawn_yaw,
        )
    )
    text = "\n".join(lines) + "\n"

    # Round-trip through the schema before writing: a path or value the format
    # cannot carry must fail here, not in the app.
    try:
        parsed = parse_manifest_text(text, args.out)
    except ManifestError as exc:
        die("composed encounter manifest does not parse: %s" % exc)
    if len(parsed[0]) != ARENA["instances"] or len(parsed[1]) != 1 or len(parsed[2]) != 1:
        die(
            "composed encounter manifest does not round-trip: %d instances, %d spawns, %d entities"
            % (len(parsed[0]), len(parsed[1]), len(parsed[2]))
        )

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w") as fh:
        fh.write(text)

    print("compose-encounter: library %s" % library)
    print(
        "compose-encounter: source %s (sha256 %s, %d instances)"
        % (s3_path, digest[:16] + "…", len(instances))
    )
    print(
        "compose-encounter: entity %s clip %s at (%s, %s) ground %s yaw %.6f (placement=%s)"
        % (
            ENCOUNTER_ENTITY_GLB,
            ENCOUNTER_ENTITY_CLIP,
            fmt_manifest_float(ENCOUNTER_ENTITY_XZ[0]),
            fmt_manifest_float(ENCOUNTER_ENTITY_XZ[1]),
            fmt_ground(entity_ground),
            entity_yaw,
            ENCOUNTER_PLACEMENT,
        )
    )
    print(
        "compose-encounter: spawn (%s, %s) ground %s yaw %.6f (provenance=%s)"
        % (
            fmt_manifest_float(ENCOUNTER_SPAWN_XZ[0]),
            fmt_manifest_float(ENCOUNTER_SPAWN_XZ[1]),
            fmt_ground(spawn_ground),
            spawn_yaw,
            ENCOUNTER_SPAWN_PROVENANCE,
        )
    )
    print(
        "compose-encounter: wrote %s (%d instances + 1 entity + 1 spawn + %d comment lines)"
        % (out, len(instances), len(header))
    )
    return 0


CHECK_ENCOUNTER_SPEC = [
    ("manifest parses (S3 schema + entity directive)", "structure", "ok"),
    ("manifest instances (the S3 arena set)", "structure", ARENA["instances"]),
    ("instance set == the library's non-*_collision* set", "structure",
     "exact (%d)" % ARENA["instances"]),
    ("every glbRelPath exists under the library", "structure",
     "%d/%d" % (ARENA["instances"], ARENA["instances"])),
    ("identity transforms on every instance", "structure",
     "%d/%d" % (ARENA["instances"], ARENA["instances"])),
    ("exactly one entity", "structure", 1),
    ("exactly one spawn", "structure", 1),
    ("header provenance placement=builder-pick", "provenance", ENCOUNTER_PLACEMENT),
    ("header provenance spawn=data-position+builder-yaw", "provenance",
     ENCOUNTER_SPAWN_PROVENANCE),
    ("header provenance entity clip=minotaur_a_stand", "provenance", ENCOUNTER_ENTITY_CLIP),
    ("entity glb", "structure", ENCOUNTER_ENTITY_GLB),
    ("entity glb exists under the library", "structure", "present"),
    ("entity clip", "structure", ENCOUNTER_ENTITY_CLIP),
    ("spawn inside the arena bowl (x/z)", "structure", "inside"),
    ("entity inside the arena bowl (x/z)", "structure", "inside"),
    ("spawn y == the sampled ground (%.3f)" % ENCOUNTER_SPAWN_GROUND_Y, "measured",
     "±0.001"),
    ("entity y == the sampled ground (%.3f)" % ENCOUNTER_ENTITY_GROUND_Y, "measured",
     "±0.001"),
    ("entity faces the spawn (quaternion yaw)", "measured", "±0.01 deg"),
    ("spawn faces the entity (yaw)", "measured", "±0.01 deg"),
    ("entity yawed bind footprint inside the bowl", "measured", "inside"),
    ("spawn outside the entity's yawed bind footprint", "structure", "outside"),
    ("spawn-entity distance < 100 m", "measured", "< 100"),
]


def cmd_check_encounter(args):
    library = os.path.abspath(args.library)
    if not os.path.isdir(library):
        die("library not found: %s (the converted glb tree, e.g. out/library)" % library)
    path = os.path.abspath(args.file)
    checks = []

    def add(name, kind, expected, measured, ok):
        checks.append(Check(name, kind, expected, measured, ok))

    def fail_all(reason):
        for name, kind, expected in CHECK_ENCOUNTER_SPEC:
            add(name, kind, expected, "unparsed: %s" % reason, False)

    try:
        if not os.path.isfile(path):
            raise ManifestError("%s: cannot open manifest file" % path)
        raw = read_bytes(path)
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ManifestError("%s: not UTF-8 text (%s)" % (path, exc))
        instances, spawns, entities = parse_manifest_text(text, path)
        error = None
    except ManifestError as exc:
        instances, spawns, entities, error = None, None, None, str(exc)

    if error is not None:
        fail_all(error)
    else:
        want = encounter_instance_set(library)
        rels = [rel for rel, _ in instances]
        add(
            "manifest parses (S3 schema + entity directive)",
            "structure",
            "ok",
            "ok",
            True,
        )
        add(
            "manifest instances (the S3 arena set)",
            "structure",
            ARENA["instances"],
            len(instances),
            len(instances) == ARENA["instances"],
        )
        exact = sorted(rels) == sorted(want) and len(rels) == len(set(rels))
        missing = sorted(set(want) - set(rels))
        extra = sorted(set(rels) - set(want))
        duplicates = sorted(rel for rel in set(rels) if rels.count(rel) > 1)
        if exact:
            set_detail = "exact (%d)" % len(want)
        else:
            parts = []
            if missing:
                parts.append("missing %d (%s)" % (len(missing), ", ".join(missing[:3])))
            if extra:
                parts.append("extra %d (%s)" % (len(extra), ", ".join(extra[:3])))
            if duplicates:
                parts.append("duplicates %d (%s)" % (len(duplicates), ", ".join(duplicates[:3])))
            set_detail = "differs: " + "; ".join(parts)
        add(
            "instance set == the library's non-*_collision* set",
            "structure",
            "exact (%d)" % len(want),
            set_detail,
            exact,
        )
        existing = [rel for rel in rels if os.path.isfile(os.path.join(library, rel))]
        missing_files = sorted(set(rels) - set(existing))
        exists_detail = "%d/%d" % (len(existing), len(rels))
        if missing_files:
            exists_detail += " missing: %s" % ", ".join(missing_files[:3])
        add(
            "every glbRelPath exists under the library",
            "structure",
            "%d/%d" % (len(rels), len(rels)),
            exists_detail,
            len(existing) == len(rels),
        )
        identity = [
            1
            for _, v in instances
            if v[:3] == [0, 0, 0] and v[3:7] == [0, 0, 0, 1] and v[7:10] == [1, 1, 1]
        ]
        add(
            "identity transforms on every instance",
            "structure",
            "%d/%d" % (len(instances), len(instances)),
            "%d/%d" % (len(identity), len(instances)),
            len(identity) == len(instances),
        )
        add("exactly one entity", "structure", 1, len(entities), len(entities) == 1)
        add("exactly one spawn", "structure", 1, len(spawns), len(spawns) == 1)

        add(
            "header provenance placement=builder-pick",
            "provenance",
            ENCOUNTER_PLACEMENT,
            "present" if "placement=%s" % ENCOUNTER_PLACEMENT in text else "absent",
            "placement=%s" % ENCOUNTER_PLACEMENT in text,
        )
        add(
            "header provenance spawn=data-position+builder-yaw",
            "provenance",
            ENCOUNTER_SPAWN_PROVENANCE,
            "present" if "spawn=%s" % ENCOUNTER_SPAWN_PROVENANCE in text else "absent",
            "spawn=%s" % ENCOUNTER_SPAWN_PROVENANCE in text,
        )
        add(
            "header provenance entity clip=minotaur_a_stand",
            "provenance",
            ENCOUNTER_ENTITY_CLIP,
            "present" if "clip=%s" % ENCOUNTER_ENTITY_CLIP in text else "absent",
            "clip=%s" % ENCOUNTER_ENTITY_CLIP in text,
        )

        entity = entities[0] if entities else None
        entity_rel, entity_values, entity_clip = entity if entity is not None else ("", None, "")
        add(
            "entity glb",
            "structure",
            ENCOUNTER_ENTITY_GLB,
            entity_rel or "absent",
            entity_rel == ENCOUNTER_ENTITY_GLB,
        )
        entity_exists = entity is not None and os.path.isfile(
            os.path.join(library, entity_rel)
        )
        add(
            "entity glb exists under the library",
            "structure",
            "present",
            "present" if entity_exists else "missing",
            entity_exists,
        )
        add(
            "entity clip",
            "structure",
            ENCOUNTER_ENTITY_CLIP,
            entity_clip or "absent",
            entity_clip == ENCOUNTER_ENTITY_CLIP,
        )

        bowl = ENCOUNTER_BOWL_XZ
        spawn = spawns[0] if spawns else None

        def in_bowl(point):
            return bowl[0][0] <= point[0] <= bowl[0][1] and bowl[1][0] <= point[2] <= bowl[1][1]

        add(
            "spawn inside the arena bowl (x/z)",
            "structure",
            "inside",
            "inside" if spawn is not None and in_bowl(spawn) else
            ("outside (%s)" % ("%.3f %.3f" % (spawn[0], spawn[2]) if spawn else "absent")),
            spawn is not None and in_bowl(spawn),
        )
        add(
            "entity inside the arena bowl (x/z)",
            "structure",
            "inside",
            "inside" if entity is not None and in_bowl(entity_values) else
            ("outside (%s)" % ("%.3f %.3f" % (entity_values[0], entity_values[2])
                               if entity is not None else "absent")),
            entity is not None and in_bowl(entity_values),
        )

        ground_glb = os.path.join(library, ENCOUNTER_GROUND_GLB)
        measured_spawn_ground = None
        measured_entity_ground = None
        try:
            if spawn is not None:
                measured_spawn_ground = glb_ground_height(ground_glb, spawn[0], spawn[2])
            if entity is not None:
                measured_entity_ground = glb_ground_height(
                    ground_glb, entity_values[0], entity_values[2]
                )
        except GlbError as exc:
            add("spawn y == the sampled ground (%.3f)" % ENCOUNTER_SPAWN_GROUND_Y,
                "measured", "±0.001", "unparsed: %s" % exc, False)
            add("entity y == the sampled ground (%.3f)" % ENCOUNTER_ENTITY_GROUND_Y,
                "measured", "±0.001", "unparsed: %s" % exc, False)
        else:
            for label, point, measured, expected in (
                ("spawn", spawn, measured_spawn_ground, ENCOUNTER_SPAWN_GROUND_Y),
                ("entity", entity_values, measured_entity_ground, ENCOUNTER_ENTITY_GROUND_Y),
            ):
                if not point:
                    add(
                        "%s y == the sampled ground (%.3f)" % (label, expected),
                        "measured",
                        "±0.001",
                        "absent",
                        False,
                    )
                    continue
                ok = (
                    measured is not None
                    and abs(point[1] - measured) <= 0.001
                    and abs(measured - expected) <= 0.001
                )
                detail = (
                    "manifest %.3f, ground %s"
                    % (point[1], "no hit" if measured is None else "%.3f" % measured)
                )
                add(
                    "%s y == the sampled ground (%.3f)" % (label, expected),
                    "measured",
                    "±0.001",
                    detail,
                    ok,
                )

        try:
            entity_yaw = yaw_from_quaternion(entity_values) if entity is not None else None
        except ManifestError as exc:
            entity_yaw = None
            add("entity faces the spawn (quaternion yaw)", "measured", "±0.01 deg",
                "unparsed: %s" % exc, False)
        else:
            if entity is not None and spawn is not None:
                expected_yaw = yaw_between(
                    (entity_values[0], entity_values[2]), (spawn[0], spawn[2])
                )
                delta = abs((entity_yaw - expected_yaw + 180.0) % 360.0 - 180.0)
                add(
                    "entity faces the spawn (quaternion yaw)",
                    "measured",
                    "±0.01 deg",
                    "%.6f (expected %.6f, delta %.6f)" % (entity_yaw, expected_yaw, delta),
                    delta <= 0.01,
                )
            else:
                add("entity faces the spawn (quaternion yaw)", "measured", "±0.01 deg",
                    "absent", False)

        if entity is not None and spawn is not None:
            expected_yaw = yaw_between(
                (spawn[0], spawn[2]), (entity_values[0], entity_values[2])
            )
            delta = abs((spawn[3] - expected_yaw + 180.0) % 360.0 - 180.0)
            add(
                "spawn faces the entity (yaw)",
                "measured",
                "±0.01 deg",
                "%.6f (expected %.6f, delta %.6f)" % (spawn[3], expected_yaw, delta),
                delta <= 0.01,
            )
        else:
            add("spawn faces the entity (yaw)", "measured", "±0.01 deg", "absent", False)

        # The yawed bind footprint (the re4 note's bowl-margin check): rotate the
        # entity glb's local POSITION bounds by the placement yaw and translate.
        if entity is None or entity_yaw is None:
            add("entity yawed bind footprint inside the bowl", "measured", "inside",
                "absent", False)
            add("spawn outside the entity's yawed bind footprint", "structure",
                "outside", "absent", False)
        else:
            try:
                # Skinned model: its accessor bounds are already in the skin
                # space the app poses, so node transforms must not be required.
                local_lo, local_hi = glb_bounds(
                    os.path.join(library, ENCOUNTER_ENTITY_GLB), require_identity=False
                )
            except GlbError as exc:
                add("entity yawed bind footprint inside the bowl", "measured", "inside",
                    "unparsed: %s" % exc, False)
                add("spawn outside the entity's yawed bind footprint", "structure",
                    "outside", "unparsed: %s" % exc, False)
            else:
                radians = math.radians(entity_yaw)
                cos_yaw, sin_yaw = math.cos(radians), math.sin(radians)
                corners = []
                for lx in (local_lo[0], local_hi[0]):
                    for lz in (local_lo[2], local_hi[2]):
                        corners.append(
                            (
                                entity_values[0] + cos_yaw * lx + sin_yaw * lz,
                                entity_values[2] - sin_yaw * lx + cos_yaw * lz,
                            )
                        )
                footprint = (
                    (min(c[0] for c in corners), max(c[0] for c in corners)),
                    (min(c[1] for c in corners), max(c[1] for c in corners)),
                )
                inside = (
                    footprint[0][0] >= bowl[0][0]
                    and footprint[0][1] <= bowl[0][1]
                    and footprint[1][0] >= bowl[1][0]
                    and footprint[1][1] <= bowl[1][1]
                )
                add(
                    "entity yawed bind footprint inside the bowl",
                    "measured",
                    "inside",
                    "x[%.3f,%.3f] z[%.3f,%.3f]"
                    % (footprint[0][0], footprint[0][1], footprint[1][0], footprint[1][1]),
                    inside,
                )
                if spawn is not None:
                    outside = not (
                        footprint[0][0] <= spawn[0] <= footprint[0][1]
                        and footprint[1][0] <= spawn[2] <= footprint[1][1]
                    )
                    add(
                        "spawn outside the entity's yawed bind footprint",
                        "structure",
                        "outside",
                        "outside" if outside else "inside",
                        outside,
                    )
                else:
                    add("spawn outside the entity's yawed bind footprint", "structure",
                        "outside", "absent", False)
        if entity is not None and spawn is not None:
            distance = math.hypot(
                spawn[0] - entity_values[0], spawn[2] - entity_values[2]
            )
            add(
                "spawn-entity distance < 100 m",
                "measured",
                "< 100",
                "%.3f m" % distance,
                distance < 100.0,
            )
        else:
            add("spawn-entity distance < 100 m", "measured", "< 100", "absent", False)

    print("check-encounter: %s (library %s)" % (path, library))
    for check in checks:
        status = "PASS" if check.ok else "FAIL"
        print(
            "%-5s %-9s %-58s expected %s, measured %s"
            % (status, check.kind, check.name, check.expected, check.measured)
        )
    failed = [c for c in checks if not c.ok]
    print("summary: %d/%d gating assertions pass" % (len(checks) - len(failed), len(checks)))
    if failed:
        print(
            "gate: NOT MET — %d assertion(s) fail; re-measure and record "
            "(docs/notes/re4-placement.md §8)" % len(failed)
        )
        return 1
    print("gate: MET")
    return 0


# ------------------------------------------------------------------------ main


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="probe.py", description="RE probe for the stage-composition family (.fs + inventory)"
    )
    sub = parser.add_subparsers(dest="command")

    p_fs = sub.add_parser("fs", help="structure dump of a .fs")
    p_fs.add_argument("file")
    p_fs.add_argument("--json", metavar="OUT", help="also write the dump as JSON")
    p_fs.set_defaults(func=cmd_fs)

    p_class = sub.add_parser("classify", help="inventory of the composition families")
    p_class.set_defaults(func=cmd_classify)

    p_check = sub.add_parser("check-fs", help="pinned assertions for the stage .fs family")
    p_check.set_defaults(func=cmd_check_fs)

    p_cwks = sub.add_parser("cwks", help="structure dump of the workspace dependencies file")
    p_cwks.add_argument("file", nargs="?", help="defaults to workspaces/_ps4/shadow.cwks")
    p_cwks.add_argument("--json", metavar="OUT", help="also write the dump as JSON")
    p_cwks.add_argument(
        "--arena-out", metavar="OUT", help="write the pool path list matching --match (one path per line)"
    )
    p_cwks.add_argument(
        "--match",
        metavar="SUBSTR",
        default=None,
        help="pool path substring for the extracted list (default: --stage's cwks_match, %r)"
        % ARENA["cwks_match"],
    )
    p_cwks.add_argument(
        "--stage", choices=sorted(STAGES), default="arena_01", help="the stage whose match is the default"
    )
    p_cwks.set_defaults(func=cmd_cwks)

    p_check_cwks = sub.add_parser(
        "check-cwks", help="pinned assertions for the workspace dependencies file"
    )
    p_check_cwks.set_defaults(func=cmd_check_cwks)

    p_compose = sub.add_parser("compose", help="emit the arena scene manifest (the W1<->W2 format)")
    p_compose.add_argument(
        "--library", required=True, metavar="DIR", help="the converted glb tree (out/library)"
    )
    p_compose.add_argument(
        "--out", default=MANIFEST_OUT, metavar="FILE", help="manifest output (default %s)" % MANIFEST_OUT
    )
    p_compose.add_argument(
        "--paths",
        default=ARENA_PATHS_OUT,
        metavar="FILE",
        help="W2.2's arena path list to cross-check against (default %s)" % ARENA_PATHS_OUT,
    )
    p_compose.add_argument(
        "--stage", choices=sorted(STAGES), default="arena_01", help="the stage to compose (default arena_01)"
    )
    p_compose.set_defaults(func=cmd_compose)

    p_check_manifest = sub.add_parser(
        "check-manifest", help="pinned assertions for the arena manifest"
    )
    p_check_manifest.add_argument("file", nargs="?", default=MANIFEST_OUT,
                                  help="manifest (default %s)" % MANIFEST_OUT)
    p_check_manifest.add_argument(
        "--library", required=True, metavar="DIR", help="the converted glb tree (out/library)"
    )
    p_check_manifest.add_argument(
        "--stage", choices=sorted(STAGES), default="arena_01", help="the stage to check (default arena_01)"
    )
    p_check_manifest.set_defaults(func=cmd_check_manifest)

    p_compose_encounter = sub.add_parser(
        "compose-encounter",
        help="derive the S4 encounter manifest from the S3 arena manifest",
    )
    p_compose_encounter.add_argument(
        "--library", required=True, metavar="DIR", help="the converted glb tree (out/library)"
    )
    p_compose_encounter.add_argument(
        "--s3-manifest",
        default=MANIFEST_OUT,
        metavar="FILE",
        help="the arena manifest to derive from (default %s)" % MANIFEST_OUT,
    )
    p_compose_encounter.add_argument(
        "--out",
        default=ENCOUNTER_OUT,
        metavar="FILE",
        help="encounter manifest output (default %s)" % ENCOUNTER_OUT,
    )
    p_compose_encounter.set_defaults(func=cmd_compose_encounter)

    p_check_encounter = sub.add_parser(
        "check-encounter", help="pinned assertions for the S4 encounter manifest"
    )
    p_check_encounter.add_argument("file", nargs="?", default=ENCOUNTER_OUT,
                                   help="manifest (default %s)" % ENCOUNTER_OUT)
    p_check_encounter.add_argument(
        "--library", required=True, metavar="DIR", help="the converted glb tree (out/library)"
    )
    p_check_encounter.set_defaults(func=cmd_check_encounter)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
