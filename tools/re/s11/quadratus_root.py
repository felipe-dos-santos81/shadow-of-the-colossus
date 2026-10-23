#!/usr/bin/env python3
"""S11 Task 10b: write app/src/quadratus_root.h, Quadratus's root-motion tables.

  python3 -I tools/re/s11/quadratus_root.py [glb] [quadratus_root.h]

The glb defaults to $SOTC_LIB/models/shadow/characters/colossus_02_bull_a/_cmn/
colossus_02_bull_a.glb (it must carry his S11 bake: out/libviews/s11-quadratus until S11's merge
promotes it); the `.anb` files are read from $SOTC_GAME_ROOT (both read-only). The header names
the glb's sha256.

What the header carries (all measured; recon: .superpowers/sdd/2026-10-23-s10-s11-acceleration/
s11-quadratus-recon/clips.md):
  - root: every `boss_a_*` clip with a root series, per-frame root (x, y, z) relative to
    frame 0 in the clip-start (glb model) frame, read with parse2.decode_root through
    s8/root_z.anb_root. **x is negated** (clips.md §Frames: glb x = -anb x; only so do the
    planted hooves hold still). The bake zeroes root x/z in the glb (GlbWriter.cs) and
    keeps y, which this script checks against the glb's root track. Clips without a
    series are listed as such. `boot` has a series but its absolute y (-83.5 m) is in an
    unresolved frame, so it is listed as unusable, not tabled.
  - turn yaw: the heading change, first -> last frame, of the body axis (the rear hips'
    mid `Bone_MOMO_*` -> the shoulders' mid `Bone_JOWAN_*`) for the four turn clips; app
    convention, + = toward the model's +x = a left turn.
No mesh data: the hoof soles are read from the glb at load (quadratus_motion.h).
The loop set is authored (clips.md §1, "loop (proposed)"). Stdlib only.
"""

import hashlib
import math
import os
import struct
import sys
import json

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "s8"))
import root_z  # noqa: E402  (anb_root, _qmul, _qrot)

GLB_REL = "models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.glb"
PREFIX = "boss_a_"
# Authored (clips.md §1 "loop (proposed)"): the root-carrying clips that wrap.
# attack_{r,l}_loop are 2-frame landing holds that do not close: they clamp, not wrap.
LOOPS = {"angry_1", "back", "down_b_l_loop", "down_b_r_loop",
         "down_f_l_loop", "down_f_r_loop", "search", "stand", "walk"}
UNUSABLE = {"boot": "absolute root y -83.5 m in an unresolved frame"}
TURNS = ["turn_l", "turn_r", "walk_turn_l", "walk_turn_r"]


def f(v):
    return repr(float(round(v, 4))) + "f"


class Glb(object):
    CT = {5126: "f", 5123: "H", 5125: "I", 5121: "B"}
    NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

    def __init__(self, path):
        d = open(path, "rb").read()
        n = struct.unpack_from("<I", d, 12)[0]
        self.j = json.loads(d[20:20 + n])
        off = 20 + n
        self.bin = d[off + 8:off + 8 + struct.unpack_from("<I", d, off)[0]]
        self.nodes = self.j["nodes"]
        self.parent = {c: i for i, nd in enumerate(self.nodes) for c in nd.get("children", [])}
        self.index = {nd.get("name", ""): i for i, nd in enumerate(self.nodes)}
        if not self.j.get("animations"):
            raise SystemExit("%s has no animations: not Quadratus's S11 bake (use the "
                             "out/libviews/s11-quadratus library, or SOTC_LIB pointing at it)" % path)
        self.anims = {a["name"]: a for a in self.j["animations"]}
        self.sha256 = hashlib.sha256(d).hexdigest()

    def acc(self, i):
        a = self.j["accessors"][i]
        bv = self.j["bufferViews"][a["bufferView"]]
        nc = self.NC[a["type"]]
        ct = self.CT[a["componentType"]]
        if bv.get("byteStride") not in (None, nc * struct.calcsize(ct)):
            raise ValueError("interleaved accessor %d" % i)
        o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        flat = struct.unpack_from("<%d%s" % (a["count"] * nc, ct), self.bin, o)
        return [flat[k * nc:(k + 1) * nc] for k in range(a["count"])]

    def clip(self, name):
        anim = self.anims[name]
        out, frames, last_t = {}, 1, 0.0
        for ch in anim["channels"]:
            sm = anim["samplers"][ch["sampler"]]
            v = self.acc(sm["output"])
            out[(ch["target"]["node"], ch["target"]["path"])] = v
            if len(v) > frames:
                frames, last_t = len(v), self.acc(sm["input"])[-1][0]
        return out, frames, last_t

    def world(self, clip, node, frame):
        """(position, rotation) of a TRS-chain node in model space at a clip frame."""
        chain = []
        i = node
        while i is not None:
            chain.append(i)
            i = self.parent.get(i)
        pos, rot = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)
        for i in reversed(chain):
            nd = self.nodes[i]
            if "matrix" in nd:
                raise ValueError("matrix node on a chain: %s" % nd.get("name"))
            t = clip.get((i, "translation"))
            r = clip.get((i, "rotation"))
            t = t[min(frame, len(t) - 1)] if t else nd.get("translation", [0, 0, 0])
            r = r[min(frame, len(r) - 1)] if r else nd.get("rotation", [0, 0, 0, 1])
            pos = tuple(p + q for p, q in zip(pos, root_z._qrot(rot, t)))
            rot = root_z._qmul(rot, tuple(r))
        return pos, rot

def heading_deg(glb, clip, frame):
    def mid(a, b):
        pa = glb.world(clip, glb.index[a], frame)[0]
        pb = glb.world(clip, glb.index[b], frame)[0]
        return [(u + v) * 0.5 for u, v in zip(pa, pb)]
    rear, front = mid("Bone_MOMO_L", "Bone_MOMO_R"), mid("Bone_JOWAN_L", "Bone_JOWAN_R")
    return math.degrees(math.atan2(front[0] - rear[0], front[2] - rear[2]))


def measure(glb_path, game=None):
    glb = Glb(glb_path)
    names = sorted(n[len(PREFIX):] for n in glb.anims if n.startswith(PREFIX))
    root, rootless = {}, []
    for c in names:
        clip, frames, last_t = glb.clip(PREFIX + c)
        try:
            n, fps, v = root_z.anb_root(PREFIX + c, game)
        except ValueError:
            rootless.append(c)
            continue
        if n < 2:
            raise ValueError("%s: a %d-frame root series cannot be sampled" % (c, n))
        if n != frames or (n > 1 and abs((n - 1) / fps - last_t) > 1e-3):
            raise ValueError("%s: anb %d frames @ %.4f vs glb %d frames, %.4f s" % (c, n, fps, frames, last_t))
        gy = clip.get((glb.index["Bone_root"], "translation"))
        if gy and max(abs(a[1] - b[1]) for a, b in zip(gy, v)) > 1e-3:
            raise ValueError("%s: glb root y differs from the anb's" % c)
        if c in UNUSABLE:
            continue
        x0, y0, z0 = v[0]
        root[c] = {"fps": fps, "loop": c in LOOPS,
                   "xyz": [(-(x - x0), y - y0, z - z0) for x, y, z in v]}   # glb x = -anb x
    yaw = {}
    for c in TURNS:
        clip, frames, _ = glb.clip(PREFIX + c)
        yaw[c] = heading_deg(glb, clip, frames - 1) - heading_deg(glb, clip, 0)

    return {"root": root, "rootless": rootless, "yaw": yaw, "glb": os.path.basename(glb_path),
            "sha256": glb.sha256}


def render(d):
    out = []
    w = out.append
    w("#pragma once")
    w("")
    w("// GENERATED by tools/re/s11/quadratus_root.py from Quadratus's baked glb and his `.anb`")
    w("// root series — do not edit; re-run the generator. glb: %s (the S11 bake," % d["glb"])
    w("// out/libviews/s11-quadratus), sha256 %s." % d["sha256"])
    w("// All values measured (clips.md, S11 recon); the loop flags are authored (clips.md §1).")
    w("//")
    w("//   kRoots — every boss_a_* clip with a root series: per-frame root (x, y, z), metres,")
    w("//     relative to frame 0, in the clip-start frame (glb model frame: +z front, +x the")
    w("//     model's left, +y up). x is the NEGATED `.anb` x (glb x = -anb x: only so do the")
    w("//     planted hooves hold still); y and z are raw. y is also in the glb's own root track.")
    w("//     frame = time * fps; the clip's length is (frames - 1) / fps.")
    w("//   kNoRoot — clips with no usable root series (they move nothing).")
    w("//   kTurnYaw — heading change, first -> last frame, of the body axis (rear hips' mid ->")
    w("//     shoulders' mid); + = toward the model's +x = a left turn (app yaw).")
    w("")
    w("namespace quadratus_root {")
    w("")
    w("struct RootSeries {")
    w("    const char* clip;")
    w("    float fps;")
    w("    int frames;")
    w("    bool loop;")
    w("    const float (*xyz)[3];")
    w("};")
    w("")
    for c, r in d["root"].items():
        w("inline constexpr float kXYZ_%s[][3] = {%s};" % (
            c, ", ".join("{%s, %s, %s}" % tuple(f(v) for v in p) for p in r["xyz"])))
    w("")
    w("inline constexpr RootSeries kRoots[] = {")
    for c, r in d["root"].items():
        w('    {"%s%s", %s, %d, %s, kXYZ_%s},' % (PREFIX, c, f(r["fps"]), len(r["xyz"]),
                                               "true" if r["loop"] else "false", c))
    w("};")
    w("")
    w("// No root series in the `.anb` (and `boot`: %s)." % UNUSABLE["boot"])
    w("inline constexpr const char* kNoRoot[] = {%s};" % ", ".join(
        '"%s%s"' % (PREFIX, c) for c in sorted(d["rootless"] + list(UNUSABLE))))
    w("")
    w("struct TurnYaw {")
    w("    const char* clip;")
    w("    float deg;")
    w("};")
    w("")
    w("inline constexpr TurnYaw kTurnYaw[] = {")
    for c, v in d["yaw"].items():
        w('    {"%s%s", %s},' % (PREFIX, c, repr(round(v, 2)) + "f"))
    w("};")
    w("")
    w("}  // namespace quadratus_root")
    return "\n".join(out) + "\n"


def main(argv):
    lib = os.environ.get("SOTC_LIB") or os.path.join(REPO, "out", "library")
    src = argv[1] if len(argv) > 1 else os.path.join(lib, GLB_REL)
    dst = argv[2] if len(argv) > 2 else os.path.join(REPO, "app", "src", "quadratus_root.h")
    if not os.path.isfile(src):
        raise SystemExit("%s: no such glb (export SOTC_LIB=<repo>/out/libviews/s11-quadratus)" % src)
    d = measure(src)
    with open(dst, "w") as fh:
        fh.write(render(d))
    for c, r in d["root"].items():
        x, y, z = r["xyz"][-1]
        print("root %-28s %3d @ %7.4f %s  end (%+8.3f, %+8.3f, %+8.3f)" % (
            c, len(r["xyz"]), r["fps"], "loop" if r["loop"] else "    ", x, y, z))
    print("no root: %s" % ", ".join(sorted(d["rootless"] + list(UNUSABLE))))
    print("turn yaw: %s" % ", ".join("%s %+.2f" % kv for kv in d["yaw"].items()))
    print("wrote %s (glb sha256 %s)" % (dst, d["sha256"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
