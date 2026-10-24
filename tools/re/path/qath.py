"""Parser for the SOTC (PS4) `qath` route files (cp3shadow/path/**/*.path); ported from the recon parser
(.superpowers/sdd/2026-10-23-s13-recon/path/py/qath.py, stdlib only). Positions are returned as stored (game frame).

Layout (binary: loader FUN_0068a1f0, accessors FUN_00689960/6899c0/689a30/689aa0): header 0x30 = 'qath', u32 size,
section offsets +08 nodes, +0c links, +10 routes, +14 gps, +18 points, u32 stage id +1c. Every section is
`u32 count, u32 bytes, 8 pad` then 0x30-byte records from +0x10; u16 index lists sit at section-relative offsets.
Sections S5-S7 (+20/+24/+28) are not decoded and are not read.
"""
import struct

SECTIONS = {"nodes": 0x08, "links": 0x0C, "routes": 0x10, "gps": 0x14, "points": 0x18}


def parse(b):
    if b[:4] != b"qath":
        raise ValueError("not qath")
    size, stage_id = struct.unpack_from("<I", b, 4)[0], struct.unpack_from("<I", b, 0x1C)[0]
    if size != len(b):
        raise ValueError("size %d != file %d" % (size, len(b)))

    def section(name):
        off = struct.unpack_from("<I", b, SECTIONS[name])[0]
        if off == 0:
            return 0, 0
        n, sz = struct.unpack_from("<II", b, off)
        assert off + sz <= size, (name, off, sz)
        return off, n

    def u16list(base, n, rel):
        return list(struct.unpack_from("<%dH" % n, b, base + rel)) if n else []

    out = {"stage_id": stage_id}
    # node: xyz, w, u32 nlinks, u32 list off, u32 class bits (+18), u16 tag (0xffff = none), ...
    off, n = section("nodes")
    out["nodes"] = []
    for i in range(n):
        x, y, z, _, cnt, lo, cls, tag = struct.unpack_from("<4f3IH", b, off + 0x10 + i * 0x30)
        out["nodes"].append({"i": i, "pos": [x, y, z], "class": cls, "tag": -1 if tag == 0xFFFF else tag,
                             "links": u16list(off, cnt, lo)})
    # link: u16 a, u16 b, u32 flags default (+04), u32 flags live (+08), u16 x2 (+0c), i16 tags x2 (+10), u32, f32 length (+18)
    off, n = section("links")
    out["links"] = []
    for i in range(n):
        a, bb, _, live, _, _, t0, t1, _, ln = struct.unpack_from("<2H2I2H2hIf", b, off + 0x10 + i * 0x30)
        out["links"].append({"i": i, "a": a, "b": bb, "flags": live, "tags": [t0, t1], "length": ln})
    # route: xyz, w, i32 id, u32 ngp, u32 list off -> gp indices;  gp: xyz, w, i32 tag, u32 npoint, u32 list off
    for name in ("routes", "gps"):
        off, n = section(name)
        out[name] = []
        for i in range(n):
            x, y, z, _, key, cnt, lo = struct.unpack_from("<4fiII", b, off + 0x10 + i * 0x30)
            out[name].append({"i": i, "pos": [x, y, z], "key": key, "list": u16list(off, cnt, lo)})
    # point: xyz, w, u32 flags (bit0 AP, bit1 PP), f32 angle degrees, i32 owning gp
    off, n = section("points")
    out["points"] = []
    for i in range(n):
        x, y, z, _, fl, ang, gp = struct.unpack_from("<4fIfi", b, off + 0x10 + i * 0x30)
        out["points"].append({"i": i, "pos": [x, y, z], "ap": bool(fl & 1), "pp": bool(fl & 2), "angle": ang, "gp": gp})
    validate(out)
    return out


def validate(o):
    nn, nl, ng, npt = len(o["nodes"]), len(o["links"]), len(o["gps"]), len(o["points"])
    for nd in o["nodes"]:
        for li in nd["links"]:
            assert li < nl and nd["i"] in (o["links"][li]["a"], o["links"][li]["b"]), ("node", nd["i"], li)
    for lk in o["links"]:
        assert lk["a"] < nn and lk["b"] < nn, ("link", lk)
    for r in o["routes"]:
        assert all(g < ng for g in r["list"]), ("route", r)
    for g in o["gps"]:
        assert all(p < npt for p in g["list"]), ("gp", g)
    for p in o["points"]:
        assert p["gp"] == -1 or p["gp"] < ng, ("point", p)
