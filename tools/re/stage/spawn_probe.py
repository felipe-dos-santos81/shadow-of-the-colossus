#!/usr/bin/env python3
"""RE-4 probe: the bounded S4 MinA placement pull (read-only, static only).

S4.0 "The encounter" needs a MinA (Valus) spawn/placement record with a world
x/z + yaw, or a documented negative.  The candidates are the arena's compiled
scripts and the families already mined in RE-3:

  cgpr <file> [--json OUT]   the entity/placement graph (Oodle -> records + transforms)
  csml <file> [--json OUT]   the static scene instance list (transform + model refs)
  cwks [file] [--match S]    the workspace dependency pool (strings only)
  path <file>                 the .path nodes (the minotaur patrol path)
  ground <glb> X,Z...         downward ray-cast on a converted glb (floor height)
  pick <glb> X Z             placement check: ground, facing yaw, yawed AABB, floor range
  scan cgpr|csml [--pattern]  tree-wide sweep of the script families for a name
  arena                       run the whole bounded pull and print the acceptance table

What is measured here (2026-10-19, arena_01 family):

  * The `.cgpr`/`.csml`/`.csli`/`.cpid` files are Oodle `8c0a` containers
    (the last 4 bytes hold decompressed-size + 0x60000000); the probe
    decompresses through the vendored dylib
    (tools/native/liboo2core_8_win64_.dylib, personal use only).
  * A SOTC `.cgpr` entity record is laid out as measured:
        u32 0x10 | guid[16] | u8 nameLen | name | u32 0 | u32 count
        | 39-byte property block (GUID+u32, GUID+u24)
        | body
    The body starts at nameEnd + 47 (measured on all 21,524 named records in
    the 342 shipped .cgpr; every decoded transform is a proper rotation,
    det = 1.000000, max |row norm - 1| = 1.9e-7) and holds typed subobjects:
        u32 magic | u16 len (LE) | payload
    Two body magics are decoded here:
        0xF543030E len 48  -> transform (12 floats: 4 rows of 3; rows 0-2
                              basis, row 3 translation)
        0x2D870A80 len 12  -> vec3 (bounds/scale)
  * A `.csml` instance is a 48-byte transform followed by one subobject whose
    payload holds the 0x08-count and the `$/models/...` string refs; the file
    carries no colossus/minotaur ref in the arena sets (measured).

Inputs are read-only; derived dumps go to out/re/stage/ (gitignored).  No game
or port runs.  Stdlib only (+ ctypes for the vendored Oodle dylib).
"""

import argparse
import ctypes
import glob
import json
import math
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
OODLE_LIB = os.path.join(REPO, "tools", "native", "liboo2core_8_win64_.dylib")
DEFAULT_GAME_ROOT = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"

# measured SOTC cgpr body magics
MAGIC_TRANSFORM = 0xF543030E
MAGIC_VEC3 = 0x2D870A80
RECORD_PREFIX = b"\x10\x00\x00\x00"
RECORD_BODY_DELTA = 47      # nameEnd -> first body subobject
TRANSFORM_LEN = 48
VEC3_LEN = 12


def die(msg, code=2):
    sys.stderr.write("spawn_probe: %s\n" % msg)
    raise SystemExit(code)


def game_root():
    root = os.environ.get("SOTC_GAME_ROOT", DEFAULT_GAME_ROOT)
    if not os.path.isdir(root):
        die("SOTC_GAME_ROOT does not exist: %s (set the env var)" % root)
    return root


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


# ---------------------------------------------------------------- Oodle ----

_oodle = None


def _oodle_fn():
    global _oodle
    if _oodle is None:
        if not os.path.exists(OODLE_LIB):
            die("vendored Oodle dylib missing: %s" % OODLE_LIB)
        lib = ctypes.CDLL(OODLE_LIB)
        fn = lib.OodleLZ_Decompress
        fn.restype = ctypes.c_long
        fn.argtypes = [
            ctypes.c_char_p, ctypes.c_long, ctypes.c_char_p, ctypes.c_long,
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
            ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_long, ctypes.c_int,
        ]
        _oodle = fn
    return _oodle


def decompress(data):
    """Return (bytes, compressed). Oodle 8c0a/8cc/68c/6cc -> body + 12-byte footer.

    Mirrors the vendored CompressionHandler: the decompressed payload is
    followed by the container's last 12 bytes (the PRPG/TILS footer).
    """
    if len(data) < 16:
        return data, False
    cc = struct.unpack_from("<H", data, 0)[0]
    base = {0xA8C: 0x60000000, 0xACC: 0x60000000,
            0x68C: 0x80000000, 0x6CC: 0x80000000}.get(cc)
    if base is None:
        return data, False
    declen = struct.unpack_from("<I", data, len(data) - 4)[0] - base
    if declen <= 0 or declen > (1 << 31):
        return data, False
    out = ctypes.create_string_buffer(declen)
    got = _oodle_fn()(data, len(data), out, declen, 1, 0, 0, None, 0, None, None, None, 0, 0)
    if got != declen:
        die("Oodle decompression failed (got %d, want %d)" % (got, declen))
    body = out.raw[:declen] + data[-12:]
    return body, True


def load(path):
    return decompress(read_bytes(path))


# ----------------------------------------------------------------- cgpr ----

def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _f32(b, o):
    return struct.unpack_from("<f", b, o)[0]


def name_records(b):
    """All `10 00 00 00 | guid16 | len | name` record starts, validated.

    The pattern alone is not enough (the byte pair occurs in float data); a
    record also has u32 0 at nameEnd and a small count at nameEnd+4.
    """
    out = []
    for i in range(0, len(b) - 24):
        if b[i:i + 4] != RECORD_PREFIX:
            continue
        nl = b[i + 20]
        if not (2 <= nl <= 80):
            continue
        nm = b[i + 21:i + 21 + nl]
        if not all(32 <= c < 127 for c in nm) or not nm[:1].isalpha():
            continue
        name_end = i + 21 + nl
        if name_end + 8 > len(b):
            continue
        u0 = _u32(b, name_end)
        u1 = _u32(b, name_end + 4)
        if u0 != 0 or not (1 <= u1 <= 64):
            continue
        out.append({"offset": i, "name": nm.decode("ascii"),
                    "guid": b[i + 4:i + 20].hex(), "name_end": name_end,
                    "count": u1})
    return out


def decode_transform(b, o):
    """magic|len|12 floats -> dict. 4 rows of 3: rows 0-2 basis, row 3 translation."""
    if o + 6 + TRANSFORM_LEN > len(b):
        return None
    if _u32(b, o) != MAGIC_TRANSFORM or _u16(b, o + 4) != TRANSFORM_LEN:
        return None
    rows = [[_f32(b, o + 6 + 12 * r + 4 * c) for c in range(3)] for r in range(4)]
    t = rows[3]
    # Y-rotation yaw from the first basis row: atan2(z, x)
    yaw = math.degrees(math.atan2(rows[0][2], rows[0][0]))
    # orthonormality sanity: row norms and pairwise dots
    norms = [math.sqrt(sum(v * v for v in rows[r])) for r in range(3)]
    dots = [sum(rows[a][k] * rows[c][k] for k in range(3)) for a, c in ((0, 1), (0, 2), (1, 2))]
    det = (rows[0][0] * (rows[1][1] * rows[2][2] - rows[1][2] * rows[2][1])
           - rows[0][1] * (rows[1][0] * rows[2][2] - rows[1][2] * rows[2][0])
           + rows[0][2] * (rows[1][0] * rows[2][1] - rows[1][1] * rows[2][0]))
    return {"offset": o, "rows": rows, "translation": t, "yaw_deg": yaw,
            "row_norms": norms, "row_dots": dots, "det": det}


def decode_vec3(b, o):
    if o + 6 + VEC3_LEN > len(b):
        return None
    if _u32(b, o) != MAGIC_VEC3 or _u16(b, o + 4) != VEC3_LEN:
        return None
    v = [_f32(b, o + 6 + 4 * k) for k in range(3)]
    return {"offset": o, "value": v}


def record_body(b, rec):
    """Decode the first body subobjects of a record (vec3 optional, transform)."""
    body = rec["name_end"] + RECORD_BODY_DELTA
    out = {"body_offset": body, "vec3": None, "transform": None}
    vec = decode_vec3(b, body)
    if vec is not None:
        out["vec3"] = vec
        out["transform"] = decode_transform(b, body + 6 + VEC3_LEN)
    else:
        out["transform"] = decode_transform(b, body)
    return out


def parse_cgpr(path):
    raw = read_bytes(path)
    b, compressed = decompress(raw)
    body = b[:-12] if len(b) >= 12 else b
    recs = name_records(body)
    for r in recs:
        r.update(record_body(body, r))
    doc = {
        "file": path,
        "compressed": compressed,
        "compressed_size": len(raw),
        "decompressed_size": len(body),
        "footer": body[-12:].hex() if len(body) >= 12 else None,
        "records": recs,
    }
    return doc, body


def fmt_transform(tr):
    if not tr:
        return "-"
    t = tr["translation"]
    return "t=(%.3f, %.3f, %.3f) yaw=%.2f det=%.3f n=(%.4f,%.4f,%.4f)" % (
        t[0], t[1], t[2], tr["yaw_deg"], tr["det"],
        tr["row_norms"][0], tr["row_norms"][1], tr["row_norms"][2])


def cmd_cgpr(args):
    path = resolve(args.file)
    doc, body = parse_cgpr(path)
    print("cgpr %s  compressed=%s  %d -> %d B  footer=%s" % (
        path, doc["compressed"], doc["compressed_size"], doc["decompressed_size"], doc["footer"]))
    print("record                                 offset    guid                              count  vec3                       transform")
    for r in doc["records"]:
        v = r["vec3"]["value"] if r["vec3"] else None
        vtext = "(%.3f, %.3f, %.3f)" % tuple(v) if v else "-"
        print("%-38s 0x%06x  %s  %2d     %-24s %s" % (
            r["name"], r["offset"], r["guid"], r["count"], vtext, fmt_transform(r["transform"])))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(doc, f, indent=1)
        print("wrote %s" % args.json)
    return doc


# ----------------------------------------------------------------- csml ----

def parse_csml(path):
    raw = read_bytes(path)
    b, compressed = decompress(raw)
    body = b[:-12] if len(b) >= 12 else b
    count = _u32(body, 0)
    strings = []
    for m in re.finditer(rb"[ -~]{5,}", body):
        s = m.group().decode("ascii", "replace")
        cut = s.find("$/")
        if cut > 0:
            s = s[cut:]
        strings.append((m.start(), s))
    refs = [s for s in strings if s[1].startswith("$/models/") or s[1].startswith("$/scripts/")]
    modelos = [s for s in refs if s[1].startswith("$/models/")]
    hits = [s for s in refs if re.search(r"coloss|minotaur|mino", s[1], re.I)]
    # first instance: 48-byte transform then subobject (magic|len|payload)
    inst = {"transform": None, "sub_magic": None, "sub_len": None}
    if len(body) >= 0x34:
        tr = [[_f32(body, 4 + 12 * r + 4 * c) for c in range(3)] for r in range(4)]
        inst["transform"] = tr
        o = 0x04 + 48
        inst["sub_magic"] = _u32(body, o)
        inst["sub_len"] = _u16(body, o + 4)
    return {"file": path, "compressed": compressed, "count": count,
            "strings": len(strings), "model_refs": len(modelos),
            "refs": refs, "hits": hits, "first_instance": inst}, body


def cmd_csml(args):
    path = resolve(args.file)
    doc, body = parse_csml(path)
    print("csml %s  compressed=%s  count=%d  strings=%d  model_refs=%d" % (
        path, doc["compressed"], doc["count"], doc["strings"], doc["model_refs"]))
    fi = doc["first_instance"]
    if fi["transform"]:
        t = fi["transform"][3]
        print("first instance transform row3=(%.3f, %.3f, %.3f) sub magic=0x%08x len=%d" % (
            t[0], t[1], t[2], fi["sub_magic"], fi["sub_len"]))
    print("colossus/minotaur refs: %d" % len(doc["hits"]))
    for o, s in doc["hits"][:20]:
        print("  0x%06x %s" % (o, s))
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"file": doc["file"], "compressed": doc["compressed"],
                       "count": doc["count"], "strings": doc["strings"],
                       "model_refs": doc["model_refs"], "hits": doc["hits"]}, f, indent=1)
        print("wrote %s" % args.json)


# ----------------------------------------------------------------- cwks ----

def parse_cwks(path):
    b = read_bytes(path)
    if b[:4] != b"PSKW":
        die("not a PSKW workspace file: %s" % path)
    index_count = _u32(b, 0x0C)
    pool = 0x10 + index_count * 16
    entries = []
    pos = pool
    while pos < len(b):
        if b[pos] == 0x24:  # "$" -> bare entry
            end = b.find(b"\x00", pos)
            if end < 0:
                break
            entries.append((pos, b[pos:end].decode("ascii", "replace")))
            pos = end + 1
        else:
            ln = b[pos]
            if ln == 0:
                pos += 1
                continue
            payload = b[pos + 1:pos + 1 + ln]
            entries.append((pos, payload.decode("ascii", "replace")))
            pos += 1 + ln
    return {"index_count": index_count, "pool": pool, "entries": entries}, b


def cmd_cwks(args):
    path = args.file or os.path.join(game_root(), "workspaces", "_ps4", "shadow.cwks")
    path = resolve(path)
    doc, b = parse_cwks(path)
    match = args.match.lower()
    hits = [(o, s) for o, s in doc["entries"] if match in s.lower()]
    print("cwks %s  %d B  index=%d  entries=%d  match=%r hits=%d" % (
        path, len(b), doc["index_count"], len(doc["entries"]), args.match, len(hits)))
    for o, s in hits[:40]:
        print("  0x%06x %s" % (o, s))
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"file": path, "index_count": doc["index_count"],
                       "entries": len(doc["entries"]),
                       "hits": [{"offset": o, "path": s} for o, s in hits]}, f, indent=1)
        print("wrote %s" % args.json)


# ----------------------------------------------------------------- path ----

def parse_path(path):
    b = read_bytes(path)
    if b[:4] != b"qath":
        die("not a qath path file: %s" % path)
    size = _u32(b, 4)
    header = _u32(b, 8)
    count = _u32(b, header)
    nodes = []
    for i in range(count):
        o = header + 0x10 + i * 0x30      # measured stride: 48 B (3 floats + w)
        if o + 16 > len(b):
            break
        p = [_f32(b, o + 4 * k) for k in range(3)]
        w = _f32(b, o + 12)
        nodes.append({"offset": o, "pos": p, "w": w})
    return {"file": path, "size": size, "header": header, "count": count,
            "nodes": nodes}, b


def cmd_path(args):
    path = resolve(args.file)
    doc, b = parse_path(path)
    print("path %s  %d B  header=%d  nodes=%d" % (path, len(b), doc["header"], doc["count"]))
    for i, n in enumerate(doc["nodes"]):
        p = n["pos"]
        print("  node %2d @0x%04x  (%.3f, %.3f, %.3f)  w=%.3f" % (
            i, n["offset"], p[0], p[1], p[2], n["w"]))
    if doc["nodes"]:
        xs = [n["pos"][0] for n in doc["nodes"]]
        ys = [n["pos"][1] for n in doc["nodes"]]
        zs = [n["pos"][2] for n in doc["nodes"]]
        print("  bbox x[%.3f, %.3f] y[%.3f, %.3f] z[%.3f, %.3f]" % (
            min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))


# ----------------------------------------------------------------- glb -----

def load_glb(path):
    b = read_bytes(path)
    magic, ver, length = struct.unpack_from("<III", b, 0)
    if magic != 0x46546C67:
        die("not a glb: %s" % path)
    o = 12
    js = bin_ = None
    while o + 8 <= len(b):
        clen, ctype = struct.unpack_from("<II", b, o)
        payload = b[o + 8:o + 8 + clen]
        if ctype == 0x4E4F534A:
            js = json.loads(payload.decode("utf-8"))
        elif ctype == 0x4E4942:
            bin_ = payload
        o += 8 + clen
    return js, bin_


def glb_triangles(path):
    js, bin_ = load_glb(path)
    out = []
    for mesh in js.get("meshes", []):
        for prim in mesh["primitives"]:
            pa = prim["attributes"].get("POSITION")
            ia = prim.get("indices")
            if pa is None:
                continue
            acc = js["accessors"][pa]
            bv = js["bufferViews"][acc["bufferView"]]
            base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
            stride = bv.get("byteStride", 12)
            pos = [struct.unpack_from("<fff", bin_, base + i * stride) for i in range(acc["count"])]
            if ia is None:
                idx = list(range(len(pos)))
            else:
                iacc = js["accessors"][ia]
                ibv = js["bufferViews"][iacc["bufferView"]]
                ibase = ibv.get("byteOffset", 0) + iacc.get("byteOffset", 0)
                ctype = {5121: "<B", 5123: "<H", 5125: "<I"}[iacc["componentType"]]
                size = struct.calcsize(ctype)
                idx = [struct.unpack_from(ctype, bin_, ibase + i * size)[0] for i in range(iacc["count"])]
            for k in range(0, len(idx) - 2, 3):
                out.append((pos[idx[k]], pos[idx[k + 1]], pos[idx[k + 2]]))
    return out


def ground_height(tris, x, z):
    best = None
    for a, b, c in tris:
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


def cmd_ground(args):
    path = resolve(args.glb)
    tris = glb_triangles(path)
    print("glb %s  triangles=%d" % (path, len(tris)))
    for col in args.columns:
        x, z = col
        y = ground_height(tris, x, z)
        print("  column (%7.2f, %7.2f) -> floor %s" % (
            x, z, "%.3f m" % y if y is not None else "no hit"))


def _triangle_hits(tris, x, z):
    n = 0
    for a, b, c in tris:
        d = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2])
        if abs(d) < 1e-9:
            continue
        u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d
        v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / d
        if u < 0 or v < 0 or u + v > 1:
            continue
        n += 1
    return n


def cmd_pick(args):
    """Evaluate a placement: ground, facing yaw, yawed bind AABB, clearance, floor range."""
    ground = glb_triangles(resolve(args.glb))
    x, z = args.x, args.z
    sx, sz = args.spawn
    yaw = math.degrees(math.atan2(sx - x, sz - z))
    rad = math.radians(yaw)
    c, s = math.cos(rad), math.sin(rad)
    x0, x1, z0, z1 = args.extents
    corners = [(x + c * lx + s * lz, z - s * lx + c * lz)
               for lx in (x0, x1) for lz in (z0, z1)]
    ax0 = min(p[0] for p in corners)
    ax1 = max(p[0] for p in corners)
    az0 = min(p[1] for p in corners)
    az1 = max(p[1] for p in corners)
    gy = ground_height(ground, x, z)
    d = math.hypot(x - sx, z - sz)
    print("pick (%g, %g)  spawn (%g, %g)" % (x, z, sx, sz))
    print("  ground y        %s" % ("%.6f" % gy if gy is not None else "no hit"))
    print("  yaw (face spawn) %.6f deg  q=(0, %.6f, 0, %.6f)" % (
        yaw, math.sin(math.radians(yaw) / 2), math.cos(math.radians(yaw) / 2)))
    print("  yawed AABB      x[%.3f, %.3f] z[%.3f, %.3f]" % (ax0, ax1, az0, az1))
    print("  bowl margins    west %.2f  east %.2f  south %.2f  north %.2f" % (
        ax0 - BOWL["x"][0], BOWL["x"][1] - ax1, az0 - BOWL["z"][0], BOWL["z"][1] - az1))
    print("  distance        %.3f m" % d)
    for label, path in (("main", args.main), ("shared", args.shared)):
        if path:
            tris = glb_triangles(resolve(path))
            print("  %-6s hits     %d above the column" % (label, _triangle_hits(tris, x, z)))
    ys = []
    for i in range(5):
        lx = x0 + (x1 - x0) * i / 4.0
        for j in range(5):
            lz = z0 + (z1 - z0) * j / 4.0
            y = ground_height(ground, x + c * lx + s * lz, z - s * lx + c * lz)
            if y is not None:
                ys.append(y)
    if ys:
        print("  floor 5x5       %d/25 hits  %.3f..%.3f m" % (len(ys), min(ys), max(ys)))
    sy = math.degrees(math.atan2(x - sx, z - sz))
    sgy = ground_height(ground, sx, sz)
    print("  spawn: ground %s  yaw %.6f deg  q=(0, %.6f, 0, %.6f)" % (
        "%.6f" % sgy if sgy is not None else "no hit", sy,
        math.sin(math.radians(sy) / 2), math.cos(math.radians(sy) / 2)))


def cmd_scan(args):
    """Tree-wide sweep of the compiled-script families (read-only)."""
    root = game_root()
    pat = re.compile(args.pattern, re.I)
    if args.family == "cgpr":
        files = sorted(glob.glob(os.path.join(root, "scripts", "**", "*.cgpr"), recursive=True))
        total_recs = 0
        matches = []
        for p in files:
            doc, _ = parse_cgpr(p)
            total_recs += len(doc["records"])
            for r in doc["records"]:
                if pat.search(r["name"]):
                    matches.append((os.path.relpath(p, root), r))
        print("scan cgpr: %d files, %d records, %d name matches for %r" % (
            len(files), total_recs, len(matches), args.pattern))
        for rel, r in matches:
            t = r["transform"]["translation"] if r["transform"] else None
            print("  %-64s 0x%06x %-34s %s" % (
                rel, r["offset"], r["name"],
                "t=(%.2f, %.2f, %.2f)" % tuple(t) if t else "-"))
    elif args.family == "csml":
        files = sorted(glob.glob(os.path.join(root, "scripts", "**", "*.csml"), recursive=True))
        total_inst = 0
        matches = []
        for p in files:
            doc, _ = parse_csml(p)
            total_inst += doc["count"]
            for o, s in doc["refs"]:
                if pat.search(s):
                    matches.append((os.path.relpath(p, root), o, s))
        print("scan csml: %d files, %d instances, %d ref matches for %r" % (
            len(files), total_inst, len(matches), args.pattern))
        for rel, o, s in matches[:40]:
            print("  %-64s 0x%06x %s" % (rel, o, s))
    else:
        die("unknown family %r" % args.family)


# ----------------------------------------------------------------- misc ----

def resolve(arg):
    if os.path.exists(arg):
        return arg
    root = game_root()
    for base in ("scripts/shadow/world", "scripts", "cp3shadow", ""):
        p = os.path.join(root, base, arg)
        if os.path.exists(p):
            return p
    die("cannot resolve %s against %s" % (arg, root))


ARENA = [
    ("cgpr", "scripts/shadow/world/arena_01/_cmn/arena_01.cgpr"),
    ("cgpr", "scripts/shadow/world/arena_01/_cmn/arena_01_shared.cgpr"),
    ("cgpr", "scripts/shadow/world/arena_01_entrance/_cmn/arena_01_entrance.cgpr"),
    ("cgpr", "scripts/shadow/world/arena_01_entrance/_cmn/arena_01_entrance_shared.cgpr"),
    ("cgpr", "scripts/shadow/world/arena_01_entrance/_cmn/arena_01_entrance_low.cgpr"),
    ("cgpr", "scripts/shadow/entities/colossi/_cmn/colossus_01_minotaur_a.cgpr"),
    ("csml", "scripts/shadow/world/arena_01/_cmn/arena_01.csml"),
    ("csml", "scripts/shadow/world/arena_01/_cmn/arena_01_shared.csml"),
    ("csml", "scripts/shadow/world/arena_01_entrance/_cmn/arena_01_entrance.csml"),
]
PATHS = ["cp3shadow/path/pathdata/minotaur.path"]

# acceptance bar (design §7.3)
BOWL = {"x": (237.37, 431.39), "z": (297.56, 390.04), "y": (28.0, 31.0)}
PLAYER = (353.0, 345.0)
MAX_DIST = 100.0


def inside_bowl(t):
    return (BOWL["x"][0] <= t[0] <= BOWL["x"][1]
            and BOWL["z"][0] <= t[2] <= BOWL["z"][1])


def dist_player(t):
    return math.hypot(t[0] - PLAYER[0], t[2] - PLAYER[1])


def cmd_arena(args):
    root = game_root()
    rows = []
    for kind, rel in ARENA:
        p = os.path.join(root, rel)
        if not os.path.exists(p):
            print("missing: %s" % rel)
            continue
        if kind == "cgpr":
            doc, body = parse_cgpr(p)
            for r in doc["records"]:
                tr = r["transform"]
                t = tr["translation"] if tr else None
                rows.append({
                    "file": rel, "name": r["name"], "offset": r["offset"],
                    "count": r["count"], "transform": tr, "t": t,
                })
            print("%-72s records=%3d transforms=%3d" % (
                rel, len(doc["records"]), sum(1 for r in doc["records"] if r["transform"])))
        else:
            d, _ = parse_csml(p)
            print("%-72s instances=%d model_refs=%d colossus_refs=%d" % (
                rel, d["count"], d["model_refs"], len(d["hits"])))
    print()
    print("records with an encoded transform, acceptance-bar cross-check")
    print("(a) name/type   (b) bowl      (c) player d   y      x       z       yaw    verdict")
    for r in rows:
        t = r["t"]
        if not t:
            continue
        name = r["name"]
        mina = bool(re.search(r"mino|minotaur", name, re.I))
        typed_spawn = bool(re.search(r"spawn|placement|body", name, re.I))
        bowl = inside_bowl(t)
        near_floor = BOWL["y"][0] <= t[1] <= BOWL["y"][1]
        d = dist_player(t)
        verdict = []
        if not (mina and typed_spawn):
            verdict.append("(a) not a spawn/placement")
        if not bowl:
            verdict.append("(b) outside bowl")
        elif not near_floor:
            verdict.append("(b) y=%.1f not near floor" % t[1])
        if d > MAX_DIST:
            verdict.append("(b) >100 m")
        if not verdict:
            verdict.append("candidate")
        print("%-38s %-9s %-5s %8.2f  %7.2f %8.2f %8.2f %6.2f  %s" % (
            name, "Mino" if mina else "-", "yes" if bowl else "no", d,
            t[1], t[0], t[2], r["transform"]["yaw_deg"], "; ".join(verdict)))
    print()
    for rel in PATHS:
        p = os.path.join(root, rel)
        if os.path.exists(p):
            d, _ = parse_path(p)
            pts = [n["pos"] for n in d["nodes"]]
            print("path %s: %d nodes, bbox x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f] (frame has no anchor)" % (
                rel, len(pts), min(p[0] for p in pts), max(p[0] for p in pts),
                min(p[1] for p in pts), max(p[1] for p in pts),
                min(p[2] for p in pts), max(p[2] for p in pts)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("cgpr", help="entity records + decoded transforms")
    p.add_argument("file")
    p.add_argument("--json", metavar="OUT")
    p.set_defaults(func=cmd_cgpr)

    p = sub.add_parser("csml", help="static scene instance summary")
    p.add_argument("file")
    p.add_argument("--json", metavar="OUT")
    p.set_defaults(func=cmd_csml)

    p = sub.add_parser("cwks", help="workspace dependency pool search")
    p.add_argument("file", nargs="?")
    p.add_argument("--match", default="minotaur")
    p.add_argument("--json", metavar="OUT")
    p.set_defaults(func=cmd_cwks)

    p = sub.add_parser("path", help="qath path nodes")
    p.add_argument("file")
    p.set_defaults(func=cmd_path)

    p = sub.add_parser("ground", help="ray-cast a glb at columns (x,z)")
    p.add_argument("glb")
    p.add_argument("columns", nargs="+", metavar="X,Z",
                   type=lambda s: tuple(float(v) for v in s.split(",")))
    p.set_defaults(func=cmd_ground)

    p = sub.add_parser("pick", help="evaluate a placement (ground, yawed AABB, floor range)")
    p.add_argument("glb")
    p.add_argument("--x", type=float, required=True)
    p.add_argument("--z", type=float, required=True)
    p.add_argument("--spawn", type=lambda s: tuple(float(v) for v in s.split(",")),
                   default=(353.0, 345.0))
    p.add_argument("--extents", type=lambda s: tuple(float(v) for v in s.split(",")),
                   default=(-17.481, 17.481, -4.439, 5.367))
    p.add_argument("--main")
    p.add_argument("--shared")
    p.set_defaults(func=cmd_pick)

    p = sub.add_parser("scan", help="tree-wide sweep of a script family for a name")
    p.add_argument("family", choices=["cgpr", "csml"])
    p.add_argument("--pattern", default=r"(mino|minotaur|coloss)")
    p.set_defaults(func=cmd_scan)

    p = sub.add_parser("arena", help="the bounded pull + acceptance table")
    p.set_defaults(func=cmd_arena)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        pass
