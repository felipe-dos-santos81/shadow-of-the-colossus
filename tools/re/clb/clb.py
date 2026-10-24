"""Stdlib reader for SOTC (PS4) collision files `.clb` (xff container, root version 5).

Ported from the S13 recon (`.superpowers/sdd/2026-10-23-s13-recon/clb/py/clb.py`; findings.md). All layout facts are
measured on the 1,042 files under cp3shadow, the attribute lookup is binary (FUN_00599f70 hit -> attr idx,
FUN_0059a090 idx -> entry, FUN_00598e30 prims). Offsets are relative to the xff data base
B = u32@0x9c - (u16@0x22 << 16) (normally 0x110); pointers inside the data are B-relative.

  root (B + u32@0x10): +00 u32 version (5)  +04 u32 nshapes  +08 ptr shapes (0x90 B each)  +18 ptr prims (0x40 B,
       attr u32 @+0x3c)  +30 u32 nattr  +34 ptr attr[nattr] (16 B: u64 attribute word, u64 0)
  shape: +00 u32 nverts  +04 ptr verts  +08 u8 quantized  +09 u8 compact_tris  +10 u32 ntris  +14 ptr tris
         +50 u32 nprims  +54 ptr u32[nprims] (indices into root+0x18)
  verts: quantized -> at verts-0x20 {vec4 centre, vec4 half-extent}, then int16 xyz: c + q/32767 * e; else vec4
  tris:  compact (20 B) u16 attr, u16 v0 v1 v2, 3 x u32 edge; float (52 B) u32 v0 v1 v2, 3 x {u32, u32, u32}, u32 attr
Character colis: shape i = skeleton bone i of <stem>.skb (measured).

read() returns {"shapes": [{"verts", "tris": [[v0, v1, v2, attr_idx]], "prim_attrs": [attr_idx]}], "attrs": [u64]}.
"""
import os
import struct

GAME = os.environ.get("SOTC_GAME_ROOT", "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game")


def stage_word(w):
    """(terrain class, owner) of a STAGE attribute word, as the game's masks see it.

    The stored stage words sit one bit low (clb findings headline 3, measured on kirins_hill, poseidons_lake,
    canyon, lakeside and temple; the game's masks are 0x7f<<48 and 0x3c00000): class = (w >> 47) & 0x7f,
    owner = (w << 1) & 0x3c00000. Character/boss words are NOT shifted. Where the shift happens is not found.
    """
    return (w >> 47) & 0x7F, (w << 1) & 0x3C00000


def region(w):
    """Character-word grip region (clb findings headline 7): w & 0x3f0; bit 0 = attached, bit 3 = grip (guess)."""
    return w & 0x3F0


def read(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"xff\0":
        raise ValueError("%s: not an xff container" % path)
    B = struct.unpack_from("<I", d, 0x9C)[0] - (struct.unpack_from("<H", d, 0x22)[0] << 16)
    root = struct.unpack_from("<I", d, 0x10)[0]
    out = {"file": os.path.basename(path), "shapes": [], "attrs": []}
    if root == 0:
        return out                                   # empty container (83 files)
    u = lambda o: struct.unpack_from("<I", d, B + o)[0]
    f4 = lambda o: struct.unpack_from("<4f", d, B + o)
    if u(root) != 5:
        raise ValueError("%s: root version %d" % (path, u(root)))
    na, ap, pp = u(root + 0x30), u(root + 0x34), u(root + 0x18)
    out["attrs"] = [struct.unpack_from("<Q", d, B + ap + 16 * i)[0] for i in range(na)]
    for i in range(u(root + 4)):
        b = u(root + 8) + 0x90 * i
        nv, vp, quant, compact = u(b), u(b + 4), d[B + b + 8], d[B + b + 9]
        nt, tp = u(b + 0x10), u(b + 0x14)
        s = {"index": i, "verts": [], "tris": [], "prim_attrs": []}
        if quant:
            c, e = f4(vp - 0x20), f4(vp - 0x10)
            for j in range(nv):
                q = struct.unpack_from("<3h", d, B + vp + 6 * j)
                s["verts"].append([c[a] + q[a] / 32767.0 * e[a] for a in range(3)])
        else:
            s["verts"] = [list(f4(vp + 16 * j)[:3]) for j in range(nv)]
        for j in range(nt):
            if compact:
                t = struct.unpack_from("<4H", d, B + tp + 20 * j)
                s["tris"].append([t[1], t[2], t[3], t[0]])
            else:
                t = struct.unpack_from("<13I", d, B + tp + 52 * j)
                s["tris"].append([t[0], t[1], t[2], t[12]])
        for t in s["tris"]:
            if max(t[:3]) >= nv or t[3] >= max(na, 1):
                raise ValueError("%s shape %d: bad triangle %r (nv %d, nattr %d)" % (path, i, t, nv, na))
        nn = u(b + 0x50)
        if nn:
            nl = u(b + 0x54)
            for k in struct.unpack_from("<%dI" % nn, d, B + nl):
                a = u(pp + 64 * k + 0x3C)
                if a >= max(na, 1):
                    raise ValueError("%s shape %d: bad prim attr %d" % (path, i, a))
                s["prim_attrs"].append(a)
        out["shapes"].append(s)
    return out
