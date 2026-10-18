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
  check-skb [options]         pinned assertions for cp3shadow/skel/player_a.skb

Inputs are read-only.  Files are resolved against the game tree
($SOTC_GAME_ROOT, default below); patterns (shell-quoted, e.g. 'anim/*.anb')
are expanded against that tree too, so the probe can be run from the repo root.
"""

import argparse
import glob
import json
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
    out.write("tagged pointers (value = tag<<16 | offset)\n")
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
