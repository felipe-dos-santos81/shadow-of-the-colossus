#!/usr/bin/env python3
"""S8 gate (W2): compose and verify tools/re/s8/placements.json.

  placements.py <reach-dir> <agro-glb> [--out tools/re/s8/placements.json]

<reach-dir> holds the four walks of tools/re/s8/reach.cpp (the app's own heightmap code):

  shrine.bin         reach shrine <lib> out/re/stage/shrine.manifest shrine.bin
  shrine_arrive.bin  reach shrine <lib> out/re/stage/shrine.manifest shrine_arrive.bin <ARRIVE_SHRINE>
  plain.bin          reach plain <lib> <SPAWN_PLAIN> plain.bin <PLAIN_GROUND...>
  plain_arrive.bin   reach plain <lib> <ARRIVE_PLAIN> plain_arrive.bin <PLAIN_GROUND...>

<agro-glb> is a scratch bake of agro.cmsh with --clips horse_a_uma_walk,horse_a_uma_run_n,
horse_a_uma_run (root_z.py footfalls). The script re-measures, checks every placement
against the walks (asserts; exit 1 on a failed check), prints the evidence lines, and
writes the JSON. Authored values are constants below, labelled `source: authored`.

Frames: `shrine` = the cathedral glbs' frame (the S7 shrine manifest); `world` = the plain
(levels/home) frame; world = shrine + SHRINE_TO_WORLD (home.cgpr level_setup). Yaw is the
app's +Y yaw in degrees (0 faces +Z, 90 faces +X).

Stdlib only; reads the library ($SOTC_LIB, default below) and the game tree read-only.
"""

import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import root_z  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DEFAULT_LIB = "/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library"
SHRINE_MANIFEST = os.path.join(REPO, "out", "re", "stage", "shrine.manifest")
SHRINE_TO_WORLD = (300.0, 0.071, -300.0)          # home.cgpr `level_setup` @0xda54
PLAIN_GROUND = ["models/shadow/levels/home/_cmn/" + n + ".glb" for n in
                ("home", "home_ground", "home_cliff_displaced", "home_skirts",
                 "home_plainsgrey_skirt", "home_pond", "home_wall_pillars_01")]

# ---------------------------------------------------------------- the placements
SHRINE_SPAWN = (0.0, 8.998, 1.0)                    # S7 shrine_placements.json (data)
SPAWN_PLAIN = (301.0, -7.14, -210.0)                # cathedral_ext.sgpr/home.sgpr start (data), yaw authored
DOOR_SHRINE = ((-2.5, -0.5, 44.0), (2.5, 3.0, 46.0))        # shrine frame: the walk's +Z end (authored box)
ARRIVE_PLAIN = (301.0, -245.0)                      # world xz, the shrine steps' foot (authored)
DOOR_PLAIN = ((296.0, -2.0, -258.5), (306.0, 2.5, -252.5))  # world: the shrine steps (authored box)
ARRIVE_SHRINE = (0.0, 34.0)                         # shrine xz, the front terrace (authored)
AGRO = (301.0, -203.0)                              # 7 m ahead (+Z) of the spawn (authored)
LINE_HEADING = -54.0                                # the whistle/gallop corridor (authored)
WHISTLE_M = 60.0
GALLOP_M = 300.0
EDGE = ((114.3, -280.5), (76.9, -294.6))            # into the plateau's west cliff (authored)
SPEEDS = {"walk": 1.31, "trot": 5.29, "gallop": 14.7, "back": -1.1, "source": "measured"}
FOOTFALLS = {"walk": [0.25, 0.375, 0.75, 0.875], "trot": [0.0, 0.0, 0.33, 0.89],
             "gallop": [0.11, 0.44, 0.67, 0.95], "source": "measured"}
# The design's per-hoof touchdowns (§6) behind FOOTFALLS, for the per-hoof re-measure diff.
FOOTFALLS_BY_HOOF = {
    "walk": {"Bone_F_TUME_R": 0.25, "Bone_B_TUME_L": 0.375, "Bone_F_TUME_L": 0.75, "Bone_B_TUME_R": 0.875},
    "trot": {"Bone_B_TUME_R": 0.0, "Bone_F_TUME_L": 0.0, "Bone_F_TUME_R": 0.33, "Bone_B_TUME_L": 0.89},
    "gallop": {"Bone_B_TUME_R": 0.11, "Bone_F_TUME_L": 0.44, "Bone_F_TUME_R": 0.67, "Bone_B_TUME_L": 0.95}}
RISE_M, DROP_M = 1.0, 1.5                           # design D3 (refuseHoles)
SLOPE_MAX_DEG = 10.0


def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        raise SystemExit(1)


# ---------------------------------------------------------------- reach dumps
class Grid(object):
    def __init__(self, path, arrays):
        d = open(path, "rb").read()
        self.w, self.h = struct.unpack_from("<ii", d, 0)
        self.x0, self.z0, self.step = struct.unpack_from("<fff", d, 8)
        n = self.w * self.h
        self.a = [struct.unpack_from("<%df" % n, d, 20 + 4 * n * k) for k in range(arrays)]

    def ij(self, x, z):
        return int(round((x - self.x0) / self.step)), int(round((z - self.z0) / self.step))

    def at(self, k, i, j):
        if 0 <= i < self.w and 0 <= j < self.h:
            return self.a[k][j * self.w + i]
        return float("nan")

    def node(self, k, x, z):
        i, j = self.ij(x, z)
        return self.at(k, i, j)

    def bilinear(self, k, x, z):
        fx, fz = (x - self.x0) / self.step, (z - self.z0) / self.step
        i, j = int(math.floor(fx)), int(math.floor(fz))
        q = [self.at(k, i + a, j + b) for b in (0, 1) for a in (0, 1)]
        if any(math.isnan(v) for v in q):
            return self.node(k, x, z)
        u, v = fx - i, fz - j
        return (q[0] * (1 - u) + q[1] * u) * (1 - v) + (q[2] * (1 - u) + q[3] * u) * v

    def slope_deg(self, x, z):
        """The steepest of the 3x3 neighbourhood's central-difference slopes (k = 0 heights)."""
        i, j = self.ij(x, z)
        worst = 0.0
        for b in (-1, 0, 1):
            for a in (-1, 0, 1):
                hx = (self.at(0, i + a + 1, j + b) - self.at(0, i + a - 1, j + b)) / (2 * self.step)
                hz = (self.at(0, i + a, j + b + 1) - self.at(0, i + a, j + b - 1)) / (2 * self.step)
                if math.isnan(hx) or math.isnan(hz):
                    return 90.0
                worst = max(worst, math.degrees(math.atan(math.hypot(hx, hz))))
        return worst

    def box_points(self, k, lo, hi):
        """Reached points inside an XZ box (and its feet inside the box's Y range)."""
        pts = []
        i0, j0 = self.ij(lo[0], lo[2])
        i1, j1 = self.ij(hi[0], hi[2])
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                x, z = self.x0 + i * self.step, self.z0 + j * self.step
                y = self.at(k, i, j)
                if lo[0] <= x <= hi[0] and lo[2] <= z <= hi[2] and lo[1] <= y <= hi[1]:
                    pts.append((x, y, z))
        return pts

    def count(self, k):
        return sum(1 for v in self.a[k] if not math.isnan(v))


def walk_line(g, a, b, step=1.0):
    """Walk a -> b in `step` metres over the plain's node heights with the D3 rule.

    Returns (metres walked, why it stopped or 'ok', worst slope along the walked part)."""
    dist = math.hypot(b[0] - a[0], b[1] - a[1])
    dx, dz = (b[0] - a[0]) / dist, (b[1] - a[1]) / dist
    prev = g.node(0, a[0], a[1])
    worst = g.slope_deg(a[0], a[1])
    n = int(dist / step)
    for k in range(1, n + 1):
        x, z = a[0] + dx * k * step, a[1] + dz * k * step
        y = g.node(0, x, z)
        if math.isnan(y):
            return (k - 1) * step, "hole", worst
        if y - prev > RISE_M:
            return (k - 1) * step, "rise %.2f m" % (y - prev), worst
        if prev - y > DROP_M:
            return (k - 1) * step, "drop %.2f m" % (prev - y), worst
        worst = max(worst, g.slope_deg(x, z))
        prev = y
    return dist, "ok", worst


def ahead(p, heading, m):
    return (round(p[0] + math.sin(math.radians(heading)) * m, 1),
            round(p[1] + math.cos(math.radians(heading)) * m, 1))


def box_dist(p, lo, hi):
    """XZ distance from a point to a box (0 inside)."""
    dx = max(lo[0] - p[0], 0.0, p[0] - hi[0])
    dz = max(lo[2] - p[2], 0.0, p[2] - hi[2])
    return math.hypot(dx, dz)


# ---------------------------------------------------------------- backdrops by bounds
def _mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def _node_local(nd):
    if "matrix" in nd:
        m = nd["matrix"]
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]   # column-major
    x, y, z, w = nd.get("rotation", [0, 0, 0, 1])
    sx, sy, sz = nd.get("scale", [1, 1, 1])
    t = nd.get("translation", [0, 0, 0])
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    s = (sx, sy, sz)
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def glb_bounds(path):
    """World-space AABB of a glb's mesh nodes from the POSITION accessors' min/max."""
    d = open(path, "rb").read()
    if len(d) <= 72:
        return None
    j = json.loads(d[20:20 + struct.unpack_from("<I", d, 12)[0]])
    nodes = j.get("nodes", [])
    parent = {c: i for i, nd in enumerate(nodes) for c in nd.get("children", [])}
    lo, hi, tris = [1e30] * 3, [-1e30] * 3, 0
    for i, nd in enumerate(nodes):
        if "mesh" not in nd:
            continue
        m, k = _node_local(nd), parent.get(i)
        while k is not None:
            m = _mat_mul(_node_local(nodes[k]), m)
            k = parent.get(k)
        for prim in j["meshes"][nd["mesh"]]["primitives"]:
            acc = j["accessors"][prim["attributes"]["POSITION"]]
            a, b = acc["min"], acc["max"]
            for c in [(x, y, z) for x in (a[0], b[0]) for y in (a[1], b[1]) for z in (a[2], b[2])]:
                w = [sum(m[r][q] * (c + (1,))[q] for q in range(4)) for r in range(3)]
                lo = [min(u, v) for u, v in zip(lo, w)]
                hi = [max(u, v) for u, v in zip(hi, w)]
            if "indices" in prim:
                tris += j["accessors"][prim["indices"]]["count"] // 3
    return lo, hi, tris


def backdrops(lib):
    """The shrine's level glbs whose bounds leave the hall (cathedral_interior.glb's box).

    The plain surrounds the shrine (its ground has a hole under the shrine's footprint), so
    a piece with geometry outside the hall faces the plain; the `cathedral_int*` pieces are
    the interior's own (the stair shaft reaches below the hall floor, not into view)."""
    glbs = [ln.split()[1] for ln in open(SHRINE_MANIFEST)
            if ln.startswith("instance ") and "/levels/cathedral/" in ln]
    hall = glb_bounds(os.path.join(lib, "models/shadow/levels/cathedral/_cmn/cathedral_interior.glb"))
    keep = []
    for g in glbs:
        name = os.path.basename(g)[:-4]
        lo, hi, tris = glb_bounds(os.path.join(lib, g))
        inside = all(hall[0][k] <= lo[k] and hi[k] <= hall[1][k] for k in range(3))
        chosen = not inside and not name.startswith("cathedral_int")
        print("%s %-40s x[%7.1f, %7.1f] y[%6.1f, %6.1f] z[%7.1f, %6.1f] tris %7d" % (
            "back" if chosen else "    ", name, lo[0] + SHRINE_TO_WORLD[0], hi[0] + SHRINE_TO_WORLD[0],
            lo[1] + SHRINE_TO_WORLD[1], hi[1] + SHRINE_TO_WORLD[1], lo[2] + SHRINE_TO_WORLD[2],
            hi[2] + SHRINE_TO_WORLD[2], tris))
        if chosen:
            keep.append(g)
    return glbs, keep


# ---------------------------------------------------------------- compose
def main(argv):
    args = list(argv)
    out = os.path.join(HERE, "placements.json")
    if "--out" in args:
        k = args.index("--out")
        out = args[k + 1]
        del args[k:k + 2]
    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 2
    rdir, agro_glb = args
    lib = os.environ.get("SOTC_LIB") or DEFAULT_LIB

    shrine = Grid(os.path.join(rdir, "shrine.bin"), 1)
    shrine_back = Grid(os.path.join(rdir, "shrine_arrive.bin"), 1)
    plain = Grid(os.path.join(rdir, "plain.bin"), 2)
    plain_back = Grid(os.path.join(rdir, "plain_arrive.bin"), 2)

    # R1 — the shrine door: the S7 walk's +Z end.
    zmax = max(shrine.z0 + j * shrine.step for j in range(shrine.h)
               if any(not math.isnan(shrine.at(0, i, j)) for i in range(shrine.w)))
    pts = shrine.box_points(0, *DOOR_SHRINE)
    lo, hi = DOOR_SHRINE
    check(len(pts) > 0, "door_shrine: %d reached 0.25 m points inside the box (walk's +Z end z %.2f, feet %.2f..%.2f)"
          % (len(pts), zmax, min(p[1] for p in pts), max(p[1] for p in pts)))
    check(lo[2] <= zmax <= hi[2], "door_shrine: the box spans the walk's farthest +Z (%.2f)" % zmax)
    d_spawn = box_dist(SHRINE_SPAWN, lo, hi)
    check(d_spawn >= 5.0, "door_shrine: %.2f m from the shrine spawn (>= 5, not overlapping)" % d_spawn)
    ys = shrine_back.node(0, ARRIVE_SHRINE[0], ARRIVE_SHRINE[1])
    arrive_shrine = (ARRIVE_SHRINE[0], round(ys, 3), ARRIVE_SHRINE[1])
    check(not math.isnan(shrine.node(0, ARRIVE_SHRINE[0], ARRIVE_SHRINE[1])),
          "arrive_shrine %s: reached by the walk from the shrine spawn" % (arrive_shrine,))
    back_pts = shrine_back.box_points(0, *DOOR_SHRINE)
    check(len(back_pts) > 0 and not math.isnan(shrine_back.node(0, SHRINE_SPAWN[0], SHRINE_SPAWN[2])),
          "arrive_shrine: its own walk reaches door_shrine (%d points) and the shrine spawn (%d points walked)"
          % (len(back_pts), shrine_back.count(0)))
    d_arr = box_dist(arrive_shrine, lo, hi)
    check(d_arr >= 3.0, "arrive_shrine: %.2f m from door_shrine (>= 3)" % d_arr)

    # R1 — the plain door on the shrine steps.
    plo, phi = DOOR_PLAIN
    ppts = plain.box_points(1, plo, phi)
    check(len(ppts) > 0, "door_plain: %d reached 1 m nodes inside the box from spawn_plain (ground %.2f..%.2f)"
          % (len(ppts), min(p[1] for p in ppts), max(p[1] for p in ppts)))
    yp = plain.bilinear(0, ARRIVE_PLAIN[0], ARRIVE_PLAIN[1])
    arrive_plain = (ARRIVE_PLAIN[0], round(yp, 3), ARRIVE_PLAIN[1])
    check(not math.isnan(plain.node(1, *ARRIVE_PLAIN)), "arrive_plain %s: reached from spawn_plain" % (arrive_plain,))
    check(box_dist(arrive_plain, plo, phi) > 0,
          "arrive_plain: outside door_plain (%.2f m)" % box_dist(arrive_plain, plo, phi))
    check(len(plain_back.box_points(1, plo, phi)) > 0 and not math.isnan(plain_back.node(1, SPAWN_PLAIN[0], SPAWN_PLAIN[2])),
          "arrive_plain: its own walk reaches door_plain and spawn_plain (%d nodes walked; from the spawn %d)"
          % (plain_back.count(1), plain.count(1)))
    check(box_dist(SPAWN_PLAIN, plo, phi) > 0, "spawn_plain: outside door_plain (%.2f m)" % box_dist(SPAWN_PLAIN, plo, phi))

    # Step 2 — Agro and the probes (the plain's unlayered ground, D3 rule).
    ya = plain.bilinear(0, *AGRO)
    sa = plain.slope_deg(*AGRO)
    check(not math.isnan(plain.node(1, *AGRO)) and sa <= SLOPE_MAX_DEG,
          "agro (%.1f, %.3f, %.1f): reached, %.1f m ahead of the spawn, slope %.2f deg (<= 10)"
          % (AGRO[0], ya, AGRO[1], math.hypot(AGRO[0] - SPAWN_PLAIN[0], AGRO[1] - SPAWN_PLAIN[2]), sa))
    whistle_from = ahead(AGRO, LINE_HEADING, WHISTLE_M)
    for a, b, what in ((AGRO, whistle_from, "agro -> whistle_from"), (whistle_from, AGRO, "whistle_from -> agro")):
        m, why, worst = walk_line(plain, a, b)
        check(why == "ok", "%s: %.1f m straight, %s, worst slope %.2f deg" % (what, m, why, worst))
    gallop_to = ahead(AGRO, LINE_HEADING, GALLOP_M)
    m, why, worst = walk_line(plain, AGRO, gallop_to)
    check(why == "ok" and worst < SLOPE_MAX_DEG,
          "gallop_line %s heading %.0f: %.0f m, %s, worst slope %.2f deg (< 10)" % (AGRO, LINE_HEADING, m, why, worst))
    fan = [LINE_HEADING + d for d in (-4, -2, 2, 4)]
    fan_worst = [walk_line(plain, AGRO, ahead(AGRO, h, GALLOP_M))[2] for h in fan]
    print("info gallop corridor: headings %s worst slopes %s" % (fan, ["%.2f" % w for w in fan_worst]))
    m, why, worst = walk_line(plain, EDGE[0], EDGE[1])
    total = math.hypot(EDGE[1][0] - EDGE[0][0], EDGE[1][1] - EDGE[0][1])
    check(not math.isnan(plain.node(1, *EDGE[0])) and why != "ok" and why.startswith(("drop", "hole")) and m >= 15.0,
          "edge %s -> %s: walkable %.0f m of %.1f, then refused (%s)" % (EDGE[0], EDGE[1], m, total, why))

    # Backdrops by bounds.
    shrine_glbs, keep = backdrops(lib)
    print("info backdrops: %d of the shrine's %d level glbs" % (len(keep), len(shrine_glbs)))

    # Step 3 — the root tables and the footfalls.
    rz, rider, fps = root_z.tables()
    check(sorted(rz) == sorted(root_z.NON_LOOP) and len(rz) == 14, "root_z: %d non-loop clips" % len(rz))
    check(sorted(rider) == sorted(root_z.RIDER_CLIPS), "rider_root_xz: %d clips" % len(rider))
    ff = root_z.footfalls(agro_glb)
    for gait in ("walk", "trot", "gallop"):
        per = ff[gait]["per_hoof"]
        diffs = {h: round(min(abs(per[h] - v), 1 - abs(per[h] - v)), 3) for h, v in FOOTFALLS_BY_HOOF[gait].items()}
        print("info footfalls %-6s design %s re-measured %s per-hoof cyclic diff %s (1 frame = %.3f)" % (
            gait, FOOTFALLS[gait], ff[gait]["fractions"], diffs, 1.0 / (ff[gait]["frames"] - 1)))

    doc = {
        "agro": {"p": [AGRO[0], round(ya, 3), AGRO[1]], "yaw_deg": 180.0, "source": "authored",
                 "slope_deg": round(sa, 2), "note": "7 m ahead (+Z) of spawn_plain, facing it; y = the ground"},
        "spawn_plain": {"p": list(SPAWN_PLAIN), "yaw_deg": 0, "source": "data|authored",
                        "p_source": "data", "yaw_source": "authored"},
        "door_shrine": {"frame": "shrine", "min": list(DOOR_SHRINE[0]), "max": list(DOOR_SHRINE[1]),
                        "arrive_plain": {"p": list(arrive_plain), "yaw_deg": 0.0}, "source": "authored",
                        "reach": {"walk": "S7 layered shrine map, 0.25 m lattice from the shrine spawn",
                                  "points_inside": len(pts), "walk_zmax": round(zmax, 2),
                                  "spawn_dist_m": round(d_spawn, 2)}},
        "door_plain": {"frame": "world", "min": list(DOOR_PLAIN[0]), "max": list(DOOR_PLAIN[1]),
                       "arrive_shrine": {"p": list(arrive_shrine), "yaw_deg": 180.0}, "source": "authored",
                       "reach": {"walk": "plain unlayered 1 m map (levels/home ground, refuseHoles) from spawn_plain",
                                 "nodes_inside": len(ppts), "arrive_shrine_to_door_shrine_m": round(d_arr, 2)}},
        "probes": {"whistle_from": list(whistle_from),
                   "gallop_line": {"from": list(AGRO), "heading_deg": LINE_HEADING, "len_m": GALLOP_M},
                   "edge": {"from": list(EDGE[0]), "to": list(EDGE[1])}, "source": "authored"},
        "speeds": SPEEDS,
        "backdrops": keep,
        "root_z": rz,
        "rider_root_xz": rider,
        "root_fps": fps,
        "footfalls": FOOTFALLS,
        "footfalls_remeasured": {g: ff[g]["fractions"] for g in ("walk", "trot", "gallop")},
        "frames": {"shrine": "cathedral glbs' frame", "world": "levels/home frame",
                   "shrine_to_world": list(SHRINE_TO_WORLD)},
    }
    with open(out, "w") as fh:
        json.dump(doc, fh, indent=1)
        fh.write("\n")
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
