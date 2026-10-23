#!/usr/bin/env python3
"""S8 gate (W2, Step 3): the riding clips' root travel tables and the hoof touchdowns.

  root_z.py tables              per-frame root tables read from the `.anb` with
                                tools/re/anb/parse2.decode_root (a baked glb has root
                                x/z zeroed, GlbWriter.cs): `root_z` = Agro's 14 non-loop
                                riding clips, cumulative metres along +Z from frame 0;
                                `rider_root_xz` = Wander's five mount/dismount clips,
                                [x, z] metres per frame in Agro's frame relative to
                                OptBone_boy (the ride clips are authored with it as
                                origin; y comes from the clip itself)
  root_z.py footfalls <agro.glb> the touchdown fractions of walk/trot/gallop
                                (`horse_a_uma_walk`/`run_n`/`run`) from the hoof bones
                                Bone_{F,B}_TUME_{L,R} of a scratch bake

Touchdown (measured on the baked loop, fraction = frame / (frames - 1), frame-exact):
the first frame of the hoof's longest cyclic run of frames whose height above its loop
minimum, `y - min`, is under TOUCH_M (frames - 1 is the loop period, the last frame
repeats the first; the longest run skips a 1-frame re-dip inside a swing). The loop's root y is part of the pose; root x/z are zeroed by the
bake and do not enter the height.

Stdlib only; inputs read-only ($SOTC_GAME_ROOT, default below). Prints a summary;
`--json OUT` writes the tables/fractions.
"""

import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "anb"))
import parse2  # noqa: E402

DEFAULT_GAME_ROOT = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"
ANIM_DIR = "cp3shadow/anim"

# Agro's 19 riding clips (design §6); the loops are excluded from root_z.
AGRO_CLIPS = ["stand1", "walk_start", "walk", "walk_stop", "run_n_start", "run_n", "run_n_stop",
              "run_n_from_walk", "run_start", "run", "run_from_run_n", "run_stop1", "run_stop2",
              "run_stop3", "back_start", "back", "back_stop", "neigh", "neigh_end"]
AGRO_LOOPS = {"stand1", "walk", "run_n", "run", "back"}
NON_LOOP = ["horse_a_uma_" + c for c in AGRO_CLIPS if c not in AGRO_LOOPS]
RIDER_CLIPS = ["player_a_uma_stand_ride_l", "player_a_uma_stand_ride_r",
               "player_a_uma_stand_getoff_l_start", "player_a_uma_stand_getoff_r_start",
               "player_a_uma_stand_getoff_fall"]
GAITS = {"walk": "horse_a_uma_walk", "trot": "horse_a_uma_run_n", "gallop": "horse_a_uma_run"}
HOOVES = ["Bone_F_TUME_L", "Bone_F_TUME_R", "Bone_B_TUME_L", "Bone_B_TUME_R"]
TOUCH_M = 0.05   # touchdown: hoof within 5 cm of its loop minimum


def game_root():
    return os.environ.get("SOTC_GAME_ROOT") or DEFAULT_GAME_ROOT


def anb_root(name, game=None):
    """(frames, fps, [(x, y, z) per root key]) of one clip, read from its `.anb`."""
    path = os.path.join(game or game_root(), ANIM_DIR, name + ".anb")
    data = open(path, "rb").read()
    info = parse2.parse_anb(data, path)
    rate_word = parse2.u16(data, parse2.BLOB_BASE + info["structure"] + 6) >> 4
    fps = 30.0 * (1.0 if rate_word == 0 else rate_word / 4096.0)   # AnbReader.cs's rate field
    values = parse2.decode_root(info)["values"]
    if len(values) != info["frames"]:
        raise ValueError("%s: %d root keys for %d frames" % (name, len(values), info["frames"]))
    return info["frames"], fps, values


def tables(game=None):
    root_z, rider, fps = {}, {}, {}
    for name in NON_LOOP:
        frames, f, v = anb_root(name, game)
        root_z[name] = [round(p[2] - v[0][2], 4) for p in v]
        fps[name] = round(f, 4)
    for name in RIDER_CLIPS:
        frames, f, v = anb_root(name, game)
        rider[name] = [[round(p[0], 4), round(p[2], 4)] for p in v]
        fps[name] = round(f, 4)
    return root_z, rider, fps


# ---------------------------------------------------------------- footfalls (glb FK)

def _qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def _qrot(q, v):
    x, y, z, w = q
    p = _qmul(_qmul(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w))
    return p[:3]


class Glb(object):
    def __init__(self, path):
        d = open(path, "rb").read()
        n = struct.unpack_from("<I", d, 12)[0]
        self.j = json.loads(d[20:20 + n])
        off = 20 + n
        self.bin = d[off + 8:off + 8 + struct.unpack_from("<I", d, off)[0]]
        self.nodes = self.j["nodes"]
        self.parent = {c: i for i, nd in enumerate(self.nodes) for c in nd.get("children", [])}
        self.index = {nd.get("name", ""): i for i, nd in enumerate(self.nodes)}

    def acc(self, i):
        a = self.j["accessors"][i]
        bv = self.j["bufferViews"][a["bufferView"]]
        nc = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a["type"]]
        o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        flat = struct.unpack_from("<%df" % (a["count"] * nc), self.bin, o)
        return [flat[k * nc:(k + 1) * nc] for k in range(a["count"])]

    def clip(self, name):
        anim = next(a for a in self.j["animations"] if a["name"] == name)
        out = {}
        for ch in anim["channels"]:
            sm = anim["samplers"][ch["sampler"]]
            out[(ch["target"]["node"], ch["target"]["path"])] = self.acc(sm["output"])
        return out

    def world_pos(self, clip, node, frame):
        """The node's model-space position at a clip frame (TRS nodes; no scale on the chain)."""
        chain = []
        i = node
        while i is not None:
            chain.append(i)
            i = self.parent.get(i)
        pos, rot = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)
        for i in reversed(chain):
            nd = self.nodes[i]
            if "matrix" in nd:
                raise ValueError("matrix node on a hoof chain: %s" % nd.get("name"))
            t = clip.get((i, "translation"))
            r = clip.get((i, "rotation"))
            t = t[min(frame, len(t) - 1)] if t else nd.get("translation", [0, 0, 0])
            r = r[min(frame, len(r) - 1)] if r else nd.get("rotation", [0, 0, 0, 1])
            pos = tuple(p + q for p, q in zip(pos, _qrot(rot, t)))
            rot = _qmul(rot, tuple(r))
        return pos


def footfalls(glb_path):
    g = Glb(glb_path)
    out = {}
    for gait, name in GAITS.items():
        c = g.clip(name)
        frames = len(next(iter(c.values())))
        period = frames - 1
        per_hoof = {}
        for hoof in HOOVES:
            y = [g.world_pos(c, g.index[hoof], f)[1] for f in range(period)]
            lo = min(y)
            under = [v - lo < TOUCH_M for v in y]
            best = None   # (run length, first frame)
            for f in range(period):
                if under[f] and not under[f - 1]:
                    n = 1
                    while n < period and under[(f + n) % period]:
                        n += 1
                    best = max(best or (0, 0), (n, -f))
            if best is None:
                raise ValueError("%s %s: no touchdown" % (name, hoof))
            touch = [-best[1]]
            per_hoof[hoof] = round((touch[0] / float(period)) % 1.0, 3)
        out[gait] = {"fractions": sorted(per_hoof.values()), "per_hoof": per_hoof,
                     "frames": frames}
    return out


def main(argv):
    args = list(argv)
    js = None
    if "--json" in args:
        k = args.index("--json")
        js = args[k + 1]
        del args[k:k + 2]
    if args[:1] == ["tables"]:
        root_z, rider, fps = tables()
        for n, v in root_z.items():
            print("root_z %-34s frames %3d fps %7.4f  z %8.4f -> %8.4f" % (n, len(v), fps[n], v[0], v[-1]))
        for n, v in rider.items():
            print("rider  %-34s frames %3d fps %7.4f  x %7.4f -> %7.4f  z %7.4f -> %7.4f" % (
                n, len(v), fps[n], v[0][0], v[-1][0], v[0][1], v[-1][1]))
        doc = {"root_z": root_z, "rider_root_xz": rider, "root_fps": fps}
    elif args[:1] == ["footfalls"] and len(args) == 2:
        doc = footfalls(args[1])
        for gait, v in doc.items():
            print("footfalls %-6s frames %2d  %s  %s" % (gait, v["frames"], v["fractions"], v["per_hoof"]))
    else:
        sys.stderr.write(__doc__)
        return 2
    if js:
        with open(js, "w") as fh:
            json.dump(doc, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
