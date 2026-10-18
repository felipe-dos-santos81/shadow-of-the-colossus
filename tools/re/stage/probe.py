#!/usr/bin/env python3
"""RE probe for SOTC's stage-composition family — the `.fs` files + the inventory.

The family (measured 2026-10-18, docs/notes/re-stage.md §1):
  cp3shadow/data/fs/stage/*.fs              1,896 stage segment lists
  cp3shadow/data/fs/stagetexseg_def/*.fs      112 texture-segment defs
  cp3shadow/data/fs/rootseg.fs                  1 root segment list
  workspaces/_ps4/shadow.cwks                   1 workspace string pool (§2)
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

Modes
  fs <file> [--json <out>]   structure dump of a .fs: header, entries, v histogram
  classify                   one line per family: counts, size ranges, name ranges
  check-fs                   pinned assertions for the stage .fs family (exit 0/1)

Inputs are read-only.  Files are resolved against the game tree ($SOTC_GAME_ROOT,
default below); a bare `.fs` name also resolves inside the family's directories,
so `fs 1010.fs` works from anywhere.
"""

import argparse
import glob
import json
import os
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


def fs_entries(path):
    """Parse the `.fs` layout; raise FsError with the measured numbers on mismatch."""
    data = read_bytes(path)
    size = len(data)
    if size < 4:
        raise FsError("shorter than the 4-byte header (%d bytes)" % size)
    n = u32le(data, 0)
    if size != 4 + 8 * n:
        raise FsError("size %d != 4 + 8*%d (= %d)" % (size, n, 4 + 8 * n))
    return size, n, [(u32le(data, 4 + 8 * i), u32le(data, 8 + 8 * i)) for i in range(n)]


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
        """(size, n, entries, error) for a pinned file — never dies on layout."""
        try:
            size, n, entries = fs_entries(os.path.join(root, rel))
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

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
