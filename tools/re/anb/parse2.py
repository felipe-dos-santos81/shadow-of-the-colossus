#!/usr/bin/env python3
"""RE-2 T1: flag-clear `.anb` reader — the parse-chain model, end to end.

This is the *new* reader for the **flag-clear** layout variant of BluePoint `.anb`
motion banks (version word bit 16 clear).  It implements the model measured in
`docs/notes/re2-recon.md` (parse chain) and pinned in `docs/notes/re2-keys.md`
(per-type key formats): the file→structure mapping, the per-track blob/descriptor
layout, the key-data formats (stride/packing/arithmetic — read out of the engine's
key accessor `FUN_0060bba0` and vector accessor `FUN_0060e0c0` in `eboot.elf`), and
the key counts.

Layout summary (all offsets data-region-relative; the data region starts at file
0x110 — see the notes for the measured evidence):

  structure      = file[0x110 + u32@0x10]
  +0x00 version word (byte 0 = version, bit 16 = the flag-set variant marker)
  +0x04 low u16 = frame count        +0x08 -> per-track type byte array
  +0x0C -> root descriptor (6 floats) +0x10 -> per-track blob pointer array
  +0x14 track count                  +0x44 -> first track block (cross-check)
  root series    = [frames x 6 B int16 triplets][6 float32]  (keys before the
                   descriptor; the floats are the per-component offset/scale pairs,
                   applied by the engine's vector accessor: value = i16/32767*scale
                   + offset)
  per track i    = [keys_i][blob_i];  keys_i = [keyptr_i, blob_i), where keyptr_i
                   is the pointer field inside blob_i (its offset depends on the
                   type) and count_i = (blob_i - keyptr_i) / stride.  The count
                   equals the frame count for every type; measured exceptions
                   (see the notes): 0xC key blocks carry 0 or 2 pad bytes, and
                   0xD arrays may carry one extra key (a 0 sentinel).
  type 0         = a static track: blob = 4 float32 (a unit quaternion x,y,z,w),
                   no keys, no pointer field

Per-type key formats (stride and arithmetic: `docs/notes/re2-keys.md`):

  type 0xD   int16 scalar, 2 B stride:  s = i16/32767*scale + offset; angle
             theta = s*0.5; quaternion = (axis)*sin(theta), w = cos(theta) where
             axis = the blob's floats [2],[3],[4]
  types 3-6  u32 packed, 4 B stride: three signed 9-bit fields (1/255 scale) +
             a signed 4-bit residual + 1 sign bit; the three stored fields map to
             the quaternion components that are NOT the case's derived index
             (case 3 -> derived x, 4 -> y, 5 -> z, 6 -> w), the descriptor's
             offset/scale pairs apply in field order (f0/f1 -> 1st field, ...,
             f4/f5 -> 3rd, f6/f7 -> derived); the derived component =
             f6 + f7*residual/7 +/- sqrt(|1-|v_stored|^2|) (the sqrt term only
             when |v_stored|^2 <= 1; the sign bit set -> negative)
  type 0xC   3 x int16, 6 B stride: per-component i16/32767*scale + offset;
             w = +/- sqrt(|1-|v|^2|), sign = LSB of the first int16 (set -> neg)
  type 0xE   u32 packed, 4 B stride: x = bits 31..21 (1/1023), y = bits 20..11
             (1/511), z = bits 10..1 (1/511), each scaled/offset by the blob's
             pairs; w = +/- sqrt(|1-|v|^2|) (bit 0 of the word)
  type 2     (not observed in the 4,455-file corpus) float32 angle, 4 B stride:
             quaternion = (axis)*sin(angle), w = cos(angle); axis = floats [0..2]
  type 0xB   (not a track type in the corpus; the *root* series has the 6 B
             triplet shape but is a position series, see the notes)

Modes
  dump <file> [--json OUT]     structure + per-track table (type, blob, keyptr,
                               stride, key count, extent, first/last words)
  keys <file> [--json OUT] [--track N]
                               the decoded per-track key arrays (quaternions; the
                               0xD scalars and the type-0 static quaternions are
                               reported too)
  root <file> [--json OUT]     the pre-region (root position series) decode
  verify [--json OUT] [--full]
                               the pinned assertions (exit non-zero on failure):
                               adjacent-key dots > 0.9 on long tracks; first ==
                               last on looped tracks; type-0xD scalar smoothness;
                               descriptor-normalized norms <= 1 (+ a small
                               quantization tolerance, measured); plus the
                               count/extent rules per type and the type-0 static
                               quaternion norms.  Runs the two pinned samples +
                               a 300-file deterministic corpus sample (--full:
                               all 4,455 .anb).

Inputs are read-only; files/patterns resolve against the game tree
($SOTC_GAME_ROOT, default below).  Python 3 stdlib only; no state, no writes
outside the requested --json path.
"""

import argparse
import glob
import json
import math
import os
import random
import struct
import sys

DEFAULT_GAME_ROOT = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"

XFF_MAGIC = b"\x78\x66\x66\x00"
BLOB_BASE = 0x110            # the data region's file offset (constant, measured)
FLAG_SET_BIT = 1 << 16       # version word bit 16 = the 12-byte-record variant

# The per-type key-pointer field inside the blob (FUN_0066dec0's re-based field).
KEY_PTR_FIELD = {0x02: 0x0C, 0x03: 0x20, 0x04: 0x20, 0x05: 0x20, 0x06: 0x20,
                 0x0C: 0x18, 0x0D: 0x14, 0x0E: 0x18}
# The per-type key stride in bytes (measured; types 3-6/0xE/0x2 = 4, 0xC = 6, 0xD = 2).
KEY_STRIDE = {0x02: 4, 0x03: 4, 0x04: 4, 0x05: 4, 0x06: 4,
              0x0C: 6, 0x0D: 2, 0x0E: 4}
# The per-type blob size in bytes (floats + the pointer word; measured).
BLOB_SIZE = {0x00: 16, 0x02: 16, 0x03: 36, 0x04: 36, 0x05: 36, 0x06: 36,
             0x0C: 28, 0x0D: 24, 0x0E: 28}

# The engine constants (read from eboot.elf; docs/notes/re2-keys.md):
#   0x0126b2ac = 1/32767, 0x0126b2b0 = 0.5, 0x0126b2b4 = 1/1023,
#   0x0126b2b8 = 1/511, 0x0126b2c0 = 1/255, 0x0126b2c4 = 1/7,
#   0x0126b8e0 = {+1.0, -1.0} (the derived-component sign table)
Q15 = 1.0 / 32767.0
Q9 = 1.0 / 255.0
Q11 = 1.0 / 1023.0
Q10 = 1.0 / 511.0
RESIDUAL_SCALE = 1.0 / 7.0

# The case layouts for types 3-6: (derived component index,
# [(component index, bit shift, descriptor pair index), ...], residual shift, sign bit).
# The three stored 9-bit fields occupy the quaternion components other than the
# derived one, packed from the top of the word; the descriptor pairs apply in
# field order (the first stored field uses f0/f1, ...), the derived uses f6/f7.
PACKED_LAYOUTS = {
    0x03: (0, [(1, 18, 0), (2, 9, 1), (3, 0, 2)], 27, 31),
    0x04: (1, [(0, 23, 0), (2, 9, 1), (3, 0, 2)], 18, 22),
    0x05: (2, [(0, 23, 0), (1, 14, 1), (3, 0, 2)], 9, 13),
    0x06: (3, [(0, 23, 0), (1, 14, 1), (2, 5, 2)], 0, 4),
}

# The verification gates (measured bounds; docs/notes/re2-keys.md §4).
NORM_TOL = 4e-3          # stored |v|^2 may exceed 1 by quantization (measured max 2.7e-3)
STATIC_QUAT_TOL = 1e-3   # type-0 static quaternion norm (measured exact 1.000000)
DOT_MIN = 0.9            # the adjacent-key dot gate
LONG_TRACK_KEYS = 64     # "long track" for the dot gate
LONG_TRACK_PAIR_FRAC = 0.95   # per type, over the corpus (measured >= 0.965)
SAMPLE_PAIR_FRAC = 0.95       # per sample file (horse; measured 0.983)
D0D_SMOOTH_SAMPLE = 0.20      # 0xD mean|d|/range on the samples (measured: player <= 0.0507,
                              # horse <= 0.1279 — the kick's scalars are fast but not noise;
                              # a one-period sine at 16 keys is ~0.125, noise ~0.5)
D0D_SMOOTH_MEDIAN = 0.15      # corpus median (sample 0.066 / full 0.0812)
D0D_SMOOTH_P90 = 0.30         # corpus p90 (sample 0.1538 / full 0.2222)
SAMPLE_SEED = 20261018        # the corpus sample (300 of 4,455; the recon's shape)
SAMPLE_SIZE = 300

SAMPLES = ["cp3shadow/anim/player_a_stand.anb",
           "cp3shadow/anim/horse_a_add_horse_kick.anb"]


def die(msg, code=2):
    sys.stderr.write("parse2: %s\n" % msg)
    sys.exit(code)


# --------------------------------------------------------------------------- io


def game_root():
    root = os.environ.get("SOTC_GAME_ROOT") or DEFAULT_GAME_ROOT
    if not os.path.isdir(root):
        die("game tree not found: %s (set SOTC_GAME_ROOT to the extracted game dir)" % root)
    return root


def resolve(path):
    if os.path.isabs(path):
        return path
    return os.path.join(game_root(), path)


def read_bytes(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        die("cannot read %s: %s" % (path, exc.strerror or exc))


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def i16(d, o):
    return struct.unpack_from("<h", d, o)[0]


def f32(d, o):
    return struct.unpack_from("<f", d, o)[0]


def signed9(raw):
    return raw - 512 if raw >= 256 else raw


def signed4(raw):
    return raw - 16 if raw >= 8 else raw


def sext(value, bits):
    mask = 1 << (bits - 1)
    return (value & (mask - 1)) - (value & mask)


# ------------------------------------------------------------------- structure


def parse_anb(data, path="<bytes>"):
    """Parse one flag-clear `.anb` into the structure/track model (no decoding).

    Returns a dict; raises `ValueError` for anything outside the flag-clear scope
    (non-xff magic, truncated files, the flag-set variant).
    """
    if not data.startswith(XFF_MAGIC):
        raise ValueError("%s is not an xff container (magic %s)" % (path, data[:4].hex()))
    if len(data) < BLOB_BASE + 0x40:
        raise ValueError("%s is shorter than the data region" % path)
    size_field = u32(data, 0x14)
    if size_field != len(data):
        raise ValueError("%s: size field @0x14 = %d != file size %d" % (path, size_field, len(data)))
    struct_off = u32(data, 0x10)
    if not 0 < struct_off < len(data) - BLOB_BASE:
        raise ValueError("%s: structure offset 0x%X out of range" % (path, struct_off))
    st = BLOB_BASE + struct_off
    version_word = u32(data, st)
    if version_word & FLAG_SET_BIT:
        raise ValueError("%s: flag-set variant (version word 0x%08X bit 16 set) — "
                         "out of parse2's scope (RE-2 T2)" % (path, version_word))
    frames = u16(data, st + 4)
    tracks = u32(data, st + 0x14)
    if not 0 < tracks <= 4096:
        raise ValueError("%s: implausible track count %d" % (path, tracks))
    types_off = u32(data, st + 0x08)
    blobs_off = u32(data, st + 0x10)
    desc_off = u32(data, st + 0x0C)
    if BLOB_BASE + types_off + tracks > len(data) or BLOB_BASE + blobs_off + 4 * tracks > len(data):
        raise ValueError("%s: type/blob arrays out of range" % path)
    types = [data[BLOB_BASE + types_off + i] for i in range(tracks)]
    blob_ptrs = [u32(data, BLOB_BASE + blobs_off + 4 * i) for i in range(tracks)]

    track_rows = []
    for i in range(tracks):
        t = types[i]
        row = {"index": i, "type": t, "blob": blob_ptrs[i], "keyptr": None,
               "stride": KEY_STRIDE.get(t), "keys": None, "extent": None,
               "blob_size": BLOB_SIZE.get(t)}
        b = BLOB_BASE + blob_ptrs[i]
        if b + BLOB_SIZE.get(t, 0) > len(data):
            row["error"] = "blob out of range"
            track_rows.append(row)
            continue
        if t in KEY_PTR_FIELD:
            kp = u32(data, b + KEY_PTR_FIELD[t])
            row["keyptr"] = kp
            row["extent"] = blob_ptrs[i] - kp
            stride = KEY_STRIDE[t]
            if 0 < row["extent"]:
                row["keys"] = row["extent"] // stride
        track_rows.append(row)

    return {
        "file": path,
        "size": len(data),
        "version_word": version_word,
        "version": version_word & 0xFF,
        "flag_set": bool(version_word & FLAG_SET_BIT),
        "frames": frames,
        "tracks": tracks,
        "structure": struct_off,
        "types_offset": types_off,
        "blobs_offset": blobs_off,
        "descriptor_offset": desc_off,
        "descriptor": ([f32(data, BLOB_BASE + desc_off + 4 * j) for j in range(6)]
                       if desc_off and BLOB_BASE + desc_off + 0x1C <= len(data) else []),
        "descriptor_ptr_slot": (u32(data, BLOB_BASE + desc_off + 0x18)
                                if desc_off and BLOB_BASE + desc_off + 0x1C <= len(data) else None),
        "root_keys": root_key_count(desc_off),
        "track_rows": track_rows,
        "data": data,
    }


def root_key_count(desc_off):
    """The root series' key count from the descriptor offset (None when absent).

    Measured over the 2,994 flag-clear files: descriptor offset 0 (no root series, 1,381
    files) or `6 * frames + pad` with pad in {0, 2} (1,613/1,613) — the same 4-byte
    alignment pad as the 0xC key blocks.
    """
    if desc_off == 0:
        return None
    rem = desc_off % 6
    if rem == 0:
        return desc_off // 6
    if rem == 2:
        return (desc_off - 2) // 6
    return None


def blob_floats(data, blob):
    """The blob's float words (8; zero-padded if a truncated file runs short)."""
    avail = max(0, min(8, (len(data) - blob) // 4))
    vals = [f32(data, blob + 4 * j) for j in range(avail)]
    vals.extend([0.0] * (8 - len(vals)))
    return vals


# --------------------------------------------------------------------- decoding


def decode_track(data, row):
    """Decode one track's keys per its type.  Returns (keys, meta).

    `keys` is a list of quaternion 4-tuples (x, y, z, w) for the rotation types
    (3-6, 0xC, 0xD, 0xE, 2) and `None` for the static type 0; `meta` carries the
    type-specific extras (the 0xD scalars, the static quaternion, per-key stats).
    """
    t = row["type"]
    b = BLOB_BASE + row["blob"]
    meta = {"type": t}
    if t == 0:
        q = [f32(data, b + 4 * j) for j in range(4)]
        meta["static_quaternion"] = q
        return None, meta
    if row.get("keyptr") is None or not row.get("keys"):
        meta["error"] = "no key data"
        return [], meta
    kp = BLOB_BASE + row["keyptr"]
    n = row["keys"]
    f = blob_floats(data, b)
    meta["descriptor"] = f[:6] + [f[6], f[7]]
    meta["max_stored_norm2_minus1"] = 0.0
    keys = []

    if t in PACKED_LAYOUTS:
        derived, fields, rshift, sbit = PACKED_LAYOUTS[t]
        for k in range(n):
            w = u32(data, kp + 4 * k)
            comp = [0.0, 0.0, 0.0, 0.0]
            for idx, sh, pair in fields:
                comp[idx] = signed9((w >> sh) & 0x1FF) * Q9 * f[2 * pair + 1] + f[2 * pair]
            n2 = comp[0] ** 2 + comp[1] ** 2 + comp[2] ** 2 + comp[3] ** 2
            meta["max_stored_norm2_minus1"] = max(meta["max_stored_norm2_minus1"], n2 - 1.0)
            residual = signed4((w >> rshift) & 0xF)
            sign = -1.0 if (w >> sbit) & 1 else 1.0
            sq = math.sqrt(abs(1.0 - n2)) if n2 <= 1.0 else 0.0
            comp[derived] = f[6] + f[7] * (residual * RESIDUAL_SCALE) + sign * sq
            keys.append(tuple(comp))
    elif t == 0x0C:
        for k in range(n):
            o = kp + 6 * k
            x = i16(data, o) * Q15 * f[1] + f[0]
            y = i16(data, o + 2) * Q15 * f[3] + f[2]
            z = i16(data, o + 4) * Q15 * f[5] + f[4]
            n2 = x * x + y * y + z * z
            meta["max_stored_norm2_minus1"] = max(meta["max_stored_norm2_minus1"], n2 - 1.0)
            sq = math.sqrt(abs(1.0 - n2)) if n2 <= 1.0 else 0.0
            sign = -1.0 if (u16(data, o) & 1) else 1.0
            keys.append((x, y, z, sign * sq))
    elif t == 0x0D:
        scalars = []
        for k in range(n):
            s = i16(data, kp + 2 * k) * Q15 * f[1] + f[0]
            scalars.append(s)
            theta = s * 0.5
            keys.append((f[2] * math.sin(theta), f[3] * math.sin(theta),
                         f[4] * math.sin(theta), math.cos(theta)))
        meta["scalars"] = scalars
    elif t == 0x0E:
        for k in range(n):
            w = u32(data, kp + 4 * k)
            x = sext(w >> 21, 11) * Q11 * f[1] + f[0]
            y = sext((w >> 11) & 0x3FF, 10) * Q10 * f[3] + f[2]
            z = sext((w >> 1) & 0x3FF, 10) * Q10 * f[5] + f[4]
            n2 = x * x + y * y + z * z
            meta["max_stored_norm2_minus1"] = max(meta["max_stored_norm2_minus1"], n2 - 1.0)
            sq = math.sqrt(abs(1.0 - n2)) if n2 <= 1.0 else 0.0
            sign = 1.0 if (~(w * 2) & 2) else -1.0
            keys.append((x, y, z, sign * sq))
    elif t == 0x02:
        for k in range(n):
            angle = f32(data, kp + 4 * k)
            keys.append((f[0] * math.sin(angle), f[1] * math.sin(angle),
                         f[2] * math.sin(angle), math.cos(angle)))
    else:
        meta["error"] = "unhandled type 0x%02X" % t
    return keys, meta


def decode_root(info):
    """The pre-region: [frames x 6 B int16 triplets][6 float32 descriptor].

    The keys are the root's per-frame 3-component series (the engine's *vector*
    accessor, FUN_0060e0c0 format 2): value_c = i16/32767 * scale_c + offset_c
    with (offset, scale) = descriptor pairs (f0,f1), (f2,f3), (f4,f5).  The
    descriptor's pointer slot (the word at descriptor+0x18) is reported as-is.
    Raises ValueError when the file carries no root series (descriptor offset 0).
    """
    data = info["data"]
    desc = BLOB_BASE + info["descriptor_offset"]
    keys_off = BLOB_BASE
    n = info["root_keys"]
    if n is None:
        raise ValueError("%s: no root series (descriptor offset %s)" %
                         (info["file"], info["descriptor_offset"]))
    f = info["descriptor"]
    values = []
    for k in range(n):
        o = keys_off + 6 * k
        values.append(tuple(i16(data, o + 2 * c) * Q15 * f[2 * c + 1] + f[2 * c]
                            for c in range(3)))
    return {"keys": n, "values": values, "descriptor": f,
            "descriptor_ptr_slot": info["descriptor_ptr_slot"]}


def adjacent_dots(keys):
    dots = []
    for a, b in zip(keys, keys[1:]):
        na = math.sqrt(sum(c * c for c in a))
        nb = math.sqrt(sum(c * c for c in b))
        if na and nb:
            dots.append(sum(x * y for x, y in zip(a, b)) / (na * nb))
    return dots


def first_last_l2(keys):
    if not keys:
        return None
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(keys[0], keys[-1])))


def scalar_smoothness(values):
    """Mean |delta| / range of a scalar series (0.0 for a constant series)."""
    if len(values) < 2:
        return None
    rng = max(values) - min(values)
    if rng == 0:
        return 0.0
    dv = [abs(values[i + 1] - values[i]) for i in range(len(values) - 1)]
    return sum(dv) / len(dv) / rng


# ------------------------------------------------------------------ mode: dump


def cmd_dump(args):
    path = resolve(args.file)
    data = read_bytes(path)
    try:
        info = parse_anb(data, path)
    except ValueError as exc:
        die(str(exc))
    info.pop("data", None)

    out = sys.stdout
    out.write("parse2 dump %s (%d bytes)\n" % (info["file"], info["size"]))
    out.write("  version word 0x%08X  version %d  flag-set %s\n" % (
        info["version_word"], info["version"], info["flag_set"]))
    out.write("  frames %d  tracks %d  structure @data 0x%X  descriptor @data 0x%X\n" % (
        info["frames"], info["tracks"], info["structure"], info["descriptor_offset"]))
    out.write("  descriptor floats %s  ptr slot %s  root keys %s\n" % (
        "[%s]" % ", ".join("%.6g" % v for v in info["descriptor"]) if info["descriptor"] else "-",
        ("0x%08X" % info["descriptor_ptr_slot"]) if info["descriptor_ptr_slot"] is not None else "-",
        info["root_keys"] if info["root_keys"] is not None else "none"))
    out.write("  %-4s %-5s %-7s %-7s %-6s %-7s %-6s %s\n" % (
        "idx", "type", "blob", "keyptr", "extent", "stride", "keys", "first/last words"))
    for row in info["track_rows"]:
        if row.get("error"):
            out.write("  %-4d 0x%02X  %s\n" % (row["index"], row["type"], row["error"]))
            continue
        if row["type"] == 0:
            q = [f32(data, BLOB_BASE + row["blob"] + 4 * j) for j in range(4)]
            out.write("  %-4d 0x%02X  0x%04X  %-7s %-7s %-6s %-7s static quat %s\n" % (
                row["index"], row["type"], row["blob"], "-", "-", "-", "-",
                "(" + ", ".join("%.4f" % v for v in q) + ")"))
            continue
        kp = BLOB_BASE + row["keyptr"]
        stride = row["stride"]
        first = data[kp:kp + min(stride, 8)].hex()
        last = kp + stride * (row["keys"] - 1)
        lastb = data[last:last + min(stride, 8)].hex()
        out.write("  %-4d 0x%02X  0x%04X  0x%04X  %-7d %-6d %-7d %s | %s\n" % (
            row["index"], row["type"], row["blob"], row["keyptr"], row["extent"],
            stride, row["keys"], first, lastb))
    counts = {}
    for row in info["track_rows"]:
        counts[row["type"]] = counts.get(row["type"], 0) + 1
    out.write("  type histogram: %s\n" % " ".join(
        "0x%02X=%d" % kv for kv in sorted(counts.items())))

    if args.json:
        out_path = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w") as fh:
            json.dump(info, fh, indent=2)
            fh.write("\n")
        sys.stderr.write("parse2: wrote %s\n" % out_path)
    return 0


# ------------------------------------------------------------------ mode: keys


def cmd_keys(args):
    path = resolve(args.file)
    data = read_bytes(path)
    try:
        info = parse_anb(data, path)
    except ValueError as exc:
        die(str(exc))

    tracks = []
    for row in info["track_rows"]:
        if args.track is not None and row["index"] != args.track:
            continue
        if row.get("error"):
            tracks.append(dict(row, error=row["error"]))
            continue
        keys, meta = decode_track(data, row)
        entry = {
            "index": row["index"],
            "type": row["type"],
            "blob": row["blob"],
            "keyptr": row["keyptr"],
            "stride": row["stride"],
            "count": row["keys"],
            "extent": row["extent"],
            "descriptor": meta.get("descriptor"),
        }
        if keys is None:
            entry["static_quaternion"] = meta["static_quaternion"]
        else:
            entry["keys"] = [list(k) for k in keys]
            dots = adjacent_dots(keys)
            entry["dot_min"] = min(dots) if dots else None
            entry["first_last_l2"] = first_last_l2(keys)
            entry["max_stored_norm2_minus1"] = meta.get("max_stored_norm2_minus1")
            if "scalars" in meta:
                entry["scalars"] = meta["scalars"]
        tracks.append(entry)

    payload = {
        "file": info["file"],
        "frames": info["frames"],
        "tracks_total": info["tracks"],
        "descriptor": info["descriptor"],
        "tracks": tracks,
    }
    if args.json:
        out_path = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w") as fh:
            json.dump(payload, fh, indent=2)
            fh.write("\n")
        sys.stderr.write("parse2: wrote %s\n" % out_path)

    out = sys.stdout
    out.write("parse2 keys %s  frames %d\n" % (info["file"], info["frames"]))
    for entry in tracks:
        if entry.get("error"):
            out.write("  t%02d type 0x%02X %s\n" % (entry["index"], entry["type"], entry["error"]))
            continue
        if "static_quaternion" in entry:
            out.write("  t%02d type 0x00 static quat (%s)\n" % (
                entry["index"], ", ".join("%.5f" % v for v in entry["static_quaternion"])))
            continue
        out.write("  t%02d type 0x%02X keys %d (extent %d, stride %d) dotmin %s "
                  "first-last L2 %s stored-norm overshoot %.2e\n" % (
                      entry["index"], entry["type"], entry["count"], entry["extent"],
                      entry["stride"],
                      "%.4f" % entry["dot_min"] if entry["dot_min"] is not None else "-",
                      "%.6f" % entry["first_last_l2"] if entry["first_last_l2"] is not None else "-",
                      entry["max_stored_norm2_minus1"] or 0.0))
    return 0


# ------------------------------------------------------------------ mode: root


def cmd_root(args):
    path = resolve(args.file)
    data = read_bytes(path)
    try:
        info = parse_anb(data, path)
    except ValueError as exc:
        die(str(exc))
    try:
        root = decode_root(info)
    except ValueError as exc:
        print("parse2 root %s: %s" % (info["file"], exc))
        return 0
    values = root["values"]
    steps = [math.sqrt(sum((a - b) ** 2 for a, b in zip(values[i], values[i + 1])))
             for i in range(len(values) - 1)]
    lo = [min(v[c] for v in values) for c in range(3)]
    hi = [max(v[c] for v in values) for c in range(3)]
    print("parse2 root %s  frames %d  root keys %d" % (info["file"], info["frames"], root["keys"]))
    print("  descriptor [%s]  ptr slot 0x%08X" % (
        ", ".join("%.6g" % v for v in root["descriptor"]), root["descriptor_ptr_slot"]))
    print("  component ranges: x [%.4f, %.4f] y [%.4f, %.4f] z [%.4f, %.4f]" % (
        lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    print("  per-key step: max %.5f  mean %.5f" % (
        max(steps) if steps else 0.0, sum(steps) / len(steps) if steps else 0.0))
    if args.json:
        out_path = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w") as fh:
            json.dump({"file": info["file"], "frames": info["frames"],
                       "keys": root["keys"],
                       "descriptor": root["descriptor"],
                       "descriptor_ptr_slot": root["descriptor_ptr_slot"],
                       "values": [list(v) for v in values]}, fh, indent=2)
            fh.write("\n")
        sys.stderr.write("parse2: wrote %s\n" % out_path)
    return 0


# ---------------------------------------------------------------- mode: verify


class Check(object):
    def __init__(self, name, expected, measured, ok, detail=""):
        self.name = name
        self.expected = expected
        self.measured = measured
        self.ok = ok
        self.detail = detail


def corpus_files(full):
    root = game_root()
    files = sorted(glob.glob(os.path.join(root, "cp3shadow", "**", "*.anb"), recursive=True))
    if not files:
        die("no .anb files under %s/cp3shadow" % root)
    if full:
        return files, "full"
    rng = random.Random(SAMPLE_SEED)
    return rng.sample(files, min(SAMPLE_SIZE, len(files))), "sample-%d-seed-%d" % (
        min(SAMPLE_SIZE, len(files)), SAMPLE_SEED)


def scan_file(data, path, stats):
    """Accumulate the corpus statistics for one flag-clear file."""
    try:
        info = parse_anb(data, path)
    except ValueError:
        return False
    stats["files"] += 1
    frames = info["frames"]
    desc_off = info["descriptor_offset"]
    if desc_off == 0:
        stats["root_absent"] += 1
    elif info["root_keys"] == frames:
        stats["root_present"] += 1
    else:
        stats["root_violations"].append((path, desc_off, frames))
    for row in info["track_rows"]:
        t = row["type"]
        stats["types"][t] = stats["types"].get(t, 0) + 1
        if t == 0:
            if not row.get("error"):
                q = [f32(data, BLOB_BASE + row["blob"] + 4 * j) for j in range(4)]
                norm = math.sqrt(sum(v * v for v in q))
                stats["t0_norms"].append(norm)
            continue
        if row.get("error") or row.get("keyptr") is None:
            continue
        stats["keyed"] += 1
        stride = row["stride"]
        count = row["keys"]
        extent = row["extent"]
        if count and count != frames:
            rels = stats["count_rel"].setdefault(t, {})
            rels[count - frames] = rels.get(count - frames, 0) + 1
        if count and extent is not None:
            pads = stats["pad"].setdefault(t, {})
            pads[extent - count * stride] = pads.get(extent - count * stride, 0) + 1
        if t == 0x0D and count == frames + 1 and count >= 1:
            last = i16(data, BLOB_BASE + row["keyptr"] + 2 * (count - 1))
            stats["d0d_sentinel"][0 if last == 0 else 1] += 1
        keys, meta = decode_track(data, row)
        if meta.get("max_stored_norm2_minus1"):
            stats["norm_over"].append(meta["max_stored_norm2_minus1"])
        if t == 0x0D and "scalars" in meta and count >= 8 and count == frames:
            s = scalar_smoothness(meta["scalars"])
            if s is not None:
                stats["d0d_smooth"].append(s)
        if not keys:
            continue
        # looped invariant: identical first/last key bytes -> identical decoded keys
        kp = BLOB_BASE + row["keyptr"]
        last_off = kp + stride * (count - 1)
        if all(data[kp + j] == data[last_off + j] for j in range(stride)):
            stats["looped"] += 1
            l2 = first_last_l2(keys)
            if l2 and l2 > 1e-9:
                stats["looped_violations"].append((path, row["index"], l2))
        dots = adjacent_dots(keys)
        if not dots:
            continue
        below = sum(1 for x in dots if x < DOT_MIN)
        stats["pairs"][t] = stats["pairs"].get(t, 0) + len(dots)
        stats["pairs_below"][t] = stats["pairs_below"].get(t, 0) + below
        if count >= LONG_TRACK_KEYS:
            stats["long_pairs"][t] = stats["long_pairs"].get(t, 0) + len(dots)
            stats["long_below"][t] = stats["long_below"].get(t, 0) + below
            stats["long_dots"].setdefault(t, []).extend(dots)
    return True


def new_stats():
    return {
        "files": 0, "keyed": 0, "looped": 0, "looped_violations": [],
        "types": {}, "count_rel": {}, "pad": {}, "d0d_sentinel": [0, 0],
        "norm_over": [], "t0_norms": [], "d0d_smooth": [],
        "pairs": {}, "pairs_below": {}, "long_pairs": {}, "long_below": {},
        "long_dots": {},
        "root_absent": 0, "root_present": 0, "root_violations": [],
    }


def percentile(values, frac):
    if not values:
        return None
    values = sorted(values)
    idx = min(len(values) - 1, int(frac * (len(values) - 1)))
    return values[idx]


def cmd_verify(args):
    checks = []

    def add(name, expected, measured, ok, detail=""):
        checks.append(Check(name, expected, measured, ok, detail))

    # --- the pinned samples ---------------------------------------------------
    sample_stats = {}
    for rel in SAMPLES:
        path = resolve(rel)
        if not os.path.isfile(path):
            die("pinned sample missing: %s" % rel)
        data = read_bytes(path)
        info = parse_anb(data, path)
        name = os.path.basename(path)
        frames = info["frames"]
        count_ok = True
        extent_ok = True
        loop_ok = True
        dots_all = []
        overshoot = 0.0
        d0d_smooth = []
        for row in info["track_rows"]:
            if row["type"] == 0:
                continue
            keys, meta = decode_track(data, row)
            overshoot = max(overshoot, meta.get("max_stored_norm2_minus1") or 0.0)
            if row["keys"] != frames or row["extent"] != row["keys"] * row["stride"]:
                count_ok = False
            kp = BLOB_BASE + row["keyptr"]
            stride = row["stride"]
            last_off = kp + stride * (row["keys"] - 1)
            same = all(data[kp + j] == data[last_off + j] for j in range(stride))
            if not same:
                loop_ok = False
            if keys:
                dots_all.extend(adjacent_dots(keys))
                if "scalars" in meta:
                    d0d_smooth.append(scalar_smoothness(meta["scalars"]))
        below = sum(1 for x in dots_all if x < DOT_MIN)
        sample_stats[name] = {
            "frames": frames, "pairs": len(dots_all), "below": below,
            "dot_min": min(dots_all) if dots_all else None,
            "overshoot": overshoot, "looped": loop_ok,
            "d0d_smooth_max": max(d0d_smooth) if d0d_smooth else None,
        }
        add("%s: flag-clear + size field" % name, "parses", "ok", True)
        add("%s: key count == frames on every track" % name, "all", "all" if count_ok else "NO", count_ok)
        add("%s: extent == keys x stride on every track" % name, "all",
            "all" if extent_ok else "NO", extent_ok)
        add("%s: stored |v|^2 <= 1 + %.0e" % (name, NORM_TOL),
            "<= %.0e" % NORM_TOL, "%.2e" % overshoot, overshoot <= NORM_TOL)
        if dots_all:
            add("%s: adjacent dots > %.1f (>= %.0f%% of pairs)" % (name, DOT_MIN, 100 * SAMPLE_PAIR_FRAC),
                ">= %.0f%%" % (100 * SAMPLE_PAIR_FRAC),
                "%.2f%% (%d/%d pairs below; min %.4f)" % (
                    100.0 * (len(dots_all) - below) / len(dots_all), below, len(dots_all),
                    min(dots_all)),
                (len(dots_all) - below) / len(dots_all) >= SAMPLE_PAIR_FRAC,
                "the horse kick's strike frames wrap in the 9-bit fields (measured); "
                "player_a_stand is all > 0.9" if "horse" in name else "")
        if d0d_smooth:
            add("%s: 0xD scalar smoothness (mean|d|/range) < %.2f" % (name, D0D_SMOOTH_SAMPLE),
                "< %.2f" % D0D_SMOOTH_SAMPLE, "max %.4f" % max(d0d_smooth),
                max(d0d_smooth) < D0D_SMOOTH_SAMPLE)

    # first == last on looped tracks: player_a_stand is the looped sample
    player = sample_stats[os.path.basename(SAMPLES[0])]
    add("player_a_stand: every keyed track is looped (first == last key bytes)",
        "all", "all" if player["looped"] else "NO", player["looped"])

    # --- the corpus -----------------------------------------------------------
    files, scope = corpus_files(args.full)
    stats = new_stats()
    parsed = 0
    for path in files:
        data = read_bytes(path)
        if scan_file(data, path, stats):
            parsed += 1
    if parsed == 0:
        die("no flag-clear .anb parsed in the corpus scope")

    add("corpus[%s]: flag-clear files parsed" % scope, ">0", parsed, parsed > 0)
    # count rules
    bad_count = []
    for t, rels in sorted(stats["count_rel"].items()):
        for rel, n in rels.items():
            allowed = (t in (0x03, 0x04, 0x05, 0x06, 0x0C, 0x0E) and rel == 0) or \
                      (t == 0x0D and rel in (0, 1)) or (t == 0x02 and rel == 0)
            if not allowed:
                bad_count.append("0x%02X:%+d x%d" % (t, rel, n))
    add("corpus: key count == frames (0xD may carry one extra key)",
        "3-6/0xC/0xE: == frames; 0xD: frames or frames+1",
        "ok" if not bad_count else ", ".join(bad_count), not bad_count)
    add("corpus: 0xD frames+1 tracks end with the 0 sentinel", "all",
        "%d/%d" % (stats["d0d_sentinel"][0], sum(stats["d0d_sentinel"])),
        stats["d0d_sentinel"][1] == 0)
    bad_pad = ["0x%02X:%d x%d" % (t, p, n) for t, pads in sorted(stats["pad"].items())
               for p, n in pads.items() if p not in (0, 2)]
    add("corpus: extent - keys x stride in {0, 2} (0xC 4-byte blob alignment)",
        "{0,2}", "ok" if not bad_pad else ", ".join(bad_pad), not bad_pad)
    add("corpus: root series present => descriptor offset == 6 x frames (+0/2 pad)",
        "0 violations",
        "%d with a root series, %d without; %d violations" % (
            stats["root_present"], stats["root_absent"], len(stats["root_violations"])),
        not stats["root_violations"])

    # long-track dots per type
    for t in sorted(stats["long_pairs"]):
        total = stats["long_pairs"][t]
        below = stats["long_below"][t]
        frac = (total - below) / total if total else 1.0
        dots = stats["long_dots"].get(t, [])
        add("corpus: type 0x%02X long-track (>=%d keys) dots > %.1f (>= %.0f%%)" % (
            t, LONG_TRACK_KEYS, DOT_MIN, 100 * LONG_TRACK_PAIR_FRAC),
            ">= %.0f%%" % (100 * LONG_TRACK_PAIR_FRAC),
            "%.2f%% (%d/%d below; median %.4f)" % (
                100 * frac, below, total, percentile(dots, 0.5) if dots else 0.0),
            frac >= LONG_TRACK_PAIR_FRAC)
    # 0xE (rare): every pair
    if 0x0E in stats["pairs"]:
        total = stats["pairs"][0x0E]
        below = stats["pairs_below"][0x0E]
        add("corpus: type 0x0E every adjacent dot > %.1f" % DOT_MIN, "0 below",
            "%d/%d below" % (below, total), below == 0)

    # type-0 static quaternions
    if stats["t0_norms"]:
        worst = max(abs(n - 1.0) for n in stats["t0_norms"])
        add("corpus: type-0 static quaternion norms within %.0e of 1" % STATIC_QUAT_TOL,
            "<= %.0e" % STATIC_QUAT_TOL, "max |n-1| = %.2e (%d tracks)" % (worst, len(stats["t0_norms"])),
            worst <= STATIC_QUAT_TOL)
    # stored-norm overshoot
    worst_over = max(stats["norm_over"]) if stats["norm_over"] else 0.0
    add("corpus: descriptor-normalized |v|^2 <= 1 + %.0e" % NORM_TOL,
        "<= %.0e" % NORM_TOL, "max overshoot %.2e" % worst_over, worst_over <= NORM_TOL)
    # looped invariant
    add("corpus: looped tracks (identical first/last key bytes) decode equal",
        "0 violations", "%d violations of %d looped tracks" % (
            len(stats["looped_violations"]), stats["looped"]),
        not stats["looped_violations"])
    # 0xD smoothness
    if stats["d0d_smooth"]:
        med = percentile(stats["d0d_smooth"], 0.5)
        p90 = percentile(stats["d0d_smooth"], 0.9)
        add("corpus: 0xD scalar smoothness median < %.2f" % D0D_SMOOTH_MEDIAN,
            "< %.2f" % D0D_SMOOTH_MEDIAN, "%.4f (%d tracks)" % (med, len(stats["d0d_smooth"])),
            med < D0D_SMOOTH_MEDIAN)
        add("corpus: 0xD scalar smoothness p90 < %.2f" % D0D_SMOOTH_P90,
            "< %.2f" % D0D_SMOOTH_P90, "%.4f" % p90, p90 < D0D_SMOOTH_P90)

    # --- report ---------------------------------------------------------------
    print("parse2 verify: samples + corpus[%s]" % scope)
    for name, s in sorted(sample_stats.items()):
        print("  %s: frames %d, %d pairs, min dot %s, stored-norm overshoot %.2e%s" % (
            name, s["frames"], s["pairs"],
            "%.4f" % s["dot_min"] if s["dot_min"] is not None else "-",
            s["overshoot"],
            ", 0xD smoothness max %.4f" % s["d0d_smooth_max"] if s["d0d_smooth_max"] is not None else ""))
    print("  corpus: %d files, %d keyed tracks, %d looped; types %s" % (
        stats["files"], stats["keyed"], stats["looped"],
        " ".join("0x%02X=%d" % kv for kv in sorted(stats["types"].items()))))
    for check in checks:
        print("%-5s %-72s expected %s, measured %s%s" % (
            "PASS" if check.ok else "FAIL", check.name, check.expected, check.measured,
            "  [%s]" % check.detail if check.detail and not check.ok else ""))
    failed = [c for c in checks if not c.ok]
    print("summary: %d/%d assertions pass" % (len(checks) - len(failed), len(checks)))
    if args.json:
        out_path = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w") as fh:
            json.dump({"scope": scope, "files": stats["files"], "keyed": stats["keyed"],
                       "checks": [vars(c) for c in checks],
                       "samples": sample_stats, "gate": "MET" if not failed else "NOT MET"},
                      fh, indent=2)
            fh.write("\n")
        sys.stderr.write("parse2: wrote %s\n" % out_path)
    return 1 if failed else 0


# ------------------------------------------------------------------------ main


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="parse2.py",
        description="RE-2 T1: flag-clear .anb reader (parse-chain model, per-track key arrays)")
    sub = parser.add_subparsers(dest="command")

    p_dump = sub.add_parser("dump", help="structure + per-track table of one flag-clear .anb")
    p_dump.add_argument("file")
    p_dump.add_argument("--json", metavar="OUT", help="also write the dump as JSON")
    p_dump.set_defaults(func=cmd_dump)

    p_keys = sub.add_parser("keys", help="decode per-track key arrays")
    p_keys.add_argument("file")
    p_keys.add_argument("--track", type=int, help="only this track index")
    p_keys.add_argument("--json", metavar="OUT", help="also write the keys as JSON")
    p_keys.set_defaults(func=cmd_keys)

    p_root = sub.add_parser("root", help="decode the pre-region (root position series)")
    p_root.add_argument("file")
    p_root.add_argument("--json", metavar="OUT", help="also write the series as JSON")
    p_root.set_defaults(func=cmd_root)

    p_verify = sub.add_parser("verify", help="the pinned assertions (exit non-zero on failure)")
    p_verify.add_argument("--full", action="store_true",
                          help="scan all .anb instead of the 300-file sample")
    p_verify.add_argument("--json", metavar="OUT", help="also write the result as JSON")
    p_verify.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
