"""Per-bone grip regions of a boss collision: `w & 0x3f0` of every attribute word with bit 3 set (clb findings
headline 7/8: bit 3 = grip is a strong guess, region = the band code), over triangles and swept-sphere prims.
Shape i of a character coli = bone i of the .skb (measured). JSON on stdout: {"<bone>": ["0x10", ...]}, every
shape listed (a bone with no grip face has []).

usage: boss_regions.py <coli.clb> <skel.skb>      (relative paths resolve under $SOTC_GAME_ROOT/cp3shadow)
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clb  # noqa: E402


def skb_names(path):
    """Bone names in order: tag-0x000 tail entries of the .skb are name offsets (tools/re/anb/probe.py)."""
    with open(path, "rb") as f:
        d = f.read()
    B, off, ent = 0x110, len(d) - 8, []
    while off >= 0 and struct.unpack_from("<I", d, off + 4)[0] in (0x102, 0):
        ent.append(struct.unpack_from("<2I", d, off))
        off -= 8
    names = []
    for o, t in reversed(ent):
        if t == 0:
            s = d[B + o:d.index(b"\0", B + o)].decode("latin-1")
            if s.startswith(("Bone_", "OptBone_", "Weak")):
                names.append(s)
    return names


def regions(coli, skb):
    g = lambda r: r if os.path.isabs(r) else os.path.join(clb.GAME, "cp3shadow", r)
    c, names = clb.read(g(coli)), skb_names(g(skb))
    out = {}
    for s in c["shapes"]:
        i = s["index"]
        used = set(t[3] for t in s["tris"]) | set(s["prim_attrs"])
        out[names[i] if i < len(names) else "shape_%d" % i] = sorted(
            set("%#x" % clb.region(c["attrs"][a]) for a in used if c["attrs"][a] & 8))
    return out


if __name__ == "__main__":
    print(json.dumps(regions(sys.argv[1], sys.argv[2]), indent=1, sort_keys=True))
