#!/usr/bin/env python3
"""RE probe for SOTC's BluePoint "xff" animation container family.

The family (measured 2026-10-18, docs/notes/re-anb.md):
  .skb  skeleton bank        "xff\\0" container, plaintext, carries a bone-name blob
  .anb  animation bank       "xff\\0" container, plaintext, no name blob
  .cani control-animation    "80AE" container, plaintext or Oodle-compressed,
                             ends with a 12-byte "MINA" v9 footer

Modes
  skb <file> [--json <out>]   structure dump of a .skb (header, tables, name blob)
  classify <pat>...           one line per file: magic, size field, 0x0C, comp, bank
  anb [<file>] [--json <out>] [--curves <out.json>]
                              .anb structure dump (header, tagged pointers, the two
                              tail tables, the record map) and an optional candidate
                              per-joint curve extraction (UNVERIFIED, see below)
  check-skb [options]         pinned assertions for cp3shadow/skel/player_a.skb
  check-curves [options]      the .anb gate's pinned assertions (exit 0/1)

The .anb curve extraction is a *candidate*, not a decode: the container is mapped
(docs/notes/re-anb.md §3) but the key encoding is not, so `--curves` marks its
output UNVERIFIED and `check-curves` fails (NOT MET) with the exact numbers.

Inputs are read-only.  Files are resolved against the game tree
($SOTC_GAME_ROOT, default below); patterns (shell-quoted, e.g. 'anim/*.anb')
are expanded against that tree too, so the probe can be run from the repo root.
"""

import argparse
import glob
import json
import math
import os
import re
import struct
import sys

DEFAULT_GAME_ROOT = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"

XFF_MAGIC = b"\x78\x66\x66\x00"  # "xff\0" — .anb / .skb / .nmo / .nto / .clb / .cls
CANI_MAGIC = b"80AE"  # .cani container (same little-endian fields as the xff family)
MINA_MAGIC = b"MINA"  # 12-byte footer magic (v9) at the end of a .cani

# Oodle block headers: first u16 (LE) -> flag base added to the decompressed size
# that is stored in the file's last 4 bytes.  Mirrors the vendored library's
# CompressionHandler (AquaModelLibrary.Data/BluePoint/CompressionHandler.cs).
OODLE_KINDS = {0x0A8C: 0x60000000, 0x0ACC: 0x60000000, 0x068C: 0x80000000, 0x06CC: 0x80000000}

# The xff container's string/data base: the .skb name blob and the tail-table
# offsets are relative to it (measured: constant 0x110 across all 57 .skb).
BLOB_BASE = 0x110
# Fields 0x20..0x4C that hold tagged pointers (tag == u16 @0x22), in file order.
POINTER_FIELDS = [0x20, 0x28, 0x2C, 0x30, 0x34, 0x3C, 0x44, 0x48]
# Tail-table entry tags.
TAG_RECORD = 0x102  # .skb: one entry per 0x40-byte bone record / .anb: descriptor field
TAG_OFFSET = 0x000

SKB_DEFAULT = "cp3shadow/skel/player_a.skb"
GLB_DEFAULT_REL = "out/library/models/shadow/characters/wander/_cmn/wander.glb"
SKB_NAME_COUNT = 348  # plan expectation: full skeleton (wander.cskl / wander.glb)
SKB_FIRST_NAME = "Bone_root"  # plan expectation

NAME_RE = re.compile(rb"[ -~]{1,}\x00")
NAME_PREFIXES = ("Bone_", "OptBone_")


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


def resolve(path):
    """Absolute paths are used as-is; anything else resolves against the game tree."""
    if os.path.isabs(path):
        return path
    return os.path.join(game_root(), path)


def expand(args, limit=None):
    """Expand shell-quoted patterns against the game tree; keep plain paths as given.

    `limit` caps the number of files taken from each pattern (sorted), which is how
    the .cani family gets sampled without truncating the .anb glob.
    """
    files = []
    for arg in args:
        if any(c in arg for c in "*?["):
            hits = sorted(glob.glob(resolve(arg), recursive=True))
            if not hits:
                die("pattern matched nothing: %s" % arg)
            files.extend(hits[:limit] if limit else hits)
        else:
            p = resolve(arg)
            if not os.path.isfile(p):
                die("file not found: %s" % arg)
            files.append(p)
    return files


def read_bytes(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        die("cannot read %s: %s" % (path, exc.strerror or exc))


# ------------------------------------------------------------------- primitives


def u16le(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32le(data, off):
    return struct.unpack_from("<I", data, off)[0]


def hexs(value, width=2):
    return ("%0" + str(width) + "X") % value


def ascii_of(raw):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in raw)


def oodle_info(data):
    """Return the Oodle block description if the file starts with one, else None."""
    if len(data) < 0x20:
        return None
    kind = u16le(data, 0)
    if kind not in OODLE_KINDS:
        return None
    flag = u32le(data, len(data) - 4)
    size = flag - OODLE_KINDS[kind]
    return {
        "kind": hexs(kind, 4),
        "flag_base": hexs(OODLE_KINDS[kind], 8),
        "raw_flag": hexs(flag, 8),
        "decompressed_size": size if 0 < size < 0x40000000 else None,
    }


def extract_names(data):
    """NUL-terminated printable strings that look like bone names, in file order."""
    names = []
    for match in NAME_RE.finditer(data):
        text = match.group()[:-1].decode("latin-1")
        if text.startswith(NAME_PREFIXES):
            names.append((match.start(), text))
    return names


def tail_tables(data):
    """Walk the container's tail tables (8-byte {u32 offset, u32 tag} entries).

    Entries are read back-to-front from EOF while the tag is 0x102 or 0x000.
    Returns (start, entries) with entries in file order.
    """
    if len(data) < 16:
        return None
    end = len(data)
    off = end - 8
    count = 0
    while off >= 0 and count < 65536:  # measured max: 4,517 entries (an .anb)
        if u32le(data, off + 4) not in (TAG_RECORD, TAG_OFFSET):
            break
        count += 1
        off -= 8
    if count == 0:
        return None
    start = end - 8 * count
    entries = [
        {"offset": u32le(data, start + 8 * i), "tag": u32le(data, start + 8 * i + 4)}
        for i in range(count)
    ]
    return start, entries


def xff_header(data):
    if not data.startswith(XFF_MAGIC):
        return None
    if len(data) < 0x70:
        die("xff container shorter than its 0x70-byte header (%d bytes)" % len(data))
    tag = u16le(data, 0x22)
    pointers = []
    for field in POINTER_FIELDS:
        value = u32le(data, field)
        pointers.append(
            {
                "field": field,
                "value": hexs(value, 8),
                "tag": hexs(value >> 16, 4),
                "tag_matches": (value >> 16) == tag,
                # Low 16 bits: exact for these 8 fields (their offsets are < 0x10000
                # in all 4,455 .anb); the general pointer form is (tag<<16)+offset,
                # so a >= 0x10000 offset would carry into the tag half — see §3.1.
                "offset": value & 0xFFFF,
            }
        )
    return {
        "magic": data[:4].hex(),
        "u32_0x04": u32le(data, 0x04),
        "u32_0x08": u32le(data, 0x08),
        "u16_0x0c": u16le(data, 0x0C),
        "u16_0x0e": u16le(data, 0x0E),
        "last_section_0x10": u32le(data, 0x10),
        "size_field_0x14": u32le(data, 0x14),
        "size_matches": u32le(data, 0x14) == len(data),
        "bytes_0x18": data[0x18:0x20].hex(),
        "ascii_0x18": ascii_of(data[0x18:0x20]),
        "u32_0x1c": u32le(data, 0x1C),
        "tag_0x22": hexs(tag, 4),
        "pointers": pointers,
        "small_ints": {
            "0x24": u32le(data, 0x24),
            "0x38": u32le(data, 0x38),
            "0x40": u32le(data, 0x40),
            "0x4c": u32le(data, 0x4C),
        },
        "offset_table_0x50": [u32le(data, 0x50 + 4 * i) for i in range(8)],
    }


def skb_records(data, entries, blob_start):
    """Split the tag-0x102 entries into bone records and trailing descriptors.

    Measured across all 57 .skb: the table starts with a run of 0x40-byte bone
    records (first u32 = blob-relative name offset, f14 = parent index) followed by
    up to three section descriptors whose first u32 is a section offset instead.
    """
    record_entries = []
    for entry in entries:
        if not record_entries:
            record_entries.append(entry)
        elif entry["offset"] == record_entries[0]["offset"] + 0x40 * len(record_entries):
            record_entries.append(entry)
        else:
            break
    descriptors = entries[len(record_entries):]
    records = []
    for entry in record_entries:
        off = blob_start + entry["offset"]
        if off + 0x40 > len(data):
            records.append({"offset": entry["offset"], "error": "out of range"})
            continue
        name_off = u32le(data, off)
        name = None
        name_at = blob_start + name_off
        if 0 <= name_at < len(data):
            end = data.find(b"\x00", name_at)
            if end > name_at:
                text = data[name_at:end].decode("latin-1")
                if text.startswith(NAME_PREFIXES):
                    name = text
        fields = [u32le(data, off + 4 * i) for i in range(16)]
        records.append(
            {
                "offset": entry["offset"],
                "file_offset": off,
                "name_offset": name_off,
                "name": name,
                "parent": signed(fields[14]),
                "index": signed(fields[12]),
                "quat0": [round_f(fields[4 + i]) for i in range(4)],
                "quat1": [round_f(fields[8 + i]) for i in range(4)],
                "raw": [hexs(f, 8) for f in fields],
            }
        )
    return records, [e["offset"] for e in descriptors]


def signed(value):
    return value - 0x100000000 if value >= 0x80000000 else value


def round_f(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


# ------------------------------------------------------------------ skb command


def parse_skb(path):
    data = read_bytes(path)
    if oodle_info(data):
        die("%s is Oodle-compressed; the probe reads the plaintext layout only" % path)
    header = xff_header(data)
    if header is None:
        die("%s is not an xff container (magic %s)" % (path, data[:4].hex()))
    names = extract_names(data)
    blob_start = names[0][0] if names else None
    tables = tail_tables(data)
    result = {
        "file": path,
        "size": len(data),
        "header": header,
        "name_blob": {
            "start": blob_start,
            "count": len(names),
            "names": [name for _, name in names],
        },
        "tail": None,
        "records": [],
    }
    if tables:
        start, entries = tables
        result["tail"] = {
            "start": start,
            "entries": entries,
            "record_entries": sum(1 for e in entries if e["tag"] == TAG_RECORD),
            "offset_entries": sum(1 for e in entries if e["tag"] == TAG_OFFSET),
        }
        if blob_start is not None:
            record_candidates = [e for e in entries if e["tag"] == TAG_RECORD]
            records, descriptors = skb_records(data, record_candidates, blob_start)
            result["records"] = records
            result["section_descriptors"] = descriptors
            for entry in entries:
                if entry["tag"] != TAG_OFFSET:
                    continue
                at = blob_start + entry["offset"]
                entry["resolves_to"] = None
                if 0 <= at < len(data):
                    end = data.find(b"\x00", at)
                    if end > at:
                        text = data[at:end].decode("latin-1")
                        if text.startswith(NAME_PREFIXES):
                            entry["resolves_to"] = text
    return result


def cmd_skb(args):
    info = parse_skb(resolve(args.file))
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(info, fh, indent=2, sort_keys=False)
            fh.write("\n")
        sys.stderr.write("probe: wrote %s\n" % out)

    header = info["header"]
    blob = info["name_blob"]
    out = sys.stdout
    out.write("skb %s (%d bytes)\n" % (info["file"], info["size"]))
    out.write("header\n")
    out.write('  0x00 magic             %s  ("xff\\0")\n' % header["magic"])
    out.write("  0x04 u32               %d\n" % header["u32_0x04"])
    out.write("  0x08 u32               %d\n" % header["u32_0x08"])
    out.write("  0x0C u16 variant       %d\n" % header["u16_0x0c"])
    out.write("  0x0E u16               %d\n" % header["u16_0x0e"])
    out.write(
        "  0x10 u32 last section  0x%X (%d)\n"
        % (header["last_section_0x10"], header["last_section_0x10"])
    )
    out.write(
        "  0x14 u32 file size     0x%X (%d)%s\n"
        % (
            header["size_field_0x14"],
            header["size_field_0x14"],
            "" if header["size_matches"] else "  != actual size",
        )
    )
    out.write(
        '  0x18 8 bytes           %s  ("%s")  [opaque]\n'
        % (header["bytes_0x18"], header["ascii_0x18"])
    )
    out.write("  0x1C u32               %d\n" % header["u32_0x1c"])
    out.write("  0x22 u16 tag           0x%s\n" % header["tag_0x22"])
    out.write("tagged pointers (value = (tag<<16) + offset)\n")
    for ptr in header["pointers"]:
        mark = "" if ptr["tag_matches"] else "  tag mismatch"
        out.write(
            "  0x%02X -> 0x%s  offset 0x%04X%s\n"
            % (ptr["field"], ptr["value"], ptr["offset"], mark)
        )
    out.write(
        "  0x24/0x38/0x40/0x4C small ints %s\n"
        % [header["small_ints"][k] for k in ("0x24", "0x38", "0x40", "0x4c")]
    )
    out.write(
        "  0x50..0x70 offsets     %s\n"
        % " ".join(hexs(v, 4) for v in header["offset_table_0x50"])
    )
    out.write("name blob\n")
    if blob["start"] is None:
        out.write("  none (no Bone_/OptBone_ strings)\n")
    else:
        out.write("  start 0x%X, %d names\n" % (blob["start"], blob["count"]))
        for i, name in enumerate(blob["names"]):
            out.write("    [%2d] %s\n" % (i, name))
    out.write("tail tables\n")
    if info["tail"] is None:
        out.write("  none\n")
    else:
        tail = info["tail"]
        out.write(
            "  start 0x%X: %d entries (%d tag=0x102, %d tag=0x000)\n"
            % (
                tail["start"],
                len(tail["entries"]),
                tail["record_entries"],
                tail["offset_entries"],
            )
        )
        for i, entry in enumerate(tail["entries"]):
            extra = ""
            if entry.get("resolves_to"):
                extra = ' -> "%s"' % entry["resolves_to"]
            out.write(
                "    [%2d] 0x%04X tag=0x%03X%s\n"
                % (i, entry["offset"], entry["tag"], extra)
            )
    out.write("bone records\n")
    if not info["records"]:
        out.write("  none\n")
    for rec in info["records"]:
        if rec.get("error"):
            out.write("  @0x%04X %s\n" % (rec["offset"], rec["error"]))
            continue
        out.write(
            "  @0x%04X name=%-22s parent=%-4s index=%-4s q1=%s q2=%s\n"
            % (
                rec["offset"],
                rec["name"] or "(unresolved)",
                rec["parent"],
                rec["index"],
                compact_floats(rec["quat0"]),
                compact_floats(rec["quat1"]),
            )
        )


def compact_floats(values):
    return "(" + ", ".join("%.4g" % v for v in values) + ")"


# ------------------------------------------------------------- classify command


def classify_one(path):
    data = read_bytes(path)
    result = {
        "file": path,
        "size": len(data),
        "magic": data[:4].hex(),
        "magic_ascii": ascii_of(data[:4]),
        "compression": "plain",
        "size_field": None,
        "size_field_note": "",
        "field_0x0c": None,
        "field_0x0c_note": "",
        "bank": "other",
        "extra": {},
    }
    oodle = oodle_info(data)
    if oodle:
        result["compression"] = "oodle-" + oodle["kind"]
        result["extra"]["oodle"] = oodle
        # The plaintext header fields are meaningless inside an Oodle block; report
        # them raw and marked, so the "garbage header" class is visible.
        if len(data) >= 0x20:
            result["size_field"] = u32le(data, 0x14)
            result["size_field_note"] = "garbage"
            result["field_0x0c"] = u32le(data, 0x0C)
            result["field_0x0c_note"] = "garbage"
        footer = data[-12:]
        if footer[:4] == MINA_MAGIC:
            result["bank"] = "cani"
            result["extra"]["footer"] = "MINA.v%d" % footer[4]
        return result

    if data.startswith(XFF_MAGIC):
        header = xff_header(data)
        result["size_field"] = header["size_field_0x14"]
        if not header["size_matches"]:
            result["size_field_note"] = "!= file size"
        result["field_0x0c"] = header["u16_0x0c"]
        names = extract_names(data)
        if names:
            result["bank"] = "skb"
            result["extra"]["names"] = len(names)
        elif header["u16_0x0c"] == 0:
            result["bank"] = "anb-v0"
        else:
            result["bank"] = "anb"
        return result

    if data.startswith(CANI_MAGIC):
        result["bank"] = "cani"
        footer = data[-12:]
        if footer[:4] == MINA_MAGIC:
            result["size_field"] = u32le(footer, 8)
            result["extra"]["footer"] = "MINA.v%d" % footer[4]
        if len(data) >= 0x10:
            result["field_0x0c"] = u16le(data, 0x0C)
            result["extra"]["fps"] = round_f(u32le(data, 0x08))
            result["extra"]["u16_0x0e"] = u16le(data, 0x0E)
        return result

    return result


def classify_line(entry):
    size = entry["size_field"]
    size_text = "none" if size is None else hexs(size, 4)
    if entry["size_field_note"]:
        size_text += "(%s)" % entry["size_field_note"]
    field = entry["field_0x0c"]
    field_text = "none" if field is None else hexs(field, 4)
    if entry["field_0x0c_note"]:
        field_text += "(%s)" % entry["field_0x0c_note"]
    line = "%s magic=%s size=%s 0x0C=%s comp=%s bank=%s" % (
        entry["file"],
        entry["magic"],
        size_text,
        field_text,
        entry["compression"],
        entry["bank"],
    )
    extra = entry["extra"]
    if "names" in extra:
        line += " names=%d" % extra["names"]
    if "footer" in extra:
        line += " footer=%s" % extra["footer"]
    if "oodle" in extra and extra["oodle"]["decompressed_size"]:
        line += " decomp=%d" % extra["oodle"]["decompressed_size"]
    if "fps" in extra:
        line += " fps=%g joints=%d" % (extra["fps"], extra["u16_0x0e"])
    return line


def cmd_classify(args):
    files = expand(args.patterns, limit=args.limit)
    if not files:
        die("nothing to classify")
    banks = {}
    comps = {}
    entries = []
    for path in files:
        entry = classify_one(path)
        entries.append(entry)
        banks[entry["bank"]] = banks.get(entry["bank"], 0) + 1
        comps[entry["compression"]] = comps.get(entry["compression"], 0) + 1
        print(classify_line(entry))
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(entries, fh, indent=2)
            fh.write("\n")
    sys.stderr.write(
        "probe: %d files; banks=%s; comp=%s\n"
        % (
            len(files),
            ", ".join("%s=%d" % kv for kv in sorted(banks.items())),
            ", ".join("%s=%d" % kv for kv in sorted(comps.items())),
        )
    )
    return 0


# ---------------------------------------------------------------- anb command
#
# The .anb layout, measured (docs/notes/re-anb.md §3):
#   0x00..0x110  header: 8 tagged pointers, small ints, and the descriptor region
#                (the fields B1 left opaque: 0xA0 data size, 0xB4 data offset,
#                0xC0..0xD8 the two table descriptors, 0x70..0x110 records)
#   0x110..      the data region (size 0xA0; the sibling .cani's data lives here too)
#   t1p..        table 1: `count` 8-byte {u32 offset, u32 tag=0x102} entries
#   t2p..EOF     table 2: `count` 8-byte {u32 offset, u32 tag=0x000} entries
# Both tables have the same `count` (u32 @0xC4); t1p = u32 @0xCC's low half,
# t2p = u32 @0xD0's; t1 ends exactly where t2 starts and t2 ends at EOF.

ANB_DEFAULT = "cp3shadow/anim/player_a_stand.anb"  # the gate file
# Second sample used in docs/notes/re-anb.md §3: cp3shadow/anim/horse_a_add_horse_kick.anb
# The character's joint count for the cross-check: a plaintext sibling .cani of
# the same character (u16 @0x0E == 348 for every wander clip, §1.2).  The
# compressed sibling player_a_stand.cani carries no readable header.
CANI_SIBLING_DEFAULT = (
    "models/shadow/characters/wander/animation/_cmn/player_a_add_horse_kick.cani"
)
# The .cani-family frame rate: the float at .cani 0x08 is bits 0x41EFFFFF in every
# plaintext file (measured, §1.2) = 29.999998.  The .anb stores no time fields
# (measured, §3.4), so the candidate extraction derives uniform key times from it.
FAMILY_FPS_BITS = 0x41EFFFFF


def anb_header(data):
    """The .anb reading of the shared xff header (adds the fields B1 left opaque)."""
    base = xff_header(data)
    base.update(
        {
            "data_size_0xa0": u32le(data, 0xA0),
            "align_0xa4": u32le(data, 0xA4),
            "count_0xc0": u32le(data, 0xC0),
            "table_count_0xc4": u32le(data, 0xC4),
            "count_0xc8": u32le(data, 0xC8),
            "table1_ptr_0xcc": u32le(data, 0xCC),
            "table2_ptr_0xd0": u32le(data, 0xD0),
            "table1_off_0xd4": u32le(data, 0xD4),
            "table2_off_0xd8": u32le(data, 0xD8),
            "data_offset_0xb4": u32le(data, 0xB4),
        }
    )
    return base


def anb_tables(data):
    """Read the two tail tables (exact counts, robust to offsets >= 0x10000).

    Measured on all 4,455 `.anb`: both tables hold `count` = u32@0xC4 entries of
    8-byte {u32 offset, u32 tag}; table 1 (tags all 0x102) starts at the offset in
    u32@0xD4, table 2 (tags all 0x000) at u32@0xD8's, they are *contiguous*
    (t2p == t1p + 8*count) and end exactly at EOF — 4,455/4,455 each.

    **The tagged pointer is `(tag<<16) + offset` — addition with carry**, not
    `tag<<16 | offset16`: `u32@0xCC == (tag<<16) + u32@0xD4` and
    `u32@0xD0 == (tag<<16) + u32@0xD8` — 4,455/4,455.  Offsets >= 0x10000
    therefore carry into the tag half (83 files for table 1, deltas
    0x10000-0xB0000; 86 for table 2), so the low 16 bits read here are a
    *truncation*.  The untagged mirrors @0xD4/@0xD8 hold the exact offsets; this
    function keeps the low-half read (B1's shape) and falls back to the
    walk-back-from-EOF rule *with the exact count*, which recovers the true
    tables in every case (walking back stops at the first non-{0x102,0x000} tag;
    the last 2*count entries are [table 1][table 2], the walk's extra leading
    entries being the data region's trailing zero bytes, exactly as B1 saw).
    """
    count = u32le(data, 0xC4)
    size = len(data)
    t1p = u32le(data, 0xCC) & 0xFFFF  # truncates offsets >= 0x10000 (see docstring)
    t2p = u32le(data, 0xD0) & 0xFFFF
    header_t2p = t2p
    if t2p != t1p + 8 * count:
        # Measured: contiguity holds for all 4,455 files (t2p == t1p + 8*count,
        # and @0xD8 == @0xD4 + 8*count); a truncated low half breaks it, so
        # contiguity restores the true table-2 offset.
        t2p = t1p + 8 * count

    def read(p):
        entries = []
        for i in range(count):
            if p + 8 * i + 8 > size:
                break
            entries.append(
                {"offset": u32le(data, p + 8 * i), "tag": u32le(data, p + 8 * i + 4)}
            )
        return entries

    def plausible(entries):
        return bool(entries) and all(
            e["offset"] < size and e["tag"] in (TAG_RECORD, TAG_OFFSET)
            for e in entries
        )

    method = "header-pointers"
    t1, t2 = read(t1p), read(t2p)
    if not (t1p + 8 * count == t2p and plausible(t1) and plausible(t2)):
        # Variant fallback: walk back from EOF while the tag is a table tag, then
        # take the last 2*count entries as [table 1][table 2].
        off = size - 8
        walked = 0
        while off >= 0 and walked < 65536:
            if u32le(data, off + 4) not in (TAG_RECORD, TAG_OFFSET):
                break
            walked += 1
            off -= 8
        if walked >= 2 * count:
            start = size - 8 * walked
            t1 = read(start + 8 * (walked - 2 * count))
            t2 = read(start + 8 * (walked - count))
            t1p = start + 8 * (walked - 2 * count)
            t2p = start + 8 * (walked - count)
            method = "walk-back"
    return {
        "count": count,
        "method": method,
        "table1": {"start": t1p, "entries": t1},
        "table2": {"start": t2p, "entries": t2},
        "tags_table1": sorted({hexs(e["tag"], 3) for e in t1}),
        "tags_table2": sorted({hexs(e["tag"], 3) for e in t2}),
        "header_t2_agrees": header_t2p == t2p,
        "contiguous": t1p + 8 * count == t2p,
        "ends_at_eof": t2p + 8 * count == size,
        # Measured pairing: t2[i] = t1[i-1] + 4 for the leading run (§3.2).
        "t2_shift_matches": sum(
            1
            for i in range(1, min(len(t1), len(t2)))
            if t2[i]["offset"] == t1[i - 1]["offset"] + 4
        ),
    }


def unit_float_quats(data, start, end):
    """Float32 4-tuples (16-byte stride, found by scan) whose norm is 1 ± 1e-3."""
    found = []
    off = start
    while off + 16 <= end:
        v = struct.unpack_from("<4f", data, off)
        if all(x == x and abs(x) <= 1.5 for x in v):
            n2 = sum(x * x for x in v)
            if abs(n2 - 1.0) < 2e-3:
                found.append(off)
                off += 16
                continue
        off += 4
    return found


def longest_const_run(data, start, end):
    """Longest run of identical 4-byte words (a static/stub track marker)."""
    best = 0
    run = 1
    prev = None
    for off in range(start, end - 3, 4):
        word = data[off : off + 4]
        if word == prev:
            run += 1
            best = max(best, run)
        else:
            run = 1
        prev = word
    return best


def triplet_smoothness(data, start, end):
    """Mean |delta| / range of the stride-6 int16 channel (the triplet reading).

    The .anb payload is dominated by 16-bit streams; the stride-6 reading is the
    one hypothesis that makes some records continuous (§3.5).  This measure is
    hypothesis-specific evidence, not semantics.
    """
    vals = []
    o = start
    while o + 2 <= end:
        vals.append(struct.unpack_from("<h", data, o)[0])
        o += 6
    if len(vals) < 4:
        return None
    rng = max(vals) - min(vals)
    if rng == 0:
        return 0.0
    dv = [abs(vals[i + 1] - vals[i]) for i in range(len(vals) - 1)]
    return round(sum(dv) / len(dv) / rng, 4)


def group_smoothness(data, start, end):
    """Best (min) byte-plane smoothness at stride 4 (the 4-byte-group reading).

    Records end in blocks where three of the four stride-4 byte planes vary
    smoothly and the fourth is low-order noise (§3.3); this is the best plane.
    """
    best = None
    for p in range(4):
        vals = [data[o] for o in range(start + p, end, 4)]
        if len(vals) < 4:
            continue
        dv = [abs(vals[i + 1] - vals[i]) for i in range(len(vals) - 1)]
        s = sum(dv) / len(dv) / 255.0
        best = s if best is None else min(best, s)
    return round(best, 4) if best is not None else None


def anb_records(data, tables):
    """The t1 records: offset, size (up to the next entry / table start) + flags.

    Flags are measured content classes, not decoded semantics: `float_quats`
    counts verified unit float32 quaternions inside the record, `const_run` the
    longest run of identical 4-byte words, `triplet_smooth`/`group_smooth` the
    relative smoothness under the two payload readings (lower = smoother).
    """
    t1 = [e["offset"] for e in tables["table1"]["entries"]]
    t1p = tables["table1"]["start"]
    records = []
    for i, off in enumerate(t1):
        end = t1[i + 1] if i + 1 < len(t1) else t1p
        rec = {"index": i, "offset": off, "size": end - off}
        if 0 <= off < end <= len(data):
            quats = unit_float_quats(data, off, end)
            rec["float_quats"] = len(quats)
            rec["float_quat_offsets"] = quats
            rec["const_run"] = longest_const_run(data, off, end)
            rec["triplet_smooth"] = triplet_smoothness(data, off, end)
            rec["group_smooth"] = group_smoothness(data, off, end)
        else:
            rec["error"] = "out of range"
        records.append(rec)
    return records


def candidate_curves(data, records, fps):
    """The candidate decode: int16 (x, y, z) Q15 triplets from each record start.

    This is the only reading found that yields *continuous* rotations in some
    records, but it is NOT verified (§3.5): it fails the gate's own sanity checks
    on player_a_stand, and continuity alone cannot verify it (any smooth triplet
    stream yields continuous derived-w quaternions).  Keys whose vector part has
    |v| > 1 keep w = 0 and are counted — never silently normalized.
    """
    joints = []
    for rec in records:
        off, size = rec["offset"], rec["size"]
        nkeys = max(0, size // 6)
        times, rotations, over = [], [], 0
        for k in range(nkeys):
            x, y, z = struct.unpack_from("<3h", data, off + 6 * k)
            v = (x / 32768.0, y / 32768.0, z / 32768.0)
            n2 = sum(c * c for c in v)
            if n2 <= 1.0:
                w = math.sqrt(1.0 - n2)
            else:
                w = 0.0
                over += 1
            times.append(k / fps)
            rotations.append([v[0], v[1], v[2], w])
        joints.append(
            {
                "index": rec["index"],
                "record_offset": off,
                "record_size": size,
                "keys": nkeys,
                "times": times,
                "rotations": rotations,
                "translations": [],
                "over_unity_keys": over,
            }
        )
    return joints


def curve_sanity(joints):
    """The gate's sanity numbers over a candidate extraction."""
    norms, dots, nan = [], [], 0
    for j in joints:
        for r in j["rotations"]:
            if any(x != x for x in r):
                nan += 1
                continue
            norms.append(abs(math.sqrt(sum(x * x for x in r)) - 1.0))
        for a, b in zip(j["rotations"], j["rotations"][1:]):
            dots.append(abs(sum(x * y for x, y in zip(a, b))))
    return {
        "keys": sum(j["keys"] for j in joints),
        "nan_quats": nan,
        "max_norm_error": max(norms) if norms else None,
        "min_adjacent_dot": min(dots) if dots else None,
        "adjacent_pairs": len(dots),
        "pairs_below_0_9": sum(1 for d in dots if d < 0.9),
        "over_unity_keys": sum(j["over_unity_keys"] for j in joints),
    }


def anb_info(path):
    """Full structure dump of one .anb, as a dict (shared by anb/check-curves)."""
    data = read_bytes(path)
    if oodle_info(data):
        die("%s is Oodle-compressed; the probe reads the plaintext layout only" % path)
    header = anb_header(data)
    if header is None:
        die("%s is not an xff container (magic %s)" % (path, data[:4].hex()))
    tables = anb_tables(data)
    records = anb_records(data, tables)
    return {
        "file": path,
        "size": len(data),
        "header": header,
        "tables": tables,
        "records": records,
        "fps": struct.unpack("<f", struct.pack("<I", FAMILY_FPS_BITS))[0],
    }


def cmd_anb(args):
    info = anb_info(resolve(args.file))
    data = read_bytes(info["file"])
    header, tables, records = info["header"], info["tables"], info["records"]

    curves_path = None
    if args.curves:
        joints = candidate_curves(data, records, info["fps"])
        sanity = curve_sanity(joints)
        payload = {
            "clip": os.path.splitext(os.path.basename(info["file"]))[0],
            "fps": info["fps"],
            "joints": joints,
            "source": info["file"],
            "decode": (
                "candidate: int16 (x, y, z) Q15 triplets at each record start, "
                "w = sqrt(1 - |v|^2), uniform key times — UNVERIFIED (§3.5)"
            ),
            "sanity": sanity,
        }
        curves_path = os.path.abspath(args.curves)
        os.makedirs(os.path.dirname(curves_path) or ".", exist_ok=True)
        with open(curves_path, "w") as fh:
            json.dump(payload, fh, indent=2)
            fh.write("\n")
        sys.stderr.write("probe: wrote %s\n" % curves_path)
        sys.stderr.write(
            "probe: UNVERIFIED candidate decode — %d keys, max norm error %s, "
            "min adjacent dot %s, %d/%d adjacent pairs below 0.9\n"
            % (
                sanity["keys"],
                "%.3g" % sanity["max_norm_error"],
                "%.3g" % sanity["min_adjacent_dot"],
                sanity["pairs_below_0_9"],
                sanity["adjacent_pairs"],
            )
        )

    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(info, fh, indent=2)
            fh.write("\n")
        sys.stderr.write("probe: wrote %s\n" % out)

    out = sys.stdout
    out.write("anb %s (%d bytes)\n" % (info["file"], info["size"]))
    out.write("header\n")
    out.write('  0x00 magic             %s  ("xff\\0")\n' % header["magic"])
    out.write("  0x0C u16 variant       %d\n" % header["u16_0x0c"])
    out.write(
        "  0x10 u32 last section  0x%X   (B1 scope note: in the character samples this\n"
        "                              is a block boundary *inside* the data region, not\n"
        "                              a separate tail; objanim_* samples can point into\n"
        "                              the header instead — see docs/notes §3.1)\n"
        % header["last_section_0x10"]
    )
    out.write(
        "  0x14 u32 file size     0x%X%s\n"
        % (
            header["size_field_0x14"],
            "" if header["size_matches"] else "  != actual size",
        )
    )
    out.write('  0x18 8 bytes           "%s"  [opaque, unchanged from B1]\n' % header["ascii_0x18"])
    out.write("  0x22 u16 tag           0x%s\n" % header["tag_0x22"])
    out.write("  0x90 descriptor        data offset 0x%X, data size %d (0x%X)\n" % (
        header["data_offset_0xb4"], header["data_size_0xa0"], header["data_size_0xa0"]))
    out.write("  0xB0 table descriptor  count %d, table1 ptr 0x%08X, table2 ptr 0x%08X\n" % (
        header["table_count_0xc4"], header["table1_ptr_0xcc"], header["table2_ptr_0xd0"]))
    out.write("  untagged mirrors       table1 @0xD4 0x%X, table2 @0xD8 0x%X "
              "(the exact offsets; ptr == (tag<<16) + offset)\n" % (
        header["table1_off_0xd4"], header["table2_off_0xd8"]))
    out.write("tagged pointers (value = (tag<<16) + offset)\n")
    for ptr in header["pointers"]:
        mark = "" if ptr["tag_matches"] else "  tag mismatch"
        out.write(
            "  0x%02X -> 0x%s  offset 0x%04X%s\n"
            % (ptr["field"], ptr["value"], ptr["offset"], mark)
        )
    out.write("  0x9C -> 0x%08X  (data start)\n" % u32le(data, 0x9C))
    out.write("tail tables (exact; both have `count` = u32 @0xC4 entries)\n")
    out.write(
        "  location: %s%s\n"
        % (
            tables["method"],
            "  (the low-half read truncates offsets >= 0x10000: the tagged\n"
            "   pointer is (tag<<16) + offset (carry), and @0xD4/@0xD8 hold the\n"
            "   exact offsets.  The walk-back-from-EOF + count rule recovers the\n"
            "   true tables here — a cross-check, not a writer quirk; see §3.1)"
            if tables["method"] != "header-pointers"
            else "",
        )
    )
    out.write(
        "  table1 @0x%04X: %d entries, tags %s\n"
        % (tables["table1"]["start"], len(tables["table1"]["entries"]),
           ",".join(tables["tags_table1"]))
    )
    out.write(
        "  table2 @0x%04X: %d entries, tags %s\n"
        % (tables["table2"]["start"], len(tables["table2"]["entries"]),
           ",".join(tables["tags_table2"]))
    )
    out.write(
        "  contiguous %s, ends at EOF %s, t2[i] == t1[i-1]+4 for %d/%d leading entries\n"
        % (tables["contiguous"], tables["ends_at_eof"], tables["t2_shift_matches"],
           max(0, len(tables["table1"]["entries"]) - 1))
    )
    out.write("records (t1 offsets; size = up to the next entry)\n")
    out.write(
        "  %-4s %-6s %-6s %-6s %-6s %-8s %s\n"
        % ("idx", "offset", "size", "quats", "const", "triplet", "group")
    )
    for rec in records:
        if rec.get("error"):
            out.write("  %-4d 0x%04X  %s\n" % (rec["index"], rec["offset"], rec["error"]))
            continue
        out.write(
            "  %-4d 0x%04X %-6d %-6d %-6d %-8s %s\n"
            % (
                rec["index"],
                rec["offset"],
                rec["size"],
                rec.get("float_quats", 0),
                rec.get("const_run", 0),
                rec.get("triplet_smooth", "-"),
                rec.get("group_smooth", "-"),
            )
        )
    quat_records = [r["index"] for r in records if r.get("float_quats")]
    out.write(
        "summary: %d records; unit float32 quaternions in records %s; "
        "%d records with a >=8-word constant run\n"
        % (
            len(records),
            quat_records or "none",
            sum(1 for r in records if r.get("const_run", 0) >= 8),
        )
    )
    if curves_path:
        out.write("curves (candidate, UNVERIFIED)\n  wrote %s\n" % curves_path)
    return 0


# ---------------------------------------------------------- check-curves command


def cani_is_plaintext(path):
    """True when a `.cani` starts with the plaintext magic (no Oodle block)."""
    try:
        with open(path, "rb") as fh:
            head = fh.read(4)
    except OSError:
        return False
    return head[:4] == CANI_MAGIC


def find_sibling_cani(path, explicit):
    """A *plaintext* `.cani` of the same character, for the joint-count cross-check.

    Exact stem first (`horse_a_add_horse_kick.anb` -> `horse_a_add_horse_kick.cani`,
    which exists under `characters/agro/`), else any plaintext `.cani` of the same
    character directory (the joint count is a character property: 348 for every
    wander clip, 132 for the horse, §1.2) — the gate file's own `player_a_stand.cani`
    is Oodle-compressed and has no readable header.  Falls back to the pinned
    wander sample when nothing matches.
    """
    if explicit:
        return resolve(explicit), None
    clip = os.path.splitext(os.path.basename(path))[0]
    root = resolve(os.path.join("models", "shadow", "characters"))
    exact_dir = None
    for d in sorted(glob.glob(os.path.join(root, "*", "animation", "_cmn"))):
        if clip + ".cani" in os.listdir(d):
            exact_dir = d
            exact = os.path.join(d, clip + ".cani")
            if cani_is_plaintext(exact):
                return exact, None
    if exact_dir:
        for name in sorted(os.listdir(exact_dir)):
            if name.endswith(".cani"):
                p = os.path.join(exact_dir, name)
                if cani_is_plaintext(p):
                    return p, "same character, different clip"
    return resolve(CANI_SIBLING_DEFAULT), "pinned wander sample"


def cani_joint_count(path):
    """The u16 @0x0E of a plaintext .cani (the full skeleton's joint count)."""
    if not path or not os.path.isfile(path):
        return None
    data = read_bytes(path)
    if not data.startswith(CANI_MAGIC):
        return None
    if oodle_info(data):
        return None  # compressed: no readable header (B1 §1.2)
    return u16le(data, 0x0E)


def find_sibling_skb(path):
    """The skeleton bank for a clip: the longest skb stem that prefixes the clip's.

    Measured naming rule (B1 §2.2): clips are named `<bank>_<state>` and the bank
    lives in the sibling `../skel/` directory (`player_a_stand.anb` -> `player_a.skb`).
    """
    clip = os.path.splitext(os.path.basename(path))[0]
    skel_dir = os.path.join(os.path.dirname(os.path.dirname(path)), "skel")
    if not os.path.isdir(skel_dir):
        return None
    best = None
    for name in sorted(os.listdir(skel_dir)):
        if not name.endswith(".skb"):
            continue
        stem = name[:-4]
        if clip == stem or clip.startswith(stem + "_"):
            if best is None or len(stem) > len(os.path.splitext(best)[0]):
                best = name
    return os.path.join(skel_dir, best) if best else None


def cmd_check_curves(args):
    path = resolve(args.file)
    if not os.path.isfile(path):
        die("file not found: %s" % args.file)
    info = anb_info(path)
    data = read_bytes(path)
    header, tables, records = info["header"], info["tables"], info["records"]

    skb = find_sibling_skb(path)
    skb_names = None
    if skb and os.path.isfile(skb):
        skb_names = len(extract_names(read_bytes(skb)))
    cani_path, cani_note = find_sibling_cani(path, args.cani)
    cani_joints = cani_joint_count(cani_path)

    checks = []

    def add(name, kind, expected, measured, ok, skip=False, reason=None):
        check = Check(name, kind, expected, measured, ok)
        check.skipped = skip
        check.reason = reason
        checks.append(check)

    # --- structure (the container decode: these are verifiable today) ---------
    add("magic is 78 66 66 00", "structure", "78666600", header["magic"],
        header["magic"] == "78666600")
    add("size field @0x14 == file size", "structure", info["size"],
        header["size_field_0x14"], header["size_matches"])
    add("data size @0xA0 == table1 start - data offset", "structure",
        header["data_size_0xa0"],
        (tables["table1"]["start"] - header["data_offset_0xb4"]),
        header["data_size_0xa0"] == tables["table1"]["start"] - header["data_offset_0xb4"])
    add("table count @0xC4 matches both tables", "structure", tables["count"],
        [len(tables["table1"]["entries"]), len(tables["table2"]["entries"])],
        tables["count"] == len(tables["table1"]["entries"]) == len(tables["table2"]["entries"]))
    variant = tables["method"] != "header-pointers"
    add("table location method", "finding", "header-pointers", tables["method"],
        not variant)
    add("tables are contiguous and end at EOF", "structure", True,
        (tables["contiguous"], tables["ends_at_eof"]),
        tables["contiguous"] and tables["ends_at_eof"], skip=variant,
        reason="offset >= 0x10000: located via the walk-back cross-check")
    add("table1 tags are all 0x102", "structure", "0x102", tables["tags_table1"],
        tables["tags_table1"] == ["102"])
    add("table2 tags are all 0x000", "structure", "0x000", tables["tags_table2"],
        tables["tags_table2"] == ["000"])
    add("records are in range and sized", "structure", ">0", len(records),
        bool(records) and all(not r.get("error") for r in records))
    add("t2[i] == t1[i-1] + 4 (leading run)", "structure", "first 4 entries",
        "%d/%d entries" % (tables["t2_shift_matches"], max(0, tables["count"] - 1)),
        tables["t2_shift_matches"] >= min(4, tables["count"] - 1))

    # --- joint-count cross-check (pinned to what the data shows) --------------
    record_count = len(records)
    add("clip track count == declared count @0xC4", "joint-count",
        tables["count"], record_count, record_count == tables["count"])
    if cani_joints is not None:
        add("tracks <= full skeleton joints (sibling .cani)", "joint-count",
            "<=%d" % cani_joints, record_count, record_count <= cani_joints)
    else:
        add("tracks <= full skeleton joints (sibling .cani)", "joint-count",
            "<=348 (sibling unreadable)", record_count, False, skip=True,
            reason=cani_note or "no readable sibling .cani")
    if skb_names is not None:
        # Reported, not gating: B1 measured the .skb to be a *part bank* (52 names
        # for player_a), while the clip declares more tracks (58) — the pin is the
        # measured relationship, per the plan's "pin whatever the data shows".
        add("tracks vs part-bank bone names (sibling .skb)", "finding",
            skb_names, record_count, record_count == skb_names)

    # --- the gate's curve assertions (run on the candidate extraction) --------
    joints = candidate_curves(data, records, info["fps"])
    sanity = curve_sanity(joints)
    add("curve extraction produces keys", "curves", ">0", sanity["keys"],
        sanity["keys"] > 0)
    add("no NaN quaternions", "curves", 0, sanity["nan_quats"],
        sanity["nan_quats"] == 0)
    add("quaternion norms within 1e-3 of 1", "curves", "<=1e-3",
        sanity["max_norm_error"], (sanity["max_norm_error"] or 1) <= 1e-3)
    add("adjacent-key quaternion dot > 0.9", "curves", "0 pairs below",
        "%d/%d" % (sanity["pairs_below_0_9"], sanity["adjacent_pairs"]),
        sanity["pairs_below_0_9"] == 0)
    add("keys with |v| > 1 (w undefined)", "curves", 0, sanity["over_unity_keys"],
        sanity["over_unity_keys"] == 0)
    # No time fields were found in the payload (§3.4): the candidate model derives
    # uniform times from the family fps, so these two checks are reported as
    # unverified rather than passing vacuously.
    times_ok = all(
        all(b > a for a, b in zip(j["times"], j["times"][1:])) for j in joints
    )
    add("times strictly increasing (derived uniform model)", "curves",
        "increasing", "uniform @ %.6f fps" % info["fps"], times_ok, skip=True,
        reason="no time fields in the payload")
    add("timing consistent with family fps (29.999998)", "curves",
        "0x41EFFFFF", hexs(FAMILY_FPS_BITS, 8),
        struct.unpack("<I", struct.pack("<f", info["fps"]))[0] == FAMILY_FPS_BITS,
        skip=True, reason="no time fields in the payload")

    print("check-curves: %s" % path)
    print("  sibling .skb: %s (%s names)" % (
        os.path.basename(skb) if skb else "NOT FOUND",
        skb_names if skb_names is not None else "-"))
    print("  sibling .cani: %s (%s joints)%s" % (
        os.path.basename(cani_path), cani_joints if cani_joints is not None else "unreadable",
        " [%s]" % cani_note if cani_note else ""))
    for check in checks:
        if check.skipped:
            print("SKIP  %-11s %-52s (%s)" % (
                check.kind, check.name, getattr(check, "reason", None) or "unverified"))
            continue
        status = "PASS" if check.ok else ("DELTA" if check.kind == "finding" else "FAIL")
        print("%-5s %-11s %-52s expected %s, measured %s" % (
            status, check.kind, check.name, check.expected, check.measured))
    gating = [c for c in checks if c.kind != "finding" and not c.skipped]
    failed = [c for c in gating if not c.ok]
    deltas = [c for c in checks if c.kind == "finding" and not c.ok]
    print("summary: %d/%d gating assertions pass (%d skipped, %d finding delta(s))" % (
        len(gating) - len(failed), len(gating), sum(1 for c in checks if c.skipped),
        len(deltas)))
    if failed:
        print("gate: NOT MET — %d curve assertion(s) fail; the container decode is"
              % len(failed))
        print("      verified but the curve encoding is not (docs/notes/re-anb.md §3).")
    else:
        print("gate: MET")
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(
                {
                    "file": path,
                    "checks": [vars(c) for c in checks],
                    "sanity": sanity,
                    "gate": "MET" if not failed else "NOT MET",
                },
                fh,
                indent=2,
            )
            fh.write("\n")
    return 1 if failed else 0


# ------------------------------------------------------------ check-skb command


def glb_joint_names(path):
    """Read the joint node names out of a .glb (JSON chunk only, no dependencies)."""
    data = read_bytes(path)
    if data[:4] != b"glTF":
        die("%s is not a glb (magic %s)" % (path, data[:4].hex()))
    length = struct.unpack_from("<I", data, 8)[0]
    off = 12
    doc = None
    while off + 8 <= length:
        chunk_len, chunk_type = struct.unpack_from("<II", data, off)
        if chunk_type == 0x4E4F534A:  # "JSON"
            doc = json.loads(
                data[off + 8 : off + 8 + chunk_len].decode("utf-8").rstrip("\x00 ")
            )
            break
        off += 8 + chunk_len
    if doc is None:
        die("%s has no glTF JSON chunk" % path)
    if not doc.get("skins"):
        die("%s has no skin" % path)
    return [
        doc["nodes"][joint].get("name", "") for joint in doc["skins"][0]["joints"]
    ]


def weightinfo_names(path):
    if not os.path.isfile(path):
        return None
    names = []
    with open(path, "rb") as fh:
        for line in fh.read().decode("latin-1").split("\r\n"):
            if line.strip():
                names.append(line.split()[0].lstrip("#"))
    return names


def repo_root():
    """Walk up from this file until the directory that holds .git."""
    here = os.path.dirname(os.path.abspath(__file__))
    while here != os.path.dirname(here):
        if os.path.exists(os.path.join(here, ".git")):
            return here
        here = os.path.dirname(here)
    return None


def find_glb(explicit):
    if explicit:
        # Try the cwd first (the glb lives in out/, not in the game tree), then the game tree.
        path = explicit if os.path.isabs(explicit) or os.path.isfile(explicit) else resolve(explicit)
        if not os.path.isfile(path):
            die("glb not found: %s" % path)
        return path
    env = os.environ.get("SOTC_GLB")
    if env:
        if not os.path.isfile(env):
            die("SOTC_GLB points at a missing file: %s" % env)
        return env
    repo = repo_root()
    candidates = []
    if repo:
        candidates.append(os.path.join(repo, GLB_DEFAULT_REL))
        # A linked worktree has no out/ of its own: follow the gitdir into the main checkout.
        gitfile = os.path.join(repo, ".git")
        if os.path.isfile(gitfile):
            with open(gitfile) as fh:
                line = fh.read().strip()
            if line.startswith("gitdir:"):
                gitdir = line.split(":", 1)[1].strip()
                main = os.path.dirname(os.path.dirname(os.path.dirname(gitdir)))
                candidates.append(os.path.join(main, GLB_DEFAULT_REL))
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


class Check:
    def __init__(self, name, kind, expected, measured, ok):
        self.name = name
        self.kind = kind
        self.expected = expected
        self.measured = measured
        self.ok = ok
        self.skipped = False


def cmd_check_skb(args):
    path = resolve(args.file)
    if not os.path.isfile(path):
        die("file not found: %s" % path)
    info = parse_skb(path)
    header = info["header"]
    blob = info["name_blob"]
    names = blob["names"]
    name_set = set(names)

    glb = find_glb(args.glb)
    glb_names = glb_joint_names(glb) if glb else None
    weightinfo = weightinfo_names(os.path.splitext(path)[0] + ".weightinfo")

    checks = []

    def add(name, kind, expected, measured, ok):
        checks.append(Check(name, kind, expected, measured, ok))

    add("magic is 78 66 66 00", "structural", "78666600", header["magic"],
        header["magic"] == "78666600")
    add("size field @0x14 == file size", "structural", info["size"],
        header["size_field_0x14"], header["size_matches"])
    add("0x0C is the .skb value 0", "structural", 0, header["u16_0x0c"],
        header["u16_0x0c"] == 0)
    add("name blob starts at 0x110", "structural", "0x110",
        None if blob["start"] is None else "0x%X" % blob["start"],
        blob["start"] == BLOB_BASE)
    add("names extracted", "structural", ">0", len(names), len(names) > 0)
    add("first name is Bone_KOSI", "structural", "Bone_KOSI",
        names[0] if names else None, bool(names) and names[0] == "Bone_KOSI")
    add("Bone_KOSI and Bone_MOMO_L present", "plan", True,
        "Bone_KOSI" in name_set and "Bone_MOMO_L" in name_set,
        "Bone_KOSI" in name_set and "Bone_MOMO_L" in name_set)

    tail = info["tail"]
    records = info["records"]
    descriptors = info.get("section_descriptors", [])
    add("tail tables present", "structural", True, tail is not None, tail is not None)
    if tail:
        add("tail table tags are 0x102/0x000", "structural", "0x102/0x000",
            sorted({hexs(e["tag"], 3) for e in tail["entries"]}),
            all(e["tag"] in (TAG_RECORD, TAG_OFFSET) for e in tail["entries"]))
        resolved = sum(1 for e in tail["entries"] if e.get("resolves_to"))
        add("table-B entries resolve to names", "structural", ">=1", resolved, resolved > 0)
        # Recompute the leading 0x40-stride run from the raw tag-0x102 entries and
        # require the parser's split to agree with it (independent cross-check).
        raw_offsets = [e["offset"] for e in tail["entries"] if e["tag"] == TAG_RECORD]
        stride_run = 1
        while (stride_run < len(raw_offsets)
               and raw_offsets[stride_run] == raw_offsets[0] + 0x40 * stride_run):
            stride_run += 1
        add("record run at stride 0x40", "structural", len(records), stride_run,
            bool(raw_offsets) and stride_run >= 1 and stride_run == len(records))
        add("records are a subset of the names", "structural", "<=%d" % len(names),
            len(records), 0 < len(records) <= len(names))
        named = sum(1 for r in records if r.get("name"))
        add("record name offsets resolve", "structural", len(records), named,
            bool(records) and named == len(records))
        add("trailing tag-0x102 entries are section descriptors", "structural", "<=3",
            len(descriptors), len(descriptors) <= 3)
        first = records[0] if records else None
        add("first record is Bone_KOSI", "structural", "Bone_KOSI",
            first.get("name") if first else None, bool(first) and first.get("name") == "Bone_KOSI")
    else:
        for name in ("tail table tags are 0x102/0x000", "table-B entries resolve to names",
                     "record run at stride 0x40", "records are a subset of the names",
                     "record name offsets resolve",
                     "trailing tag-0x102 entries are section descriptors",
                     "first record is Bone_KOSI"):
            add(name, "structural", None, None, False)

    add("name set is a subset of the glb joints", "glb", True,
        None if glb_names is None else len(name_set - set(glb_names)),
        None if glb_names is None else name_set <= set(glb_names))
    add("name set equals the sibling .weightinfo", "sibling", True,
        None if weightinfo is None else len(set(weightinfo)),
        None if weightinfo is None else name_set == set(weightinfo))

    # Plan expectations (2026-10-18 RE-1 plan, task B1): these describe the *full*
    # skeleton (wander.cskl / wander.glb), not the per-part skeleton bank.
    add("plan: name count == %d" % SKB_NAME_COUNT, "plan", SKB_NAME_COUNT, len(names),
        len(names) == SKB_NAME_COUNT)
    add('plan: first name == "%s"' % SKB_FIRST_NAME, "plan", SKB_FIRST_NAME,
        names[0] if names else None, bool(names) and names[0] == SKB_FIRST_NAME)
    add("plan: name set == glb joints", "plan", True,
        None if glb_names is None else len(name_set ^ set(glb_names)),
        None if glb_names is None else name_set == set(glb_names))

    if glb is None:
        for check in checks:
            if check.kind == "glb" or check.name.startswith("plan: name set"):
                check.skipped = True

    print("check-skb: %s" % path)
    if glb:
        print("  glb: %s (%d joints)" % (glb, len(glb_names)))
    else:
        print("  glb: NOT FOUND (set SOTC_GLB or run after the M0 conversion) — glb checks skipped")
    print("  sibling: %s" % (os.path.splitext(path)[0] + ".weightinfo"))
    for check in checks:
        if check.skipped:
            print("SKIP  %-9s %-40s (glb unavailable)" % (check.kind, check.name))
            continue
        status = "PASS" if check.ok else ("DELTA" if check.kind == "plan" else "FAIL")
        detail = "expected %s, measured %s" % (check.expected, check.measured)
        if check.kind == "plan" and not check.ok:
            detail = "plan expected %s, measured %s" % (check.expected, check.measured)
        print("%-5s %-9s %-40s %s" % (status, check.kind, check.name, detail))

    gating = [c for c in checks if c.kind != "plan" and not c.skipped]
    plan = [c for c in checks if c.kind == "plan" and not c.skipped]
    failed = [c for c in gating if not c.ok]
    deltas = [c for c in plan if not c.ok]
    skipped = [c for c in checks if c.skipped]
    print(
        "summary: gating %d/%d pass, plan %d/%d hold, %d skipped"
        % (len(gating) - len(failed), len(gating), len(plan) - len(deltas), len(plan), len(skipped))
    )
    if deltas:
        print(
            "NOTE: %d plan expectation(s) do not hold for the .skb bank — the plan's %d/"
            '"%s" values describe the full skeleton (wander.cskl / wander.glb), while the'
            % (len(deltas), SKB_NAME_COUNT, SKB_FIRST_NAME)
        )
        print(
            "      skeleton bank holds this model part's bone subset: %d names here,"
            % len(names)
        )
        print(
            '      "%s" %s. Recorded as a finding in docs/notes/re-anb.md §2.'
            % (
                SKB_FIRST_NAME,
                "is absent" if SKB_FIRST_NAME not in name_set else "is present",
            )
        )
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(
                {
                    "file": path,
                    "glb": glb,
                    "checks": [vars(c) for c in checks],
                    "summary": {
                        "gating_pass": len(gating) - len(failed),
                        "gating_total": len(gating),
                        "plan_hold": len(plan) - len(deltas),
                        "plan_total": len(plan),
                        "skipped": len(skipped),
                    },
                    "measured": {
                        "name_count": len(names),
                        "first_name": names[0] if names else None,
                        "glb_joint_count": len(glb_names) if glb_names else None,
                        "weightinfo_count": len(weightinfo) if weightinfo else None,
                    },
                },
                fh,
                indent=2,
            )
            fh.write("\n")

    if failed:
        print("probe: check-skb FAILED (%d gating assertion(s))" % len(failed))
        return 1
    if args.strict_plan and deltas:
        print("probe: check-skb FAILED (--strict-plan: %d plan delta(s))" % len(deltas))
        return 1
    return 0


# ------------------------------------------------------------------------ main


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="probe.py", description="RE probe for the xff animation container family"
    )
    sub = parser.add_subparsers(dest="command")

    p_skb = sub.add_parser("skb", help="structure dump of a .skb")
    p_skb.add_argument("file")
    p_skb.add_argument("--json", metavar="OUT", help="also write the dump as JSON")
    p_skb.set_defaults(func=cmd_skb)

    p_class = sub.add_parser("classify", help="classify container files")
    p_class.add_argument("patterns", nargs="+", metavar="PATTERN",
                         help="paths or shell-quoted globs, resolved against the game tree")
    p_class.add_argument("--limit", type=int, metavar="N", help="max files per pattern (sorted)")
    p_class.add_argument("--json", metavar="OUT", help="also write the rows as JSON")
    p_class.set_defaults(func=cmd_classify)

    p_anb = sub.add_parser("anb", help="structure dump of a .anb (and optional curve extraction)")
    p_anb.add_argument("file", nargs="?", default=ANB_DEFAULT,
                       help="default: %s" % ANB_DEFAULT)
    p_anb.add_argument("--json", metavar="OUT", help="also write the structure dump as JSON")
    p_anb.add_argument("--curves", metavar="OUT",
                       help="also write the candidate per-joint curves as JSON (unverified)")
    p_anb.set_defaults(func=cmd_anb)

    p_cc = sub.add_parser("check-curves", help="the gate's pinned assertions for player_a_stand")
    p_cc.add_argument("--file", default=ANB_DEFAULT, help="default: %s" % ANB_DEFAULT)
    p_cc.add_argument("--cani", help="plaintext sibling .cani (default: %s)" % CANI_SIBLING_DEFAULT)
    p_cc.add_argument("--json", metavar="OUT", help="also write the check result as JSON")
    p_cc.set_defaults(func=cmd_check_curves)

    p_check = sub.add_parser("check-skb", help="pinned assertions for player_a.skb")
    p_check.add_argument("--file", default=SKB_DEFAULT, help="default: %s" % SKB_DEFAULT)
    p_check.add_argument("--glb", help="wander.glb (default: out/library/... in the repo)")
    p_check.add_argument("--json", metavar="OUT", help="also write the check result as JSON")
    p_check.add_argument("--strict-plan", action="store_true",
                         help="also fail when a plan expectation does not hold")
    p_check.set_defaults(func=cmd_check_skb)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
