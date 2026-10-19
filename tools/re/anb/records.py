#!/usr/bin/env python3
"""RE-2 T2 — the flag-set ``.anb`` record reader + the sampler's per-type dispatch.

The ``.anb`` container's motion structure comes in two variants, selected by bit 16
of the structure's version word (``motion+0x00``):

  * flag-clear — the per-track "type byte array" at ``motion+0x08`` + a u32 blob
    pointer array at ``motion+0x10`` (RE-1 / recon §2);
  * **flag-set** — a 12-byte ``{type, p1, p2}`` record array at ``motion+0x10``,
    ``motion+0x14`` records (this file's target).

Everything below is *measured* against the read-only game tree and the Ghidra
project (``eboot.elf``, image base 0; VA→file = ``+0x4000``), and is documented in
``docs/notes/re2-flagset.md``.  The load-time fixups are ``FUN_0066fe60`` (the
structure walk; it dispatches the record walk on bit 16 and re-bases both record
pointers through ``FUN_0066f9a0``) and the per-type blob walkers (``FUN_0066ecd0``
for the types 10/13/14/… family).  The runtime sampler is ``FUN_005fd900``
("GetObjectAnimationFloatFrame"), which reads ``*(int *)(records + track*0xC)`` and
switches on ``type - 1`` over the per-type case bodies tabulated in ``DISPATCH``.

Record model (measured; all offsets are **data-region-relative**, data region base
= ``u32 @0x0C`` header's ``0xB4`` field, 0x110 in every shipped file):

  record+0x00  u32  type + flag bits (bits 16..19 / 8..11 / 0..3 are per-channel
                    format selectors for the blob's three accessor calls; bits
                    24..29 are flags read back by some case bodies)
  record+0x04  u32  31-bit pointer p1 — a name string for the object types
                    (type 13: e.g. "CERBERUS_RUSH_IMPACT"; the sampler itself never
                    reads p1; the loader re-bases it: FUN_0066f9a0)
  record+0x08  u32  31-bit pointer p2 — the track's key blob (the sampler's input)

Key blob (what p2 points at; the loader re-bases word 1 and word 3, and for the
type-13 family also the descriptor pointers, FUN_0066ecd0):

  blob+0x00  u32  format word: bits 0..3 → FUN_0060e640's format, bits 8..11 →
                  FUN_0060bba0's format, bits 16..19 → FUN_0060e0c0's format
  blob+0x04  u32  31-bit pointer A → the FUN_0060e0c0 sample descriptor
  blob+0x08  u32  31-bit pointer B → the FUN_0060bba0 sample descriptor
  blob+0x0C  u32  31-bit pointer C → the FUN_0060e640 sample descriptor

The three accessors (exact arithmetic read from the decompiles; scale constants
read from the binary):

  FUN_0060e0c0  vector channel (w := 1); formats 0 (raw 3 floats), 2 (6
                offset/scale floats + int16 triplets at desc[6], 6-byte stride),
                else (float triplets at desc + key*12)
  FUN_0060bba0  quaternion channel; formats 0 (raw 4 floats), 1, 2, 3..6 (packed
                u32), 0xb (int16 triplets), 0xc, 0xd (scalar angle → axis-angle),
                0xe (packed u32); >0xe → zeros
  FUN_0060e640  vector channel; formats 0, 2 (packed u32 at desc[6]), 3 (packed
                u16 at desc[6]), else (float triplets at desc + key*12)

Time model (C3 §6.4, re-checked): the sampler's time is a float frame index; keys
are indexed by the integer frame, key i and i+1 are blended linearly by the
fraction, and the wrap is modulo frames-1.  Integer frames read key i alone.

Usage (inputs read-only; patterns expand against the game tree, $SOTC_GAME_ROOT):

  python3 tools/re/anb/records.py dump   cp3shadow/anim/objanim_leo_a_jump_fall.anb
  python3 tools/re/anb/records.py decode cp3shadow/anim/objanim_leo_a_jump_fall.anb --track 0
  python3 tools/re/anb/records.py scan   'cp3shadow/anim/*.anb' [--limit N] [--json out]
  python3 tools/re/anb/records.py verify [--json out]        # pinned assertions, exit 0/1
"""

import argparse
import glob
import json
import math
import os
import struct
import sys

DEFAULT_GAME_ROOT = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"

XFF_MAGIC = b"\x78\x66\x66\x00"  # "xff\0"
DATA_BASE_DEFAULT = 0x110  # u32 @0xB4; measured constant in all .anb
RECORD_STRIDE = 12
FLAG_SET_BIT = 0x10000  # bit 16 of the motion structure's version word

# Float constants read from eboot.elf (VA → file +0x4000); see docs/notes/re2-flagset.md.
C_INV_32767 = 3.05185094e-05  # 0x0126b2ac / 0x0126b2dc — int16 → [-1,1]
C_INV_255 = 0.00392156886  # 0x0126b2c0 — packed 9-bit → [-1,1]
C_INV_7 = 0.142857149  # 0x0126b2c4 — 4-bit residual → [-4/7, 4/7]
C_INV_2047 = 0.000488519785  # 0x0126b2e4 — packed 11-bit
C_INV_1023 = 0.000977517106  # 0x0126b2e8 / 0x0126b2b4 — packed 10/11-bit
C_INV_511 = 0.001956947147846222  # 0x0126b2b8 — the 0xe y/z scale (10-bit fields)
C_INV_31 = 0.0322580636  # 0x0126b2ec — packed 5/6-bit
C_HALF = 0.5  # 0x0126b2b0 — the 0xd angle pre-scale
M32 = 0xFFFFFFFF

# ---------------------------------------------------------------------------------
# The sampler's per-type dispatch — FUN_005fd900, switch on `*out - 1`.
#
# Each entry: (case label, handler / body, what the body reads).  Measured from the
# decompile (out/re/log-flagset-1.log, docs/notes/re2-flagset.md §4).  Types not
# listed fall to the default (no-op) case, notably type 8 and types ≥ 49.
DISPATCH = {
    1: ("case 0 (inline)", "FUN_0060ce90(t, out+0x20, out+0x10, out+0x30, p2, frames)",
        "p2 blob; flags from blob word0 >>0x18; OBJANIMTYPE_OBJECT"),
    2: ("caseD_1", "FUN_00602040(t, out, out+0x10, p2, frames)",
        "p2 blob; byte blob+3 → the flag table at 0x0126be70"),
    3: ("caseD_2", "FUN_0060e0c0(out+0x10, blob_w0>>8 & 0xff, blob+4, key, key+1); "
                   "FUN_0060e640(out+0x20, blob byte0, blob+8)",
        "p2 blob as a 3-pointer descriptor block"),
    4: ("caseD_3", "FUN_006011f0(t, out, out+0x10, p2, frames)", "p2 blob"),
    5: ("case 4 (inline)", "FUN_00600810(t, motion, 0, out+0x20, out+0x10, out+0x30, track)",
        "p2 blob; dissolve byte array at blob+0x10 indexed by frame; "
        "OBJANIMTYPE_OBJECT_WITH_DISSOLVE"),
    6: ("caseD_5", "FUN_00601590(t, out+0x10, p2, frames)", "p2 blob"),
    7: ("caseD_6", "FUN_00602040(t, out, out+0x10, p2, frames)", "p2 blob"),
    9: ("caseD_8", "FUN_0060e0c0(out+0x10, byte blob+2, blob+4, key, key+1); "
                   "FUN_0060bba0(out+0x20, blob_w0>>8 & 0xff, blob+8, key); "
                   "FUN_0060e370(t, t2, out+0x30, blob byte0, blob+12)",
        "p2 blob as a 4-pointer block"),
    10: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    11: ("caseD_1", "FUN_00602040(t, out, out+0x10, p2, frames)",
         "p2 blob; byte blob+3 → the flag table at 0x0126be70"),
    12: ("caseD_b", "FUN_0060e0c0(out+0x10, blob_w0>>4 & 0xff, blob+4, key, key+1); "
                    "FUN_0060e370(t, t2, out+0x20, blob byte0, blob+8)",
         "p2 blob as a 3-pointer block"),
    13: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    14: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    15: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    16: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    17: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    18: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    19: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    20: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    21: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    22: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    23: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    24: ("caseD_17", "FUN_0060e0c0(out+0x10, ushort blob+2 & 0xf, blob+4); "
                     "FUN_0060bba0(out+0x20, blob_w0>>8 & 0xf, blob+8, key, key+1); "
                     "FUN_0060e640(out+0x30, blob_w0 & 0xf, blob+0xc); "
                     "out[0x10] = *(int *)(blob+0x10)",
         "p2 blob; an extra int at blob+0x10"),
    25: ("caseD_e", "FUN_00601930(t, out+0x10, p2, frames)", "p2 blob"),
    26: ("caseD_19", "FUN_0060bba0(out+0x10, blob_w0>>8 & 0xff, blob+8, key); "
                     "FUN_0060e640(out+0x20, blob byte0, blob+0xc); bytes blob+0x10..0x13",
         "p2 blob; trailing bytes → out[0xb..0xd], out[0x35]"),
    27: ("caseD_5", "FUN_00601590(t, out+0x10, p2, frames)", "p2 blob"),
    28: ("caseD_5", "FUN_00601590(t, out+0x10, p2, frames)", "p2 blob"),
    29: ("caseD_1c", "two branches on motion version < 0xb vs >= 0xb",
         "p2 blob as a 4-word + 4-pointer block"),
    30: ("caseD_1d", "FUN_0060e0c0(out+0x10, char blob+2, blob+4, key, key+1); "
                     "FUN_0060bba0(out+0x20, char blob+1, blob+8, key)",
         "p2 blob; formats are signed bytes at blob+1/+2"),
    31: ("caseD_1d", "FUN_0060e0c0(out+0x10, char blob+2, blob+4, key, key+1); "
                     "FUN_0060bba0(out+0x20, char blob+1, blob+8, key)",
         "p2 blob; formats are signed bytes at blob+1/+2"),
    32: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    33: ("caseD_20", "FUN_00601c80(t, out+0x10, p2, frames)", "p2 blob"),
    34: ("caseD_20", "FUN_00601c80(t, out+0x10, p2, frames)", "p2 blob"),
    35: ("caseD_20", "FUN_00601c80(t, out+0x10, p2, frames)", "p2 blob"),
    36: ("case 0x23 (inline)", "FUN_00600810(t, motion, 0, out+0x20, out+0x10, out+0x30, track); "
                               "camera id *(int *)(motion+8 + track*4); flag 0xffffff < *(u32 *)p2; "
                               "FUN_00600d70(t, 0, motion, \"camera_0\", viewAngle/aspectRatio/timeScale)",
         "p2 blob word0; motion+0x08 array; OBJANIMTYPE_CAMERA"),
    37: ("case 0x24 (inline)", "FUN_00600810(t, motion, 0, out+0x20, out+0x10, 0, track); "
                               "camera id *(int *)(motion+8 + track*4)",
         "p2 blob; motion+0x08 array; OBJANIMTYPE_CAMERA_TARGET"),
    38: ("caseD_8", "FUN_0060e0c0(out+0x10, byte blob+2, blob+4, key, key+1); "
                    "FUN_0060bba0(out+0x20, blob_w0>>8 & 0xff, blob+8, key); "
                    "FUN_0060e370(t, t2, out+0x30, blob byte0, blob+12)",
         "p2 blob as a 4-pointer block"),
    39: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    40: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    41: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    42: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    43: ("caseD_2a", "FUN_0060e0c0(out+0x10, w0>>15 & 0x1f, blob+4); "
                     "FUN_0060bba0(out+0x20, w0>>10 & 0x1f, blob+8); "
                     "FUN_0060e640(out+0x30, w0>>5 & 0x1f, blob+0xc); "
                     "FUN_0060e640(out+0x40, w0 & 0x1f, blob+0x10); out[0x14] = w0>>0x14",
         "p2 blob as a 5-pointer block; 5-bit formats packed in word0"),
    44: ("caseD_2a", "as type 43", "p2 blob as a 5-pointer block"),
    45: ("caseD_2c", "FUN_0060e0c0(out+0x10, w0>>12 & 0xf, blob+4, key, key+1); "
                     "FUN_0060bba0(out+0x20, w0>>8 & 0xf, blob+8, key); "
                     "FUN_0060e370(t, t2, out+0x30, w0>>4 & 0xf, blob+0xc); "
                     "out[0x14] = dissolve(blob+0x10); byte blob+3 → flag table",
         "p2 blob as a 4-pointer block"),
    46: ("caseD_9", "FUN_00600ef0(t, out+0x10, p2, frames)", "p2 blob (formats/A/B/C)"),
    47: ("caseD_2a", "as type 43", "p2 blob as a 5-pointer block"),
    48: ("caseD_2f", "FUN_006011f0(t, out, out+0x10, p2, frames); "
                     "out[0x14..0x17] = *(int *)(blob+0x14..0x1c); out[0x17] = 1.0f",
         "p2 blob; trailing ints"),
}

TYPE13_FAMILY = tuple(sorted(t for t, d in DISPATCH.items() if d[0] == "caseD_9"))


def die(msg, code=2):
    sys.stderr.write("records: %s\n" % msg)
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


def expand(args, limit=None):
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
    with open(path, "rb") as fh:
        return fh.read()


# ------------------------------------------------------------------- primitives


def u16le(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32le(data, off):
    return struct.unpack_from("<I", data, off)[0]


def s16le(data, off):
    return struct.unpack_from("<h", data, off)[0]


def f32le(data, off):
    return struct.unpack_from("<f", data, off)[0]


def i32(value):
    """C int32 wrap of a 32-bit value."""
    value &= M32
    return value - (1 << 32) if value & 0x80000000 else value


def ash(value, bits):
    """C arithmetic right shift on a 32-bit int (sign-preserving)."""
    return i32(value) >> bits


def abs_ptr(value, data_base):
    """A 31-bit field → file offset.  Bit 31 = 'base-relative' (load-time marker);
    the file-level resolution is always data_base + (value & 0x7fffffff)."""
    return data_base + (value & 0x7FFFFFFF)


class ParseError(Exception):
    pass


# ------------------------------------------------------------ the motion structure


class Motion(object):
    """The flag-set motion structure (measured; see docs/notes/re2-flagset.md §2)."""

    __slots__ = ("file_off", "version", "flag_set", "frames", "aux8_off", "pre_off",
                 "records_off", "tracks", "count18", "off1c")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


def parse_motion(data):
    """Locate and parse the motion structure; raises ParseError when implausible."""
    if len(data) < DATA_BASE_DEFAULT or data[:4] != XFF_MAGIC:
        raise ParseError("not an xff container")
    data_base = u32le(data, 0xB4) if len(data) >= 0xB8 else DATA_BASE_DEFAULT
    if not (0x100 <= data_base < len(data)):
        data_base = DATA_BASE_DEFAULT
    file_off = data_base + u32le(data, 0x10)
    if file_off + 0x20 > len(data):
        raise ParseError("motion structure out of range (%#x)" % file_off)
    word0 = u32le(data, file_off)
    version = word0 & 0xFF
    if not (1 <= version <= 15):
        raise ParseError("implausible version byte %d at %#x" % (version, file_off))
    frames = u16le(data, file_off + 4)
    tracks = u32le(data, file_off + 0x14)
    if not (1 <= frames <= 0xFFFF and 1 <= tracks <= 4096):
        raise ParseError("implausible frames/tracks (%d/%d)" % (frames, tracks))
    m = Motion(file_off=file_off, version=version,
               flag_set=bool(word0 & FLAG_SET_BIT), frames=frames,
               aux8_off=u32le(data, file_off + 0x08),
               pre_off=u32le(data, file_off + 0x0C),
               records_off=u32le(data, file_off + 0x10),
               tracks=tracks, count18=u32le(data, file_off + 0x18),
               off1c=u32le(data, file_off + 0x1C))
    if m.flag_set:
        end = data_base + m.records_off + tracks * RECORD_STRIDE
        if m.records_off < 0x20 or end > len(data):
            raise ParseError("record array out of range (off %#x, %d tracks)"
                             % (m.records_off, tracks))
    return m, data_base


class Record(object):
    __slots__ = ("track", "type", "flags", "p1", "p2", "p1_off", "p2_off")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


def read_records(data, motion, data_base):
    """The flag-set 12-byte record array, stride 0xC, count = motion.tracks.

    Flag-clear files have no record array here (their ``+0x10`` array is a u32
    blob-pointer array read at stride 4 by the direct FUN_0060bba0 loop), so this
    raises for them instead of returning garbage.
    """
    if not motion.flag_set:
        raise ParseError("flag-clear variant: no 12-byte record array at +0x10")
    rec = data_base + motion.records_off
    out = []
    for k in range(motion.tracks):
        off = rec + k * RECORD_STRIDE
        word0 = u32le(data, off)
        p1 = u32le(data, off + 4)
        p2 = u32le(data, off + 8)
        out.append(Record(track=k, type=word0 & 0xFF, flags=word0 & ~0xFF,
                          p1=p1, p2=p2,
                          p1_off=abs_ptr(p1, data_base),
                          p2_off=abs_ptr(p2, data_base)))
    return out


def data_region_end(data, data_base):
    """Table 1's start — the end of the data region (u32 @0xD4, untagged mirror)."""
    t1p = u32le(data, 0xD4)
    if not (data_base < t1p <= len(data)):
        t1p = len(data)
    return t1p


def c_string(data, off, limit=128):
    """A NUL-terminated printable string at a file offset, or None."""
    if not (0 <= off < len(data)):
        return None
    end = data.find(b"\x00", off, min(off + limit, len(data)))
    if end < 0:
        return None
    raw = data[off:end]
    if not raw or any(b < 0x20 or b > 0x7E for b in raw):
        return None
    return raw.decode("ascii")


# ------------------------------------------------------------- the key blob


class Blob(object):
    __slots__ = ("off", "word0", "a", "b", "c", "a_off", "b_off", "c_off",
                 "fmt_a", "fmt_b", "fmt_c")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


def read_blob(data, record, data_base):
    off = record.p2_off
    if off + 16 > len(data):
        raise ParseError("track %d: blob out of range (%#x)" % (record.track, off))
    w0 = u32le(data, off)
    a = u32le(data, off + 4)
    b = u32le(data, off + 8)
    c = u32le(data, off + 0xC)
    return Blob(off=off, word0=w0, a=a, b=b, c=c,
                a_off=abs_ptr(a, data_base),
                b_off=abs_ptr(b, data_base),
                c_off=abs_ptr(c, data_base),
                fmt_a=(w0 >> 16) & 0xF, fmt_b=(w0 >> 8) & 0xF, fmt_c=w0 & 0xF)


# ------------------------------------------------------- the three key accessors


def _desc_ptr(data, data_base, slot):
    """A descriptor's pointer slot (stored in a float slot; read via ABS())."""
    return data_base + (slot & 0x7FFFFFFF)


def sample_e0c0(data, data_base, fmt, desc, key):
    """FUN_0060e0c0 — the vector channel.  Returns (x, y, z)."""
    if fmt == 0:
        return (f32le(data, desc), f32le(data, desc + 4), f32le(data, desc + 8))
    if fmt == 2:
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 24))
        out = []
        for i in range(3):
            s = s16le(data, ptr + key * 6 + 2 * i)
            out.append(f32le(data, desc + 8 * i) + s * f32le(data, desc + 4 + 8 * i) * C_INV_32767)
        return tuple(out)
    o = desc + key * 12
    return (f32le(data, o), f32le(data, o + 4), f32le(data, o + 8))


def sample_bba0(data, data_base, fmt, desc, key):
    """FUN_0060bba0 — the quaternion channel.  Returns (x, y, z, w)."""
    if fmt == 0:
        return (f32le(data, desc), f32le(data, desc + 4),
                f32le(data, desc + 8), f32le(data, desc + 12))
    if fmt == 1:
        o = desc + key * 16
        x, y, z = f32le(data, o + 4), f32le(data, o + 8), f32le(data, o + 12)
        w = math.sqrt(abs(1.0 - min(1.0, x * x + y * y + z * z)))
        if data[o + 1] & 0x80:
            w = -w
        return (x, y, z, w)
    if fmt == 2:
        ax, ay, az = (f32le(data, desc + 4 * i) for i in range(3))
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 12))
        ang = f32le(data, ptr + key * 4)
        s, c = math.sin(ang), math.cos(ang)
        return (ax * s, ay * s, az * s, c)
    if fmt in (3, 4, 5, 6):
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 32))
        word = u32le(data, ptr + key * 4)
        if fmt == 3:
            fx, fy, fz = ash(word << 5, 23), ash(word << 14, 23), ash(word << 23, 23)
            res = ash((word * 2) & M32, 28)
            sign = (word >> 31) & 1
        elif fmt == 4:
            fx, fy, fz = ash(word, 23), ash(word << 14, 23), ash(word << 23, 23)
            res = ash(word << 10, 28)
            sign = (word >> 22) & 1
        elif fmt == 5:
            fx, fy, fz = ash(word, 23), ash(word << 9, 23), ash(word << 23, 23)
            res = ash(word << 19, 28)
            sign = (word >> 13) & 1
        else:
            fx, fy, fz = ash(word, 23), ash(word << 9, 23), ash(word << 18, 23)
            res = ash(word << 28, 28)
            sign = (word >> 4) & 1
        x = fx * f32le(data, desc + 4) * C_INV_255 + f32le(data, desc)
        y = fy * f32le(data, desc + 12) * C_INV_255 + f32le(data, desc + 8)
        z = fz * f32le(data, desc + 20) * C_INV_255 + f32le(data, desc + 16)
        comp = 1.0 - (x * x + y * y + z * z)
        der = math.sqrt(abs(comp)) if comp >= 0.0 else 0.0
        if sign:
            der = -der
        out = [0.0, 0.0, 0.0, 0.0]
        slots = [i for i in range(4) if i != fmt - 3]
        for slot, value in zip(slots, (x, y, z)):
            out[slot] = value
        out[fmt - 3] = (f32le(data, desc + 24)
                        + res * f32le(data, desc + 28) * C_INV_7 + der)
        return tuple(out)
    if fmt == 10:
        o = desc + key * 12
        x, y, z = f32le(data, o), f32le(data, o + 4), f32le(data, o + 8)
        w = math.sqrt(abs(1.0 - (x * x + y * y + z * z)))
        if u32le(data, o) & 1:
            w = -w
        return (x, y, z, w)
    if fmt == 0xB:
        o = desc + key * 6
        x = s16le(data, o) * C_INV_32767
        y = s16le(data, o + 2) * C_INV_32767
        z = s16le(data, o + 4) * C_INV_32767
        w = math.sqrt(abs(1.0 - (x * x + y * y + z * z)))
        if u16le(data, o) & 1:
            w = -w
        return (x, y, z, w)
    if fmt == 0xC:
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 24))
        o = ptr + key * 6
        x = s16le(data, o) * f32le(data, desc + 4) * C_INV_32767 + f32le(data, desc)
        y = s16le(data, o + 2) * f32le(data, desc + 12) * C_INV_32767 + f32le(data, desc + 8)
        z = s16le(data, o + 4) * f32le(data, desc + 20) * C_INV_32767 + f32le(data, desc + 16)
        w = math.sqrt(abs(1.0 - (x * x + y * y + z * z)))
        if u16le(data, o) & 1:
            w = -w
        return (x, y, z, w)
    if fmt == 0xD:
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 20))
        ang = (s16le(data, ptr + key * 2) * f32le(data, desc + 4) * C_INV_32767
               + f32le(data, desc)) * C_HALF
        s, c = math.sin(ang), math.cos(ang)
        return (f32le(data, desc + 8) * s, f32le(data, desc + 12) * s,
                f32le(data, desc + 16) * s, c)
    if fmt == 0xE:
        # 0xe: arithmetic shifts (SAR); x is an 11-bit field (scale 1/1023), y/z are
        # 10-bit fields (scale 1/511 = 0x0126b2b8); sign bit = bit 0 of the word.
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 24))
        word = u32le(data, ptr + key * 4)
        x = ash(word, 21) * f32le(data, desc + 4) * C_INV_1023 + f32le(data, desc)
        y = ash(word << 11, 22) * f32le(data, desc + 12) * C_INV_511 + f32le(data, desc + 8)
        z = ash(word << 21, 22) * f32le(data, desc + 20) * C_INV_511 + f32le(data, desc + 16)
        w = math.sqrt(abs(1.0 - (x * x + y * y + z * z)))
        if not (~((i32(word) * 2) & M32) & 2):
            w = -w
        return (x, y, z, w)
    return (0.0, 0.0, 0.0, 0.0)  # > 0xe → the engine's zero case


def sample_e640(data, data_base, fmt, desc, key):
    """FUN_0060e640 — the second vector channel.  Returns (x, y, z)."""
    if fmt == 0:
        return (f32le(data, desc), f32le(data, desc + 4), f32le(data, desc + 8))
    if fmt == 2:
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 24))
        word = u32le(data, ptr + key * 4)
        return (f32le(data, desc) + (word >> 0x15) * f32le(data, desc + 4) * C_INV_2047,
                f32le(data, desc + 8) + ((word >> 10) & 0x7FF) * f32le(data, desc + 12) * C_INV_2047,
                f32le(data, desc + 16) + (word & 0x3FF) * f32le(data, desc + 20) * C_INV_1023)
    if fmt == 3:
        ptr = _desc_ptr(data, data_base, u32le(data, desc + 24))
        word = u16le(data, ptr + key * 2)
        return (f32le(data, desc) + (word >> 11) * f32le(data, desc + 4) * C_INV_31,
                f32le(data, desc + 8) + ((word >> 5) & 0x3F) * f32le(data, desc + 12) * C_INV_31,
                f32le(data, desc + 16) + (word & 0x1F) * f32le(data, desc + 20) * C_INV_31)
    o = desc + key * 12
    return (f32le(data, o), f32le(data, o + 4), f32le(data, o + 8))


def blend(frac, k0, k1):
    """The sampler's linear blend: frac * key_{i+1} + (1 - frac) * key_i."""
    if frac == 0.0:
        return k0
    return tuple(frac * b + (1.0 - frac) * a for a, b in zip(k0, k1))


def decode_type13_track(data, motion, data_base, track, frames=None):
    """The FUN_00600ef0 path: p2 blob → (A vector, B quaternion, C vector) per frame.

    Returns a list of per-frame dicts.  Integer frames read key i alone (frac = 0),
    which is what the runtime does for exact frame times.
    """
    frames = frames or motion.frames
    records = read_records(data, motion, data_base)
    if not (0 <= track < len(records)):
        raise ParseError("track %d out of range (0..%d)" % (track, len(records) - 1))
    rec = records[track]
    if rec.type not in TYPE13_FAMILY:
        raise ParseError("track %d is type %d, not in the caseD_9 family" % (track, rec.type))
    blob = read_blob(data, rec, data_base)
    rows = []
    for i in range(frames):
        a = sample_e0c0(data, data_base, blob.fmt_a, blob.a_off, i)
        b = sample_bba0(data, data_base, blob.fmt_b, blob.b_off, i)
        c = sample_e640(data, data_base, blob.fmt_c, blob.c_off, i)
        rows.append({"frame": i, "a": a, "b": b, "c": c})
    return rec, blob, rows


# --------------------------------------------------------------------- helpers


def norm3(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def norm4(v):
    return math.sqrt(sum(x * x for x in v))


def smoothness(series):
    """(max adjacent |delta| / component range, per component) over a 3-series."""
    if len(series) < 2:
        return [0.0, 0.0, 0.0]
    out = []
    for j in range(3):
        rng = max(s[j] for s in series) - min(s[j] for s in series)
        if rng <= 1e-12:
            out.append(0.0)
            continue
        mx = max(abs(series[i + 1][j] - series[i][j]) for i in range(len(series) - 1))
        out.append(mx / rng)
    return out


def scan_file(data):
    """Parse one file's motion structure.  Returns (motion, data_base, recs, t1p);
    `recs` is None for the flag-clear variant (no 12-byte record array)."""
    motion, data_base = parse_motion(data)
    recs = read_records(data, motion, data_base) if motion.flag_set else None
    t1p = data_region_end(data, data_base)
    return motion, data_base, recs, t1p


# ------------------------------------------------------------------- CLI modes


def cmd_dump(args):
    for path in expand(args.files):
        data = read_bytes(path)
        try:
            motion, data_base = parse_motion(data)
        except ParseError as exc:
            print("%s: %s" % (os.path.basename(path), exc))
            continue
        print("%s" % path)
        print("  container: %d B, data base %#x" % (len(data), data_base))
        print("  motion @%#x: version %d%s, %d frames, %d tracks, aux8 %#x, records %#x"
              % (motion.file_off, motion.version, " FLAG-SET" if motion.flag_set else "",
                 motion.frames, motion.tracks, motion.aux8_off, motion.records_off))
        if not motion.flag_set:
            print("  flag-clear variant: +0x10 is the u32 blob-pointer array "
                  "(no 12-byte records; this tool reads the flag-set variant)")
            continue
        recs = read_records(data, motion, data_base)
        hist = {}
        for r in recs:
            hist[r.type] = hist.get(r.type, 0) + 1
        print("  record types: %s" % ", ".join("%d×%d" % (n, t)
                                               for t, n in sorted(hist.items())))
        for r in recs:
            name = c_string(data, r.p1_off)
            entry = DISPATCH.get(r.type)
            extra = ""
            if r.type in TYPE13_FAMILY:
                blob = read_blob(data, r, data_base)
                extra = " blob=%#x formats=(%#x,%#x,%#x) A=%#x B=%#x C=%#x" % (
                    blob.off, blob.fmt_a, blob.fmt_b, blob.fmt_c,
                    blob.a_off, blob.b_off, blob.c_off)
            print("    track %2d: type %2d  p1=%#08x%s  p2=%#08x  %s%s"
                  % (r.track, r.type, r.p1,
                     " %r" % name if name else "", r.p2,
                     entry[1] if entry else "(default: no-op)", extra))


def cmd_decode(args):
    path = resolve(args.file)
    data = read_bytes(path)
    try:
        motion, data_base = parse_motion(data)
    except ParseError as exc:
        die("%s: %s" % (os.path.basename(path), exc), 1)
    if not motion.flag_set:
        die("%s: not a flag-set file (version word %#x)"
            % (os.path.basename(path), u32le(data, motion.file_off)), 1)
    rec, blob, rows = decode_type13_track(data, motion, data_base, args.track)
    print("%s track %d: type %d, %d frames, blob @%#x formats=(%#x, %#x, %#x)"
          % (os.path.basename(path), args.track, rec.type, motion.frames, blob.off,
             blob.fmt_a, blob.fmt_b, blob.fmt_c))
    name = c_string(data, rec.p1_off)
    if name:
        print("  p1 name: %r" % name)
    for row in rows:
        print("  frame %3d: A=(% .6f, % .6f, % .6f) B=(% .6f, % .6f, % .6f, % .6f) |B|=%.6f "
              "C=(% .6f, % .6f, % .6f)"
              % (row["frame"], row["a"][0], row["a"][1], row["a"][2],
                 row["b"][0], row["b"][1], row["b"][2], row["b"][3],
                 norm4(row["b"]), row["c"][0], row["c"][1], row["c"][2]))
    sm_a = smoothness([r["a"] for r in rows])
    sm_c = smoothness([r["c"] for r in rows])
    print("  smoothness (max adjacent delta / range): A=%s C=%s"
          % (["%.4f" % x for x in sm_a], ["%.4f" % x for x in sm_c]))
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"file": path, "track": args.track, "type": rec.type,
                       "frames": motion.frames, "p1_name": name,
                       "blob": {"off": blob.off, "formats": [blob.fmt_a, blob.fmt_b, blob.fmt_c],
                                "a": blob.a_off, "b": blob.b_off, "c": blob.c_off},
                       "keys": [{"frame": r["frame"], "a": r["a"], "b": r["b"], "c": r["c"]}
                                for r in rows]}, fh, indent=1)
        print("  json: %s" % args.json)


def cmd_scan(args):
    files = expand(args.patterns, args.limit)
    stats = {"files": 0, "xff": 0, "flag_set": 0, "flag_clear": 0, "parse_errors": 0,
             "type_hist": {}, "t13_files": 0, "t13_tracks": 0, "t13_decode_errors": 0}
    errors = []
    for path in files:
        stats["files"] += 1
        data = read_bytes(path)
        if data[:4] != XFF_MAGIC:
            continue
        stats["xff"] += 1
        try:
            motion, data_base, recs, t1p = scan_file(data)
        except ParseError as exc:
            stats["parse_errors"] += 1
            errors.append((path, str(exc)))
            continue
        if motion.flag_set:
            stats["flag_set"] += 1
        else:
            stats["flag_clear"] += 1
        if not motion.flag_set:
            continue
        has13 = False
        for r in recs:
            stats["type_hist"][r.type] = stats["type_hist"].get(r.type, 0) + 1
            if r.type in TYPE13_FAMILY and r.type == 13:
                has13 = True
                stats["t13_tracks"] += 1
                try:
                    decode_type13_track(data, motion, data_base, r.track)
                except (ParseError, struct.error, IndexError, ValueError) as exc:
                    stats["t13_decode_errors"] += 1
                    errors.append((path, "track %d: %s" % (r.track, exc)))
        if has13:
            stats["t13_files"] += 1
    print("scan: %d files, %d xff, %d flag-set, %d flag-clear, %d parse errors"
          % (stats["files"], stats["xff"], stats["flag_set"], stats["flag_clear"],
             stats["parse_errors"]))
    print("  type histogram: %s" % ", ".join(
        "%d:%d" % (t, n) for t, n in sorted(stats["type_hist"].items())))
    print("  type-13: %d tracks in %d files, %d decode errors"
          % (stats["t13_tracks"], stats["t13_files"], stats["t13_decode_errors"]))
    for path, msg in errors[:20]:
        print("  ERROR %s: %s" % (os.path.basename(path), msg))
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(stats, fh, indent=1, sort_keys=True)
        print("  json: %s" % args.json)
    return 1 if (stats["parse_errors"] or stats["t13_decode_errors"]) else 0


# ------------------------------------------------------------------ verification

LEO = "cp3shadow/anim/objanim_leo_a_jump_fall.anb"
CERBERUS = "cp3shadow/anim/objanim_cerberus_a_atk_near_l.anb"
ATH_DAMAGE = "cp3shadow/anim/objanim_ath_damage.anb"
CERBERUS_ANIM_TRACK = 0  # formats (2, 0, 3) — animated A + C
ATH_ANIM_TRACK = 4       # formats (0, 0, 1) — animated C, 60 keys


def verify(verbose=True):
    """The T2 pinned assertions.  Returns (passed, failed) counts."""
    passed = failed = 0
    lines = []

    def check(name, ok, detail=""):
        nonlocal passed, failed
        if ok:
            passed += 1
        else:
            failed += 1
        lines.append("%s  %s%s" % ("PASS" if ok else "FAIL", name,
                                   ("  — " + detail) if detail else ""))

    # -- 1. the leo sample: record roles -------------------------------------
    data = read_bytes(resolve(LEO))
    motion, data_base = parse_motion(data)
    recs = read_records(data, motion, data_base)
    check("leo: flag-set motion structure", motion.flag_set,
          "version word %#x" % u32le(data, motion.file_off))
    check("leo: 15 tracks, 21 frames", motion.tracks == 15 and motion.frames == 21,
          "%d tracks, %d frames" % (motion.tracks, motion.frames))
    check("leo: all records are type 13", all(r.type == 13 for r in recs),
          "types %s" % sorted({r.type for r in recs}))

    # p1 = name-string offsets (the memo's "p1 = k × frames" is REFUTED).
    names = [c_string(data, r.p1_off) if r.p1_off else None for r in recs]
    check("leo: every p1 resolves to a printable NUL-terminated string",
          all(n is not None for n in names), "%d/%d" % (sum(n is not None for n in names), len(recs)))
    cursor = 0
    tiled = True
    for r, n in zip(recs, names):
        if n is None or r.p1 != cursor:
            tiled = False
            break
        cursor += len(n) + 1
    check("leo: the p1 strings tile the data region from 0 (p1 = name offsets)",
          tiled, "last string ends at %#x" % cursor)
    k_times_frames_ok = all(r.p1 == r.track * motion.frames for r in recs[:6])
    k_times_frames_broken = recs[6].p1 != 6 * motion.frames
    check("leo: memo's p1 == k*frames holds only for k<=5 and fails at k=6 "
          "(claim refuted, string offsets instead)",
          k_times_frames_ok and k_times_frames_broken,
          "p1[6]=%#x vs 6*21=%#x" % (recs[6].p1, 6 * motion.frames))

    # -- 2. the leo decode: end-to-end, static, unit quaternions ---------------
    leo_rows = []
    for r in recs:
        rec, blob, rows = decode_type13_track(data, motion, data_base, r.track)
        leo_rows.append((rec, blob, rows))
    all_unit = all(abs(norm4(row["b"]) - 1.0) <= 1e-5
                   for _, _, rows in leo_rows for row in rows)
    check("leo: all 15 tracks decode; every B key is a unit quaternion",
          all_unit, "max |1-|q|| = %.2e" % max(abs(norm4(row["b"]) - 1.0)
                                               for _, _, rows in leo_rows for row in rows))
    static = all(len({tuple(row["a"]) for row in rows}) == 1
                 and len({tuple(row["b"]) for row in rows}) == 1
                 and len({tuple(row["c"]) for row in rows}) == 1
                 for _, _, rows in leo_rows)
    check("leo: format-0 tracks are static across all 21 frames", static,
          "formats %s" % sorted({(b.fmt_a, b.fmt_b, b.fmt_c) for _, b, _ in leo_rows}))

    # -- 3. an animated type-13 decode + smoothness ----------------------------
    d2 = read_bytes(resolve(ATH_DAMAGE))
    m2, base2 = parse_motion(d2)
    rec2, blob2, rows2 = decode_type13_track(d2, m2, base2, ATH_ANIM_TRACK)
    series_c = [row["c"] for row in rows2]
    sm = smoothness(series_c)
    check("ath_damage t4: C channel decodes to %d keys (fmt 1 float triplets)" % m2.frames,
          len(rows2) == 60 and blob2.fmt_c == 1, "formats=(%#x,%#x,%#x)" % (
              blob2.fmt_a, blob2.fmt_b, blob2.fmt_c))
    check("ath_damage t4: C channel is smooth (max adjacent delta <= 0.25 * range)",
          max(sm) <= 0.25, "max ratio %.4f" % max(sm))
    check("ath_damage t4: C channel is animated (>= 50 of %d keys change)" % m2.frames,
          sum(1 for i in range(len(series_c) - 1) if series_c[i] != series_c[i + 1]) >= 50,
          "%d changing steps" % sum(1 for i in range(len(series_c) - 1)
                                    if series_c[i] != series_c[i + 1]))

    d3 = read_bytes(resolve(CERBERUS))
    m3, base3 = parse_motion(d3)
    rec3, blob3, rows3 = decode_type13_track(d3, m3, base3, CERBERUS_ANIM_TRACK)
    sm3 = smoothness([row["a"] for row in rows3])
    sm3_steady = smoothness([row["a"] for row in rows3[1:]])
    check("cerberus t0: A channel (fmt 2 int16 triplets) decodes, 25 keys",
          len(rows3) == 25 and blob3.fmt_a == 2,
          "formats=(%#x,%#x,%#x)" % (blob3.fmt_a, blob3.fmt_b, blob3.fmt_c))
    check("cerberus t0: A channel is smooth after the measured key-0 jump "
          "(steady segment max ratio <= 0.5)",
          max(sm3_steady) <= 0.5,
          "full %.2f (key-0 jump), steady %.3f" % (max(sm3), max(sm3_steady)))
    check("cerberus t0: B quaternion unit, C channel (fmt 3) decodes",
          all(abs(norm4(row["b"]) - 1.0) <= 1e-4 for row in rows3)
          and len({tuple(row["c"]) for row in rows3}) > 1)

    # -- 3b. the 0xe packed-format self-check (in-memory; no game input) --------
    # No corpus track uses B fmt 0xe, so this pins the SAR field extraction and the
    # 1/511 (0x0126b2b8) y/z scale against the engine's arithmetic directly: for the
    # all-ones word the engine decodes a unit quaternion (the review's demo vector).
    buf = bytearray(64)
    struct.pack_into("<8f", buf, 0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0)
    struct.pack_into("<I", buf, 24, 32)  # desc[6] → the packed word at offset 32
    struct.pack_into("<I", buf, 32, 0xFFFFFFFF)
    q = sample_bba0(bytes(buf), 0, 0xE, 0, 0)
    expected = (-1.0 / 1023.0, -1.0 / 511.0, -1.0 / 511.0, -0.9999956926)
    check("0xe self-check: SAR shifts + 1/511 y/z scale (engine's demo vector)",
          max(abs(a - b) for a, b in zip(q, expected)) <= 1e-5
          and abs(norm4(q) - 1.0) <= 1e-5,
          "q=(%.6f, %.6f, %.6f, %.6f) |q|=%.6f" % (q + (norm4(q),)))

    # -- 4. p2/A/B/C plausibility across every type-13 track in the corpus -----
    files = expand(["cp3shadow/anim/*.anb"])
    bad = []
    bad_names = []
    t13 = 0
    fmt_hist = {}
    for path in files:
        d = read_bytes(path)
        if d[:4] != XFF_MAGIC:
            continue
        try:
            m, base, recsx, t1p = scan_file(d)
        except ParseError:
            continue
        if not m.flag_set:
            continue
        for r in recsx:
            if r.type != 13:
                continue
            t13 += 1
            blob = read_blob(d, r, base)
            for label, off in (("p2", r.p2_off), ("A", blob.a_off), ("B", blob.b_off),
                               ("C", blob.c_off)):
                if not (base <= off < t1p):
                    bad.append((path, r.track, label, off))
            if c_string(d, r.p1_off) is None:
                bad_names.append((path, r.track, r.p1))
            key = (blob.fmt_a, blob.fmt_b, blob.fmt_c)
            fmt_hist[key] = fmt_hist.get(key, 0) + 1
    check("corpus: every type-13 p1 resolves to a printable NUL-terminated string",
          not bad_names, "%d tracks, %d violations" % (t13, len(bad_names)))
    check("corpus: every type-13 p2/A/B/C pointer lands inside the data region",
          not bad, "%d tracks checked, %d violations" % (t13, len(bad)))
    check("corpus: every type-13 blob format nibble is within the vocabulary (<= 0xe)",
          all(f <= 0xE for combo in fmt_hist for f in combo),
          "%d format combos" % len(fmt_hist))

    # -- 5. the corpus scan: no crashes ----------------------------------------
    n_flag = n_files = n_parse_err = 0
    t13_decode_fail = []
    for path in files:
        d = read_bytes(path)
        if d[:4] != XFF_MAGIC:
            continue
        n_files += 1
        try:
            m, base, recsx, t1p = scan_file(d)
        except ParseError:
            n_parse_err += 1
            continue
        if m.flag_set:
            n_flag += 1
            for r in recsx:
                if r.type == 13:
                    try:
                        decode_type13_track(d, m, base, r.track)
                    except (ParseError, struct.error, IndexError, ValueError) as exc:
                        t13_decode_fail.append((path, r.track, str(exc)))
    check("corpus: all .anb parse as flag-set/flag-clear without errors",
          n_parse_err == 0, "%d files, %d flag-set, %d parse errors"
          % (n_files, n_flag, n_parse_err))
    check("corpus: every type-13 track decodes end-to-end without exceptions",
          not t13_decode_fail,
          "%d type-13 tracks, %d decode failures" % (t13, len(t13_decode_fail)))
    for path, track, msg in t13_decode_fail[:5]:
        check("corpus decode failure: %s track %d" % (os.path.basename(path), track),
              False, msg)

    if verbose:
        for line in lines:
            print(line)
        print("summary: %d/%d assertions pass" % (passed, passed + failed))
    return passed, failed


def cmd_verify(args):
    passed, failed = verify()
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"passed": passed, "failed": failed}, fh, indent=1)
    return 1 if failed else 0


# --------------------------------------------------------------------- argparse


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode")

    p = sub.add_parser("dump", help="structure + record dump of flag-set files")
    p.add_argument("files", nargs="+")
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser("decode", help="decode one type-13 track (the FUN_00600ef0 path)")
    p.add_argument("file")
    p.add_argument("--track", type=int, required=True)
    p.add_argument("--json")
    p.set_defaults(func=cmd_decode)

    p = sub.add_parser("scan", help="walk patterns; classify and decode")
    p.add_argument("patterns", nargs="+")
    p.add_argument("--limit", type=int)
    p.add_argument("--json")
    p.set_defaults(func=cmd_scan)

    p = sub.add_parser("verify", help="the T2 pinned assertions (exit 0/1)")
    p.add_argument("--json")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    if not getattr(args, "func", None):
        parser.print_help()
        return 2
    return args.func(args) or 0


if __name__ == "__main__":
    sys.exit(main())
