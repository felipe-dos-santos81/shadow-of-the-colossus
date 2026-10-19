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
  cwks <file> [--json <out>] [--arena-out <out>] [--match SUBSTR]
                             structure dump of the workspace file: header,
                             records, string pool; extracts the pool path list
                             matching SUBSTR (default: the arena match)
  check-cwks                 pinned assertions for the workspace file (exit 0/1)
  compose --library <dir> --out <file> [--paths <file>]
                             emit the arena scene manifest (the W1↔W2 format):
                             the level sets' non-*_collision* glbs, identity
                             transforms, the labeled spawn pick
  check-manifest [<file>] --library <dir>
                             pinned assertions for the arena manifest (exit 0/1)

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
CWKS_ARENA_MATCH = "levels/arena_01/"
CWKS_ARENA_COUNT = 142
CWKS_ARENA_SHA256 = "13e3bb60daf3dc91bbf36818b3bc6dd50dc59c27e9730f341d2c4b24d77e5706"
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

# The arena composition pins (W2.3; all re-measured 2026-10-18, docs/notes/re-stage.md §3).
# The two level sets (library-relative), the measured glb file counts, and the
# *_collision* subset: the non-*_collision* glbs are the manifest's instances
# (identity transforms — the glbs are already world-positioned); the collision
# glbs are collision geometry, not render dependencies (the workspace pool
# references their `.cclm` definitions, and the non-collision cmsh exactly — §3).
ARENA_SETS = (
    "models/shadow/levels/arena_01",
    "models/shadow/levels/arena_01_entrance",
)
ARENA_SET_FILES = (13, 22)        # glb files per set, before the collision filter
ARENA_SET_COLLISION = (1, 4)      # *_collision* glb files per set
ARENA_INSTANCES = 30              # 12 + 18 non-*_collision* glbs (35 - 5)
ARENA_LEVEL_GLB = "models/shadow/levels/arena_01/_cmn/arena_01.glb"
ENTRANCE_LEVEL_GLB = "models/shadow/levels/arena_01_entrance/_cmn/arena_01_entrance.glb"
COLLISION_MARK = "_collision"
MANIFEST_OUT = "out/re/stage/valus.manifest"
ARENA_PATHS_OUT = "out/re/stage/arena_paths.txt"
# The labeled spawn pick (§3): the entrance `_ground` mesh's footprint centre
# (309.3118, 285.4028) at its surface height (4.3940 m, ray-cast), facing +Z —
# the app's yaw 0 is +Z, toward the arena bowl; the sets meet near z≈300.  A
# builder pick, NOT data-derived (the `.path`/camdata candidates are in §3).
ARENA_SPAWN = (309.312, 4.394, 285.403, 0.0)
# The level glbs' bounds evidence (the plan's rounded numbers; check-manifest
# re-derives them from the accessor min/max and matches at this precision —
# x/z to integers, the entrance's y to 1 dp).
ARENA_BOUNDS_EVIDENCE = ("x[237,431]", "y[28,70]", "z[298,390]")
ENTRANCE_BOUNDS_EVIDENCE = ("x[259,345]", "y[-1.7,81.5]", "z[249,307]")


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


def arena_list(entries, match=CWKS_ARENA_MATCH):
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
    match = args.match
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
        add("arena list (match %r)" % CWKS_ARENA_MATCH, "structure",
            CWKS_ARENA_COUNT, len(arena), len(arena) == CWKS_ARENA_COUNT)
        add("arena list sha256", "structure", CWKS_ARENA_SHA256, arena_sha,
            arena_sha == CWKS_ARENA_SHA256)
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
    ("arena list (match %r)" % CWKS_ARENA_MATCH, "structure", CWKS_ARENA_COUNT),
    ("arena list sha256", "structure", CWKS_ARENA_SHA256),
    ("tail stage paths (13 verbatim from 0x%X)" % CWKS_TAIL_FIRST, "structure",
     "%d paths" % len(CWKS_TAIL_PATHS)),
]


# ------------------------------------------------------- manifest (W1↔W2) format
# W1's committed schema (docs/notes/scene-manifest.md): one directive per line;
# `#` starts a comment to end of line; blank lines are skipped; `instance
# <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>` (12 tokens)
# and `spawn <px> <py> <pz> <yawDeg>` (5); every number a full-string finite
# float (the app's parse_manifest refuses `nan`, `inf`, `1.5x`, `1e999`).

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


def parse_manifest_text(text, path):
    """Parse the W1↔W2 schema; return (instances, spawns) where instances =
    [(glbRelPath, [10 floats])] and spawns = [[4 floats]].  Raises
    ManifestError shaped `<path>:<line>: <reason>` (the app's message shape)."""
    instances = []
    spawns = []
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
    return instances, spawns


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
    for setrel in ARENA_SETS:
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


def fmt_counts(counts):
    return " + ".join("%d" % n for n in counts) + " = %d" % sum(counts)


class GlbError(Exception):
    """The file does not fit the glb container the bounds check needs."""


def glb_bounds(path):
    """The glb's bounds (lo, hi) as the union of every POSITION accessor's
    min/max.  Requires identity node transforms: the converter's output has
    none (checked), and a node transform would make the raw accessor bounds
    wrong — fail loudly instead of silently mis-measuring."""
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
    if sha != CWKS_ARENA_SHA256:
        die(
            "path list %s: sha256 %s != the pin %s (regenerate with cwks --arena-out)"
            % (list_path, sha, CWKS_ARENA_SHA256)
        )
    listed = [line for line in raw.decode("utf-8").splitlines() if line]

    parts = []
    for setrel in ARENA_SETS:
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

    sets = arena_sets(library)
    instances = [rel for _, set_instances, _ in sets for rel in set_instances]
    if len(instances) != ARENA_INSTANCES:
        die(
            "instance set is %d glbs, expected %d (%s files minus %s *_collision*)"
            % (
                len(instances),
                ARENA_INSTANCES,
                fmt_counts([len(i) + len(e) for _, i, e in sets]),
                fmt_counts([len(e) for _, _, e in sets]),
            )
        )

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
    lines = list(header)
    for rel in instances:
        lines.append("instance %s 0 0 0 0 0 0 1 1 1 1" % rel)
    lines.append("spawn %s" % " ".join(fmt_manifest_float(v) for v in ARENA_SPAWN))
    text = "\n".join(lines) + "\n"

    # Round-trip the text through the schema before writing: a path that the
    # format cannot carry (whitespace, `#`) must fail here, not in the app.
    parsed_instances, parsed_spawns = parse_manifest_text(text, args.out)
    if len(parsed_instances) != len(instances) or len(parsed_spawns) != 1:
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
            "compose: %-44s %d glb (%d instances, %d *_collision* excluded)"
            % (setrel, len(set_instances) + len(set_excluded), len(set_instances), len(set_excluded))
        )
    print("compose: cross-check %s (sha256 %s): %s" % (args.paths, CWKS_ARENA_SHA256[:8] + "…", summary))
    print(
        "compose: spawn %s (builder pick — docs/notes/re-stage.md §3)"
        % " ".join(fmt_manifest_float(v) for v in ARENA_SPAWN)
    )
    print(
        "compose: wrote %s (%d instances + 1 spawn + %d comment lines)"
        % (out, len(instances), len(header))
    )
    return 0


# ------------------------------------------------------------ check-manifest


def cmd_check_manifest(args):
    library = os.path.abspath(args.library)
    if not os.path.isdir(library):
        die("library not found: %s (the converted glb tree, e.g. out/library)" % library)
    path = os.path.abspath(args.file)
    checks = []

    def add(name, kind, expected, measured, ok):
        checks.append(Check(name, kind, expected, measured, ok))

    def fail_all(reason):
        for name, kind, expected in CHECK_MANIFEST_SPEC:
            add(name, kind, expected, "unparsed: %s" % reason, False)

    try:
        if not os.path.isfile(path):
            raise ManifestError("%s: cannot open manifest file" % path)
        raw = read_bytes(path)
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ManifestError("%s: not UTF-8 text (%s)" % (path, exc))
        instances, spawns = parse_manifest_text(text, path)
        error = None
    except ManifestError as exc:
        instances, spawns, error = None, None, str(exc)

    if error is not None:
        fail_all(error)
    else:
        sets = arena_sets(library)
        want = [rel for _, set_instances, _ in sets for rel in set_instances]
        files = [len(i) + len(e) for _, i, e in sets]
        collisions = [len(e) for _, _, e in sets]
        rels = [rel for rel, _ in instances]

        add(
            "manifest parses (W1↔W2 schema: comments, exact tokens, strict finite floats)",
            "structure",
            "ok",
            "ok",
            True,
        )
        add(
            "arena glb files (pre-filter)",
            "structure",
            fmt_counts(ARENA_SET_FILES),
            fmt_counts(files),
            tuple(files) == ARENA_SET_FILES,
        )
        add(
            "*_collision* glbs excluded",
            "structure",
            fmt_counts(ARENA_SET_COLLISION),
            fmt_counts(collisions),
            tuple(collisions) == ARENA_SET_COLLISION,
        )
        add(
            "manifest instances (non-*_collision*)",
            "structure",
            "%d (%d - %d)" % (ARENA_INSTANCES, sum(ARENA_SET_FILES), sum(ARENA_SET_COLLISION)),
            len(instances),
            len(instances) == ARENA_INSTANCES,
        )
        exact = sorted(set(rels)) == want and len(rels) == len(set(rels))
        add(
            "instance set == the library's non-*_collision* set",
            "structure",
            "exact (%d)" % len(want),
            "exact (%d)" % len(want)
            if exact
            else "differs (missing %d, extra %d, duplicates %d)"
            % (
                len(set(want) - set(rels)),
                len(set(rels) - set(want)),
                len(rels) - len(set(rels)),
            ),
            exact,
        )
        existing = [rel for rel in rels if os.path.isfile(os.path.join(library, rel))]
        add(
            "every glbRelPath exists under the library",
            "structure",
            "%d/%d" % (len(rels), len(rels)),
            "%d/%d" % (len(existing), len(rels)),
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
        for key, rel, evidence in (
            ("arena_01", ARENA_LEVEL_GLB, ARENA_BOUNDS_EVIDENCE),
            ("entrance", ENTRANCE_LEVEL_GLB, ENTRANCE_BOUNDS_EVIDENCE),
        ):
            name = "%s bounds evidence (plan %s)" % (key, " ".join(evidence))
            try:
                lo, hi = glb_bounds(os.path.join(library, rel))
            except GlbError as exc:
                add(name, "measured", " ".join(evidence), "unparsed: %s" % exc, False)
                continue
            measured = fmt_bounds(lo, hi, evidence)
            add(name, "measured", " ".join(evidence), measured, measured == " ".join(evidence))
            bounds[key] = (lo, hi)

        spawn = spawns[0] if spawns else None
        if bounds.get("entrance") is None or spawn is None:
            add(
                "spawn inside the entrance bounds",
                "structure",
                "inside " + " ".join(ENTRANCE_BOUNDS_EVIDENCE),
                "unparsed: %s" % ("no entrance bounds" if spawn is not None else "no spawn line"),
                False,
            )
        else:
            lo, hi = bounds["entrance"]
            inside = all(lo[i] <= spawn[i] <= hi[i] for i in range(3))
            add(
                "spawn inside the entrance bounds",
                "structure",
                "inside " + " ".join(ENTRANCE_BOUNDS_EVIDENCE),
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
CHECK_MANIFEST_SPEC = [
    ("manifest parses (W1↔W2 schema: comments, exact tokens, strict finite floats)",
     "structure", "ok"),
    ("arena glb files (pre-filter)", "structure", fmt_counts(ARENA_SET_FILES)),
    ("*_collision* glbs excluded", "structure", fmt_counts(ARENA_SET_COLLISION)),
    ("manifest instances (non-*_collision*)", "structure",
     "%d (%d - %d)" % (ARENA_INSTANCES, sum(ARENA_SET_FILES), sum(ARENA_SET_COLLISION))),
    ("instance set == the library's non-*_collision* set", "structure",
     "exact (%d)" % ARENA_INSTANCES),
    ("every glbRelPath exists under the library", "structure",
     "%d/%d" % (ARENA_INSTANCES, ARENA_INSTANCES)),
    ("identity transforms on every instance", "structure",
     "%d/%d" % (ARENA_INSTANCES, ARENA_INSTANCES)),
    ("exactly one spawn", "structure", 1),
    ("arena_01 bounds evidence (plan %s)" % " ".join(ARENA_BOUNDS_EVIDENCE), "measured",
     " ".join(ARENA_BOUNDS_EVIDENCE)),
    ("entrance bounds evidence (plan %s)" % " ".join(ENTRANCE_BOUNDS_EVIDENCE), "measured",
     " ".join(ENTRANCE_BOUNDS_EVIDENCE)),
    ("spawn inside the entrance bounds", "structure",
     "inside " + " ".join(ENTRANCE_BOUNDS_EVIDENCE)),
]


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
        default=CWKS_ARENA_MATCH,
        help="pool path substring for the extracted list (default %r)" % CWKS_ARENA_MATCH,
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
    p_compose.set_defaults(func=cmd_compose)

    p_check_manifest = sub.add_parser(
        "check-manifest", help="pinned assertions for the arena manifest"
    )
    p_check_manifest.add_argument("file", nargs="?", default=MANIFEST_OUT,
                                  help="manifest (default %s)" % MANIFEST_OUT)
    p_check_manifest.add_argument(
        "--library", required=True, metavar="DIR", help="the converted glb tree (out/library)"
    )
    p_check_manifest.set_defaults(func=cmd_check_manifest)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
